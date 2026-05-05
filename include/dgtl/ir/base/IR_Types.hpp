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
#include <bit>

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

namespace dgtl::ir {

// ============================================================
// === [CORE TYPE DEFINITIONS]
// ============================================================

struct IRCore {
    uint32_t result_id = 0;

    uint32_t op_a = 0;
    uint32_t op_b = 0;
    uint32_t op_c = 0;

    uint64_t intent_hash = 0;   // GVN / structural identity

    uint32_t logic_flags = 0;   // Extended semantic flags (Tier-3)
    uint32_t aux_flags   = 0;   // execution / scheduling / backend hints

    // === NEW (DO NOT BREAK ABI SIZE EXPECTATIONS IN FUTURE) ===
    // These are *interpreted*, not always used.
    uint32_t debug_id    = 0;   // debug / trace / mapping to source
    uint32_t epoch       = 0;   // versioning for IR evolution / mutation safety
};

// ============================================================
// === [OPCODES — EXTENDED, BUT STRUCTURED]
// ============================================================

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
    IMMEDIATE = 62,  // Unique value (not an alias to LOAD_IMM)

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

    // --- NEW: Extended Integer ---
    ADD_I64 = 60,
    SUB_I64 = 61,
    MUL_I64 = 62,
    DIV_I64 = 63,

    // --- Floating ---
    ADD_F32 = 30,
    SUB_F32 = 31,
    MUL_F32 = 32,
    DIV_F32 = 33,
    FMA_F32 = 35,
    FMA_WIDEN_F16_F32 = 36,

    // --- NEW: FP64 ---
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

    // --- NEW: Generic SIMD ---
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

    // --- Async ---
    ASYNC_LAUNCH = 300,
    SYNC_BARRIER = 301,

    // --- GPU ---
    GPU_LAUNCH = 400,

    // ============================================================================
    // NEW: OWNERSHIP & MEMORY SAFETY (410-419)
    // ============================================================================
    OWNERSHIP_ACQUIRE = 410,     // Acquire ownership
    OWNERSHIP_RELEASE = 411,     // Release ownership  
    OWNERSHIP_TRANSFER = 412,    // Transfer ownership (move semantics)
    ATOMIC_SNAPSHOT = 413,       // Atomic capture of current value

    // ============================================================================
    // NEW: CONCURRENCY PRIMITIVES (420-439)
    // ============================================================================
    ASYNC_SPAWN = 420,           // Spawn async task (spawn keyword)
    ASYNC_AWAIT = 421,           // Await async result (await keyword)
    FIBER_YIELD = 422,           // Yield from fiber (fiber keyword)
    BARRIER_SYNC = 423,          // Synchronization barrier (barrier keyword)
    CHAN_SEND = 424,             // Send on channel (send keyword)
    CHAN_RECV = 425,             // Receive from channel (recv keyword)
    LOCK_ACQUIRE = 426,          // Lock acquisition (lock keyword)
    LOCK_RELEASE = 427,          // Lock release (release keyword)
    MEMORY_FENCE = 428,          // Memory fence (fence keyword)
    THREAD_SPAWN = 429,          // Spawn OS thread (thread keyword)

    // ============================================================================
    // NEW: DSA OPERATIONS (440-459) - Graph, Tree, Trie
    // ============================================================================
    GRAPH_BUILD = 440,           // Construct graph from edges
    GRAPH_TRAVERSE = 441,        // Traverse graph (DFS/BFS)
    TREE_BUILD = 442,            // Construct tree structure
    TREE_TRAVERSE = 443,         // Traverse tree (in-order, post-order, etc)
    TRIE_BUILD = 444,            // Build trie (prefix tree)
    TRIE_LOOKUP = 445,           // Trie key lookup
    TRIE_INSERT = 446,           // Insert into trie
    DSA_ALLOCATE = 447,          // Allocate DSA node
    DSA_DEALLOCATE = 448,        // Deallocate DSA node
    SCOP_ANALYZE = 449,          // Analyze polyhedral loop structure

