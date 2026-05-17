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

#pragma once

#include <cstdint>

#ifdef CONST
#undef CONST
#endif

#ifdef INVALID_ATOM
#undef INVALID_ATOM
#endif

#ifdef RELEASE
#undef RELEASE
#endif

#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif

#ifdef OPAQUE
#undef OPAQUE
#endif

#ifdef MOV
#undef MOV
#endif

namespace dgtl::ir {

enum class OpCode : uint16_t {

    // --- Core ---
    NOP = 0,

    // --- Memory ---
    LOAD = 10,
    STORE = 11,
    REG_MOVE = 12,
    MOV = 12,
    LOAD_IMM = 13,
    CONST = 13,
    LEA = 14,
    PREFETCH = 15,
    IMMEDIATE = 80,

    // --- Integer ---
    OP_ADD = 20,
    ADD_I32 = 20,
    SUB_I32 = 21,
    MUL_I32 = 22,
    DIV_I32 = 23,
    SHL_I32 = 24,
    AND_I32 = 25,
    OR_I32  = 26,
    XOR_I32 = 27,
    MOD_I32 = 28,
    SHR_I32 = 29,

    // --- Extended Integer (I64) ---
    ADD_I64 = 60,
    SUB_I64 = 61,
    MUL_I64 = 62,
    DIV_I64 = 63,
    MOD_I64 = 64,

    // --- Floating ---
    ADD_F32 = 30,
    SUB_F32 = 31,
    MUL_F32 = 32,
    DIV_F32 = 33,
    FMA_F32 = 35,
    FMA_WIDEN_F16_F32 = 36,

    // --- FP64 ---
    ADD_F64 = 70,
    SUB_F64 = 71,
    MUL_F64 = 72,
    DIV_F64 = 73,

    // --- Compare ---
    CMP_EQ_I32 = 40,
    CMP_NE_I32 = 41,
    CMP_LT_I32 = 42,
    CMP_LE_I32 = 43,
    CMP_GT_I32 = 44,
    CMP_GE_I32 = 45,

    // --- Bitfield ---
    BIT_FIELD_INSERT = 50,
    BIT_FIELD_EXTRACT = 51,

    // --- SIMD ---
    GATHER_F32_512 = 0x500,
    GATHER_16 = 0x501,
    STORE_V512 = 0x502,
    VEC_ADD_F32_512 = 0x503,
    VEC_MUL_F32_512 = 0x504,
    SIMD_BSL_PREDICATE = 0x507,
    FCVTL_F16_TO_F32 = 0x508,
    FCVTN_F32_TO_F16 = 0x509,

    // --- Generic SIMD ---
    VEC_LOAD = 0x510,
    VEC_STORE = 0x511,
    VEC_SHUFFLE = 0x512,
    VEC_REDUCE_ADD = 0x513,

    // --- Control ---
    LABEL = 98,
    GOTO = 99,
    BRANCH = 100,
    RET = 101,
    RETURN = 101,
    BRANCH_COND = 102,
    BRANCH_COMPARE_FUSED = 103,
    CALL = 110,
    SYSCALL = 111,
    GUARD_ALIAS = 120,

    // --- SSA / IR ---
    PHI_NODE = 202,

    // --- Speculation ---
    GUARD_VALUE = 200,
    DEOPT_TRIGGER = 201,
    ASSUME = 203,

    // --- Async (legacy aliases) ---
    ASYNC_LAUNCH = 300,
    SYNC_BARRIER = 301,

    // --- GPU ---
    GPU_LAUNCH = 400,

    // --- Ownership & Memory Safety ---
    OWNERSHIP_ACQUIRE = 410,
    OWNERSHIP_RELEASE = 411,
    OWNERSHIP_TRANSFER = 412,
    ATOMIC_SNAPSHOT = 413,

