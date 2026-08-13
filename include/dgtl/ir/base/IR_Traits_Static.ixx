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

module;

#include <cstdlib>

// Global Module Fragment: Include standard sizing layers
#include <cstdint>

export module dgtl.ir_traits;

// Modern Named Module Ingestion
import dgtl.ir_enums;

export namespace dgtl::ir {

/**
 * @class IRTraits
 * @brief Static utility traits for rapid semantic analysis of OpCodes and Flags.
 * Guaranteed zero-overhead as all functions are constexpr/inline.
 */
struct IRTraits {

    [[nodiscard]] static constexpr bool is_branch(OpCode op) noexcept {
        return op == OpCode::BRANCH ||
               op == OpCode::BRANCH_COND ||
               op == OpCode::BRANCH_COMPARE_FUSED ||
               op == OpCode::BRANCH_LIKELY ||
               op == OpCode::BRANCH_UNLIKELY ||
               op == OpCode::SWITCH_DISPATCH ||
               op == OpCode::GOTO;
    }

    [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
        return is_branch(op) ||
               op == OpCode::RET || op == OpCode::RETURN ||
               op == OpCode::RESUME_CTX;
    }

    [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
        return op == OpCode::LOAD ||
               op == OpCode::ATOMIC_LOAD ||
               op == OpCode::STORE ||
               op == OpCode::ATOMIC_STORE ||
               op == OpCode::ATOMIC_FENCE ||
               op == OpCode::GATHER_F32_512 ||
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::LEA ||
               op == OpCode::VEC_LOAD ||
               op == OpCode::VEC_STORE ||
               op == OpCode::PREFETCH;
    }