    // ============================================================================
    // NEW: TENSOR OPERATIONS (460-479) - ML/HPC
    // ============================================================================
    TENSOR_RESHAPE = 460,        // Reshape tensor dimensions
    TENSOR_REDUCE = 461,         // Reduce tensor (sum, max, etc)
    TENSOR_TRANSPOSE = 462,      // Transpose/permute dimensions
    TENSOR_SLICE = 463,          // Extract tensor slice
    TENSOR_CONCAT = 464,         // Concatenate tensors
    TENSOR_BROADCAST = 465,      // Broadcast to shape
    TENSOR_GEMM = 466,           // General matrix multiplication
    TENSOR_CONV = 467,           // Convolution operation
    TENSOR_POOL = 468,           // Pooling operation
    TENSOR_SOFTMAX = 469,        // Softmax normalization

    // --- NEW: Pipeline / Fusion ---
    REGION_BEGIN = 600,
    REGION_END   = 601,
    FUSION_GROUP = 602,

    // --- Custom ---
    CUSTOM_DIALECT_START = 0x8000
};

// ============================================================
// === [IR ATOM]
// ============================================================

#pragma pack(push, 1)
struct IRAtom {
    OpCode opcode;
    uint16_t flags;

    constexpr IRAtom() = default;
    constexpr IRAtom(OpCode op, uint16_t fl) noexcept : opcode(op), flags(fl) {}

    constexpr operator uint32_t() const noexcept {
        return (static_cast<uint32_t>(opcode) << 16) | flags;
    }

    constexpr IRAtom& operator=(uint32_t value) noexcept {
        opcode = static_cast<OpCode>(value >> 16);
        flags = static_cast<uint16_t>(value & 0xFFFF);
        return *this;
    }
};
#pragma pack(pop)

// ============================================================
// === [FLAGS — TIER 1: FAST PATH FILTERING]
// ============================================================

namespace Flags {

    inline constexpr uint16_t NONE               = 0x0000;
    inline constexpr uint16_t IS_CONST           = 0x0001;
    inline constexpr uint16_t IS_DEAD            = 0x0002;
    inline constexpr uint16_t HAS_SIDE_EFFECTS   = 0x0004;
    inline constexpr uint16_t IS_VOLATILE        = 0x0008;

    inline constexpr uint16_t IS_SIMD            = 0x0010;
    inline constexpr uint16_t IS_SIMD_BURST      = 0x0010;
    inline constexpr uint16_t IS_VECTOR_LANE     = 0x0020;
    inline constexpr uint16_t IS_PREDICATED      = 0x0040;

    inline constexpr uint16_t IS_FUSED           = 0x0080;
    inline constexpr uint16_t IS_MACRO_FUSED     = 0x0080;
    inline constexpr uint16_t IS_OPTIMIZED       = 0x0100;
    inline constexpr uint16_t IS_LOOP_INVARIANT  = 0x0200;

    inline constexpr uint16_t IS_MEMORY_OP       = 0x0400;
    inline constexpr uint16_t NON_TEMPORAL       = 0x0800;

    inline constexpr uint16_t IS_BRANCH          = 0x1000;
    inline constexpr uint16_t IS_RETURN          = 0x2000;

    inline constexpr uint16_t IS_GUARD           = 0x4000;
    inline constexpr uint16_t MAY_DEOPT          = 0x8000;

    // === NEW (micro-architectural hints packed cleverly) ===
    inline constexpr uint16_t LATENCY_SENSITIVE  = 0x2000; // overlaps allowed via layering
    inline constexpr uint16_t THROUGHPUT_MODE    = 0x4000;
    
    // === EXTENDED FLAGS (16-bit mappings for compatibility) ===
    // Note: Some of these overlap with higher bits, used in different contexts
    inline constexpr uint16_t IS_SPEC_CONST      = 0x0400;  // Unique bit for speculated constants (avoids IS_CONST collision)
    inline constexpr uint16_t REQUIRES_VEC_PORT  = 0x0002;  // Reuse IS_DEAD bit in extended context
    inline constexpr uint16_t SVE_ACTIVE         = 0x0004;  // Reuse HAS_SIDE_EFFECTS
    inline constexpr uint16_t IS_SPILL_CANDIDATE = 0x0008;  // Reuse IS_VOLATILE
    inline constexpr uint16_t BRANCH_LIKELY      = 0x1000;  // Reuse IS_BRANCH
    inline constexpr uint16_t BRANCH_UNLIKELY    = 0x2000;  // Reuse IS_RETURN
    inline constexpr uint16_t IS_HOSTED          = 0x0040;  // Reuse IS_PREDICATED
    inline constexpr uint16_t REQUIRES_DEPASS_TARGET = 0x0100; // Reuse IS_OPTIMIZED
    inline constexpr uint16_t NON_TEMPORAL_HINT  = 0x0800;  // Reuse NON_TEMPORAL

} // namespace Flags