    // --- Concurrency Primitives ---
    ASYNC_SPAWN = 420,
    ASYNC_AWAIT = 421,
    FIBER_YIELD = 422,
    BARRIER_SYNC = 423,
    CHAN_SEND = 424,
    CHAN_RECV = 425,
    LOCK_ACQUIRE = 426,
    LOCK_RELEASE = 427,
    MEMORY_FENCE = 428,
    THREAD_SPAWN = 429,

    // --- DSA Operations ---
    GRAPH_BUILD = 440,
    GRAPH_TRAVERSE = 441,
    TREE_BUILD = 442,
    TREE_TRAVERSE = 443,
    TRIE_BUILD = 444,
    TRIE_LOOKUP = 445,
    TRIE_INSERT = 446,
    DSA_ALLOCATE = 447,
    DSA_DEALLOCATE = 448,
    SCOP_ANALYZE = 449,

    // --- Tensor Operations ---
    TENSOR_RESHAPE = 460,
    TENSOR_REDUCE = 461,
    TENSOR_TRANSPOSE = 462,
    TENSOR_SLICE = 463,
    TENSOR_CONCAT = 464,
    TENSOR_BROADCAST = 465,
    TENSOR_GEMM = 466,
    TENSOR_CONV = 467,
    TENSOR_POOL = 468,
    TENSOR_SOFTMAX = 469,

    // --- Pipeline / Fusion ---
    REGION_BEGIN = 600,
    REGION_END   = 601,
    FUSION_GROUP = 602,

    // --- Custom ---
    CUSTOM_DIALECT_START = 0x8000,

    // Dialect Interface opcodes
    DGTL_FAST_ADD         = 0x8001,
    DGTL_FAST_FMA         = 0x8002,

    // SIMD dialect fused ops
    SIMD_REDUCE_MAX       = 0x8010,
    SIMD_REDUCE_MIN       = 0x8011,
    SIMD_SCATTER_INDIRECT = 0x8012,
    SIMD_MASKED_STORE     = 0x8013,
    SIMD_INTERLEAVED_LOAD = 0x8014,
    SIMD_COMPLEX_FMA      = 0x8015,

    // GPU dialect ops
    GPU_MEM_TRANSFER      = 0x8020,
    GPU_BARRIER           = 0x8021,
    GPU_SYNC_POINT        = 0x8022,
    GPU_KERNEL_FUSE       = 0x8023,
    GPU_ANNOTATE_DEVICE   = 0x8024,

    // Tensor ML ops
    TENSOR_RELU           = 0x8030,
    TENSOR_BATCHNORM      = 0x8031,
    TENSOR_GATHER         = 0x8032,
    TENSOR_SCATTER_D      = 0x8033,

    // Cross-language dialect ops
    CPP_TEMPLATE_SPEC     = 0x8101,
    RUST_BORROW_MUT       = 0x8102
};

namespace Flags {

    inline constexpr uint16_t NONE               = 0x0000;
    inline constexpr uint16_t IS_CONST           = 0x0001;
    inline constexpr uint16_t IS_DEAD            = 0x0002;
    inline constexpr uint16_t HAS_SIDE_EFFECTS   = 0x0004;
    inline constexpr uint16_t IS_VOLATILE        = 0x0008;

    inline constexpr uint16_t IS_SIMD            = 0x0010;
    inline constexpr uint16_t IS_SIMD_BURST      = IS_SIMD;
    inline constexpr uint16_t IS_VECTOR_LANE     = 0x0020;
    inline constexpr uint16_t IS_PREDICATED      = 0x0040;

    inline constexpr uint16_t IS_FUSED           = 0x0080;
    inline constexpr uint16_t IS_MACRO_FUSED     = IS_FUSED;
    inline constexpr uint16_t IS_OPTIMIZED       = 0x0100;
    inline constexpr uint16_t IS_LOOP_INVARIANT  = 0x0200;

    inline constexpr uint16_t IS_SPEC_CONST      = 0x0400;
    inline constexpr uint16_t NON_TEMPORAL       = 0x0800;
    inline constexpr uint16_t NON_TEMPORAL_HINT  = NON_TEMPORAL;

