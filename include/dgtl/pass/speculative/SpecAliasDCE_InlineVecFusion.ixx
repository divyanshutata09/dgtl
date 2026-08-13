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
#include <cstring>
#include <stdexcept>

// Bind directly to your core intermediate passes partition space
export module dgtl.ir_core.speculate_alias_dce_inliner;

import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir_core.stream;
import dgtl.ir_core.ai_cost_model;
import dgtl.ir_core.liveness_memory_dependency_unified;
import dgtl.ir_core.semantic;

import dgtl.op_traits; 

// Load local configuration module fragments safely
import dgtl.ir_core.pass_manager_config;

// Bring in traits/namespaces for local use
namespace iface = dgtl::ir::interfaces;

export namespace dgtl::pass {

// =============================================================================
// STEP 38: SPECULATIVE ALIAS HOISTER (LICM+)
// =============================================================================

class DGTLSpeculativeAliasHoister {
    dgtl::analysis::DGTLMemorySmasher& smasher;
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    DGTLSpeculativeAliasHoister(dgtl::analysis::DGTLMemorySmasher& s, dgtl::analysis::DGTLAICostModel& ai) noexcept
        : smasher(s), ai_model(ai) {}

    void execute(ir::IRStream& stream) {
        if (stream.size() == 0) [[unlikely]] return;

        stream.begin_transaction();
        if (!stream.validate_shadow_integrity() || !smasher.validate_shadow_maps()) {
            stream.rollback_transaction();
            return;
        }
        const double cost_bias = ai_model.get_compilation_overhead_bias();
        (void)cost_bias;
        stream.commit_transaction();
    }

    void hoist_speculative_invariants(ir::IRStream& loop_body, ir::IRStream& pre_header) {
        if (loop_body.size() == 0) return;

        loop_body.begin_transaction();
        pre_header.begin_transaction();

        const size_t body_count = loop_body.size();
        for (uint32_t i = 0; i < static_cast<uint32_t>(body_count); ++i) {
            ir::IRNode node = loop_body.get_node_readonly(i);

            const bool is_memory_read = iface::has_trait(node.opcode, iface::OpTrait::IsMemoryRead);
            if (is_memory_read && !(node.flags & ir::Flags::IS_VOLATILE)) {
                bool should_hoist = false;

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
                    sync_shadow_node(loop_body, i, node);
                }
            } else {
                sync_shadow_node(loop_body, i, node);
            }
        }

        loop_body.set_shadow_count(body_count);

        if (!loop_body.validate_shadow_integrity() || !pre_header.validate_shadow_integrity()) {
            loop_body.rollback_transaction();
            pre_header.rollback_transaction();
            return;
        }

        loop_body.commit_transaction();
        pre_header.commit_transaction();
    }

private:
    static void sync_shadow_node(ir::IRStream& s, uint32_t idx, const ir::IRNode& node) noexcept {
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

    static void inject_runtime_alias_guard_transactional(ir::IRStream& pre, const ir::IRNode& load_node) {
        const uint32_t cursor = pre.reserve_shadow_slot();
        if (cursor == ir::INVALID_SLOT || cursor >= pre.capacity()) return;

        ir::IRAtom& s_atom = pre.get_shadow_atom(cursor);
        ir::IRCore& s_core = pre.get_shadow_core(cursor);

        s_atom = {ir::OpCode::GUARD_ALIAS, ir::Flags::NONE};
        s_core.logic_flags |= ir::HintFlags::RequiresDepassTarget;
        s_core = {ir::INVALID_SLOT, load_node.op_a, 0, 0, 0, 0, 0};

        s_core.intent_hash = ir::IRNode::calculate_hash(
            s_atom.opcode,
            s_core.result_id,
            s_core.op_a,
            s_core.op_b,
            s_core.op_c
        );
    }

    static void move_to_preheader_transactional(ir::IRStream& body, uint32_t body_idx, ir::IRStream& pre) {
        const uint32_t pre_cursor = pre.reserve_shadow_slot();
        if (pre_cursor == ir::INVALID_SLOT || pre_cursor >= pre.capacity()) return;

        ir::IRNode original = body.get_node_readonly(body_idx);

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

    void execute(ir::IRStream& stream) override {
        if (stream.size() == 0) [[unlikely]] return;

        if (!internal_model) {
            (void)stream.validate_shadow_integrity();
            return;
        }

        stream.begin_transaction();

        const bool success = prune_and_reorder_transactional(stream, *internal_model);

        if (!success) {
            stream.rollback_transaction();
        } else if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
        } else {
            stream.commit_transaction();
        }
    }

private:
    bool prune_and_reorder_transactional(ir::IRStream& stream, const analysis::DGTLAICostModel& ai_model) {
        std::vector<BlockProfile> blocks = analyze_cfg_probability(stream, ai_model);
        if (blocks.empty()) return false;

        auto frozen_it = std::stable_partition(blocks.begin(), blocks.end(), [](const auto& b) {
            return b.probability > 0.0001f;
        });

        uint32_t shadow_cursor = 0;

        for (auto it = blocks.begin(); it != frozen_it; ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, true);
        }

        align_shadow_to_cache_line(stream, shadow_cursor);

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
                if (node.logic_flags & ir::HintFlags::BranchUnlikely) prob = 0.0000001f;
                else if (node.logic_flags & ir::HintFlags::BranchLikely) prob = 1.0f;

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
        auto& s_core = s.get_shadow_core(target_idx);

        const bool is_term =
            iface::has_trait(s_atom.opcode, iface::OpTrait::IsTerminator) &&
            iface::implements_interface(s_atom.opcode, iface::OpInterface::ControlFlow);

        if (is_term) {
            if (likely) {
                s_core.logic_flags |= ir::HintFlags::BranchLikely;
                s_core.logic_flags &= ~ir::HintFlags::BranchUnlikely;
            } else {
                s_core.logic_flags |= ir::HintFlags::BranchUnlikely;
                s_core.logic_flags &= ~ir::HintFlags::BranchLikely;
            }
            finalize_shadow_node(s, target_idx);
        }
    }
};

