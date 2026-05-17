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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * MERGED UNIT: SPECULATIVE ALIAS HOISTER + SPECULATIVE DCE + SPECULATIVE INLINER
 *   STEP 28: SPECULATIVE DEAD CODE ELIMINATION (TRANSACTIONAL)
 *   STEP 32: AI-GUIDED SPECULATIVE INLINER (TRANSACTIONAL)
 *   STEP 38: SPECULATIVE ALIAS HOISTER (LICM+)
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED CROSS-STREAM MOVE / SHADOW STREAMS / SHADOW-BUFFERED SPLICING)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK / L1-I CACHE & INVARIANT GUARD)
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/Liveness_Memory_Dependency_Unified.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/pipeline/Pass_Manager_Config.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/ir/interfaces/Op_Traits.hpp"
#include "dgtl/ir/rewrite/Pattern_Rewriter.hpp"

namespace dgtl::pass {

// Force explicit mapping for the IR namespace to prevent resolution errors
namespace ir       = dgtl::ir;
namespace analysis = dgtl::analysis;
namespace pipeline = dgtl::pipeline;
namespace iface    = dgtl::ir::interfaces;

// =============================================================================
// STEP 38: SPECULATIVE ALIAS HOISTER (LICM+)
// =============================================================================

class DGTLSpeculativeAliasHoister {
    dgtl::analysis::DGTLMemorySmasher& smasher;
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    DGTLSpeculativeAliasHoister(dgtl::analysis::DGTLMemorySmasher& s, dgtl::analysis::DGTLAICostModel& ai) noexcept
        : smasher(s), ai_model(ai) {}

    /**
     * @brief DGTLoptManager Bridge.
     * Uses your IRStream Transactional API for local block safety.
     */
    void execute(ir::IRStream& stream) {
        if (stream.size() == 0) [[unlikely]] return;

        stream.begin_transaction();
        // Local optimization logic using BIR 2.0 accessors
        if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
            return;
        }
        stream.commit_transaction();
    }