    inline constexpr uint16_t IS_BRANCH          = 0x1000;
    inline constexpr uint16_t IS_RETURN          = 0x2000;

    inline constexpr uint16_t IS_GUARD           = 0x4000;
    inline constexpr uint16_t MAY_DEOPT          = 0x8000;

    // Phase-1 semantic lock: canonical semantic bits must remain collision-free.
    inline constexpr uint16_t kCanonicalSemanticMask =
        IS_CONST | IS_DEAD | HAS_SIDE_EFFECTS | IS_VOLATILE |
        IS_SIMD | IS_VECTOR_LANE | IS_PREDICATED |
        IS_FUSED | IS_OPTIMIZED | IS_LOOP_INVARIANT |
        IS_SPEC_CONST | NON_TEMPORAL |
        IS_BRANCH | IS_RETURN | IS_GUARD | MAY_DEOPT;

    static_assert(kCanonicalSemanticMask == 0xFFFFu,
                  "Flags canonical semantic bits must be unique and fully packed (16/16 used)");

} // namespace Flags

namespace ExtendedFlags {

    static constexpr uint32_t MEM_RELAXED   = 0x00000001;
    static constexpr uint32_t MEM_ACQUIRE   = 0x00000002;
    static constexpr uint32_t MEM_RELEASE_  = 0x00000004;
    static constexpr uint32_t MEM_SEQ_CST   = 0x00000008;

    static constexpr uint32_t NO_ALIAS      = 0x00000010;
    static constexpr uint32_t MAY_ALIAS     = 0x00000020;

    static constexpr uint32_t FAST_MATH     = 0x00000040;
    static constexpr uint32_t STRICT_FP     = 0x00000080;

    static constexpr uint32_t LATENCY_CRITICAL = 0x00000100;
    static constexpr uint32_t THROUGHPUT_MODE  = 0x00000200;

    static constexpr uint32_t PREFETCH_HINT = 0x00000400;

    static constexpr uint32_t VECTORIZABLE  = 0x00000800;
    static constexpr uint32_t VEC_WIDTH_128 = 0x00001000;
    static constexpr uint32_t VEC_WIDTH_256 = 0x00002000;
    static constexpr uint32_t VEC_WIDTH_512 = 0x00004000;

    static constexpr uint32_t PARALLEL_SAFE = 0x00008000;
    static constexpr uint32_t REQUIRES_SYNC = 0x00010000;

    static constexpr uint32_t HOT_PATH      = 0x00020000;
    static constexpr uint32_t COLD_PATH     = 0x00040000;
    static constexpr uint32_t JIT_ONLY      = 0x00080000;
    static constexpr uint32_t AOT_ONLY      = 0x00100000;
    static constexpr uint32_t HYBRID_READY  = 0x00200000;

    static constexpr uint32_t DEOPT_SAFEPOINT = 0x00400000;

    static constexpr uint32_t PROFILE_GUIDED   = 0x00800000;
    static constexpr uint32_t SPECULATIVE_PATH = 0x01000000;
    static constexpr uint32_t CACHE_HOT        = 0x02000000;
    static constexpr uint32_t CACHE_COLD       = 0x04000000;
    static constexpr uint32_t INLINE_CANDIDATE = 0x08000000;
    static constexpr uint32_t NO_INLINE        = 0x10000000;
    static constexpr uint32_t VIRTUALIZED      = 0x20000000;
    static constexpr uint32_t SECURITY_HARDEN  = 0x40000000;
    static constexpr uint32_t DETERMINISTIC    = 0x80000000;

    static constexpr uint64_t IS_OWNED         = 0x0001000000ULL;
    static constexpr uint64_t IS_SHARED        = 0x0002000000ULL;
    static constexpr uint64_t IS_ATOMIC        = 0x0004000000ULL;
    static constexpr uint64_t IS_ISOLATED      = 0x0008000000ULL;
    static constexpr uint64_t IS_LINEAR        = 0x0010000000ULL;
    static constexpr uint64_t IS_BORROWED      = 0x0020000000ULL;
    static constexpr uint64_t MAY_DEADLOCK     = 0x0040000000ULL;
    static constexpr uint64_t REQUIRES_FENCE   = 0x0080000000ULL;

