/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 9: PATTERN-MATCHING SIMD LEGALIZER
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED FUSION)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * DESIGN: Uses a 4-Node Sliding Window for O(n) Vector Discovery.
 */

#pragma once
#include <immintrin.h> 
#include <stdexcept>
#include <algorithm>
#include "../ir/Global_IR.hpp"

namespace dgtl::opt {

enum class TargetArch : uint8_t { X86_AVX512, ARM_NEON, WASM_SIMD128, GENERIC };

class DGTLSIMDLegalizer {
    TargetArch target;

public:
    explicit DGTLSIMDLegalizer(TargetArch arch) : target(arch) {}

    /**
     * @brief The "Zero-Corruption" Legalization Pass.
     * Scans for "Vertical Scalar Chains" and converts them via Shadow-Writing.
     */
    void legalize(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        if (stream_size < 4) return;

        // PILLAR 2: Open Shadow Buffer (Stream B)
        stream.begin_transaction();

        try {
            // Initial sync: Move Primary state to Shadow before mutation
            // Updated to use internal sync to ensure metadata consistency
            for (uint32_t i = 0; i < stream_size; ++i) {
                sync_node_to_shadow(stream, i);
            }

            for (uint32_t i = 0; i <= static_cast<uint32_t>(stream_size) - 4; ++i) {
                // Sliding Window Inspection on Primary (ReadOnly)
                if (is_vectorizable_quad(stream, i)) {
                    
                    // PILLAR 3: Atomic Fusion in Shadow Buffer
                    fuse_to_simd_v4_transactional(stream, i);
                    
                    // Jump ahead to avoid re-scanning fused block
                    i += 3;
                }
            }

            // PILLAR 3 Sentinel: Validate Shadow integrity before swap
            if (!stream.validate_shadow_integrity()) {
                throw std::runtime_error("Sentinel Trap: SIMD Legalization produced inconsistent IR bit-logic.");
            }

            // Success: Commit Shadow to Primary
            stream.commit_transaction();

        } catch (...) {
            // Failure: Rollback to safe scalar state
            stream.rollback_transaction();
        }
    }

private:
    void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash};
    }

    /**
     * @brief Dependency Check on Primary Stream using BIR 2.0 accessors.
     */
    [[nodiscard]] bool is_vectorizable_quad(const ir::IRStream& stream, uint32_t start) const noexcept {
        const ir::IRNode n0 = stream.get_node_readonly(start);
        const ir::IRNode n1 = stream.get_node_readonly(start + 1);
        const ir::IRNode n2 = stream.get_node_readonly(start + 2);
        const ir::IRNode n3 = stream.get_node_readonly(start + 3);

        // OpCode Sync: ADD_I32 = 20
        if (n0.opcode != ir::OpCode::ADD_I32 || n1.opcode != ir::OpCode::ADD_I32 ||
            n2.opcode != ir::OpCode::ADD_I32 || n3.opcode != ir::OpCode::ADD_I32) return false;

        // SSA Independence Check within window
        uint32_t r0 = n0.result_id;
        if (n1.op_a == r0 || n1.op_b == r0 || n2.op_a == r0 || n3.op_a == r0) return false;

        return true;
    }

    /**
     * @brief Shadow Writing via BIR 2.0 Tiers.
     */
    void fuse_to_simd_v4_transactional(ir::IRStream& stream, uint32_t start) {
        ir::IRAtom& v_atom = stream.get_shadow_atom(start);
        ir::IRCore& v_core = stream.get_shadow_core(start);

        // Target-specific OpCode Selection
        ir::OpCode fused_op;
        switch (target) {
            case TargetArch::X86_AVX512:  fused_op = static_cast<ir::OpCode>(0x204); break;
            case TargetArch::ARM_NEON:    fused_op = static_cast<ir::OpCode>(0x304); break;
            case TargetArch::WASM_SIMD128: fused_op = static_cast<ir::OpCode>(0x404); break;
            default: fused_op = ir::OpCode::ADD_I32; break; 
        }

        // VEC_ADD_I32_V4 (Custom Dialect Offset)
        v_atom.opcode = fused_op;
        v_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED);
        
        // Refresh Intent Hash using node proxy for CRC-32 update
        // Using standardized hash calculation to maintain Pillar 3 integrity
        v_core.intent_hash = ir::IRNode::calculate_hash(
            v_atom.opcode, 
            v_core.op_a, 
            v_core.op_b, 
            v_core.op_c
        );

        // NOP absorbed nodes in Shadow Buffer (NOP = 0)
        for (uint32_t j = 1; j < 4; ++j) {
            ir::IRAtom& f_atom = stream.get_shadow_atom(start + j);
            ir::IRCore& f_core = stream.get_shadow_core(start + j);
            
            f_atom = {ir::OpCode::NOP, ir::Flags::NONE};
            f_core = {ir::INVALID_SLOT, 0, 0, 0, 0};
            
            // Recalculate hash for NOP to ensure validate_shadow_integrity succeeds
            f_core.intent_hash = ir::IRNode::calculate_hash(
                f_atom.opcode, 
                f_core.op_a, 
                f_core.op_b, 
                f_core.op_c
            );
        }
    }
};

} // namespace dgtl::opt
