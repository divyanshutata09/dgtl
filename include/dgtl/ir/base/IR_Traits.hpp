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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * FUNDAMENTAL traits and semantic queries for IR nodes and opcodes.
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once
#include <cstdint>
#include <bit>
#include <type_traits>

// Ensure IR_Types defines the OpCode and Flags used below
#include "IR_Types.hpp"


/**
 * @section Preprocessor Hygiene
 * Neutralize Windows macro pollution to prevent "expected unqualified-id" errors.
 */
#ifdef CONST
    #undef CONST
#endif
#ifdef MOV
    #undef MOV
#endif

namespace dgtl::ir {


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
               op == OpCode::GOTO;
    }

    [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
        return is_branch(op) || op == OpCode::RET || op == OpCode::RETURN;
    }

    [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
        return op == OpCode::LOAD || 
               op == OpCode::STORE || 
               op == OpCode::GATHER_F32_512 || 
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::LEA ||
               op == OpCode::VEC_LOAD ||
               op == OpCode::VEC_STORE ||
               op == OpCode::PREFETCH;
    }

    [[nodiscard]] static constexpr bool has_side_effects(OpCode op, uint16_t flags) noexcept {
        // Direct flag checks
        if (flags & Flags::HAS_SIDE_EFFECTS) return true; 
        if (flags & Flags::IS_VOLATILE)       return true; 
        
        // OpCode-level architecture invariants
        switch (op) {
            case OpCode::STORE:
            case OpCode::STORE_V512:
            case OpCode::CALL:
            case OpCode::SYSCALL:
            case OpCode::RET:
            case OpCode::BRANCH:
            case OpCode::BRANCH_COND:
            case OpCode::GOTO:
            case OpCode::GUARD_ALIAS:
            case OpCode::GUARD_VALUE:
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
        return op >= OpCode::CMP_EQ_I32 && op <= OpCode::CMP_GE_I32;
    }

    [[nodiscard]] static constexpr bool is_control_flow(OpCode op) noexcept {
        return is_branch(op) || op == OpCode::RET || op == OpCode::RETURN ||
               op == OpCode::CALL || op == OpCode::SYSCALL || op == OpCode::LABEL;
    }

    [[nodiscard]] static constexpr bool is_valid_slot(uint32_t slot_id) noexcept {
        return slot_id != INVALID_SLOT && slot_id < 0xFFFFFF00;
    }

    [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
        if (is_branch(op)) return 5.0;
        if (op == OpCode::SYSCALL) return 100.0; 
        if (is_simd(op)) return 3.0;  // SIMD ops have moderate inline weight
        return 1.0;
    }

    // ========================================================================
    // === CROSS-PLATFORM BACKEND SUPPORT DETECTION (8-9 Backends)
    // ========================================================================
    
    // ========================================================================
    // === BACKEND SUPPORT DETECTION - PRODUCTION GRADE (9 ISA TARGETS)
    // ========================================================================

    [[nodiscard]] static constexpr bool supports_on_x64_avx512(OpCode op) noexcept {
        // x86-64 with AVX-512 (Intel/AMD EPYC) - Primary target
        // Full support for all operations including SIMD, gather/scatter, FMA
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;
        if (op == OpCode::STORE_V512) return true;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return true;
        return !is_compare_op(op) || op <= OpCode::CMP_GE_I32;
    }

    [[nodiscard]] static constexpr bool supports_on_arm64_sve(OpCode op) noexcept {
        // ARM64 with SVE (Scalable Vector Extension) - HPC/Server target
        // SVE supports dynamic-width gather/scatter, FMA, and all FP ops
        if (op == OpCode::GATHER_F32_512) return true;  // SVE GATHER is native
        if (op == OpCode::GATHER_16) return true;       // SVE GATHER.16
        if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::VEC_MUL_F32_512) return true;
        if (op == OpCode::FMA_F32) return true;         // SVE has native FMA
        return true; // SVE has comprehensive support
    }

    [[nodiscard]] static constexpr bool supports_on_arm32_neon(OpCode op) noexcept {
        // ARM32 with NEON (ARMv8, legacy mobile) - Limited 128-bit SIMD
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;  // No scatter-gather
        if (op == OpCode::STORE_V512) return false;  // 512-bit unsupported
        if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::VEC_MUL_F32_512) return false;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return true;  // NEON F16
        return true; // General ops OK
    }

    [[nodiscard]] static constexpr bool supports_on_riscv64_rv64gc(OpCode op) noexcept {
        // RISC-V 64-bit with general + compressed extensions - Open ISA
        // No native SIMD; use scalar fallback
        if (is_simd(op) && op != OpCode::FCVTL_F16_TO_F32 && op != OpCode::FCVTN_F32_TO_F16) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_wasm(OpCode op) noexcept {
        // WebAssembly (limited SIMD, no gather/scatter) - Browser/edge
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return false;
        if (op == OpCode::STORE_V512) return false;
        if (op == OpCode::FCVTL_F16_TO_F32 || op == OpCode::FCVTN_F32_TO_F16) return false;  // F16 unsupported
        if (is_simd(op)) return false;  // WASM SIMD is limited
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_cuda_ptx(OpCode op) noexcept {
        // NVIDIA CUDA (GPU via PTX) - parallel compute target
        // No traditional control flow; gather is native for warps
        if (is_control_flow(op) && op != OpCode::LABEL) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;  // GPU gather
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_rocm_llvmir(OpCode op) noexcept {
        // AMD ROCm (GPU via LLVM IR) - parallel compute target
        // Similar to CUDA but slightly different semantics
        if (is_control_flow(op) && op != OpCode::LABEL) return false;
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return true;
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_nasm_x64() noexcept {
        // NASM x64 inline assembly - Direct hardware mapping
        // Full x64 ISA support for inline asm
        return true;
    }

    [[nodiscard]] static constexpr bool supports_on_fpga_verilog() noexcept {
        // FPGA synthesis (Verilog RTL) - Reconfigurable hardware
        // All ops translatable to HDL; SIMD native
        return true;
    }

    // JIT/AOT/Hybrid flags for backend selection
    [[nodiscard]] static constexpr bool is_jit_friendly(OpCode op) noexcept {
        // Operations that compile quickly in JIT
        return !(is_simd(op) && op != OpCode::FCVTL_F16_TO_F32 && op != OpCode::FCVTN_F32_TO_F16);
    }

    [[nodiscard]] static constexpr bool is_aot_intensive(OpCode op) noexcept {
        // Operations that benefit from offline optimization
        return is_simd(op) || op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16;
    }

    // ========================================================================
    // === EXTENDED OPTIMIZATION HINTS (Better than LLVM)
    // ========================================================================

    [[nodiscard]] static constexpr bool is_commutative(OpCode op) noexcept {
        // Operations where operand order doesn't matter (enable reordering)
        switch (op) {
            case OpCode::ADD_I32:
            case OpCode::ADD_I64:
            case OpCode::ADD_F32:
            case OpCode::ADD_F64:
            case OpCode::MUL_I32:
            case OpCode::MUL_I64:
            case OpCode::MUL_F32:
            case OpCode::MUL_F64:
            case OpCode::AND_I32:
            case OpCode::OR_I32:
            case OpCode::XOR_I32:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_associative(OpCode op) noexcept {
        // Operations where grouping doesn't matter (enable tree reduction)
        return is_commutative(op) && op != OpCode::ADD_F32 && op != OpCode::ADD_F64 && 
               op != OpCode::MUL_F32 && op != OpCode::MUL_F64; // FP ops are not strictly associative
    }

    [[nodiscard]] static constexpr bool is_single_def_use(OpCode op) noexcept {
        // Operations typically used only once (good candidates for inlining)
        switch (op) {
            case OpCode::LOAD_IMM:
            case OpCode::IMMEDIATE:
            case OpCode::LEA:
            case OpCode::LOAD:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool can_fold_constants(OpCode op) noexcept {
        // Operations that can be folded at compile time
        switch (op) {
            case OpCode::ADD_I32:
            case OpCode::SUB_I32:
            case OpCode::MUL_I32:
            case OpCode::DIV_I32:
            case OpCode::AND_I32:
            case OpCode::OR_I32:
            case OpCode::XOR_I32:
            case OpCode::SHL_I32:
            case OpCode::SHR_I32:
            case OpCode::MOD_I32:
            case OpCode::ADD_I64:
            case OpCode::SUB_I64:
            case OpCode::MUL_I64:
            case OpCode::DIV_I64:
            case OpCode::BIT_FIELD_INSERT:
            case OpCode::BIT_FIELD_EXTRACT:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_memory_barrier(OpCode op) noexcept {
        // Operations that have memory fence semantics
        switch (op) {
            case OpCode::CALL:
            case OpCode::SYSCALL:
            case OpCode::STORE:
            case OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool can_be_reordered(OpCode op1, OpCode op2) noexcept {
        // Check if two operations can be safely reordered
        if (is_memory_barrier(op1) || is_memory_barrier(op2)) return false;
        if (has_side_effects(op1, Flags::NONE) || has_side_effects(op2, Flags::NONE)) return false;
        return true;
    }

    [[nodiscard]] static constexpr double estimate_latency_cycles(OpCode op) noexcept {
        // Estimate hardware latency for pipeline scheduling
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 6.0;   // Memory latency
        if (op == OpCode::DIV_I32 || op == OpCode::DIV_I64 || op == OpCode::DIV_F32 || op == OpCode::DIV_F64) return 15.0;
        if (op == OpCode::MUL_I32 || op == OpCode::MUL_I64) return 3.0;
        if (op == OpCode::FMA_F32) return 4.0;                    // Fused multiply-add
        if (op == OpCode::MUL_F32 || op == OpCode::MUL_F64) return 4.0;
        if (op == OpCode::LOAD || op == OpCode::STORE) return 2.5;
        if (is_branch(op)) return 0.5;                            // Branch is zero-latency in superscalar
        return 1.0; // Default ALU latency
    }

    [[nodiscard]] static constexpr uint32_t estimate_throughput_slots(OpCode op) noexcept {
        // How many execution ports this op uses (for superscalar scheduling)
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 1;  // Load port
        if (op == OpCode::STORE_V512) return 1;                                   // Store port
        if (op == OpCode::DIV_I32 || op == OpCode::DIV_I64) return 1;            // Special unit
        if (op == OpCode::MUL_I32 || op == OpCode::MUL_I64 || op == OpCode::FMA_F32) return 1;
        if (is_branch(op)) return 0;                                              // Branch is free
        return 1; // Default: single execution unit
    }

    // ============================================================================
    // NEW: OWNERSHIP & CONCURRENCY DETECTION (UNIVERSAL LANGUAGE SUPPORT)
    // ============================================================================

    [[nodiscard]] static constexpr bool is_ownership_op(OpCode op) noexcept {
        // Operations that manage resource ownership
        switch (op) {
            case OpCode::OWNERSHIP_ACQUIRE:
            case OpCode::OWNERSHIP_RELEASE:
            case OpCode::OWNERSHIP_TRANSFER:
            case OpCode::ATOMIC_SNAPSHOT:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_concurrency_op(OpCode op) noexcept {
        // Operations that involve concurrency primitives
        switch (op) {
            case OpCode::ASYNC_SPAWN:
            case OpCode::ASYNC_AWAIT:
            case OpCode::FIBER_YIELD:
            case OpCode::BARRIER_SYNC:
            case OpCode::CHAN_SEND:
            case OpCode::CHAN_RECV:
            case OpCode::LOCK_ACQUIRE:
            case OpCode::LOCK_RELEASE:
            case OpCode::MEMORY_FENCE:
            case OpCode::THREAD_SPAWN:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_dsa_op(OpCode op) noexcept {
        // Operations on complex data structures (Graphs, Trees, Tries)
        switch (op) {
            case OpCode::GRAPH_BUILD:
            case OpCode::GRAPH_TRAVERSE:
            case OpCode::TREE_BUILD:
            case OpCode::TREE_TRAVERSE:
            case OpCode::TRIE_BUILD:
            case OpCode::TRIE_LOOKUP:
            case OpCode::TRIE_INSERT:
            case OpCode::DSA_ALLOCATE:
            case OpCode::DSA_DEALLOCATE:
            case OpCode::SCOP_ANALYZE:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool is_tensor_op(OpCode op) noexcept {
        // ML/HPC tensor operations
        switch (op) {
            case OpCode::TENSOR_RESHAPE:
            case OpCode::TENSOR_REDUCE:
            case OpCode::TENSOR_TRANSPOSE:
            case OpCode::TENSOR_SLICE:
            case OpCode::TENSOR_CONCAT:
            case OpCode::TENSOR_BROADCAST:
            case OpCode::TENSOR_GEMM:
            case OpCode::TENSOR_CONV:
            case OpCode::TENSOR_POOL:
            case OpCode::TENSOR_SOFTMAX:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool must_synchronize(OpCode op) noexcept {
        // Operations that require synchronization barriers
        switch (op) {
            case OpCode::ASYNC_SPAWN:
            case OpCode::THREAD_SPAWN:
            case OpCode::BARRIER_SYNC:
            case OpCode::CHAN_SEND:
            case OpCode::CHAN_RECV:
            case OpCode::MEMORY_FENCE:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static constexpr bool may_create_deadlock(OpCode op) noexcept {
        // Operations with potential deadlock risk
        return op == OpCode::LOCK_ACQUIRE || 
               op == OpCode::BARRIER_SYNC ||
               op == OpCode::CHAN_SEND ||
               op == OpCode::CHAN_RECV;
    }

    [[nodiscard]] static constexpr bool is_frontend_specific(OpCode op) noexcept {
        // Operations requiring language-specific handling
        return is_ownership_op(op) || 
               is_concurrency_op(op) || 
               is_dsa_op(op) ||
               is_tensor_op(op);
    }
};



} // namespace dgtl::ir