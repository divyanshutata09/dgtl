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

#ifndef DGTL_SDK_H
#define DGTL_SDK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * SECTION 1: SYSTEM ANCHORS, VISIBILITY & CALLING CONVENTIONS
 * ============================================================================ */

#if defined(DGTL_SHARED_BUILD)
  #if defined(_WIN32) || defined(_WIN64)
    #define DGTL_API __declspec(dllexport)
  #else
    #define DGTL_API __attribute__((visibility("default")))
  #endif
#elif defined(DGTL_SHARED_IMPORT)
  #if defined(_WIN32) || defined(_WIN64)
    #define DGTL_API __declspec(dllimport)
  #else
    #define DGTL_API
  #endif
#else
  #define DGTL_API
#endif

#if defined(_WIN32) || defined(_WIN64)
  #define DGTL_CALL __cdecl
#else
  #define DGTL_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Global Identity & Versioning ---------- */
#define DGTL_VERSION_MAJOR   2
#define DGTL_VERSION_MINOR   0
#define DGTL_VERSION_PATCH   0
#define DGTL_VERSION_STRING  "2.0.0-SOVEREIGN"

DGTL_API const char* DGTL_CALL dgtl_version_string(void);

/* ---------- System Constants & Sentinels ---------- */
#define DGTL_INVALID_SLOT    0xFFFFFFFFu
#define DGTL_NULL_REGISTER   0x00000000u
#define DGTL_MAX_DIAGNOSTICS 512u

/* ---------- Opaque Engine Handles ---------- */
typedef struct dgtl_context           dgtl_context_t;
typedef struct dgtl_stream            dgtl_stream_t;
typedef struct dgtl_session           dgtl_session_t;
typedef struct dgtl_runtime_context   dgtl_runtime_context_t;
typedef struct dgtl_type_descriptor   dgtl_type_descriptor_t;

typedef uint16_t dgtl_op_t;
typedef uint32_t dgtl_value_t;

/* ---------- Runtime Task Function Pointer Definition ---------- */
typedef void (DGTL_CALL *dgtl_runtime_task_fn)(void* arg);

/* ============================================================================
 * SECTION 2: CANONICAL OPCODES (FULL 178 BIR SPECIFICATION)
 * ============================================================================ */

#ifndef DGTL_OPCODE_DEFINED
#define DGTL_OPCODE_DEFINED
typedef enum dgtl_opcode {
    /* --- Core Dataflow & Memory --- */
    DGTL_OP_NOP                   = 0,
    DGTL_OP_LOAD                  = 10,
    DGTL_OP_STORE                 = 11,
    DGTL_OP_REG_MOVE              = 12,
    DGTL_OP_MOV                   = 12,
    DGTL_OP_LOAD_IMM              = 13,
    DGTL_OP_CONST                 = 13,
    DGTL_OP_LEA                   = 14,
    DGTL_OP_PREFETCH              = 15,
    DGTL_OP_ATOMIC_LOAD           = 16,
    DGTL_OP_ATOMIC_STORE          = 17,
    DGTL_OP_ATOMIC_FENCE          = 18,
    DGTL_OP_IMMEDIATE             = 80,
    DGTL_OP_PTR_CAST              = 81,
    DGTL_OP_PTR_DIFF              = 82,
    DGTL_OP_BOOL_SELECT           = 83,
    DGTL_OP_STRING_CONST          = 84,
    DGTL_OP_STRING_LENGTH         = 85,
    DGTL_OP_STRING_COMPARE        = 86,
    DGTL_OP_STRING_CONCAT         = 87,
    DGTL_OP_STRING_SUBSTRING      = 88,
    DGTL_OP_UNDEF                 = 89,
    DGTL_OP_GEP                   = 90,
    DGTL_OP_PTR_TO_INT            = 91,
    DGTL_OP_INT_TO_PTR            = 92,
    DGTL_OP_LABEL                 = 98,
    DGTL_OP_GOTO                  = 99,
    DGTL_OP_BRANCH                = 100,
    DGTL_OP_RET                   = 101,
    DGTL_OP_RETURN                = 101,
    DGTL_OP_BRANCH_COND           = 102,
    DGTL_OP_BRANCH_COMPARE_FUSED  = 103,
    DGTL_OP_CALL                  = 110,
    DGTL_OP_SYSCALL               = 111,
    DGTL_OP_GUARD_ALIAS           = 120,

    /* --- Integer Arithmetic --- */
    DGTL_OP_ADD_I8                = 19,
    DGTL_OP_ADD_I32               = 20,
    DGTL_OP_SUB_I32               = 21,
    DGTL_OP_MUL_I32               = 22,
    DGTL_OP_DIV_I32               = 23,
    DGTL_OP_SHL_I32               = 24,
    DGTL_OP_AND_I32               = 25,
    DGTL_OP_OR_I32                = 26,
    DGTL_OP_XOR_I32               = 27,
    DGTL_OP_MOD_I32               = 28,
    DGTL_OP_SHR_I32               = 29,
    DGTL_OP_ADD_I64               = 60,
    DGTL_OP_SUB_I64               = 61,
    DGTL_OP_MUL_I64               = 62,
    DGTL_OP_DIV_I64               = 63,
    DGTL_OP_MOD_I64               = 64,
    DGTL_OP_AND_I64               = 65,
    DGTL_OP_OR_I64                = 66,
    DGTL_OP_XOR_I64               = 67,
    DGTL_OP_SHL_I64               = 68,
    DGTL_OP_SHR_I64               = 69,
    DGTL_OP_ADD_I128              = 130,
    DGTL_OP_ADD_I256              = 131,

    /* --- Floating Point Arithmetic --- */
    DGTL_OP_ADD_F8                = 132,
    DGTL_OP_ADD_BF16              = 133,
    DGTL_OP_ADD_F32               = 30,
    DGTL_OP_SUB_F32               = 31,
    DGTL_OP_MUL_F32               = 32,
    DGTL_OP_DIV_F32               = 33,
    DGTL_OP_FMA_F32               = 35,
    DGTL_OP_FMA_WIDEN_F16_F32     = 36,
    DGTL_OP_ADD_F64               = 70,
    DGTL_OP_SUB_F64               = 71,
    DGTL_OP_MUL_F64               = 72,
    DGTL_OP_DIV_F64               = 73,

    /* --- Comparisons & Bit Manipulation --- */
    DGTL_OP_CMP_EQ_I32            = 40,
    DGTL_OP_CMP_NE_I32            = 41,
    DGTL_OP_CMP_LT_I32            = 42,
    DGTL_OP_CMP_LE_I32            = 43,
    DGTL_OP_CMP_GT_I32            = 44,
    DGTL_OP_CMP_GE_I32            = 45,
    DGTL_OP_BIT_FIELD_INSERT      = 50,
    DGTL_OP_BIT_FIELD_EXTRACT     = 51,

    /* --- Speculative SSA Operations --- */
    DGTL_OP_GUARD_VALUE           = 200,
    DGTL_OP_DEOPT_TRIGGER         = 201,
    DGTL_OP_PHI_NODE              = 202,
    DGTL_OP_ASSUME                = 203,
    DGTL_OP_SELECT                = 204,

    /* --- Phase D: Multi-Result & Variadic Operations --- */
    DGTL_OP_PHI_MULTI             = 0x0200,
    DGTL_OP_TUPLE_PACK            = 0x0201,
    DGTL_OP_TUPLE_UNPACK          = 0x0202,
    DGTL_OP_SELECT_MULTI          = 0x0203,
    DGTL_OP_CALL_VARIADIC         = 0x0204,
    DGTL_OP_SWITCH_DISPATCH       = 0x0205,
    DGTL_OP_VOLATILE_LOAD         = 0x0206,
    DGTL_OP_VOLATILE_STORE        = 0x0207,
    DGTL_OP_ACQUIRE_FENCE         = 0x0208,
    DGTL_OP_RELEASE_FENCE         = 0x0209,

    /* --- Acceleration & Concurrency --- */
    DGTL_OP_ASYNC_LAUNCH          = 300,
    DGTL_OP_SYNC_BARRIER          = 301,
    DGTL_OP_GPU_LAUNCH            = 400,
    DGTL_OP_OWNERSHIP_ACQUIRE     = 410,
    DGTL_OP_OWNERSHIP_RELEASE     = 411,
    DGTL_OP_OWNERSHIP_TRANSFER    = 412,
    DGTL_OP_ATOMIC_SNAPSHOT       = 413,
    DGTL_OP_ASYNC_SPAWN           = 420,
    DGTL_OP_ASYNC_AWAIT           = 421,
    DGTL_OP_FIBER_YIELD           = 422,
    DGTL_OP_BARRIER_SYNC          = 423,
    DGTL_OP_CHAN_SEND             = 424,
    DGTL_OP_CHAN_RECV             = 425,
    DGTL_OP_LOCK_ACQUIRE          = 426,
    DGTL_OP_LOCK_RELEASE          = 427,
    DGTL_OP_MEMORY_FENCE          = 428,
    DGTL_OP_THREAD_SPAWN          = 429,
    DGTL_OP_WASM_INVOKE           = 0x410,
    DGTL_OP_FIBER_YIELD_LOOP      = 0x411,
    DGTL_OP_SUSPEND_CTX           = 0x412,
    DGTL_OP_RESUME_CTX            = 0x413,
    DGTL_OP_NEON_VADD_Q           = 0x414,

    /* --- Structural DSA & SCoP Transforms --- */
    DGTL_OP_GRAPH_BUILD           = 440,
    DGTL_OP_GRAPH_TRAVERSE        = 441,
    DGTL_OP_TREE_BUILD            = 442,
    DGTL_OP_TREE_TRAVERSE         = 443,
    DGTL_OP_TRIE_BUILD            = 444,
    DGTL_OP_TRIE_LOOKUP           = 445,
    DGTL_OP_TRIE_INSERT           = 446,
    DGTL_OP_DSA_ALLOCATE          = 447,
    DGTL_OP_DSA_DEALLOCATE        = 448,
    DGTL_OP_SCOP_ANALYZE          = 449,

    /* --- Tensors & Neural Operators --- */
    DGTL_OP_TENSOR_RESHAPE        = 460,
    DGTL_OP_TENSOR_REDUCE         = 461,
    DGTL_OP_TENSOR_TRANSPOSE      = 462,
    DGTL_OP_TENSOR_SLICE          = 463,
    DGTL_OP_TENSOR_CONCAT         = 464,
    DGTL_OP_TENSOR_BROADCAST      = 465,
    DGTL_OP_TENSOR_GEMM           = 466,
    DGTL_OP_TENSOR_CONV           = 467,
    DGTL_OP_TENSOR_POOL           = 468,
    DGTL_OP_TENSOR_SOFTMAX        = 469,
    DGTL_OP_MATMUL_BF16           = 0x480,
    DGTL_OP_MATMUL_FP8            = 0x481,
    DGTL_OP_SDPA_F16              = 0x482,
    DGTL_OP_SOFTMAX_FAST          = 0x483,
    DGTL_OP_LAYER_NORM            = 0x484,
    DGTL_OP_ROPE_EMBED            = 0x485,

    /* --- SIMD Vector Operations --- */
    DGTL_OP_GATHER_F32_512        = 0x500,
    DGTL_OP_GATHER_16             = 0x501,
    DGTL_OP_STORE_V512            = 0x502,
    DGTL_OP_VEC_ADD_F32_512       = 0x503,
    DGTL_OP_VEC_MUL_F32_512       = 0x504,
    DGTL_OP_SIMD_BSL_PREDICATE    = 0x507,
    DGTL_OP_FCVTL_F16_TO_F32      = 0x508,
    DGTL_OP_FCVTN_F32_TO_F16      = 0x509,
    DGTL_OP_VEC_LOAD              = 0x510,
    DGTL_OP_VEC_STORE             = 0x511,
    DGTL_OP_VEC_SHUFFLE           = 0x512,
    DGTL_OP_VEC_REDUCE_ADD        = 0x513,

    /* --- HFT Low-Latency Hardware Primitives --- */
    DGTL_OP_READ_TSC              = 0x600,
    DGTL_OP_BRANCH_LIKELY         = 0x601,
    DGTL_OP_BRANCH_UNLIKELY       = 0x602,
    DGTL_OP_CLFLUSH               = 0x603,
    DGTL_OP_PREFETCH_T0           = 0x604,
    DGTL_OP_SPIN_ACQUIRE          = 0x605,
    DGTL_OP_RING_PUSH_FREE        = 0x606,

    /* --- Spatial Game Mathematics --- */
    DGTL_OP_FAST_INV_SQRT         = 0x700,
    DGTL_OP_VEC3_CROSS            = 0x701,
    DGTL_OP_VEC4_DOT              = 0x702,
    DGTL_OP_MAT4_MUL              = 0x703,
    DGTL_OP_QUAT_SLERP            = 0x704,
    DGTL_OP_RAY_AABB_ISECT        = 0x705,

    /* --- Pipeline Regions & Fusion --- */
    DGTL_OP_REGION_BEGIN          = 600,
    DGTL_OP_REGION_END            = 601,
    DGTL_OP_FUSION_GROUP          = 602,

    /* --- Custom Extensible Dialects --- */
    DGTL_OP_CUSTOM_DIALECT_START  = 0x8000,
    DGTL_OP_DGTL_FAST_ADD         = 0x8001,
    DGTL_OP_DGTL_FAST_FMA         = 0x8002,
    DGTL_OP_SIMD_REDUCE_MAX       = 0x8010,
    DGTL_OP_SIMD_REDUCE_MIN       = 0x8011,
    DGTL_OP_SIMD_SCATTER_INDIRECT = 0x8012,
    DGTL_OP_SIMD_MASKED_STORE     = 0x8013,
    DGTL_OP_SIMD_INTERLEAVED_LOAD = 0x8014,
    DGTL_OP_SIMD_COMPLEX_FMA      = 0x8015,
    DGTL_OP_GPU_MEM_TRANSFER      = 0x8020,
    DGTL_OP_GPU_BARRIER           = 0x8021,
    DGTL_OP_GPU_SYNC_POINT        = 0x8022,
    DGTL_OP_GPU_KERNEL_FUSE       = 0x8023,
    DGTL_OP_GPU_ANNOTATE_DEVICE   = 0x8024,
    DGTL_OP_TENSOR_RELU           = 0x8030,
    DGTL_OP_TENSOR_BATCHNORM      = 0x8031,
    DGTL_OP_TENSOR_GATHER         = 0x8032,
    DGTL_OP_TENSOR_SCATTER_D      = 0x8033,
    DGTL_OP_CPP_TEMPLATE_SPEC     = 0x8101,
    DGTL_OP_RUST_BORROW_MUT       = 0x8102,
    DGTL_OP_SENTINEL              = 0xFFFF
} dgtl_opcode_t;
#endif

