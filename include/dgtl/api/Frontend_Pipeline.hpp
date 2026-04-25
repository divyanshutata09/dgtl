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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dgtl/api/ABI.hpp"
#include "dgtl/api/ControlFlow.hpp"
#include "dgtl/api/Function.hpp"
#include "dgtl/api/FrontendHelpers.hpp"
#include "dgtl/ir/base/IR_Types.hpp"

// Fix for Windows.h defining CONST as a macro
#ifdef CONST
#undef CONST
#endif

namespace dgtl::core {

enum class FrontendInstructionKind : uint8_t {
    const_i64,
    add_i32,
    sub_i32,
    mul_i32,
    div_i32,
    mod_i32,
    cmp_eq,
    cmp_ne,
    cmp_lt,
    cmp_le,
    cmp_gt,
    cmp_ge,
    call,
    ret,
    label,
    jmp,
    br_if,
};

struct FrontendLoweringParameter {
    uint32_t value_id = dgtl::ir::INVALID_SLOT;
    std::string name;
    FrontendTypeHandle type_handle = 0;
    DGTLABIValueClass abi_class = DGTLABIValueClass::custom;
};

struct FrontendLoweringInstruction {
    FrontendInstructionKind kind = FrontendInstructionKind::const_i64;

    uint32_t result_value_id = dgtl::ir::INVALID_SLOT;
    FrontendTypeHandle type_handle = 0;

    uint64_t immediate = 0;

    uint32_t lhs_value_id = dgtl::ir::INVALID_SLOT;
    uint32_t rhs_value_id = dgtl::ir::INVALID_SLOT;

    std::string callee_name;
    std::vector<uint32_t> arg_value_ids;

    uint32_t ret_value_id = dgtl::ir::INVALID_SLOT;
};

struct FrontendLoweringFunction {
    std::string name;
    FrontendTypeHandle return_type_handle = 0;
    DGTLABIValueClass return_abi_class = DGTLABIValueClass::custom;

