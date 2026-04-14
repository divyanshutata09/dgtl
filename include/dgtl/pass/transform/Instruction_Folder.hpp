#pragma once
#include <optional>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <algorithm>   // ✅ for std::fill
#include <cstddef>     // ✅ for size_t

#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Verified_Formal.hpp"

namespace dgtl::pass {

class DGTLInstructionFolder {
    dgtl::core::DGTLFormalVerifier& formal_verifier;

    // 🔥 ID → constant tracking
    std::vector<std::optional<uint64_t>> const_table;

public:
    explicit DGTLInstructionFolder(dgtl::core::DGTLFormalVerifier& fv,
                                   size_t max_ids = 1048576) noexcept
        : formal_verifier(fv)
    {
        const_table.resize(max_ids);
    }

    void fold_stream(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size == 0) [[unlikely]] return;

        std::fill(const_table.begin(), const_table.end(), std::nullopt);

        stream.begin_transaction();
        bool transaction_valid = true;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::NOP) {
                sync_and_finalize(stream, i, node);
                continue;
            }

            auto collapsed = try_fold_node(node);

            auto simpler = (!collapsed)
                ? formal_verifier.find_minimal_equivalent(node)
                : std::nullopt;

            if (collapsed || simpler) {
                ir::IRAtom& s_atom = stream.get_shadow_atom(i);
                ir::IRCore& s_core = stream.get_shadow_core(i);

                if (collapsed) {
                    s_atom.opcode = ir::OpCode::LOAD_IMM;
                    s_atom.flags  = ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST;

                    s_core.result_id = node.result_id;
                    s_core.op_a = static_cast<uint32_t>(collapsed->low);
                    s_core.op_b = static_cast<uint32_t>(collapsed->high);
                    s_core.op_c = ir::INVALID_SLOT;

                    if (node.result_id < const_table.size()) {
                        const_table[node.result_id] = static_cast<uint64_t>(collapsed->low);
                    }

                } else {
                    s_atom.opcode = simpler->opcode;
                    s_atom.flags  = simpler->flags | ir::Flags::IS_MACRO_FUSED;

                    s_core.result_id = node.result_id;
                    s_core.op_a = simpler->op_a;
                    s_core.op_b = simpler->op_b;
                    s_core.op_c = simpler->op_c;
                }

                std::atomic_signal_fence(std::memory_order_release);
                finalize_shadow_node(stream, i);
            } 
            else {
                propagate_const(node);
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

    void propagate_const(const ir::IRNode& node) noexcept {
        if (node.opcode == ir::OpCode::LOAD_IMM) {
            if (node.result_id < const_table.size()) {
                const_table[node.result_id] = static_cast<uint64_t>(node.op_a);
            }
        }
    }

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

        core.intent_hash =
            dgtl::core::DGTLFormalVerifier::compute_master_hash_bits(
                atom.opcode, core.op_a, core.op_b, core.op_c
            );
    }

    std::optional<dgtl::core::ConstantResult>
    try_fold_node(const ir::IRNode& node) noexcept {

        auto get_const = [&](uint32_t id) -> std::optional<uint64_t> {
            if (id < const_table.size()) return const_table[id];
            return std::nullopt;
        };

        auto a = get_const(node.op_a);
        auto b = get_const(node.op_b);

        switch (node.opcode) {

            case ir::OpCode::ADD_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a + *b), 0
                    };
                break;

            case ir::OpCode::MUL_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a * *b), 0
                    };
                break;

            case ir::OpCode::SUB_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a - *b), 0
                    };
                if (node.op_a == node.op_b && !node.is_volatile())
                    return dgtl::core::ConstantResult{0, 0};
                break;

            case ir::OpCode::AND_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a & *b), 0
                    };
                break;

            case ir::OpCode::OR_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a | *b), 0
                    };
                break;

            case ir::OpCode::XOR_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a ^ *b), 0
                    };
                break;

            case ir::OpCode::DIV_I32:
                if (a && b && *b != 0)
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>(*a / *b), 0
                    };
                break;

            default:
                return formal_verifier.solve_static_math(node);
        }

        return std::nullopt;
    }
};

} // namespace dgtl::pass

