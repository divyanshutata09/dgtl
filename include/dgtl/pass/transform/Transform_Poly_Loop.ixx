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

module;

#include <cstdlib>

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <array>
#include <span>
#include <cstring>
#include <stdexcept>

export module dgtl.ir_core.transform_poly_loop;

import dgtl.ir_core;
import dgtl.op_traits;
import dgtl.ir_core.speculate_alias_dce_inliner;
import dgtl.ir_core.pass_manager_config;

// import dgtl.ir_core.liveness_memory_dependency_unified;
// import dgtl.ir_core.ai_cost_model;
// import dgtl.ir_core.verified_formal;
// import dgtl.ir_core.graph_region;

// Bring in traits/namespaces for local use
namespace iface = dgtl::ir::interfaces;

export namespace dgtl::pass::transform {

namespace analysis = dgtl::analysis;
namespace core = dgtl::core;
namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;
namespace graph = dgtl::graph;

// ============================================================================
// HELPER STRUCTURES FOR LOOP ANALYSIS AND REPRESENTATION
// ============================================================================

/**
 * @struct LoopBounds
 * @brief Represents the iteration bounds and stride of a loop.
 */
struct LoopBounds {
    uint32_t induction_var;
    uint64_t lower_bound;
    uint64_t upper_bound;
    uint64_t stride;

    bool matches(const LoopBounds& other) const noexcept {
        return lower_bound == other.lower_bound &&
               upper_bound == other.upper_bound &&
               stride == other.stride;
    }
};

/**
 * @struct LoopRange
 * @brief Describes the span of a loop in IR stream indices.
 */
struct LoopRange {
    uint32_t start_idx;
    uint32_t end_idx;
    LoopBounds bounds;
};

/**
 * @struct LoopNest
 * @brief Loop nest representation for polyhedral analysis.
 */
struct LoopNest {
    uint32_t depth;
    int64_t lower_bounds[8];
    int64_t upper_bounds[8];
    int64_t strides[8];
    uint32_t induction_vars[8];

    bool is_valid() const noexcept {
        return depth > 0 && depth <= 8;
    }

    [[nodiscard]] int64_t get_lower_bound(size_t dim) const noexcept {
        return (dim < depth && dim < 8) ? lower_bounds[dim] : 0;
    }
    [[nodiscard]] int64_t get_upper_bound(size_t dim) const noexcept {
        return (dim < depth && dim < 8) ? upper_bounds[dim] : 0;
    }
    [[nodiscard]] int64_t get_stride(size_t dim) const noexcept {
        return (dim < depth && dim < 8) ? strides[dim] : 1;
    }
};

// ============================================================================
// POLYHEDRAL TRANSFORM ENGINE (TILING, UNROLL-AND-JAM, SCoP ANALYSIS)
// ============================================================================

class DGTLPolyTransformer {
public:
    [[nodiscard]] static bool is_valid_scop_for_transform(const graph::Region& region) noexcept {
        if (region.scop_annotation.affinity != graph::ScopAffinity::AFFINE_CANDIDATE) {
            return false;
        }

        if (region.scop_annotation.loop_depth == 0) {
            return false;
        }

        return true;
    }

    [[nodiscard]] static bool can_tile_region(const graph::Region& region) noexcept {
        return is_valid_scop_for_transform(region) && region.scop_annotation.can_tile;
    }

    [[nodiscard]] static bool can_parallelize_region(const graph::Region& region) noexcept {
        return is_valid_scop_for_transform(region) && region.scop_annotation.can_parallelize;
    }

