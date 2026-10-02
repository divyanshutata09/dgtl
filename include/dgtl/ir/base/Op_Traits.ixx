/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 
 * Canonical operation metadata is generated exclusively from
 * gen/defs/phase1_core_ops.dgtl_def. No handwritten fallback table is allowed.
 */
module;

#include <cstdint>
#include <optional>
#include <string_view>

export module dgtl.op_traits;

import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir.gen;

export namespace dgtl::ir::interfaces {

enum class OpTrait : std::uint32_t {
    None = 0,
    Commutative = 1u << 0,
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
    IsLatencyCritical = 1u << 14,
    IsHardwareIntrinsic = 1u << 15,
    Associative = 1u << 16,
    ConstantFoldable = 1u << 17,
    MemoryBarrier = 1u << 18,
    SingleDefUse = 1u << 19,
};

[[nodiscard]] constexpr OpTrait operator|(OpTrait a, OpTrait b) noexcept {
    return static_cast<OpTrait>(
        static_cast<std::uint32_t>(a) |
        static_cast<std::uint32_t>(b)
    );
}

[[nodiscard]] constexpr OpTrait operator&(OpTrait a, OpTrait b) noexcept {
    return static_cast<OpTrait>(
        static_cast<std::uint32_t>(a) &
        static_cast<std::uint32_t>(b)
    );
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
    PointerMath = 1u << 12,
    NeuralNet = 1u << 13,
    SpatialMath = 1u << 14,
};

[[nodiscard]] constexpr OpInterface operator|(OpInterface a, OpInterface b) noexcept {
    return static_cast<OpInterface>(
        static_cast<std::uint32_t>(a) |
        static_cast<std::uint32_t>(b)
    );
}

[[nodiscard]] constexpr OpInterface operator&(OpInterface a, OpInterface b) noexcept {
    return static_cast<OpInterface>(
        static_cast<std::uint32_t>(a) &
        static_cast<std::uint32_t>(b)
    );
}

struct OpMetadata {
    OpCode opcode = OpCode::NOP;
    std::uint16_t stable_id = 0;

    // Canonical schema uses uint64_t for these bitfields.
    std::uint64_t trait_bits = 0;
    std::uint64_t interface_bits = 0;
    std::uint64_t attribute_bits = 0;

    std::uint8_t operand_count = 0;
    bool has_result = false;

    std::string_view name = "UnknownOp";
    std::string_view dialect = {};
    std::string_view result_type = {};
};

[[nodiscard]] constexpr std::optional<OpMetadata>
canonical_schema_metadata(OpCode op) noexcept {
    if (const auto d = schema::lookup(op)) {
        return OpMetadata{
            d->opcode,
            d->stable_id,
            d->trait_bits,
            d->interface_bits,
            d->attribute_bits,
            d->operand_count,
            d->has_result,
            d->name,
            d->dialect,
            d->result_type
        };
    }

    return std::nullopt;
}

[[nodiscard]] constexpr bool is_canonical(OpCode op) noexcept {
    return schema::is_canonical(op);
}

[[nodiscard]] constexpr std::optional<TypeCode>
expected_result_type_code(OpCode op) noexcept {
    const auto* def = gen::find_generated_op_def(op);
    if (!def || !def->has_result) {
        return std::nullopt;
    }
    const std::string_view rt = def->result_type;
    if (rt == "void") return TypeCode::VOID_T;
    if (rt == "Bool") return TypeCode::BOOL;
    if (rt == "Predicate") return TypeCode::PREDICATE;
    if (rt == "AddressSpace") return TypeCode::ADDRESS_SPACE;
    if (rt == "Ptr") return TypeCode::PTR;
    if (rt == "StringHandle") return TypeCode::STRING_HANDLE;

    if (rt == "I4") return TypeCode::I4;
    if (rt == "I8") return TypeCode::I8;
    if (rt == "I16") return TypeCode::I16;
    if (rt == "I32") return TypeCode::I32;
    if (rt == "I64") return TypeCode::I64;
    if (rt == "I128") return TypeCode::I128;
    if (rt == "I256") return TypeCode::I256;
    if (rt == "I512") return TypeCode::I512;

    if (rt == "F8") return TypeCode::F8;
    if (rt == "BF16") return TypeCode::BF16;
    if (rt == "F16") return TypeCode::F16;
    if (rt == "F32") return TypeCode::F32;
    if (rt == "F64") return TypeCode::F64;
    if (rt == "F128") return TypeCode::F128;

    if (rt == "Vec3xF32") return TypeCode::VEC3_F32;
    if (rt == "Vec4xF32") return TypeCode::VEC4_F32;
    if (rt == "Mat4x4F32") return TypeCode::MAT4X4_F32;
    if (rt == "QuatF32") return TypeCode::QUAT_F32;
    if (rt == "VectorF32x16") return TypeCode::VECTOR_F32X16;
    if (rt == "VectorI32x16") return TypeCode::VECTOR_I32X16;
    if (rt == "VectorF16x32") return TypeCode::VECTOR_F16X32;

    if (rt == "TensorFP8") return TypeCode::TENSOR_FP8;
    if (rt == "TensorBF16") return TypeCode::TENSOR_BF16;
    if (rt == "TensorF32") return TypeCode::TENSOR_F32;
    if (rt == "TensorI64") return TypeCode::TENSOR_I64;

    return std::nullopt;
}

} // namespace dgtl::ir::interfaces

