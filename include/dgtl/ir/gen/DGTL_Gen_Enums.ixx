/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Auto-generated DGTL IR Enumerations and Core Opcodes
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 *
 * ============================================================================
 * AUTO-GENERATED FILE — DO NOT EDIT MANUALLY
 * Generator:        tools/cpp/dgtl_gen_main.cpp
 * Canonical Source: defs/ir/phase1_core_ops.dgtl_def
 * ============================================================================
 */

module;

#include <cstddef>
#include <cstdint>

export module dgtl.ir_enums;

export namespace dgtl::ir {

enum class OpCode : std::uint16_t {
    NOP = 0x0,
    UNDEF = 0x59,
    LOAD = 0xA,
    STORE = 0xB,
    ATOMIC_LOAD = 0x10,
    ATOMIC_STORE = 0x11,
    ATOMIC_FENCE = 0x12,
    PTR_CAST = 0x51,
    PTR_DIFF = 0x52,
    GEP = 0x5A,
    PTR_TO_INT = 0x5B,
    INT_TO_PTR = 0x5C,
    BOOL_SELECT = 0x53,
    REG_MOVE = 0xC,
    LOAD_IMM = 0xD,
    LEA = 0xE,
    PREFETCH = 0xF,
    IMMEDIATE = 0x50,
    STRING_CONST = 0x54,
    STRING_LENGTH = 0x55,
    STRING_COMPARE = 0x56,
    STRING_CONCAT = 0x57,
    STRING_SUBSTRING = 0x58,
    ADD_I8 = 0x13,
    ADD_I32 = 0x14,
    SUB_I32 = 0x15,
    MUL_I32 = 0x16,
    DIV_I32 = 0x17,
    SHL_I32 = 0x18,
    AND_I32 = 0x19,
    OR_I32 = 0x1A,
    XOR_I32 = 0x1B,
    MOD_I32 = 0x1C,
    SHR_I32 = 0x1D,
    ADD_I64 = 0x3C,
    SUB_I64 = 0x3D,
    MUL_I64 = 0x3E,
    DIV_I64 = 0x3F,
    MOD_I64 = 0x40,
    ADD_I128 = 0x82,
    ADD_I256 = 0x83,
    ADD_F8 = 0x84,
    ADD_BF16 = 0x85,
    ADD_F32 = 0x1E,
    SUB_F32 = 0x1F,
    MUL_F32 = 0x20,
    DIV_F32 = 0x21,
    FMA_F32 = 0x23,
    FMA_WIDEN_F16_F32 = 0x24,
    ADD_F64 = 0x46,
    SUB_F64 = 0x47,
    MUL_F64 = 0x48,
    DIV_F64 = 0x49,
    CMP_EQ_I32 = 0x28,
    CMP_NE_I32 = 0x29,
    CMP_LT_I32 = 0x2A,
    CMP_LE_I32 = 0x2B,
    CMP_GT_I32 = 0x2C,
    CMP_GE_I32 = 0x2D,
    BIT_FIELD_INSERT = 0x32,
    BIT_FIELD_EXTRACT = 0x33,
    LABEL = 0x62,
    GOTO = 0x63,
    BRANCH = 0x64,
    BRANCH_COND = 0x66,
    BRANCH_COMPARE_FUSED = 0x67,
    CALL = 0x6E,
    SYSCALL = 0x6F,
    GUARD_ALIAS = 0x78,
    RET = 0x65,
    PHI_NODE = 0xCA,
    SELECT = 0xCC,
    GUARD_VALUE = 0xC8,
    DEOPT_TRIGGER = 0xC9,
    ASSUME = 0xCB,
    REGION_BEGIN = 0x258,
    REGION_END = 0x259,
    FUSION_GROUP = 0x25A,
    GATHER_F32_512 = 0x500,
    GATHER_16 = 0x501,
    STORE_V512 = 0x502,
    VEC_ADD_F32_512 = 0x503,
    VEC_MUL_F32_512 = 0x504,
    SIMD_BSL_PREDICATE = 0x507,
    FCVTL_F16_TO_F32 = 0x508,
    FCVTN_F32_TO_F16 = 0x509,
    VEC_LOAD = 0x510,
    VEC_STORE = 0x511,
    VEC_SHUFFLE = 0x512,
    VEC_REDUCE_ADD = 0x513,
    SIMD_REDUCE_MAX = 0x8010,
    SIMD_REDUCE_MIN = 0x8011,
    SIMD_SCATTER_INDIRECT = 0x8012,
    SIMD_MASKED_STORE = 0x8013,
    SIMD_INTERLEAVED_LOAD = 0x8014,
    SIMD_COMPLEX_FMA = 0x8015,
    GPU_LAUNCH = 0x190,
    OWNERSHIP_ACQUIRE = 0x19A,
    OWNERSHIP_RELEASE = 0x19B,
    OWNERSHIP_TRANSFER = 0x19C,
    ATOMIC_SNAPSHOT = 0x19D,
    GPU_MEM_TRANSFER = 0x8020,
    GPU_BARRIER = 0x8021,
    GPU_SYNC_POINT = 0x8022,
    GPU_KERNEL_FUSE = 0x8023,
    GPU_ANNOTATE_DEVICE = 0x8024,
    ASYNC_SPAWN = 0x1A4,
    ASYNC_AWAIT = 0x1A5,
    FIBER_YIELD = 0x1A6,
    BARRIER_SYNC = 0x1A7,
    CHAN_SEND = 0x1A8,
    CHAN_RECV = 0x1A9,
    LOCK_ACQUIRE = 0x1AA,
    LOCK_RELEASE = 0x1AB,
    MEMORY_FENCE = 0x1AC,
    THREAD_SPAWN = 0x1AD,
    WASM_INVOKE = 0x410,
    FIBER_YIELD_LOOP = 0x411,
    SUSPEND_CTX = 0x412,
    RESUME_CTX = 0x413,
    NEON_VADD_Q = 0x414,
    GRAPH_BUILD = 0x1B8,
    GRAPH_TRAVERSE = 0x1B9,
    TREE_BUILD = 0x1BA,
    TREE_TRAVERSE = 0x1BB,
    TRIE_BUILD = 0x1BC,
    TRIE_LOOKUP = 0x1BD,
    TRIE_INSERT = 0x1BE,
    DSA_ALLOCATE = 0x1BF,
    DSA_DEALLOCATE = 0x1C0,
    SCOP_ANALYZE = 0x1C1,
    TENSOR_RESHAPE = 0x1CC,
    TENSOR_REDUCE = 0x1CD,
    TENSOR_TRANSPOSE = 0x1CE,
    TENSOR_SLICE = 0x1CF,
    TENSOR_CONCAT = 0x1D0,
    TENSOR_BROADCAST = 0x1D1,
    TENSOR_GEMM = 0x1D2,
    TENSOR_CONV = 0x1D3,
    TENSOR_POOL = 0x1D4,
    TENSOR_SOFTMAX = 0x1D5,
    TENSOR_RELU = 0x8030,
    TENSOR_BATCHNORM = 0x8031,
    TENSOR_GATHER = 0x8032,
    TENSOR_SCATTER_D = 0x8033,
    DGTL_FAST_ADD = 0x8001,
    DGTL_FAST_FMA = 0x8002,
    CPP_TEMPLATE_SPEC = 0x8101,
    RUST_BORROW_MUT = 0x8102,
    PHI_MULTI = 0x200,
    TUPLE_PACK = 0x201,
    TUPLE_UNPACK = 0x202,
    SELECT_MULTI = 0x203,
    CALL_VARIADIC = 0x204,
    SWITCH_DISPATCH = 0x205,
    VOLATILE_LOAD = 0x206,
    VOLATILE_STORE = 0x207,
    ACQUIRE_FENCE = 0x208,
    RELEASE_FENCE = 0x209,
    READ_TSC = 0x600,
    BRANCH_LIKELY = 0x601,
    BRANCH_UNLIKELY = 0x602,
    CLFLUSH = 0x603,
    PREFETCH_T0 = 0x604,
    SPIN_ACQUIRE = 0x605,
    RING_PUSH_FREE = 0x606,
    MATMUL_BF16 = 0x480,
    MATMUL_FP8 = 0x481,
    SDPA_F16 = 0x482,
    SOFTMAX_FAST = 0x483,
    LAYER_NORM = 0x484,
    ROPE_EMBED = 0x485,
    FAST_INV_SQRT = 0x700,
    VEC3_CROSS = 0x701,
    VEC4_DOT = 0x702,
    MAT4_MUL = 0x703,
    QUAT_SLERP = 0x704,
    RAY_AABB_ISECT = 0x705,
    AND_I64 = 0x41,
    OR_I64 = 0x42,
    XOR_I64 = 0x43,
    SHL_I64 = 0x44,
    SHR_I64 = 0x45,