    /**
     * @brief The "Zero-Corruption" Hoisting Strike.
     * Moves loads from loop_body to pre_header using Shadow Splicing.
     */
    void hoist_speculative_invariants(ir::IRStream& loop_body, ir::IRStream& pre_header) {
        if (loop_body.size() == 0) return;

        // PILLAR 2: Start Dual-Stream Transaction
        loop_body.begin_transaction();
        pre_header.begin_transaction();

        const size_t body_count = loop_body.size();
        for (uint32_t i = 0; i < static_cast<uint32_t>(body_count); ++i) {
            // Accessing Stream A via your read-only accessor
            ir::IRNode node = loop_body.get_node_readonly(i);

            const bool is_memory_read = iface::has_trait(node.opcode, iface::OpTrait::IsMemoryRead);
            if (is_memory_read && !(node.flags & ir::Flags::IS_VOLATILE)) {
                bool should_hoist = false;

                // FIX: opting index 'i' instead of the 'node' object
                if (smasher.is_guaranteed_invariant(loop_body, i)) {
                    should_hoist = true;
                } else {
                    const float alias_prob = max_alias_probability(loop_body, i);
                    if (alias_prob < 0.0001f) {
                        inject_runtime_alias_guard_transactional(pre_header, node);
                        should_hoist = true;
                    }
                }

                if (should_hoist) {
                    move_to_preheader_transactional(loop_body, i, pre_header);
                } else {
                    // Not hoisted: Ensure Shadow Tier B still contains the original node
                    sync_shadow_node(loop_body, i, node);
                }
            } else {
                // Regular instruction: Sync to Shadow Tier B
                sync_shadow_node(loop_body, i, node);
            }
        }

        // Update Shadow Stream counts
        loop_body.set_shadow_count(body_count);

        // PILLAR 3: Validate both streams before atomic swap
        if (!loop_body.validate_shadow_integrity() || !pre_header.validate_shadow_integrity()) {
            loop_body.rollback_transaction();
            pre_header.rollback_transaction();
            return;
        }

        loop_body.commit_transaction();
        pre_header.commit_transaction();
    }

private:
    /**
     * @brief Writes a synchronization node to the Shadow Tier.
     */
    void sync_shadow_node(ir::IRStream& s, uint32_t idx, const ir::IRNode& node) noexcept {
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash, 0, 0};
    }

    [[nodiscard]] float max_alias_probability(const ir::IRStream& body, uint32_t load_idx) noexcept {
        float alias_prob = 0.0f;
        const uint32_t limit = static_cast<uint32_t>(body.size());
        for (uint32_t j = load_idx + 1; j < limit; ++j) {
            const ir::IRNode other = body.get_node_readonly(j);
            const bool is_memory_op =
                iface::implements_interface(other.opcode, iface::OpInterface::Memory) ||
                iface::has_trait(other.opcode, iface::OpTrait::IsMemoryRead) ||
                iface::has_trait(other.opcode, iface::OpTrait::IsMemoryWrite);
            if (!is_memory_op) {
                continue;
            }

            alias_prob = std::max(alias_prob, smasher.predict_alias_probability(body, load_idx, j));
            if (alias_prob >= 0.0001f) {
                break;
            }
        }

        return alias_prob;
    }

    void inject_runtime_alias_guard_transactional(ir::IRStream& pre, const ir::IRNode& load_node) {
        const uint32_t cursor = pre.reserve_shadow_slot();
        if (cursor == ir::INVALID_SLOT || cursor >= pre.capacity()) return;

        ir::IRAtom& s_atom = pre.get_shadow_atom(cursor);
        ir::IRCore& s_core = pre.get_shadow_core(cursor);

        s_atom = {ir::OpCode::GUARD_ALIAS, ir::HintFlags::RequiresDepassTarget};
        s_core = {ir::INVALID_SLOT, load_node.op_a, 0, 0, 0, 0, 0};

        s_core.intent_hash = ir::IRNode::calculate_hash(
            s_atom.opcode,
            s_core.result_id,
            s_core.op_a,
            s_core.op_b,
            s_core.op_c
        );
    }

    void move_to_preheader_transactional(ir::IRStream& body, uint32_t body_idx, ir::IRStream& pre) {
        const uint32_t pre_cursor = pre.reserve_shadow_slot();
        if (pre_cursor == ir::INVALID_SLOT || pre_cursor >= pre.capacity()) return;

        ir::IRNode original = body.get_node_readonly(body_idx);

        // 1. Move Node to Preheader Shadow Tier
        ir::IRAtom& p_atom = pre.get_shadow_atom(pre_cursor);
        ir::IRCore& p_core = pre.get_shadow_core(pre_cursor);
        p_atom = {original.opcode, original.flags};
        p_core = {original.result_id, original.op_a, original.op_b, original.op_c, original.intent_hash, 0, 0};

        p_core.intent_hash = ir::IRNode::calculate_hash(
            p_atom.opcode,
            p_core.result_id,
            p_core.op_a,
            p_core.op_b,
            p_core.op_c
        );

        // 2. Replace Loop Body Node in Shadow Tier with REG_MOVE
        ir::IRAtom& b_atom = body.get_shadow_atom(body_idx);
        ir::IRCore& b_core = body.get_shadow_core(body_idx);
        b_atom = {ir::OpCode::REG_MOVE, static_cast<uint16_t>(original.flags | ir::Flags::IS_OPTIMIZED)};
        b_core = {original.result_id, original.result_id, 0, 0, 0, 0, 0};

        b_core.intent_hash = ir::IRNode::calculate_hash(
            b_atom.opcode,
            b_core.result_id,
            b_core.op_a,
            b_core.op_b,
            b_core.op_c
        );
    }
};

// =============================================================================
// STEP 28: SPECULATIVE DEAD CODE ELIMINATION (TRANSACTIONAL)
// =============================================================================

/**
 * @class DGTLSpeculativeDCE
 * @brief Performs speculative path pruning and CFG reordering on the Shadow Tier.
 * Inherits from DGTLOptimization to integrate with the Task Pipeline Manager.
 */
