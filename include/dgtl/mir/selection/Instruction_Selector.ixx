/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Transition Gate Instruction Selector (HIR -> Generic MIR)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.mir.instruction_selector;

// Import native compiler base interfaces and sibling partitions
import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir.gen;
import dgtl.ir_traits;
import dgtl.ir_node;
import dgtl.mir.runtime_policy;

import dgtl.mir.isel_gen;
import dgtl.mir.machine_ir;

import dgtl.mir.target_isel;

// Explicitly import target descriptions to resolve ArchType and TargetMachine
import dgtl.target;
import dgtl.ir.stream;

/**
 * @brief Instruction Selector (ISel)
 *
 * The official architectural transition gate between high-level IR (ir::OpCode)
 * and the Machine IR layer (dgtl::machine::GenericMachineOpcode).
 *
 * Performs tree pattern matching, addressing mode discovery, and populates
 * MachineSelection records with precise GM_* opcodes. Downstream passes operate
 * exclusively on MachineOpcode.
 */
export namespace dgtl::mir::isel {

using dgtl::mir::MachineForm;

struct ISelReplacement {
    bool matched = false;
    ir::IRNode node{};
    std::string_view rule_name{};
    uint32_t benefit = 0;
};

struct SelectedInstruction {
    MachineForm form = MachineForm::Unhandled;
    ir::IRNode node{};
    ir::OpCode semantic_opcode = ir::OpCode::NOP;
    machine::MachineSemanticKind semantic_kind = machine::MachineSemanticKind::None;
    std::string_view rule_name{};
    bool matched_rule = false;
    std::optional<machine::MachineMemoryOperand> memory_operand = std::nullopt;
    std::vector<uint32_t> overflow_operands{};
    machine::RegisterClassKind result_class = machine::RegisterClassKind::General;
    std::vector<machine::RegisterClassKind> call_arg_classes{};
};

/* ============================================================================
 * Canonical semantic classification & Generic Machine Opcode Mapping (LUT)
 * ========================================================================== */

[[nodiscard]] constexpr machine::MachineForm to_machine_form(MachineForm form) noexcept {
    return form;
}

namespace detail {

inline constexpr std::size_t kMaxOpCodeCount = 2048;

inline constexpr auto kOpCodeToGenericMachineTable = []() constexpr {
    using namespace ir;
    using namespace machine;

    std::array<GenericMachineOpcode, kMaxOpCodeCount> table{};
    table.fill(GM_NOOP);

    auto bind = [&](OpCode op, GenericMachineOpcode gm) constexpr {
        const auto idx = static_cast<std::size_t>(op);
        if (idx < kMaxOpCodeCount) {
            table[idx] = gm;
        }
    };

    bind(OpCode::NOP, GM_NOOP);
    bind(OpCode::REG_MOVE, GM_COPY);
    bind(OpCode::MOV, GM_COPY);
    bind(OpCode::LOAD_IMM, GM_IMMEDIATE);
    bind(OpCode::IMMEDIATE, GM_IMMEDIATE);
    bind(OpCode::CONST, GM_IMMEDIATE);

    // 32-bit Integer Arithmetic & Bitwise
    bind(OpCode::ADD_I32, GM_IADD32);
    bind(OpCode::ADD_I8, GM_IADD32);
    bind(OpCode::SUB_I32, GM_ISUB32);
    bind(OpCode::MUL_I32, GM_IMUL32);
    bind(OpCode::DIV_I32, GM_IDIV32);
    bind(OpCode::MOD_I32, GM_IREM32);
    bind(OpCode::AND_I32, GM_IAND32);
    bind(OpCode::OR_I32, GM_IOR32);
    bind(OpCode::XOR_I32, GM_IXOR32);
    bind(OpCode::SHL_I32, GM_ISHL32);
    bind(OpCode::SHR_I32, GM_ISHR32);

    // 64-bit Integer Arithmetic & Bitwise
    bind(OpCode::ADD_I64, GM_IADD64);
    bind(OpCode::ADD_I128, GM_IADD64);
    bind(OpCode::ADD_I256, GM_IADD64);
    bind(OpCode::SUB_I64, GM_ISUB64);
    bind(OpCode::MUL_I64, GM_IMUL64);
    bind(OpCode::DIV_I64, GM_IDIV64);
    bind(OpCode::MOD_I64, GM_IREM64);
    bind(OpCode::AND_I64, GM_IAND64);
    bind(OpCode::OR_I64, GM_IOR64);
    bind(OpCode::XOR_I64, GM_IXOR64);
    bind(OpCode::SHL_I64, GM_ISHL64);
    bind(OpCode::SHR_I64, GM_ISHR64);

    // 32-bit Floating Point
    bind(OpCode::ADD_F32, GM_FADD32);
    bind(OpCode::ADD_BF16, GM_FADD32);
    bind(OpCode::ADD_F8, GM_FADD32);
    bind(OpCode::SUB_F32, GM_FSUB32);
    bind(OpCode::MUL_F32, GM_FMUL32);
    bind(OpCode::DIV_F32, GM_FDIV32);
    bind(OpCode::FMA_F32, GM_FFMA32);
    bind(OpCode::FAST_INV_SQRT, GM_FAST_INV_SQRT);

    // 64-bit Floating Point
    bind(OpCode::ADD_F64, GM_FADD64);
    bind(OpCode::SUB_F64, GM_FSUB64);
    bind(OpCode::MUL_F64, GM_FMUL64);
    bind(OpCode::DIV_F64, GM_FDIV64);

    // Comparison & Selection
    bind(OpCode::CMP_EQ_I32, GM_ICMP32);
    bind(OpCode::CMP_NE_I32, GM_ICMP32);
    bind(OpCode::CMP_LT_I32, GM_ICMP32);
    bind(OpCode::CMP_LE_I32, GM_ICMP32);
    bind(OpCode::CMP_GT_I32, GM_ICMP32);
    bind(OpCode::CMP_GE_I32, GM_ICMP32);

    // Control Flow
    bind(OpCode::GOTO, GM_GOTO);
    bind(OpCode::BRANCH, GM_GOTO);
    bind(OpCode::BRANCH_COND, GM_BR_COND);
    bind(OpCode::BRANCH_LIKELY, GM_BR_COND);
    bind(OpCode::BRANCH_UNLIKELY, GM_BR_COND);
    bind(OpCode::BRANCH_COMPARE_FUSED, GM_BR_COMPARE);
    bind(OpCode::SWITCH_DISPATCH, GM_BR_TABLE);
    bind(OpCode::CALL, GM_CALL);
    bind(OpCode::CALL_VARIADIC, GM_CALL_VARIADIC);
    bind(OpCode::RET, GM_RETURN);
    bind(OpCode::SYSCALL, GM_SYSCALL);

    // Memory Operations & Addressing
    bind(OpCode::LOAD, GM_LOAD);
    bind(OpCode::VOLATILE_LOAD, GM_LOAD);
    bind(OpCode::STORE, GM_STORE);
    bind(OpCode::VOLATILE_STORE, GM_STORE);
    bind(OpCode::VEC_LOAD, GM_VEC_LOAD);
    bind(OpCode::VEC_STORE, GM_VEC_STORE);
    bind(OpCode::STORE_V512, GM_VEC_STORE);
    bind(OpCode::GATHER_16, GM_VEC_GATHER);
    bind(OpCode::GATHER_F32_512, GM_VEC_GATHER);
    bind(OpCode::SIMD_SCATTER_INDIRECT, GM_VEC_SCATTER);
    bind(OpCode::LEA, GM_LEA);
    bind(OpCode::GEP, GM_GEP);
    bind(OpCode::PTR_DIFF, GM_PTR_DIFF);
    bind(OpCode::PTR_TO_INT, GM_PTR_TO_INT);
    bind(OpCode::INT_TO_PTR, GM_INT_TO_PTR);
    bind(OpCode::PREFETCH, GM_PREFETCH);
    bind(OpCode::PREFETCH_T0, GM_PREFETCH_T0);
    bind(OpCode::CLFLUSH, GM_CLFLUSH);
    bind(OpCode::READ_TSC, GM_READ_TSC);

    // Synchronization & Atomics
    bind(OpCode::MEMORY_FENCE, GM_FENCE);
    bind(OpCode::ACQUIRE_FENCE, GM_ACQUIRE_FENCE);
    bind(OpCode::RELEASE_FENCE, GM_RELEASE_FENCE);
    bind(OpCode::ATOMIC_FENCE, GM_ATOMIC_FENCE);
    bind(OpCode::ATOMIC_LOAD, GM_ATOMIC_LOAD);
    bind(OpCode::ATOMIC_STORE, GM_ATOMIC_STORE);
    bind(OpCode::ATOMIC_SNAPSHOT, GM_ATOMIC_SNAPSHOT);
    bind(OpCode::BARRIER_SYNC, GM_BARRIER_SYNC);
    bind(OpCode::SYNC_BARRIER, GM_SYNC_BARRIER);
    bind(OpCode::LOCK_ACQUIRE, GM_LOCK_ACQUIRE);
    bind(OpCode::LOCK_RELEASE, GM_LOCK_RELEASE);
    bind(OpCode::SPIN_ACQUIRE, GM_SPIN_ACQUIRE);

    // Async, Fibers, Coroutines, Threads
    bind(OpCode::ASYNC_AWAIT, GM_ASYNC_AWAIT);
    bind(OpCode::ASYNC_LAUNCH, GM_ASYNC_LAUNCH);
    bind(OpCode::ASYNC_SPAWN, GM_ASYNC_SPAWN);
    bind(OpCode::FIBER_YIELD, GM_FIBER_YIELD);
    bind(OpCode::FIBER_YIELD_LOOP, GM_FIBER_YIELD_LOOP);
    bind(OpCode::SUSPEND_CTX, GM_SUSPEND_CTX);
    bind(OpCode::RESUME_CTX, GM_RESUME_CTX);
    bind(OpCode::THREAD_SPAWN, GM_THREAD_SPAWN);

    // Strings
    bind(OpCode::STRING_LENGTH, GM_STRING_LENGTH);
    bind(OpCode::STRING_COMPARE, GM_STRING_COMPARE);
    bind(OpCode::STRING_CONCAT, GM_STRING_CONCAT);
    bind(OpCode::STRING_SUBSTRING, GM_STRING_SUBSTRING);

    // Dynamic Allocations & Data Structures
    bind(OpCode::DSA_ALLOCATE, GM_DSA_ALLOCATE);
    bind(OpCode::DSA_DEALLOCATE, GM_DSA_DEALLOCATE);
    bind(OpCode::RING_PUSH_FREE, GM_RING_PUSH_FREE);
    bind(OpCode::DEOPT_TRIGGER, GM_DEOPT_TRIGGER);
    bind(OpCode::TREE_BUILD, GM_TREE_BUILD);
    bind(OpCode::TREE_TRAVERSE, GM_TREE_TRAVERSE);
    bind(OpCode::TRIE_BUILD, GM_TRIE_BUILD);
    bind(OpCode::TRIE_INSERT, GM_TRIE_INSERT);
    bind(OpCode::TRIE_LOOKUP, GM_TRIE_LOOKUP);
    bind(OpCode::GRAPH_BUILD, GM_GRAPH_BUILD);
    bind(OpCode::GRAPH_TRAVERSE, GM_GRAPH_TRAVERSE);
    bind(OpCode::TUPLE_PACK, GM_TUPLE_PACK);
    bind(OpCode::TUPLE_UNPACK, GM_TUPLE_UNPACK);

    // Tensor & Hardware Accelerators
    bind(OpCode::TENSOR_GEMM, GM_TENSOR_GEMM);
    bind(OpCode::TENSOR_BROADCAST, GM_TENSOR_BROADCAST);
    bind(OpCode::TENSOR_CONCAT, GM_TENSOR_CONCAT);
    bind(OpCode::TENSOR_CONV, GM_TENSOR_CONV);
    bind(OpCode::TENSOR_GATHER, GM_TENSOR_GATHER);
    bind(OpCode::TENSOR_POOL, GM_TENSOR_POOL);
    bind(OpCode::TENSOR_REDUCE, GM_TENSOR_REDUCE);
    bind(OpCode::TENSOR_RESHAPE, GM_TENSOR_RESHAPE);
    bind(OpCode::TENSOR_SCATTER_D, GM_TENSOR_SCATTER_D);
    bind(OpCode::TENSOR_SLICE, GM_TENSOR_SLICE);
    bind(OpCode::TENSOR_SOFTMAX, GM_TENSOR_SOFTMAX);
    bind(OpCode::TENSOR_TRANSPOSE, GM_TENSOR_TRANSPOSE);
    bind(OpCode::MATMUL_BF16, GM_MATMUL_BF16);
    bind(OpCode::MATMUL_FP8, GM_MATMUL_FP8);
    bind(OpCode::SDPA_F16, GM_SDPA_F16);
    bind(OpCode::SOFTMAX_FAST, GM_SOFTMAX_FAST);
    bind(OpCode::LAYER_NORM, GM_LAYER_NORM);
    bind(OpCode::ROPE_EMBED, GM_ROPE_EMBED);
    bind(OpCode::MAT4_MUL, GM_MAT4_MUL);
    bind(OpCode::QUAT_SLERP, GM_QUAT_SLERP);
    bind(OpCode::VEC3_CROSS, GM_VEC3_CROSS);
    bind(OpCode::VEC4_DOT, GM_VEC4_DOT);
    bind(OpCode::RAY_AABB_ISECT, GM_RAY_AABB_ISECT);
    bind(OpCode::GPU_LAUNCH, GM_GPU_LAUNCH);
    bind(OpCode::WASM_INVOKE, GM_WASM_INVOKE);

    return table;
}();

} // namespace detail

[[nodiscard]] constexpr machine::GenericMachineOpcode map_to_generic_opcode(
    ir::OpCode op,
    MachineForm form,
    machine::MachineSemanticKind /*sem*/) noexcept {
    using namespace machine;

    const auto idx = static_cast<std::size_t>(op);
    if (idx < detail::kMaxOpCodeCount) {
        const auto direct = detail::kOpCodeToGenericMachineTable[idx];
        if (direct != GM_NOOP || op == ir::OpCode::NOP) {
            return direct;
        }
    }

    // Fallback based on MachineForm
    switch (form) {
        case MachineForm::NoOp:             return GM_NOOP;
        case MachineForm::Move:             return GM_COPY;
        case MachineForm::LoadImmediate:    return GM_IMMEDIATE;
        case MachineForm::LoadStack:        return GM_LOAD_STACK;
        case MachineForm::StoreStack:       return GM_STORE_STACK;
        case MachineForm::IntegerBinary32:  return GM_IADD32;
        case MachineForm::IntegerBinary64:  return GM_IADD64;
        case MachineForm::FloatBinary32:    return GM_FADD32;
        case MachineForm::FloatBinary64:    return GM_FADD64;
        case MachineForm::FloatTernaryFma32:return GM_FFMA32;
        case MachineForm::Compare32:        return GM_ICMP32;
        case MachineForm::Goto:             return GM_GOTO;
        case MachineForm::BranchCond:       return GM_BR_COND;
        case MachineForm::BranchCompare:    return GM_BR_COMPARE;
        case MachineForm::Call:             return GM_CALL;
        case MachineForm::Return:           return GM_RETURN;
        case MachineForm::Syscall:          return GM_SYSCALL;
        case MachineForm::HardwareFence:
        case MachineForm::ConcurrentSync:   return GM_FENCE;
        case MachineForm::AtomicOp:         return GM_ATOMIC_LOAD;
        case MachineForm::TensorOp:         return GM_TENSOR_OP;
        case MachineForm::VectorBinary512:  return GM_VEC_LOAD;
        case MachineForm::VectorGather512:  return GM_VEC_GATHER;
        case MachineForm::VectorStore512:   return GM_VEC_STORE;
        default:                            return GM_NOOP;
    }
}

[[nodiscard]] inline machine::MachineSelection to_machine_selection(
    const SelectedInstruction& selected,
    machine::RegisterClassKind result_class) {
    machine::MachineSelection selection{};
    selection.form = to_machine_form(selected.form);
    selection.semantic_kind = selected.semantic_kind;
    selection.opcode = map_to_generic_opcode(selected.semantic_opcode, selected.form, selected.semantic_kind);
    selection.semantic_provenance =
        dgtl::ir::schema::stable_id(selected.semantic_opcode);
    selection.debug_id = 0;
    selection.result_value = selected.node.result_id;
    selection.result_class = (result_class != machine::RegisterClassKind::Invalid && result_class != machine::RegisterClassKind::General)
                                 ? result_class
                                 : selected.result_class;
    selection.intent_hash = selected.node.intent_hash;

    // Lossless transfer of IR schema traits, domains, and attributes
    if (const auto* def = dgtl::ir::gen::find_generated_op_def(selected.semantic_opcode)) {
        selection.traits = static_cast<machine::MachineTrait>(def->trait_bits);
        selection.domains = static_cast<machine::MachineDomain>(def->interface_bits);
        selection.attributes = static_cast<machine::MachineAttribute>(def->attribute_bits);
    }

    const auto default_info = machine::CommonTargetInstructionInfo::default_form_info(selection.form);
    selection.traits = selection.traits | default_info.default_traits;
    selection.domains = selection.domains | default_info.default_domains;
    selection.latency_cycles = default_info.default_latency;

    if ((selected.node.flags & dgtl::ir::Flags::IS_VOLATILE) != 0u) {
        selection.attributes = selection.attributes | machine::MachineAttribute::Volatile;
    }
    if ((selected.node.flags & dgtl::ir::Flags::IS_PREDICATED) != 0u) {
        selection.attributes = selection.attributes | machine::MachineAttribute::Predicated;
    }

    if (selected.memory_operand.has_value()) {
        selection.operands.push_back(
            machine::MachineSelectionOperand::memory_ref(
                *selected.memory_operand,
                selected.form == MachineForm::StoreStack ? machine::OperandRole::Def : machine::OperandRole::Use));
        if (selected.form == MachineForm::StoreStack) {
            const uint32_t val = (selected.node.op_b != ir::INVALID_SLOT) ? selected.node.op_b : selected.node.op_a;
            if (val != ir::INVALID_SLOT) {
                selection.operands.push_back(
                    machine::MachineSelectionOperand::value_ref(val, machine::OperandRole::Use));
            }
        }
    } else if (selected.form == MachineForm::Call) {
        selection.flags = selection.flags | machine::MachineInstrFlag::Call;
        size_t arg_idx = 0;
        auto get_arg_class = [&](size_t idx) -> machine::RegisterClassKind {
            return (idx < selected.call_arg_classes.size()) ? selected.call_arg_classes[idx] : machine::RegisterClassKind::General;
        };

        if (selected.node.op_a != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::symbol_ref(selected.node.op_a, machine::OperandRole::Use));
        }
        if (selected.node.op_b != ir::INVALID_SLOT) {
            auto op = machine::MachineSelectionOperand::value_ref(selected.node.op_b, machine::OperandRole::Use);
            op.reg_class = get_arg_class(arg_idx++);
            selection.operands.push_back(op);
        }
        if (selected.node.op_c != ir::INVALID_SLOT) {
            auto op = machine::MachineSelectionOperand::value_ref(selected.node.op_c, machine::OperandRole::Use);
            op.reg_class = get_arg_class(arg_idx++);
            selection.operands.push_back(op);
        }
        if (selected.node.logic_flags != ir::INVALID_SLOT && selected.node.logic_flags != 0u) {
            auto op = machine::MachineSelectionOperand::value_ref(selected.node.logic_flags, machine::OperandRole::Use);
            op.reg_class = get_arg_class(arg_idx++);
            selection.operands.push_back(op);
        }
        for (uint32_t ov : selected.overflow_operands) {
            if (ov != ir::INVALID_SLOT) {
                auto op = machine::MachineSelectionOperand::value_ref(ov, machine::OperandRole::Use);
                op.reg_class = get_arg_class(arg_idx++);
                selection.operands.push_back(op);
            }
        }
    } else if (selected.form == MachineForm::LoadImmediate ||
               selected.node.opcode == ir::OpCode::CONST ||
               selected.node.opcode == ir::OpCode::LOAD_IMM ||
               selected.node.opcode == ir::OpCode::IMMEDIATE) {
        int64_t imm_val = 0;
        if (selected.node.op_a == ir::INVALID_SLOT) {
            imm_val = static_cast<int64_t>(static_cast<int32_t>(selected.node.op_b));
        } else if (selected.node.op_b == ir::INVALID_SLOT) {
            imm_val = static_cast<int64_t>(static_cast<int32_t>(selected.node.op_a));
        } else {
            const uint64_t raw = (static_cast<uint64_t>(selected.node.op_b) << 32) | static_cast<uint64_t>(selected.node.op_a);
            imm_val = static_cast<int64_t>(raw);
        }
        selection.operands.push_back(machine::MachineSelectionOperand::imm(imm_val));
    } else if (selected.form == MachineForm::Goto) {
        if (selected.node.op_a != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::block_ref(selected.node.op_a));
        }
    } else if (selected.form == MachineForm::BranchCond) {
        if (selected.node.op_a != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::value_ref(selected.node.op_a, machine::OperandRole::Use));
        }
        if (selected.node.op_b != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::block_ref(selected.node.op_b));
        }
    } else if (selected.form == MachineForm::Compare32) {
        if (selected.node.op_a != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::value_ref(selected.node.op_a, machine::OperandRole::Use));
        }
        if (selected.node.op_b != ir::INVALID_SLOT) {
            selection.operands.push_back(
                machine::MachineSelectionOperand::value_ref(selected.node.op_b, machine::OperandRole::Use));
        }
        machine::ComparisonPredicate pred = machine::ComparisonPredicate::Equal;
        switch (selected.node.opcode) {
            case ir::OpCode::CMP_EQ_I32: pred = machine::ComparisonPredicate::Equal; break;
            case ir::OpCode::CMP_NE_I32: pred = machine::ComparisonPredicate::NotEqual; break;
            case ir::OpCode::CMP_LT_I32: pred = machine::ComparisonPredicate::SignedLessThan; break;
            case ir::OpCode::CMP_LE_I32: pred = machine::ComparisonPredicate::SignedLessEqual; break;
            case ir::OpCode::CMP_GT_I32: pred = machine::ComparisonPredicate::SignedGreaterThan; break;
            case ir::OpCode::CMP_GE_I32: pred = machine::ComparisonPredicate::SignedGreaterEqual; break;
            default: break;
        }
        selection.semantic_provenance = static_cast<std::uint32_t>(pred);
        machine::MachineSelectionOperand pred_op = machine::MachineSelectionOperand::imm(static_cast<std::int64_t>(pred));
        selection.operands.push_back(pred_op);
    } else {
        const std::uint32_t operands[3] = {
            selected.node.op_a, selected.node.op_b, selected.node.op_c
        };

        const bool is_fp_op = (selection.result_class == machine::RegisterClassKind::FloatingPoint);
        for (std::size_t i = 0; i < 3; ++i) {
            if (operands[i] != ir::INVALID_SLOT) {
                auto op = machine::MachineSelectionOperand::value_ref(
                    operands[i], machine::OperandRole::Use);
                if (is_fp_op) {
                    op.reg_class = machine::RegisterClassKind::FloatingPoint;
                }
                selection.operands.push_back(op);
            }
        }
    }

    if (selected.node.has_side_effects()) {
        selection.flags = selection.flags | machine::MachineInstrFlag::SideEffect;
    }

    switch (selection.form) {
        case MachineForm::Goto:
        case MachineForm::BranchCond:
        case MachineForm::BranchCompare:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Terminator |
                              machine::MachineInstrFlag::Branch;
            break;
        case MachineForm::Return:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Terminator |
                              machine::MachineInstrFlag::Return;
            break;
        case MachineForm::Call:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Call |
                              machine::MachineInstrFlag::SideEffect;
            break;
        case MachineForm::LoadStack:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Load |
                              machine::MachineInstrFlag::MayLoad;
            break;
        case MachineForm::StoreStack:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Store |
                              machine::MachineInstrFlag::MayStore;
            break;
        case MachineForm::AtomicOp:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Atomic |
                              machine::MachineInstrFlag::SideEffect;
            break;
        case MachineForm::HardwareFence:
        case MachineForm::ConcurrentSync:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::Barrier |
                              machine::MachineInstrFlag::SideEffect;
            break;
        case MachineForm::Syscall:
        case MachineForm::TensorOp:
            selection.flags = selection.flags |
                              machine::MachineInstrFlag::SideEffect;
            break;
        default:
            break;
    }

    return selection;
}