/* ============================================================================
 * SECTION 3: CANONICAL TYPE CODES & BITFIELD DOMAINS
 * ============================================================================ */

typedef enum dgtl_type_code {
    DGTL_TYPE_VOID_T        = 0,
    DGTL_TYPE_BOOL          = 1,
    DGTL_TYPE_PREDICATE     = 2,
    DGTL_TYPE_ADDRESS_SPACE = 3,
    DGTL_TYPE_PTR           = 4,
    DGTL_TYPE_STRING_HANDLE = 5,
    DGTL_TYPE_I4            = 6,
    DGTL_TYPE_I8            = 7,
    DGTL_TYPE_I16           = 8,
    DGTL_TYPE_I32           = 9,
    DGTL_TYPE_I64           = 10,
    DGTL_TYPE_I128          = 11,
    DGTL_TYPE_I256          = 12,
    DGTL_TYPE_I512          = 13,
    DGTL_TYPE_F8            = 14,
    DGTL_TYPE_BF16          = 15,
    DGTL_TYPE_F16           = 16,
    DGTL_TYPE_F32           = 17,
    DGTL_TYPE_F64           = 18,
    DGTL_TYPE_F128          = 19,
    DGTL_TYPE_VEC3_F32      = 20,
    DGTL_TYPE_VEC4_F32      = 21,
    DGTL_TYPE_MAT4X4_F32    = 22,
    DGTL_TYPE_QUAT_F32      = 23,
    DGTL_TYPE_VECTOR_F32X16 = 24,
    DGTL_TYPE_VECTOR_I32X16 = 25,
    DGTL_TYPE_VECTOR_F16X32 = 26,
    DGTL_TYPE_TENSOR_FP8    = 27,
    DGTL_TYPE_TENSOR_BF16   = 28,
    DGTL_TYPE_TENSOR_F32    = 29,
    DGTL_TYPE_TENSOR_I64    = 30
} dgtl_type_code_t;

/* 16-bit Semantic Flags */
#define DGTL_FLAG_NONE               0x0000u
#define DGTL_FLAG_IS_CONST           0x0001u
#define DGTL_FLAG_IS_DEAD            0x0002u
#define DGTL_FLAG_HAS_SIDE_EFFECTS   0x0004u
#define DGTL_FLAG_IS_VOLATILE        0x0008u
#define DGTL_FLAG_IS_SIMD            0x0010u
#define DGTL_FLAG_IS_VECTOR_LANE     0x0020u
#define DGTL_FLAG_IS_PREDICATED      0x0040u
#define DGTL_FLAG_IS_FUSED           0x0080u
#define DGTL_FLAG_IS_OPTIMIZED       0x0100u
#define DGTL_FLAG_IS_LOOP_INVARIANT  0x0200u
#define DGTL_FLAG_IS_SPEC_CONST      0x0400u
#define DGTL_FLAG_NON_TEMPORAL       0x0800u
#define DGTL_FLAG_IS_BRANCH          0x1000u
#define DGTL_FLAG_IS_RETURN          0x2000u
#define DGTL_FLAG_IS_GUARD           0x4000u
#define DGTL_FLAG_MAY_DEOPT          0x8000u

/* 32-bit Extended Execution & Memory Flags */
#define DGTL_EXT_MEM_RELAXED         0x00000001u
#define DGTL_EXT_MEM_ACQUIRE         0x00000002u
#define DGTL_EXT_MEM_RELEASE         0x00000004u
#define DGTL_EXT_MEM_SEQ_CST         0x00000008u
#define DGTL_EXT_NO_ALIAS            0x00000010u
#define DGTL_EXT_MAY_ALIAS           0x00000020u
#define DGTL_EXT_FAST_MATH           0x00000040u
#define DGTL_EXT_STRICT_FP           0x00000080u
#define DGTL_EXT_LATENCY_CRITICAL    0x00000100u
#define DGTL_EXT_THROUGHPUT_MODE     0x00000200u
#define DGTL_EXT_PREFETCH_HINT       0x00000400u
#define DGTL_EXT_VECTORIZABLE        0x00000800u
#define DGTL_EXT_VEC_WIDTH_128       0x00001000u
#define DGTL_EXT_VEC_WIDTH_256       0x00002000u
#define DGTL_EXT_VEC_WIDTH_512       0x00004000u
#define DGTL_EXT_HOT_PATH            0x00020000u
#define DGTL_EXT_COLD_PATH           0x00040000u
#define DGTL_EXT_DETERMINISTIC       0x80000000u

/* Auxiliary Pointer Address Space Flags */
#define DGTL_AUX_NONE                    0x00000000u
#define DGTL_AUX_PTR_ADDRSPACE_GENERIC   0x00000001u
#define DGTL_AUX_PTR_ADDRSPACE_GLOBAL    0x00000002u
#define DGTL_AUX_PTR_ADDRSPACE_SHARED    0x00000004u
#define DGTL_AUX_PTR_ADDRSPACE_LOCAL     0x00000008u
#define DGTL_AUX_PTR_ADDRSPACE_CONSTANT  0x00000010u
#define DGTL_AUX_PREDICATE_CANONICAL     0x00000020u

/* Phase D Semantic Verification Flags */
#define DGTL_PHASED_IS_VARIADIC_OP       0x00000001u
#define DGTL_PHASED_HAS_MULTI_RESULT     0x00000002u
#define DGTL_PHASED_EFFECT_TRACKED       0x00000004u
#define DGTL_PHASED_DOMINANCE_ROOT       0x00000008u
#define DGTL_PHASED_TYPE_VERIFIED        0x00000010u
#define DGTL_PHASED_EFFECT_VERIFIED      0x00000020u
#define DGTL_PHASED_DOMINANCE_VERIFIED   0x00000040u

