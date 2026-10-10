/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * DGTL HIR operation traits.
 *
 * Canonical semantic classification is generated from:
 *   gen/defs/phase1_core_ops.dgtl_def
 *
 * This file contains no canonical opcode switch tables. If a semantic
 * property belongs to an operation, it must be represented by the DSL and
 * projected through DGTL-Gen.
 */
module;

#include <cstdint>
#include <string_view>

export module dgtl.ir_traits;

import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir.gen;

export namespace dgtl::ir {

struct IRTraits {
    [[nodiscard]] static constexpr bool is_canonical(OpCode op) noexcept {
        return schema::is_canonical(op);
    }

    [[nodiscard]] static constexpr std::uint32_t schema_traits(OpCode op) noexcept {
        return schema::trait_bits(op);
    }

    [[nodiscard]] static constexpr std::uint32_t schema_interfaces(OpCode op) noexcept {
        return schema::interface_bits(op);
    }

    [[nodiscard]] static constexpr std::uint32_t schema_attributes(OpCode op) noexcept {
        return schema::attribute_bits(op);
    }

    [[nodiscard]] static constexpr bool has_trait(OpCode op, gen::GenTrait trait) noexcept {
        return gen::op_has_trait(op, trait);
    }

    [[nodiscard]] static constexpr bool has_interface(OpCode op, gen::GenInterface iface) noexcept {
        return gen::op_implements_interface(op, iface);
    }

    [[nodiscard]] static constexpr bool has_attribute(OpCode op, gen::GenAttribute attr) noexcept {
        return gen::op_has_attribute(op, attr);
    }

    [[nodiscard]] static constexpr bool has_result(OpCode op) noexcept {
        if (auto d = schema::lookup(op)) return d->has_result;
        return false;
    }

    [[nodiscard]] static constexpr std::uint8_t operand_count(OpCode op) noexcept {
        if (auto d = schema::lookup(op)) return d->operand_count;
        return 0;
    }

    [[nodiscard]] static constexpr std::string_view name(OpCode op) noexcept {
        return schema::name(op);
    }

    [[nodiscard]] static constexpr bool is_branch(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::IsTerminator) &&
               has_interface(op, gen::GenInterface::ControlFlow);
    }

    [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::IsTerminator);
    }

    [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::IsMemoryOp) ||
               has_trait(op, gen::GenTrait::IsMemoryRead) ||
               has_trait(op, gen::GenTrait::IsMemoryWrite) ||
               has_interface(op, gen::GenInterface::Memory);
    }

    [[nodiscard]] static constexpr bool has_side_effects(OpCode op,
                                                         std::uint16_t flags) noexcept {
        if ((flags & Flags::HAS_SIDE_EFFECTS) != 0u) return true;
        if ((flags & Flags::IS_VOLATILE) != 0u) return true;
        if (!is_canonical(op)) return true; // invalid HIR operations are conservatively effectful until rejected by verification
        return !has_trait(op, gen::GenTrait::HasNoSideEffects);
    }

    [[nodiscard]] static constexpr bool is_simd(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::SIMD) ||
               has_trait(op, gen::GenTrait::IsVectorizable);
    }

    [[nodiscard]] static constexpr bool is_integer_op(OpCode op) noexcept {
        const auto d = schema::lookup(op);
        if (!d) return false;
        return d->result_type.starts_with("I") &&
               (has_interface(op, gen::GenInterface::Arithmetic) ||
                has_interface(op, gen::GenInterface::BitwiseOp));
    }

    [[nodiscard]] static constexpr bool is_float_op(OpCode op) noexcept {
        const auto d = schema::lookup(op);
        if (!d) return false;
        return d->result_type.starts_with("F") ||
               d->result_type == "BF16";
    }

    [[nodiscard]] static constexpr bool is_compare_op(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Comparison);
    }

    [[nodiscard]] static constexpr bool is_string_op(OpCode op) noexcept {
        const auto d = schema::lookup(op);
        return d && (d->result_type == "StringHandle" ||
                     d->name.starts_with("String"));
    }

    [[nodiscard]] static constexpr bool is_control_flow(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::ControlFlow);
    }

    [[nodiscard]] static constexpr bool is_valid_slot(std::uint32_t slot_id) noexcept {
        return slot_id != INVALID_SLOT && slot_id < 0xFFFFFF00u;
    }

    [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {
        // Cost is deliberately a neutral HIR heuristic. Target-specific costs
        // belong after legalization in the target backend.
        if (is_branch(op)) return 5.0;
        if (is_simd(op)) return 3.0;
        return 1.0;
    }

    [[nodiscard]] static constexpr bool is_commutative(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::Commutative);
    }

    [[nodiscard]] static constexpr bool is_associative(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::Associative);
    }

    [[nodiscard]] static constexpr bool is_single_def_use(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::SingleDefUse);
    }

    [[nodiscard]] static constexpr bool can_fold_constants(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::ConstantFoldable);
    }

    [[nodiscard]] static constexpr bool is_memory_barrier(OpCode op) noexcept {
        return has_trait(op, gen::GenTrait::MemoryBarrier);
    }

    [[nodiscard]] static constexpr bool can_be_reordered(OpCode a, OpCode b) noexcept {
        return !is_memory_barrier(a) && !is_memory_barrier(b) &&
               !has_side_effects(a, Flags::NONE) &&
               !has_side_effects(b, Flags::NONE);
    }

    [[nodiscard]] static constexpr double estimate_latency_cycles(OpCode op) noexcept {
        // HIR deliberately does not encode target microarchitecture latency.
        // This is a stable structural estimate only.
        if (is_branch(op)) return 1.0;
        return 1.0;
    }

    [[nodiscard]] static constexpr std::uint32_t estimate_throughput_slots(OpCode op) noexcept {
        return is_branch(op) ? 0u : 1u;
    }

    [[nodiscard]] static constexpr bool is_ownership_op(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Ownership);
    }

    [[nodiscard]] static constexpr bool is_concurrency_op(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Concurrency);
    }

    [[nodiscard]] static constexpr bool is_dsa_op(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::DSA);
    }

    [[nodiscard]] static constexpr bool is_tensor_op(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Tensor);
    }

    [[nodiscard]] static constexpr bool must_synchronize(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Concurrency) &&
               !has_trait(op, gen::GenTrait::IsAsyncSpawnable);
    }

    [[nodiscard]] static constexpr bool may_create_deadlock(OpCode op) noexcept {
        return has_interface(op, gen::GenInterface::Concurrency) &&
               !has_trait(op, gen::GenTrait::HasNoSideEffects);
    }

    [[nodiscard]] static constexpr bool is_frontend_specific(OpCode op) noexcept {
        return is_ownership_op(op) || is_concurrency_op(op) ||
               is_dsa_op(op) || is_tensor_op(op);
    }
};

} // namespace dgtl::ir


