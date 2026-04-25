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
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * TYPES ENGINE: Core Opcodes, Flags, and Semantic Traits
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Invariants)
 */

#pragma once
#include <cstdint>
#include <bit>

#ifdef CONST
#undef CONST
#endif

#ifdef INVALID_ATOM
#undef INVALID_ATOM
#endif

namespace dgtl::ir {

// === [CORE TYPE DEFINITIONS] ===

/**
 * @struct IRCore
 * @brief 32-byte cache-line optimized core (2 cores per 64-byte line).
 * Tier 2: The register and data payload.
 */
struct IRCore {
    uint32_t result_id = 0;     // 4 bytes: Destination Virtual Register
    
    uint32_t op_a = 0;
    uint32_t op_b = 0;
    uint32_t op_c = 0;
    
    uint64_t intent_hash = 0;   // 8 bytes: Structural identity for GVN (Tier 3)
    uint32_t logic_flags = 0;   // 4 bytes: Dialect-specific metadata
    uint32_t padding = 0;       // 4 bytes: Future-proofing / Alignment
};

/**
 * @enum OpCode
 * @brief 16-bit packed opcodes for high-density IR streams.
 */
enum class OpCode : uint16_t {
    NOP                  = 0,

    // Memory Operations (10-19)
    LOAD                 = 10,
    STORE                = 11,
    REG_MOVE             = 12,
    MOV                  = 12, // ALIAS
    LOAD_IMM             = 13,
    CONST                = 13, // ALIAS
    LEA                  = 14, 

    // Integer Arithmetic (20-29)
    OP_ADD               = 20, // Synchronized with test suite
    ADD_I32              = 20,
    SUB_I32              = 21,
    MUL_I32              = 22,
    DIV_I32              = 23,
    SHL_I32              = 24,
    AND_I32              = 25,
    OR_I32               = 26,
    XOR_I32              = 27,
    MOD_I32              = 28,

    // Integer Comparison (40-49) Ã¢â‚¬â€ result_id = 1 if true, 0 if false
    CMP_EQ_I32           = 40,
    CMP_NE_I32           = 41,
    CMP_LT_I32           = 42,
    CMP_LE_I32           = 43,
    CMP_GT_I32           = 44,
    CMP_GE_I32           = 45,

    // Floating Point (30-39)
    ADD_F32              = 30,
    SUB_F32              = 31,
    MUL_F32              = 32,
    DIV_F32              = 33,
    FMA_F32              = 35,
    FMA_WIDEN_F16_F32    = 36,

    // SIMD / Vector Extensions (0x500+)
    GATHER_F32_512       = 0x500,
    GATHER_16            = 0x501, 
    STORE_V512           = 0x502,
    VEC_ADD_F32_512      = 0x503,
    VEC_MUL_F32_512      = 0x504,
    FCVTL_F16_TO_F32     = 0x505,
    FCVTN_F32_TO_F16     = 0x506,
    SIMD_BSL_PREDICATE   = 0x507,

    // Control Flow (99-119)
    GOTO                 = 99,
    BRANCH               = 100,
    RET                  = 101, 
    RETURN               = 101, 
    BRANCH_COND          = 102,
    BRANCH_COMPARE_FUSED = 103,
    CALL                 = 110,
    SYSCALL              = 111,

    // Bit Manipulation (150-160)
    BIT_FIELD_INSERT     = 150,
    BIT_FIELD_EXTRACT    = 151,

    // Specialization Opcodes (200-210)
    GUARD_VALUE          = 200,
    IMMEDIATE            = 201,
    PHI_NODE             = 202,

    // Dialect Extensions (0x8000+)
    CUSTOM_DIALECT_START = 0x8000,
    GUARD_ALIAS          = 0x8001
};

/** * @struct IRAtom
 * @brief 32-bit handle for IR entities. 
 * Tier 1 Atom: High 16-bits = OpCode, Low 16-bits = Flags.
 */
#pragma pack(push, 1)
struct IRAtom {
    OpCode opcode;
    uint16_t flags;
    
    // Default constructor
    IRAtom() = default;
    