    // Canonical source-level aliases — emitted from the DSL.
    MOV = REG_MOVE,
    CONST = LOAD_IMM,
    OP_ADD = ADD_I32,
    RETURN = RET,
    ASYNC_LAUNCH = ASYNC_SPAWN,
    SYNC_BARRIER = BARRIER_SYNC,
};

enum class TypeCode : std::uint16_t {
    VOID_T = 0,
    BOOL = 1,
    PREDICATE = 2,
    ADDRESS_SPACE = 3,
    PTR = 4,
    STRING_HANDLE = 5,
    I4 = 6,
    I8 = 7,
    I16 = 8,
    I32 = 9,
    I64 = 10,
    I128 = 11,
    I256 = 12,
    I512 = 13,
    F8 = 14,
    BF16 = 15,
    F16 = 16,
    F32 = 17,
    F64 = 18,
    F128 = 19,
    VEC3_F32 = 20,
    VEC4_F32 = 21,
    MAT4X4_F32 = 22,
    QUAT_F32 = 23,
    VECTOR_F32X16 = 24,
    VECTOR_I32X16 = 25,
    VECTOR_F16X32 = 26,
    TENSOR_FP8 = 27,
    TENSOR_BF16 = 28,
    TENSOR_F32 = 29,
    TENSOR_I64 = 30,
};


inline constexpr std::uint32_t kMaxTypeHandle = static_cast<std::uint32_t>(TypeCode::TENSOR_I64);

[[nodiscard]] inline constexpr bool is_valid_type_handle(std::uint32_t type_handle) noexcept {
    return type_handle >= static_cast<std::uint32_t>(TypeCode::BOOL) &&
           type_handle <= kMaxTypeHandle;
}

[[nodiscard]] inline constexpr bool is_floating_type_code(TypeCode type_code) noexcept {
    return type_code == TypeCode::F8 ||
           type_code == TypeCode::BF16 ||
           type_code == TypeCode::F16 ||
           type_code == TypeCode::F32 ||
           type_code == TypeCode::F64 ||
           type_code == TypeCode::F128;
}

[[nodiscard]] inline constexpr bool is_pointer_type_code(TypeCode type_code) noexcept {
    return type_code == TypeCode::PTR;
}

namespace Flags {
    inline constexpr std::uint16_t NONE               = 0x0000u;
    inline constexpr std::uint16_t IS_CONST           = 0x0001u;
    inline constexpr std::uint16_t IS_DEAD            = 0x0002u;
    inline constexpr std::uint16_t HAS_SIDE_EFFECTS   = 0x0004u;
    inline constexpr std::uint16_t IS_VOLATILE        = 0x0008u;

    inline constexpr std::uint16_t IS_SIMD            = 0x0010u;
    inline constexpr std::uint16_t IS_SIMD_BURST      = IS_SIMD;
    inline constexpr std::uint16_t IS_VECTOR_LANE     = 0x0020u;
    inline constexpr std::uint16_t IS_PREDICATED      = 0x0040u;

    inline constexpr std::uint16_t IS_FUSED           = 0x0080u;
    inline constexpr std::uint16_t IS_MACRO_FUSED     = IS_FUSED;
    inline constexpr std::uint16_t IS_OPTIMIZED       = 0x0100u;
    inline constexpr std::uint16_t IS_LOOP_INVARIANT  = 0x0200u;

    inline constexpr std::uint16_t IS_SPEC_CONST      = 0x0400u;
    inline constexpr std::uint16_t NON_TEMPORAL       = 0x0800u;
    inline constexpr std::uint16_t NON_TEMPORAL_HINT  = NON_TEMPORAL;

    inline constexpr std::uint16_t IS_BRANCH          = 0x1000u;
    inline constexpr std::uint16_t IS_RETURN          = 0x2000u;

    inline constexpr std::uint16_t IS_GUARD           = 0x4000u;
    inline constexpr std::uint16_t MAY_DEOPT          = 0x8000u;

    inline constexpr std::uint16_t kCanonicalSemanticMask =
        IS_CONST | IS_DEAD | HAS_SIDE_EFFECTS | IS_VOLATILE |
        IS_SIMD | IS_VECTOR_LANE | IS_PREDICATED |
        IS_FUSED | IS_OPTIMIZED | IS_LOOP_INVARIANT |
        IS_SPEC_CONST | NON_TEMPORAL |
        IS_BRANCH | IS_RETURN | IS_GUARD | MAY_DEOPT;

    static_assert(kCanonicalSemanticMask == 0xFFFFu,
                  "Flags canonical semantic bits must be unique and fully packed (16/16 used)");
} // namespace Flags

namespace ExtendedFlags {
    inline constexpr std::uint32_t MEM_RELAXED   = 0x00000001u;
    inline constexpr std::uint32_t MEM_ACQUIRE   = 0x00000002u;
    inline constexpr std::uint32_t MEM_RELEASE_  = 0x00000004u;
    inline constexpr std::uint32_t MEM_SEQ_CST   = 0x00000008u;

    inline constexpr std::uint32_t NO_ALIAS      = 0x00000010u;
    inline constexpr std::uint32_t MAY_ALIAS     = 0x00000020u;

    inline constexpr std::uint32_t FAST_MATH     = 0x00000040u;
    inline constexpr std::uint32_t STRICT_FP     = 0x00000080u;

    inline constexpr std::uint32_t LATENCY_CRITICAL = 0x00000100u;
    inline constexpr std::uint32_t THROUGHPUT_MODE  = 0x00000200u;

    inline constexpr std::uint32_t PREFETCH_HINT = 0x00000400u;

    inline constexpr std::uint32_t VECTORIZABLE  = 0x00000800u;
    inline constexpr std::uint32_t VEC_WIDTH_128 = 0x00001000u;
    inline constexpr std::uint32_t VEC_WIDTH_256 = 0x00002000u;
    inline constexpr std::uint32_t VEC_WIDTH_512 = 0x00004000u;

    inline constexpr std::uint32_t PARALLEL_SAFE = 0x00008000u;
    inline constexpr std::uint32_t REQUIRES_SYNC = 0x00010000u;

    inline constexpr std::uint32_t HOT_PATH      = 0x00020000u;
    inline constexpr std::uint32_t COLD_PATH     = 0x00040000u;
    inline constexpr std::uint32_t HOST_ONLY     = 0x00080000u;
    inline constexpr std::uint32_t DEVICE_ONLY   = 0x00100000u;
    inline constexpr std::uint32_t PIPELINE_READY = 0x00200000u;

