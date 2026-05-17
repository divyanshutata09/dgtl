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
/* Auto-generated from include/dgtl/isa/defs/x64_core.dgtl_isel. Do not edit manually. */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "dgtl/ir/base/IR_Node.hpp"
#include "dgtl/isa/lowering/gen/X64_ISel_Rules_Generated.hpp"

namespace dgtl::isa::lowering {

enum class MachineForm : uint8_t {
    Unhandled = 0,
    NoOp,
    Move,
    LoadImmediate,
    LoadStack,
    StoreStack,
    IntegerBinary32,
    IntegerBinary64,
    FloatBinary32,
    FloatTernaryFma32,
    Compare32,
    VectorBinary512,
    VectorGather512,
    VectorStore512,
    Call,
    Return,
    Goto,
    BranchCond,
    BranchCompare,
    Syscall,
};

namespace generated {

enum class X64IntegerBinary32Kind : uint8_t {
    None = 0,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    And,
    Or,
    Xor,
    Shl,
    Shr,
};

enum class X64IntegerBinary64Kind : uint8_t {
    None = 0,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
};

enum class X64FloatBinary32Kind : uint8_t {
    None = 0,
    Add,
    Mul,
};

enum class X64Compare32Kind : uint8_t {
    None = 0,
    Any,
};

enum class X64VectorBinary512Kind : uint8_t {
    None = 0,
    Add,
    Mul,
};

inline constexpr auto kMachineFormTable = []() constexpr {
    std::array<MachineForm, 2048> table{};
    table.fill(MachineForm::Unhandled);
    auto set = [&](ir::OpCode opcode, MachineForm form) constexpr {
        const size_t index = static_cast<size_t>(opcode);
        if (index < table.size()) {
            table[index] = form;
        }
    };

    set(ir::OpCode::NOP, MachineForm::NoOp);
    set(ir::OpCode::LABEL, MachineForm::NoOp);
    set(ir::OpCode::PHI_NODE, MachineForm::NoOp);
    set(ir::OpCode::REG_MOVE, MachineForm::Move);
    set(ir::OpCode::LOAD_IMM, MachineForm::LoadImmediate);
    set(ir::OpCode::IMMEDIATE, MachineForm::LoadImmediate);
    set(ir::OpCode::LOAD, MachineForm::LoadStack);
    set(ir::OpCode::STORE, MachineForm::StoreStack);
    set(ir::OpCode::ADD_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SUB_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::MUL_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::DIV_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::MOD_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::AND_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::OR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::XOR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SHL_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SHR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::ADD_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::SUB_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::MUL_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::DIV_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::MOD_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::ADD_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::MUL_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::FMA_F32, MachineForm::FloatTernaryFma32);
    set(ir::OpCode::CMP_EQ_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_NE_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_LT_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_LE_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_GT_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_GE_I32, MachineForm::Compare32);
    set(ir::OpCode::VEC_ADD_F32_512, MachineForm::VectorBinary512);
    set(ir::OpCode::VEC_MUL_F32_512, MachineForm::VectorBinary512);
    set(ir::OpCode::GATHER_F32_512, MachineForm::VectorGather512);
    set(ir::OpCode::GATHER_16, MachineForm::VectorGather512);
    set(ir::OpCode::STORE_V512, MachineForm::VectorStore512);
    set(ir::OpCode::CALL, MachineForm::Call);
    set(ir::OpCode::RET, MachineForm::Return);
    set(ir::OpCode::GOTO, MachineForm::Goto);
    set(ir::OpCode::BRANCH, MachineForm::Goto);
    set(ir::OpCode::BRANCH_COND, MachineForm::BranchCond);
    set(ir::OpCode::BRANCH_COMPARE_FUSED, MachineForm::BranchCompare);
    set(ir::OpCode::SYSCALL, MachineForm::Syscall);
    return table;
}();

inline constexpr auto kIntegerBinary32KindTable = []() constexpr {
    std::array<X64IntegerBinary32Kind, 2048> table{};
    table.fill(X64IntegerBinary32Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_I32)] = X64IntegerBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_I32)] = X64IntegerBinary32Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_I32)] = X64IntegerBinary32Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_I32)] = X64IntegerBinary32Kind::Div;
    table[static_cast<size_t>(ir::OpCode::MOD_I32)] = X64IntegerBinary32Kind::Mod;
    table[static_cast<size_t>(ir::OpCode::AND_I32)] = X64IntegerBinary32Kind::And;
    table[static_cast<size_t>(ir::OpCode::OR_I32)] = X64IntegerBinary32Kind::Or;
    table[static_cast<size_t>(ir::OpCode::XOR_I32)] = X64IntegerBinary32Kind::Xor;
    table[static_cast<size_t>(ir::OpCode::SHL_I32)] = X64IntegerBinary32Kind::Shl;
    table[static_cast<size_t>(ir::OpCode::SHR_I32)] = X64IntegerBinary32Kind::Shr;
    return table;
}();