// ============================================================
// === [EXTENDED FLAGS — TIER 2/3 SEMANTICS]
// ============================================================

namespace ExtendedFlags {

    // --- Memory Ordering ---
    static constexpr uint32_t MEM_RELAXED   = 0x00000001;
    static constexpr uint32_t MEM_ACQUIRE   = 0x00000002;
    static constexpr uint32_t MEM_RELEASE_  = 0x00000004;
    static constexpr uint32_t MEM_SEQ_CST   = 0x00000008;

    // --- Alias ---
    static constexpr uint32_t NO_ALIAS      = 0x00000010;
    static constexpr uint32_t MAY_ALIAS     = 0x00000020;

    // --- FP ---
    static constexpr uint32_t FAST_MATH     = 0x00000040;
    static constexpr uint32_t STRICT_FP     = 0x00000080;

    // --- Scheduling ---
    static constexpr uint32_t LATENCY_CRITICAL = 0x00000100;
    static constexpr uint32_t THROUGHPUT_MODE  = 0x00000200;

    // --- Cache ---
    static constexpr uint32_t PREFETCH_HINT = 0x00000400;

    // --- Vector ---
    static constexpr uint32_t VECTORIZABLE  = 0x00000800;
    static constexpr uint32_t VEC_WIDTH_128 = 0x00001000;
    static constexpr uint32_t VEC_WIDTH_256 = 0x00002000;
    static constexpr uint32_t VEC_WIDTH_512 = 0x00004000;

    // --- Parallel ---
    static constexpr uint32_t PARALLEL_SAFE = 0x00008000;
    static constexpr uint32_t REQUIRES_SYNC = 0x00010000;

    // --- Hybrid Execution ---
    static constexpr uint32_t HOT_PATH      = 0x00020000;
    static constexpr uint32_t COLD_PATH     = 0x00040000;
    static constexpr uint32_t JIT_ONLY      = 0x00080000;
    static constexpr uint32_t AOT_ONLY      = 0x00100000;
    static constexpr uint32_t HYBRID_READY  = 0x00200000;

    // --- Deopt ---
    static constexpr uint32_t DEOPT_SAFEPOINT = 0x00400000;

    // --- NEW: Execution Intelligence ---
    static constexpr uint32_t PROFILE_GUIDED   = 0x00800000;
    static constexpr uint32_t SPECULATIVE_PATH = 0x01000000;
    static constexpr uint32_t CACHE_HOT        = 0x02000000;
    static constexpr uint32_t CACHE_COLD       = 0x04000000;
    static constexpr uint32_t INLINE_CANDIDATE = 0x08000000;
    static constexpr uint32_t NO_INLINE        = 0x10000000;
    static constexpr uint32_t VIRTUALIZED      = 0x20000000;
    static constexpr uint32_t SECURITY_HARDEN  = 0x40000000;
    static constexpr uint32_t DETERMINISTIC    = 0x80000000;

    // ============================================================================
    // NEW: OWNERSHIP & LANGUAGE SUPPORT (TIER 2 SEMANTICS - 64-BIT EXTENDED)
    // ============================================================================
    
    // --- Ownership Model Tracking ---
    static constexpr uint64_t IS_OWNED        = 0x0001000000ULL;  // Bit 32-39
    static constexpr uint64_t IS_SHARED       = 0x0002000000ULL;  // Bit 33-40
    static constexpr uint64_t IS_ATOMIC       = 0x0004000000ULL;  // Bit 34
    static constexpr uint64_t IS_ISOLATED     = 0x0008000000ULL;  // Bit 35
    static constexpr uint64_t IS_LINEAR       = 0x0010000000ULL;  // Bit 36
    static constexpr uint64_t IS_BORROWED     = 0x0020000000ULL;  // Bit 37
    static constexpr uint64_t MAY_DEADLOCK    = 0x0040000000ULL;  // Bit 38
    static constexpr uint64_t REQUIRES_FENCE  = 0x0080000000ULL;  // Bit 39