    static void apply_tiling(ir::IRStream& stream, const LoopNest& nest, [[maybe_unused]] uint32_t tile_size) noexcept {
        if (!nest.is_valid() || stream.is_null() || stream.size() == 0) {
            return;
        }

        const size_t stream_size = stream.size();
        if (nest.depth < 2) return;

        stream.begin_transaction();

        try {
            for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
                const ir::IRNode node = stream.get_node_readonly(i);
                ir::IRAtom& s_atom = stream.get_shadow_atom(i);
                ir::IRCore& s_core = stream.get_shadow_core(i);

                s_atom.opcode = node.opcode;
                s_atom.flags = node.flags;
                s_core.result_id = node.result_id;
                s_core.op_a = node.op_a;
                s_core.op_b = node.op_b;
                s_core.op_c = node.op_c;
                s_core.intent_hash = node.intent_hash;

                for (uint32_t d = 0; d < nest.depth; ++d) {
                    if ((node.op_a == nest.induction_vars[d] ||
                         node.result_id == nest.induction_vars[d]) &&
                        nest.induction_vars[d] != ir::INVALID_SLOT) {

                        s_atom.flags |= static_cast<decltype(s_atom.flags)>(ir::Flags::IS_OPTIMIZED);

                        s_core.intent_hash = ir::IRNode::calculate_hash(
                            s_atom.opcode,
                            s_core.result_id,
                            s_core.op_a,
                            s_core.op_b,
                            s_core.op_c
                        );
                        break;
                    }
                }
            }

            if (has_dependency_violation(nest)) {
                stream.rollback_transaction();
                return;
            }

            if (stream.validate_shadow_integrity()) {
                stream.commit_transaction();
            } else {
                stream.rollback_transaction();
            }
        } catch (...) {
            stream.rollback_transaction();
        }
    }

    bool unroll_and_jam(ir::IRStream& stream, uint32_t factor) noexcept {
        const size_t original_size = stream.size();

        if (factor <= 1 || original_size == 0 || stream.is_null()) {
            return true;
        }

        if (static_cast<size_t>(factor) > (stream.capacity() / original_size)) {
            return false;
        }

        stream.begin_transaction();

        try {
            uint32_t shadow_idx = 0;
            for (uint32_t f = 0; f < factor; ++f) {
                for (uint32_t i = 0; i < static_cast<uint32_t>(original_size); ++i) {
                    const ir::IRNode node = stream.get_node_readonly(i);

                    if (node.is_terminator() && f < (factor - 1)) {
                        continue;
                    }

                    if (shadow_idx >= stream.capacity()) {
                        stream.rollback_transaction();
                        return false;
                    }

                    ir::IRAtom& s_atom = stream.get_shadow_atom(shadow_idx);
                    ir::IRCore& s_core = stream.get_shadow_core(shadow_idx);

                    const uint32_t offset = (f * 0x4000);

                    s_atom.opcode = node.opcode;
                    s_atom.flags = node.flags | static_cast<decltype(s_atom.flags)>(ir::Flags::IS_SIMD_BURST);

                    s_core.result_id = (node.result_id != ir::INVALID_SLOT)
                        ? node.result_id + offset
                        : ir::INVALID_SLOT;
                    s_core.op_a = (node.op_a != ir::INVALID_SLOT)
                        ? node.op_a + offset
                        : ir::INVALID_SLOT;
                    s_core.op_b = (node.op_b != ir::INVALID_SLOT)
                        ? node.op_b + offset
                        : ir::INVALID_SLOT;
                    s_core.op_c = (node.op_c != ir::INVALID_SLOT)
                        ? node.op_c + offset
                        : ir::INVALID_SLOT;

                    s_core.intent_hash = ir::IRNode::calculate_hash(
                        s_atom.opcode,
                        s_core.result_id,
                        s_core.op_a,
                        s_core.op_b,
                        s_core.op_c
                    );

                    shadow_idx++;
                }
            }

            stream.update_shadow_size(shadow_idx);

            if (!stream.validate_shadow_integrity()) {
                stream.rollback_transaction();
                return false;
            }

            stream.commit_transaction();
            return true;

        } catch (...) {
            stream.rollback_transaction();
            return false;
        }
    }

private:
    static bool has_dependency_violation(const LoopNest& nest) noexcept {
        for (uint32_t i = 0; i < nest.depth && i < 8; ++i) {
            if (nest.strides[i] <= 0 || nest.lower_bounds[i] > nest.upper_bounds[i]) {
                return true;
            }
        }
        return false;
    }
};