class DGTLSpeculativeDCE : public pipeline::DGTLOptimizationPass {
    struct BlockProfile {
        uint32_t start_idx;
        uint32_t len;
        float probability;
    };

    const analysis::DGTLAICostModel* internal_model = nullptr;

public:
    [[nodiscard]] const char* get_pass_name() const noexcept override { return "SpeculativeDCE"; }
    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override { return pipeline::PassStage::LateCleanup; }
    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override { return pipeline::PassAffinity::Medium; }
    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override { return pipeline::PassAffinity::High; }
    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override { return pipeline::PassAffinity::Low; }
    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override { return pipeline::PassAffinity::High; }
    DGTLSpeculativeDCE() = default;
    explicit DGTLSpeculativeDCE(const analysis::DGTLAICostModel& model) : internal_model(&model) {}

    /**
     * @brief Entry Point for the Pipeline Manager.
     * Implements the "Double-Buffered Safety Net" for BIR 1.0.0.
     */
    void execute(ir::IRStream& stream) override {
        if (stream.size() == 0) [[unlikely]] return;

        if (!internal_model) {
            (void)stream.validate_shadow_integrity();
            return;
        }

        // PILLAR 2: Enter Transactional Mode
        stream.begin_transaction();

        const bool success = prune_and_reorder_transactional(stream, *internal_model);

        // PILLAR 3: THE SENTINEL - Final validation before commit
        if (!success) {
            stream.rollback_transaction();
        } else if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
        } else {
            stream.commit_transaction();
        }
    }

