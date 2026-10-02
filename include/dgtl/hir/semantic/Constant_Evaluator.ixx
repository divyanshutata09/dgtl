/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Compile-Time Expression Evaluator & Constexpr VM
 * Standards:   C++26 / UCRT64 / Undefined-Behavior Free
 */

module;

#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <optional>
#include <variant>
#include <bit>
#include <limits>
#include <unordered_map>

export module dgtl.hir.constant_evaluator;

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir.stream;
import dgtl.hir.semantic;
import dgtl.hir.type_system;

export namespace dgtl::hir::semantic {

/**
 * @struct EvalValue
 * @brief Polymorphic compile-time literal representation.
 */
struct EvalValue {
    enum class Kind : uint8_t {
        Undefined = 0,
        Int64,
        Float64,
        StringHandle,
        Bool
    } kind = Kind::Undefined;

    union {
        int64_t i64 = 0;
        double f64;
        uint32_t str_handle;
        bool b;
    } data{};

    [[nodiscard]] constexpr bool is_int() const noexcept { return kind == Kind::Int64; }
    [[nodiscard]] constexpr bool is_float() const noexcept { return kind == Kind::Float64; }
    [[nodiscard]] constexpr bool is_bool() const noexcept { return kind == Kind::Bool; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return kind != Kind::Undefined; }

    [[nodiscard]] static constexpr EvalValue from_i64(int64_t v) noexcept {
        EvalValue val; val.kind = Kind::Int64; val.data.i64 = v; return val;
    }
    [[nodiscard]] static constexpr EvalValue from_f64(double v) noexcept {
        EvalValue val; val.kind = Kind::Float64; val.data.f64 = v; return val;
    }
    [[nodiscard]] static constexpr EvalValue from_bool(bool v) noexcept {
        EvalValue val; val.kind = Kind::Bool; val.data.b = v; return val;
    }
};

/**
 * @class ConstantEvaluator
 * @brief Evaluates compile-time expressions and detects constant undefined behavior.
 */
class ConstantEvaluator {
public:
    explicit ConstantEvaluator(SymbolTable& sym_table) noexcept
        : symbol_table_(sym_table) {}

    ~ConstantEvaluator() = default;

