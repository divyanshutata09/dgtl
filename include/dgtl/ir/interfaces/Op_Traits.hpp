/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#pragma once

#include <cstdint>

#include "dgtl/ir/base/IR_Node.hpp"
#include "dgtl/ir/base/IR_Traits_Static.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"

namespace dgtl::ir::interfaces {

enum class OpTrait : uint32_t {
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

enum class OpInterface : uint32_t {
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

struct OpMetadata {
    OpCode opcode = OpCode::NOP;
    uint32_t trait_bits = 0;
    uint32_t interface_bits = 0;
    uint8_t operand_count = 0;
    bool has_result = false;
};

[[nodiscard]] constexpr uint32_t to_bits(OpTrait trait) noexcept {
    return static_cast<uint32_t>(trait);
}

[[nodiscard]] constexpr uint32_t to_bits(OpInterface iface) noexcept {
    return static_cast<uint32_t>(iface);
}

[[nodiscard]] inline OpMetadata get_metadata(OpCode opcode) noexcept {
    if (const auto* def = dgtl::ir::gen::find_generated_op_def(opcode)) {
        OpMetadata md{};
        md.opcode = opcode;
        md.operand_count = def->operand_count;
        md.has_result = def->has_result;

        md.trait_bits = static_cast<uint32_t>(def->trait_bits);
        md.interface_bits = static_cast<uint32_t>(def->interface_bits);

        return md;
    }

    // Conservative fallback for non-generated opcodes.

    // Final fallback for unregistered opcodes.
    OpMetadata fallback{};
    fallback.opcode = opcode;
    if (IRTraits::is_terminator(opcode)) {
        fallback.trait_bits |= to_bits(OpTrait::IsTerminator);
        fallback.interface_bits |= to_bits(OpInterface::ControlFlow) | to_bits(OpInterface::Terminator);
    }
    if (!IRTraits::has_side_effects(opcode, Flags::NONE)) {
        fallback.trait_bits |= to_bits(OpTrait::HasNoSideEffects);
    }
    return fallback;
}

[[nodiscard]] inline bool has_trait(OpCode opcode, OpTrait trait) noexcept {
    return (get_metadata(opcode).trait_bits & to_bits(trait)) != 0u;
}

[[nodiscard]] inline bool implements_interface(OpCode opcode, OpInterface iface) noexcept {
    return (get_metadata(opcode).interface_bits & to_bits(iface)) != 0u;
}

} // namespace dgtl::ir::interfaces
