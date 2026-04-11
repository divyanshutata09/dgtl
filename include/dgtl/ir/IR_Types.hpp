/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * FUNDAMENTAL TYPE DEFINITIONS
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once
#include <cstdint>

namespace dgtl::ir {

/**
 * @section Global Sentinels
 */
static constexpr uint32_t INVALID_SLOT = 0xFFFFFFFF;
static constexpr uint32_t NULL_REGISTER = 0x0;

/**
 * @enum OpCode
 * @brief 16-bit packed opcodes for high-density IR streams.
 * Range 0x0000 - 0x7FFF: Reserved for Core/Standard Opcodes.
 * Range 0x8000 - 0xFFFF: Reserved for Custom Dialects.
 */
enum class OpCode : uint16_t {
    NOP                  = 0,

    // Memory Operations (10-19)
    LOAD                 = 10,
    STORE                = 11,
    REG_MOVE             = 12,
    LOAD_IMM             = 13,
    LEA                  = 14, // Load Effective Address

    // Integer Arithmetic (I32) (20-29)
    ADD_I32              = 20,
    SUB_I32              = 21,
    MUL_I32              = 22,
    DIV_I32              = 23,
    SHL_I32              = 24,
    AND_I32              = 25,
    OR_I32               = 26,
    XOR_I32              = 27,

    // Floating Point (F32) (30-39)
    ADD_F32              = 30,
    SUB_F32              = 31,
    MUL_F32              = 32,
    DIV_F32              = 33,
    FMA_F32              = 35,

    // SIMD / Vector Extensions (0x500+)
    GATHER_F32_512       = 0x500,
    GATHER_16            = 0x501, // [FIX]: Explicit Gather-16 for Strike Emitter
    STORE_V512           = 0x502,
    VEC_ADD_F32_512      = 0x503,
    VEC_MUL_F32_512      = 0x504,

    // Control Flow (99-119)
    GOTO                 = 99,
    BRANCH               = 100,
    RET                  = 101,   // ✅ RETURN FIX
    RETURN               = 101,   // ✅ ALIAS
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

/**
 * @struct Flags
 * @brief 16-bit atom flags for optimization hints and side-effect tracking.
 */
struct Flags {
    static constexpr uint16_t NONE                   = 0x0000;
    static constexpr uint16_t IS_SPEC_CONST          = 0x0001; 
    static constexpr uint16_t IS_SPILL_CANDIDATE      = 0x0002; 
    static constexpr uint16_t REQUIRES_STACK_EMIT     = 0x0004; 
    static constexpr uint16_t HAS_STACK_OPERAND       = 0x0008; 
    static constexpr uint16_t IS_FUSED                = 0x0010; 
    static constexpr uint16_t IS_OPTIMIZED            = 0x0020; 
    static constexpr uint16_t IS_DEAD                 = 0x0040; 
    static constexpr uint16_t IS_SIMD_BURST           = 0x0080; 
    static constexpr uint16_t REQUIRES_VEC_PORT       = 0x0100;

    // [FIX]: Separated Side Effects from Return bits to prevent DCE corruption
    static constexpr uint16_t HAS_SIDE_EFFECTS        = 0x0200; 
    static constexpr uint16_t IS_RETURN               = 0x0400; 

    static constexpr uint16_t IS_VOLATILE             = 0x0800; 
    static constexpr uint16_t IS_CONST                = 0x1000; 
    static constexpr uint16_t IS_MACRO_FUSED          = 0x2000; 
    static constexpr uint16_t SVE_ACTIVE              = 0x4000; 
    static constexpr uint16_t BRANCH_LIKELY           = 0x4000; 
    static constexpr uint16_t BRANCH_UNLIKELY         = 0x8000; 
    
    // High-performance hints
    static constexpr uint16_t REQUIRES_DEpass_TARGET   = 0x1000; 
    static constexpr uint16_t IS_HOISTED              = 0x2000;
};

/**
 * @struct ExtendedFlags
 * @brief 32-bit flags for advanced tiling and cache-aware optimizations.
 */
struct ExtendedFlags {
    static constexpr uint32_t IS_TILING_OPTIMIZED    = 0x00010000; 
    static constexpr uint32_t IS_PREFETCHED          = 0x00020000;
};

#pragma pack(push, 1)

/**
 * @struct IRAtom
 * @brief Tier 1: The lightweight opcode carrier.
 * Statically asserted to 4 bytes for HFT stride consistency.
 */
struct IRAtom {
    OpCode opcode;
    uint16_t flags;
};

/**
 * @struct IRCore
 * @brief Tier 2: The register and data payload.
 * ✅ BIR 2.1: intent_hash and op_c included to reach exactly 20 bytes.
 */
struct IRCore {
    uint32_t result_id;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint32_t intent_hash;
};

#pragma pack(pop)

// --- Invariants for HFT/Sub-millisecond execution tiers ---
static_assert(sizeof(IRAtom) == 4, "IRAtom must be 4 bytes for cache-line packing");
static_assert(sizeof(IRCore) == 20, "IRCore must be 20 bytes to satisfy Sentinel intent_hash alignment");

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
