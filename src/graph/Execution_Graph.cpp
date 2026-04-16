#include "dgtl/graph/Execution_Graph.hpp"

#include <algorithm>
#include <unordered_map>
#include "dgtl/analysis/polyhedral/Scop_Detector.hpp"

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

} // namespace dgtl::graph