inline constexpr auto kIntegerBinary64KindTable = []() constexpr {
    std::array<X64IntegerBinary64Kind, 2048> table{};
    table.fill(X64IntegerBinary64Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_I64)] = X64IntegerBinary64Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_I64)] = X64IntegerBinary64Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_I64)] = X64IntegerBinary64Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_I64)] = X64IntegerBinary64Kind::Div;
    table[static_cast<size_t>(ir::OpCode::MOD_I64)] = X64IntegerBinary64Kind::Mod;
    return table;
}();

inline constexpr auto kFloatBinary32KindTable = []() constexpr {
    std::array<X64FloatBinary32Kind, 2048> table{};
    table.fill(X64FloatBinary32Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_F32)] = X64FloatBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::MUL_F32)] = X64FloatBinary32Kind::Mul;
    return table;
}();

inline constexpr auto kCompare32KindTable = []() constexpr {
    std::array<X64Compare32Kind, 2048> table{};
    table.fill(X64Compare32Kind::None);
    table[static_cast<size_t>(ir::OpCode::CMP_EQ_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_NE_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_LT_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_LE_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_GT_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_GE_I32)] = X64Compare32Kind::Any;
    return table;
}();

inline constexpr auto kVectorBinary512KindTable = []() constexpr {
    std::array<X64VectorBinary512Kind, 2048> table{};
    table.fill(X64VectorBinary512Kind::None);
    table[static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512)] = X64VectorBinary512Kind::Add;
    table[static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512)] = X64VectorBinary512Kind::Mul;
    return table;
}();

[[nodiscard]] constexpr MachineForm classify_machine_form(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kMachineFormTable.size() ? kMachineFormTable[index] : kMachineFormTable[0];
}

[[nodiscard]] constexpr X64IntegerBinary32Kind integer_binary32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kIntegerBinary32KindTable.size() ? kIntegerBinary32KindTable[index] : X64IntegerBinary32Kind::None;
}

[[nodiscard]] constexpr X64IntegerBinary64Kind integer_binary64_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kIntegerBinary64KindTable.size() ? kIntegerBinary64KindTable[index] : X64IntegerBinary64Kind::None;
}

[[nodiscard]] constexpr X64FloatBinary32Kind float_binary32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kFloatBinary32KindTable.size() ? kFloatBinary32KindTable[index] : X64FloatBinary32Kind::None;
}

[[nodiscard]] constexpr X64Compare32Kind compare32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kCompare32KindTable.size() ? kCompare32KindTable[index] : X64Compare32Kind::None;
}

[[nodiscard]] constexpr X64VectorBinary512Kind vector_binary512_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kVectorBinary512KindTable.size() ? kVectorBinary512KindTable[index] : X64VectorBinary512Kind::None;
}

} // namespace generated
} // namespace dgtl::isa::lowering