    // Constructor from opcode and flags
    constexpr IRAtom(OpCode op, uint16_t fl) noexcept : opcode(op), flags(fl) {}
    
    // Conversion to uint32_t for compatibility
    constexpr operator uint32_t() const noexcept {
        return (static_cast<uint32_t>(opcode) << 16) | flags;
    }
    
    // Assignment from uint32_t
    constexpr IRAtom& operator=(uint32_t value) noexcept {
        opcode = static_cast<OpCode>(value >> 16);
        flags = static_cast<uint16_t>(value & 0xFFFF);
        return *this;
    }
};
#pragma pack(pop)

/**
 * @struct Flags
 * @brief 16-bit atom flags. Used in Tier 1 for rapid filtering.
 */
namespace Flags {
    inline constexpr uint16_t NONE                  = 0x0000;
    inline constexpr uint16_t IS_SPEC_CONST         = 0x0001; 
    inline constexpr uint16_t IS_SPILL_CANDIDATE    = 0x0002; 
    inline constexpr uint16_t REQUIRES_STACK_EMIT   = 0x0004; 
    inline constexpr uint16_t HAS_STACK_OPERAND     = 0x0008; 
    inline constexpr uint16_t IS_FUSED              = 0x0010; 
    inline constexpr uint16_t IS_OPTIMIZED          = 0x0020; 
    inline constexpr uint16_t IS_DEAD               = 0x0040; 
    inline constexpr uint16_t IS_SIMD_BURST         = 0x0080; 
    inline constexpr uint16_t REQUIRES_VEC_PORT     = 0x0100;
    inline constexpr uint16_t SVE_ACTIVE            = 0x0200; 
    inline constexpr uint16_t HAS_SIDE_EFFECTS      = 0x0400; 
    inline constexpr uint16_t IS_RETURN             = 0x0800; 
    inline constexpr uint16_t IS_VOLATILE           = 0x1000; 
    inline constexpr uint16_t IS_CONST              = 0x2000; 
    inline constexpr uint16_t IS_HOSTED             = 0x4000; // Flag for environment-hosted resources
    inline constexpr uint16_t REQUIRES_DEPASS_TARGET = IS_HOSTED; // Compatibility alias for deoptimization targets
    inline constexpr uint16_t IS_MACRO_FUSED        = 0x8000; // Shifted to maintain 16-bit alignment
    inline constexpr uint16_t NON_TEMPORAL_HINT     = IS_HOSTED; // Compatibility alias for cache-bypass hinting
    
    // Branch hints (utilizing high bits or specific patterns)
    inline constexpr uint16_t BRANCH_LIKELY         = 0xA000; 
    inline constexpr uint16_t BRANCH_UNLIKELY       = 0xC000; 
}

/**
 * @section Global Sentinels
 */
inline constexpr uint32_t NULL_REGISTER = 0x0;
inline constexpr uint32_t INVALID_ATOM = 0xFFFFFFFF;
inline constexpr uint32_t INVALID_SLOT = INVALID_ATOM; 

/**
 * @struct ExtendedFlags
 * @brief 32-bit flags for advanced tiling and cache-aware optimizations.
 */
struct ExtendedFlags {
    static constexpr uint32_t IS_TILING_OPTIMIZED    = 0x00010000; 
    static constexpr uint32_t IS_PREFETCHED          = 0x00020000;
};

// === [UTILITIES] ===

/** @brief Helper to reconstruct high-level info from a Tier 1 Atom. */
struct AtomSplitter {
    static constexpr OpCode get_opcode(IRAtom atom) noexcept { 
        return atom.opcode;
    }
    static constexpr uint16_t get_flags(IRAtom atom) noexcept { 
        return atom.flags;
    }
    static constexpr IRAtom pack(OpCode op, uint16_t flags) noexcept {
        return IRAtom(op, flags);
    }
    static constexpr IRAtom pack(uint32_t value) noexcept {
        return IRAtom(static_cast<OpCode>(value >> 16), static_cast<uint16_t>(value & 0xFFFF));
    }
};

} // namespace dgtl::ir