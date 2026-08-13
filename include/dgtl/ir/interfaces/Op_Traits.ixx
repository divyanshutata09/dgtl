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
    };
    
    // Complete fallback table covering all standard core opcodes
    inline constexpr std::array<GeneratedOpDef, 64> kCoreFallbackDefs = {{
        { 0u, to_bits(OpInterface::ControlFlow), 0u, false, "void", "Nop" },
        { to_bits(OpTrait::IsMemoryRead) | to_bits(OpTrait::IsMemoryOp), to_bits(OpInterface::Memory), 1u, true, "I32", "Load" },
        { to_bits(OpTrait::IsMemoryWrite) | to_bits(OpTrait::IsMemoryOp), to_bits(OpInterface::Memory), 2u, false, "void", "Store" },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 1u, true, "I32", "RegMove" },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 0u, true, "I32", "LoadImm" },
        { to_bits(OpTrait::HasNoSideEffects) | to_bits(OpTrait::IsCommutative), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "AddI32" },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "SubI32" },
        { to_bits(OpTrait::HasNoSideEffects) | to_bits(OpTrait::IsCommutative), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "MulI32" },
        { to_bits(OpTrait::HasNoSideEffects), to_bits(OpInterface::Arithmetic), 2u, true, "I32", "DivI32" },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 1u, false, "void", "Return" },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 0u, false, "void", "Goto" },
        { to_bits(OpTrait::IsTerminator), to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator), 1u, false, "void", "BranchCond" }
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
            return opcode == OpCode::RETURN || opcode == OpCode::BRANCH || opcode == OpCode::BRANCH_COND || opcode == OpCode::GOTO;
        }
        [[nodiscard]] static constexpr bool has_side_effects(OpCode opcode, std::uint16_t flags) noexcept {
            if ((flags & Flags::IS_CONST) != 0) return false;
            return opcode == OpCode::STORE || opcode == OpCode::ATOMIC_STORE || opcode == OpCode::SYSCALL || opcode == OpCode::CALL;
        }
    };
}

[[nodiscard]] constexpr OpMetadata get_metadata(OpCode opcode) noexcept {
    if (const auto def = fallback_detail::find_generated_op_def(opcode)) {
        OpMetadata md{};
        md.opcode = opcode;
        md.operand_count = def->operand_count;
        md.has_result = def->has_result;
        md.trait_bits = def->trait_bits;
        md.interface_bits = def->interface_bits;
        if (def->name != nullptr) {
            md.name = std::string_view(def->name);
        }
        return md;
    }

    // Conservative fallback for non-generated or unregistered opcodes.
    OpMetadata fallback{};
    fallback.opcode = opcode;
    if (fallback_detail::IRTraits::is_terminator(opcode)) {
        fallback.trait_bits |= to_bits(OpTrait::IsTerminator);
        fallback.interface_bits |= to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator);
    }
    if (!fallback_detail::IRTraits::has_side_effects(opcode, Flags::NONE)) {
        fallback.trait_bits |= to_bits(OpTrait::HasNoSideEffects);
    }
    return fallback;
}

[[nodiscard]] constexpr bool has_trait(OpCode opcode, OpTrait trait) noexcept {
    return (get_metadata(opcode).trait_bits & to_bits(trait)) != 0u;
}

[[nodiscard]] constexpr bool implements_interface(OpCode opcode, OpInterface iface) noexcept {
    return (get_metadata(opcode).interface_bits & to_bits(iface)) != 0u;
}

[[nodiscard]] constexpr std::optional<TypeCode> expected_result_type_code(OpCode opcode) noexcept {
    const auto def = fallback_detail::find_generated_op_def(opcode);
    if (!def || def->result_type == nullptr) return std::nullopt;

    const std::string_view ty(def->result_type);
    if (ty == "void") return TypeCode::VOID_T;
    if (ty == "Bool") return TypeCode::BOOL;
    if (ty == "Predicate") return TypeCode::PREDICATE;
    if (ty == "AddressSpace") return TypeCode::ADDRESS_SPACE;
    if (ty == "Ptr") return TypeCode::PTR;
    if (ty == "StringHandle") return TypeCode::STRING_HANDLE;
    if (ty == "I4") return TypeCode::I4;
    if (ty == "I8") return TypeCode::I8;
    if (ty == "I16") return TypeCode::I16;
    if (ty == "I32") return TypeCode::I32;
    if (ty == "I64") return TypeCode::I64;
    if (ty == "I128") return TypeCode::I128;
    if (ty == "I256") return TypeCode::I256;
    if (ty == "I512") return TypeCode::I512;
    if (ty == "F8") return TypeCode::F8;
    if (ty == "BF16") return TypeCode::BF16;
    if (ty == "F16") return TypeCode::F16;
    if (ty == "F32") return TypeCode::F32;
    if (ty == "F64") return TypeCode::F64;
    if (ty == "F128") return TypeCode::F128;
    if (ty == "Vec3xF32") return TypeCode::VEC3_F32;
    if (ty == "Vec4xF32") return TypeCode::VEC4_F32;
    if (ty == "Mat4x4F32") return TypeCode::MAT4X4_F32;
    if (ty == "QuatF32") return TypeCode::QUAT_F32;
    if (ty == "VectorF32x16") return TypeCode::VECTOR_F32X16;
    if (ty == "VectorI32x16") return TypeCode::VECTOR_I32X16;
    if (ty == "VectorF16x32") return TypeCode::VECTOR_F16X32;
    if (ty == "TensorFP8") return TypeCode::TENSOR_FP8;
    if (ty == "TensorBF16") return TypeCode::TENSOR_BF16;
    if (ty == "TensorF32") return TypeCode::TENSOR_F32;
    if (ty == "TensorI64") return TypeCode::TENSOR_I64;
    return std::nullopt;
}

[[nodiscard]] constexpr bool has_expected_result_type(OpCode opcode) noexcept {
    const auto expected = expected_result_type_code(opcode);
    return expected.has_value() && *expected != TypeCode::VOID_T;
}

} // namespace dgtl::ir::interfaces