    [[nodiscard]] static constexpr bool has_side_effects(OpCode op, std::uint16_t flags) noexcept {
        if (flags & Flags::HAS_SIDE_EFFECTS) return true;
        if (flags & Flags::IS_VOLATILE)       return true;
        switch (op) {
            case OpCode::STORE:
            case OpCode::VOLATILE_STORE:
            case OpCode::ATOMIC_STORE:
            case OpCode::ATOMIC_FENCE:
            case OpCode::ACQUIRE_FENCE:
            case OpCode::RELEASE_FENCE:
            case OpCode::MEMORY_FENCE:
            case OpCode::STORE_V512:
            case OpCode::CALL:
            case OpCode::CALL_VARIADIC:
            case OpCode::SYSCALL:
            case OpCode::RET:
            case OpCode::BRANCH:
            case OpCode::BRANCH_COND:
            case OpCode::BRANCH_LIKELY:
            case OpCode::BRANCH_UNLIKELY:
            case OpCode::SWITCH_DISPATCH:
            case OpCode::GOTO:
            case OpCode::ASYNC_SPAWN:
            case OpCode::ASYNC_AWAIT:
            case OpCode::THREAD_SPAWN:
            case OpCode::BARRIER_SYNC:
            case OpCode::LOCK_ACQUIRE:
            case OpCode::LOCK_RELEASE:
            case OpCode::CHAN_SEND:
            case OpCode::CHAN_RECV:
            case OpCode::GUARD_ALIAS:
            case OpCode::GUARD_VALUE:
            case OpCode::RESUME_CTX:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_simd(OpCode op) noexcept {
        return op == OpCode::GATHER_F32_512 ||
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::VEC_ADD_F32_512 ||
               op == OpCode::VEC_MUL_F32_512 ||
               op == OpCode::VEC_LOAD ||
               op == OpCode::VEC_STORE ||
               op == OpCode::VEC_SHUFFLE ||
               op == OpCode::VEC_REDUCE_ADD ||
               op == OpCode::FCVTL_F16_TO_F32 ||
               op == OpCode::FCVTN_F32_TO_F16 ||
               op == OpCode::SIMD_BSL_PREDICATE;
    }

    [[nodiscard]] static constexpr bool is_integer_op(OpCode op) noexcept {
        return (op >= OpCode::ADD_I32 && op <= OpCode::SHR_I32) ||
               (op >= OpCode::ADD_I64 && op <= OpCode::DIV_I64) ||
               op == OpCode::BIT_FIELD_INSERT ||
               op == OpCode::BIT_FIELD_EXTRACT;
    }

    [[nodiscard]] static constexpr bool is_float_op(OpCode op) noexcept {
        return (op >= OpCode::ADD_F32 && op <= OpCode::FMA_WIDEN_F16_F32) ||
               (op >= OpCode::ADD_F64 && op <= OpCode::DIV_F64);
    }

    [[nodiscard]] static constexpr bool is_compare_op(OpCode op) noexcept {
        return (op >= OpCode::CMP_EQ_I32 && op <= OpCode::CMP_GE_I32) ||
               op == OpCode::STRING_COMPARE;
    }

    [[nodiscard]] static constexpr bool is_string_op(OpCode op) noexcept {
        switch (op) {
            case OpCode::STRING_CONST:
            case OpCode::STRING_LENGTH:
            case OpCode::STRING_COMPARE:
            case OpCode::STRING_CONCAT:
            case OpCode::STRING_SUBSTRING:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_control_flow(OpCode op) noexcept {
         return is_branch(op) ||
               op == OpCode::RET || op == OpCode::RETURN || op == OpCode::RESUME_CTX ||
               op == OpCode::CALL || op == OpCode::CALL_VARIADIC ||
               op == OpCode::SYSCALL || op == OpCode::LABEL;
    }

    [[nodiscard]] static constexpr bool is_valid_slot(std::uint32_t slot_id) noexcept {
        return slot_id != INVALID_SLOT && slot_id < 0xFFFFFF00u;
    }

    [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
        if (is_branch(op)) return 5.0;
        if (op == OpCode::SYSCALL) return 100.0;
        if (is_simd(op)) return 3.0;
        return 1.0;
    }

    [[nodiscard]] static constexpr bool supports_on_x64_avx512(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;
        if (op == OpCode::STORE_V512) return true;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return true;
        return !is_compare_op(op) || op <= OpCode::CMP_GE_I32;
    }

    [[nodiscard]] static constexpr bool supports_on_arm64_sve(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512) return true;
        if (op == OpCode::GATHER_16) return true;
        if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::VEC_MUL_F32_512) return true;
        if (op == OpCode::FMA_F32) return true;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_arm32_neon(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;
        if (op == OpCode::STORE_V512) return false;
        if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::VEC_MUL_F32_512) return false;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return true;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_riscv64_rv64gc(OpCode op) noexcept {
        if (is_simd(op) && op != OpCode::FCVTL_F16_TO_F32 && op != OpCode::FCVTN_F32_TO_F16) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_wasm(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;
        if (op == OpCode::STORE_V512) return false;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return false;
        if (is_simd(op)) return false;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_cuda_ptx(OpCode op) noexcept {
        if (is_control_flow(op) && op != OpCode::LABEL) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_rocm_llvmir(OpCode op) noexcept {
        if (is_control_flow(op) && op != OpCode::LABEL) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_nasm_x64() noexcept { return true; }
    [[nodiscard]] static constexpr bool supports_on_fpga_verilog() noexcept { return true; }

    [[nodiscard]] static constexpr bool is__friendly(OpCode op) noexcept {
        return !(is_simd(op) && op != OpCode::FCVTL_F16_TO_F32 && op != OpCode::FCVTN_F32_TO_F16);
    }

    [[nodiscard]] static constexpr bool is__intensive(OpCode op) noexcept {
        return is_simd(op) || op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16;
    }

    [[nodiscard]] static constexpr bool is_commutative(OpCode op) noexcept {
        switch (op) {
            case OpCode::ADD_I32: case OpCode::ADD_I64:
            case OpCode::ADD_F32: case OpCode::ADD_F64:
            case OpCode::MUL_I32: case OpCode::MUL_I64:
            case OpCode::MUL_F32: case OpCode::MUL_F64:
            case OpCode::AND_I32: case OpCode::OR_I32: case OpCode::XOR_I32:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_associative(OpCode op) noexcept {
        return is_commutative(op) && op != OpCode::ADD_F32 && op != OpCode::ADD_F64 &&
               op != OpCode::MUL_F32 && op != OpCode::MUL_F64;
    }

    [[nodiscard]] static constexpr bool is_single_def_use(OpCode op) noexcept {
        switch (op) {
            case OpCode::LOAD_IMM: case OpCode::IMMEDIATE:
            case OpCode::LEA:      case OpCode::LOAD:
            case OpCode::ATOMIC_LOAD:
            case OpCode::PTR_CAST:
            case OpCode::PTR_DIFF:
            case OpCode::STRING_CONST:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool can_fold_constants(OpCode op) noexcept {
        switch (op) {
            case OpCode::ADD_I32: case OpCode::SUB_I32: case OpCode::MUL_I32: case OpCode::DIV_I32:
            case OpCode::AND_I32: case OpCode::OR_I32:  case OpCode::XOR_I32: case OpCode::SHL_I32:
            case OpCode::SHR_I32: case OpCode::MOD_I32:
            case OpCode::ADD_I64: case OpCode::SUB_I64: case OpCode::MUL_I64: case OpCode::DIV_I64:
            case OpCode::BIT_FIELD_INSERT: case OpCode::BIT_FIELD_EXTRACT:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_memory_barrier(OpCode op) noexcept {
        switch (op) {
            case OpCode::CALL: case OpCode::SYSCALL:
            case OpCode::LOAD: case OpCode::ATOMIC_LOAD:
            case OpCode::STORE: case OpCode::ATOMIC_STORE:
            case OpCode::ATOMIC_FENCE:
            case OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool can_be_reordered(OpCode op1, OpCode op2) noexcept {
        if (is_memory_barrier(op1) || is_memory_barrier(op2)) return false;
        if (has_side_effects(op1, Flags::NONE) || has_side_effects(op2, Flags::NONE)) return false;
        return true;
    }

    [[nodiscard]] static constexpr double estimate_latency_cycles(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 6.0;
        if (op == OpCode::DIV_I32 || op == OpCode::DIV_I64 || op == OpCode::DIV_F32 || op == OpCode::DIV_F64) return 15.0;
        if (op == OpCode::MUL_I32 || op == OpCode::MUL_I64) return 3.0;
        if (op == OpCode::FMA_F32) return 4.0;
        if (op == OpCode::MUL_F32 || op == OpCode::MUL_F64) return 4.0;
        if (op == OpCode::LOAD || op == OpCode::ATOMIC_LOAD ||
            op == OpCode::STORE || op == OpCode::ATOMIC_STORE) return 2.5;
        if (is_branch(op)) return 0.5;
        return 1.0;
    }

    [[nodiscard]] static constexpr std::uint32_t estimate_throughput_slots(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 1u;
        if (op == OpCode::STORE_V512) return 1u;
        if (op == OpCode::DIV_I32 || op == OpCode::DIV_I64) return 1u;
        if (op == OpCode::MUL_I32 || op == OpCode::MUL_I64 || op == OpCode::FMA_F32) return 1u;
        if (is_branch(op)) return 0u;
        return 1u;
    }

    [[nodiscard]] static constexpr bool is_ownership_op(OpCode op) noexcept {
        switch (op) {
            case OpCode::OWNERSHIP_ACQUIRE: case OpCode::OWNERSHIP_RELEASE:
            case OpCode::OWNERSHIP_TRANSFER: case OpCode::ATOMIC_SNAPSHOT:
                return true;
            default: return false;
        }
    }

    [[nodiscard]] static constexpr bool is_concurrency_op(OpCode op) noexcept {
        switch (op) {
            case OpCode::ASYNC_SPAWN:  case OpCode::ASYNC_AWAIT:
            case OpCode::FIBER_YIELD:  case OpCode::BARRIER_SYNC:
            case OpCode::CHAN_SEND:    case OpCode::CHAN_RECV:
            case OpCode::LOCK_ACQUIRE: case OpCode::LOCK_RELEASE:
            case OpCode::MEMORY_FENCE: case OpCode::THREAD_SPAWN:
                return true;
            default: return false;
        }
    }

    [[nodiscard]] static constexpr bool is_dsa_op(OpCode op) noexcept {
        switch (op) {
            case OpCode::GRAPH_BUILD:  case OpCode::GRAPH_TRAVERSE:
            case OpCode::TREE_BUILD:   case OpCode::TREE_TRAVERSE:
            case OpCode::TRIE_BUILD:   case OpCode::TRIE_LOOKUP:   case OpCode::TRIE_INSERT:
            case OpCode::DSA_ALLOCATE: case OpCode::DSA_DEALLOCATE: case OpCode::SCOP_ANALYZE:
                return true;
            default: return false;
        }
    }

    [[nodiscard]] static constexpr bool is_tensor_op(OpCode op) noexcept {
        switch (op) {
            case OpCode::TENSOR_RESHAPE: case OpCode::TENSOR_REDUCE:
            case OpCode::TENSOR_TRANSPOSE: case OpCode::TENSOR_SLICE:
            case OpCode::TENSOR_CONCAT: case OpCode::TENSOR_BROADCAST:
            case OpCode::TENSOR_GEMM: case OpCode::TENSOR_CONV:
            case OpCode::TENSOR_POOL: case OpCode::TENSOR_SOFTMAX:
                return true;
            default: return false;
        }
    }

    [[nodiscard]] static constexpr bool must_synchronize(OpCode op) noexcept {
        switch (op) {
            case OpCode::ASYNC_SPAWN: case OpCode::THREAD_SPAWN:
            case OpCode::BARRIER_SYNC: case OpCode::CHAN_SEND:
            case OpCode::CHAN_RECV: case OpCode::MEMORY_FENCE:
                return true;
            default: return false;
        }
    }

    [[nodiscard]] static constexpr bool may_create_deadlock(OpCode op) noexcept {
        return op == OpCode::LOCK_ACQUIRE || op == OpCode::BARRIER_SYNC ||
               op == OpCode::CHAN_SEND   || op == OpCode::CHAN_RECV;
    }

    [[nodiscard]] static constexpr bool is_frontend_specific(OpCode op) noexcept {
         return is_ownership_op(op) || is_concurrency_op(op) ||
                is_dsa_op(op)       || is_tensor_op(op);
    }
};

} // namespace dgtl::ir