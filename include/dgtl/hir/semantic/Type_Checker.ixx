/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Semantic Type Checker & Invariant Verifier
 * Standards:   C++26 / UCRT64 / Direct-to-IR Verification
 */

module;

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <string_view>

export module dgtl.hir.type_checker;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir.stream;
import dgtl.hir.semantic;
import dgtl.hir.type_system;
import dgtl.ir_core.abi;

export namespace dgtl::hir::semantic {

struct TypeError {
    size_t node_index = 0;
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
    std::string message;
};

class TypeChecker {
public:
    TypeChecker(TypeSystem& type_sys, SymbolTable& sym_table) noexcept
        : type_system_(type_sys), symbol_table_(sym_table) {}

    [[nodiscard]] bool verify_stream(const dgtl::ir::IRStream& stream, std::vector<TypeError>& errors) const {
        errors.clear();
        if (stream.is_null() || stream.size() == 0) return true;

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto node = stream.get_node(i);
            if (node.opcode == dgtl::ir::OpCode::NOP) continue;

            check_instruction(stream, i, node, errors);
        }

        return errors.empty();
    }

private:
    TypeSystem& type_system_;
    SymbolTable& symbol_table_;

    void report(std::vector<TypeError>& errors, size_t idx, uint32_t slot, std::string msg) const {
        errors.push_back(TypeError{
            .node_index = idx,
            .slot_id = slot,
            .message = std::move(msg)
        });
    }

    [[nodiscard]] const TypeDescriptor* resolve_slot_type(const dgtl::ir::IRStream& stream, uint32_t slot) const noexcept {
        if (slot == dgtl::ir::INVALID_SLOT) return nullptr;

        // 1. Query IRStream result type map
        const dgtl::ir::TypeCode code = stream.get_result_type_code(slot);
        if (code != dgtl::ir::TypeCode::VOID_T) {
            return type_system_.get_primitive(code);
        }

        // 2. Query Semantic SymbolTable
        if (const auto* sym = symbol_table_.lookup_by_slot(slot)) {
            return type_system_.get_primitive(sym->type_code);
        }

        return nullptr;
    }

    void check_instruction(const dgtl::ir::IRStream& stream, size_t idx, const dgtl::ir::IRNode& node, std::vector<TypeError>& errors) const {
        using Op = dgtl::ir::OpCode;

        switch (node.opcode) {
            // ---- Integer BinOps (I8) ----
            case Op::ADD_I8: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::I8, errors);
                break;
            }

            // ---- Integer BinOps (I32) ----
            case Op::ADD_I32: case Op::SUB_I32: case Op::MUL_I32: case Op::DIV_I32:
            case Op::MOD_I32: case Op::AND_I32: case Op::OR_I32:  case Op::XOR_I32:
            case Op::SHL_I32: case Op::SHR_I32: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::I32, errors);
                break;
            }

            // ---- Extended Integer BinOps (I64) ----
            case Op::ADD_I64: case Op::SUB_I64: case Op::MUL_I64: case Op::DIV_I64:
            case Op::MOD_I64: case Op::AND_I64: case Op::OR_I64:  case Op::XOR_I64:
            case Op::SHL_I64: case Op::SHR_I64: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::I64, errors);
                break;
            }

            // ---- Integer Comparisons (I32) ----
            case Op::CMP_EQ_I32: case Op::CMP_NE_I32: case Op::CMP_LT_I32:
            case Op::CMP_LE_I32: case Op::CMP_GT_I32: case Op::CMP_GE_I32: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::I32, errors);
                break;
            }

            // ---- Floating Point BinOps ----
            case Op::ADD_F32: case Op::SUB_F32: case Op::MUL_F32: case Op::DIV_F32: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::F32, errors);
                break;
            }
            case Op::ADD_F64: case Op::SUB_F64: case Op::MUL_F64: case Op::DIV_F64: {
                check_binary_arithmetic(stream, idx, node, dgtl::ir::TypeCode::F64, errors);
                break;
            }

            // ---- Pointer Arithmetic / GEP ----
            case Op::GEP: {
                const auto* base = resolve_slot_type(stream, node.op_a);
                if (base && !base->is_pointer()) {
                    report(errors, idx, node.op_a, "GEP base must evaluate to pointer type");
                }
                const auto* offset = resolve_slot_type(stream, node.op_b);
                if (offset && !offset->is_integer()) {
                    report(errors, idx, node.op_b, "GEP offset must evaluate to integer type");
                }
                break;
            }

            // ---- Memory Loads ----
            case Op::LOAD: case Op::VOLATILE_LOAD: case Op::ATOMIC_LOAD: {
                const auto* ptr = resolve_slot_type(stream, node.op_a);
                if (ptr && !ptr->is_pointer()) {
                    report(errors, idx, node.op_a, "Load source must be a pointer type");
                }
                break;
            }

            // ---- Memory Stores ----
            case Op::STORE: case Op::VOLATILE_STORE: case Op::ATOMIC_STORE: {
                const auto* ptr = resolve_slot_type(stream, node.op_a);
                if (ptr && !ptr->is_pointer()) {
                    report(errors, idx, node.op_a, "Store destination must be a pointer type");
                }
                break;
            }

            // ---- Fused Multiply-Add ----
            case Op::FMA_F32: {
                const auto* a = resolve_slot_type(stream, node.op_a);
                const auto* b = resolve_slot_type(stream, node.op_b);
                const auto* c = resolve_slot_type(stream, node.op_c);
                if ((a && a->canonical_code != dgtl::ir::TypeCode::F32) ||
                    (b && b->canonical_code != dgtl::ir::TypeCode::F32) ||
                    (c && c->canonical_code != dgtl::ir::TypeCode::F32)) {
                    report(errors, idx, node.result_id, "FMA_F32 operands must all evaluate to F32");
                }
                break;
            }

            default:
                break;
        }
    }

    void check_binary_arithmetic(
        const dgtl::ir::IRStream& stream,
        size_t idx,
        const dgtl::ir::IRNode& node,
        dgtl::ir::TypeCode expected,
        std::vector<TypeError>& errors
    ) const {
        const auto* ty_a = resolve_slot_type(stream, node.op_a);
        const auto* ty_b = resolve_slot_type(stream, node.op_b);

        if (ty_a && ty_a->canonical_code != expected) {
            report(errors, idx, node.op_a, "Left operand type mismatch for arithmetic operation");
        }
        if (ty_b && ty_b->canonical_code != expected) {
            report(errors, idx, node.op_b, "Right operand type mismatch for arithmetic operation");
        }
    }
};

} // namespace dgtl::hir::semantic

export namespace dgtl::semantic {
    using namespace dgtl::hir::semantic;
    using TypeError = dgtl::hir::semantic::TypeError;
    using TypeChecker = dgtl::hir::semantic::TypeChecker;
}

export namespace dgtl::hir {
    using namespace dgtl::hir::semantic;
    using TypeError = dgtl::hir::semantic::TypeError;
    using TypeChecker = dgtl::hir::semantic::TypeChecker;
}