namespace detail {

[[nodiscard]] inline std::string_view trim_copy(std::string_view value) noexcept {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

[[nodiscard]] inline bool ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::vector<std::string> split_top_level(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
        } else if (ch == delimiter && depth == 0) {
            parts.push_back(std::string(trim_copy(text.substr(start, i - start))));
            start = i + 1;
        }
    }
    parts.push_back(std::string(trim_copy(text.substr(start))));
    return parts;
}

[[nodiscard]] inline std::optional<ir::OpCode> parse_opcode_name(
    std::string_view name) noexcept {
    return dgtl::ir::gen::find_opcode_by_name(detail::trim_copy(name));
}

struct PatternTerm {
    std::string capture;
    std::optional<ir::OpCode> opcode;
    std::vector<PatternTerm> children;
};

[[nodiscard]] inline PatternTerm parse_term(std::string_view text) {
    const std::string_view token = trim_copy(text);
    if (!token.empty() && token[0] == '$') {
        PatternTerm term{};
        term.capture = std::string(token.substr(1));
        return term;
    }

    const size_t open = token.find('(');
    if (open == std::string::npos) {
        PatternTerm term{};
        term.opcode = parse_opcode_name(token);
        return term;
    }

    const size_t close = token.rfind(')');
    PatternTerm term{};
    term.opcode = parse_opcode_name(token.substr(0, open));
    if (close != std::string::npos && close > open + 1) {
        for (const std::string& part : split_top_level(token.substr(open + 1, close - open - 1), ',')) {
            term.children.push_back(parse_term(part));
        }
    }
    return term;
}

