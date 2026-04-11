/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 31: FORMAL-SOLVER INSTRUCTION FOLDER
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING (SHADOW STREAM)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * VERSION 3.1: TITAN-STABLE (FORMAL-SYNC REVISION)
 */

#pragma once
#include <optional>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"

namespace dgtl::opt {

class DGTLInstructionFolder {
    core::DGTLFormalVerifier& formal_verifier;

public:
    explicit DGTLInstructionFolder(core::DGTLFormalVerifier& fv) noexcept 
        : formal_verifier(fv) {}

    void fold_stream(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size == 0) [[unlikely]] return;

        stream.begin_transaction();
        bool transaction_valid = true;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);
            
            if (node.opcode == ir::OpCode::NOP) {
                sync_and_finalize(stream, i, node);
                continue;
            }

            auto collapsed = try_fold_node(node);
            auto simpler = (collapsed) ? std::nullopt : formal_verifier.find_minimal_equivalent(node);

            if (collapsed || simpler) {
                ir::IRAtom& s_atom = stream.get_shadow_atom(i);
                ir::IRCore& s_core = stream.get_shadow_core(i);

                if (collapsed) {
                    s_atom.opcode = ir::OpCode::LOAD_IMM; 
                    s_atom.flags = node.flags | ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST;
                    s_core.result_id = node.result_id;
                    s_core.op_a = collapsed->low;
                    s_core.op_b = collapsed->high;
                    s_core.op_c = ir::INVALID_SLOT;
                } else {
                    s_atom.opcode = simpler->opcode;
                    s_atom.flags  = simpler->flags | ir::Flags::IS_MACRO_FUSED;
                    s_core.result_id = simpler->result_id;
                    s_core.op_a = simpler->op_a;
                    s_core.op_b = simpler->op_b;
                    s_core.op_c = simpler->op_c;
                }

                std::atomic_signal_fence(std::memory_order_release);
                finalize_shadow_node(stream, i);
            } else {
                sync_and_finalize(stream, i, node);
            }
        }

        if (transaction_valid && stream.validate_shadow_integrity()) {
            stream.commit_transaction(); 
        } else {
            stream.rollback_transaction();
        }
    }

private:
    void sync_and_finalize(ir::IRStream& stream, uint32_t i, const ir::IRNode& node) noexcept {
        ir::IRAtom& s_atom = stream.get_shadow_atom(i);
        ir::IRCore& s_core = stream.get_shadow_core(i);
        
        s_atom.opcode = node.opcode;
        s_atom.flags  = node.flags;
        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;

        finalize_shadow_node(stream, i);
    }

    void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        core.intent_hash = core::DGTLFormalVerifier::compute_master_hash_bits(
            atom.opcode, core.op_a, core.op_b, core.op_c
        );
    }

    std::optional<core::ConstantResult> try_fold_node(const ir::IRNode& node) noexcept {
        const bool a_const = (node.flags & ir::Flags::IS_SPEC_CONST);
        const bool b_const = (node.flags & ir::Flags::IS_CONST);

        switch (node.opcode) {
            case ir::OpCode::ADD_I32:
                if (a_const && b_const) return core::ConstantResult{ node.op_a + node.op_b, 0 };
                break;
            case ir::OpCode::MUL_I32:
                if (a_const && b_const) return core::ConstantResult{ node.op_a * node.op_b, 0 };
                break;
            case ir::OpCode::SUB_I32:
                if (node.op_a == node.op_b && !node.is_volatile()) return core::ConstantResult{ 0, 0 };
                break;
            default:
                return formal_verifier.solve_static_math(node);
        }
        return std::nullopt;
    }
};

} // namespace dgtl::opt