    /**
     * @brief Evaluates an IR binary operation on constant inputs.
     */
    [[nodiscard]] std::optional<EvalValue> evaluate_binary_op(
        dgtl::ir::OpCode opcode,
        const EvalValue& lhs,
        const EvalValue& rhs
    ) const noexcept {
        if (!lhs.is_valid() || !rhs.is_valid()) return std::nullopt;

        // ---- Integer Arithmetic ----
        if (lhs.is_int() && rhs.is_int()) {
            const int64_t a = lhs.data.i64;
            const int64_t b = rhs.data.i64;

            switch (opcode) {
                case dgtl::ir::OpCode::ADD_I8:
                case dgtl::ir::OpCode::ADD_I32:
                case dgtl::ir::OpCode::ADD_I64: return EvalValue::from_i64(a + b);
                case dgtl::ir::OpCode::SUB_I32:
                case dgtl::ir::OpCode::SUB_I64: return EvalValue::from_i64(a - b);
                case dgtl::ir::OpCode::MUL_I32:
                case dgtl::ir::OpCode::MUL_I64: return EvalValue::from_i64(a * b);
                case dgtl::ir::OpCode::DIV_I32:
                case dgtl::ir::OpCode::DIV_I64:
                    if (b == 0 || (a == std::numeric_limits<int64_t>::min() && b == -1)) return std::nullopt; // Prevent compile-time div by 0 and overflow trap
                    return EvalValue::from_i64(a / b);
                case dgtl::ir::OpCode::MOD_I32:
                case dgtl::ir::OpCode::MOD_I64:
                    if (b == 0 || (a == std::numeric_limits<int64_t>::min() && b == -1)) return std::nullopt;
                    return EvalValue::from_i64(a % b);
                case dgtl::ir::OpCode::AND_I32:
                case dgtl::ir::OpCode::AND_I64: return EvalValue::from_i64(a & b);
                case dgtl::ir::OpCode::OR_I32:
                case dgtl::ir::OpCode::OR_I64:  return EvalValue::from_i64(a | b);
                case dgtl::ir::OpCode::XOR_I32:
                case dgtl::ir::OpCode::XOR_I64: return EvalValue::from_i64(a ^ b);
                case dgtl::ir::OpCode::SHL_I32:
                case dgtl::ir::OpCode::SHL_I64: return EvalValue::from_i64(a << (b & 63));
                case dgtl::ir::OpCode::SHR_I32:
                case dgtl::ir::OpCode::SHR_I64: return EvalValue::from_i64(a >> (b & 63));

                // Comparisons
                case dgtl::ir::OpCode::CMP_EQ_I32: return EvalValue::from_bool(a == b);
                case dgtl::ir::OpCode::CMP_NE_I32: return EvalValue::from_bool(a != b);
                case dgtl::ir::OpCode::CMP_LT_I32: return EvalValue::from_bool(a < b);
                case dgtl::ir::OpCode::CMP_LE_I32: return EvalValue::from_bool(a <= b);
                case dgtl::ir::OpCode::CMP_GT_I32: return EvalValue::from_bool(a > b);
                case dgtl::ir::OpCode::CMP_GE_I32: return EvalValue::from_bool(a >= b);
                default: break;
            }
        }

        // ---- Floating Point Arithmetic ----
        if (lhs.is_float() && rhs.is_float()) {
            const double a = lhs.data.f64;
            const double b = rhs.data.f64;

            switch (opcode) {
                case dgtl::ir::OpCode::ADD_F32:
                case dgtl::ir::OpCode::ADD_F64: return EvalValue::from_f64(a + b);
                case dgtl::ir::OpCode::SUB_F32:
                case dgtl::ir::OpCode::SUB_F64: return EvalValue::from_f64(a - b);
                case dgtl::ir::OpCode::MUL_F32:
                case dgtl::ir::OpCode::MUL_F64: return EvalValue::from_f64(a * b);
                case dgtl::ir::OpCode::DIV_F32:
                case dgtl::ir::OpCode::DIV_F64:
                    if (b == 0.0) return std::nullopt;
                    return EvalValue::from_f64(a / b);
                default: break;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Traverses stream nodes and substitutes constant values in the SymbolTable.
     */
    void propagate_stream_constants(dgtl::ir::IRStream& stream) {
        if (stream.is_null() || stream.size() == 0) return;

        std::unordered_map<uint32_t, EvalValue> const_map;
        const_map.reserve(64);

        for (size_t i = 0; i < stream.size(); ++i) {
            auto node = stream.get_node(i);

            if (node.opcode == dgtl::ir::OpCode::LOAD_IMM ||
                node.opcode == dgtl::ir::OpCode::IMMEDIATE) {
                uint64_t raw_val = (static_cast<uint64_t>(node.op_b) << 32u) | (static_cast<uint64_t>(node.op_a) & 0xFFFFFFFFu);
                if (raw_val == 0 && node.intent_hash != 0) raw_val = node.intent_hash;

                EvalValue v = EvalValue::from_i64(static_cast<int64_t>(raw_val));
                const_map[node.result_id] = v;

                // Sync into symbol table
                if (auto* sym = symbol_table_.lookup_by_slot(node.result_id)) {
                    sym->constant_value = raw_val;
                    sym->set_flag(SymbolFlags::IsConst);
                }
                continue;
            }

            // Check if binary operation has constant operands
            auto it_a = const_map.find(node.op_a);
            auto it_b = const_map.find(node.op_b);

            if (it_a != const_map.end() && it_b != const_map.end()) {
                auto folded = evaluate_binary_op(node.opcode, it_a->second, it_b->second);
                if (folded.has_value() && folded->is_int()) {
                    const_map[node.result_id] = *folded;

                    // Mutate node to constant load in stream
                    node.opcode = dgtl::ir::OpCode::LOAD_IMM;
                    node.op_a = static_cast<uint32_t>(folded->data.i64 & 0xFFFFFFFFu);
                    node.op_b = static_cast<uint32_t>((folded->data.i64 >> 32u) & 0xFFFFFFFFu);
                    node.flags |= dgtl::ir::Flags::IS_CONST;
                    node.refresh_intent_hash();
                    stream.set_node(i, node);

                    // Preserve existing result type code in the stream
                    const auto existing_tc = stream.get_result_type_code(node.result_id);
                    if (existing_tc != dgtl::ir::TypeCode::VOID_T) {
                        stream.set_result_type(node.result_id, existing_tc);
                    }

                    if (auto* sym = symbol_table_.lookup_by_slot(node.result_id)) {
                        sym->constant_value = static_cast<uint64_t>(folded->data.i64);
                        sym->set_flag(SymbolFlags::IsConst);
                    }
                }
            }
        }
    }

private:
    SymbolTable& symbol_table_;
};

} // namespace dgtl::hir::semantic

export namespace dgtl::semantic {
    using namespace dgtl::hir::semantic;
    using EvalValue = dgtl::hir::semantic::EvalValue;
    using ConstantEvaluator = dgtl::hir::semantic::ConstantEvaluator;
}

export namespace dgtl::hir {
    using namespace dgtl::hir::semantic;
    using EvalValue = dgtl::hir::semantic::EvalValue;
    using ConstantEvaluator = dgtl::hir::semantic::ConstantEvaluator;
}