/* Backend Architectures */
typedef enum dgtl_target {
    DGTL_TARGET_X64_AVX512 = 0,
    DGTL_TARGET_ARM64_SVE  = 1,
    DGTL_TARGET_ARM32_NEON = 2,
    DGTL_TARGET_RISCV64    = 3,
    DGTL_TARGET_WASM       = 4,
    DGTL_TARGET_CUDA_PTX   = 5,
    DGTL_TARGET_ROCM       = 6,
    DGTL_TARGET_NASM_X64   = 7,
    DGTL_TARGET_ELF_OBJ    = 8,
    DGTL_TARGET_COFF_OBJ   = 9,
    DGTL_TARGET_MACHO      = 10,
    DGTL_TARGET_STATIC_LIB = 11,
    DGTL_TARGET_COUNT      = 12
} dgtl_target_t;

DGTL_API const char* DGTL_CALL dgtl_target_name(dgtl_target_t target);
DGTL_API const char* DGTL_CALL dgtl_target_extension(dgtl_target_t target);

/* ============================================================================
 * SECTION 4: EXACT CACHE-LINE ALIGNED STRUCTURES
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct dgtl_node {
    uint16_t opcode;       /* Canonical OpCode (dgtl_opcode_t) */
    uint16_t flags;        /* 16-bit Semantic Flags (DGTL_FLAG_*) */
    uint32_t result_id;    /* Virtual Register / SSA Slot */
    uint32_t op_a;         /* Primary Operand */
    uint32_t op_b;         /* Secondary Operand */
    uint32_t op_c;         /* Tertiary Operand */
    uint64_t intent_hash;  /* Structural Cryptographic Hash */
    uint32_t logic_flags;  /* 32-bit Extended Execution Flags (DGTL_EXT_*) */
} dgtl_node_t;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(dgtl_node_t) == 32, "dgtl_node_t must be exactly 32 bytes for AVX-512 alignment");
#endif

typedef struct dgtl_core_record {
    uint32_t result_id;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint64_t intent_hash;
    uint32_t logic_flags;
    uint32_t aux_flags;
    uint32_t debug_id;
    uint32_t epoch;
} dgtl_core_record_t;

/* Stream Evaluation Modes */
typedef enum dgtl_ir_mode {
    DGTL_IR_MODE_INT   = 0,
    DGTL_IR_MODE_FLOAT = 1,
    DGTL_IR_MODE_FMA   = 2
} dgtl_ir_mode_t;

/* Stream Optimization Profiles */
typedef enum dgtl_opt_profile {
    DGTL_OPT_PROFILE_DEFAULT     = 0,
    DGTL_OPT_PROFILE_FOLD        = 1,
    DGTL_OPT_PROFILE_FUSION      = 2,
    DGTL_OPT_PROFILE_SIMD        = 3,
    DGTL_OPT_PROFILE_PRESSURE    = 4,
    DGTL_OPT_PROFILE_PREDICATION = 5
} dgtl_opt_profile_t;

/* ============================================================================
 * SECTION 5: PERFORMANCE PROFILING, DIAGNOSTICS & STATUS
 * ============================================================================ */

typedef enum dgtl_severity {
    DGTL_SEV_INFO    = 0,
    DGTL_SEV_WARNING = 1,
    DGTL_SEV_ERROR   = 2,
    DGTL_SEV_FATAL   = 3
} dgtl_severity_t;

typedef struct dgtl_status {
    int32_t         ok;
    int32_t         error_code;
    dgtl_severity_t severity;
    char            message[256];
} dgtl_status_t;

typedef struct dgtl_strike_result {
    int32_t         ok;
    size_t          code_size;
    uint64_t        logic_hash;
    double          compilation_latency_ms;
    char            message[256];
} dgtl_strike_result_t;

typedef struct dgtl_perf_metrics {
    uint64_t total_compilation_time_us;
    uint64_t optimization_time_us;
    uint64_t emission_time_us;
    uint32_t peak_register_count;
    uint32_t peak_memory_usage_bytes;
    uint64_t estimated_cycle_count;
} dgtl_perf_metrics_t;

typedef struct dgtl_trace_event {
    uint64_t timestamp_us;
    uint32_t pass_id;
    uint32_t node_count;
    char     pass_name[64];
} dgtl_trace_event_t;

/* ============================================================================
 * SECTION 6: CONTEXT, STREAM & TRANSACTIONAL ISOLATION
 * ============================================================================ */

DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create(void);
DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create_custom(size_t worker_threads, size_t arena_mb_per_worker);
DGTL_API void            DGTL_CALL dgtl_context_destroy(dgtl_context_t* ctx);

DGTL_API dgtl_stream_t*  DGTL_CALL dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes);
DGTL_API void            DGTL_CALL dgtl_stream_destroy(dgtl_stream_t* stream);
DGTL_API size_t          DGTL_CALL dgtl_stream_node_count(const dgtl_stream_t* stream);
DGTL_API uint64_t        DGTL_CALL dgtl_stream_logic_hash(const dgtl_stream_t* stream);
DGTL_API void            DGTL_CALL dgtl_stream_reset(dgtl_stream_t* stream);
DGTL_API bool            DGTL_CALL dgtl_stream_reserve(dgtl_stream_t* stream, size_t additional_nodes);
DGTL_API dgtl_status_t   DGTL_CALL dgtl_stream_compact(dgtl_stream_t* s);

/* Transactional Shadow Core */
DGTL_API void DGTL_CALL dgtl_stream_begin_transaction(dgtl_stream_t* stream);
DGTL_API bool DGTL_CALL dgtl_stream_commit_transaction(dgtl_stream_t* stream);
DGTL_API void DGTL_CALL dgtl_stream_rollback_transaction(dgtl_stream_t* stream);
DGTL_API bool DGTL_CALL dgtl_stream_validate_shadow_integrity(const dgtl_stream_t* stream);

/* ============================================================================
 * SECTION 7: CANONICAL IR EMISSION ENGINE
 * ============================================================================ */

DGTL_API uint32_t DGTL_CALL dgtl_emit_node(dgtl_stream_t* s, dgtl_opcode_t op, uint16_t flags,
                                          uint32_t result_slot, uint32_t op_a, uint32_t op_b,
                                          uint32_t op_c, uint64_t intent_hash);
DGTL_API void     DGTL_CALL dgtl_set_node_logic_flags(dgtl_stream_t* s, size_t node_idx, uint32_t logic_flags);
DGTL_API void     DGTL_CALL dgtl_set_result_type(dgtl_stream_t* s, uint32_t result_slot, dgtl_type_code_t type_code);

/* Generic Op with Payload */
DGTL_API uint32_t DGTL_CALL dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint32_t flags);
DGTL_API uint32_t DGTL_CALL dgtl_emit_op_with_result(dgtl_stream_t* s, uint32_t result_slot, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint32_t flags);