    static constexpr uint64_t IS_CONCURRENT    = 0x0100000000ULL;
    static constexpr uint64_t IS_SPAWN         = 0x0200000000ULL;
    static constexpr uint64_t IS_FIBER         = 0x0400000000ULL;
    static constexpr uint64_t IS_ASYNC         = 0x0800000000ULL;
    static constexpr uint64_t REQUIRES_SYNC_OP = 0x1000000000ULL;
    static constexpr uint64_t BARRIER_POINT    = 0x2000000000ULL;
    static constexpr uint64_t CHANNEL_OP       = 0x4000000000ULL;
    static constexpr uint64_t LOCK_OP          = 0x8000000000ULL;

    static constexpr uint64_t IS_GRAPH_OP      = 0x10000000000ULL;
    static constexpr uint64_t IS_TREE_OP       = 0x20000000000ULL;
    static constexpr uint64_t IS_TRIE_OP       = 0x40000000000ULL;
    static constexpr uint64_t IS_TENSOR_OP     = 0x80000000000ULL;
    static constexpr uint64_t HAS_SCOP         = 0x100000000000ULL;

    static constexpr uint64_t LANG_MYZON       = 0x200000000000ULL;
    static constexpr uint64_t LANG_CPP         = 0x400000000000ULL;
    static constexpr uint64_t LANG_RUST        = 0x800000000000ULL;

} // namespace ExtendedFlags

// Non-semantic optimization/scheduling hints are stored in IRNode::logic_flags.
namespace HintFlags {

    // Hint flags live in IRAtom::flags for backward compatibility.
    // They are intentionally segregated from semantic Flags namespace.
    inline constexpr uint16_t LatencySensitive     = 0x2000;
    inline constexpr uint16_t ThroughputMode       = 0x4000;

    inline constexpr uint16_t RequiresVecPort      = 0x0002;
    inline constexpr uint16_t SVEActive            = 0x0004;
    inline constexpr uint16_t IsSpillCandidate     = 0x0008;

    inline constexpr uint16_t BranchLikely         = 0x1000;
    inline constexpr uint16_t BranchUnlikely       = 0x2000;

    inline constexpr uint16_t IsHosted             = 0x0040;
    inline constexpr uint16_t RequiresDepassTarget = 0x0100;
    inline constexpr uint16_t NonTemporalHint      = 0x0800;

} // namespace HintFlags

inline constexpr uint32_t NULL_REGISTER = 0x0;
inline constexpr uint32_t INVALID_ATOM  = 0xFFFFFFFF;
inline constexpr uint32_t INVALID_SLOT  = INVALID_ATOM;

enum class OwnershipModel : uint8_t {
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

enum class ConcurrencyClass : uint8_t {
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

enum class DSAType : uint8_t {
    SCALAR = 0,
    TENSOR = 1,
    GRAPH = 2,
    TREE = 3,
    TRIE = 4,
    NODE = 5,
    EDGE = 6,
    STREAM = 7
};

enum class FrontendLanguage : uint8_t {
    UNIVERSAL = 0,
    MYZON = 1,
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
    OwnershipModel model = OwnershipModel::OWNED;
    uint32_t acquire_site = 0;
    uint32_t release_site = 0;
    uint32_t transfer_count = 0;
    uint32_t last_transfer_site = 0;
    bool is_borrowed = false;
    bool has_aliasing = false;
};

struct TypeContext {
    FrontendLanguage language = FrontendLanguage::UNIVERSAL;
    OwnershipModel default_ownership = OwnershipModel::OWNED;
    ConcurrencyClass concurrency_class = ConcurrencyClass::SEQUENTIAL;
    DSAType primary_dsa = DSAType::SCALAR;
    bool is_value_type = false;
    bool is_generic = false;
    uint32_t generic_params = 0;
    uint64_t type_id = 0;
};

} // namespace dgtl::ir