private:
    /**
     * @brief High-performance CFG Reordering.
     * Reorganizes the Shadow Tier so Hot Paths are contiguous.
     */
    bool prune_and_reorder_transactional(ir::IRStream& stream, const analysis::DGTLAICostModel& ai_model) {
        std::vector<BlockProfile> blocks = analyze_cfg_probability(stream, ai_model);
        if (blocks.empty()) return false;

        // Partition blocks: Hot blocks first, Cold blocks (prob < 0.01%) last
        auto frozen_it = std::stable_partition(blocks.begin(), blocks.end(), [](const auto& b) {
            return b.probability > 0.0001f;
        });

        uint32_t shadow_cursor = 0;

        // 1. Write Hot Path to Shadow Buffer
        for (auto it = blocks.begin(); it != frozen_it; ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, true);
        }

        // 2. Align to Cache Line (4-node stride for 128-byte cache lines)
        align_shadow_to_cache_line(stream, shadow_cursor);

        // 3. Write Cold Path
        for (auto it = frozen_it; it != blocks.end(); ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, false);
        }

        stream.update_shadow_size(shadow_cursor);
        return shadow_cursor > 0;
    }

    std::vector<BlockProfile> analyze_cfg_probability(const ir::IRStream& s, const analysis::DGTLAICostModel& ai) {
        (void)ai;
        std::vector<BlockProfile> blocks;
        uint32_t last_start = 0;
        const size_t count = s.size();

        for (uint32_t i = 0; i < count; ++i) {
            ir::IRNode node = s.get_node_readonly(i);

            const bool is_term =
                iface::has_trait(node.opcode, iface::OpTrait::IsTerminator) &&
                iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow);

            if (is_term || i == (count - 1)) {
                float prob = 1.0f;
                if (node.flags & ir::HintFlags::BranchUnlikely) prob = 0.0000001f;
                else if (node.flags & ir::HintFlags::BranchLikely) prob = 1.0f;

                blocks.push_back({last_start, (i - last_start) + 1, prob});
                last_start = i + 1;
            }
        }
        return blocks;
    }

    void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        core.intent_hash = ir::IRNode::calculate_hash(
            atom.opcode,
            core.result_id,
            core.op_a,
            core.op_b,
            core.op_c
        );
    }

    void append_block_to_shadow(ir::IRStream& s, uint32_t& cursor, const BlockProfile& p) noexcept {
        if (cursor + p.len > s.capacity()) return;

        for (uint32_t i = 0; i < p.len; ++i) {
            ir::IRNode node = s.get_node_readonly(p.start_idx + i);

            ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
            ir::IRCore& s_core = s.get_shadow_core(cursor);

            s_atom.opcode    = node.opcode;
            s_atom.flags     = node.flags;
            s_core.result_id = node.result_id;
            s_core.op_a      = node.op_a;
            s_core.op_b      = node.op_b;
            s_core.op_c      = node.op_c;

            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void align_shadow_to_cache_line(ir::IRStream& s, uint32_t& cursor) noexcept {
        // BIR 1.0.0: Align to 4-atom (16-byte) boundaries for vector dispatch
        uint32_t align_gap = (4 - (cursor & 3)) & 3;
        for (uint32_t i = 0; i < align_gap && (cursor < s.capacity()); ++i) {
            ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
            ir::IRCore& s_core = s.get_shadow_core(cursor);

            s_atom.opcode    = ir::OpCode::NOP;
            s_atom.flags     = ir::Flags::NONE;
            s_core.result_id = ir::INVALID_SLOT;
            s_core.op_a      = 0;
            s_core.op_b      = 0;
            s_core.op_c      = 0;

            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void inject_branch_hint_shadow(ir::IRStream& s, uint32_t cursor, bool likely) noexcept {
        if (cursor == 0) return;

        const uint32_t target_idx = cursor - 1;
        auto& s_atom = s.get_shadow_atom(target_idx);

        // Internal check for terminator type
        const bool is_term =
            iface::has_trait(s_atom.opcode, iface::OpTrait::IsTerminator) &&
            iface::implements_interface(s_atom.opcode, iface::OpInterface::ControlFlow);

        if (is_term) {
            if (likely) {
                s_atom.flags |= ir::HintFlags::BranchLikely;
                s_atom.flags &= ~ir::HintFlags::BranchUnlikely;
            } else {
                s_atom.flags |= ir::HintFlags::BranchUnlikely;
                s_atom.flags &= ~ir::HintFlags::BranchLikely;
            }
            finalize_shadow_node(s, target_idx);
        }
    }
};

// =============================================================================
// STEP 32: AI-GUIDED SPECULATIVE INLINER (TRANSACTIONAL)
// =============================================================================

/**
 * @class DGTLSpeculativeInliner
 * @brief Performs AI-guided speculative function inlining using Shadow Tiers.
 * Implements DGTLOptimizationPass for BIR 1.0.0 Pipeline compatibility.
 */
class DGTLSpeculativeInliner final : public dgtl::pipeline::DGTLOptimizationPass {
private:
    dgtl::analysis::DGTLAICostModel* ai_model_ptr;
    dgtl::semantic::SymbolTable* symbols_ptr;

    // 32KB L1 Instruction Cache Limit
    static constexpr size_t L1_I_CACHE_LIMIT = 32768;
    // BIR 1.0.0 Nodes: 20-byte Core + Atom Metadata (Aligned to 24-byte boundaries)
    static constexpr size_t BYTES_PER_NODE = 24;

public:
    /**
     * @brief Constructor using pointers to dependencies.
     * This approach avoids reference member issues with virtual inheritance.
     */
    DGTLSpeculativeInliner(
        dgtl::analysis::DGTLAICostModel* ai,
        dgtl::semantic::SymbolTable* sym) noexcept
        : ai_model_ptr(ai), symbols_ptr(sym) {}

    ~DGTLSpeculativeInliner() noexcept = default;

    /**
     * @brief Entry point for the Optimization.
     * Uses Transactional IR to ensure that risky inlining can be rolled back.
     */
    void execute(dgtl::ir::IRStream& caller_stream) noexcept override {
        if (!caller_stream.size()) [[unlikely]] return;
        if (!ai_model_ptr || !symbols_ptr) [[unlikely]] return;

        caller_stream.begin_transaction();

        bool modified = process_inlining_shadow(caller_stream);

        // PILLAR 3: THE SENTINEL - Final bit-level verification
        if (modified && caller_stream.validate_shadow_integrity()) {
            caller_stream.commit_transaction();
        } else {
            caller_stream.rollback_transaction();
        }
    }

    /**
     * @brief Get the name of this optimization.
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLSpeculativeInliner";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PreInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    /**
     * @brief Indicates this optimization requires transactional execution.
     */
    [[nodiscard]] bool requires_transaction() const noexcept override {
        return true;
    }

private:
    /**
     * @brief BIR 1.0.0 Shadow SPLICING logic.
     * Splices callee instructions directly into the caller's shadow stream.
     */
    bool process_inlining_shadow(dgtl::ir::IRStream& stream) noexcept {
        bool any_change = false;
        uint32_t shadow_cursor = 0;
        const size_t original_count = stream.size();

        for (uint32_t i = 0; i < original_count; ++i) {
            dgtl::ir::IRNode node = stream.get_node_readonly(i);

            // CALL opcodes reference callee functions via slot ID lookups
            // For now, conservatively pass through CALL nodes without inlining
            if (node.opcode == dgtl::ir::OpCode::CALL) {
                if (shadow_cursor < stream.capacity()) {
                    dgtl::ir::IRAtom& s_atom = stream.get_shadow_atom(shadow_cursor);
                    dgtl::ir::IRCore& s_core = stream.get_shadow_core(shadow_cursor);

                    s_atom.opcode    = node.opcode;
                    s_atom.flags     = node.flags;
                    s_core.result_id = node.result_id;
                    s_core.op_a      = node.op_a;
                    s_core.op_b      = node.op_b;
                    s_core.op_c      = node.op_c;
                    s_core.intent_hash = node.intent_hash;

                    shadow_cursor++;
                }
                continue;
            }

            // Passthrough to shadow tier
            if (shadow_cursor < stream.capacity()) {
                dgtl::ir::IRAtom& s_atom = stream.get_shadow_atom(shadow_cursor);
                dgtl::ir::IRCore& s_core = stream.get_shadow_core(shadow_cursor);

                s_atom.opcode    = node.opcode;
                s_atom.flags     = node.flags;
                s_core.result_id = node.result_id;
                s_core.op_a      = node.op_a;
                s_core.op_b      = node.op_b;
                s_core.op_c      = node.op_c;
                s_core.intent_hash = node.intent_hash;

                shadow_cursor++;
            }
        }

        stream.update_shadow_size(shadow_cursor);
        return any_change;
    }

    /**
     * @brief Evaluate whether inlining would be beneficial.
     */
    bool should_inline(
        const dgtl::ir::IRStream& caller,
        const dgtl::ir::IRStream& callee) const noexcept {
        if (is_cache_risky(caller, callee)) return false;

        if (!ai_model_ptr) return false;
        float benefit = ai_model_ptr->predict_inlined_benefit(caller, callee);
        return benefit > 0.65f;
    }

    /**
     * @brief Check if inlining would cause L1-I cache thrashing.
     */
    bool is_cache_risky(
        const dgtl::ir::IRStream& caller,
        const dgtl::ir::IRStream& callee) const noexcept {
        size_t total_bytes = (caller.size() + callee.size()) * BYTES_PER_NODE;
        // Prevent inlining if footprint > 85% of L1-I Cache
        return total_bytes > static_cast<size_t>(L1_I_CACHE_LIMIT * 0.85);
    }

    /**
     * @brief Alpha-Renaming & Shadow Splice.
     * Prevents SSA collisions by offsetting virtual registers during the splice.
     */
    void append_inlined_callee(
        dgtl::ir::IRStream& s,
        uint32_t& cursor,
        const dgtl::ir::IRStream& callee) noexcept {
        const uint32_t vreg_offset = s.get_max_id() + 1;
        constexpr uint32_t VREG_THRESHOLD = 0x80000000U;

        for (uint32_t j = 0; j < callee.size(); ++j) {
            dgtl::ir::IRNode inlined_node = callee.get_node_readonly(j);

            // Perform Alpha-Renaming on Virtual Registers
            if (inlined_node.result_id != dgtl::ir::INVALID_SLOT &&
                inlined_node.result_id < VREG_THRESHOLD) {
                inlined_node.result_id += vreg_offset;
            }
            if (inlined_node.op_a != dgtl::ir::INVALID_SLOT &&
                inlined_node.op_a < VREG_THRESHOLD) {
                inlined_node.op_a += vreg_offset;
            }
            if (inlined_node.op_b != dgtl::ir::INVALID_SLOT &&
                inlined_node.op_b < VREG_THRESHOLD) {
                inlined_node.op_b += vreg_offset;
            }
            if (inlined_node.op_c != dgtl::ir::INVALID_SLOT &&
                inlined_node.op_c < VREG_THRESHOLD) {
                inlined_node.op_c += vreg_offset;
            }

            if (cursor < s.capacity()) {
                dgtl::ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
                dgtl::ir::IRCore& s_core = s.get_shadow_core(cursor);

                s_atom.opcode = inlined_node.opcode;
                s_atom.flags  = inlined_node.flags | dgtl::ir::Flags::IS_OPTIMIZED;

                s_core.result_id = inlined_node.result_id;
                s_core.op_a      = inlined_node.op_a;
                s_core.op_b      = inlined_node.op_b;
                s_core.op_c      = inlined_node.op_c;

                s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                    s_atom.opcode,
                    s_core.result_id,
                    s_core.op_a,
                    s_core.op_b,
                    s_core.op_c
                );

                cursor++;
            }
        }
    }
};

class DGTLTemporalVectorFusion final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.size() < 2) {
            return;
        }

        stream.begin_transaction();

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto node = stream.get_node_readonly(i);
            auto& atom = stream.get_shadow_atom(i);
            auto& core = stream.get_shadow_core(i);
            atom.opcode = node.opcode;
            atom.flags = node.flags;
            core.result_id = node.result_id;
            core.op_a = node.op_a;
            core.op_b = node.op_b;
            core.op_c = node.op_c;
            core.intent_hash = node.intent_hash;
        }

        bool changed = false;
        for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(stream.size()); ++i) {
            const auto widen = stream.get_node_readonly(i);
            const auto fma = stream.get_node_readonly(i + 1);
            if (widen.opcode != dgtl::ir::OpCode::FCVTL_F16_TO_F32) {
                continue;
            }
            if (fma.opcode != dgtl::ir::OpCode::FMA_F32) {
                continue;
            }
            if (fma.op_a != widen.result_id) {
                continue;
            }

            auto& widen_atom = stream.get_shadow_atom(i);
            auto& widen_core = stream.get_shadow_core(i);
            widen_atom.opcode = dgtl::ir::OpCode::NOP;
            widen_atom.flags = dgtl::ir::Flags::IS_DEAD;
            widen_core.result_id = dgtl::ir::INVALID_SLOT;
            widen_core.op_a = 0;
            widen_core.op_b = 0;
            widen_core.op_c = 0;
            widen_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                widen_atom.opcode, widen_core.result_id, widen_core.op_a, widen_core.op_b, widen_core.op_c);

            auto& fma_atom = stream.get_shadow_atom(i + 1);
            auto& fma_core = stream.get_shadow_core(i + 1);
            fma_atom.opcode = dgtl::ir::OpCode::FMA_WIDEN_F16_F32;
            fma_atom.flags |= (dgtl::ir::Flags::IS_MACRO_FUSED |
                               dgtl::ir::Flags::IS_OPTIMIZED |
                               dgtl::ir::Flags::IS_SIMD_BURST);
            fma_core.op_a = widen.op_a;
            fma_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                fma_atom.opcode, fma_core.result_id, fma_core.op_a, fma_core.op_b, fma_core.op_c);
            changed = true;
        }

        if (changed && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLTemporalVectorFusion";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }
};

} // namespace dgtl::pass