/* Immediates & Memory */
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f32(dgtl_stream_t* s, float value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f64(dgtl_stream_t* s, double value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_binary(dgtl_stream_t* s, const void* data, size_t len);
DGTL_API uint32_t DGTL_CALL dgtl_emit_load(dgtl_stream_t* s, uint32_t ptr_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_store(dgtl_stream_t* s, uint32_t ptr_slot, uint32_t val_slot);

/* Integer ALU */
DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mod_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Bitwise / Shift Operations */
DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Floating-Point ALU */
DGTL_API uint32_t DGTL_CALL dgtl_emit_fadd(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fsub(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fmul(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fdiv(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fadd_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fsub_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fmul_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fdiv_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Integer & Floating-Point Comparisons */
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_eq_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ne_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_lt_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_le_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_gt_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ge_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_eq_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ne_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_lt_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_le_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_gt_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ge_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_eq_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ne_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_lt_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_le_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_gt_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ge_f32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_eq_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ne_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_lt_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_le_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_gt_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_cmp_ge_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Unary & Bitwise Negation */
DGTL_API uint32_t DGTL_CALL dgtl_emit_neg_i32(dgtl_stream_t* s, uint32_t val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_not_i32(dgtl_stream_t* s, uint32_t val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_neg_i64(dgtl_stream_t* s, uint32_t val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_not_i64(dgtl_stream_t* s, uint32_t val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_neg_f32(dgtl_stream_t* s, uint32_t val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_neg_f64(dgtl_stream_t* s, uint32_t val);

/* Selection & Memory Operations */
DGTL_API uint32_t DGTL_CALL dgtl_emit_select(dgtl_stream_t* s, uint32_t cond, uint32_t true_val, uint32_t false_val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_bool_select(dgtl_stream_t* s, uint32_t cond, uint32_t true_val, uint32_t false_val);
DGTL_API uint32_t DGTL_CALL dgtl_emit_gep(dgtl_stream_t* s, uint32_t base_ptr, uint32_t offset_bytes);
DGTL_API uint32_t DGTL_CALL dgtl_emit_ptr_to_int(dgtl_stream_t* s, uint32_t ptr_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_int_to_ptr(dgtl_stream_t* s, uint32_t int_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_ptr_cast(dgtl_stream_t* s, uint32_t ptr_slot, dgtl_type_code_t target_type);
DGTL_API uint32_t DGTL_CALL dgtl_emit_ptr_diff(dgtl_stream_t* s, uint32_t lhs_ptr, uint32_t rhs_ptr);
DGTL_API uint32_t DGTL_CALL dgtl_emit_reg_move(dgtl_stream_t* s, uint32_t dst_slot, uint32_t src_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_alloca(dgtl_stream_t* s, uint32_t size_bytes, uint32_t align);

/* Concurrency & Async Primitives */
DGTL_API uint32_t DGTL_CALL dgtl_emit_async_spawn(dgtl_stream_t* s, uint32_t fn_slot, uint32_t arg_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_async_await(dgtl_stream_t* s, uint32_t task_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_chan_send(dgtl_stream_t* s, uint32_t chan_slot, uint32_t val_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_chan_recv(dgtl_stream_t* s, uint32_t chan_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_barrier_sync(dgtl_stream_t* s, uint32_t barrier_slot);

/* Control Flow & Calls */
DGTL_API uint32_t DGTL_CALL dgtl_emit_label(dgtl_stream_t* s, const char* label_name);
DGTL_API uint32_t DGTL_CALL dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_with_params(dgtl_stream_t* s, uint32_t function_slot, const uint32_t* params, size_t param_count);
DGTL_API void     DGTL_CALL dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label_slot);
DGTL_API void     DGTL_CALL dgtl_emit_branch_cond(dgtl_stream_t* s, uint32_t cond_slot, uint32_t target_label_slot);
DGTL_API void     DGTL_CALL dgtl_emit_branch_predictive(dgtl_stream_t* s, uint32_t cond_reg, uint32_t true_label, uint32_t false_label, uint8_t weight);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API void     DGTL_CALL dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot);

/* String Handling */
DGTL_API void     DGTL_CALL dgtl_emit_constant_str(dgtl_stream_t* s, dgtl_value_t dest_reg, const char* value, size_t length);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_handle(dgtl_stream_t* s, uint32_t string_handle);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_utf8(dgtl_stream_t* s, const char* utf8_literal);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_length(dgtl_stream_t* s, uint32_t string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_compare(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_concat(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_substring(dgtl_stream_t* s, uint32_t string_slot, uint32_t start_slot, uint32_t length_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_rodata_rip(dgtl_stream_t* s, const char* str, size_t len);

/* Phase D: Variadics & Multi-Result */
DGTL_API uint32_t DGTL_CALL dgtl_emit_phi(dgtl_stream_t* s, uint32_t* incoming_slots, size_t incoming_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_phi_multi(dgtl_stream_t* s, uint32_t result_slot, const uint32_t* incoming_values, const uint32_t* incoming_blocks, size_t count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_tuple_pack(dgtl_stream_t* s, const uint32_t* elements, size_t count);
DGTL_API void     DGTL_CALL dgtl_emit_tuple_unpack(dgtl_stream_t* s, uint32_t tuple_slot, const uint32_t* out_result_slots, size_t count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);

/* Data Structures & SCoP Analysis */
DGTL_API uint32_t DGTL_CALL dgtl_emit_graph_build(dgtl_stream_t* s, uint32_t nodes_vector, uint32_t edges_vector);
DGTL_API uint32_t DGTL_CALL dgtl_emit_graph_traverse(dgtl_stream_t* s, uint32_t graph_handle, uint32_t visitor_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_trie_insert(dgtl_stream_t* s, uint32_t trie_handle, uint32_t key_str_handle, uint32_t val_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_scop_analyze(dgtl_stream_t* s, uint32_t region_begin_slot);

/* Linear Ownership Lifecycle */
DGTL_API uint32_t DGTL_CALL dgtl_emit_ownership_acquire(dgtl_stream_t* s, uint32_t resource_handle);
DGTL_API void     DGTL_CALL dgtl_emit_ownership_release(dgtl_stream_t* s, uint32_t resource_handle);
DGTL_API uint32_t DGTL_CALL dgtl_emit_ownership_transfer(dgtl_stream_t* s, uint32_t src_handle, uint32_t dest_context);
DGTL_API bool     DGTL_CALL dgtl_verify_ownership_lifetimes(const dgtl_stream_t* stream, char* err_buf, size_t err_buf_size);

/* AI, Neural & Spatial Acceleration */
DGTL_API uint32_t DGTL_CALL dgtl_emit_matmul_bf16(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_matmul_fp8(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sdpa_f16(dgtl_stream_t* s, uint32_t q, uint32_t k, uint32_t v, uint32_t mask);
DGTL_API uint32_t DGTL_CALL dgtl_emit_softmax_fast(dgtl_stream_t* s, uint32_t input);
DGTL_API uint32_t DGTL_CALL dgtl_emit_layer_norm(dgtl_stream_t* s, uint32_t input, uint32_t gamma, uint32_t beta);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fast_inv_sqrt(dgtl_stream_t* s, uint32_t input);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec3_cross(dgtl_stream_t* s, uint32_t a, uint32_t b);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec4_dot(dgtl_stream_t* s, uint32_t a, uint32_t b);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mat4_mul(dgtl_stream_t* s, uint32_t a, uint32_t b);

/* HFT Nanosecond Primitives */
DGTL_API uint32_t      DGTL_CALL dgtl_emit_read_tsc(dgtl_stream_t* s);
DGTL_API void          DGTL_CALL dgtl_emit_clflush(dgtl_stream_t* s, uint32_t addr_slot);
DGTL_API void          DGTL_CALL dgtl_emit_prefetch_t0(dgtl_stream_t* s, uint32_t addr_slot);
DGTL_API void          DGTL_CALL dgtl_emit_spin_acquire(dgtl_stream_t* s, uint32_t lock_slot);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_ring_push_free(dgtl_stream_t* s, uint32_t ring_handle, uint32_t payload_slot);
DGTL_API void          DGTL_CALL dgtl_emit_hardware_affinity(dgtl_stream_t* s, dgtl_value_t mask_register_id);
DGTL_API dgtl_status_t DGTL_CALL dgtl_apply_quiet_core_policy(uint32_t preferred_core);

/* ============================================================================
 * SECTION 8: CFG MATRIX, PASS MANAGEMENT & COMPACTION
 * ============================================================================ */

typedef enum dgtl_cfg_edge_kind {
    DGTL_CFG_EDGE_FALLTHROUGH       = 0,
    DGTL_CFG_EDGE_UNCONDITIONAL     = 1,
    DGTL_CFG_EDGE_CONDITIONAL_TRUE  = 2,
    DGTL_CFG_EDGE_CONDITIONAL_FALSE = 3
} dgtl_cfg_edge_kind_t;

typedef struct dgtl_cfg_block {
    uint32_t id;
    size_t   begin;
    size_t   end;
    uint32_t primary_label;
    int32_t  is_entry;
} dgtl_cfg_block_t;

typedef struct dgtl_cfg_edge {
    uint32_t             from;
    uint32_t             to;
    dgtl_cfg_edge_kind_t kind;
} dgtl_cfg_edge_t;

typedef struct dgtl_emit_options {
    int32_t inline_threshold;
    int32_t unroll_factor;
    int32_t enable_cache_optimization;
    int32_t enable_branch_prediction;
    int32_t vectorize_threshold;
    int32_t reserved[11];
} dgtl_emit_options_t;

DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_block_count(const dgtl_stream_t* s);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_edge_count(const dgtl_stream_t* s);
DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_block(const dgtl_stream_t* s, size_t index, dgtl_cfg_block_t* out);
DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_edge(const dgtl_stream_t* s, size_t index, dgtl_cfg_edge_t* out);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_successors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_predecessors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);

DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize(dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize_with_profile(dgtl_stream_t* s, dgtl_opt_profile_t profile);
DGTL_API dgtl_status_t DGTL_CALL dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode);
DGTL_API dgtl_status_t DGTL_CALL dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name);
DGTL_API int32_t       DGTL_CALL dgtl_has_pass(const char* pass_name);
DGTL_API const char**  DGTL_CALL dgtl_list_passes(size_t* out_count);

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_enabled(dgtl_stream_t* s, int32_t enabled);
DGTL_API int32_t       DGTL_CALL dgtl_get_optimization_level(const dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_level(dgtl_stream_t* s, int32_t level);
DGTL_API dgtl_emit_options_t DGTL_CALL dgtl_emit_options_default(dgtl_target_t target);
DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream_with_options(dgtl_stream_t* s, dgtl_target_t target, const dgtl_emit_options_t* options);

/* Diagnostics, Cycle Projections & Tracing */
DGTL_API size_t        DGTL_CALL dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target);
DGTL_API size_t        DGTL_CALL dgtl_stream_memory_usage(const dgtl_stream_t* s);
DGTL_API size_t        DGTL_CALL dgtl_context_memory_usage(const dgtl_context_t* ctx);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_shrink_to_fit(dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_enable_tracing(dgtl_stream_t* s);
DGTL_API dgtl_trace_event_t* DGTL_CALL dgtl_stream_get_trace_events(const dgtl_stream_t* s, size_t* out_count);
DGTL_API uint64_t      DGTL_CALL dgtl_stream_estimate_cycles(const dgtl_stream_t* s);
DGTL_API int32_t       DGTL_CALL dgtl_stream_get_optimization_score(const dgtl_stream_t* s);
DGTL_API int32_t       DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out);

DGTL_API void          DGTL_CALL dgtl_stream_dump_ir(const dgtl_stream_t* s, void* custom_file_handle_opt);
DGTL_API const char*   DGTL_CALL dgtl_get_last_error(void);
DGTL_API void          DGTL_CALL dgtl_clear_error(void);
DGTL_API void          DGTL_CALL dgtl_set_verbose(int32_t enabled);
DGTL_API const char*   DGTL_CALL dgtl_get_build_config(void);

/* Stream Node Inspection */
DGTL_API bool          DGTL_CALL dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_t* out_node);
DGTL_API bool          DGTL_CALL dgtl_stream_get_core_record(const dgtl_stream_t* s, size_t index, dgtl_core_record_t* out_record);
DGTL_API uint32_t      DGTL_CALL dgtl_stream_get_max_virtual_slot(const dgtl_stream_t* s);
DGTL_API bool          DGTL_CALL dgtl_node_is_terminator(dgtl_opcode_t op, uint16_t flags);
DGTL_API bool          DGTL_CALL dgtl_node_has_side_effects(dgtl_opcode_t op, uint16_t flags);
DGTL_API double        DGTL_CALL dgtl_node_predict_inlined_benefit(dgtl_opcode_t op);

/* Phase D: Sidecar Buffer Operations */
DGTL_API void          DGTL_CALL dgtl_stream_set_call_overflow_args(dgtl_stream_t* s, size_t node_idx, const uint32_t* args, size_t count);
DGTL_API size_t        DGTL_CALL dgtl_stream_get_call_overflow_args(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_args, size_t max_out);
DGTL_API void          DGTL_CALL dgtl_stream_set_operand_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* operands, size_t count);
DGTL_API size_t        DGTL_CALL dgtl_stream_get_operand_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_operands, size_t max_out);
DGTL_API void          DGTL_CALL dgtl_stream_set_result_ids(dgtl_stream_t* s, size_t node_idx, const uint32_t* result_ids, size_t count);
DGTL_API size_t        DGTL_CALL dgtl_stream_get_result_ids(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_result_ids, size_t max_out);
DGTL_API void          DGTL_CALL dgtl_stream_set_node_effects(dgtl_stream_t* s, size_t node_idx, uint32_t effect_set);
DGTL_API uint32_t      DGTL_CALL dgtl_stream_get_node_effects(const dgtl_stream_t* s, size_t node_idx);

/* Binary Interchange Serialization */
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t blob_size);
DGTL_API void          DGTL_CALL dgtl_free_serialized_blob(void* blob);
DGTL_API int32_t       DGTL_CALL dgtl_binary_loader_validate_payload(const uint8_t* bytes, size_t len);

/* Task Runtime Controls */
DGTL_API void          DGTL_CALL dgtl_yield(void);
DGTL_API uint64_t      DGTL_CALL dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg);
DGTL_API void          DGTL_CALL dgtl_join_task(uint64_t task_handle);
DGTL_API dgtl_runtime_context_t* DGTL_CALL dgtl_runtime_context_create(size_t worker_threads, size_t arena_mb_per_worker);
DGTL_API void          DGTL_CALL dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx);
DGTL_API uint64_t      DGTL_CALL dgtl_runtime_spawn_task(dgtl_runtime_context_t* ctx, dgtl_runtime_task_fn fn, void* arg);
DGTL_API void          DGTL_CALL dgtl_runtime_join_task(dgtl_runtime_context_t* ctx, uint64_t task_handle);
DGTL_API void          DGTL_CALL dgtl_runtime_yield(dgtl_runtime_context_t* ctx);

/* Linker Fallback Diagnostics */
DGTL_API size_t        DGTL_CALL dgtl_last_linker_fallback_count(void);
DGTL_API size_t        DGTL_CALL dgtl_last_linker_fallback_message(size_t index, char* out_buffer, size_t out_buffer_size);

/* ============================================================================
 * SECTION 9: SIMD VECTORIZATION, FMA & GATHER/SCATTER
 * ============================================================================ */

typedef enum dgtl_simd_arch {
    DGTL_SIMD_ARCH_GENERIC         = 0,
    DGTL_SIMD_ARCH_X86_SSE42       = 1,
    DGTL_SIMD_ARCH_X86_AVX         = 2,
    DGTL_SIMD_ARCH_X86_AVX2        = 3,
    DGTL_SIMD_ARCH_X86_AVX512_F_DQ = 4,
    DGTL_SIMD_ARCH_X86_AVX512_FULL = 5,
    DGTL_SIMD_ARCH_ARM_NEON        = 6,
    DGTL_SIMD_ARCH_ARM64_SVE       = 7,
    DGTL_SIMD_ARCH_ARM64_SVE2      = 8,
    DGTL_SIMD_ARCH_RISCV_V_128     = 9,
    DGTL_SIMD_ARCH_RISCV_V_256     = 10,
    DGTL_SIMD_ARCH_RISCV_V_512     = 11,
    DGTL_SIMD_ARCH_WASM_SIMD128    = 12,
    DGTL_SIMD_ARCH_COUNT           = 13
} dgtl_simd_arch_t;

typedef enum dgtl_simd_width {
    DGTL_SIMD_WIDTH_128 = 16,
    DGTL_SIMD_WIDTH_256 = 32,
    DGTL_SIMD_WIDTH_512 = 64
} dgtl_simd_width_t;

typedef enum dgtl_simd_scale {
    DGTL_SIMD_SCALE_BYTE  = 1,
    DGTL_SIMD_SCALE_WORD  = 2,
    DGTL_SIMD_SCALE_DWORD = 4,
    DGTL_SIMD_SCALE_QWORD = 8
} dgtl_simd_scale_t;

typedef enum dgtl_simd_mask_mode {
    DGTL_SIMD_MASK_NONE      = 0,
    DGTL_SIMD_MASK_ZEROING   = 1,
    DGTL_SIMD_MASK_MERGING   = 2
} dgtl_simd_mask_mode_t;

typedef struct dgtl_simd_caps {
    dgtl_simd_arch_t native_arch;
    uint32_t         max_vector_width_bytes;
    uint32_t         supported_element_widths_mask;
    bool             has_fma;
    bool             has_native_gather;
    bool             has_native_scatter;
    bool             has_fp16_arithmetic;
    bool             has_bf16_arithmetic;
    bool             has_masked_operations;
    bool             has_transcendental_approximations;
    uint32_t         native_vector_register_count;
} dgtl_simd_caps_t;

typedef struct dgtl_simd_gather_spec {
    uint32_t              base_ptr_slot;
    uint32_t              index_vector_slot;
    uint32_t              mask_slot;
    dgtl_simd_scale_t     scale;
    dgtl_simd_width_t     vector_width;
    dgtl_type_code_t      element_type;
    dgtl_simd_mask_mode_t mask_mode;
    uint32_t              memory_alignment;
} dgtl_simd_gather_spec_t;

typedef struct dgtl_simd_scatter_spec {
    uint32_t              base_ptr_slot;
    uint32_t              index_vector_slot;
    uint32_t              value_vector_slot;
    uint32_t              mask_slot;
    dgtl_simd_scale_t     scale;
    dgtl_simd_width_t     vector_width;
    dgtl_type_code_t      element_type;
    dgtl_simd_mask_mode_t mask_mode;
    uint32_t              memory_alignment;
} dgtl_simd_scatter_spec_t;

typedef struct dgtl_simd_legalize_options {
    dgtl_simd_arch_t  target_arch;
    uint32_t          forced_max_vector_width;
    bool              allow_software_gather_scatter;
    bool              decompose_unsupported_lanes;
    bool              enable_reciprocal_approximations;
} dgtl_simd_legalize_options_t;

DGTL_API dgtl_simd_caps_t DGTL_CALL dgtl_simd_query_host_caps(void);
DGTL_API bool             DGTL_CALL dgtl_simd_arch_is_supported(dgtl_simd_arch_t arch);

DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_f64(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fms_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fnmadd_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_widen_f16_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_widen_bf16_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_complex_fma(dgtl_stream_t* s, uint32_t acc, uint32_t a, uint32_t b);

DGTL_API uint32_t DGTL_CALL dgtl_emit_gather(dgtl_stream_t* s, const dgtl_simd_gather_spec_t* spec);
DGTL_API void     DGTL_CALL dgtl_emit_scatter(dgtl_stream_t* s, const dgtl_simd_scatter_spec_t* spec);

DGTL_API uint32_t DGTL_CALL dgtl_emit_gather_f32_512(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices);
DGTL_API uint32_t DGTL_CALL dgtl_emit_gather_16(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices);
DGTL_API uint32_t DGTL_CALL dgtl_emit_scatter_indirect(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices, uint32_t values);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_masked_store(dgtl_stream_t* s, uint32_t base_ptr, uint32_t mask, uint32_t values);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_interleaved_load(dgtl_stream_t* s, uint32_t base_ptr, uint8_t stride_elements);

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_load(dgtl_stream_t* s, uint32_t ptr, dgtl_simd_width_t width);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store(dgtl_stream_t* s, uint32_t ptr, uint32_t val, dgtl_simd_width_t width);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_load_aligned(dgtl_stream_t* s, uint32_t ptr, dgtl_simd_width_t width, uint32_t align);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store_aligned(dgtl_stream_t* s, uint32_t ptr, uint32_t val, dgtl_simd_width_t width, uint32_t align);

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_add_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_mul_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_add_f64_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_mul_f64_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_store_v512(dgtl_stream_t* s, uint32_t addr_id, uint32_t val_id);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store_v512(dgtl_stream_t* s, uint32_t addr_id, uint32_t val_id);

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_shuffle(dgtl_stream_t* s, uint32_t vec, uint32_t mask);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_blend(dgtl_stream_t* s, uint32_t mask, uint32_t if_true, uint32_t if_false);

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_reduce_add(dgtl_stream_t* s, uint32_t vec);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_reduce_max(dgtl_stream_t* s, uint32_t vec);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_reduce_min(dgtl_stream_t* s, uint32_t vec);

DGTL_API uint32_t DGTL_CALL dgtl_emit_fcvtl_f16_to_f32(dgtl_stream_t* s, uint32_t f16_vec);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fcvtn_f32_to_f16(dgtl_stream_t* s, uint32_t f32_vec);
DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_bsl_predicate(dgtl_stream_t* s, uint32_t mask, uint32_t t_val, uint32_t f_val);

DGTL_API dgtl_status_t DGTL_CALL dgtl_legalize_simd(dgtl_stream_t* s, dgtl_simd_arch_t arch);
DGTL_API dgtl_status_t DGTL_CALL dgtl_legalize_simd_ex(dgtl_stream_t* s, const dgtl_simd_legalize_options_t* opts);
DGTL_API uint32_t      DGTL_CALL dgtl_simd_get_native_lane_count(dgtl_simd_arch_t arch, dgtl_type_code_t type_code);

/* ============================================================================
 * SECTION 10: GPU ACCELERATION & PLATFORM PIPELINES
 * ============================================================================ */

typedef enum dgtl_quad_flags {
    DGTL_QUAD_SOLID_COLOR     = 0x00,
    DGTL_QUAD_LINEAR_GRADIENT = 0x01,
    DGTL_QUAD_RADIAL_GRADIENT = 0x02,
    DGTL_QUAD_TEXTURE_SAMPLED = 0x04,
    DGTL_QUAD_BOX_SHADOW      = 0x08,
    DGTL_QUAD_BORDER_ONLY     = 0x10,
    DGTL_QUAD_CLIP_ENABLED    = 0x20
} dgtl_quad_flags_t;

typedef enum dgtl_event_type {
    DGTL_EVENT_NONE            = 0x0000,
    DGTL_EVENT_MOUSE_DOWN      = 0x0001,
    DGTL_EVENT_MOUSE_UP        = 0x0002,
    DGTL_EVENT_MOUSE_MOVE      = 0x0004,
    DGTL_EVENT_CLICK           = 0x0008,
    DGTL_EVENT_KEY_DOWN        = 0x0010,
    DGTL_EVENT_KEY_UP          = 0x0020,
    DGTL_EVENT_SCROLL          = 0x0040,
    DGTL_EVENT_TOUCH_START     = 0x0080,
    DGTL_EVENT_TOUCH_MOVE      = 0x0100,
    DGTL_EVENT_TOUCH_END       = 0x0200,
    DGTL_EVENT_TOUCH_CANCEL    = 0x0400,
    DGTL_EVENT_FOCUS           = 0x0800,
    DGTL_EVENT_BLUR            = 0x1000,
    DGTL_EVENT_DPI_CHANGED     = 0x2000,
    DGTL_EVENT_IME_COMPOSITION = 0x4000,
    DGTL_EVENT_IME_COMMIT      = 0x8000
} dgtl_event_type_t;

typedef enum dgtl_cursor_type {
    DGTL_CURSOR_ARROW          = 0,
    DGTL_CURSOR_IBEAM          = 1,
    DGTL_CURSOR_HAND           = 2,
    DGTL_CURSOR_CROSSHAIR      = 3,
    DGTL_CURSOR_RESIZE_EW      = 4,
    DGTL_CURSOR_RESIZE_NS      = 5,
    DGTL_CURSOR_RESIZE_NWSE    = 6,
    DGTL_CURSOR_RESIZE_NESW    = 7,
    DGTL_CURSOR_NOT_ALLOWED    = 8
} dgtl_cursor_type_t;

#pragma pack(push, 1)
typedef struct dgtl_quad_instance {
    float    x, y, w, h;
    float    r, g, b, a;
    float    secondary_r, secondary_g;
    float    secondary_b, secondary_a;
    float    radius_tl, radius_tr;
    float    radius_br, radius_bl;
    float    border_width;
    float    shadow_blur;
    float    shadow_spread;
    uint32_t flags;
    uint32_t texture_slot;
    uint32_t clip_rect_id;
    float    _padding[10];
} dgtl_quad_instance_t;

typedef struct dgtl_glyph_gpu_instance {
    float    screen_x, screen_y;
    float    screen_w, screen_h;
    float    uv_x, uv_y;
    float    uv_w, uv_h;
    float    color_r, color_g;
    float    color_b, color_a;
    float    font_size;
    float    px_range;
    uint32_t atlas_texture_slot;
    uint32_t clip_rect_id;
} dgtl_glyph_gpu_instance_t;

typedef struct dgtl_vector_path_instance {
    float    p0_x, p0_y;
    float    p1_x, p1_y;
    float    p2_x, p2_y;
    float    stroke_width;
    float    r, g, b, a;
    uint32_t path_type;
    uint32_t clip_rect_id;
    float    _padding[3];
} dgtl_vector_path_instance_t;

typedef struct dgtl_raw_input_event {
    uint64_t timestamp_us;
    uint32_t event_type;
    float    pointer_x, pointer_y;
    float    delta_x, delta_y;
    uint32_t key_code;
    uint32_t modifiers;
    uint32_t touch_id;
    float    dpi_scale;
    uint32_t target_node;
    uint8_t  is_handled;
    char     ime_text[15];
} dgtl_raw_input_event_t;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(dgtl_quad_instance_t) == 128, "dgtl_quad_instance_t must be exactly 128 bytes");
_Static_assert(sizeof(dgtl_glyph_gpu_instance_t) == 64, "dgtl_glyph_gpu_instance_t must be exactly 64 bytes");
_Static_assert(sizeof(dgtl_vector_path_instance_t) == 64, "dgtl_vector_path_instance_t must be exactly 64 bytes");
_Static_assert(sizeof(dgtl_raw_input_event_t) == 64, "dgtl_raw_input_event_t must be exactly 64 bytes");
#endif

DGTL_API const char* DGTL_CALL dgtl_sdk_gpu_version(void) noexcept;
DGTL_API void*    DGTL_CALL dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* title) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_resize(void* ctx_ptr, uint32_t width, uint32_t height, float dpi_scale) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_destroy_pipeline(void* ctx_ptr) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_begin_frame(void* ctx_ptr, float r, float g, float b, float a) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_end_frame(void* ctx_ptr) noexcept;
DGTL_API uint32_t DGTL_CALL dgtl_gpu_push_clip_rect(void* ctx_ptr, float x, float y, float w, float h) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_pop_clip_rect(void* ctx_ptr) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_render_quad_instances(void* ctx_ptr, const dgtl_quad_instance_t* instances, uint32_t count) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_render_glyph_instances(void* ctx_ptr, uint32_t atlas_slot, const dgtl_glyph_gpu_instance_t* instances, uint32_t count) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_render_vector_paths(void* ctx_ptr, const dgtl_vector_path_instance_t* instances, uint32_t count) noexcept;
DGTL_API uint32_t DGTL_CALL dgtl_gpu_upload_texture(void* ctx_ptr, uint32_t width, uint32_t height, const uint8_t* rgba) noexcept;
DGTL_API void     DGTL_CALL dgtl_gpu_destroy_texture(void* ctx_ptr, uint32_t texture_slot) noexcept;
DGTL_API bool     DGTL_CALL dgtl_platform_poll_events(void* ctx_ptr, dgtl_raw_input_event_t* out_event) noexcept;
DGTL_API uint64_t DGTL_CALL dgtl_platform_get_time_us(void) noexcept;
DGTL_API void     DGTL_CALL dgtl_platform_set_cursor(void* ctx_ptr, uint32_t cursor_type) noexcept;

/* ============================================================================
 * SECTION 11: COMPILATION SESSIONS, AOT/JIT & NATIVE LINKERS
 * ============================================================================ */

typedef struct dgtl_session dgtl_session_t;

typedef enum dgtl_session_mode {
    DGTL_SESSION_MODE_JIT    = 0,
    DGTL_SESSION_MODE_AOT    = 1,
    DGTL_SESSION_MODE_HYBRID = 2
} dgtl_session_mode_t;

typedef enum dgtl_session_opt_level {
    DGTL_SESSION_OPT_O0      = 0,
    DGTL_SESSION_OPT_O1      = 1,
    DGTL_SESSION_OPT_O2      = 2,
    DGTL_SESSION_OPT_O3      = 3,
    DGTL_SESSION_OPT_OSTRIKE = 4
} dgtl_session_opt_level_t;

typedef enum dgtl_host_symbol_flags {
    DGTL_HOST_SYMBOL_BIND_BY_NAME = 0x1,
    DGTL_HOST_SYMBOL_BIND_BY_SLOT = 0x2
} dgtl_host_symbol_flags_t;

typedef struct dgtl_session_config {
    dgtl_session_mode_t      mode;
    dgtl_session_opt_level_t opt_level;
    uint64_t                 timeout_us;
    int32_t                  enable_temporal_fusion;
    int32_t                  enable_polyhedral_scop;
    int32_t                  enable_speculation;
    int32_t                  enable_vectorization;
    int32_t                  enable_deterministic_replay;
    uint32_t                 osr_threshold_invocations;
    uint32_t                 background_worker_threads;
    uint32_t                 preferred_numa_node;
    uint32_t                 reserved[5];
} dgtl_session_config_t;

typedef struct dgtl_host_symbol {
    const char* name;
    uint32_t    slot;
    uintptr_t   address;
    uint32_t    flags;
} dgtl_host_symbol_t;

typedef struct dgtl_callee_binding {
    uint32_t    slot;
    const char* symbol_name;
} dgtl_callee_binding_t;

DGTL_API dgtl_session_config_t DGTL_CALL dgtl_session_config_default(void);
DGTL_API const char*           DGTL_CALL dgtl_session_mode_name(dgtl_session_mode_t mode);
DGTL_API dgtl_session_t*       DGTL_CALL dgtl_session_create(const dgtl_session_config_t* cfg);
DGTL_API void                  DGTL_CALL dgtl_session_destroy(dgtl_session_t* session);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol(dgtl_session_t* s, const char* name, uintptr_t addr);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol_slot(dgtl_session_t* s, uint32_t slot, uintptr_t addr);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbols(dgtl_session_t* s, const dgtl_host_symbol_t* syms, size_t count);
DGTL_API void          DGTL_CALL dgtl_session_clear_host_symbols(dgtl_session_t* s);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_compile_stream(dgtl_session_t* session, dgtl_stream_t* stream);
DGTL_API uint64_t      DGTL_CALL dgtl_session_get_symbol_address(dgtl_session_t* session, const char* symbol_name);
DGTL_API uint64_t      DGTL_CALL dgtl_session_get_symbol_address_by_slot(dgtl_session_t* session, uint32_t slot);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_object(dgtl_session_t* session, const char* output_path);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_binary(dgtl_session_t* session, const char* output_path);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_i64(dgtl_session_t* session, const char* symbol, int64_t* out_result);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_f64(dgtl_session_t* session, const char* symbol, double* out_result);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_ptr(dgtl_session_t* session, const char* symbol, void** out_result);
DGTL_API void*         DGTL_CALL dgtl_session_get_function_pointer(dgtl_session_t* session, const char* symbol);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_i64_args(dgtl_session_t* session, const char* symbol, const int64_t* args, size_t arg_count, int64_t* out_result);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_f64_args(dgtl_session_t* session, const char* symbol, const double* args, size_t arg_count, double* out_result);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_ptr_args(dgtl_session_t* session, const char* symbol, const void* const* args, size_t arg_count, void** out_result);

/* Direct Native Binary Emitters */
DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target);
DGTL_API dgtl_status_t        DGTL_CALL dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target, const char* output_path);
DGTL_API dgtl_status_t        DGTL_CALL dgtl_emit_pe_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API dgtl_status_t        DGTL_CALL dgtl_emit_elf_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API dgtl_status_t        DGTL_CALL dgtl_emit_macho_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API void                 DGTL_CALL dgtl_free_binary_buffer(void* buf);

/* String Interning Registry */
DGTL_API uint32_t    DGTL_CALL dgtl_intern_string_handle_utf8(const char* data, size_t len);
DGTL_API const char* DGTL_CALL dgtl_try_lookup_string_handle(uint32_t handle, size_t* out_len);
DGTL_API void        DGTL_CALL dgtl_register_string_handle(uint32_t handle, const char* data, size_t len);

/* Callee Bindings */
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slot(dgtl_stream_t* stream, uint32_t slot, const char* symbol_name);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slots(dgtl_stream_t* stream, const dgtl_callee_binding_t* bindings, size_t count);

/* ============================================================================
 * SECTION 12: CAPABILITIES, NUMA TOPOLOGY & SMT VERIFICATION
 * ============================================================================ */

#define DGTL_CAPABILITY_SCHEMA_VERSION 2u

#define DGTL_CAP_FEATURE_JIT                    (1ull << 0)
#define DGTL_CAP_FEATURE_AOT                    (1ull << 1)
#define DGTL_CAP_FEATURE_HYBRID                 (1ull << 2)
#define DGTL_CAP_FEATURE_HOST_BIND_NAME         (1ull << 3)
#define DGTL_CAP_FEATURE_HOST_BIND_SLOT         (1ull << 4)
#define DGTL_CAP_FEATURE_OBJECT_EMISSION        (1ull << 5)
#define DGTL_CAP_FEATURE_BINARY_EMISSION        (1ull << 6)
#define DGTL_CAP_FEATURE_IN_PROCESS_EXEC        (1ull << 7)
#define DGTL_CAP_FEATURE_CFG_INTROSPECTION      (1ull << 8)
#define DGTL_CAP_FEATURE_PER_PASS_CONTROL       (1ull << 9)
#define DGTL_CAP_FEATURE_SPECULATIVE_CONST      (1ull << 10)
#define DGTL_CAP_FEATURE_OSR                    (1ull << 11)
#define DGTL_CAP_FEATURE_DEOPT_RECOVERY         (1ull << 12)
#define DGTL_CAP_FEATURE_POLYHEDRAL_SCOP        (1ull << 13)
#define DGTL_CAP_FEATURE_LINEAR_BORROW_CHECK    (1ull << 14)
#define DGTL_CAP_FEATURE_SMT_FORMAL_VERIFY      (1ull << 15)
#define DGTL_CAP_FEATURE_NUMA_AWARE             (1ull << 16)
#define DGTL_CAP_FEATURE_WX_MEMORY_PROTECT      (1ull << 17)

/* SIMD Capability Tiers */
#define DGTL_CAP_SIMD_SCALAR                    0x0000u
#define DGTL_CAP_SIMD_AVX2                      0x0001u
#define DGTL_CAP_SIMD_AVX512                    0x0002u
#define DGTL_CAP_SIMD_ARM_NEON                  0x0004u
#define DGTL_CAP_SIMD_ARM_SVE                   0x0008u
#define DGTL_CAP_SIMD_RISCV_V                   0x0010u
#define DGTL_CAP_SIMD_WASM128                   0x0020u

typedef struct dgtl_capabilities {
    uint32_t schema_version;
    uint32_t supported_simd_tiers;
    uint64_t feature_mask;
    uint64_t backend_target_mask;
    uint64_t optimization_profile_mask;
    uint32_t max_call_arity;
    uint32_t cache_line_bytes;
    uint32_t numa_node_count;
    uint32_t physical_core_count;
    uint32_t reserved[6];
} dgtl_capabilities_t;

DGTL_API dgtl_capabilities_t DGTL_CALL dgtl_get_capabilities(void);
DGTL_API bool                DGTL_CALL dgtl_capabilities_support_target(const dgtl_capabilities_t* caps, dgtl_target_t target);
DGTL_API bool                DGTL_CALL dgtl_capabilities_has_feature(const dgtl_capabilities_t* caps, uint64_t feature_flag);

/* System NUMA Controller */
DGTL_API uint32_t DGTL_CALL dgtl_numa_node_count(void);
DGTL_API uint32_t DGTL_CALL dgtl_numa_current_node(void);
DGTL_API int32_t  DGTL_CALL dgtl_numa_pin_thread_to_node(uint32_t node_id);
DGTL_API int32_t  DGTL_CALL dgtl_numa_pin_thread_to_core(uint32_t core_id);
DGTL_API void*    DGTL_CALL dgtl_numa_alloc_on_node(size_t size, uint32_t node_id);
DGTL_API void     DGTL_CALL dgtl_numa_free_on_node(void* ptr, size_t size);

/* Mathematical SMT Verification & Source Mapping */
DGTL_API int32_t DGTL_CALL dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* err_buf, size_t err_buf_size);
DGTL_API int32_t DGTL_CALL dgtl_verify_pass_equivalence(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass);
DGTL_API size_t  DGTL_CALL dgtl_export_smtlib2_script(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass, char* out, size_t sz);
DGTL_API void    DGTL_CALL dgtl_record_source_location(dgtl_stream_t* s, size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <vector>
#include <type_traits>
#include <utility>

#ifdef alloca
#  undef alloca
#endif
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

namespace dgtl {
namespace sdk {

/**
 * @class Context
 * @brief High-performance thread-safe memory and compilation manager.
 */
class Context {
public:
    Context() : handle_(dgtl_context_create()) {
        if (!handle_) throw std::bad_alloc();
    }
    Context(size_t worker_threads, size_t arena_mb_per_worker)
        : handle_(dgtl_context_create_custom(worker_threads, arena_mb_per_worker)) {
        if (!handle_) throw std::bad_alloc();
    }
    ~Context() {
        if (handle_) {
            dgtl_context_destroy(handle_);
            handle_ = nullptr;
        }
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }
    Context& operator=(Context&& o) noexcept {
        if (this != &o) {
            if (handle_) dgtl_context_destroy(handle_);
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] dgtl_context_t* get() const noexcept { return handle_; }
    [[nodiscard]] dgtl_context_t* handle() const noexcept { return handle_; }

private:
    dgtl_context_t* handle_ = nullptr;
};

/**
 * @class Stream
 * @brief Linear transactional IR stream container.
 */
class Stream {
public:
    explicit Stream(Context& ctx, size_t reserved_nodes = 1024)
        : stream_(dgtl_stream_create(ctx.handle(), reserved_nodes)) {
        if (!stream_) throw std::bad_alloc();
    }
    explicit Stream(dgtl_context_t* ctx_raw, size_t reserved_nodes = 1024)
        : stream_(dgtl_stream_create(ctx_raw, reserved_nodes)) {
        if (!stream_) throw std::bad_alloc();
    }
    ~Stream() {
        if (stream_) {
            dgtl_stream_destroy(stream_);
            stream_ = nullptr;
        }
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&& o) noexcept : stream_(o.stream_) { o.stream_ = nullptr; }
    Stream& operator=(Stream&& o) noexcept {
        if (this != &o) {
            if (stream_) dgtl_stream_destroy(stream_);
            stream_ = o.stream_;
            o.stream_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] dgtl_stream_t* get() const noexcept { return stream_; }
    [[nodiscard]] dgtl_stream_t* handle() const noexcept { return stream_; }

    [[nodiscard]] size_t size() const noexcept { return dgtl_stream_node_count(stream_); }
    [[nodiscard]] uint64_t hash() const noexcept { return dgtl_stream_logic_hash(stream_); }
    void reset() noexcept { dgtl_stream_reset(stream_); }
    bool reserve(size_t additional) noexcept { return dgtl_stream_reserve(stream_, additional); }
    dgtl_status_t compact() noexcept { return dgtl_stream_compact(stream_); }
    dgtl_status_t optimize() noexcept { return dgtl_optimize(stream_); }
    dgtl_status_t optimize(dgtl_opt_profile_t prof) noexcept { return dgtl_optimize_with_profile(stream_, prof); }

    void begin_transaction() noexcept { dgtl_stream_begin_transaction(stream_); }
    bool commit_transaction() noexcept { return dgtl_stream_commit_transaction(stream_); }
    void rollback_transaction() noexcept { dgtl_stream_rollback_transaction(stream_); }

    void bind_callee_slot(uint32_t slot, const char* symbol_name) noexcept {
        dgtl_stream_bind_callee_slot(stream_, slot, symbol_name);
    }

private:
    dgtl_stream_t* stream_ = nullptr;
};

/**
 * @class Builder
 * @brief Fluid, ergonomic SSA code emission builder designed for compiler frontends.
 */
class Builder {
public:
    explicit Builder(Stream& stream) noexcept : s_(stream.handle()) {}
    explicit Builder(dgtl_stream_t* stream) noexcept : s_(stream) {}

    [[nodiscard]] dgtl_stream_t* stream() const noexcept { return s_; }

    // Immediates
    uint32_t const_i64(uint64_t val) noexcept { return dgtl_emit_const_i64(s_, val); }
    uint32_t const_f32(float val) noexcept { return dgtl_emit_const_f32(s_, val); }
    uint32_t const_f64(double val) noexcept { return dgtl_emit_const_f64(s_, val); }
    uint32_t const_str(const char* str) noexcept { return dgtl_emit_string_const_utf8(s_, str); }
    uint32_t const_str(std::string_view sv) noexcept {
        return dgtl_emit_string_const_handle(s_, dgtl_intern_string_handle_utf8(sv.data(), sv.size()));
    }

    // Memory & Addresses
    uint32_t load(uint32_t ptr) noexcept { return dgtl_emit_load(s_, ptr); }
    uint32_t store(uint32_t ptr, uint32_t val) noexcept { return dgtl_emit_store(s_, ptr, val); }
    uint32_t gep(uint32_t base, uint32_t offset) noexcept { return dgtl_emit_gep(s_, base, offset); }
    uint32_t ptr_to_int(uint32_t ptr) noexcept { return dgtl_emit_ptr_to_int(s_, ptr); }
    uint32_t int_to_ptr(uint32_t val) noexcept { return dgtl_emit_int_to_ptr(s_, val); }
    uint32_t ptr_cast(uint32_t ptr, dgtl_type_code_t ty) noexcept { return dgtl_emit_ptr_cast(s_, ptr, ty); }
    uint32_t ptr_diff(uint32_t a, uint32_t b) noexcept { return dgtl_emit_ptr_diff(s_, a, b); }
    uint32_t reg_move(uint32_t dst, uint32_t src) noexcept { return dgtl_emit_reg_move(s_, dst, src); }
    uint32_t (alloca)(uint32_t size, uint32_t align = 8) noexcept { return dgtl_emit_alloca(s_, size, align); }
    uint32_t emit_alloca(uint32_t size, uint32_t align = 8) noexcept { return dgtl_emit_alloca(s_, size, align); }

    // 32-bit Integer ALU
    uint32_t add_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_add_i32(s_, a, b); }
    uint32_t sub_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_sub_i32(s_, a, b); }
    uint32_t mul_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_mul_i32(s_, a, b); }
    uint32_t div_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_div_i32(s_, a, b); }
    uint32_t mod_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_mod_i32(s_, a, b); }
    uint32_t and_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_and_i32(s_, a, b); }
    uint32_t or_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_or_i32(s_, a, b); }
    uint32_t xor_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_xor_i32(s_, a, b); }
    uint32_t shl_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_shl_i32(s_, a, b); }
    uint32_t shr_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_shr_i32(s_, a, b); }
    uint32_t neg_i32(uint32_t a) noexcept { return dgtl_emit_neg_i32(s_, a); }
    uint32_t not_i32(uint32_t a) noexcept { return dgtl_emit_not_i32(s_, a); }

    // 64-bit Integer ALU
    uint32_t add_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_add_i64(s_, a, b); }
    uint32_t sub_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_sub_i64(s_, a, b); }
    uint32_t mul_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_mul_i64(s_, a, b); }
    uint32_t div_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_div_i64(s_, a, b); }
    uint32_t mod_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_mod_i64(s_, a, b); }
    uint32_t and_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_and_i64(s_, a, b); }
    uint32_t or_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_or_i64(s_, a, b); }
    uint32_t xor_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_xor_i64(s_, a, b); }
    uint32_t shl_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_shl_i64(s_, a, b); }
    uint32_t shr_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_shr_i64(s_, a, b); }
    uint32_t neg_i64(uint32_t a) noexcept { return dgtl_emit_neg_i64(s_, a); }
    uint32_t not_i64(uint32_t a) noexcept { return dgtl_emit_not_i64(s_, a); }

    // Floating-Point ALU
    uint32_t fadd(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fadd(s_, a, b); }
    uint32_t fsub(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fsub(s_, a, b); }
    uint32_t fmul(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fmul(s_, a, b); }
    uint32_t fdiv(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fdiv(s_, a, b); }
    uint32_t neg_f32(uint32_t a) noexcept { return dgtl_emit_neg_f32(s_, a); }
    uint32_t fadd_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fadd_f64(s_, a, b); }
    uint32_t fsub_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fsub_f64(s_, a, b); }
    uint32_t fmul_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fmul_f64(s_, a, b); }
    uint32_t fdiv_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_fdiv_f64(s_, a, b); }
    uint32_t neg_f64(uint32_t a) noexcept { return dgtl_emit_neg_f64(s_, a); }

    // Comparisons (i32)
    uint32_t cmp_eq_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_eq_i32(s_, a, b); }
    uint32_t cmp_ne_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ne_i32(s_, a, b); }
    uint32_t cmp_lt_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_lt_i32(s_, a, b); }
    uint32_t cmp_le_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_le_i32(s_, a, b); }
    uint32_t cmp_gt_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_gt_i32(s_, a, b); }
    uint32_t cmp_ge_i32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ge_i32(s_, a, b); }

    // Comparisons (i64)
    uint32_t cmp_eq_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_eq_i64(s_, a, b); }
    uint32_t cmp_ne_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ne_i64(s_, a, b); }
    uint32_t cmp_lt_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_lt_i64(s_, a, b); }
    uint32_t cmp_le_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_le_i64(s_, a, b); }
    uint32_t cmp_gt_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_gt_i64(s_, a, b); }
    uint32_t cmp_ge_i64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ge_i64(s_, a, b); }

    // Comparisons (f32)
    uint32_t cmp_eq_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_eq_f32(s_, a, b); }
    uint32_t cmp_ne_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ne_f32(s_, a, b); }
    uint32_t cmp_lt_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_lt_f32(s_, a, b); }
    uint32_t cmp_le_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_le_f32(s_, a, b); }
    uint32_t cmp_gt_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_gt_f32(s_, a, b); }
    uint32_t cmp_ge_f32(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ge_f32(s_, a, b); }

    // Comparisons (f64)
    uint32_t cmp_eq_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_eq_f64(s_, a, b); }
    uint32_t cmp_ne_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ne_f64(s_, a, b); }
    uint32_t cmp_lt_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_lt_f64(s_, a, b); }
    uint32_t cmp_le_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_le_f64(s_, a, b); }
    uint32_t cmp_gt_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_gt_f64(s_, a, b); }
    uint32_t cmp_ge_f64(uint32_t a, uint32_t b) noexcept { return dgtl_emit_cmp_ge_f64(s_, a, b); }

    // Selection
    uint32_t select(uint32_t cond, uint32_t true_val, uint32_t false_val) noexcept {
        return dgtl_emit_select(s_, cond, true_val, false_val);
    }
    uint32_t bool_select(uint32_t cond, uint32_t true_val, uint32_t false_val) noexcept {
        return dgtl_emit_bool_select(s_, cond, true_val, false_val);
    }

    // Control Flow
    uint32_t label(const char* name) noexcept { return dgtl_emit_label(s_, name); }
    uint32_t label_slot(uint32_t slot) noexcept { return dgtl_emit_label_slot(s_, slot); }
    uint32_t function_label(uint32_t slot) noexcept { return dgtl_emit_function_label_slot(s_, slot); }
    uint32_t function_label_with_params(uint32_t slot, const uint32_t* params, size_t count) noexcept {
        return dgtl_emit_function_label_with_params(s_, slot, params, count);
    }
    void branch(uint32_t target) noexcept { dgtl_emit_branch(s_, target); }
    void branch_cond(uint32_t cond, uint32_t target) noexcept { dgtl_emit_branch_cond(s_, cond, target); }
    void branch_predictive(uint32_t cond, uint32_t t_label, uint32_t f_label, uint8_t weight) noexcept {
        dgtl_emit_branch_predictive(s_, cond, t_label, f_label, weight);
    }
    void ret(uint32_t val) noexcept { dgtl_emit_return(s_, val); }

    // Calls
    uint32_t call(uint32_t callee, const uint32_t* args = nullptr, size_t count = 0) noexcept {
        return dgtl_emit_call(s_, callee, args, count);
    }
    uint32_t call(uint32_t callee, const std::vector<uint32_t>& args) noexcept {
        return dgtl_emit_call(s_, callee, args.data(), args.size());
    }
    uint32_t call_with_result(uint32_t result, uint32_t callee, const uint32_t* args = nullptr, size_t count = 0) noexcept {
        return dgtl_emit_call_with_result(s_, result, callee, args, count);
    }
    uint32_t call_with_result(uint32_t result, uint32_t callee, const std::vector<uint32_t>& args) noexcept {
        return dgtl_emit_call_with_result(s_, result, callee, args.data(), args.size());
    }

    // Concurrency & Async
    uint32_t async_spawn(uint32_t fn, uint32_t arg) noexcept { return dgtl_emit_async_spawn(s_, fn, arg); }
    uint32_t async_await(uint32_t task) noexcept { return dgtl_emit_async_await(s_, task); }
    uint32_t chan_send(uint32_t chan, uint32_t val) noexcept { return dgtl_emit_chan_send(s_, chan, val); }
    uint32_t chan_recv(uint32_t chan) noexcept { return dgtl_emit_chan_recv(s_, chan); }
    uint32_t barrier_sync(uint32_t barrier) noexcept { return dgtl_emit_barrier_sync(s_, barrier); }