    inline constexpr std::uint32_t DEOPT_SAFEPOINT = 0x00400000u;

    inline constexpr std::uint32_t PROFILE_GUIDED   = 0x00800000u;
    inline constexpr std::uint32_t SPECULATIVE_PATH = 0x01000000u;
    inline constexpr std::uint32_t CACHE_HOT        = 0x02000000u;
    inline constexpr std::uint32_t CACHE_COLD       = 0x04000000u;
    inline constexpr std::uint32_t INLINE_CANDIDATE = 0x08000000u;
    inline constexpr std::uint32_t NO_INLINE        = 0x10000000u;
    inline constexpr std::uint32_t VIRTUALIZED      = 0x20000000u;
    inline constexpr std::uint32_t SECURITY_HARDEN  = 0x40000000u;
    inline constexpr std::uint32_t DETERMINISTIC    = 0x80000000u;

    inline constexpr std::uint64_t IS_OWNED         = 0x0001000000ULL;
    inline constexpr std::uint64_t IS_SHARED        = 0x0002000000ULL;
    inline constexpr std::uint64_t IS_ATOMIC        = 0x0004000000ULL;
    inline constexpr std::uint64_t IS_ISOLATED      = 0x0008000000ULL;
    inline constexpr std::uint64_t IS_LINEAR        = 0x0010000000ULL;
    inline constexpr std::uint64_t IS_BORROWED      = 0x0020000000ULL;
    inline constexpr std::uint64_t MAY_DEADLOCK     = 0x0040000000ULL;
    inline constexpr std::uint64_t REQUIRES_FENCE   = 0x0080000000ULL;

    inline constexpr std::uint64_t IS_CONCURRENT    = 0x0100000000ULL;
    inline constexpr std::uint64_t IS_SPAWN         = 0x0200000000ULL;
    inline constexpr std::uint64_t IS_FIBER         = 0x0400000000ULL;
    inline constexpr std::uint64_t IS_ASYNC         = 0x0800000000ULL;
    inline constexpr std::uint64_t REQUIRES_SYNC_OP = 0x1000000000ULL;
    inline constexpr std::uint64_t BARRIER_POINT    = 0x2000000000ULL;
    inline constexpr std::uint64_t CHANNEL_OP       = 0x4000000000ULL;
    inline constexpr std::uint64_t LOCK_OP          = 0x8000000000ULL;

    inline constexpr std::uint64_t IS_GRAPH_OP      = 0x10000000000ULL;
    inline constexpr std::uint64_t IS_TREE_OP       = 0x20000000000ULL;
    inline constexpr std::uint64_t IS_TRIE_OP       = 0x40000000000ULL;
    inline constexpr std::uint64_t IS_TENSOR_OP     = 0x80000000000ULL;
    inline constexpr std::uint64_t HAS_SCOP         = 0x100000000000ULL;

    inline constexpr std::uint64_t LANG_DSL         = 0x200000000000ULL;
    inline constexpr std::uint64_t LANG_CPP         = 0x400000000000ULL;
    inline constexpr std::uint64_t LANG_RUST        = 0x800000000000ULL;
} // namespace ExtendedFlags

namespace AuxFlags {
    inline constexpr std::uint32_t NONE = 0x00000000u;

    inline constexpr std::uint32_t PTR_ADDRSPACE_GENERIC  = 0x00000001u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_GLOBAL   = 0x00000002u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_SHARED   = 0x00000004u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_LOCAL    = 0x00000008u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_CONSTANT = 0x00000010u;

    inline constexpr std::uint32_t PTR_ADDRSPACE_MASK =
        PTR_ADDRSPACE_GENERIC |
        PTR_ADDRSPACE_GLOBAL |
        PTR_ADDRSPACE_SHARED |
        PTR_ADDRSPACE_LOCAL |
        PTR_ADDRSPACE_CONSTANT;

    inline constexpr std::uint32_t PREDICATE_CANONICAL = 0x00000020u;

    static_assert((PTR_ADDRSPACE_MASK & PREDICATE_CANONICAL) == 0u,
                  "AuxFlags domains must not overlap");
} // namespace AuxFlags

namespace PhaseDFlags {
    inline constexpr std::uint32_t IS_VARIADIC_OP     = 0x00000001u;
    inline constexpr std::uint32_t HAS_MULTI_RESULT   = 0x00000002u;
    inline constexpr std::uint32_t EFFECT_TRACKED    = 0x00000004u;
    inline constexpr std::uint32_t DOMINANCE_ROOT    = 0x00000008u;

    inline constexpr std::uint32_t TYPE_VERIFIED      = 0x00000010u;
    inline constexpr std::uint32_t EFFECT_VERIFIED    = 0x00000020u;
    inline constexpr std::uint32_t DOMINANCE_VERIFIED = 0x00000040u;
} // namespace PhaseDFlags

namespace HintFlags {
    inline constexpr std::uint16_t LatencySensitive     = 0x2000u;
    inline constexpr std::uint16_t ThroughputMode       = 0x4000u;

    inline constexpr std::uint16_t RequiresVecPort      = 0x0002u;
    inline constexpr std::uint16_t SVEActive            = 0x0004u;
    inline constexpr std::uint16_t IsSpillCandidate     = 0x0008u;

    inline constexpr std::uint16_t BranchLikely         = 0x1000u;
    inline constexpr std::uint16_t BranchUnlikely       = 0x2000u;