struct TargetSelector {
    std::optional<core::TargetFamily> family;
    std::optional<core::ArchType> arch;
    std::optional<core::ObjectFormat> object_format;
};

struct SelectionRule {
    std::string name;
    PatternTerm match;
    PatternTerm replace;
    std::vector<TargetSelector> targets;
    uint32_t benefit = 1;
    bool require_fma = false;
};

[[nodiscard]] inline std::optional<TargetSelector> parse_target_name(std::string_view value) noexcept {
    const std::string_view key = trim_copy(value);

    // Exact ISA/subtarget selectors
    if (ascii_equal_case_insensitive(key, "x86_64_avx512")) {
        return TargetSelector{std::nullopt, core::ArchType::X86_64_AVX512, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "x86_64_avx2")) {
        return TargetSelector{std::nullopt, core::ArchType::X86_64_AVX2, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "aarch64_sve") ||
        ascii_equal_case_insensitive(key, "arm64_sve")) {
        return TargetSelector{std::nullopt, core::ArchType::AARCH64_SVE, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "arm32_neon")) {
        return TargetSelector{std::nullopt, core::ArchType::ARM32_NEON, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "riscv64_rvv")) {
        return TargetSelector{std::nullopt, core::ArchType::RISCV64_RVV, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "wasm_simd")) {
        return TargetSelector{std::nullopt, core::ArchType::WASM_SIMD, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "cuda_ptx")) {
        return TargetSelector{std::nullopt, core::ArchType::CUDA_PTX, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "rocm_amdgcn")) {
        return TargetSelector{std::nullopt, core::ArchType::ROCM_AMDGCN, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "fpga_verilog")) {
        return TargetSelector{std::nullopt, core::ArchType::FPGA_VERILOG, std::nullopt};
    }

    // Canonical target-family aliases
    if (ascii_equal_case_insensitive(key, "x64") ||
        ascii_equal_case_insensitive(key, "x86_64") ||
        ascii_equal_case_insensitive(key, "amd64") ||
        ascii_equal_case_insensitive(key, "x86-64")) {
        return TargetSelector{core::TargetFamily::X64, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "arm64") ||
        ascii_equal_case_insensitive(key, "aarch64") ||
        ascii_equal_case_insensitive(key, "armv8") ||
        ascii_equal_case_insensitive(key, "sve")) {
        return TargetSelector{core::TargetFamily::ARM64, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "arm32") ||
        ascii_equal_case_insensitive(key, "armv7a") ||
        ascii_equal_case_insensitive(key, "armv7")) {
        return TargetSelector{core::TargetFamily::ARM32, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "wasm") ||
        ascii_equal_case_insensitive(key, "wasm32") ||
        ascii_equal_case_insensitive(key, "wasm64")) {
        return TargetSelector{core::TargetFamily::WASM, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "riscv64") ||
        ascii_equal_case_insensitive(key, "rv64") ||
        ascii_equal_case_insensitive(key, "rv64gc")) {
        return TargetSelector{core::TargetFamily::RISCV64, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "cuda") ||
        ascii_equal_case_insensitive(key, "ptx") ||
        ascii_equal_case_insensitive(key, "nvptx")) {
        return TargetSelector{core::TargetFamily::CUDA, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "rocm") ||
        ascii_equal_case_insensitive(key, "amdgcn") ||
        ascii_equal_case_insensitive(key, "amd-gcn") ||
        ascii_equal_case_insensitive(key, "amdgcn-gfx") ||
        ascii_equal_case_insensitive(key, "hip")) {
        return TargetSelector{core::TargetFamily::ROCm, std::nullopt, std::nullopt};
    }
    if (ascii_equal_case_insensitive(key, "fpga") ||
        ascii_equal_case_insensitive(key, "verilog") ||
        ascii_equal_case_insensitive(key, "fpga_verilog")) {
        return TargetSelector{core::TargetFamily::FPGA, std::nullopt, std::nullopt};
    }

    if (ascii_equal_case_insensitive(key, "macho") ||
        ascii_equal_case_insensitive(key, "mach-o") ||
        ascii_equal_case_insensitive(key, "darwin")) {
        return TargetSelector{std::nullopt, std::nullopt, core::ObjectFormat::MachO};
    }

    return std::nullopt;
}

