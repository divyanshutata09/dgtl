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

/* DGTL HIR — Canonical Schema Access Layer. */
module;
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
export module dgtl.ir_schema;
import dgtl.ir_enums;
import dgtl.ir.gen;

export namespace dgtl::ir::schema {
inline constexpr std::uint32_t kSchemaMajor = 3;
inline constexpr std::uint32_t kSchemaMinor = 0;
inline constexpr std::uint16_t kOpcodeWidth = 16;
inline constexpr std::size_t kCanonicalOperationCount = gen::kGeneratedOpDefs.size();

struct Operation {
    OpCode opcode{};
    std::uint16_t stable_id{};
    std::string_view name{};
    std::string_view dialect{};
    std::string_view result_type{};
    std::uint64_t trait_bits{};
    std::uint64_t interface_bits{};
    std::uint64_t attribute_bits{};
    std::uint8_t operand_count{};
    bool has_result{};
    bool canonical{false};
};

[[nodiscard]] constexpr const gen::OpDefMetadata* lookup(OpCode op) noexcept {
    return gen::find_generated_op_def(op);
}

[[nodiscard]] constexpr std::optional<Operation> materialize(OpCode op) noexcept {
    if (const auto* d = gen::find_generated_op_def(op)) {
        return Operation{d->opcode,d->stable_id,d->name,d->dialect,d->result_type,
            d->trait_bits,d->interface_bits,d->attribute_bits,d->operand_count,
            d->has_result,true};
    }
    return std::nullopt;
}
[[nodiscard]] constexpr bool is_canonical(OpCode op) noexcept { return gen::find_generated_op_def(op) != nullptr; }
[[nodiscard]] constexpr std::uint16_t stable_id(OpCode op) noexcept {
    if (const auto* d=gen::find_generated_op_def(op)) return d->stable_id;
    return static_cast<std::uint16_t>(op);
}
[[nodiscard]] constexpr std::string_view name(OpCode op) noexcept {
    if (const auto* d=gen::find_generated_op_def(op)) return d->name;
    return {};
}
[[nodiscard]] constexpr std::uint64_t trait_bits(OpCode op) noexcept {
    if (const auto* d=gen::find_generated_op_def(op)) return d->trait_bits;
    return 0;
}
[[nodiscard]] constexpr std::uint64_t interface_bits(OpCode op) noexcept {
    if (const auto* d=gen::find_generated_op_def(op)) return d->interface_bits;
    return 0;
}
[[nodiscard]] constexpr std::uint64_t attribute_bits(OpCode op) noexcept {
    if (const auto* d=gen::find_generated_op_def(op)) return d->attribute_bits;
    return 0;
}
[[nodiscard]] constexpr bool has_unique_stable_ids() noexcept {
    for (std::size_t i=0;i<gen::kGeneratedOpDefs.size();++i)
        for (std::size_t j=i+1;j<gen::kGeneratedOpDefs.size();++j)
            if (gen::kGeneratedOpDefs[i].stable_id==gen::kGeneratedOpDefs[j].stable_id) return false;
    return true;
}
[[nodiscard]] constexpr bool validate_canonical_schema() noexcept {
    return kCanonicalOperationCount > 0 && has_unique_stable_ids() && gen::validate_generated_op_defs();
}
} // namespace dgtl::ir::schema

export namespace dgtl::ir {
[[nodiscard]] constexpr std::string_view opcode_name(OpCode op) noexcept {
    return schema::name(op);
}
} // namespace dgtl::ir
