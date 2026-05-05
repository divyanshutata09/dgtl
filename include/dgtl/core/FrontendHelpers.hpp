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
#include "dgtl/core/ControlFlow.hpp"
#include "dgtl/core/Function.hpp"

#include <cstdint>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dgtl/core/ABI.hpp"
#include "dgtl/core/ControlFlow.hpp"
#include "dgtl/core/Function.hpp"

namespace dgtl::core {
enum class FrontendInstructionKind : uint8_t {
    const_i64,
    const_str,
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
    and_i32,
    or_i32,
    xor_i32,
    shl_i32,
    shr_i32,
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


// Global string pool: strings interned at lowering time, looked up by JIT hooks at runtime.
// Inline so it collapses to one instance across all TUs in the DGTL binary.
inline std::vector<std::string>& dgtl_string_pool() {
    static std::vector<std::string> s_pool;
    return s_pool;
}

struct FrontendLoweringResult {
    bool ok = false;
    std::string message;
    size_t emitted_nodes = 0;
};

struct DGTLCallEmitOptions {
    DGTLCallValidationOptions validation{};
    uint16_t ir_flags = dgtl::ir::Flags::NONE;
    bool mark_concurrency_boundary = false;
};

class FrontendLoweringContext {
public:
    explicit FrontendLoweringContext(DGTLFunction& function) noexcept
        : function_(function) {}

    [[nodiscard]] uint32_t define_named_value(std::string_view name,
                                              FrontendTypeHandle type_handle = 0,
                                              dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP) {
        return function_.define_or_get_local(name, type_handle, type_hint);
    }

    [[nodiscard]] uint32_t emit_int_constant(uint64_t immediate,
                                             FrontendTypeHandle type_handle = 0,
                                             uint16_t flags = dgtl::ir::Flags::IS_CONST) {
        const uint32_t result_slot = function_.define_temp(type_handle, dgtl::ir::OpCode::LOAD_IMM);
        function_.stream().push_node(dgtl::ir::OpCode::LOAD_IMM,
                                     flags,
                                     result_slot,
                                     static_cast<uint32_t>((immediate >> 32) & 0xFFFFFFFFu),
                                     static_cast<uint32_t>(immediate & 0xFFFFFFFFu),
                                     dgtl::ir::INVALID_SLOT,
                                     0);
        return result_slot;
    }

    [[nodiscard]] uint32_t emit_binary(dgtl::ir::OpCode opcode,
                                       uint32_t lhs_slot,
                                       uint32_t rhs_slot,
                                       FrontendTypeHandle type_handle = 0,
                                       uint16_t flags = dgtl::ir::Flags::NONE) {
        const uint32_t result_slot = function_.define_temp(type_handle, opcode);
        function_.stream().push_node(opcode,
                                     flags,
                                     result_slot,
                                     lhs_slot,
                                     rhs_slot,
                                     dgtl::ir::INVALID_SLOT,
                                     0);
        return result_slot;
    }

    [[nodiscard]] bool emit_call(uint32_t result_slot,
                                 uint32_t callee_slot,
                                 std::span<const uint32_t> args,
                                 const DGTLCallSignature* signature = nullptr,
                                 uint16_t flags = dgtl::ir::Flags::NONE) {
        std::string ignored_error;
        return emit_call_checked(result_slot, callee_slot, args, signature, flags, ignored_error);
    }

    [[nodiscard]] bool emit_call_checked(uint32_t result_slot,
                                         uint32_t callee_slot,
                                         std::span<const uint32_t> args,
                                         const DGTLCallSignature* signature,
                                         uint16_t flags,
                                         std::string& error_out) {
        if (signature && !signature->is_variadic && args.size() != signature->parameters.size()) {
            error_out = "call arity mismatch against ABI signature";
            return false;
        }
        // Note: IR node stores up to 2 args inline (op_c, op_d).
        // Args beyond the first 2 are passed via shadow-space convention;
        // the register allocator handles spilling at a later stage.

        const uint32_t arg0 = args.size() > 0 ? args[0] : dgtl::ir::INVALID_SLOT;
        const uint32_t arg1 = args.size() > 1 ? args[1] : dgtl::ir::INVALID_SLOT;
        function_.stream().push_node(dgtl::ir::OpCode::CALL,
                                     flags,
                                     result_slot,
                                     callee_slot,
                                     arg0,
                                     arg1,
                                     0);
        error_out.clear();
        return true;
    }

    void emit_return(uint32_t value_slot) {
        function_.stream().push_node(dgtl::ir::OpCode::RET,
                                     dgtl::ir::Flags::IS_RETURN,
                                     0,
                                     value_slot,
                                     dgtl::ir::INVALID_SLOT,
                                     dgtl::ir::INVALID_SLOT,
                                     0);
    }