    inline constexpr std::uint16_t IsHosted             = 0x0040u;
    inline constexpr std::uint16_t RequiresDepassTarget = 0x0100u;
    inline constexpr std::uint16_t NonTemporalHint      = 0x0800u;
} // namespace HintFlags

inline constexpr std::uint32_t NULL_REGISTER = 0x0u;
inline constexpr std::uint32_t INVALID_ATOM  = 0xFFFFFFFFu;
inline constexpr std::uint32_t INVALID_SLOT  = INVALID_ATOM;

enum class OwnershipModel : std::uint8_t {
    OWNED = 0,
    SHARED = 1,
    CONST = 2,
    ATOMIC = 3,
    ISOLATED = 4,
    MUT = 5,
    AUTO = 6,
    VOLATILE = 7,
    LINEAR = 8,
    MANAGED = 9,
    OPAQUE = 10,
    SEALED = 11,
    CAP = 12
};

enum class ConcurrencyClass : std::uint8_t {
    SEQUENTIAL = 0,
    SPAWN = 1,
    ASYNC = 2,
    FIBER = 3,
    ELASTIC = 4,
    THREAD = 5,
    SYNC = 6,
    AWAIT_READY = 7,
    JOIN_POINT = 8,
    LOCK_OP = 9,
    FENCE = 10,
    RECV_OP = 11,
    SEND_OP = 12,
    BARRIER = 13
};

enum class DSAType : std::uint8_t {
    SCALAR = 0,
    TENSOR = 1,
    GRAPH = 2,
    TREE = 3,
    TRIE = 4,
    NODE = 5,
    EDGE = 6,
    STREAM = 7
};

enum class FrontendLanguage : std::uint8_t {
    UNIVERSAL = 0,
    DSL = 1,
    CPP = 2,
    RUST = 3,
    PYTHON = 4,
    GO = 5,
    SWIFT = 6,
    KOTLIN = 7,
    JAVA = 8,
    C = 9
};

struct OwnershipLifetime {
    OwnershipModel model         = OwnershipModel::OWNED;
    std::uint32_t acquire_site   = 0u;
    std::uint32_t release_site   = 0u;
    std::uint32_t transfer_count = 0u;
    std::uint32_t last_transfer_site = 0u;
    bool is_borrowed             = false;
    bool has_aliasing            = false;
};

struct TypeContext {
    FrontendLanguage language         = FrontendLanguage::UNIVERSAL;
    OwnershipModel default_ownership  = OwnershipModel::OWNED;
    ConcurrencyClass concurrency_class = ConcurrencyClass::SEQUENTIAL;
    DSAType primary_dsa               = DSAType::SCALAR;
    bool is_value_type                = false;
    bool is_generic                   = false;
    std::uint32_t generic_params      = 0u;
    std::uint64_t type_id             = 0ULL;
};


inline constexpr std::uint16_t kMaxOpcode = []() constexpr {
    std::uint16_t value = 0;
    if (value < 0x0) value = 0x0;
    if (value < 0x59) value = 0x59;
    if (value < 0xA) value = 0xA;
    if (value < 0xB) value = 0xB;
    if (value < 0x10) value = 0x10;
    if (value < 0x11) value = 0x11;
    if (value < 0x12) value = 0x12;
    if (value < 0x51) value = 0x51;
    if (value < 0x52) value = 0x52;
    if (value < 0x5A) value = 0x5A;
    if (value < 0x5B) value = 0x5B;
    if (value < 0x5C) value = 0x5C;
    if (value < 0x53) value = 0x53;
    if (value < 0xC) value = 0xC;
    if (value < 0xD) value = 0xD;
    if (value < 0xE) value = 0xE;
    if (value < 0xF) value = 0xF;
    if (value < 0x50) value = 0x50;
    if (value < 0x54) value = 0x54;
    if (value < 0x55) value = 0x55;
    if (value < 0x56) value = 0x56;
    if (value < 0x57) value = 0x57;
    if (value < 0x58) value = 0x58;
    if (value < 0x13) value = 0x13;
    if (value < 0x14) value = 0x14;
    if (value < 0x15) value = 0x15;
    if (value < 0x16) value = 0x16;
    if (value < 0x17) value = 0x17;
    if (value < 0x18) value = 0x18;
    if (value < 0x19) value = 0x19;
    if (value < 0x1A) value = 0x1A;
    if (value < 0x1B) value = 0x1B;
    if (value < 0x1C) value = 0x1C;
    if (value < 0x1D) value = 0x1D;
    if (value < 0x3C) value = 0x3C;
    if (value < 0x3D) value = 0x3D;
    if (value < 0x3E) value = 0x3E;
    if (value < 0x3F) value = 0x3F;
    if (value < 0x40) value = 0x40;
    if (value < 0x82) value = 0x82;
    if (value < 0x83) value = 0x83;
    if (value < 0x84) value = 0x84;
    if (value < 0x85) value = 0x85;
    if (value < 0x1E) value = 0x1E;
    if (value < 0x1F) value = 0x1F;
    if (value < 0x20) value = 0x20;
    if (value < 0x21) value = 0x21;
    if (value < 0x23) value = 0x23;
    if (value < 0x24) value = 0x24;
    if (value < 0x46) value = 0x46;
    if (value < 0x47) value = 0x47;
    if (value < 0x48) value = 0x48;
    if (value < 0x49) value = 0x49;
    if (value < 0x28) value = 0x28;
    if (value < 0x29) value = 0x29;
    if (value < 0x2A) value = 0x2A;
    if (value < 0x2B) value = 0x2B;
    if (value < 0x2C) value = 0x2C;
    if (value < 0x2D) value = 0x2D;
    if (value < 0x32) value = 0x32;
    if (value < 0x33) value = 0x33;
    if (value < 0x62) value = 0x62;
    if (value < 0x63) value = 0x63;
    if (value < 0x64) value = 0x64;
    if (value < 0x66) value = 0x66;
    if (value < 0x67) value = 0x67;
    if (value < 0x6E) value = 0x6E;
    if (value < 0x6F) value = 0x6F;
    if (value < 0x78) value = 0x78;
    if (value < 0x65) value = 0x65;
    if (value < 0xCA) value = 0xCA;
    if (value < 0xCC) value = 0xCC;
    if (value < 0xC8) value = 0xC8;
    if (value < 0xC9) value = 0xC9;
    if (value < 0xCB) value = 0xCB;
    if (value < 0x258) value = 0x258;
    if (value < 0x259) value = 0x259;
    if (value < 0x25A) value = 0x25A;
    if (value < 0x500) value = 0x500;
    if (value < 0x501) value = 0x501;
    if (value < 0x502) value = 0x502;
    if (value < 0x503) value = 0x503;
    if (value < 0x504) value = 0x504;
    if (value < 0x507) value = 0x507;
    if (value < 0x508) value = 0x508;
    if (value < 0x509) value = 0x509;
    if (value < 0x510) value = 0x510;
    if (value < 0x511) value = 0x511;
    if (value < 0x512) value = 0x512;
    if (value < 0x513) value = 0x513;
    if (value < 0x8010) value = 0x8010;
    if (value < 0x8011) value = 0x8011;
    if (value < 0x8012) value = 0x8012;
    if (value < 0x8013) value = 0x8013;
    if (value < 0x8014) value = 0x8014;
    if (value < 0x8015) value = 0x8015;
    if (value < 0x190) value = 0x190;
    if (value < 0x19A) value = 0x19A;
    if (value < 0x19B) value = 0x19B;
    if (value < 0x19C) value = 0x19C;
    if (value < 0x19D) value = 0x19D;
    if (value < 0x8020) value = 0x8020;
    if (value < 0x8021) value = 0x8021;
    if (value < 0x8022) value = 0x8022;
    if (value < 0x8023) value = 0x8023;
    if (value < 0x8024) value = 0x8024;
    if (value < 0x1A4) value = 0x1A4;
    if (value < 0x1A5) value = 0x1A5;
    if (value < 0x1A6) value = 0x1A6;
    if (value < 0x1A7) value = 0x1A7;
    if (value < 0x1A8) value = 0x1A8;
    if (value < 0x1A9) value = 0x1A9;
    if (value < 0x1AA) value = 0x1AA;
    if (value < 0x1AB) value = 0x1AB;
    if (value < 0x1AC) value = 0x1AC;
    if (value < 0x1AD) value = 0x1AD;
    if (value < 0x410) value = 0x410;
    if (value < 0x411) value = 0x411;
    if (value < 0x412) value = 0x412;
    if (value < 0x413) value = 0x413;
    if (value < 0x414) value = 0x414;
    if (value < 0x1B8) value = 0x1B8;
    if (value < 0x1B9) value = 0x1B9;
    if (value < 0x1BA) value = 0x1BA;
    if (value < 0x1BB) value = 0x1BB;
    if (value < 0x1BC) value = 0x1BC;
    if (value < 0x1BD) value = 0x1BD;
    if (value < 0x1BE) value = 0x1BE;
    if (value < 0x1BF) value = 0x1BF;
    if (value < 0x1C0) value = 0x1C0;
    if (value < 0x1C1) value = 0x1C1;
    if (value < 0x1CC) value = 0x1CC;
    if (value < 0x1CD) value = 0x1CD;
    if (value < 0x1CE) value = 0x1CE;
    if (value < 0x1CF) value = 0x1CF;
    if (value < 0x1D0) value = 0x1D0;
    if (value < 0x1D1) value = 0x1D1;
    if (value < 0x1D2) value = 0x1D2;
    if (value < 0x1D3) value = 0x1D3;
    if (value < 0x1D4) value = 0x1D4;
    if (value < 0x1D5) value = 0x1D5;
    if (value < 0x8030) value = 0x8030;
    if (value < 0x8031) value = 0x8031;
    if (value < 0x8032) value = 0x8032;
    if (value < 0x8033) value = 0x8033;
    if (value < 0x8001) value = 0x8001;
    if (value < 0x8002) value = 0x8002;
    if (value < 0x8101) value = 0x8101;
    if (value < 0x8102) value = 0x8102;
    if (value < 0x200) value = 0x200;
    if (value < 0x201) value = 0x201;
    if (value < 0x202) value = 0x202;
    if (value < 0x203) value = 0x203;
    if (value < 0x204) value = 0x204;
    if (value < 0x205) value = 0x205;
    if (value < 0x206) value = 0x206;
    if (value < 0x207) value = 0x207;
    if (value < 0x208) value = 0x208;
    if (value < 0x209) value = 0x209;
    if (value < 0x600) value = 0x600;
    if (value < 0x601) value = 0x601;
    if (value < 0x602) value = 0x602;
    if (value < 0x603) value = 0x603;
    if (value < 0x604) value = 0x604;
    if (value < 0x605) value = 0x605;
    if (value < 0x606) value = 0x606;
    if (value < 0x480) value = 0x480;
    if (value < 0x481) value = 0x481;
    if (value < 0x482) value = 0x482;
    if (value < 0x483) value = 0x483;
    if (value < 0x484) value = 0x484;
    if (value < 0x485) value = 0x485;
    if (value < 0x700) value = 0x700;
    if (value < 0x701) value = 0x701;
    if (value < 0x702) value = 0x702;
    if (value < 0x703) value = 0x703;
    if (value < 0x704) value = 0x704;
    if (value < 0x705) value = 0x705;
    if (value < 0x41) value = 0x41;
    if (value < 0x42) value = 0x42;
    if (value < 0x43) value = 0x43;
    if (value < 0x44) value = 0x44;
    if (value < 0x45) value = 0x45;
    return value;
}();
inline constexpr std::uint16_t kMaxTypeCode = 30;
inline constexpr std::size_t kCanonicalOpCount = 183;

[[nodiscard]] constexpr std::uint16_t to_underlying(OpCode op) noexcept {
    return static_cast<std::uint16_t>(op);
}
[[nodiscard]] constexpr std::uint16_t to_underlying(TypeCode type) noexcept {
    return static_cast<std::uint16_t>(type);
}
[[nodiscard]] constexpr bool is_valid_type_code(TypeCode type) noexcept {
    return to_underlying(type) <= kMaxTypeCode;
}

static_assert(static_cast<std::uint16_t>(OpCode::NOP) == 0x0, "DGTL stable opcode ID drift: NOP");
static_assert(static_cast<std::uint16_t>(OpCode::UNDEF) == 0x59, "DGTL stable opcode ID drift: UNDEF");
static_assert(static_cast<std::uint16_t>(OpCode::LOAD) == 0xA, "DGTL stable opcode ID drift: LOAD");
static_assert(static_cast<std::uint16_t>(OpCode::STORE) == 0xB, "DGTL stable opcode ID drift: STORE");
static_assert(static_cast<std::uint16_t>(OpCode::ATOMIC_LOAD) == 0x10, "DGTL stable opcode ID drift: ATOMIC_LOAD");
static_assert(static_cast<std::uint16_t>(OpCode::ATOMIC_STORE) == 0x11, "DGTL stable opcode ID drift: ATOMIC_STORE");
static_assert(static_cast<std::uint16_t>(OpCode::ATOMIC_FENCE) == 0x12, "DGTL stable opcode ID drift: ATOMIC_FENCE");
static_assert(static_cast<std::uint16_t>(OpCode::PTR_CAST) == 0x51, "DGTL stable opcode ID drift: PTR_CAST");
static_assert(static_cast<std::uint16_t>(OpCode::PTR_DIFF) == 0x52, "DGTL stable opcode ID drift: PTR_DIFF");
static_assert(static_cast<std::uint16_t>(OpCode::GEP) == 0x5A, "DGTL stable opcode ID drift: GEP");
static_assert(static_cast<std::uint16_t>(OpCode::PTR_TO_INT) == 0x5B, "DGTL stable opcode ID drift: PTR_TO_INT");
static_assert(static_cast<std::uint16_t>(OpCode::INT_TO_PTR) == 0x5C, "DGTL stable opcode ID drift: INT_TO_PTR");
static_assert(static_cast<std::uint16_t>(OpCode::BOOL_SELECT) == 0x53, "DGTL stable opcode ID drift: BOOL_SELECT");
static_assert(static_cast<std::uint16_t>(OpCode::REG_MOVE) == 0xC, "DGTL stable opcode ID drift: REG_MOVE");
static_assert(static_cast<std::uint16_t>(OpCode::LOAD_IMM) == 0xD, "DGTL stable opcode ID drift: LOAD_IMM");
static_assert(static_cast<std::uint16_t>(OpCode::LEA) == 0xE, "DGTL stable opcode ID drift: LEA");
static_assert(static_cast<std::uint16_t>(OpCode::PREFETCH) == 0xF, "DGTL stable opcode ID drift: PREFETCH");
static_assert(static_cast<std::uint16_t>(OpCode::IMMEDIATE) == 0x50, "DGTL stable opcode ID drift: IMMEDIATE");
static_assert(static_cast<std::uint16_t>(OpCode::STRING_CONST) == 0x54, "DGTL stable opcode ID drift: STRING_CONST");
static_assert(static_cast<std::uint16_t>(OpCode::STRING_LENGTH) == 0x55, "DGTL stable opcode ID drift: STRING_LENGTH");
static_assert(static_cast<std::uint16_t>(OpCode::STRING_COMPARE) == 0x56, "DGTL stable opcode ID drift: STRING_COMPARE");
static_assert(static_cast<std::uint16_t>(OpCode::STRING_CONCAT) == 0x57, "DGTL stable opcode ID drift: STRING_CONCAT");
static_assert(static_cast<std::uint16_t>(OpCode::STRING_SUBSTRING) == 0x58, "DGTL stable opcode ID drift: STRING_SUBSTRING");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_I8) == 0x13, "DGTL stable opcode ID drift: ADD_I8");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_I32) == 0x14, "DGTL stable opcode ID drift: ADD_I32");
static_assert(static_cast<std::uint16_t>(OpCode::SUB_I32) == 0x15, "DGTL stable opcode ID drift: SUB_I32");
static_assert(static_cast<std::uint16_t>(OpCode::MUL_I32) == 0x16, "DGTL stable opcode ID drift: MUL_I32");
static_assert(static_cast<std::uint16_t>(OpCode::DIV_I32) == 0x17, "DGTL stable opcode ID drift: DIV_I32");
static_assert(static_cast<std::uint16_t>(OpCode::SHL_I32) == 0x18, "DGTL stable opcode ID drift: SHL_I32");
static_assert(static_cast<std::uint16_t>(OpCode::AND_I32) == 0x19, "DGTL stable opcode ID drift: AND_I32");
static_assert(static_cast<std::uint16_t>(OpCode::OR_I32) == 0x1A, "DGTL stable opcode ID drift: OR_I32");
static_assert(static_cast<std::uint16_t>(OpCode::XOR_I32) == 0x1B, "DGTL stable opcode ID drift: XOR_I32");
static_assert(static_cast<std::uint16_t>(OpCode::MOD_I32) == 0x1C, "DGTL stable opcode ID drift: MOD_I32");
static_assert(static_cast<std::uint16_t>(OpCode::SHR_I32) == 0x1D, "DGTL stable opcode ID drift: SHR_I32");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_I64) == 0x3C, "DGTL stable opcode ID drift: ADD_I64");
static_assert(static_cast<std::uint16_t>(OpCode::SUB_I64) == 0x3D, "DGTL stable opcode ID drift: SUB_I64");
static_assert(static_cast<std::uint16_t>(OpCode::MUL_I64) == 0x3E, "DGTL stable opcode ID drift: MUL_I64");
static_assert(static_cast<std::uint16_t>(OpCode::DIV_I64) == 0x3F, "DGTL stable opcode ID drift: DIV_I64");
static_assert(static_cast<std::uint16_t>(OpCode::MOD_I64) == 0x40, "DGTL stable opcode ID drift: MOD_I64");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_I128) == 0x82, "DGTL stable opcode ID drift: ADD_I128");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_I256) == 0x83, "DGTL stable opcode ID drift: ADD_I256");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_F8) == 0x84, "DGTL stable opcode ID drift: ADD_F8");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_BF16) == 0x85, "DGTL stable opcode ID drift: ADD_BF16");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_F32) == 0x1E, "DGTL stable opcode ID drift: ADD_F32");
static_assert(static_cast<std::uint16_t>(OpCode::SUB_F32) == 0x1F, "DGTL stable opcode ID drift: SUB_F32");
static_assert(static_cast<std::uint16_t>(OpCode::MUL_F32) == 0x20, "DGTL stable opcode ID drift: MUL_F32");
static_assert(static_cast<std::uint16_t>(OpCode::DIV_F32) == 0x21, "DGTL stable opcode ID drift: DIV_F32");
static_assert(static_cast<std::uint16_t>(OpCode::FMA_F32) == 0x23, "DGTL stable opcode ID drift: FMA_F32");
static_assert(static_cast<std::uint16_t>(OpCode::FMA_WIDEN_F16_F32) == 0x24, "DGTL stable opcode ID drift: FMA_WIDEN_F16_F32");
static_assert(static_cast<std::uint16_t>(OpCode::ADD_F64) == 0x46, "DGTL stable opcode ID drift: ADD_F64");
static_assert(static_cast<std::uint16_t>(OpCode::SUB_F64) == 0x47, "DGTL stable opcode ID drift: SUB_F64");
static_assert(static_cast<std::uint16_t>(OpCode::MUL_F64) == 0x48, "DGTL stable opcode ID drift: MUL_F64");
static_assert(static_cast<std::uint16_t>(OpCode::DIV_F64) == 0x49, "DGTL stable opcode ID drift: DIV_F64");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_EQ_I32) == 0x28, "DGTL stable opcode ID drift: CMP_EQ_I32");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_NE_I32) == 0x29, "DGTL stable opcode ID drift: CMP_NE_I32");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_LT_I32) == 0x2A, "DGTL stable opcode ID drift: CMP_LT_I32");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_LE_I32) == 0x2B, "DGTL stable opcode ID drift: CMP_LE_I32");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_GT_I32) == 0x2C, "DGTL stable opcode ID drift: CMP_GT_I32");
static_assert(static_cast<std::uint16_t>(OpCode::CMP_GE_I32) == 0x2D, "DGTL stable opcode ID drift: CMP_GE_I32");
static_assert(static_cast<std::uint16_t>(OpCode::BIT_FIELD_INSERT) == 0x32, "DGTL stable opcode ID drift: BIT_FIELD_INSERT");
static_assert(static_cast<std::uint16_t>(OpCode::BIT_FIELD_EXTRACT) == 0x33, "DGTL stable opcode ID drift: BIT_FIELD_EXTRACT");
static_assert(static_cast<std::uint16_t>(OpCode::LABEL) == 0x62, "DGTL stable opcode ID drift: LABEL");
static_assert(static_cast<std::uint16_t>(OpCode::GOTO) == 0x63, "DGTL stable opcode ID drift: GOTO");
static_assert(static_cast<std::uint16_t>(OpCode::BRANCH) == 0x64, "DGTL stable opcode ID drift: BRANCH");
static_assert(static_cast<std::uint16_t>(OpCode::BRANCH_COND) == 0x66, "DGTL stable opcode ID drift: BRANCH_COND");
static_assert(static_cast<std::uint16_t>(OpCode::BRANCH_COMPARE_FUSED) == 0x67, "DGTL stable opcode ID drift: BRANCH_COMPARE_FUSED");
static_assert(static_cast<std::uint16_t>(OpCode::CALL) == 0x6E, "DGTL stable opcode ID drift: CALL");
static_assert(static_cast<std::uint16_t>(OpCode::SYSCALL) == 0x6F, "DGTL stable opcode ID drift: SYSCALL");
static_assert(static_cast<std::uint16_t>(OpCode::GUARD_ALIAS) == 0x78, "DGTL stable opcode ID drift: GUARD_ALIAS");
static_assert(static_cast<std::uint16_t>(OpCode::RET) == 0x65, "DGTL stable opcode ID drift: RET");
static_assert(static_cast<std::uint16_t>(OpCode::PHI_NODE) == 0xCA, "DGTL stable opcode ID drift: PHI_NODE");
static_assert(static_cast<std::uint16_t>(OpCode::SELECT) == 0xCC, "DGTL stable opcode ID drift: SELECT");
static_assert(static_cast<std::uint16_t>(OpCode::GUARD_VALUE) == 0xC8, "DGTL stable opcode ID drift: GUARD_VALUE");
static_assert(static_cast<std::uint16_t>(OpCode::DEOPT_TRIGGER) == 0xC9, "DGTL stable opcode ID drift: DEOPT_TRIGGER");
static_assert(static_cast<std::uint16_t>(OpCode::ASSUME) == 0xCB, "DGTL stable opcode ID drift: ASSUME");
static_assert(static_cast<std::uint16_t>(OpCode::REGION_BEGIN) == 0x258, "DGTL stable opcode ID drift: REGION_BEGIN");
static_assert(static_cast<std::uint16_t>(OpCode::REGION_END) == 0x259, "DGTL stable opcode ID drift: REGION_END");
static_assert(static_cast<std::uint16_t>(OpCode::FUSION_GROUP) == 0x25A, "DGTL stable opcode ID drift: FUSION_GROUP");
static_assert(static_cast<std::uint16_t>(OpCode::GATHER_F32_512) == 0x500, "DGTL stable opcode ID drift: GATHER_F32_512");
static_assert(static_cast<std::uint16_t>(OpCode::GATHER_16) == 0x501, "DGTL stable opcode ID drift: GATHER_16");
static_assert(static_cast<std::uint16_t>(OpCode::STORE_V512) == 0x502, "DGTL stable opcode ID drift: STORE_V512");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_ADD_F32_512) == 0x503, "DGTL stable opcode ID drift: VEC_ADD_F32_512");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_MUL_F32_512) == 0x504, "DGTL stable opcode ID drift: VEC_MUL_F32_512");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_BSL_PREDICATE) == 0x507, "DGTL stable opcode ID drift: SIMD_BSL_PREDICATE");
static_assert(static_cast<std::uint16_t>(OpCode::FCVTL_F16_TO_F32) == 0x508, "DGTL stable opcode ID drift: FCVTL_F16_TO_F32");
static_assert(static_cast<std::uint16_t>(OpCode::FCVTN_F32_TO_F16) == 0x509, "DGTL stable opcode ID drift: FCVTN_F32_TO_F16");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_LOAD) == 0x510, "DGTL stable opcode ID drift: VEC_LOAD");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_STORE) == 0x511, "DGTL stable opcode ID drift: VEC_STORE");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_SHUFFLE) == 0x512, "DGTL stable opcode ID drift: VEC_SHUFFLE");
static_assert(static_cast<std::uint16_t>(OpCode::VEC_REDUCE_ADD) == 0x513, "DGTL stable opcode ID drift: VEC_REDUCE_ADD");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_REDUCE_MAX) == 0x8010, "DGTL stable opcode ID drift: SIMD_REDUCE_MAX");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_REDUCE_MIN) == 0x8011, "DGTL stable opcode ID drift: SIMD_REDUCE_MIN");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_SCATTER_INDIRECT) == 0x8012, "DGTL stable opcode ID drift: SIMD_SCATTER_INDIRECT");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_MASKED_STORE) == 0x8013, "DGTL stable opcode ID drift: SIMD_MASKED_STORE");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_INTERLEAVED_LOAD) == 0x8014, "DGTL stable opcode ID drift: SIMD_INTERLEAVED_LOAD");
static_assert(static_cast<std::uint16_t>(OpCode::SIMD_COMPLEX_FMA) == 0x8015, "DGTL stable opcode ID drift: SIMD_COMPLEX_FMA");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_LAUNCH) == 0x190, "DGTL stable opcode ID drift: GPU_LAUNCH");
static_assert(static_cast<std::uint16_t>(OpCode::OWNERSHIP_ACQUIRE) == 0x19A, "DGTL stable opcode ID drift: OWNERSHIP_ACQUIRE");
static_assert(static_cast<std::uint16_t>(OpCode::OWNERSHIP_RELEASE) == 0x19B, "DGTL stable opcode ID drift: OWNERSHIP_RELEASE");
static_assert(static_cast<std::uint16_t>(OpCode::OWNERSHIP_TRANSFER) == 0x19C, "DGTL stable opcode ID drift: OWNERSHIP_TRANSFER");
static_assert(static_cast<std::uint16_t>(OpCode::ATOMIC_SNAPSHOT) == 0x19D, "DGTL stable opcode ID drift: ATOMIC_SNAPSHOT");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_MEM_TRANSFER) == 0x8020, "DGTL stable opcode ID drift: GPU_MEM_TRANSFER");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_BARRIER) == 0x8021, "DGTL stable opcode ID drift: GPU_BARRIER");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_SYNC_POINT) == 0x8022, "DGTL stable opcode ID drift: GPU_SYNC_POINT");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_KERNEL_FUSE) == 0x8023, "DGTL stable opcode ID drift: GPU_KERNEL_FUSE");
static_assert(static_cast<std::uint16_t>(OpCode::GPU_ANNOTATE_DEVICE) == 0x8024, "DGTL stable opcode ID drift: GPU_ANNOTATE_DEVICE");
static_assert(static_cast<std::uint16_t>(OpCode::ASYNC_SPAWN) == 0x1A4, "DGTL stable opcode ID drift: ASYNC_SPAWN");
static_assert(static_cast<std::uint16_t>(OpCode::ASYNC_AWAIT) == 0x1A5, "DGTL stable opcode ID drift: ASYNC_AWAIT");
static_assert(static_cast<std::uint16_t>(OpCode::FIBER_YIELD) == 0x1A6, "DGTL stable opcode ID drift: FIBER_YIELD");
static_assert(static_cast<std::uint16_t>(OpCode::BARRIER_SYNC) == 0x1A7, "DGTL stable opcode ID drift: BARRIER_SYNC");
static_assert(static_cast<std::uint16_t>(OpCode::CHAN_SEND) == 0x1A8, "DGTL stable opcode ID drift: CHAN_SEND");
static_assert(static_cast<std::uint16_t>(OpCode::CHAN_RECV) == 0x1A9, "DGTL stable opcode ID drift: CHAN_RECV");
static_assert(static_cast<std::uint16_t>(OpCode::LOCK_ACQUIRE) == 0x1AA, "DGTL stable opcode ID drift: LOCK_ACQUIRE");
static_assert(static_cast<std::uint16_t>(OpCode::LOCK_RELEASE) == 0x1AB, "DGTL stable opcode ID drift: LOCK_RELEASE");
static_assert(static_cast<std::uint16_t>(OpCode::MEMORY_FENCE) == 0x1AC, "DGTL stable opcode ID drift: MEMORY_FENCE");
static_assert(static_cast<std::uint16_t>(OpCode::THREAD_SPAWN) == 0x1AD, "DGTL stable opcode ID drift: THREAD_SPAWN");
static_assert(static_cast<std::uint16_t>(OpCode::WASM_INVOKE) == 0x410, "DGTL stable opcode ID drift: WASM_INVOKE");
static_assert(static_cast<std::uint16_t>(OpCode::FIBER_YIELD_LOOP) == 0x411, "DGTL stable opcode ID drift: FIBER_YIELD_LOOP");
static_assert(static_cast<std::uint16_t>(OpCode::SUSPEND_CTX) == 0x412, "DGTL stable opcode ID drift: SUSPEND_CTX");
static_assert(static_cast<std::uint16_t>(OpCode::RESUME_CTX) == 0x413, "DGTL stable opcode ID drift: RESUME_CTX");
static_assert(static_cast<std::uint16_t>(OpCode::NEON_VADD_Q) == 0x414, "DGTL stable opcode ID drift: NEON_VADD_Q");
static_assert(static_cast<std::uint16_t>(OpCode::GRAPH_BUILD) == 0x1B8, "DGTL stable opcode ID drift: GRAPH_BUILD");
static_assert(static_cast<std::uint16_t>(OpCode::GRAPH_TRAVERSE) == 0x1B9, "DGTL stable opcode ID drift: GRAPH_TRAVERSE");
static_assert(static_cast<std::uint16_t>(OpCode::TREE_BUILD) == 0x1BA, "DGTL stable opcode ID drift: TREE_BUILD");
static_assert(static_cast<std::uint16_t>(OpCode::TREE_TRAVERSE) == 0x1BB, "DGTL stable opcode ID drift: TREE_TRAVERSE");
static_assert(static_cast<std::uint16_t>(OpCode::TRIE_BUILD) == 0x1BC, "DGTL stable opcode ID drift: TRIE_BUILD");
static_assert(static_cast<std::uint16_t>(OpCode::TRIE_LOOKUP) == 0x1BD, "DGTL stable opcode ID drift: TRIE_LOOKUP");
static_assert(static_cast<std::uint16_t>(OpCode::TRIE_INSERT) == 0x1BE, "DGTL stable opcode ID drift: TRIE_INSERT");
static_assert(static_cast<std::uint16_t>(OpCode::DSA_ALLOCATE) == 0x1BF, "DGTL stable opcode ID drift: DSA_ALLOCATE");
static_assert(static_cast<std::uint16_t>(OpCode::DSA_DEALLOCATE) == 0x1C0, "DGTL stable opcode ID drift: DSA_DEALLOCATE");
static_assert(static_cast<std::uint16_t>(OpCode::SCOP_ANALYZE) == 0x1C1, "DGTL stable opcode ID drift: SCOP_ANALYZE");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_RESHAPE) == 0x1CC, "DGTL stable opcode ID drift: TENSOR_RESHAPE");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_REDUCE) == 0x1CD, "DGTL stable opcode ID drift: TENSOR_REDUCE");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_TRANSPOSE) == 0x1CE, "DGTL stable opcode ID drift: TENSOR_TRANSPOSE");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_SLICE) == 0x1CF, "DGTL stable opcode ID drift: TENSOR_SLICE");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_CONCAT) == 0x1D0, "DGTL stable opcode ID drift: TENSOR_CONCAT");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_BROADCAST) == 0x1D1, "DGTL stable opcode ID drift: TENSOR_BROADCAST");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_GEMM) == 0x1D2, "DGTL stable opcode ID drift: TENSOR_GEMM");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_CONV) == 0x1D3, "DGTL stable opcode ID drift: TENSOR_CONV");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_POOL) == 0x1D4, "DGTL stable opcode ID drift: TENSOR_POOL");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_SOFTMAX) == 0x1D5, "DGTL stable opcode ID drift: TENSOR_SOFTMAX");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_RELU) == 0x8030, "DGTL stable opcode ID drift: TENSOR_RELU");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_BATCHNORM) == 0x8031, "DGTL stable opcode ID drift: TENSOR_BATCHNORM");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_GATHER) == 0x8032, "DGTL stable opcode ID drift: TENSOR_GATHER");
static_assert(static_cast<std::uint16_t>(OpCode::TENSOR_SCATTER_D) == 0x8033, "DGTL stable opcode ID drift: TENSOR_SCATTER_D");
static_assert(static_cast<std::uint16_t>(OpCode::DGTL_FAST_ADD) == 0x8001, "DGTL stable opcode ID drift: DGTL_FAST_ADD");
static_assert(static_cast<std::uint16_t>(OpCode::DGTL_FAST_FMA) == 0x8002, "DGTL stable opcode ID drift: DGTL_FAST_FMA");
static_assert(static_cast<std::uint16_t>(OpCode::CPP_TEMPLATE_SPEC) == 0x8101, "DGTL stable opcode ID drift: CPP_TEMPLATE_SPEC");
static_assert(static_cast<std::uint16_t>(OpCode::RUST_BORROW_MUT) == 0x8102, "DGTL stable opcode ID drift: RUST_BORROW_MUT");
static_assert(static_cast<std::uint16_t>(OpCode::PHI_MULTI) == 0x200, "DGTL stable opcode ID drift: PHI_MULTI");
static_assert(static_cast<std::uint16_t>(OpCode::TUPLE_PACK) == 0x201, "DGTL stable opcode ID drift: TUPLE_PACK");
static_assert(static_cast<std::uint16_t>(OpCode::TUPLE_UNPACK) == 0x202, "DGTL stable opcode ID drift: TUPLE_UNPACK");
static_assert(static_cast<std::uint16_t>(OpCode::SELECT_MULTI) == 0x203, "DGTL stable opcode ID drift: SELECT_MULTI");
static_assert(static_cast<std::uint16_t>(OpCode::CALL_VARIADIC) == 0x204, "DGTL stable opcode ID drift: CALL_VARIADIC");
static_assert(static_cast<std::uint16_t>(OpCode::SWITCH_DISPATCH) == 0x205, "DGTL stable opcode ID drift: SWITCH_DISPATCH");
static_assert(static_cast<std::uint16_t>(OpCode::VOLATILE_LOAD) == 0x206, "DGTL stable opcode ID drift: VOLATILE_LOAD");
static_assert(static_cast<std::uint16_t>(OpCode::VOLATILE_STORE) == 0x207, "DGTL stable opcode ID drift: VOLATILE_STORE");
static_assert(static_cast<std::uint16_t>(OpCode::ACQUIRE_FENCE) == 0x208, "DGTL stable opcode ID drift: ACQUIRE_FENCE");
static_assert(static_cast<std::uint16_t>(OpCode::RELEASE_FENCE) == 0x209, "DGTL stable opcode ID drift: RELEASE_FENCE");
static_assert(static_cast<std::uint16_t>(OpCode::READ_TSC) == 0x600, "DGTL stable opcode ID drift: READ_TSC");
static_assert(static_cast<std::uint16_t>(OpCode::BRANCH_LIKELY) == 0x601, "DGTL stable opcode ID drift: BRANCH_LIKELY");
static_assert(static_cast<std::uint16_t>(OpCode::BRANCH_UNLIKELY) == 0x602, "DGTL stable opcode ID drift: BRANCH_UNLIKELY");
static_assert(static_cast<std::uint16_t>(OpCode::CLFLUSH) == 0x603, "DGTL stable opcode ID drift: CLFLUSH");
static_assert(static_cast<std::uint16_t>(OpCode::PREFETCH_T0) == 0x604, "DGTL stable opcode ID drift: PREFETCH_T0");
static_assert(static_cast<std::uint16_t>(OpCode::SPIN_ACQUIRE) == 0x605, "DGTL stable opcode ID drift: SPIN_ACQUIRE");
static_assert(static_cast<std::uint16_t>(OpCode::RING_PUSH_FREE) == 0x606, "DGTL stable opcode ID drift: RING_PUSH_FREE");
static_assert(static_cast<std::uint16_t>(OpCode::MATMUL_BF16) == 0x480, "DGTL stable opcode ID drift: MATMUL_BF16");
static_assert(static_cast<std::uint16_t>(OpCode::MATMUL_FP8) == 0x481, "DGTL stable opcode ID drift: MATMUL_FP8");
static_assert(static_cast<std::uint16_t>(OpCode::SDPA_F16) == 0x482, "DGTL stable opcode ID drift: SDPA_F16");
static_assert(static_cast<std::uint16_t>(OpCode::SOFTMAX_FAST) == 0x483, "DGTL stable opcode ID drift: SOFTMAX_FAST");
static_assert(static_cast<std::uint16_t>(OpCode::LAYER_NORM) == 0x484, "DGTL stable opcode ID drift: LAYER_NORM");
static_assert(static_cast<std::uint16_t>(OpCode::ROPE_EMBED) == 0x485, "DGTL stable opcode ID drift: ROPE_EMBED");
static_assert(static_cast<std::uint16_t>(OpCode::FAST_INV_SQRT) == 0x700, "DGTL stable opcode ID drift: FAST_INV_SQRT");
static_assert(static_cast<std::uint16_t>(OpCode::VEC3_CROSS) == 0x701, "DGTL stable opcode ID drift: VEC3_CROSS");
static_assert(static_cast<std::uint16_t>(OpCode::VEC4_DOT) == 0x702, "DGTL stable opcode ID drift: VEC4_DOT");
static_assert(static_cast<std::uint16_t>(OpCode::MAT4_MUL) == 0x703, "DGTL stable opcode ID drift: MAT4_MUL");
static_assert(static_cast<std::uint16_t>(OpCode::QUAT_SLERP) == 0x704, "DGTL stable opcode ID drift: QUAT_SLERP");
static_assert(static_cast<std::uint16_t>(OpCode::RAY_AABB_ISECT) == 0x705, "DGTL stable opcode ID drift: RAY_AABB_ISECT");
static_assert(static_cast<std::uint16_t>(OpCode::AND_I64) == 0x41, "DGTL stable opcode ID drift: AND_I64");
static_assert(static_cast<std::uint16_t>(OpCode::OR_I64) == 0x42, "DGTL stable opcode ID drift: OR_I64");
static_assert(static_cast<std::uint16_t>(OpCode::XOR_I64) == 0x43, "DGTL stable opcode ID drift: XOR_I64");
static_assert(static_cast<std::uint16_t>(OpCode::SHL_I64) == 0x44, "DGTL stable opcode ID drift: SHL_I64");
static_assert(static_cast<std::uint16_t>(OpCode::SHR_I64) == 0x45, "DGTL stable opcode ID drift: SHR_I64");

} // namespace dgtl::ir