    // --- Concurrency Class ---
    static constexpr uint64_t IS_CONCURRENT   = 0x0100000000ULL;  // Concurrent operation marker
    static constexpr uint64_t IS_SPAWN        = 0x0200000000ULL;  // spawn keyword (async task)
    static constexpr uint64_t IS_FIBER        = 0x0400000000ULL;  // fiber keyword
    static constexpr uint64_t IS_ASYNC        = 0x0800000000ULL;  // async keyword
    static constexpr uint64_t REQUIRES_SYNC_OP = 0x1000000000ULL;  // Must synchronize (renamed to avoid conflict)
    static constexpr uint64_t BARRIER_POINT   = 0x2000000000ULL;  // Synchronization barrier
    static constexpr uint64_t CHANNEL_OP      = 0x4000000000ULL;  // Send/recv on channel
    static constexpr uint64_t LOCK_OP         = 0x8000000000ULL;  // Lock/unlock operation

    // --- DSA & Language Support ---
    static constexpr uint64_t IS_GRAPH_OP     = 0x10000000000ULL;  // Graph operation
    static constexpr uint64_t IS_TREE_OP      = 0x20000000000ULL;  // Tree operation
    static constexpr uint64_t IS_TRIE_OP      = 0x40000000000ULL;  // Trie operation
    static constexpr uint64_t IS_TENSOR_OP    = 0x80000000000ULL;  // Tensor/ML operation
    static constexpr uint64_t HAS_SCOP        = 0x100000000000ULL;  // Has polyhedral structure

    // --- Frontend Language Hints ---
    static constexpr uint64_t LANG_MYZON      = 0x200000000000ULL;  // MYZON language
    static constexpr uint64_t LANG_CPP        = 0x400000000000ULL;  // C++ language
    static constexpr uint64_t LANG_RUST       = 0x800000000000ULL;  // Rust language

} // namespace ExtendedFlags

// ============================================================
// === [SENTINELS]
// ============================================================

inline constexpr uint32_t NULL_REGISTER = 0x0;
inline constexpr uint32_t INVALID_ATOM  = 0xFFFFFFFF;
inline constexpr uint32_t INVALID_SLOT  = INVALID_ATOM;

// ============================================================
// === [UTILITIES]
// ============================================================

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
        return IRAtom(
            static_cast<OpCode>(value >> 16),
            static_cast<uint16_t>(value & 0xFFFF)
        );
    }
};

// ============================================================
// === [LANGUAGE-AGNOSTIC TYPES - UNIVERSAL FRONTEND SUPPORT]
// ============================================================

/**
 * @brief Ownership Model for Language-Agnostic IR
 * Supports: owned, shared, const, atomic, isolated, mut, auto, volatile, linear, managed, opaque, sealed, cap
 */
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

/**
 * @brief Concurrency Class for Thread Safety & Synchronization
 * Supports: spawn, async, fiber, elastic, thread, sync, await, join, lock, fence, recv, send, barrier
 */
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

/**
 * @brief DSA (Data Structure Algorithm) Type for Specialized Optimizations
 * Supports: tensor, graph, tree, trie, node, edge
 */
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

/**
 * @brief Frontend Context - Language identification for IR decisions
 * Universal support for MYZON, C++, Rust, Python, Go, etc.
 */
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

/**
 * @brief Ownership Lifetime Tracking for Resource Management
 */
struct OwnershipLifetime {
    OwnershipModel model = OwnershipModel::OWNED;
    uint32_t acquire_site = 0;      // IR node where ownership acquired
    uint32_t release_site = 0;      // IR node where ownership released
    uint32_t transfer_count = 0;    // Number of transfers in lifetime
    uint32_t last_transfer_site = 0; // Last location where transfer occurred (for diagnostics)
    bool is_borrowed = false;       // Temporary non-owning reference
    bool has_aliasing = false;      // Multiple references exist
};

/**
 * @brief Type Context for Frontend-Agnostic Type Handling
 */
struct TypeContext {
    FrontendLanguage language = FrontendLanguage::UNIVERSAL;
    OwnershipModel default_ownership = OwnershipModel::OWNED;
    ConcurrencyClass concurrency_class = ConcurrencyClass::SEQUENTIAL;
    DSAType primary_dsa = DSAType::SCALAR;
    
    // Type narrowing for specialization
    bool is_value_type = false;
    bool is_generic = false;
    uint32_t generic_params = 0;
    
    uint64_t type_id = 0;  // Unique type hash
};

} // namespace dgtl::ir