    std::vector<FrontendLoweringParameter> parameters;
    std::vector<FrontendLoweringInstruction> instructions;
};

struct FrontendLoweringResult {
    bool ok = false;
    std::string message;
    size_t emitted_nodes = 0;
};

class FrontendPipelineLowerer {
public:
    static FrontendLoweringResult lower_function(
        const FrontendLoweringFunction& fn,
        DGTLFunction& out_function,
        DGTLControlFlowGraph& out_cfg,
        DGTLABIRegistry* abi_registry = nullptr)
    {
        FrontendLoweringResult result;

        if (fn.name.empty()) {
            result.message = "function name is empty";
            return result;
        }

        out_function.set_return_type(fn.return_type_handle);

        DGTLFrontendLinkAPI link_api(out_function, out_cfg, abi_registry);

        std::unordered_map<uint32_t, uint32_t> value_to_slot;

        // -------- PARAMETERS --------
        for (const auto& param : fn.parameters) {
            if (param.value_id == dgtl::ir::INVALID_SLOT) {
                result.message = "parameter value_id is invalid";
                return result;
            }

            const uint32_t slot = link_api.add_parameter(
                param.name,
                param.type_handle,
                dgtl::ir::OpCode::NOP,
                param.abi_class
            );

            value_to_slot[param.value_id] = slot;
        }

        // -------- ABI --------
        if (abi_registry) {
            abi_registry->register_internal_signature(
                fn.name,
                fn.return_abi_class,
                fn.return_type_handle,
                link_api.abi_parameters()
            );
        }

        // -------- CFG --------
        const uint32_t entry = link_api.create_block(0);
        const uint32_t exit  = link_api.create_block(0);

        if (!link_api.add_edge(entry, exit)) {
            result.message = "failed to add CFG entry->exit edge";
            return result;
        }

        // -------- INSTRUCTIONS --------
        bool emitted_ret = false;

        for (const auto& ins : fn.instructions) {

            // ---- CONST ----
            if (ins.kind == FrontendInstructionKind::const_i64) {
                if (ins.result_value_id == dgtl::ir::INVALID_SLOT) {
                    result.message = "const_i64 requires result_value_id";
                    return result;
                }

                const uint32_t slot =
                    link_api.emit_const_i64(ins.immediate, ins.type_handle);

                value_to_slot[ins.result_value_id] = slot;
                continue;
            }

            // ---- BINARY OPS ----
            if (ins.kind == FrontendInstructionKind::add_i32 ||
                ins.kind == FrontendInstructionKind::sub_i32 ||
                ins.kind == FrontendInstructionKind::mul_i32 ||
                ins.kind == FrontendInstructionKind::div_i32 ||
                ins.kind == FrontendInstructionKind::mod_i32 ||
                ins.kind == FrontendInstructionKind::cmp_eq ||
                ins.kind == FrontendInstructionKind::cmp_ne ||
                ins.kind == FrontendInstructionKind::cmp_lt ||
                ins.kind == FrontendInstructionKind::cmp_le ||
                ins.kind == FrontendInstructionKind::cmp_gt ||
                ins.kind == FrontendInstructionKind::cmp_ge)
            {
                if (ins.result_value_id == dgtl::ir::INVALID_SLOT) {
                    result.message = "binary op requires result_value_id";
                    return result;
                }

                const auto lhs_it = value_to_slot.find(ins.lhs_value_id);
                const auto rhs_it = value_to_slot.find(ins.rhs_value_id);

                if (lhs_it == value_to_slot.end() ||
                    rhs_it == value_to_slot.end()) {
                    result.message = "binary op uses undefined operand";
                    return result;
                }

                const uint32_t lhs = lhs_it->second;
                const uint32_t rhs = rhs_it->second;

                // ---- SAFE CONSTANT FOLDING ----
                const auto& stream = out_function.stream();
                size_t node_count = stream.size();
                if (lhs < node_count && rhs < node_count) {
                    const auto& lhs_node = stream.get_node(lhs);
                    const auto& rhs_node = stream.get_node(rhs);
                    auto is_constlike = [](const auto& node) {
                        using dgtl::ir::OpCode;
                        return node.opcode == OpCode::CONST ||
                               node.opcode == OpCode::LOAD_IMM ||
                               node.opcode == OpCode::IMMEDIATE ||
                               (node.flags & dgtl::ir::Flags::IS_CONST) ||
                               (node.flags & dgtl::ir::Flags::IS_SPEC_CONST);
                    };
                    if (is_constlike(lhs_node) && is_constlike(rhs_node)) {
                        uint64_t l = lhs_node.op_b;
                        uint64_t r = rhs_node.op_b;
                        uint64_t res = 0;

                        if (ins.kind == FrontendInstructionKind::add_i32) res = l + r;
                        else if (ins.kind == FrontendInstructionKind::sub_i32) res = l - r;
                        else if (ins.kind == FrontendInstructionKind::mul_i32) res = l * r;
                        else if (ins.kind == FrontendInstructionKind::div_i32) { if (r != 0) res = static_cast<uint64_t>(static_cast<int64_t>(l) / static_cast<int64_t>(r)); }
                        else if (ins.kind == FrontendInstructionKind::mod_i32) { if (r != 0) res = static_cast<uint64_t>(static_cast<int64_t>(l) % static_cast<int64_t>(r)); }
                        else if (ins.kind == FrontendInstructionKind::cmp_eq) res = (l == r) ? 1 : 0;
                        else if (ins.kind == FrontendInstructionKind::cmp_ne) res = (l != r) ? 1 : 0;
                        else if (ins.kind == FrontendInstructionKind::cmp_lt) res = (static_cast<int64_t>(l) < static_cast<int64_t>(r)) ? 1 : 0;
                        else if (ins.kind == FrontendInstructionKind::cmp_le) res = (static_cast<int64_t>(l) <= static_cast<int64_t>(r)) ? 1 : 0;
                        else if (ins.kind == FrontendInstructionKind::cmp_gt) res = (static_cast<int64_t>(l) > static_cast<int64_t>(r)) ? 1 : 0;
                        else if (ins.kind == FrontendInstructionKind::cmp_ge) res = (static_cast<int64_t>(l) >= static_cast<int64_t>(r)) ? 1 : 0;

                        res &= 0xFFFFFFFFu;

                        const uint32_t slot =
                            link_api.emit_const_i64(res, ins.type_handle);

                        value_to_slot[ins.result_value_id] = slot;
                        continue;
                    }
                }

                dgtl::ir::OpCode opcode = dgtl::ir::OpCode::ADD_I32;

                if (ins.kind == FrontendInstructionKind::sub_i32)
                    opcode = dgtl::ir::OpCode::SUB_I32;
                else if (ins.kind == FrontendInstructionKind::mul_i32)
                    opcode = dgtl::ir::OpCode::MUL_I32;
                else if (ins.kind == FrontendInstructionKind::div_i32)
                    opcode = dgtl::ir::OpCode::DIV_I32;
                else if (ins.kind == FrontendInstructionKind::mod_i32)
                    opcode = dgtl::ir::OpCode::MOD_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_eq)
                    opcode = dgtl::ir::OpCode::CMP_EQ_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_ne)
                    opcode = dgtl::ir::OpCode::CMP_NE_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_lt)
                    opcode = dgtl::ir::OpCode::CMP_LT_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_le)
                    opcode = dgtl::ir::OpCode::CMP_LE_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_gt)
                    opcode = dgtl::ir::OpCode::CMP_GT_I32;
                else if (ins.kind == FrontendInstructionKind::cmp_ge)
                    opcode = dgtl::ir::OpCode::CMP_GE_I32;

                const uint32_t slot =
                    link_api.emit_binary(opcode, lhs, rhs, ins.type_handle);

                value_to_slot[ins.result_value_id] = slot;
                continue;
            }

            // ---- LABEL ----
            if (ins.kind == FrontendInstructionKind::label) {
                continue;
            }

            // ---- JMP ----
            if (ins.kind == FrontendInstructionKind::jmp) {
                continue;
            }

            // ---- BR_IF ----
            if (ins.kind == FrontendInstructionKind::br_if) {
                continue;
            }

            // ---- CALL ----
            if (ins.kind == FrontendInstructionKind::call) {
                if (ins.result_value_id == dgtl::ir::INVALID_SLOT) {
                    result.message = "call requires result_value_id";
                    return result;
                }

                if (ins.callee_name.empty()) {
                    result.message = "call requires callee_name";
                    return result;
                }

                std::vector<uint32_t> args;
                args.reserve(ins.arg_value_ids.size());

                for (uint32_t id : ins.arg_value_ids) {
                    auto it = value_to_slot.find(id);
                    if (it == value_to_slot.end()) {
                        result.message = "call uses undefined argument";
                        return result;
                    }
                    args.push_back(it->second);
                }

                const uint32_t call_slot =
                    out_function.define_temp(ins.type_handle, dgtl::ir::OpCode::CALL);

                std::string err;
                if (!link_api.emit_call(call_slot, ins.callee_name, args, &err)) {
                    result.message = err.empty() ? "call emission failed" : err;
                    return result;
                }

                value_to_slot[ins.result_value_id] = call_slot;
                continue;
            }

            // ---- RETURN ----
            if (ins.kind == FrontendInstructionKind::ret) {
                auto it = value_to_slot.find(ins.ret_value_id);
                if (it == value_to_slot.end()) {
                    result.message = "ret uses undefined value";
                    return result;
                }

                link_api.emit_return(it->second);
                emitted_ret = true;
                continue;
            }
        }

        if (!emitted_ret) {
            result.message = "no return emitted";
            return result;
        }

        // -------- FINALIZE CFG --------
        if (!link_api.seal_block(entry, out_function.stream().size()) ||
            !link_api.seal_block(exit,  out_function.stream().size()) ||
            !link_api.validate_cfg())
        {
            result.message = "invalid CFG";
            return result;
        }

        result.ok = true;
        result.emitted_nodes = out_function.stream().size();
        result.message = "ok";
        return result;
    }
};

} // namespace dgtl::core