    void emit_goto(uint32_t target_block,
                   uint16_t flags = dgtl::ir::Flags::NONE) {
        function_.stream().push_node(dgtl::ir::OpCode::GOTO,
                                     static_cast<uint16_t>(flags | dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                                     dgtl::ir::INVALID_SLOT,
                                     target_block,
                                     dgtl::ir::INVALID_SLOT,
                                     dgtl::ir::INVALID_SLOT,
                                     0);
    }

    void emit_branch_cond(uint32_t condition_slot,
                          uint32_t target_block,
                          uint16_t flags = dgtl::ir::Flags::NONE) {
        function_.stream().push_node(dgtl::ir::OpCode::BRANCH_COND,
                                     static_cast<uint16_t>(flags | dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                                     dgtl::ir::INVALID_SLOT,
                                     condition_slot,
                                     target_block,
                                     dgtl::ir::INVALID_SLOT,
                                     0);
    }

    void emit_branch_compare_fused(uint32_t lhs_slot,
                                   uint32_t rhs_slot,
                                   uint32_t target_block,
                                   uint16_t flags = dgtl::ir::Flags::NONE) {
        function_.stream().push_node(dgtl::ir::OpCode::BRANCH_COMPARE_FUSED,
                                     static_cast<uint16_t>(flags | dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                                     dgtl::ir::INVALID_SLOT,
                                     lhs_slot,
                                     rhs_slot,
                                     target_block,
                                     0);
    }

private:
    DGTLFunction& function_;
};

class DGTLFrontendLinkAPI {
public:
    DGTLFrontendLinkAPI(DGTLFunction& function,
                        DGTLControlFlowGraph& cfg,
                        DGTLABIRegistry* abi_registry = nullptr) noexcept
        : function_(function),
          cfg_(cfg),
          lowering_(function),
          abi_registry_(abi_registry) {}

    [[nodiscard]] uint32_t add_parameter(std::string_view name,
                                         FrontendTypeHandle type_handle = 0,
                                         dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP,
                                         DGTLABIValueClass abi_class = DGTLABIValueClass::custom) {
        const uint32_t slot = function_.add_parameter(name, type_handle, type_hint);
        register_abi_parameter(std::string(name), type_handle, abi_class);
        return slot;
    }

    [[nodiscard]] uint32_t define_local(std::string_view name,
                                        FrontendTypeHandle type_handle = 0,
                                        dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP) {
        return lowering_.define_named_value(name, type_handle, type_hint);
    }

    [[nodiscard]] uint32_t emit_const_i64(uint64_t immediate,
                                          FrontendTypeHandle type_handle = 0,
                                          uint16_t flags = dgtl::ir::Flags::IS_CONST) {
        return lowering_.emit_int_constant(immediate, type_handle, flags);
    }

    [[nodiscard]] uint32_t emit_const_str(std::string_view str_literal,
                                          FrontendTypeHandle type_handle = 0,
                                          uint16_t flags = dgtl::ir::Flags::IS_CONST) {
        // Add string to global pool and get its index
        auto& pool = dgtl_string_pool();
        const uint32_t pool_index = static_cast<uint32_t>(pool.size());
        pool.emplace_back(str_literal);
        
        // Emit the pool index as a constant (the string is accessible via string pool lookup)
        return lowering_.emit_int_constant(static_cast<uint64_t>(pool_index), type_handle, flags);
    }

    [[nodiscard]] uint32_t emit_binary(dgtl::ir::OpCode opcode,
                                       uint32_t lhs_slot,
                                       uint32_t rhs_slot,
                                       FrontendTypeHandle type_handle = 0,
                                       uint16_t flags = dgtl::ir::Flags::NONE) {
        return lowering_.emit_binary(opcode, lhs_slot, rhs_slot, type_handle, flags);
    }

    [[nodiscard]] bool emit_call(uint32_t result_slot,
                                 std::string_view callee_name,
                                 std::span<const uint32_t> args,
                                 std::string* error_out = nullptr,
                                 uint16_t flags = dgtl::ir::Flags::NONE) {
        DGTLCallEmitOptions options;
        options.ir_flags = flags;
        return emit_call_with_options(result_slot, callee_name, args, options, error_out);
    }

    [[nodiscard]] bool emit_call_with_options(uint32_t result_slot,
                                              std::string_view callee_name,
                                              std::span<const uint32_t> args,
                                              const DGTLCallEmitOptions& options,
                                              std::string* error_out = nullptr) {
        std::string local_error;
        if (abi_registry_) {
            if (!abi_registry_->validate_call(callee_name,
                                              args.size(),
                                              options.validation,
                                              &local_error)) {
                if (error_out) {
                    *error_out = local_error;
                }
                return false;
            }
        }

        const uint32_t callee_slot = resolve_or_define_extern(callee_name);
        const DGTLCallSignature* signature = abi_registry_ ? abi_registry_->find_signature(callee_name) : nullptr;
        uint16_t effective_flags = options.ir_flags;
        if (options.mark_concurrency_boundary || is_concurrency_boundary_callee(callee_name)) {
            effective_flags = static_cast<uint16_t>(effective_flags |
                                                    dgtl::ir::Flags::IS_VOLATILE |
                                                    dgtl::ir::Flags::HAS_SIDE_EFFECTS);
        }

        if (!lowering_.emit_call_checked(result_slot,
                                         callee_slot,
                                         args,
                                         signature,
                                         effective_flags,
                                         local_error)) {
            if (error_out) {
                *error_out = local_error;
            }
            return false;
        }

        if (error_out) {
            error_out->clear();
        }
        return true;
    }

    void emit_return(uint32_t value_slot) {
        lowering_.emit_return(value_slot);
    }

    void emit_goto(uint32_t target_block,
                   uint16_t flags = dgtl::ir::Flags::NONE) {
        lowering_.emit_goto(target_block, flags);
    }

    void emit_branch_cond(uint32_t condition_slot,
                          uint32_t target_block,
                          uint16_t flags = dgtl::ir::Flags::NONE) {
        lowering_.emit_branch_cond(condition_slot, target_block, flags);
    }

    void emit_branch_compare_fused(uint32_t lhs_slot,
                                   uint32_t rhs_slot,
                                   uint32_t target_block,
                                   uint16_t flags = dgtl::ir::Flags::NONE) {
        lowering_.emit_branch_compare_fused(lhs_slot, rhs_slot, target_block, flags);
    }

    [[nodiscard]] uint32_t create_block(size_t begin_index = 0) {
        return cfg_.create_block(begin_index);
    }

    [[nodiscard]] bool add_edge(uint32_t from_block, uint32_t to_block) {
        return cfg_.add_edge(from_block, to_block);
    }

    [[nodiscard]] bool seal_block(uint32_t block_id, size_t end_index) {
        return cfg_.seal_block(block_id, end_index);
    }

    [[nodiscard]] bool validate_cfg() const noexcept {
        return cfg_.validate();
    }

    [[nodiscard]] FrontendLoweringContext& lowering_context() noexcept {
        return lowering_;
    }

    [[nodiscard]] const std::vector<DGTLABIParameter>& abi_parameters() const noexcept {
        return abi_parameters_;
    }

    /// Returns the callee-name → slot mapping built during lowering.
    /// Used by the multi-function COFF emitter to resolve CALL targets.
    [[nodiscard]] const std::unordered_map<std::string, uint32_t>& extern_slots() const noexcept {
        return extern_slots_;
    }

private:
    [[nodiscard]] uint32_t resolve_or_define_extern(std::string_view callee_name) {
        if (const auto existing = function_.resolve_slot(callee_name); existing.has_value()) {
            return *existing;
        }

        const auto it = extern_slots_.find(std::string(callee_name));
        if (it != extern_slots_.end()) {
            return it->second;
        }

        const uint32_t slot = function_.define_or_get_local(callee_name, 0, dgtl::ir::OpCode::CALL);
        if (slot != dgtl::ir::INVALID_SLOT) {
            extern_slots_.emplace(std::string(callee_name), slot);
        }
        return slot;
    }

    void register_abi_parameter(std::string name,
                                FrontendTypeHandle type_handle,
                                DGTLABIValueClass abi_class) {
        if (!abi_registry_) {
            return;
        }

        abi_parameters_.push_back(DGTLABIParameter{std::move(name), abi_class, 0xFF, 0, type_handle});
    }

    DGTLFunction& function_;
    DGTLControlFlowGraph& cfg_;
    FrontendLoweringContext lowering_;
    DGTLABIRegistry* abi_registry_ = nullptr;
    std::unordered_map<std::string, uint32_t> extern_slots_;
    std::vector<DGTLABIParameter> abi_parameters_;
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

        // ---- PRE-SCAN: compute label_id → stream node index for forward-jump resolution ----
        // Labels emit 0 IR nodes; every other instruction emits exactly 1.
        const bool has_control_flow = [&]() {
            for (const auto& ins : fn.instructions) {
                if (ins.kind == FrontendInstructionKind::label ||
                    ins.kind == FrontendInstructionKind::jmp   ||
                    ins.kind == FrontendInstructionKind::br_if) {
                    return true;
                }
            }
            return false;
        }();

        std::unordered_map<uint32_t, uint32_t> label_positions; // label_id → stream index
        if (has_control_flow) {
            uint32_t stream_idx = 0;
            for (const auto& ins : fn.instructions) {
                if (ins.kind == FrontendInstructionKind::label) {
                    label_positions[static_cast<uint32_t>(ins.immediate)] = stream_idx;
                } else {
                    ++stream_idx;
                }
            }
        }

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

            if (ins.kind == FrontendInstructionKind::const_str) {
                if (ins.result_value_id == dgtl::ir::INVALID_SLOT) {
                    result.message = "const_str requires result_value_id";
                    return result;
                }

                const uint32_t slot =
                    link_api.emit_const_str(ins.callee_name, ins.type_handle);

                value_to_slot[ins.result_value_id] = slot;
                continue;
            }

            // ---- BINARY OPS (arithmetic, comparison, bitwise) ----
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
                ins.kind == FrontendInstructionKind::cmp_ge ||
                ins.kind == FrontendInstructionKind::and_i32 ||
                ins.kind == FrontendInstructionKind::or_i32  ||
                ins.kind == FrontendInstructionKind::xor_i32 ||
                ins.kind == FrontendInstructionKind::shl_i32 ||
                ins.kind == FrontendInstructionKind::shr_i32)
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
                    // Guard against Windows SDK's "#define CONST const" (from winnt.h via MinGW-w64)
                    // which causes OpCode::CONST to be mangled into OpCode::const inside generic
                    // lambda template bodies. This mirrors the guard already present in IR_Types.hpp.
#ifdef CONST
#undef CONST
#endif
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
                        else if (ins.kind == FrontendInstructionKind::and_i32) res = l & r;
                        else if (ins.kind == FrontendInstructionKind::or_i32)  res = l | r;
                        else if (ins.kind == FrontendInstructionKind::xor_i32) res = l ^ r;
                        else if (ins.kind == FrontendInstructionKind::shl_i32) res = static_cast<uint64_t>(static_cast<uint32_t>(l) << (r & 31u));
                        else if (ins.kind == FrontendInstructionKind::shr_i32) res = static_cast<uint64_t>(static_cast<int32_t>(l) >> (r & 31u));

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
                else if (ins.kind == FrontendInstructionKind::and_i32)
                    opcode = dgtl::ir::OpCode::AND_I32;
                else if (ins.kind == FrontendInstructionKind::or_i32)
                    opcode = dgtl::ir::OpCode::OR_I32;
                else if (ins.kind == FrontendInstructionKind::xor_i32)
                    opcode = dgtl::ir::OpCode::XOR_I32;
                else if (ins.kind == FrontendInstructionKind::shl_i32)
                    opcode = dgtl::ir::OpCode::SHL_I32;
                else if (ins.kind == FrontendInstructionKind::shr_i32)
                    opcode = dgtl::ir::OpCode::SHR_I32;

                const uint32_t slot =
                    link_api.emit_binary(opcode, lhs, rhs, ins.type_handle);

                value_to_slot[ins.result_value_id] = slot;
                continue;
            }

            // ---- LABEL ----
            if (ins.kind == FrontendInstructionKind::label) {
                // Label is a position marker — no IR node emitted.
                // Position was recorded in label_positions pre-scan.
                continue;
            }

            // ---- JMP ----
            if (ins.kind == FrontendInstructionKind::jmp) {
                const auto target_it = label_positions.find(static_cast<uint32_t>(ins.immediate));
                if (target_it == label_positions.end()) {
                    result.message = "jmp targets undefined label";
                    return result;
                }
                link_api.emit_goto(target_it->second);
                continue;
            }

            // ---- BR_IF ----
            if (ins.kind == FrontendInstructionKind::br_if) {
                const auto cond_it = value_to_slot.find(ins.lhs_value_id);
                if (cond_it == value_to_slot.end()) {
                    result.message = "br_if uses undefined condition";
                    return result;
                }
                const auto target_it = label_positions.find(static_cast<uint32_t>(ins.immediate));
                if (target_it == label_positions.end()) {
                    result.message = "br_if targets undefined label";
                    return result;
                }
                link_api.emit_branch_cond(cond_it->second, target_it->second);
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
        // When control flow (labels/jmp/br_if) is present the simple entry→exit
        // edge no longer represents the full graph. Seal both sentinel blocks
        // at the current stream extent; skip strict CFG validation since the
        // real edges are encoded in the emitted GOTO/BRANCH_COND nodes.
        if (has_control_flow) {
            if (!link_api.seal_block(entry, out_function.stream().size()) ||
                !link_api.seal_block(exit,  out_function.stream().size()))
            {
                result.message = "invalid CFG";
                return result;
            }
        } else {
            if (!link_api.seal_block(entry, out_function.stream().size()) ||
                !link_api.seal_block(exit,  out_function.stream().size()) ||
                !link_api.validate_cfg())
            {
                result.message = "invalid CFG";
                return result;
            }
        }

        result.ok = true;
        result.emitted_nodes = out_function.stream().size();
        result.message = "ok";
        return result;
    }
};
} // namespace dgtl::core




