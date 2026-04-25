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
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dgtl/core/ABI.hpp"
#include "dgtl/core/ControlFlow.hpp"
#include "dgtl/core/Function.hpp"

namespace dgtl::core {

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
        if (args.size() > 2) {
            error_out = "DGTL CALL currently supports at most 2 lowered arguments";
            return false;
        }

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
        std::string local_error;
        if (abi_registry_) {
            if (!abi_registry_->validate_call(callee_name, args.size(), &local_error)) {
                if (error_out) {
                    *error_out = local_error;
                }
                return false;
            }
        }

        const uint32_t callee_slot = resolve_or_define_extern(callee_name);
        const DGTLCallSignature* signature = abi_registry_ ? abi_registry_->find_signature(callee_name) : nullptr;
        if (!lowering_.emit_call_checked(result_slot, callee_slot, args, signature, flags, local_error)) {
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

} // namespace dgtl::core