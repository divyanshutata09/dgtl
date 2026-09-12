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

module;

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string_view>

export module dgtl.op_traits;

// Dependency Ordered Module Imports
import dgtl.ir_enums;
import dgtl.ir_core.gen;

export namespace dgtl::ir::interfaces {

enum class OpTrait : std::uint32_t {
    None = 0,
    Commutative = 1u << 0,
    IsCommutative = Commutative,
    HasNoSideEffects = 1u << 1,
    IsTerminator = 1u << 2,
    IsMemoryRead = 1u << 3,
    IsMemoryWrite = 1u << 4,
    IsMemoryOp = 1u << 5,
    IsVectorizable = 1u << 6,
    IsFusible = 1u << 7,
    IsSpeculative = 1u << 8,
    IsGuard = 1u << 9,
    IsGPU = 1u << 10,
    IsTensor = 1u << 11,
    IsGraph = 1u << 12,
    IsAsyncSpawnable = 1u << 13,
};

// Inline bitwise utilities to make module usage semantic without casting pollution
[[nodiscard]] constexpr OpTrait operator|(OpTrait lhs, OpTrait rhs) noexcept {
    return static_cast<OpTrait>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}
[[nodiscard]] constexpr OpTrait operator&(OpTrait lhs, OpTrait rhs) noexcept {
    return static_cast<OpTrait>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

enum class OpInterface : std::uint32_t {
    None = 0,
    Arithmetic = 1u << 0,
    Memory = 1u << 1,
    ControlFlow = 1u << 2,
    Terminator = 1u << 3,
    Comparison = 1u << 4,
    BitwiseOp = 1u << 5,
    SIMD = 1u << 6,
    GPU = 1u << 7,
    Tensor = 1u << 8,
    Concurrency = 1u << 9,
    DSA = 1u << 10,
    Ownership = 1u << 11,
};

[[nodiscard]] constexpr OpInterface operator|(OpInterface lhs, OpInterface rhs) noexcept {
    return static_cast<OpInterface>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}
[[nodiscard]] constexpr OpInterface operator&(OpInterface lhs, OpInterface rhs) noexcept {
    return static_cast<OpInterface>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

struct OpMetadata {
    OpCode opcode = OpCode::NOP;
    std::uint32_t trait_bits = 0u;
    std::uint32_t interface_bits = 0u;
    std::uint8_t operand_count = 0u;
    bool has_result = false;
    std::string_view name = "UnknownOp";
};

[[nodiscard]] constexpr std::uint32_t to_bits(OpTrait trait) noexcept {
    return static_cast<std::uint32_t>(trait);
}

[[nodiscard]] constexpr std::uint32_t to_bits(OpInterface iface) noexcept {
    return static_cast<std::uint32_t>(iface);
}

// Global lookup maps injected dynamically from tablegen targets (like DGTL_Gen_Ops)
namespace fallback_detail {
    struct GeneratedOpDef {
        std::uint32_t trait_bits;
        std::uint32_t interface_bits;
        std::uint8_t operand_count;
        bool has_result;
        const char* result_type;
        const char* name;
        dgtl::ir::TypeCode result_type_code;
    };
    
    // Complete fallback table covering all standard core opcodes
    inline constexpr std::array<GeneratedOpDef, 64> kCoreFallbackDefs = {{
        { 0u, to_bits(OpInterface::ControlFlow), 0u, false, "void", "Nop", TypeCode::VOID_T },
        { to_bits(OpTrait::IsMemoryRead) | to_bits(OpTrait::IsMemoryOp), to_bits(OpInterface::Memory), 1u, true, "I32", "Load", TypeCode::I32 },
        { to_bits(OpTrait::IsMemoryWrite) | to_bits(OpTrait::IsMemoryOp), to_bits(OpInterface::Memory), 2u, false, "void", "Store", TypeCode::VOID_T },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 1u, true, "I32", "RegMove", TypeCode::I32 },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 0u, true, "I32", "LoadImm", TypeCode::I32 },
        { to_bits(OpTrait::HasNoSideEffects) | to_bits(OpTrait::IsCommutative), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "AddI32", TypeCode::I32 },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "SubI32", TypeCode::I32 },
        { to_bits(OpTrait::HasNoSideEffects) | to_bits(OpTrait::IsCommutative), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "MulI32", TypeCode::I32 },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "DivI32", TypeCode::I32 },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 1u, false, "void", "Return", TypeCode::VOID_T },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 0u, false, "void", "Goto", TypeCode::VOID_T },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 1u, false, "void", "BranchCond", TypeCode::VOID_T }
    }};

    [[nodiscard]] constexpr std::optional<GeneratedOpDef> find_generated_op_def(OpCode opcode) noexcept {
        // First try finding the op in the module-imported DSL generated metadata table
        if (const auto* gen_def = dgtl::ir::gen::find_generated_op_def(opcode)) {
            GeneratedOpDef def{};
            def.trait_bits = static_cast<std::uint32_t>(gen_def->trait_bits);
            def.interface_bits = static_cast<std::uint32_t>(gen_def->interface_bits);
            def.operand_count = static_cast<std::uint8_t>(gen_def->operand_count);
            def.has_result = gen_def->has_result;
            def.result_type = gen_def->result_type.data();
            def.name = gen_def->name.data();
            def.result_type_code = gen_def->result_type_code;
            return def;
        }

        // Secondary fallback lookup using index
        const auto index = static_cast<size_t>(opcode);
        if (index < kCoreFallbackDefs.size()) {
            return kCoreFallbackDefs[index];
        }
        return std::nullopt;
    }
    
    struct IRTraits {
        [[nodiscard]] static constexpr bool is_terminator(OpCode opcode) noexcept {
            if (const auto def = find_generated_op_def(opcode)) {
                return (def->trait_bits & to_bits(OpTrait::IsTerminator)) != 0u;
            }
            return false;
        }
    };
} // namespace fallback_detail

[[nodiscard]] constexpr OpMetadata get_metadata(OpCode opcode) noexcept {
    if (const auto def = fallback_detail::find_generated_op_def(opcode)) {
        return OpMetadata{
            .opcode = opcode,
            .trait_bits = def->trait_bits,
            .interface_bits = def->interface_bits,
            .operand_count = def->operand_count,
            .has_result = def->has_result,
            .name = def->name
        };
    }

    return OpMetadata{
        .opcode = opcode,
        .name = "Unknown"
    };
}

[[nodiscard]] constexpr bool has_trait(OpCode opcode, OpTrait trait) noexcept {
    return (get_metadata(opcode).trait_bits & to_bits(trait)) != 0u;
}

[[nodiscard]] constexpr bool implements_interface(OpCode opcode, OpInterface iface) noexcept {
    return (get_metadata(opcode).interface_bits & to_bits(iface)) != 0u;
}

[[nodiscard]] constexpr std::optional<TypeCode> expected_result_type_code(OpCode opcode) noexcept {
    const auto def = fallback_detail::find_generated_op_def(opcode);
    if (!def) return std::nullopt;
    return def->result_type_code;
}

[[nodiscard]] constexpr bool has_expected_result_type(OpCode opcode) noexcept {
    const auto expected = expected_result_type_code(opcode);
    return expected.has_value() && *expected != TypeCode::VOID_T;
}

} // namespace dgtl::ir::interfaces