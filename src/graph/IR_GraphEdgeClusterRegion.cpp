/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "dgtl/analysis/Polyhedral_Scop_Unified.hpp"

namespace {

inline bool is_valid_vreg(uint32_t id) {
    return id != dgtl::ir::INVALID_SLOT && id != 0;
}

inline bool is_memory_heavy(const dgtl::ir::IRNode& node) {
    return node.opcode == dgtl::ir::OpCode::LOAD ||
           node.opcode == dgtl::ir::OpCode::STORE ||
           node.opcode == dgtl::ir::OpCode::GATHER_F32_512 ||
           node.opcode == dgtl::ir::OpCode::STORE_V512;
}

} // namespace

namespace dgtl::graph {

void ExecutionGraph::build_from_ir(dgtl::ir::IRStream& global_ir) {
    regions.clear();

    constexpr size_t kRegionSoftLimit = 64;
    auto region = std::make_unique<Region>();

    auto flush_region = [&]() {
        if (region->nodes.empty()) {
            return;
        }
        regions.push_back(std::move(region));
        region = std::make_unique<Region>();
    };

    for (size_t i = 0; i < global_ir.size(); ++i) {
        const auto node = global_ir.get_node(i);
        region->add_node(node);

        if (is_memory_heavy(node)) {
            region->memory_bound = true;
        } else {
            region->compute_bound = true;
        }
        region->vectorizable = region->vectorizable || (node.flags & dgtl::ir::Flags::IS_SIMD_BURST);

        if (node.is_terminator() || region->nodes.size() >= kRegionSoftLimit) {
            flush_region();
        }
    }

    flush_region();

    // Build producer map: result_id -> owning region.
    std::unordered_map<uint32_t, Region*> producer_region;
    producer_region.reserve(global_ir.size());
    for (auto& region_ptr : regions) {
        Region* r = region_ptr.get();
        for (const auto& node : r->nodes) {
            if (is_valid_vreg(node.result_id) && !producer_region.contains(node.result_id)) {
                producer_region[node.result_id] = r;
            }
        }
    }

    // Add inter-region dependencies from operand use-sites.
    for (auto& region_ptr : regions) {
        Region* consumer = region_ptr.get();
        for (const auto& node : consumer->nodes) {
            const uint32_t ops[3] = {node.op_a, node.op_b, node.op_c};
            for (uint32_t op : ops) {
                if (!is_valid_vreg(op)) {
                    continue;
                }
                auto it = producer_region.find(op);
                if (it != producer_region.end() && it->second != consumer) {
                    consumer->add_dependency(it->second);
                }
            }
        }

        // Cheap region-level telemetry used by scheduling heuristics.
        consumer->estimated_cycles = static_cast<double>(consumer->nodes.size()) * 1.5;
        consumer->cache_miss_rate = consumer->memory_bound ? 0.12 : 0.04;
        consumer->is_hot = consumer->estimated_cycles > 48.0;
        consumer->speculative_candidate = consumer->vectorizable || consumer->memory_bound;
    }

    // === P0-002: SCoP DETECTION AND REGION ANNOTATION ===
    // Detect affine Static-Control Polyhedra for polyhedral parallelization.
    analysis::polyhedral::ScopDetector detector;
    for (auto& region_ptr : regions) {
        if (region_ptr) {
            // Analyze region for affine SCoP classification
            auto scop_meta = detector.analyze_region(*region_ptr);
            
            // Map ScopMetadata classification to Region ScopAnnotation
            if (scop_meta.classification == analysis::polyhedral::ScopClassification::AFFINE_CANDIATE) {
                region_ptr->scop_annotation.affinity = ScopAffinity::AFFINE_CANDIDATE;
            } else {
                region_ptr->scop_annotation.affinity = ScopAffinity::NON_AFFINE;
            }
            
            region_ptr->scop_annotation.loop_depth = scop_meta.loop_depth;
            region_ptr->scop_annotation.can_tile = scop_meta.can_tile;
            region_ptr->scop_annotation.can_fuse = scop_meta.can_fuse;
            region_ptr->scop_annotation.can_parallelize = scop_meta.can_parallelize;
            region_ptr->scop_annotation.confidence_score = scop_meta.confidence_score;
            region_ptr->scop_annotation.rejection_reason = scop_meta.rejection_reason;
        }
    }
}

void ExecutionGraph::schedule(const dgtl::core::TargetMachine& target) {
    const bool vector_bias = target.prefers_vector_heavy_schedule();
    const float mem_bias = target.memory_latency_bias();

    auto better = [vector_bias, mem_bias](const Region* a, const Region* b) {
        if (!a || !b) {
            return a != nullptr;
        }

        const double a_pressure = a->live_range_pressure();
        const double b_pressure = b->live_range_pressure();
        if (a_pressure != b_pressure) {
            return a_pressure < b_pressure;
        }

        const double a_mem = a->memory_density() * static_cast<double>(mem_bias);
        const double b_mem = b->memory_density() * static_cast<double>(mem_bias);
        if (a_mem != b_mem) {
            return a_mem < b_mem;
        }

        if (vector_bias) {
            const double a_simd = a->simd_density();
            const double b_simd = b->simd_density();
            if (a_simd != b_simd) {
                return a_simd > b_simd;
            }
        }

        if (a->dependencies.size() != b->dependencies.size()) {
            return a->dependencies.size() < b->dependencies.size();
        }
        if (a->is_hot != b->is_hot) {
            return a->is_hot;
        }
        if (a->vectorizable != b->vectorizable) {
            return a->vectorizable;
        }
        return a->estimated_cycles > b->estimated_cycles;
    };

    std::unordered_map<Region*, size_t> indegree;
    indegree.reserve(regions.size());
    for (const auto& r : regions) {
        indegree[r.get()] = 0;
    }

    for (const auto& r : regions) {
        for (Region* dep : r->dependencies) {
            if (dep && indegree.contains(r.get())) {
                indegree[r.get()] += 1;
            }
        }
    }

    std::vector<Region*> ready;
    ready.reserve(regions.size());
    for (const auto& r : regions) {
        if (indegree[r.get()] == 0) {
            ready.push_back(r.get());
        }
    }

    std::vector<std::unique_ptr<Region>> ordered;
    ordered.reserve(regions.size());

    auto take_best_ready = [&]() -> Region* {
        if (ready.empty()) return nullptr;
        auto it = std::max_element(ready.begin(), ready.end(), [&](Region* lhs, Region* rhs) {
            return !better(lhs, rhs);
        });
        Region* best = *it;
        ready.erase(it);
        return best;
    };

    while (!ready.empty()) {
        Region* chosen = take_best_ready();
        if (!chosen) {
            break;
        }

        auto pos = std::find_if(regions.begin(), regions.end(), [chosen](const auto& ptr) {
            return ptr.get() == chosen;
        });
        if (pos == regions.end()) {
            continue;
        }

        ordered.push_back(std::move(*pos));
        regions.erase(pos);

        for (Region* dependent : chosen->dependents) {
            if (!dependent || !indegree.contains(dependent)) {
                continue;
            }
            if (indegree[dependent] > 0) {
                indegree[dependent] -= 1;
                if (indegree[dependent] == 0) {
                    ready.push_back(dependent);
                }
            }
        }
    }

    // Cycle fallback: keep any remaining regions in prior relative order.
    for (auto& leftover : regions) {
        ordered.push_back(std::move(leftover));
    }
    regions = std::move(ordered);
}

std::vector<std::vector<Region*>> ExecutionGraph::find_fusion_candidates() const {
    std::vector<std::vector<Region*>> groups;
    if (regions.size() < 2) {
        return groups;
    }

    for (size_t i = 0; i + 1 < regions.size(); ++i) {
        Region* a = regions[i].get();
        Region* b = regions[i + 1].get();

        if (!a || !b) {
            continue;
        }
        if (!a->can_fuse_with(*b)) {
            continue;
        }

        // Encourage short-range local fusions where there is direct flow.
        bool flow_connected = b->dependents.contains(a) || a->dependents.contains(b);
        flow_connected = flow_connected || std::find(b->dependencies.begin(), b->dependencies.end(), a) != b->dependencies.end();

        if (flow_connected || (a->vectorizable && b->vectorizable)) {
            groups.push_back({a, b});
        }
    }

    return groups;
}

bool ExecutionGraph::validate() const {
    for (const auto& region_ptr : regions) {
        const Region* region = region_ptr.get();
        if (!region || !region->validate()) {
            return false;
        }

        for (Region* dep : region->dependencies) {
            if (!dep || dep == region) {
                return false;
            }
        }
    }

    return true;
}

void Region::add_node(const dgtl::ir::IRNode& node) {
    nodes.push_back(node);
}

void Region::add_dependency(Region* dep) {
    if (!dep || dep == this) return;

    // Check for existing dependency to prevent duplicates in the stream
    for (Region* existing : dependencies) {
        if (existing == dep) return;
    }

    dependencies.push_back(dep);
    dep->dependents.insert(this);
}

bool Region::can_fuse_with(const Region& other) const {
    if (this == &other) return false;
    if (nodes.empty() || other.nodes.empty()) return false;

    // BIR 1.0.0 Optimization: Keep fusions bounded for L1-instruction cache locality.
    // Over-fusing triggers spills in the register allocator.
    constexpr size_t kMaxFusedNodes = 128;
    if (nodes.size() + other.nodes.size() > kMaxFusedNodes) {
        return false;
    }

    return true;
}

std::unordered_set<uint32_t> Region::get_live_ins() const {
    std::unordered_set<uint32_t> produced;
    std::unordered_set<uint32_t> live_ins;

    produced.reserve(nodes.size());
    live_ins.reserve(nodes.size());

    for (const auto& node : nodes) {
        // If an operand is used before it is produced in this region, it's a live-in
        if (is_valid_vreg(node.op_a) && !produced.contains(node.op_a)) live_ins.insert(node.op_a);
        if (is_valid_vreg(node.op_b) && !produced.contains(node.op_b)) live_ins.insert(node.op_b);
        if (is_valid_vreg(node.op_c) && !produced.contains(node.op_c)) live_ins.insert(node.op_c);

        if (is_valid_vreg(node.result_id)) {
            produced.insert(node.result_id);
        }
    }

    return live_ins;
}

std::unordered_set<uint32_t> Region::get_live_outs() const {
    std::unordered_set<uint32_t> produced;
    std::unordered_set<uint32_t> used_internally;

    produced.reserve(nodes.size());
    used_internally.reserve(nodes.size());

    for (const auto& node : nodes) {
        if (is_valid_vreg(node.result_id)) produced.insert(node.result_id);
        if (is_valid_vreg(node.op_a)) used_internally.insert(node.op_a);
        if (is_valid_vreg(node.op_b)) used_internally.insert(node.op_b);
        if (is_valid_vreg(node.op_c)) used_internally.insert(node.op_c);
    }

    // Simplified logic: for now, assume all produced values are potential live-outs
    // unless they are strictly transient (to be refined by global DFA).
    return produced;
}

bool Region::validate() const {
    for (const auto& node : nodes) {
        if (node.opcode == dgtl::ir::OpCode::NOP) continue;

        // Ensure node is "useful": either has side effects, produces a value, or controls flow.
        const bool has_side_effects = node.has_side_effects();
        const bool produces_value = is_valid_vreg(node.result_id);

        if (!has_side_effects && !produces_value && !node.is_terminator()) {
            return false;
        }
    }
    return true;
}

size_t Region::node_count() const noexcept {
    return nodes.size();
}

double Region::simd_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t simd = 0;
    for (const auto& node : nodes) {
        // Checking for AVX-512 vector opcodes and SIMD burst flags
        if ((node.flags & dgtl::ir::Flags::IS_SIMD_BURST) != 0 ||
            static_cast<uint16_t>(node.opcode) >= static_cast<uint16_t>(dgtl::ir::OpCode::VEC_ADD_F32_512)) {
            ++simd;
        }
    }
    return static_cast<double>(simd) / static_cast<double>(nodes.size());
}

double Region::memory_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t mem = 0;
    for (const auto& node : nodes) {
        if (node.opcode == dgtl::ir::OpCode::LOAD || node.opcode == dgtl::ir::OpCode::STORE) {
            ++mem;
        }
    }
    return static_cast<double>(mem) / static_cast<double>(nodes.size());
}

double Region::control_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t ctrl = 0;
    for (const auto& node : nodes) {
        // is_terminator handles RET/BRANCH/GOTO
        if (node.is_terminator() || node.opcode == dgtl::ir::OpCode::CALL) {
            ++ctrl;
        }
    }
    return static_cast<double>(ctrl) / static_cast<double>(nodes.size());
}

double Region::live_range_pressure() const {
    if (nodes.empty()) return 0.0;
    const auto ins = get_live_ins();
    const auto outs = get_live_outs();
    return static_cast<double>(ins.size() + outs.size()) / static_cast<double>(nodes.size());
}

} // namespace dgtl::graph