private:
    dgtl_stream_t* s_ = nullptr;
};

/**
 * @class Session
 * @brief High-performance JIT/AOT compilation session with zero-overhead native function invocation.
 */
class Session {
public:
    Session() : Session(dgtl_session_config_default()) {}
    explicit Session(const dgtl_session_config_t& cfg)
        : session_(dgtl_session_create(&cfg)) {
        if (!session_) throw std::bad_alloc();
    }
    ~Session() {
        if (session_) {
            dgtl_session_destroy(session_);
            session_ = nullptr;
        }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&& o) noexcept : session_(o.session_) { o.session_ = nullptr; }
    Session& operator=(Session&& o) noexcept {
        if (this != &o) {
            if (session_) dgtl_session_destroy(session_);
            session_ = o.session_;
            o.session_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] dgtl_session_t* get() const noexcept { return session_; }
    [[nodiscard]] dgtl_session_t* handle() const noexcept { return session_; }

    dgtl_status_t register_symbol(const char* name, uintptr_t addr) noexcept {
        return dgtl_session_register_host_symbol(session_, name, addr);
    }
    dgtl_status_t register_symbol(const std::string& name, uintptr_t addr) noexcept {
        return dgtl_session_register_host_symbol(session_, name.c_str(), addr);
    }
    dgtl_status_t register_slot(uint32_t slot, uintptr_t addr) noexcept {
        return dgtl_session_register_host_symbol_slot(session_, slot, addr);
    }

    dgtl_status_t compile(Stream& stream) noexcept {
        return dgtl_session_compile_stream(session_, stream.handle());
    }
    dgtl_status_t compile(dgtl_stream_t* stream) noexcept {
        return dgtl_session_compile_stream(session_, stream);
    }

    [[nodiscard]] uint64_t get_symbol_address(const char* name) const noexcept {
        return dgtl_session_get_symbol_address(session_, name);
    }
    [[nodiscard]] void* get_function_pointer(const char* name) const noexcept {
        return dgtl_session_get_function_pointer(session_, name);
    }

    template <typename Signature>
    [[nodiscard]] auto get_function(const char* name) const noexcept -> Signature* {
        return reinterpret_cast<Signature*>(get_function_pointer(name));
    }

    template <typename Ret, typename... Args>
    Ret invoke(const char* name, Args... args) {
        auto fn = get_function<Ret(Args...)>(name);
        if (!fn) throw std::runtime_error(std::string("DGTL JIT symbol not found: ") + name);
        return fn(std::forward<Args>(args)...);
    }

    dgtl_status_t emit_object(const char* path) noexcept {
        return dgtl_session_emit_object(session_, path);
    }
    dgtl_status_t emit_binary(const char* path) noexcept {
        return dgtl_session_emit_binary(session_, path);
    }

    dgtl_status_t run_entry_i64(const char* symbol, int64_t* out_res) noexcept {
        return dgtl_session_run_entry_i64(session_, symbol, out_res);
    }
    dgtl_status_t run_entry_f64(const char* symbol, double* out_res) noexcept {
        return dgtl_session_run_entry_f64(session_, symbol, out_res);
    }
    dgtl_status_t run_entry_ptr(const char* symbol, void** out_res) noexcept {
        return dgtl_session_run_entry_ptr(session_, symbol, out_res);
    }

private:
    dgtl_session_t* session_ = nullptr;
};

} // namespace sdk

// Transparent namespace aliases for frontend compiler writers
using sdk::Context;
using sdk::Stream;
using sdk::Builder;
using sdk::Session;

} // namespace dgtl

#endif /* __cplusplus */

#endif /* DGTL_SDK_H */