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

/*
 * Unified loop transform header:
 * - Loop fusion (cache locality enhancement)
 * - Loop unswitching (invariant branch lifting)
 * - Polyhedral transforms (tiling, unroll-and-jam)
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <array>
#include <span>
#include <cstring>
#include <stdexcept>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager_Config.hpp"
#include "dgtl/analysis/Liveness_Memory_Dependency_Unified.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"
#include "dgtl/pass/speculative/Speculate_Alias_DCE_Inliner_Temporal_Vector_Fusion.hpp"
#include "dgtl/ir/interfaces/Op_Traits.hpp"
#include "dgtl/ir/rewrite/Pattern_Rewriter.hpp"

namespace dgtl::pass::transform {

namespace analysis = dgtl::analysis;
namespace core = dgtl::core;
namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;
namespace graph = dgtl::graph;
namespace iface = dgtl::ir::interfaces;

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
};

// ============================================================================
// POLYHEDRAL TRANSFORM ENGINE (TILING, UNROLL-AND-JAM, SCoP ANALYSIS)
// ============================================================================

class DGTLPolyTransformer {
public:
    /**
     * @brief Check if a region is a valid SCoP (Static Control Part) for polyhedral transforms.
     * @param region Region to check.
     * @return true if region is affine candidate and safe to transform.
     */
    [[nodiscard]] static bool is_valid_scop_for_transform(const graph::Region& region) noexcept {
        // Non-affine regions are never eligible for polyhedral transforms
        if (region.scop_annotation.affinity != graph::ScopAffinity::AFFINE_CANDIDATE) {
            return false;
        }

        // Region must have detectable loop structure
        if (region.scop_annotation.loop_depth == 0) {
            return false;  // Linear code is not a loop nest
        }

        return true;
    }

    /**
     * @brief Check if a region can be tiled legally.
     * @param region Region to check.
     * @return true if tiling is legal and beneficial.
     */
    [[nodiscard]] static bool can_tile_region(const graph::Region& region) noexcept {
        return is_valid_scop_for_transform(region) && region.scop_annotation.can_tile;
    }

    /**
     * @brief Check if a region can be parallelized.
     * @param region Region to check.
     * @return true if parallel lowering is legal and beneficial.
     */
    [[nodiscard]] static bool can_parallelize_region(const graph::Region& region) noexcept {
        return is_valid_scop_for_transform(region) && region.scop_annotation.can_parallelize;
    }

    /**
     * @brief Loop Tiling via Transactional Shadow Writing.
     * PILLAR 2: Transactional shadow buffer isolation
     * PILLAR 3: Sentinel validation of tiled structure
     * @param stream Target IR stream
     * @param nest Loop nest descriptor
     * @param tile_size Tile dimension (typically cache-line aware)
     */
    void apply_tiling(ir::IRStream& stream, const LoopNest& nest, [[maybe_unused]] uint32_t tile_size) noexcept {
        if (!nest.is_valid() || stream.is_null() || stream.size() == 0) {
            return;
        }

        const size_t stream_size = stream.size();
        if (nest.depth < 2) return;

        // PILLAR 2: Open transactional shadow buffer
        stream.begin_transaction();

        try {
            for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
                const ir::IRNode node = stream.get_node_readonly(i);
                ir::IRAtom& s_atom = stream.get_shadow_atom(i);
                ir::IRCore& s_core = stream.get_shadow_core(i);

                // Initial sync to shadow
                s_atom.opcode = node.opcode;
                s_atom.flags = node.flags;
                s_core.result_id = node.result_id;
                s_core.op_a = node.op_a;
                s_core.op_b = node.op_b;
                s_core.op_c = node.op_c;
                s_core.intent_hash = node.intent_hash;

                // Mark loop indices touched by tiling as optimized
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

            // PILLAR 3: Dependency validation
            if (has_dependency_violation(nest)) {
                stream.rollback_transaction();
                return;
            }

            // Validate shadow integrity before commit
            if (stream.validate_shadow_integrity()) {
                stream.commit_transaction();
            } else {
                stream.rollback_transaction();
            }
        } catch (...) {
            stream.rollback_transaction();
        }
    }

    /**
     * @brief Loop Unroll-and-Jam transformation.
     * Replicates loop body N times with register renaming to enable fusion.
     * PILLAR 2: Transactional growth with capacity checks
     * PILLAR 3: Sentinel validation before commit
     * @param stream Target IR stream
     * @param factor Unroll multiplicity (must be >= 2)
     * @return true if transformation succeeded
     */
    bool unroll_and_jam(ir::IRStream& stream, uint32_t factor) noexcept {
        const size_t original_size = stream.size();

        if (factor <= 1 || original_size == 0 || stream.is_null()) {
            return true;
        }

        // Capacity check: ensure expanded size fits in stream
        if (static_cast<size_t>(factor) > (stream.capacity() / original_size)) {
            return false;
        }

        stream.begin_transaction();
        uint32_t shadow_idx = 0;

        try {
            for (uint32_t f = 0; f < factor; ++f) {
                for (uint32_t i = 0; i < static_cast<uint32_t>(original_size); ++i) {
                    const ir::IRNode node = stream.get_node_readonly(i);

                    // Skip terminators on intermediate unrolls to enable fusion
                    if (node.is_terminator() && f < (factor - 1)) {
                        continue;
                    }

                    if (shadow_idx >= stream.capacity()) {
                        stream.rollback_transaction();
                        return false;
                    }

                    ir::IRAtom& s_atom = stream.get_shadow_atom(shadow_idx);
                    ir::IRCore& s_core = stream.get_shadow_core(shadow_idx);

                    // Register renaming offset to avoid conflicts between unrolled copies
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

            // PILLAR 3: Sentinel check before commit
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
    /**
     * @brief Dependency matrix safety check (AVX-512 accelerated in production).
     * @param nest Loop nest to check for dependency violations.
     * @return true if dependencies prevent transformation.
     */
    bool has_dependency_violation(const LoopNest& nest) const noexcept {
        // Placeholder for linear algebra dependency safety
        (void)nest;
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
    /**
     * @brief Construct loop fusion pass with optional memory dependency analyzer.
     * @param smasher Memory dependency engine (optional, for dependency checking)
     */
    explicit DGTLLoopFusion(analysis::DGTLMemorySmasher* smasher = nullptr) noexcept
        : m_smasher(smasher) {}

    /**
     * @brief Execute loop fusion optimization on IR stream.
     * ALGORITHM:
     * 1. Detect adjacent loops with identical bounds
     * 2. Verify fusion legality via dependency analysis
     * 3. Merge loop bodies in shadow tier
     * 4. Commit if validation passes
     * @param stream Target IR stream
     */
    void execute(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 10) {
            return;
        }

        stream.begin_transaction();

        // Phase 1: Detect adjacent loops
        std::vector<LoopRange> loops = detect_loops(stream);
        if (loops.size() < 2) {
            stream.rollback_transaction();
            return;
        }

        // Phase 2: Identify fusion candidates
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

        // Phase 3: Perform fusion in shadow tier
        bool modified = perform_fusion(stream, loops, fusion_pairs);

        // PILLAR 3: The Sentinel
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
    /**
     * @brief Scan IR stream to detect loop entry markers.
     * @param stream Source IR stream
     * @return Vector of detected loop ranges
     */
    std::vector<LoopRange> detect_loops(const ir::IRStream& stream) const noexcept {
        std::vector<LoopRange> loops;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);

            // Detect loop entry markers from control-flow terminators.
            const bool is_control_terminator =
                iface::has_trait(node.opcode, iface::OpTrait::IsTerminator) &&
                iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow);
            if (is_control_terminator) {
                // Heuristic: assume loop body is bounded within next 100 nodes
                uint32_t body_end = std::min(
                    i + 100,
                    static_cast<uint32_t>(stream.size() - 1)
                );

                LoopBounds bounds{
                    .induction_var = node.op_a,
                    .lower_bound = 0,
                    .upper_bound = 1000,  // Simplified detection
                    .stride = 1
                };

                loops.push_back({i, body_end, bounds});
            }
        }

        return loops;
    }

    /**
     * @brief Check if two loops can be legally fused.
     * @param stream IR stream context
     * @param loop1 First loop candidate
     * @param loop2 Second loop candidate
     * @return true if fusion is legal
     */
    bool can_fuse(const ir::IRStream& stream,
                  const LoopRange& loop1,
                  const LoopRange& loop2) const noexcept {
        // Check 1: Identical loop bounds
        if (!loop1.bounds.matches(loop2.bounds)) {
            return false;
        }

        // Check 2: Adjacent in code (no instructions between)
        if (loop2.start_idx != loop1.end_idx + 1) {
            return false;
        }

        // Check 3: No loop-carried dependencies prevent fusion
        if (m_smasher) {
            for (uint32_t i = loop1.start_idx; i <= loop1.end_idx; ++i) {
                for (uint32_t j = loop2.start_idx; j <= loop2.end_idx; ++j) {
                    if (!m_smasher->can_reorder(stream, i, j)) {
                        return false; // Dependency prevents fusion
                    }
                }
            }
        }

        return true;
    }

    /**
     * @brief Execute fusion in shadow tier.
     * @param stream Target IR stream
     * @param loops Detected loop ranges
     * @param fusion_pairs Pairs of loops to fuse
     * @return true if any fusion was performed
     */
    bool perform_fusion(ir::IRStream& stream,
                       const std::vector<LoopRange>& loops,
                       const std::vector<std::pair<size_t, size_t>>& fusion_pairs) noexcept {
        uint32_t shadow_cursor = 0;
        bool modified = false;

        for (const auto& pair : fusion_pairs) {
            const LoopRange& loop1 = loops[pair.first];
            const LoopRange& loop2 = loops[pair.second];

            // Copy loop1 to shadow
            for (uint32_t i = loop1.start_idx; i <= loop1.end_idx; ++i) {
                sync_to_shadow(stream, i, shadow_cursor++);
            }

            // Merge loop2 body into loop1 iteration
            for (uint32_t i = loop2.start_idx; i <= loop2.end_idx; ++i) {
                const auto& node = stream.get_node_readonly(i);

                // Skip loop2's exit terminator (loop1's exit is reused)
                const bool is_control_terminator =
                    iface::has_trait(node.opcode, iface::OpTrait::IsTerminator) &&
                    iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow);
                if (!is_control_terminator) {
                    sync_to_shadow(stream, i, shadow_cursor++);
                }
            }

            modified = true;
        }

        // Copy remaining code
        for (uint32_t i = static_cast<uint32_t>(stream.size()) / 2;
             i < static_cast<uint32_t>(stream.size()); ++i) {
            if (shadow_cursor < stream.capacity()) {
                sync_to_shadow(stream, i, shadow_cursor++);
            }
        }

        return modified;
    }

    /**
     * @brief Copy node from main tier to shadow tier with optimization marking.
     * @param stream IR stream
     * @param src_idx Source index in main tier
     * @param dst_idx Destination index in shadow tier
     */
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
    /**
     * @brief Construct loop unswitcher with AI cost model, DCE engine, and verifier.
     * @param ai_model AI-driven cost prediction for unswitch profitability
     * @param dce_engine Speculative DCE for dead path elimination
     * @param verifier Formal verification authority (PILLAR 3)
     */
    explicit DGTLLoopUnswitcher(
        analysis::DGTLAICostModel& ai_model,
        DGTLSpeculativeDCE& dce_engine,
        core::DGTLFormalVerifier& verifier) noexcept
        : m_ai_model(ai_model), m_dce_engine(dce_engine), m_verifier(verifier) {}

    /**
     * @brief Execute loop unswitching optimization.
     * Lifts loop-invariant branches out of hot loops via shadow specialization.
     * PILLAR 2: Transactional shadow versioning
     * PILLAR 3: Sentinel validation of specialized paths
     * @param stream Target IR stream
     */
    void unswitch_loop(ir::IRStream& stream) noexcept {
        const size_t count = stream.size();
        if (count == 0 || stream.is_null()) {
            return;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const auto& atom = stream.get_atom_readonly(i);
            const auto& core = stream.get_core_readonly(i);

            // 1. Detect invariant control-flow terminator
            const bool is_control_terminator =
                iface::has_trait(atom.opcode, iface::OpTrait::IsTerminator) &&
                iface::implements_interface(atom.opcode, iface::OpInterface::ControlFlow);
            if (is_control_terminator && is_loop_invariant(core.op_a, stream)) {

                // 2. AI profitability check
                float cost_before = m_ai_model.predict_cost(stream);
                float speedup_gain = cost_before * 0.18f;  // 18% typical speedup
                float cache_penalty = (count > 256) ? 8.5f : 1.2f;

                if (speedup_gain > cache_penalty) {
                    // PILLAR 2: Begin transactional specialization
                    stream.begin_transaction();

                    try {
                        if (perform_shadow_versioning(stream, i)) {
                            // PILLAR 3: Sentinel validation
                            if (stream.validate_shadow_integrity()) {
                                stream.commit_transaction();
                                m_dce_engine.execute(stream);
                                return;
                            }
                        }
                    } catch (...) {
                        // Transaction safety: rollback on any error
                    }

                    stream.rollback_transaction();
                }
            }
        }
    }

private:
    /**
     * @brief Prove that a register is never redefined within the stream.
     * @param cond_reg Register to check for loop invariance
     * @param stream IR stream context
     * @return true if register is never written
     */
    bool is_loop_invariant(uint32_t cond_reg, const ir::IRStream& stream) const noexcept {
        if (cond_reg == ir::INVALID_SLOT) {
            return false;
        }

        for (uint32_t i = 0; i < stream.size(); ++i) {
            const auto& core = stream.get_core_readonly(i);

            // If any instruction writes to this register, it's not invariant
            if (core.result_id == cond_reg) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Execute path specialization into shadow tier.
     * Transforms conditional branch into deterministic path jump for the TRUE case.
     * PILLAR 2: Shadow-buffered path specialization
     * @param stream IR stream
     * @param branch_idx Index of branch instruction to specialize
     * @return true if specialization succeeded
     */
    bool perform_shadow_versioning(ir::IRStream& stream, uint32_t branch_idx) noexcept {
        try {
            // Step 1: Full copy to shadow tier
            stream.push_full_to_shadow();

            // Step 2: Specialize the shadow branch to TRUE path
            auto& shad_atom = stream.get_shadow_atom(branch_idx);
            auto& shad_core = stream.get_shadow_core(branch_idx);

            shad_atom.opcode = ir::OpCode::GOTO;
            shad_atom.flags |= static_cast<decltype(shad_atom.flags)>(
                ir::Flags::IS_SPEC_CONST | ir::HintFlags::IsHosted
            );

            shad_core.result_id = ir::INVALID_SLOT;
            shad_core.op_a = shad_core.op_b;  // TRUE target becomes primary operand
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
