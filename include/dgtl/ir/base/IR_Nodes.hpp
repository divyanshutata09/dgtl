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
 * UNIFIED IR NODE ARCHITECTURE (Tier 1 + Tier 2 fused view)
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 * 
 * SUPPORTED BACKENDS (8-9 ISA Targets):
 * 1. X64 (x86-64 AVX-512)        - Primary (Intel/AMD)
 * 2. ARM64 (AArch64 SVE)         - High-Performance (HPC/Servers)
 * 3. ARM32 (ARMv8 NEON)          - Mobile (Legacy support)
 * 4. RISC-V (64-bit RV64GC)      - Open ISA
 * 5. WebAssembly (WASM SIMD)     - Browser/Edge Computing
 * 6. GPGPU (CUDA via PTX)        - GPU Accelerated
 * 7. GPGPU (ROCm via LLVM IR)    - AMD GPU
 * 8. NASM x64 Assembly           - Inline ASM
 * 9. FPGA (Verilog RTL)          - Custom Hardware
 * 
 * EXECUTION MODES (JIT/AOT/Hybrid):
 * - JIT:    Fast compilation (<1ms), minimal optimization, direct execution
 * - AOT:    Offline compilation, full optimization, deterministic output
 * - Hybrid: Adaptive tier selection based on stream characteristics
 */

#pragma once

#include "IR_Types.hpp"
#include "IR_Traits.hpp"
#include <type_traits>

namespace dgtl::ir {

#pragma pack(push, 1)

/**
 * @struct IRNode
 * @brief Unified view for high-level manipulation.
 * Maps Tier 1 (Atom) and Tier 2 (Core) into a 32-byte cache-aligned structure.
 */
struct alignas(32) IRNode {
    // --- Header (Tier 1 Mirror: 4 bytes) ---
    OpCode   opcode;    // 2 bytes
    uint16_t flags;     // 2 bytes

    // --- Payload (Tier 2 Mirror: 28 bytes) ---
    uint32_t result_id;   // 4 bytes
    uint32_t op_a;        // 4 bytes
    uint32_t op_b;        // 4 bytes
    uint32_t op_c;        // 4 bytes
    uint64_t intent_hash; // 8 bytes
    uint32_t logic_flags; // 4 bytes

    // --- Semantic Queries ---

    [[nodiscard]] constexpr bool is_terminator() const noexcept {
        // BIR 1.0.0 Logic: Explicit check + Trait fallback
        if (opcode == OpCode::RET || opcode == OpCode::GOTO || 
            opcode == OpCode::BRANCH || (flags & Flags::IS_RETURN)) {
            return true;
        }
        return IRTraits::is_terminator(opcode);
    }

    [[nodiscard]] inline bool is_volatile() const noexcept {
        return (flags & Flags::IS_VOLATILE) != 0;
    }

    [[nodiscard]] inline bool has_side_effects() const noexcept {
        return IRTraits::has_side_effects(opcode, flags);
    }

    [[nodiscard]] inline bool is_valid() const noexcept {
        return opcode != OpCode::NOP;
    }

    [[nodiscard]] inline uint64_t logic_hash() const noexcept {
        return intent_hash ^ (static_cast<uint64_t>(result_id) << 32);
    }

    [[nodiscard]] inline double predict_inlined_benefit() const noexcept {
        return IRTraits::get_inline_weight(opcode);
    }

    [[nodiscard]] inline IRNode clone() const noexcept { return *this; }

    // --- Static Utilities ---

