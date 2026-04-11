/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 36: INSTRUCTION FUSION SYNTHESIZER - VERSION 2.1 (BIR ALIGNED)
 * PILLAR 2: TRANSACTIONAL SHADOW-STATE (PATTERN MATCHING)
 * PILLAR 3: THE SENTINEL (STRICT SSA VALIDATION)
 * DESIGN: Collapses arithmetic chains into SIMD/Hardware-Specific Macros.
 */

#pragma once
#include <vector>
#include <optional>
#include <algorithm>
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"
#include "../pipeline/Pass_Manager.hpp"

namespace dgtl::opt {

/**
 * @class DGTLInstructionFusionSynth
 * @brief Orchestrates high-level instruction synthesis by collapsing 
 * multi-node SSA chains into single-node macro-ops (e.g., FMA, Logic-Shift).
 */
class DGTLInstructionFusionSynth : public pipeline::DGTLOptimizationPass {
    core::DGTLFormalVerifier& formal_verifier;

public:
    explicit DGTLInstructionFusionSynth(core::DGTLFormalVerifier& fv) noexcept 
        : formal_verifier(fv) {}

    /**
     * @brief Executes a fusion pass over the IR stream.
     * Uses the Shadow Stream to stage fused nodes before committing.
     */
    void execute(ir::IRStream& stream) override {
        synthesize(stream);
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "Instruction Fusion Synthesis";
    }

    /**
     * @brief Executes a fusion pass over the IR stream.
     * Uses the Shadow Stream to stage fused nodes before committing.
     */
    void synthesize(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size < 2) [[unlikely]] return;

        // PILLAR 2: Open Transactional Tier
        stream.begin_transaction();

        // Initial sync: Move all primary nodes to shadow for mutation
        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            sync_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size - 1); ++i) {
            ir::IRNode current = stream.get_node_readonly(i);
            
            if (current.opcode == ir::OpCode::NOP) continue;

            // Pattern 1: Fused Multiply-Add (FMA) Synthesis (BIR 2.1 Opcodes)
            if (current.opcode == ir::OpCode::MUL_I32 || current.opcode == ir::OpCode::MUL_F32) {
                if (auto fusion = try_synthesize_fma(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx; 
                    continue;
                }
            }

            // Pattern 2: Bit-Shift + Logic Fusion (HFT/Hashing Optimization)
            if (current.opcode == ir::OpCode::SHL_I32) {
                if (auto fusion = try_synthesize_shifted_logic(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx;
                    continue;
                }
            }
        }

        // PILLAR 3: Sentinel Integrity Check
        if (stream.validate_shadow_integrity()) {
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

    /**
     * @brief Fallback utility to check if a result_id is used by more than one consumer.
     * Necessary for SSA safety when collapsing nodes.
     */
    bool has_multiple_users(const ir::IRStream& s, uint32_t res_id) const noexcept {
        uint32_t use_count = 0;
        const size_t sz = s.size();
        for (size_t i = 0; i < sz; ++i) {
            const auto node = s.get_node_readonly(static_cast<uint32_t>(i));
            if (node.op_a == res_id || node.op_b == res_id || node.op_c == res_id) {
                if (++use_count > 1) return true;
            }
        }
        return false;
    }

    void sync_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        const ir::IRNode src = s.get_node_readonly(idx);
        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);
        
        atom.opcode = src.opcode;
        atom.flags = src.flags;
        core.result_id = src.result_id;
        core.op_a = src.op_a; 
        core.op_b = src.op_b; 
        core.op_c = src.op_c;
        core.intent_hash = src.intent_hash;
    }

    std::optional<FusionMatch> try_synthesize_fma(const ir::IRStream& s, uint32_t p_idx) noexcept {
        const ir::IRNode producer = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 8, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {
            const ir::IRNode consumer = s.get_node_readonly(j);
            
            if (consumer.opcode == ir::OpCode::ADD_I32 || consumer.opcode == ir::OpCode::ADD_F32) {
                if (consumer.op_a == producer.result_id || consumer.op_b == producer.result_id) {
                    
                    // FIXED: Using fallback check to avoid "red code" on .count_users
                    if (has_multiple_users(s, producer.result_id)) return std::nullopt;

                    return FusionMatch{
                        j,
                        (producer.opcode == ir::OpCode::MUL_F32) ? ir::OpCode::FMA_F32 : ir::OpCode::ADD_I32,
                        producer.op_a,
                        producer.op_b,
                        (consumer.op_a == producer.result_id) ? consumer.op_b : consumer.op_a,
                        consumer.result_id
                    };
                }
            }
            if (consumer.flags & ir::Flags::HAS_SIDE_EFFECTS) break;
        }
        return std::nullopt;
    }

    std::optional<FusionMatch> try_synthesize_shifted_logic(const ir::IRStream& s, uint32_t p_idx) noexcept {
        const ir::IRNode p = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 4, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {
            const ir::IRNode c = s.get_node_readonly(j);
            if (c.opcode == ir::OpCode::XOR_I32 || c.opcode == ir::OpCode::OR_I32) {
                if (c.op_a == p.result_id || c.op_b == p.result_id) {
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

    void apply_fusion(ir::IRStream& s, uint32_t p_idx, const FusionMatch& m) noexcept {
        // 1. NOP the Producer in Shadow Tier
        ir::IRAtom& p_atom = s.get_shadow_atom(p_idx);
        ir::IRCore& p_core = s.get_shadow_core(p_idx);
        p_atom.opcode = ir::OpCode::NOP;
        p_atom.flags = ir::Flags::NONE;
        p_core.op_a = 0; p_core.op_b = 0; p_core.op_c = ir::INVALID_SLOT;
        p_core.intent_hash = core::DGTLFormalVerifier::compute_master_hash_bits(ir::OpCode::NOP, 0, 0, 0);

        // 2. Synthesize Macro-Op in Consumer slot
        ir::IRAtom& c_atom = s.get_shadow_atom(m.consumer_idx);
        ir::IRCore& c_core = s.get_shadow_core(m.consumer_idx);
        
        c_atom.opcode = m.fused_op;
        c_atom.flags |= (ir::Flags::IS_MACRO_FUSED | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_OPTIMIZED);
        
        c_core.op_a = m.op_a;
        c_core.op_b = m.op_b;
        c_core.op_c = m.op_c;
        c_core.result_id = m.result_id;
        
        // 3. Update Master Hash for the new hardware intent
        c_core.intent_hash = core::DGTLFormalVerifier::compute_master_hash_bits(
            c_atom.opcode, c_core.op_a, c_core.op_b, c_core.op_c
        );
    }
};

} // namespace dgtl::opt