[[nodiscard]] inline std::string lower_ascii(std::string_view value) {
    std::string out(value.size(), '\0');
    std::transform(value.begin(), value.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

[[nodiscard]] inline SelectionRule parse_rule_line(std::string_view line) {
    SelectionRule rule{};
    std::string_view text = trim_copy(line);
    if (!text.starts_with("rule ")) {
        return rule;
    }

    const size_t colon = text.find(':');
    const size_t arrow = text.find("->");
    const size_t bracket = text.find('[');
    if (colon == std::string::npos || arrow == std::string::npos || arrow <= colon) {
        return rule;
    }

    rule.name = std::string(trim_copy(text.substr(5, colon - 5)));
    const size_t replace_end = (bracket == std::string::npos) ? text.size() : bracket;
    rule.match = parse_term(text.substr(colon + 1, arrow - colon - 1));
    rule.replace = parse_term(text.substr(arrow + 2, replace_end - arrow - 2));

    if (bracket != std::string::npos) {
        const size_t end_bracket = text.rfind(']');
        if (end_bracket == std::string_view::npos || end_bracket <= bracket) {
            return rule;
        }
        const std::string_view attrs = text.substr(bracket + 1, end_bracket - bracket - 1);
        for (const std::string& item : split_top_level(attrs, ',')) {
            const size_t eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = lower_ascii(trim_copy(item.substr(0, eq)));
            const std::string_view value = trim_copy(item.substr(eq + 1));
            if (key == "targets") {
                for (const std::string& target_name : split_top_level(value, '|')) {
                    const auto selector = parse_target_name(target_name);
                    if (selector.has_value()) {
                        rule.targets.push_back(*selector);
                    }
                }
            } else if (key == "benefit") {
                rule.benefit = static_cast<uint32_t>(std::max(1, std::stoi(std::string(value))));
            } else if (key == "require_fma") {
                rule.require_fma = ascii_equal_case_insensitive(value, "true") || value == "1" || ascii_equal_case_insensitive(value, "yes");
            }
        }
    }

    return rule;
}

struct MatchContext {
    const ir::IRStream& stream;
    std::unordered_map<uint32_t, ir::IRNode> producer_nodes;

    explicit MatchContext(const ir::IRStream& ir_stream) : stream(ir_stream) {
        producer_nodes.reserve(ir_stream.size() * 2u + 1u);

        for (size_t i = 0; i < ir_stream.size(); ++i) {
            const ir::IRNode node = ir_stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT) {
                producer_nodes.emplace(node.result_id, node);
            }
        }
    }

    [[nodiscard]] const ir::IRNode* lookup_producer(uint32_t slot) const noexcept {
        const auto it = producer_nodes.find(slot);
        if (it == producer_nodes.end()) return nullptr;
        return &it->second;
    }
};

[[nodiscard]] inline bool target_selector_matches(const TargetSelector& selector,
                                                  const core::TargetMachine& target) noexcept {
    if (selector.arch.has_value() && target.get_arch() != *selector.arch) {
        return false;
    }
    if (selector.family.has_value() && target.family() != *selector.family) {
        return false;
    }
    if (selector.object_format.has_value() && target.object_format() != *selector.object_format) {
        return false;
    }
    return true;
}

[[nodiscard]] inline bool arch_matches(const SelectionRule& rule,
                                       const core::TargetMachine& target) noexcept {
    if (rule.targets.empty()) {
        return true;
    }
    return std::any_of(rule.targets.begin(), rule.targets.end(),
                       [&target](const TargetSelector& selector) {
                           return target_selector_matches(selector, target);
                       });
}

[[nodiscard]] inline bool supports_rule_requirements(const SelectionRule& rule,
                                                      const core::TargetMachine& target) noexcept {
    if (rule.require_fma && !target.has_feature(core::TargetFeature::FMA)) {
        return false;
    }
    return true;
}

[[nodiscard]] inline uint32_t operand_at(const ir::IRNode& node, size_t index) noexcept {
    switch (index) {
        case 0: return node.op_a;
        case 1: return node.op_b;
        case 2: return node.op_c;
        default: return ir::INVALID_SLOT;
    }
}

[[nodiscard]] inline bool match_term(const PatternTerm& term,
                                     const ir::IRNode& node,
                                     const MatchContext& ctx,
                                     std::unordered_map<std::string, uint32_t>& captures) {
    if (!term.capture.empty()) {
        captures[term.capture] = node.result_id;
        return true;
    }

    if (!term.opcode.has_value() || node.opcode != *term.opcode) {
        return false;
    }

    for (size_t i = 0; i < term.children.size(); ++i) {
        const PatternTerm& child = term.children[i];
        const uint32_t operand = operand_at(node, i);
        if (!child.capture.empty()) {
            captures[child.capture] = operand;
            continue;
        }
        const ir::IRNode* producer = ctx.lookup_producer(operand);
        if (producer == nullptr || !match_term(child, *producer, ctx, captures)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline uint32_t lookup_capture(const std::unordered_map<std::string, uint32_t>& captures,
                                             const std::string& name) noexcept {
    const auto it = captures.find(name);
    return it == captures.end() ? ir::INVALID_SLOT : it->second;
}

[[nodiscard]] inline ir::IRNode build_replacement(const SelectionRule& rule,
                                                   const ir::IRNode& original,
                                                   const std::unordered_map<std::string, uint32_t>& captures) {
    ir::IRNode node = original;
    if (rule.replace.opcode.has_value()) {
        node.opcode = *rule.replace.opcode;
    }

    node.op_a = ir::INVALID_SLOT;
    node.op_b = ir::INVALID_SLOT;
    node.op_c = ir::INVALID_SLOT;
    for (size_t i = 0; i < rule.replace.children.size() && i < 3; ++i) {
        const PatternTerm& child = rule.replace.children[i];
        const uint32_t value = child.capture.empty() ? ir::INVALID_SLOT : lookup_capture(captures, child.capture);
        switch (i) {
            case 0: node.op_a = value; break;
            case 1: node.op_b = value; break;
            case 2: node.op_c = value; break;
            default: break;
        }
    }
    node.refresh_intent_hash();
    return node;
}

[[nodiscard]] inline std::optional<int64_t> extract_constant_immediate(const ir::IRNode& node) noexcept {
    if (node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE || node.opcode == ir::OpCode::CONST) {
        if (node.op_a == ir::INVALID_SLOT) {
            return static_cast<int64_t>(static_cast<int32_t>(node.op_b));
        }
        if (node.op_b == ir::INVALID_SLOT) {
            return static_cast<int64_t>(static_cast<int32_t>(node.op_a));
        }
        const uint64_t raw = (static_cast<uint64_t>(node.op_b) << 32) | static_cast<uint64_t>(node.op_a);
        return static_cast<int64_t>(raw);
    }
    return std::nullopt;
}

[[nodiscard]] inline bool is_scale_valid_for_target(int32_t scale, const core::TargetMachine& target) noexcept {
    if (scale == 1) return true;
    if (target.family() == core::TargetFamily::RISCV64) {
        return false;
    }
    if (target.family() == core::TargetFamily::CUDA ||
        target.family() == core::TargetFamily::ROCm ||
        target.family() == core::TargetFamily::FPGA) {
        return false;
    }
    return (scale == 2 || scale == 4 || scale == 8);
}

} // namespace detail

struct SIBAddressingMode {
    uint32_t base_value = ir::INVALID_SLOT;
    uint32_t index_value = ir::INVALID_SLOT;
    int32_t scale = 1;
    int64_t displacement = 0;
    uint32_t address_space = 0;
    bool matched = false;

    [[nodiscard]] machine::MachineMemoryOperand to_memory_operand(
        uint32_t size_bytes = 4,
        uint32_t alignment = 4,
        bool is_load = false,
        bool is_store = false,
        uint32_t addr_space = 0) const noexcept {
        machine::MachineMemoryOperand mem{};
        if (base_value != ir::INVALID_SLOT) {
            mem.base = machine::MachineRegister::virtual_reg(base_value, machine::RegisterClassKind::General);
        }
        if (index_value != ir::INVALID_SLOT) {
            mem.index = machine::MachineRegister::virtual_reg(index_value, machine::RegisterClassKind::General);
        }
        mem.scale = scale;
        mem.displacement = displacement;
        mem.address_space = (addr_space != 0) ? addr_space : address_space;
        mem.size_bytes = size_bytes;
        mem.alignment = alignment;
        mem.load = is_load;
        mem.store = is_store;
        return mem;
    }
};

namespace detail {

[[nodiscard]] inline SIBAddressingMode match_addressing_mode_recursive(
    const MatchContext& ctx,
    uint32_t val,
    const core::TargetMachine& target,
    uint32_t depth = 0) noexcept {

    SIBAddressingMode mode{};
    if (val == ir::INVALID_SLOT || depth > 5) {
        mode.base_value = val;
        mode.matched = (val != ir::INVALID_SLOT);
        return mode;
    }

    const ir::IRNode* prod = ctx.lookup_producer(val);
    if (!prod) {
        mode.base_value = val;
        mode.matched = true;
        return mode;
    }

    const uint32_t node_addr_space = (prod->logic_flags >> 24) & 0xFu;
    if (node_addr_space != 0) {
        mode.address_space = node_addr_space;
    }

    if (auto imm = extract_constant_immediate(*prod); imm.has_value()) {
        mode.displacement = *imm;
        mode.matched = true;
        return mode;
    }

    if (prod->opcode == ir::OpCode::ADD_I64 || prod->opcode == ir::OpCode::ADD_I32) {
        const uint32_t lhs = prod->op_a;
        const uint32_t rhs = prod->op_b;

        const ir::IRNode* lhs_node = ctx.lookup_producer(lhs);
        const ir::IRNode* rhs_node = ctx.lookup_producer(rhs);

        auto lhs_imm = lhs_node ? extract_constant_immediate(*lhs_node) : std::nullopt;
        auto rhs_imm = rhs_node ? extract_constant_immediate(*rhs_node) : std::nullopt;

        if (rhs_imm.has_value()) {
            SIBAddressingMode base_mode = match_addressing_mode_recursive(ctx, lhs, target, depth + 1);
            base_mode.displacement += *rhs_imm;
            base_mode.matched = true;
            return base_mode;
        }

        if (lhs_imm.has_value()) {
            SIBAddressingMode base_mode = match_addressing_mode_recursive(ctx, rhs, target, depth + 1);
            base_mode.displacement += *lhs_imm;
            base_mode.matched = true;
            return base_mode;
        }

        auto try_scaled = [&](uint32_t scale_cand, uint32_t other) -> std::optional<SIBAddressingMode> {
            const ir::IRNode* scale_node = ctx.lookup_producer(scale_cand);
            if (!scale_node) return std::nullopt;

            if (scale_node->opcode == ir::OpCode::SHL_I64 || scale_node->opcode == ir::OpCode::SHL_I32) {
                const ir::IRNode* shift_node = ctx.lookup_producer(scale_node->op_b);
                auto shift_imm = shift_node ? extract_constant_immediate(*shift_node) : std::nullopt;
                if (!shift_imm && scale_node->op_b <= 3 && scale_node->op_b >= 1) {
                    shift_imm = static_cast<int64_t>(scale_node->op_b);
                }
                if (shift_imm && *shift_imm >= 1 && *shift_imm <= 3) {
                    const int32_t sc = 1 << static_cast<int32_t>(*shift_imm);
                    if (is_scale_valid_for_target(sc, target)) {
                        SIBAddressingMode m = match_addressing_mode_recursive(ctx, other, target, depth + 1);
                        if (m.index_value == ir::INVALID_SLOT) {
                            m.index_value = scale_node->op_a;
                            m.scale = sc;
                            m.matched = true;
                            return m;
                        }
                    }
                }
            }

            if (scale_node->opcode == ir::OpCode::MUL_I64 || scale_node->opcode == ir::OpCode::MUL_I32) {
                const ir::IRNode* factor_node = ctx.lookup_producer(scale_node->op_b);
                auto factor_imm = factor_node ? extract_constant_immediate(*factor_node) : std::nullopt;
                uint32_t idx_val = scale_node->op_a;
                if (!factor_imm) {
                    const ir::IRNode* lhs_f = ctx.lookup_producer(scale_node->op_a);
                    factor_imm = lhs_f ? extract_constant_immediate(*lhs_f) : std::nullopt;
                    idx_val = scale_node->op_b;
                }
                if (factor_imm && (*factor_imm == 1 || *factor_imm == 2 || *factor_imm == 4 || *factor_imm == 8)) {
                    const int32_t sc = static_cast<int32_t>(*factor_imm);
                    if (is_scale_valid_for_target(sc, target)) {
                        SIBAddressingMode m = match_addressing_mode_recursive(ctx, other, target, depth + 1);
                        if (m.index_value == ir::INVALID_SLOT) {
                            m.index_value = idx_val;
                            m.scale = sc;
                            m.matched = true;
                            return m;
                        }
                    }
                }
            }

            return std::nullopt;
        };

        if (auto m = try_scaled(rhs, lhs); m.has_value()) return *m;
        if (auto m = try_scaled(lhs, rhs); m.has_value()) return *m;

        SIBAddressingMode m = match_addressing_mode_recursive(ctx, lhs, target, depth + 1);
        if (m.index_value == ir::INVALID_SLOT && is_scale_valid_for_target(1, target)) {
            m.index_value = rhs;
            m.scale = 1;
            m.matched = true;
            return m;
        }
    }

    if (prod->opcode == ir::OpCode::GEP) {
        const uint32_t base_ptr = prod->op_a;
        const uint32_t index_or_off = prod->op_b;
        const uint32_t elem_stride = (prod->op_c != ir::INVALID_SLOT && prod->op_c > 0) ? prod->op_c : 1u;

        const ir::IRNode* off_node = ctx.lookup_producer(index_or_off);
        auto off_imm = off_node ? extract_constant_immediate(*off_node) : std::nullopt;

        if (off_imm.has_value()) {
            SIBAddressingMode base_mode = match_addressing_mode_recursive(ctx, base_ptr, target, depth + 1);
            base_mode.displacement += (*off_imm) * static_cast<int64_t>(elem_stride);
            base_mode.matched = true;
            return base_mode;
        }

        if (elem_stride == 1 || elem_stride == 2 || elem_stride == 4 || elem_stride == 8) {
            if (is_scale_valid_for_target(static_cast<int32_t>(elem_stride), target)) {
                SIBAddressingMode base_mode = match_addressing_mode_recursive(ctx, base_ptr, target, depth + 1);
                if (base_mode.index_value == ir::INVALID_SLOT) {
                    base_mode.index_value = index_or_off;
                    base_mode.scale = static_cast<int32_t>(elem_stride);
                    base_mode.matched = true;
                    return base_mode;
                }
            }
        }
    }

    if (prod->opcode == ir::OpCode::LEA) {
        return match_addressing_mode_recursive(ctx, prod->op_a, target, depth + 1);
    }

    mode.base_value = val;
    mode.matched = true;
    return mode;
}

} // namespace detail

[[nodiscard]] inline SIBAddressingMode match_addressing_mode(
    const ir::IRStream& stream,
    const detail::MatchContext& ctx,
    uint32_t address_val,
    const core::TargetMachine& target) noexcept {
    (void)stream;
    return detail::match_addressing_mode_recursive(ctx, address_val, target, 0);
}

[[nodiscard]] inline SIBAddressingMode match_addressing_mode(
    const ir::IRStream& stream,
    uint32_t address_val,
    const core::TargetMachine& target) {
    detail::MatchContext ctx(stream);
    return match_addressing_mode(stream, ctx, address_val, target);
}

[[nodiscard]] inline MachineForm classify_machine_form_with_fallback(
    const core::TargetMachine& target,
    const ir::IRNode& node) noexcept {
    const MachineForm form = dgtl::mir::classify_machine_node(target, node);
    if (form != MachineForm::Unhandled) [[likely]] {
        return form;
    }

    if (runtime_policy::should_route_unhandled_to_runtime_call(target, node.opcode)) {
        return MachineForm::Call;
    }

    return MachineForm::Unhandled;
}

[[nodiscard]] inline MachineForm classify_machine_form_with_fallback(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept {
    const MachineForm form = dgtl::mir::classify_machine_form(target, opcode);
    if (form != MachineForm::Unhandled) [[likely]] {
        return form;
    }

    if (runtime_policy::should_route_unhandled_to_runtime_call(target, opcode)) {
        return MachineForm::Call;
    }

    return MachineForm::Unhandled;
}

class InstructionSelectorProgram {
public:
    InstructionSelectorProgram() = default;

    [[nodiscard]] static InstructionSelectorProgram from_dsl(std::string_view dsl_text) {
        InstructionSelectorProgram program;
        size_t start = 0;
        while (start < dsl_text.size()) {
            size_t end = dsl_text.find('\n', start);
            if (end == std::string_view::npos) {
                end = dsl_text.size();
            }
            const std::string_view line = detail::trim_copy(dsl_text.substr(start, end - start));
            start = end + 1;
            if (line.empty() || line[0] == '#') {
                continue;
            }
            detail::SelectionRule rule = detail::parse_rule_line(line);
            if (!rule.name.empty() && rule.match.opcode.has_value() && rule.replace.opcode.has_value()) {
                program.rules_.push_back(std::move(rule));
            }
        }
        return program;
    }

    [[nodiscard]] static InstructionSelectorProgram from_rule_lines(std::span<const std::string_view> lines) {
        InstructionSelectorProgram program;
        for (const std::string_view line_view : lines) {
            const std::string_view line = detail::trim_copy(line_view);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            detail::SelectionRule rule = detail::parse_rule_line(line);
            if (!rule.name.empty() && rule.match.opcode.has_value() && rule.replace.opcode.has_value()) {
                program.rules_.push_back(std::move(rule));
            }
        }
        return program;
    }

    [[nodiscard]] ISelReplacement select_node(const ir::IRStream& stream,
                                              size_t index,
                                              const ir::IRNode& node,
                                              const core::TargetMachine& target) const {
        (void)index;
        detail::MatchContext ctx(stream);
        ISelReplacement best{};
        for (const detail::SelectionRule& rule : rules_) {
            if (!detail::arch_matches(rule, target)) {
                continue;
            }
            if (!detail::supports_rule_requirements(rule, target)) {
                continue;
            }
            if (!rule.match.opcode.has_value() || node.opcode != *rule.match.opcode) {
                continue;
            }

            std::unordered_map<std::string, uint32_t> captures;
            if (!detail::match_term(rule.match, node, ctx, captures)) {
                continue;
            }

            if (!best.matched || rule.benefit > best.benefit) {
                best.matched = true;
                best.rule_name = rule.name;
                best.benefit = rule.benefit;
                best.node = detail::build_replacement(rule, node, captures);
            }
        }
        return best;
    }

    [[nodiscard]] SelectedInstruction select_instruction(const ir::IRStream& stream,
                                                         size_t index,
                                                         const ir::IRNode& node,
                                                         const core::TargetMachine& target) const {
        const ISelReplacement replacement = select_node(stream, index, node, target);
        SelectedInstruction selected{};
        selected.node = replacement.matched ? replacement.node : node;
        selected.semantic_opcode = selected.node.opcode;
        selected.rule_name = replacement.rule_name;
        selected.matched_rule = replacement.matched;
        selected.form = classify_machine_form_with_fallback(target, selected.node);
        selected.semantic_kind = dgtl::mir::semantic_kind(target, selected.semantic_opcode);

        if (selected.node.result_id != ir::INVALID_SLOT) {
            const auto tc = stream.get_result_type_code(selected.node.result_id);
            if (ir::is_floating_type_code(tc)) {
                selected.result_class = machine::RegisterClassKind::FloatingPoint;
            }
        }
        if (selected.form == MachineForm::FloatBinary32 ||
            selected.form == MachineForm::FloatBinary64 ||
            selected.form == MachineForm::FloatTernaryFma32) {
            selected.result_class = machine::RegisterClassKind::FloatingPoint;
        }

        if (selected.form == MachineForm::Call) {
            const auto overflow = stream.get_call_overflow_args(index);
            if (!overflow.empty()) {
                selected.overflow_operands.assign(overflow.begin(), overflow.end());
            }

            auto classify_arg = [&](uint32_t arg_val) -> machine::RegisterClassKind {
                if (arg_val != ir::INVALID_SLOT) {
                    const auto tc = stream.get_result_type_code(arg_val);
                    if (ir::is_floating_type_code(tc)) {
                        return machine::RegisterClassKind::FloatingPoint;
                    }
                }
                return machine::RegisterClassKind::General;
            };

            if (selected.node.op_b != ir::INVALID_SLOT) {
                selected.call_arg_classes.push_back(classify_arg(selected.node.op_b));
            }
            if (selected.node.op_c != ir::INVALID_SLOT) {
                selected.call_arg_classes.push_back(classify_arg(selected.node.op_c));
            }
            if (selected.node.logic_flags != ir::INVALID_SLOT && selected.node.logic_flags != 0u) {
                selected.call_arg_classes.push_back(classify_arg(selected.node.logic_flags));
            }
            for (uint32_t ov : selected.overflow_operands) {
                if (ov != ir::INVALID_SLOT) {
                    selected.call_arg_classes.push_back(classify_arg(ov));
                }
            }
        }

        if (selected.form == MachineForm::LoadStack || selected.form == MachineForm::StoreStack ||
            selected.node.opcode == ir::OpCode::LOAD || selected.node.opcode == ir::OpCode::STORE ||
            selected.node.opcode == ir::OpCode::LEA) {
            detail::MatchContext ctx(stream);
            const uint32_t addr_val = selected.node.op_a;
            if (addr_val != ir::INVALID_SLOT) {
                const SIBAddressingMode sib = match_addressing_mode(stream, ctx, addr_val, target);
                if (sib.matched && (sib.index_value != ir::INVALID_SLOT || sib.displacement != 0)) {
                    const bool is_store = (selected.form == MachineForm::StoreStack || selected.node.opcode == ir::OpCode::STORE);
                    selected.memory_operand = sib.to_memory_operand(4, 4, !is_store, is_store);
                }
            }
        }
        return selected;
    }

    [[nodiscard]] machine::MachineInstruction& append_to_machine_function(
        machine::MachineFunctionBuilder& builder,
        machine::BlockId block,
        const SelectedInstruction& selected,
        machine::RegisterClassKind result_class = machine::RegisterClassKind::Invalid) const {
        if (block == machine::INVALID_BLOCK_ID ||
            selected.form == MachineForm::Unhandled ||
            selected.semantic_kind == machine::MachineSemanticKind::None ||
            !machine::semantic_kind_matches_form(to_machine_form(selected.form), selected.semantic_kind) ||
            !dgtl::mir::verify_node_form_compatibility(selected.node, selected.form)) {
            throw std::invalid_argument(
                "DGTL ISel contract violation: invalid HIR-to-MIR semantic/form pair or block ID");
        }
        const auto effective_class = (result_class != machine::RegisterClassKind::Invalid && result_class != machine::RegisterClassKind::General)
                                         ? result_class
                                         : selected.result_class;
        return builder.append(block, to_machine_selection(selected, effective_class), effective_class);
    }

    [[nodiscard]] bool empty() const noexcept {
        return rules_.empty();
    }

private:
    std::vector<detail::SelectionRule> rules_;
};

[[nodiscard]] inline InstructionSelectorProgram make_builtin_instruction_selector_program() {
    return InstructionSelectorProgram::from_rule_lines({});
}

} // namespace dgtl::mir::isel