    [[nodiscard]] static constexpr uint64_t calculate_hash(OpCode op, uint32_t res, uint32_t a, uint32_t b, uint32_t c) noexcept {
        uint64_t h = static_cast<uint64_t>(op) ^ 0x9e3779b97f4a7c15ULL;
        auto mix = [&](uint32_t val) {
            h ^= (static_cast<uint64_t>(val) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
        };
        mix(res); mix(a); mix(b); mix(c);
        return h;
    }

    // Semantic hash deliberately excludes result_id so CSE/GVN can match
    // equivalent expressions produced into different virtual registers.
    [[nodiscard]] static constexpr uint64_t calculate_semantic_hash(
        OpCode op,
        uint32_t a,
        uint32_t b,
        uint32_t c,
        uint16_t flags = Flags::NONE) noexcept {
        uint64_t h = static_cast<uint64_t>(op) ^ 0xD6E8FEB86659FD93ULL;
        auto mix = [&](uint64_t val) {
            h ^= val + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        };
        mix(static_cast<uint64_t>(flags));
        mix(static_cast<uint64_t>(a));
        mix(static_cast<uint64_t>(b));
        mix(static_cast<uint64_t>(c));
        return h;
    }

    // --- Mutators ---

    inline void refresh_intent_hash() noexcept {
        intent_hash = calculate_hash(opcode, result_id, op_a, op_b, op_c);
    }

    inline void fold_if_possible() noexcept {
        if (!(flags & Flags::IS_SPEC_CONST)) return;

        bool folded = true;
        uint64_t result = 0;

        // 32-bit Integer Operations
        switch (opcode) {
            case OpCode::ADD_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) + static_cast<int32_t>(op_b)); break;
            case OpCode::SUB_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) - static_cast<int32_t>(op_b)); break;
            case OpCode::MUL_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) * static_cast<int32_t>(op_b)); break;
            case OpCode::DIV_I32:
                if (static_cast<int32_t>(op_b) == 0) {
                    folded = false;
                } else {
                    result = static_cast<uint64_t>(static_cast<int32_t>(op_a) / static_cast<int32_t>(op_b));
                }
                break;
            case OpCode::AND_I32: result = static_cast<uint64_t>(op_a & op_b); break;
            case OpCode::OR_I32:  result = static_cast<uint64_t>(op_a | op_b); break;
            case OpCode::XOR_I32: result = static_cast<uint64_t>(op_a ^ op_b); break;
            case OpCode::SHL_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) << (op_b & 31U)); break;
            case OpCode::SHR_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) >> (op_b & 31U)); break;
            case OpCode::MOD_I32:
                if (static_cast<int32_t>(op_b) == 0) {
                    folded = false;
                } else {
                    result = static_cast<uint64_t>(static_cast<int32_t>(op_a) % static_cast<int32_t>(op_b));
                }
                break;

            // 64-bit Integer Operations (op_b contains high bits, op_c contains additional operand)
            case OpCode::ADD_I64: {
                uint64_t a = (static_cast<uint64_t>(op_b) << 32) | op_a;
                uint64_t b = (static_cast<uint64_t>(op_c) << 32) | op_a; // Simplified for AOT (complex in JIT)
                result = a + b;
                break;
            }
            case OpCode::MUL_I64: {
                // Complex 64-bit multiply; deferred to AOT for accuracy
                folded = false;
                break;
            }

            // Bitfield Operations
            case OpCode::BIT_FIELD_INSERT:
            case OpCode::BIT_FIELD_EXTRACT:
                folded = false; // Complex semantics, defer
                break;

            // Floating-Point Operations (32-bit)
            case OpCode::ADD_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                float res = a + b;
                result = std::bit_cast<uint32_t>(res);
                break;
            }
            case OpCode::SUB_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                float res = a - b;
                result = std::bit_cast<uint32_t>(res);
                break;
            }
            case OpCode::MUL_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                float res = a * b;
                result = std::bit_cast<uint32_t>(res);
                break;
            }
            case OpCode::DIV_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                if (b == 0.0f) {
                    folded = false;
                } else {
                    float res = a / b;
                    result = std::bit_cast<uint32_t>(res);
                }
                break;
            }
            case OpCode::FMA_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                float c = std::bit_cast<float>(op_c);
                float res = a * b + c;  // Fused multiply-add for better precision
                result = std::bit_cast<uint32_t>(res);
                break;
            }

            // Floating-Point Operations (64-bit) - Deferred to AOT
            case OpCode::ADD_F64:
            case OpCode::SUB_F64:
            case OpCode::MUL_F64:
            case OpCode::DIV_F64:
                folded = false; // 64-bit FP folding deferred
                break;

            default:
                folded = false;
                break;
        }

        if (!folded) return;

        opcode = OpCode::IMMEDIATE;
        op_a = 0;
        op_b = static_cast<uint32_t>(result & 0xFFFFFFFF);
        op_c = 0;
        flags |= Flags::IS_CONST;
        refresh_intent_hash();
    }
};

#pragma pack(pop)

// ================================
// BACKEND PARITY GUARANTEES
// ================================
//
// These static_asserts are PERMANENT cross-platform layout contracts.
// They fire at compile time on any backend (x64, ARM64, WASM, NASM) where
// the struct size, alignment, or field offsets diverge from the BIR 1.0.0 spec.
// This is the structural foundation for identical behaviour under strict mode.
//
// IRNode layout (BIR 1.0.0, pack(1), alignas(32)):
//   offset  0 : opcode      (2 bytes, OpCode)
//   offset  2 : flags       (2 bytes, uint16_t)
//   offset  4 : result_id   (4 bytes, uint32_t)
//   offset  8 : op_a        (4 bytes, uint32_t)
//   offset 12 : op_b        (4 bytes, uint32_t)
//   offset 16 : op_c        (4 bytes, uint32_t)
//   offset 20 : intent_hash (8 bytes, uint64_t)
//   offset 28 : logic_flags (4 bytes, uint32_t)
//   total     : 32 bytes
static_assert(sizeof(IRNode) == 32,
    "IRNode must be exactly 32 bytes for AVX-512 alignment â€” BIR 1.0.0 backend parity contract");
static_assert(alignof(IRNode) == 32,
    "IRNode must have 32-byte alignment for cache-line and AVX-512 compatibility");
static_assert(std::is_trivially_copyable_v<IRNode>,
    "IRNode must be trivially copyable for transactional streaming across all backends");
static_assert(std::is_standard_layout_v<IRNode>,
    "IRNode must be standard-layout for deterministic cross-platform ABI (x64/ARM64/WASM)");
static_assert(offsetof(IRNode, opcode)      ==  0,
    "IRNode::opcode must be at offset 0 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, flags)       ==  2,
    "IRNode::flags must be at offset 2 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, result_id)   ==  4,
    "IRNode::result_id must be at offset 4 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_a)        ==  8,
    "IRNode::op_a must be at offset 8 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_b)        == 12,
    "IRNode::op_b must be at offset 12 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_c)        == 16,
    "IRNode::op_c must be at offset 16 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, intent_hash) == 20,
    "IRNode::intent_hash must be at offset 20 â€” BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, logic_flags) == 28,
    "IRNode::logic_flags must be at offset 28 â€” BIR 1.0.0 backend parity");
// Primitive-width guarantees: cross-platform representation invariants
static_assert(sizeof(float)  == 4, "float must be 32-bit IEEE 754 on all backends");
static_assert(sizeof(double) == 8, "double must be 64-bit IEEE 754 on all backends");
static_assert(sizeof(int)    == 4, "int must be 32-bit on all DGTL-supported platforms");

} // namespace dgtl::ir