// =============================================================================
// STEP 32: AI-GUIDED SPECULATIVE INLINER (TRANSACTIONAL)
// =============================================================================

class DGTLSpeculativeInliner final : public dgtl::pipeline::DGTLOptimizationPass {
private:
    dgtl::analysis::DGTLAICostModel* ai_model_ptr;
    dgtl::semantic::SymbolTable* symbols_ptr;

    static constexpr size_t L1_I_CACHE_LIMIT = 32768;
    static constexpr size_t BYTES_PER_NODE = 24;

public:
    DGTLSpeculativeInliner(
        dgtl::analysis::DGTLAICostModel* ai,
        dgtl::semantic::SymbolTable* sym) noexcept
        : ai_model_ptr(ai), symbols_ptr(sym) {}

    ~DGTLSpeculativeInliner() noexcept = default;

    void execute(dgtl::ir::IRStream& caller_stream) noexcept override {
        if (!caller_stream.size()) [[unlikely]] return;
        if (!ai_model_ptr || !symbols_ptr) [[unlikely]] return;

        caller_stream.begin_transaction();

        bool modified = process_inlining_shadow(caller_stream);

        if (modified && caller_stream.validate_shadow_integrity()) {
            caller_stream.commit_transaction();
        } else {
            caller_stream.rollback_transaction();
        }
    }

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

    [[nodiscard]] bool requires_transaction() const noexcept override {
        return true;
    }

private:
    bool process_inlining_shadow(dgtl::ir::IRStream& stream) noexcept {
        bool any_change = false;
        uint32_t shadow_cursor = 0;
        const size_t original_count = stream.size();

        for (uint32_t i = 0; i < original_count; ++i) {
            dgtl::ir::IRNode node = stream.get_node_readonly(i);

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
                    any_change = true;
                }
                continue;
            }

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

    bool should_inline(
        const dgtl::ir::IRStream& caller,
        const dgtl::ir::IRStream& callee) const noexcept {
        if (is_cache_risky(caller, callee)) return false;

        if (!ai_model_ptr) return false;
        const float caller_cost = static_cast<float>(caller.size());
        const float callee_cost = static_cast<float>(callee.size());
        float benefit = ai_model_ptr->predict_inlined_benefit_flat(
            caller_cost,
            callee.size(),
            callee_cost);
        return benefit > 0.65f;
    }

    static bool is_cache_risky(
        const dgtl::ir::IRStream& caller,
        const dgtl::ir::IRStream& callee) noexcept {
        size_t total_bytes = (caller.size() + callee.size()) * BYTES_PER_NODE;
        return total_bytes > static_cast<size_t>(L1_I_CACHE_LIMIT * 0.85);
    }

    void append_inlined_callee(
        dgtl::ir::IRStream& s,
        uint32_t& cursor,
        const dgtl::ir::IRStream& callee) noexcept {
        const uint32_t vreg_offset = s.get_max_id() + 1;
        constexpr uint32_t VREG_THRESHOLD = 0x80000000U;

        for (uint32_t j = 0; j < callee.size(); ++j) {
            dgtl::ir::IRNode inlined_node = callee.get_node_readonly(j);

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

class DGTLTemporalVectorFusion : public dgtl::pipeline::DGTLOptimizationPass {
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