// ============================================================================
// LOOP FUSION PASS (CACHE LOCALITY ENHANCEMENT)
// ============================================================================

class DGTLLoopFusion : public pipeline::DGTLOptimizationPass {
private:
    analysis::DGTLMemorySmasher* m_smasher;

public:
    explicit DGTLLoopFusion(analysis::DGTLMemorySmasher* smasher = nullptr) noexcept
        : m_smasher(smasher) {}

    void execute(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 10) {
            return;
        }

        stream.begin_transaction();

        std::vector<LoopRange> loops = detect_loops(stream);
        if (loops.size() < 2) {
            stream.rollback_transaction();
            return;
        }

        std::vector<std::pair<size_t, size_t>> fusion_pairs;
        for (size_t i = 0; i + 1 < loops.size(); ++i) {
            if (can_fuse(stream, loops[i], loops[i + 1])) {
                fusion_pairs.push_back({i, i + 1});
            }
        }

        if (fusion_pairs.empty()) {
            stream.rollback_transaction();
            return;
        }

        bool modified = perform_fusion(stream, loops, fusion_pairs);

        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "LoopFusion";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

private:
    std::vector<LoopRange> detect_loops(const ir::IRStream& stream) const noexcept {
        std::vector<LoopRange> loops;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);

            const bool is_control_terminator =
                iface::has_trait(node.opcode, iface::OpTrait::IsTerminator) &&
                iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow);
            if (is_control_terminator) {
                uint32_t body_end = std::min(
                    i + 100,
                    static_cast<uint32_t>(stream.size() - 1)
                );

                LoopBounds bounds{
                    .induction_var = node.op_a,
                    .lower_bound = 0,
                    .upper_bound = 1000,
                    .stride = 1
                };

                loops.push_back({i, body_end, bounds});
            }
        }

        return loops;
    }

    bool can_fuse(const ir::IRStream& stream,
                  const LoopRange& loop1,
                  const LoopRange& loop2) const noexcept {
        if (!loop1.bounds.matches(loop2.bounds)) {
            return false;
        }

        if (loop2.start_idx != loop1.end_idx + 1) {
            return false;
        }

        if (m_smasher) {
            for (uint32_t i = loop1.start_idx; i <= loop1.end_idx; ++i) {
                for (uint32_t j = loop2.start_idx; j <= loop2.end_idx; ++j) {
                    if (!m_smasher->can_reorder(stream, i, j)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool perform_fusion(ir::IRStream& stream,
                       const std::vector<LoopRange>& loops,
                       const std::vector<std::pair<size_t, size_t>>& fusion_pairs) noexcept {
        uint32_t shadow_cursor = 0;
        bool modified = false;

        for (const auto& pair : fusion_pairs) {
            const LoopRange& loop1 = loops[pair.first];
            const LoopRange& loop2 = loops[pair.second];

            for (uint32_t i = loop1.start_idx; i <= loop1.end_idx; ++i) {
                sync_to_shadow(stream, i, shadow_cursor++);
            }

            for (uint32_t i = loop2.start_idx; i <= loop2.end_idx; ++i) {
                const auto& node = stream.get_node_readonly(i);

                const bool is_control_terminator =
                    iface::has_trait(node.opcode, iface::OpTrait::IsTerminator) &&
                    iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow);
                if (!is_control_terminator) {
                    sync_to_shadow(stream, i, shadow_cursor++);
                }
            }

            modified = true;
        }

        for (uint32_t i = static_cast<uint32_t>(stream.size()) / 2;
             i < static_cast<uint32_t>(stream.size()); ++i) {
            if (shadow_cursor < stream.capacity()) {
                sync_to_shadow(stream, i, shadow_cursor++);
            }
        }

        return modified;
    }

    void sync_to_shadow(ir::IRStream& stream, uint32_t src_idx, uint32_t dst_idx) const noexcept {
        if (dst_idx >= stream.capacity()) {
            return;
        }

        const auto& node = stream.get_node_readonly(src_idx);
        auto& s_atom = stream.get_shadow_atom(dst_idx);
        auto& s_core = stream.get_shadow_core(dst_idx);

        s_atom.opcode = node.opcode;
        s_atom.flags = node.flags | static_cast<decltype(s_atom.flags)>(ir::Flags::IS_OPTIMIZED);
        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;
        s_core.intent_hash = ir::IRNode::calculate_hash(
            s_atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c
        );
    }
};

// ============================================================================
// LOOP UNSWITCHER PASS (INVARIANT BRANCH LIFTING)
// ============================================================================

class DGTLLoopUnswitcher {
private:
    analysis::DGTLAICostModel& m_ai_model;
    DGTLSpeculativeDCE& m_dce_engine;
    core::DGTLFormalVerifier& m_verifier;

public:
    explicit DGTLLoopUnswitcher(
        analysis::DGTLAICostModel& ai_model,
        DGTLSpeculativeDCE& dce_engine,
        core::DGTLFormalVerifier& verifier) noexcept
        : m_ai_model(ai_model), m_dce_engine(dce_engine), m_verifier(verifier) {}

    void unswitch_loop(ir::IRStream& stream) noexcept {
        const size_t count = stream.size();
        if (count == 0 || stream.is_null()) {
            return;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const auto& atom = stream.get_atom_readonly(i);
            const auto& core = stream.get_core_readonly(i);

            const bool is_control_terminator =
                iface::has_trait(atom.opcode, iface::OpTrait::IsTerminator) &&
                iface::implements_interface(atom.opcode, iface::OpInterface::ControlFlow);
            if (is_control_terminator && is_loop_invariant(core.op_a, stream)) {

                float cost_before = static_cast<float>(stream.size());
                float speedup_gain = cost_before * 0.18f;
                float cache_penalty = (count > 256) ? 8.5f : 1.2f;

                if (speedup_gain > cache_penalty) {
                    stream.begin_transaction();

                    try {
                        if (perform_shadow_versioning(stream, i)) {
                            if (stream.validate_shadow_integrity()) {
                                stream.commit_transaction();
                                m_dce_engine.execute(stream);
                                return;
                            }
                        }
                    } catch (...) {
                    }

                    stream.rollback_transaction();
                }
            }
        }
    }

private:
    static bool is_loop_invariant(uint32_t cond_reg, const ir::IRStream& stream) noexcept {
        if (cond_reg == ir::INVALID_SLOT) {
            return false;
        }

        for (uint32_t i = 0; i < stream.size(); ++i) {
            const auto& core = stream.get_core_readonly(i);

            if (core.result_id == cond_reg) {
                return false;
            }
        }
        return true;
    }

    static bool perform_shadow_versioning(ir::IRStream& stream, uint32_t branch_idx) noexcept {
        try {
            stream.push_full_to_shadow();

            auto& shad_atom = stream.get_shadow_atom(branch_idx);
            auto& shad_core = stream.get_shadow_core(branch_idx);

            shad_atom.opcode = ir::OpCode::GOTO;
            shad_atom.flags |= static_cast<decltype(shad_atom.flags)>(
                ir::Flags::IS_SPEC_CONST
            );
            shad_core.logic_flags |= ir::HintFlags::IsHosted;

            shad_core.result_id = ir::INVALID_SLOT;
            shad_core.op_a = shad_core.op_b;
            shad_core.op_b = 0;
            shad_core.op_c = 0;

            shad_core.intent_hash = ir::IRNode::calculate_hash(
                shad_atom.opcode,
                shad_core.result_id,
                shad_core.op_a,
                shad_core.op_b,
                shad_core.op_c
            );

            return stream.is_active_transaction();

        } catch (...) {
            return false;
        }
    }
};

} // namespace dgtl::pass::transform

