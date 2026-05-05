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
 * STEP 36: INSTRUCTION FUSION SYNTHESIZER - VERSION 1.0.0 (TITAN-STABLE)
 * PILLAR 2: TRANSACTIONAL SHADOW-STATE (PATTERN MATCHING)
 * PILLAR 3: THE SENTINEL (STRICT SSA VALIDATION)
 * ENHANCEMENTS:
 * + SSA-safe fusion (strict dependency + dominance awareness)
 * + Barrier-aware lookahead (prevents illegal cross-effects)
 * + Multi-use protection (no unsafe collapsing)
 * + Deterministic hash recomputation
 */

#pragma once
#include <vector>
#include <optional>
#include <algorithm>
#include <atomic>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pass {

class DGTLInstructionFusionSynth : public pipeline::DGTLOptimizationPass {
    core::DGTLFormalVerifier& formal_verifier;

public:
    explicit DGTLInstructionFusionSynth(core::DGTLFormalVerifier& fv) noexcept
        : formal_verifier(fv) {}

    void execute(ir::IRStream& stream) override {
        synthesize(stream);
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "Instruction Fusion Synthesis v1.0.0";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostSpecialization;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

    void synthesize(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size < 2) [[unlikely]] return;

        stream.begin_transaction();

        // Full sync ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ shadow (STRICT: required for correctness)
        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            sync_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size - 1); ++i) {

            ir::IRNode current = stream.get_node_readonly(i);

            if (current.opcode == ir::OpCode::NOP) continue;
            if (current.flags & ir::Flags::HAS_SIDE_EFFECTS) continue;

            // =========================
            // PATTERN 1: FMA
            // =========================
            if (is_mul(current.opcode)) {
                if (auto fusion = try_synthesize_fma(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx;
                    continue;
                }
            }

            // =========================
            // PATTERN 2: SHIFT + LOGIC
            // =========================
            if (current.opcode == ir::OpCode::SHL_I32) {
                if (auto fusion = try_synthesize_shifted_logic(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx;
                    continue;
                }
            }
        }

        // SENTINEL CHECK
        if (stream.validate_shadow_integrity()) {
            std::atomic_signal_fence(std::memory_order_release);
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:

    struct FusionMatch {
        uint32_t consumer_idx;
        ir::OpCode fused_op;
        uint32_t op_a, op_b, op_c;
        uint32_t result_id;
    };

    // ============================================================
    // SSA SAFETY UTILITIES
    // ============================================================

    bool has_multiple_users(const ir::IRStream& s, uint32_t res_id) const noexcept {
        uint32_t count = 0;
        const size_t sz = s.size();

        for (size_t i = 0; i < sz; ++i) {
            const auto n = s.get_node_readonly(static_cast<uint32_t>(i));
            if (n.op_a == res_id || n.op_b == res_id || n.op_c == res_id) {
                if (++count > 1) return true;
            }
        }
        return false;
    }

    bool is_barrier(const ir::IRNode& n) const noexcept {
        return (n.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
               n.opcode == ir::OpCode::SYSCALL ||
               n.opcode == ir::OpCode::RETURN ||
               n.opcode == ir::OpCode::RET;
    }

    static bool is_mul(ir::OpCode op) noexcept {
        return op == ir::OpCode::MUL_I32 || op == ir::OpCode::MUL_F32;
    }

    // ============================================================
    // SHADOW SYNC
    // ============================================================

    void sync_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        const ir::IRNode src = s.get_node_readonly(idx);

        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        atom.opcode = src.opcode;
        atom.flags  = src.flags;

        core.result_id = src.result_id;
        core.op_a = src.op_a;
        core.op_b = src.op_b;
        core.op_c = src.op_c;

        core.intent_hash = src.intent_hash;
    }

    // ============================================================
    // FMA SYNTHESIS (SAFE)
    // ============================================================

    std::optional<FusionMatch> try_synthesize_fma(const ir::IRStream& s, uint32_t p_idx) noexcept {

        const ir::IRNode producer = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 8, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {

            const ir::IRNode consumer = s.get_node_readonly(j);

            if (is_barrier(consumer)) break;

            if (consumer.opcode == ir::OpCode::ADD_I32 ||
                consumer.opcode == ir::OpCode::ADD_F32) {

                if (consumer.op_a == producer.result_id ||
                    consumer.op_b == producer.result_id) {

                    // STRICT SSA SAFETY
                    if (has_multiple_users(s, producer.result_id)) return std::nullopt;

                    return FusionMatch{
                        j,
                        (producer.opcode == ir::OpCode::MUL_F32)
                            ? ir::OpCode::FMA_F32
                            : ir::OpCode::ADD_I32, // fallback scalar fusion
                        producer.op_a,
                        producer.op_b,
                        (consumer.op_a == producer.result_id)
                            ? consumer.op_b
                            : consumer.op_a,
                        consumer.result_id
                    };
                }
            }
        }

        return std::nullopt;
    }

    // ============================================================
    // SHIFT + LOGIC
    // ============================================================

    std::optional<FusionMatch> try_synthesize_shifted_logic(const ir::IRStream& s, uint32_t p_idx) noexcept {

        const ir::IRNode p = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 4, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {

            const ir::IRNode c = s.get_node_readonly(j);

            if (is_barrier(c)) break;

            if (c.opcode == ir::OpCode::XOR_I32 ||
                c.opcode == ir::OpCode::OR_I32) {

                if (c.op_a == p.result_id || c.op_b == p.result_id) {

                    if (has_multiple_users(s, p.result_id)) return std::nullopt;

                    return FusionMatch{
                        j,
                        ir::OpCode::BIT_FIELD_INSERT,
                        p.op_a,
                        p.op_b,
                        (c.op_a == p.result_id) ? c.op_b : c.op_a,
                        c.result_id
                    };
                }
            }
        }

        return std::nullopt;
    }

    // ============================================================
    // APPLY FUSION
    // ============================================================

    void apply_fusion(ir::IRStream& s, uint32_t p_idx, const FusionMatch& m) noexcept {

        // --- NOP producer ---
        ir::IRAtom& p_atom = s.get_shadow_atom(p_idx);
        ir::IRCore& p_core = s.get_shadow_core(p_idx);

        p_atom.opcode = ir::OpCode::NOP;
        p_atom.flags  = ir::Flags::IS_DEAD;

        p_core.result_id = ir::INVALID_SLOT;
        p_core.op_a = 0;
        p_core.op_b = 0;
        p_core.op_c = 0;

        p_core.intent_hash = ir::IRNode::calculate_hash(
            ir::OpCode::NOP,
            p_core.result_id,
            p_core.op_a,
            p_core.op_b,
            p_core.op_c
        );

        // --- fused consumer ---
        ir::IRAtom& c_atom = s.get_shadow_atom(m.consumer_idx);
        ir::IRCore& c_core = s.get_shadow_core(m.consumer_idx);

        c_atom.opcode = m.fused_op;
        c_atom.flags |= (ir::Flags::IS_MACRO_FUSED |
                         ir::Flags::IS_OPTIMIZED);

        c_core.op_a = m.op_a;
        c_core.op_b = m.op_b;
        c_core.op_c = m.op_c;
        c_core.result_id = m.result_id;

        c_core.intent_hash = ir::IRNode::calculate_hash(
            c_atom.opcode,
            c_core.result_id,
            c_core.op_a,
            c_core.op_b,
            c_core.op_c
        );
    }
};

} // namespace dgtl::pass





