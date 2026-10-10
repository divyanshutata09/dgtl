/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL â€” Deep General-purpose Transformation Layer
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * DGTL is a high-velocity, frontend-agnostic compiler IR and execution SDK.
 * This header defines the authoritative, production-grade C17 ABI surface
 * and provides an ergonomic C++20 zero-overhead RAII abstraction layer.
 *
 * ABI Version: 4 (Upgraded to true Graph SSA & Universal ABI Marshalling)
 */

#ifndef DGTL_SDK_H
#define DGTL_SDK_H

/* ============================================================================
 * SECTION 0 â€” PLATFORM DETECTION, VISIBILITY & ATTRIBUTE MACROS
 * ========================================================================== */

#if defined(_WIN32) || defined(_WIN64)
#  if defined(DGTL_BUILD_SHARED)
#    define DGTL_API   __declspec(dllexport)
#  elif defined(DGTL_IMPORT_SHARED)
#    define DGTL_API   __declspec(dllimport)
#  else
#    define DGTL_API
#  endif
#  define DGTL_CALL    __cdecl
#  define DGTL_WEAK
#elif defined(__GNUC__) || defined(__clang__)
#  define DGTL_API     __attribute__((visibility("default")))
#  define DGTL_CALL
#  define DGTL_WEAK    __attribute__((weak))
#else
#  define DGTL_API
#  define DGTL_CALL
#  define DGTL_WEAK
#endif

/* Compiler-portable safety attributes */
#if defined(__GNUC__) || defined(__clang__)
#  define DGTL_WARN_UNUSED     __attribute__((warn_unused_result))
#  define DGTL_DEPRECATED(m)   __attribute__((deprecated(m)))
#  define DGTL_NONNULL(...)    
#  define DGTL_PURE            __attribute__((pure))
#  define DGTL_CONST_FN        __attribute__((const))
#  define DGTL_LIKELY(x)       __builtin_expect(!!(x), 1)
#  define DGTL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#  define DGTL_RETURNS_NONNULL __attribute__((returns_nonnull))
#elif defined(_MSC_VER)
#  define DGTL_WARN_UNUSED     _Check_return_
#  define DGTL_DEPRECATED(m)   __declspec(deprecated(m))
#  define DGTL_NONNULL(...)
#  define DGTL_PURE
#  define DGTL_CONST_FN
#  define DGTL_LIKELY(x)       (x)
#  define DGTL_UNLIKELY(x)     (x)
#  define DGTL_RETURNS_NONNULL
#else
#  define DGTL_WARN_UNUSED
#  define DGTL_DEPRECATED(m)
#  define DGTL_NONNULL(...)
#  define DGTL_PURE
#  define DGTL_CONST_FN
#  define DGTL_LIKELY(x)       (x)
#  define DGTL_UNLIKELY(x)     (x)
#  define DGTL_RETURNS_NONNULL
#endif

#ifdef __cplusplus
#  define DGTL_NOEXCEPT    noexcept
#  define DGTL_NODISCARD   [[nodiscard]]
#else
#  define DGTL_NOEXCEPT
#  define DGTL_NODISCARD   DGTL_WARN_UNUSED
#endif

/* ============================================================================
 * SECTION 1 â€” C STANDARD INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================================
 * SECTION 2 â€” ABI VERSIONING & SENTINELS
 * ========================================================================== */

#define DGTL_VERSION_MAJOR      2u
#define DGTL_VERSION_MINOR      1u
#define DGTL_VERSION_PATCH      0u
#define DGTL_VERSION_SUFFIX     "-SOVEREIGN"
#define DGTL_VERSION_STRING     "2.1.0-SOVEREIGN"
#define DGTL_ABI_VERSION        4u
#define DGTL_ABI_MIN_COMPAT     3u

#define DGTL_INVALID_SLOT       (0xFFFFFFFFu)

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SECTION 3 â€” TYPE-SAFE OPAQUE HANDLES
 * ========================================================================== */

typedef struct { uint32_t id; } dgtl_value_t;
typedef struct { uint32_t id; } dgtl_block_t;
typedef struct { uint32_t id; } dgtl_type_t;
typedef struct { uint32_t id; } dgtl_func_t;
typedef struct { uint32_t id; } dgtl_str_t;
typedef struct { uint32_t id; } dgtl_pass_t;

#ifdef __cplusplus
#  define DGTL_INVALID_VALUE  (dgtl_value_t{0xFFFFFFFFu})
#  define DGTL_INVALID_BLOCK  (dgtl_block_t{0xFFFFFFFFu})
#  define DGTL_INVALID_TYPE   (dgtl_type_t {0xFFFFFFFFu})
#  define DGTL_INVALID_FUNC   (dgtl_func_t {0xFFFFFFFFu})
#  define DGTL_INVALID_STR    (dgtl_str_t  {0xFFFFFFFFu})
#  define DGTL_INVALID_PASS   (dgtl_pass_t {0xFFFFFFFFu})
#else
#  define DGTL_INVALID_VALUE  ((dgtl_value_t){0xFFFFFFFFu})
#  define DGTL_INVALID_BLOCK  ((dgtl_block_t){0xFFFFFFFFu})
#  define DGTL_INVALID_TYPE   ((dgtl_type_t ){0xFFFFFFFFu})
#  define DGTL_INVALID_FUNC   ((dgtl_func_t ){0xFFFFFFFFu})
#  define DGTL_INVALID_STR    ((dgtl_str_t  ){0xFFFFFFFFu})
#  define DGTL_INVALID_PASS   ((dgtl_pass_t ){0xFFFFFFFFu})
#endif

static inline bool dgtl_value_is_valid(dgtl_value_t v) { return v.id != 0xFFFFFFFFu; }
static inline bool dgtl_block_is_valid(dgtl_block_t b) { return b.id != 0xFFFFFFFFu; }
static inline bool dgtl_type_is_valid(dgtl_type_t  t) { return t.id != 0xFFFFFFFFu && t.id != 0u; }
static inline bool dgtl_func_is_valid(dgtl_func_t  f) { return f.id != 0xFFFFFFFFu; }
static inline bool dgtl_pass_is_valid(dgtl_pass_t  p) { return p.id != 0xFFFFFFFFu && p.id != 0u; }

/* ============================================================================
 * SECTION 4 â€” OPAQUE STRUCT DECLARATIONS
 * ========================================================================== */

typedef struct dgtl_context          dgtl_context_t;
typedef struct dgtl_stream           dgtl_stream_t;
typedef struct dgtl_session          dgtl_session_t;
typedef struct dgtl_runtime_context  dgtl_runtime_context_t;
typedef struct dgtl_func_builder     dgtl_func_builder_t;

/* ============================================================================
 * SECTION 5 â€” CANONICAL TYPE SYSTEM DESCRIPTORS
 * ========================================================================== */

#ifdef __cplusplus
#  define DGTL_MKTYPE(n)  (dgtl_type_t{(uint32_t)(n)})
#else
#  define DGTL_MKTYPE(n)  ((dgtl_type_t){(uint32_t)(n)})
#endif

#define DGTL_TYPE_VOID        DGTL_MKTYPE(0)
#define DGTL_TYPE_BOOL        DGTL_MKTYPE(1)
#define DGTL_TYPE_PRED        DGTL_MKTYPE(2)
#define DGTL_TYPE_PTR         DGTL_MKTYPE(4)
#define DGTL_TYPE_STR         DGTL_MKTYPE(5)
#define DGTL_TYPE_I4          DGTL_MKTYPE(6)
#define DGTL_TYPE_I8          DGTL_MKTYPE(7)
#define DGTL_TYPE_I16         DGTL_MKTYPE(8)
#define DGTL_TYPE_I32         DGTL_MKTYPE(9)
#define DGTL_TYPE_I64         DGTL_MKTYPE(10)
#define DGTL_TYPE_I128        DGTL_MKTYPE(11)
#define DGTL_TYPE_I256        DGTL_MKTYPE(12)
#define DGTL_TYPE_I512        DGTL_MKTYPE(13)
#define DGTL_TYPE_F8          DGTL_MKTYPE(14)
#define DGTL_TYPE_BF16        DGTL_MKTYPE(15)
#define DGTL_TYPE_F16         DGTL_MKTYPE(16)
#define DGTL_TYPE_F32         DGTL_MKTYPE(17)
#define DGTL_TYPE_F64         DGTL_MKTYPE(18)
#define DGTL_TYPE_F128        DGTL_MKTYPE(19)
#define DGTL_TYPE_VEC3_F32    DGTL_MKTYPE(20)
#define DGTL_TYPE_VEC4_F32    DGTL_MKTYPE(21)
#define DGTL_TYPE_MAT4_F32    DGTL_MKTYPE(22)
#define DGTL_TYPE_QUAT_F32    DGTL_MKTYPE(23)
#define DGTL_TYPE_VECF32X16   DGTL_MKTYPE(24)
#define DGTL_TYPE_VECI32X16   DGTL_MKTYPE(25)
#define DGTL_TYPE_VECF16X32   DGTL_MKTYPE(26)
#define DGTL_TYPE_TENSOR_FP8  DGTL_MKTYPE(27)
#define DGTL_TYPE_TENSOR_BF16 DGTL_MKTYPE(28)
#define DGTL_TYPE_TENSOR_F32  DGTL_MKTYPE(29)
#define DGTL_TYPE_TENSOR_I64  DGTL_MKTYPE(30)

static inline bool dgtl_type_is_integer(dgtl_type_t t) { return t.id >= 6u && t.id <= 13u; }
static inline bool dgtl_type_is_float(dgtl_type_t t)   { return t.id >= 14u && t.id <= 19u; }
static inline bool dgtl_type_is_vector(dgtl_type_t t)  { return t.id >= 20u && t.id <= 26u; }
static inline bool dgtl_type_is_tensor(dgtl_type_t t)  { return t.id >= 27u && t.id <= 30u; }

/* ============================================================================
 * SECTION 6 â€” ERROR & DIAGNOSTIC CONTRACT
 * ========================================================================== */

typedef enum dgtl_severity {
    DGTL_SEV_INFO    = 0,
    DGTL_SEV_WARNING = 1,
    DGTL_SEV_ERROR   = 2,
    DGTL_SEV_FATAL   = 3
} dgtl_severity_t;

typedef struct dgtl_err {
    int32_t         code;
    dgtl_severity_t severity;
} dgtl_err_t;

typedef dgtl_err_t dgtl_status_t;

#ifdef __cplusplus
#  define DGTL_OK          (dgtl_err_t{0, DGTL_SEV_INFO})
#  define DGTL_ERR(c)      (dgtl_err_t{(int32_t)(c), DGTL_SEV_ERROR})
#  define DGTL_WARN(c)     (dgtl_err_t{(int32_t)(c), DGTL_SEV_WARNING})
#else
#  define DGTL_OK          ((dgtl_err_t){0, DGTL_SEV_INFO})
#  define DGTL_ERR(c)      ((dgtl_err_t){(int32_t)(c), DGTL_SEV_ERROR})
#  define DGTL_WARN(c)     ((dgtl_err_t){(int32_t)(c), DGTL_SEV_WARNING})
#endif

#define DGTL_IS_OK(e)      ((e).code == 0)
#define DGTL_IS_ERR(e)     ((e).code != 0)

DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_err_message(dgtl_err_t err) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_get_last_error(void) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_clear_error(void) DGTL_NOEXCEPT;

typedef struct dgtl_diag {
    dgtl_severity_t severity;
    uint32_t        node_index;
    uint32_t        source_line;
    uint16_t        source_col;
    uint16_t        code;
    const char*     message;
} dgtl_diag_t;

#define DGTL_DIAG_OK                  0x0000u
#define DGTL_DIAG_TYPE_MISMATCH       0x0001u
#define DGTL_DIAG_INVALID_OPERAND     0x0002u
#define DGTL_DIAG_UNSUPPORTED_OP      0x0003u
#define DGTL_DIAG_UNREACHABLE_BLOCK   0x0004u
#define DGTL_DIAG_PHI_PRED_MISMATCH   0x0005u
#define DGTL_DIAG_MISSING_TERMINATOR  0x0006u
#define DGTL_DIAG_OVERFLOW_ARGS       0x0010u
#define DGTL_DIAG_NULL_PARAM          0x0020u
#define DGTL_DIAG_SIMD_WIDTH_MISMATCH 0x0030u

/* ============================================================================
 * SECTION 7 â€” UNIVERSAL CALL MARSHALLER (Solves ABI & Register Corruption)
 * ========================================================================== */

typedef enum dgtl_abi_arg_kind {
    DGTL_ARG_I64    = 0,
    DGTL_ARG_F32    = 1,
    DGTL_ARG_F64    = 2,
    DGTL_ARG_PTR    = 3,
    DGTL_ARG_VEC512 = 4
} dgtl_abi_arg_kind_t;

typedef struct dgtl_abi_arg {
    dgtl_abi_arg_kind_t kind;
    union {
        int64_t  i64;
        float    f32;
        double   f64;
        void*    ptr;
        uint8_t  vec512[64];
    } val;
} dgtl_abi_arg_t;

typedef struct dgtl_call_args {
    const dgtl_abi_arg_t* args;
    size_t                count;
} dgtl_call_args_t;

/* ============================================================================
 * SECTION 8 â€” COMPARISON & MEMORY PREDICATES
 * ========================================================================== */

typedef enum dgtl_icmp_pred {
    DGTL_ICMP_EQ  = 0,
    DGTL_ICMP_NE  = 1,
    DGTL_ICMP_SLT = 2,
    DGTL_ICMP_SLE = 3,
    DGTL_ICMP_SGT = 4,
    DGTL_ICMP_SGE = 5,
    DGTL_ICMP_ULT = 6,
    DGTL_ICMP_ULE = 7,
    DGTL_ICMP_UGT = 8,
    DGTL_ICMP_UGE = 9
} dgtl_icmp_pred_t;

typedef enum dgtl_fcmp_pred {
    DGTL_FCMP_OEQ = 0,
    DGTL_FCMP_ONE = 1,
    DGTL_FCMP_OLT = 2,
    DGTL_FCMP_OLE = 3,
    DGTL_FCMP_OGT = 4,
    DGTL_FCMP_OGE = 5,
    DGTL_FCMP_UEQ = 6,
    DGTL_FCMP_UNE = 7,
    DGTL_FCMP_ULT = 8,
    DGTL_FCMP_ULE = 9,
    DGTL_FCMP_UGT = 10,
    DGTL_FCMP_UGE = 11,
    DGTL_FCMP_ORD = 12,
    DGTL_FCMP_UNO = 13
} dgtl_fcmp_pred_t;

typedef enum dgtl_memory_order {
    DGTL_MO_RELAXED  = 0,
    DGTL_MO_ACQUIRE  = 1,
    DGTL_MO_RELEASE  = 2,
    DGTL_MO_ACQ_REL  = 3,
    DGTL_MO_SEQ_CST  = 4
} dgtl_memory_order_t;

/* ============================================================================
 * SECTION 9 â€” FLAGS
 * ========================================================================== */

#define DGTL_FLAG_NONE             0x0000u
#define DGTL_FLAG_IS_CONST         0x0001u
#define DGTL_FLAG_IS_DEAD          0x0002u
#define DGTL_FLAG_HAS_SIDE_EFFECTS 0x0004u
#define DGTL_FLAG_IS_VOLATILE      0x0008u
#define DGTL_FLAG_IS_SIMD          0x0010u
#define DGTL_FLAG_IS_VECTOR_LANE   0x0020u
#define DGTL_FLAG_IS_PREDICATED    0x0040u
#define DGTL_FLAG_IS_FUSED         0x0080u
#define DGTL_FLAG_IS_OPTIMIZED     0x0100u
#define DGTL_FLAG_IS_LOOP_INV      0x0200u
#define DGTL_FLAG_IS_SPEC_CONST    0x0400u
#define DGTL_FLAG_NON_TEMPORAL     0x0800u
#define DGTL_FLAG_IS_BRANCH        0x1000u
#define DGTL_FLAG_IS_RETURN        0x2000u
#define DGTL_FLAG_IS_GUARD         0x4000u
#define DGTL_FLAG_MAY_DEOPT        0x8000u

#define DGTL_EXT_NONE              0x00000000u
#define DGTL_EXT_MEM_RELAXED       0x00000001u
#define DGTL_EXT_MEM_ACQUIRE       0x00000002u
#define DGTL_EXT_MEM_RELEASE       0x00000004u
#define DGTL_EXT_MEM_SEQ_CST       0x00000008u
#define DGTL_EXT_NO_ALIAS          0x00000010u
#define DGTL_EXT_MAY_ALIAS         0x00000020u
#define DGTL_EXT_FAST_MATH         0x00000040u
#define DGTL_EXT_STRICT_FP         0x00000080u
#define DGTL_EXT_LATENCY_CRITICAL  0x00000100u
#define DGTL_EXT_THROUGHPUT_MODE   0x00000200u
#define DGTL_EXT_PREFETCH_HINT     0x00000400u
#define DGTL_EXT_VECTORIZABLE      0x00000800u
#define DGTL_EXT_VEC_WIDTH_128     0x00001000u
#define DGTL_EXT_VEC_WIDTH_256     0x00002000u
#define DGTL_EXT_VEC_WIDTH_512     0x00004000u
#define DGTL_EXT_HOT_PATH          0x00010000u
#define DGTL_EXT_COLD_PATH         0x00020000u
#define DGTL_EXT_DETERMINISTIC     0x00040000u
#define DGTL_EXT_NON_BLOCKING      0x00080000u
#define DGTL_EXT_SPECULATION_SAFE  0x00100000u
#define DGTL_EXT_LOOP_CARRIED      0x00200000u
#define DGTL_EXT_COMMUTATIVE       0x00400000u
#define DGTL_EXT_ASSOCIATIVE       0x00800000u
#define DGTL_EXT_HFT_CRITICAL      0x01000000u
#define DGTL_EXT_GPU_KERNEL        0x02000000u
#define DGTL_EXT_WASM_EXPORTABLE   0x04000000u
#define DGTL_EXT_TENSOR_OP         0x08000000u

/* ============================================================================
 * SECTION 10 â€” OPCODES
 * ========================================================================== */

typedef uint16_t dgtl_opcode_t;

#define DGTL_OP_NOP                   0u
#define DGTL_OP_LOAD                  10u
#define DGTL_OP_STORE                 11u
#define DGTL_OP_REG_MOVE              12u
#define DGTL_OP_LOAD_IMM              13u
#define DGTL_OP_CONST                 13u
#define DGTL_OP_LEA                   14u
#define DGTL_OP_PREFETCH              15u
#define DGTL_OP_ATOMIC_LOAD           16u
#define DGTL_OP_ATOMIC_STORE          17u
#define DGTL_OP_ATOMIC_FENCE          18u
#define DGTL_OP_ADD_I8                19u
#define DGTL_OP_ADD_I32               20u
#define DGTL_OP_SUB_I32               21u
#define DGTL_OP_MUL_I32               22u
#define DGTL_OP_DIV_I32               23u
#define DGTL_OP_SHL_I32               24u
#define DGTL_OP_AND_I32               25u
#define DGTL_OP_OR_I32                26u
#define DGTL_OP_XOR_I32               27u
#define DGTL_OP_MOD_I32               28u
#define DGTL_OP_SHR_I32               29u
#define DGTL_OP_ADD_F32               30u
#define DGTL_OP_SUB_F32               31u
#define DGTL_OP_MUL_F32               32u
#define DGTL_OP_DIV_F32               33u
#define DGTL_OP_FMA_F32               35u
#define DGTL_OP_FMA_WIDEN_F16_F32     36u
#define DGTL_OP_FAST_FMA              0x8002u
#define DGTL_OP_CMP_EQ_I32            40u
#define DGTL_OP_CMP_NE_I32            41u
#define DGTL_OP_CMP_LT_I32            42u
#define DGTL_OP_CMP_LE_I32            43u
#define DGTL_OP_CMP_GT_I32            44u
#define DGTL_OP_CMP_GE_I32            45u
#define DGTL_OP_BIT_FIELD_INSERT      50u
#define DGTL_OP_BIT_FIELD_EXTRACT     51u
#define DGTL_OP_ADD_I64               60u
#define DGTL_OP_SUB_I64               61u
#define DGTL_OP_MUL_I64               62u
#define DGTL_OP_DIV_I64               63u
#define DGTL_OP_MOD_I64               64u
#define DGTL_OP_AND_I64               65u
#define DGTL_OP_OR_I64                66u
#define DGTL_OP_XOR_I64               67u
#define DGTL_OP_SHL_I64               68u
#define DGTL_OP_SHR_I64               69u
#define DGTL_OP_ADD_F64               70u
#define DGTL_OP_SUB_F64               71u
#define DGTL_OP_MUL_F64               72u
#define DGTL_OP_DIV_F64               73u
#define DGTL_OP_IMMEDIATE             80u
#define DGTL_OP_PTR_CAST              81u
#define DGTL_OP_PTR_DIFF              82u
#define DGTL_OP_BOOL_SELECT           83u
#define DGTL_OP_STRING_CONST          84u
#define DGTL_OP_STRING_LENGTH         85u
#define DGTL_OP_STRING_COMPARE        86u
#define DGTL_OP_STRING_CONCAT         87u
#define DGTL_OP_STRING_SUBSTRING      88u
#define DGTL_OP_UNDEF                 89u
#define DGTL_OP_GEP                   90u
#define DGTL_OP_PTR_TO_INT            91u
#define DGTL_OP_INT_TO_PTR            92u
#define DGTL_OP_LABEL                 98u
#define DGTL_OP_GOTO                  99u
#define DGTL_OP_BRANCH                100u
#define DGTL_OP_RET                   101u
#define DGTL_OP_BRANCH_COND           102u
#define DGTL_OP_BRANCH_FUSED          103u
#define DGTL_OP_BRANCH_COMPARE_FUSED  103u
#define DGTL_OP_CALL                  110u
#define DGTL_OP_SYSCALL               111u
#define DGTL_OP_GUARD_ALIAS           120u
#define DGTL_OP_ADD_I128              130u
#define DGTL_OP_ADD_I256              131u
#define DGTL_OP_ADD_F8                132u
#define DGTL_OP_ADD_BF16              133u
#define DGTL_OP_GUARD_VALUE           200u
#define DGTL_OP_DEOPT_TRIGGER         201u
#define DGTL_OP_PHI                   202u
#define DGTL_OP_ASSUME                203u
#define DGTL_OP_SELECT                204u
#define DGTL_OP_PHI_MULTI             0x0200u
#define DGTL_OP_TUPLE_PACK            0x0201u
#define DGTL_OP_TUPLE_UNPACK          0x0202u
#define DGTL_OP_SELECT_MULTI          0x0203u
#define DGTL_OP_CALL_VARIADIC         0x0204u
#define DGTL_OP_SWITCH_DISPATCH       0x0205u
#define DGTL_OP_VOLATILE_LOAD         0x0206u
#define DGTL_OP_VOLATILE_STORE        0x0207u
#define DGTL_OP_ACQUIRE_FENCE         0x0208u
#define DGTL_OP_RELEASE_FENCE         0x0209u
#define DGTL_OP_CMP_EQ_I64            0x0380u
#define DGTL_OP_CMP_NE_I64            0x0381u
#define DGTL_OP_CMP_LT_I64            0x0382u
#define DGTL_OP_CMP_LE_I64            0x0383u
#define DGTL_OP_CMP_GT_I64            0x0384u
#define DGTL_OP_CMP_GE_I64            0x0385u
#define DGTL_OP_ALLOCA                0x0386u
#define DGTL_OP_OWNERSHIP_ACQUIRE     410u
#define DGTL_OP_OWNERSHIP_RELEASE     411u
#define DGTL_OP_OWNERSHIP_TRANSFER    412u
#define DGTL_OP_ATOMIC_SNAPSHOT       413u
#define DGTL_OP_ASYNC_SPAWN           420u
#define DGTL_OP_ASYNC_AWAIT           421u
#define DGTL_OP_FIBER_YIELD           422u
#define DGTL_OP_BARRIER_SYNC          423u
#define DGTL_OP_CHAN_SEND             424u
#define DGTL_OP_CHAN_RECV             425u
#define DGTL_OP_LOCK_ACQUIRE          426u
#define DGTL_OP_LOCK_RELEASE          427u
#define DGTL_OP_MEMORY_FENCE          428u
#define DGTL_OP_THREAD_SPAWN          429u
#define DGTL_OP_GRAPH_BUILD           440u
#define DGTL_OP_GRAPH_TRAVERSE        441u
#define DGTL_OP_TREE_BUILD            442u
#define DGTL_OP_TREE_TRAVERSE         443u
#define DGTL_OP_TRIE_BUILD            444u
#define DGTL_OP_TRIE_LOOKUP           445u
#define DGTL_OP_TRIE_INSERT           446u
#define DGTL_OP_DSA_ALLOCATE          447u
#define DGTL_OP_DSA_DEALLOCATE        448u
#define DGTL_OP_SCOP_ANALYZE          449u
#define DGTL_OP_TENSOR_RESHAPE        460u
#define DGTL_OP_TENSOR_REDUCE         461u
#define DGTL_OP_TENSOR_TRANSPOSE      462u
#define DGTL_OP_TENSOR_SLICE          463u
#define DGTL_OP_TENSOR_CONCAT         464u
#define DGTL_OP_TENSOR_BROADCAST      465u
#define DGTL_OP_TENSOR_GEMM           466u
#define DGTL_OP_TENSOR_CONV           467u
#define DGTL_OP_TENSOR_POOL           468u
#define DGTL_OP_TENSOR_SOFTMAX        469u
#define DGTL_OP_MATMUL_BF16           0x0480u
#define DGTL_OP_MATMUL_FP8            0x0481u
#define DGTL_OP_SDPA_F16              0x0482u
#define DGTL_OP_SOFTMAX_FAST          0x0483u
#define DGTL_OP_LAYER_NORM            0x0484u
#define DGTL_OP_ROPE_EMBED            0x0485u
#define DGTL_OP_GATHER_F32_512        0x0500u
#define DGTL_OP_GATHER_16             0x0501u
#define DGTL_OP_STORE_V512            0x0502u
#define DGTL_OP_VEC_ADD_F32_512       0x0503u
#define DGTL_OP_VEC_MUL_F32_512       0x0504u
#define DGTL_OP_VEC_ADD_F64_512       0x0505u
#define DGTL_OP_VEC_MUL_F64_512       0x0506u
#define DGTL_OP_SIMD_BSL              0x0507u
#define DGTL_OP_FCVTL_F16_F32         0x0508u
#define DGTL_OP_FCVTN_F32_F16         0x0509u
#define DGTL_OP_VEC_LOAD              0x0510u
#define DGTL_OP_VEC_STORE             0x0511u
#define DGTL_OP_VEC_SHUFFLE           0x0512u
#define DGTL_OP_VEC_REDUCE_ADD        0x0513u
#define DGTL_OP_VEC_REDUCE_MAX        0x0514u
#define DGTL_OP_VEC_REDUCE_MIN        0x0515u
#define DGTL_OP_READ_TSC              0x0600u
#define DGTL_OP_BRANCH_LIKELY         0x0601u
#define DGTL_OP_BRANCH_UNLIKELY       0x0602u
#define DGTL_OP_CLFLUSH               0x0603u
#define DGTL_OP_PREFETCH_T0           0x0604u
#define DGTL_OP_SPIN_ACQUIRE          0x0605u
#define DGTL_OP_RING_PUSH_FREE        0x0606u
#define DGTL_OP_FAST_INV_SQRT         0x0700u
#define DGTL_OP_VEC3_CROSS            0x0701u
#define DGTL_OP_VEC4_DOT              0x0702u
#define DGTL_OP_MAT4_MUL              0x0703u
#define DGTL_OP_QUAT_SLERP            0x0704u
#define DGTL_OP_RAY_AABB_ISECT        0x0705u

/* ============================================================================
 * SECTION 11 â€” COMPILATION TARGETS
 * ========================================================================== */

typedef enum dgtl_target {
    DGTL_TARGET_X86_64        = 0,
    DGTL_TARGET_X86_64_AVX512 = 1,
    DGTL_TARGET_ARM64         = 2,
    DGTL_TARGET_ARM64_SVE     = 3,
    DGTL_TARGET_RISCV64       = 4,
    DGTL_TARGET_WASM          = 5,
    DGTL_TARGET_WASM_SIMD     = 6,
    DGTL_TARGET_CUDA_SM89     = 7,
    DGTL_TARGET_ROCM_GFX12    = 8,
    DGTL_TARGET_HOST          = 255
} dgtl_target_t;

/* ============================================================================
 * SECTION 12 â€” CORE STRUCTURES & PROFILING
 * ========================================================================== */

typedef struct alignas(8) dgtl_node {
    uint64_t      intent_hash;
    uint32_t      result_id;
    uint32_t      op_a;
    uint32_t      op_b;
    uint32_t      op_c;
    uint32_t      logic_flags;
    dgtl_opcode_t opcode;
    uint16_t      flags;
} dgtl_node_t;

static_assert(sizeof(dgtl_node_t) == 32, "dgtl_node_t must be exactly 32 bytes");

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

typedef enum dgtl_ir_mode {
    DGTL_IR_MODE_INT    = 0,
    DGTL_IR_MODE_FLOAT  = 1,
    DGTL_IR_MODE_TENSOR = 2,
    DGTL_IR_MODE_MIXED  = 3
} dgtl_ir_mode_t;

typedef enum dgtl_opt_level {
    DGTL_OPT_O0      = 0,
    DGTL_OPT_O1      = 1,
    DGTL_OPT_O2      = 2,
    DGTL_OPT_O3      = 3,
    DGTL_OPT_OSTRIKE = 4
} dgtl_opt_level_t;

typedef struct dgtl_emit_options {
    uint32_t inline_threshold;
    uint32_t unroll_factor;
    uint32_t vectorize_threshold;
    uint8_t  enable_cache_optimization;
    uint8_t  enable_branch_prediction;
    uint8_t  enable_numa_locality;
    uint8_t  enable_fast_math;
} dgtl_emit_options_t;

typedef struct dgtl_perf_metrics {
    uint64_t total_nodes;
    uint64_t eliminated_nodes;
    uint64_t fused_nodes;
    uint64_t vectorized_nodes;
    uint64_t inlined_calls;
    double   estimated_ipc;
    double   compilation_ms;
    uint64_t code_size_bytes;
    uint64_t logic_hash;
} dgtl_perf_metrics_t;

typedef enum dgtl_trace_kind {
    DGTL_TRACE_PASS_START = 0,
    DGTL_TRACE_PASS_END   = 1,
    DGTL_TRACE_JIT_START  = 2,
    DGTL_TRACE_JIT_END    = 3,
    DGTL_TRACE_ALLOC      = 4,
    DGTL_TRACE_FREE       = 5
} dgtl_trace_kind_t;

typedef struct dgtl_trace_event {
    dgtl_trace_kind_t kind;
    uint64_t          timestamp_ns;
    uint64_t          payload;
    const char*       label;
} dgtl_trace_event_t;

/* ============================================================================
 * SECTION 13 â€” SIMD TYPES
 * ========================================================================== */

typedef enum dgtl_simd_arch {
    DGTL_SIMD_ARCH_GENERIC         = 0,
    DGTL_SIMD_ARCH_X86_SSE42       = 1,
    DGTL_SIMD_ARCH_X86_AVX         = 2,
    DGTL_SIMD_ARCH_X86_AVX2        = 3,
    DGTL_SIMD_ARCH_X86_AVX512_FDQ  = 4,
    DGTL_SIMD_ARCH_X86_AVX512_FULL = 5,
    DGTL_SIMD_ARCH_ARM_NEON        = 6,
    DGTL_SIMD_ARCH_ARM_SVE         = 7,
    DGTL_SIMD_ARCH_WASM_SIMD128    = 8
} dgtl_simd_arch_t;

typedef enum dgtl_simd_width {
    DGTL_SIMD_WIDTH_128 = 16,
    DGTL_SIMD_WIDTH_256 = 32,
    DGTL_SIMD_WIDTH_512 = 64
} dgtl_simd_width_t;

typedef struct dgtl_simd_caps {
    dgtl_simd_arch_t native_arch;
    uint32_t         max_vector_width_bytes;
    uint32_t         native_vector_register_count;
    bool             has_fma;
    bool             has_native_gather;
    bool             has_native_scatter;
    bool             has_fp16_arithmetic;
    bool             has_bf16_arithmetic;
    bool             has_masked_operations;
    bool             has_transcendental_approximations;
    bool             _pad[1];
} dgtl_simd_caps_t;

typedef struct dgtl_gather_spec {
    uint32_t    base_ptr_slot;
    uint32_t    index_vector_slot;
    uint32_t    mask_slot;
    uint32_t    scale;
    dgtl_type_t element_type;
} dgtl_gather_spec_t;

typedef struct dgtl_scatter_spec {
    uint32_t base_ptr_slot;
    uint32_t index_vector_slot;
    uint32_t value_vector_slot;
    uint32_t mask_slot;
    uint32_t scale;
} dgtl_scatter_spec_t;

typedef struct dgtl_simd_legalize_opts {
    dgtl_simd_arch_t target_arch;
    bool             allow_scalarization;
    bool             allow_splitting;
    uint8_t          _pad[2];
} dgtl_simd_legalize_opts_t;

/* ============================================================================
 * SECTION 14 â€” SESSION CONFIGURATION
 * ========================================================================== */

typedef enum dgtl_session_mode {
    DGTL_SESSION_MODE_JIT    = 0,
    DGTL_SESSION_MODE_AOT    = 1,
    DGTL_SESSION_MODE_HYBRID = 2
} dgtl_session_mode_t;

typedef dgtl_opt_level_t dgtl_session_opt_level_t;

typedef struct dgtl_session_config {
    dgtl_session_mode_t      mode;
    dgtl_session_opt_level_t opt_level;
    dgtl_target_t            target;
    uint32_t                 jit_worker_threads;
    uint64_t                 code_cache_bytes;
    uint64_t                 timeout_us;
    bool                     enable_simd;
    bool                     enable_inliner;
    bool                     enable_profiling;
    bool                     enable_smt_verify;
    bool                     _pad[4];
} dgtl_session_config_t;

#define DGTL_HOST_SYMBOL_BIND_BY_NAME  0x01u
#define DGTL_HOST_SYMBOL_BIND_BY_SLOT  0x02u
#define DGTL_HOST_SYMBOL_WEAK          0x04u

typedef struct dgtl_host_symbol {
    const char* name;
    uintptr_t   address;
    uint32_t    slot;
    uint32_t    flags;
} dgtl_host_symbol_t;

typedef struct dgtl_callee_binding {
    uint32_t    callee_slot;
    const char* symbol_name;
    uintptr_t   address;
} dgtl_callee_binding_t;

/* ============================================================================
 * SECTION 15 â€” GPU/PLATFORM DRAW STRUCTURES
 * ========================================================================== */

typedef struct dgtl_quad_instance {
    float    x, y, w, h;
    float    r, g, b, a;
    float    corner_radius;
    float    border_width;
    uint32_t texture_slot;
    uint32_t _pad;
} dgtl_quad_instance_t;

typedef struct dgtl_glyph_gpu_instance {
    float    x, y;
    float    u, v;
    float    uw, vh;
    float    r, g, b, a;
    uint16_t glyph_id;
    uint16_t _pad;
} dgtl_glyph_gpu_instance_t;

typedef struct dgtl_vector_path_instance {
    float    x, y;
    float    scale;
    float    r, g, b, a;
    uint32_t path_slot;
} dgtl_vector_path_instance_t;

typedef struct dgtl_input_event {
    uint32_t type;
    uint32_t key_or_button;
    float    x, y;
    float    delta_x, delta_y;
    uint64_t timestamp_us;
    uint64_t modifiers;
} dgtl_input_event_t;

/* ============================================================================
 * SECTION 16 â€” VERSION & DIAGNOSTIC APIS
 * ========================================================================== */

typedef struct dgtl_version_info {
    uint32_t    major;
    uint32_t    minor;
    uint32_t    patch;
    uint32_t    abi_version;
    const char* build_id;
    bool        is_release_build;
    bool        _pad[7];
} dgtl_version_info_t;

DGTL_NODISCARD DGTL_API dgtl_version_info_t DGTL_CALL dgtl_get_version(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_abi_compatible(uint32_t client_abi_version) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_get_build_config(void) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 17 â€” CONTEXT APIS
 * ========================================================================== */

#ifdef __cplusplus
DGTL_NODISCARD DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create(size_t worker_threads = 0, size_t arena_mb_per_worker = 64) DGTL_NOEXCEPT;
#else
DGTL_NODISCARD DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create(size_t worker_threads, size_t arena_mb_per_worker) DGTL_NOEXCEPT;
#endif
DGTL_API void DGTL_CALL dgtl_context_destroy(dgtl_context_t* ctx) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_context_worker_count(const dgtl_context_t* ctx) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_context_set_verbose(dgtl_context_t* ctx, bool enabled) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 18 â€” STREAM & TRANSACTION APIS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_stream_t* DGTL_CALL dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_destroy(dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_reset(dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_stream_size(const dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_stream_max_slot(const dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_t* DGTL_NONNULL(3) out_node) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_stream_get_core_record(const dgtl_stream_t* s, size_t index, dgtl_core_record_t* DGTL_NONNULL(3) out_rec) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_emit_options(dgtl_stream_t* s, const dgtl_emit_options_t* opts) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_stream_diag_count(const dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_diag_t DGTL_CALL dgtl_stream_get_diag(const dgtl_stream_t* s, size_t index) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_clear_diags(dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_source_loc(dgtl_stream_t* s, uint32_t line, uint16_t col, const char* filename) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL dgtl_stream_set_call_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* args, size_t count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_stream_get_call_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_args, size_t max_out) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_operand_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* operands, size_t count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_stream_get_operand_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out, size_t max_out) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_result_ids(dgtl_stream_t* s, size_t node_idx, const uint32_t* ids, size_t count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_stream_get_result_ids(const dgtl_stream_t* s, size_t node_idx, uint32_t* out, size_t max_out) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_node_effects(dgtl_stream_t* s, size_t node_idx, uint32_t effect_set) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_stream_get_node_effects(const dgtl_stream_t* s, size_t node_idx) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 19 â€” FUNCTION & CONTROL FLOW GRAPH BUILDER
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_func_builder_t* DGTL_CALL dgtl_func_builder_create(dgtl_stream_t* DGTL_NONNULL(1) stream, const char* name, dgtl_type_t ret) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_func_builder_destroy(dgtl_func_builder_t* fb) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_func_add_param(dgtl_func_builder_t* DGTL_NONNULL(1) fb, dgtl_type_t type) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_func_param_count(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_func_get_param(const dgtl_func_builder_t* fb, uint32_t index) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_block_t DGTL_CALL dgtl_func_append_block(dgtl_func_builder_t* DGTL_NONNULL(1) fb, const char* name) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_func_set_insert_block(dgtl_func_builder_t* DGTL_NONNULL(1) fb, dgtl_block_t block) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_block_t DGTL_CALL dgtl_func_get_insert_block(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_func_seal_block(dgtl_func_builder_t* DGTL_NONNULL(1) fb, dgtl_block_t block) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_func_builder_finalize(dgtl_func_builder_t* DGTL_NONNULL(1) fb) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_func_builder_verify(const dgtl_func_builder_t* fb, char* err_buf, size_t err_buf_size) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL dgtl_func_set_value_type(dgtl_func_builder_t* DGTL_NONNULL(1) fb, dgtl_value_t val, dgtl_type_t type) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_type_t DGTL_CALL dgtl_func_get_value_type(const dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_func_diag_count(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_diag_t DGTL_CALL dgtl_func_get_diag(const dgtl_func_builder_t* fb, size_t index) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_func_clear_diags(dgtl_func_builder_t* DGTL_NONNULL(1) fb) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 20 â€” TYPE-DEDUCED SSA EMISSION APIS
 * ========================================================================== */

/* Arithmetic & Bitwise */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_add(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_sub(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_mul(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_div(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_rem(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_neg(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_and(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_or(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_xor(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_not(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_shl(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t amount) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_shr(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t amount) DGTL_NOEXCEPT;

/* Comparisons & Selection */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_icmp(dgtl_func_builder_t* fb, dgtl_icmp_pred_t pred, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fcmp(dgtl_func_builder_t* fb, dgtl_fcmp_pred_t pred, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_select(dgtl_func_builder_t* fb, dgtl_value_t cond, dgtl_value_t true_val, dgtl_value_t false_val) DGTL_NOEXCEPT;

/* Memory Access & Addressing */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_load(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_load_aligned(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr, uint32_t align) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_build_store(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_build_store_aligned(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr, uint32_t align) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_gep(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t offset) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_alloca(dgtl_func_builder_t* fb, dgtl_type_t type, uint32_t align) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_volatile_load(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_build_volatile_store(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr) DGTL_NOEXCEPT;

/* Casts */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_cast(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_type_t to_type) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_ptr_to_int(dgtl_func_builder_t* fb, dgtl_value_t ptr) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_int_to_ptr(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT;

/* Constant Pool */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_i64(dgtl_func_builder_t* fb, int64_t val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_i32(dgtl_func_builder_t* fb, int32_t val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_f32(dgtl_func_builder_t* fb, float val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_f64(dgtl_func_builder_t* fb, double val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_bool(dgtl_func_builder_t* fb, bool val) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_const_null(dgtl_func_builder_t* fb, dgtl_type_t type) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_str(dgtl_func_builder_t* fb, const char* utf8) DGTL_NOEXCEPT;

/* Control Flow & Graph SSA */
DGTL_API void DGTL_CALL dgtl_build_br(dgtl_func_builder_t* fb, dgtl_block_t target) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_cond_br(dgtl_func_builder_t* fb, dgtl_value_t cond, dgtl_block_t true_bb, dgtl_block_t false_bb) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_ret(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_ret_void(dgtl_func_builder_t* fb) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_phi(dgtl_func_builder_t* fb, dgtl_type_t type) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_phi_add_incoming(dgtl_func_builder_t* fb, dgtl_value_t phi, dgtl_value_t incoming_val, dgtl_block_t from_block) DGTL_NOEXCEPT;
DGTL_DEPRECATED("Use dgtl_phi_add_incoming")
DGTL_API void DGTL_CALL dgtl_build_phi_incoming(dgtl_func_builder_t* fb, dgtl_value_t phi, dgtl_value_t incoming_val, dgtl_block_t from_block) DGTL_NOEXCEPT;

/* Calls */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_call(dgtl_func_builder_t* fb, dgtl_func_t callee, const dgtl_value_t* args, size_t arg_count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_call_indirect(dgtl_func_builder_t* fb, dgtl_value_t callee_ptr, dgtl_type_t ret_type, const dgtl_value_t* args, size_t arg_count) DGTL_NOEXCEPT;

/* Fused & Vectorized Math */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fma(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fms(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fnmadd(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT;

/* Deep AI & Spatial Primitives */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_matmul(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t acc) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_softmax(dgtl_func_builder_t* fb, dgtl_value_t input) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_layer_norm(dgtl_func_builder_t* fb, dgtl_value_t x, dgtl_value_t gamma, dgtl_value_t beta) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_sdpa(dgtl_func_builder_t* fb, dgtl_value_t q, dgtl_value_t k, dgtl_value_t v, dgtl_value_t mask) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_rope_embed(dgtl_func_builder_t* fb, dgtl_value_t x) DGTL_NOEXCEPT;

/* SIMD & Gather/Scatter */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_load(dgtl_func_builder_t* fb, dgtl_value_t ptr, dgtl_type_t lane_type, uint32_t lane_count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_build_vec_store(dgtl_func_builder_t* fb, dgtl_value_t ptr, dgtl_value_t vec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_add(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_mul(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_add(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_max(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_min(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_shuffle(dgtl_func_builder_t* fb, dgtl_value_t vec, dgtl_value_t mask) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec_blend(dgtl_func_builder_t* fb, dgtl_value_t mask, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_gather(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t indices, dgtl_value_t mask) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_scatter(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t indices, dgtl_value_t vals, dgtl_value_t mask) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fcvtl_f16_f32(dgtl_func_builder_t* fb, dgtl_value_t f16_vec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fcvtn_f32_f16(dgtl_func_builder_t* fb, dgtl_value_t f32_vec) DGTL_NOEXCEPT;

/* Concurrency & Memory Safety */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_async_spawn(dgtl_func_builder_t* fb, dgtl_value_t fn, dgtl_value_t arg) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_async_await(dgtl_func_builder_t* fb, dgtl_value_t task) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_fence(dgtl_func_builder_t* fb, dgtl_memory_order_t order) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_own_acquire(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_own_release(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_own_transfer(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT;

/* HFT & Low Latency */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_read_tsc(dgtl_func_builder_t* fb) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_prefetch(dgtl_func_builder_t* fb, dgtl_value_t addr, uint32_t hint) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_build_clflush(dgtl_func_builder_t* fb, dgtl_value_t addr) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_spin_acquire(dgtl_func_builder_t* fb, dgtl_value_t lock_ptr) DGTL_NOEXCEPT;

/* Spatial & Vector Math */
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_fast_inv_sqrt(dgtl_func_builder_t* fb, dgtl_value_t x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec3_cross(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_vec4_dot(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_mat4_mul(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_quat_slerp(dgtl_func_builder_t* fb, dgtl_value_t q0, dgtl_value_t q1, dgtl_value_t t) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_value_t DGTL_CALL dgtl_build_ray_aabb_isect(dgtl_func_builder_t* fb, dgtl_value_t ray_origin, dgtl_value_t ray_dir, dgtl_value_t aabb_min, dgtl_value_t aabb_max) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 21 â€” PASS REGISTRY & EXECUTION APIS
 * ========================================================================== */

typedef dgtl_err_t (DGTL_CALL *dgtl_pass_fn_t)(dgtl_stream_t* s, void* user_data);

DGTL_NODISCARD DGTL_API dgtl_pass_t DGTL_CALL dgtl_pass_register(const char* DGTL_NONNULL(1) name, dgtl_pass_fn_t fn, void* user_data) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_pass_run(dgtl_pass_t pass, dgtl_stream_t* DGTL_NONNULL(2) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_pass_run_by_name(const char* DGTL_NONNULL(1) name, dgtl_stream_t* DGTL_NONNULL(2) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_pass_exists(const char* name) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char** DGTL_CALL dgtl_pass_list(size_t* DGTL_NONNULL(1) out_count) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 22 â€” OPTIMIZATION ENGINE APIS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_optimize(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_optimize_with_profile(dgtl_stream_t* DGTL_NONNULL(1) s, dgtl_opt_level_t level) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_dce(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_gvn(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_loop_fusion(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_inliner(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_escape_analysis(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_run_error_recovery(dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_auto_optimize(dgtl_stream_t* s, bool enabled) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_stream_set_opt_level(dgtl_stream_t* s, dgtl_opt_level_t level) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 23 â€” DIRECT BINARY SYNTHESIS
 * ========================================================================== */

/*
 * These APIs expose the sovereign in-memory binary emitters already provided
 * by the SDK implementation. The caller owns the returned buffer and must
 * release it with dgtl_free_binary_buffer().
 */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL
dgtl_emit_pe_binary_in_memory(
    dgtl_stream_t* DGTL_NONNULL(1) s,
    void** DGTL_NONNULL(2) out_buf,
    size_t* DGTL_NONNULL(3) out_size) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL
dgtl_emit_elf_binary_in_memory(
    dgtl_stream_t* DGTL_NONNULL(1) s,
    void** DGTL_NONNULL(2) out_buf,
    size_t* DGTL_NONNULL(3) out_size) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL
dgtl_emit_macho_binary_in_memory(
    dgtl_stream_t* DGTL_NONNULL(1) s,
    void** DGTL_NONNULL(2) out_buf,
    size_t* DGTL_NONNULL(3) out_size) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL
dgtl_free_binary_buffer(void* buf) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_opt_level_t DGTL_CALL dgtl_stream_get_opt_level(const dgtl_stream_t* s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_perf_metrics_t DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 23 â€” DIRECT-TO-BINARY STRIKE APIS
 * ========================================================================== */

typedef struct dgtl_strike_result {
    int32_t  ok;
    size_t   code_size;
    uint64_t logic_hash;
    double   compilation_latency_ms;
    char     message[128];
} dgtl_strike_result_t;

DGTL_NODISCARD DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream(dgtl_stream_t* DGTL_NONNULL(1) s, dgtl_target_t target) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 24 â€” JIT / AOT / HYBRID SESSIONS & MARSHALLED RUNNERS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_session_config_t DGTL_CALL dgtl_session_config_default(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_session_t* DGTL_CALL dgtl_session_create(const dgtl_session_config_t* config) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_session_destroy(dgtl_session_t* session) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_session_mode_name(dgtl_session_mode_t mode) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_register_symbol(dgtl_session_t* DGTL_NONNULL(1) session, const char* DGTL_NONNULL(2) symbol_name, uintptr_t address) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_register_symbols(dgtl_session_t* DGTL_NONNULL(1) session, const dgtl_host_symbol_t* DGTL_NONNULL(2) symbols, size_t count) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_session_clear_symbols(dgtl_session_t* DGTL_NONNULL(1) session) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_compile(dgtl_session_t* DGTL_NONNULL(1) session, dgtl_stream_t* DGTL_NONNULL(2) stream) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_session_get_symbol_address(dgtl_session_t* DGTL_NONNULL(1) session, const char* DGTL_NONNULL(2) name) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API void* DGTL_CALL dgtl_session_get_function_ptr(dgtl_session_t* DGTL_NONNULL(1) session, const char* DGTL_NONNULL(2) name) DGTL_NOEXCEPT;

/* Universal ABI Marshaller */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_run_marshalled(dgtl_session_t* DGTL_NONNULL(1) session, const char* entry_symbol, const dgtl_call_args_t* call_args, dgtl_abi_arg_t* DGTL_NONNULL(4) out_result) DGTL_NOEXCEPT;

/* Fast Homogeneous Overloads */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_run_i64(dgtl_session_t* DGTL_NONNULL(1) session, const char* entry_symbol, const int64_t* args, size_t arg_count, int64_t* DGTL_NONNULL(5) out_result) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_run_f64(dgtl_session_t* DGTL_NONNULL(1) session, const char* entry_symbol, const double* args, size_t arg_count, double* DGTL_NONNULL(5) out_result) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_run_ptr(dgtl_session_t* DGTL_NONNULL(1) session, const char* entry_symbol, const void* const* args, size_t arg_count, void** DGTL_NONNULL(5) out_result) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_emit_object(dgtl_session_t* DGTL_NONNULL(1) session, const char* DGTL_NONNULL(2) output_path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_session_emit_binary(dgtl_session_t* DGTL_NONNULL(1) session, const char* DGTL_NONNULL(2) output_path) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 25 â€” SIMD LEGALIZATION APIS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_simd_caps_t DGTL_CALL dgtl_simd_query_host_caps(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_simd_arch_is_supported(dgtl_simd_arch_t arch) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_legalize_simd(dgtl_stream_t* DGTL_NONNULL(1) s, dgtl_simd_arch_t arch) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_legalize_simd_ex(dgtl_stream_t* DGTL_NONNULL(1) s, const dgtl_simd_legalize_opts_t* DGTL_NONNULL(2) opts) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_simd_native_lane_count(dgtl_simd_arch_t arch, dgtl_type_t type) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 26 â€” IR TEXT, CFG & INTROSPECTION APIS
 * ========================================================================== */

typedef struct dgtl_text {
    char*  data;
    size_t length;
} dgtl_text_t;

DGTL_NODISCARD DGTL_API dgtl_text_t DGTL_CALL dgtl_stream_print_ir(const dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_text_t DGTL_CALL dgtl_stream_print_cfg_dot(const dgtl_stream_t* DGTL_NONNULL(1) s) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_free_text(dgtl_text_t text) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_node_is_terminator(dgtl_opcode_t op, uint16_t flags) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_node_has_side_effects(dgtl_opcode_t op, uint16_t flags) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL dgtl_node_inlining_benefit(dgtl_opcode_t op) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_opcode_name(dgtl_opcode_t op) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 27 â€” SERIALIZATION & INTEGRITY APIS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* DGTL_NONNULL(1) s, void** DGTL_NONNULL(2) out_blob, size_t* DGTL_NONNULL(3) out_size) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* DGTL_NONNULL(1) s, const void* DGTL_NONNULL(2) blob, size_t blob_size) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_free_blob(void* blob) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int32_t DGTL_CALL dgtl_binary_validate(const uint8_t* bytes, size_t len) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 28 â€” HARDWARE TOPOLOGY & NUMA APIS
 * ========================================================================== */

typedef struct dgtl_capabilities {
    int32_t  has_avx512;
    int32_t  has_avx2;
    int32_t  has_amx;
    int32_t  has_fma;
    int32_t  has_bmi2;
    uint32_t numa_node_count;
    uint32_t core_count;
    uint64_t l3_cache_bytes;
} dgtl_capabilities_t;

DGTL_NODISCARD DGTL_API dgtl_capabilities_t DGTL_CALL dgtl_query_capabilities(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_numa_pin_thread(uint32_t numa_node) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_numa_current_node(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_numa_node_count(void) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 29 â€” CONTEXT-ANCHORED STRING INTERNING
 * ========================================================================== */

DGTL_NODISCARD DGTL_API dgtl_str_t DGTL_CALL dgtl_str_intern(dgtl_context_t* DGTL_NONNULL(1) ctx, const char* DGTL_NONNULL(2) utf8, size_t len) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_str_t DGTL_CALL dgtl_str_intern_cstr(dgtl_context_t* DGTL_NONNULL(1) ctx, const char* DGTL_NONNULL(2) utf8) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_str_get(dgtl_context_t* DGTL_NONNULL(1) ctx, dgtl_str_t handle) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_str_len(dgtl_context_t* DGTL_NONNULL(1) ctx, dgtl_str_t handle) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 30 â€” SMT FORMAL VERIFICATION
 * ========================================================================== */

typedef struct dgtl_smt_result {
    bool   proved_equivalent;
    bool   has_counterexample;
    double verification_ms;
    char   solver_name[32];
    char   message[128];
} dgtl_smt_result_t;

DGTL_NODISCARD DGTL_API dgtl_smt_result_t DGTL_CALL dgtl_smt_verify(const dgtl_stream_t* original, const dgtl_stream_t* optimized, const char* solver_name) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 31 â€” ASYNC RUNTIME & SCHEDULER APIS
 * ========================================================================== */

typedef void (*dgtl_runtime_task_fn)(void* arg);

DGTL_NODISCARD DGTL_API dgtl_runtime_context_t* DGTL_CALL dgtl_runtime_context_create(size_t worker_threads, size_t arena_mb_per_worker) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_runtime_spawn(dgtl_runtime_context_t* DGTL_NONNULL(1) ctx, dgtl_runtime_task_fn fn, void* arg) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_runtime_join(dgtl_runtime_context_t* DGTL_NONNULL(1) ctx, uint64_t task_handle) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_runtime_yield(dgtl_runtime_context_t* ctx) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_join_task(uint64_t handle) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_yield(void) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 32 â€” GPU & PLATFORM WEAK SUBSYSTEMS
 * ========================================================================== */

DGTL_API void* DGTL_CALL dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* title) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_resize(void* ctx, uint32_t width, uint32_t height, float dpi_scale) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_destroy_pipeline(void* ctx) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_begin_frame(void* ctx, float r, float g, float b, float a) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_end_frame(void* ctx) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_gpu_push_clip_rect(void* ctx, float x, float y, float w, float h) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_pop_clip_rect(void* ctx) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_render_quads(void* ctx, const dgtl_quad_instance_t* instances, uint32_t count) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_render_glyphs(void* ctx, uint32_t atlas_slot, const dgtl_glyph_gpu_instance_t* instances, uint32_t count) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_render_vector_paths(void* ctx, const dgtl_vector_path_instance_t* instances, uint32_t count) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_gpu_upload_texture(void* ctx, uint32_t width, uint32_t height, const uint8_t* rgba) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_gpu_destroy_texture(void* ctx, uint32_t texture_slot) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_platform_poll_event(void* ctx, dgtl_input_event_t* DGTL_NONNULL(2) out_event) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_platform_time_us(void) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_platform_set_cursor(void* ctx, uint32_t cursor_type) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_gpu_sdk_version(void) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 33 â€” LOW-LEVEL EMISSION & LINKER DIAGNOSTICS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_emit_raw(dgtl_stream_t* DGTL_NONNULL(1) s, dgtl_opcode_t opcode, uint16_t flags, uint32_t op_a, uint32_t op_b, uint32_t op_c) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_emit_raw_with_slot(dgtl_stream_t* DGTL_NONNULL(1) s, dgtl_opcode_t opcode, uint16_t flags, uint32_t result_id, uint32_t op_a, uint32_t op_b, uint32_t op_c) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_set_verbose(int32_t enabled) DGTL_NOEXCEPT;

/*
 * dgtl_emit_call — emit one IR CALL node using DGTL's canonical call layout
 * (identical to dgtl::api::IRBuilder::emit_call):
 *
 *   opcode      = DGTL_OP_CALL (0x6E)
 *   flags       = DGTL_FLAG_HAS_SIDE_EFFECTS | DGTL_FLAG_IS_VOLATILE
 *   result_id   = result_slot (if DGTL_INVALID_SLOT, a fresh stream slot is
 *                 allocated; the call result is always node.result_id)
 *   op_a        = callee_slot
 *   op_b        = args[0]      or DGTL_INVALID_SLOT when arg_count < 1
 *   op_c        = args[1]      or DGTL_INVALID_SLOT when arg_count < 2
 *   logic_flags = args[2]      or DGTL_INVALID_SLOT when arg_count < 3
 *   call overflow sidecar (dgtl_stream_get_call_overflow(node_idx)) =
 *                 args[3 .. arg_count-1]; only set when arg_count > 3.
 *
 * Argument i of the call is therefore: i==0 -> op_b, i==1 -> op_c,
 * i==2 -> logic_flags, i>=3 -> overflow[i-3]. The number of arguments is
 * the count of leading non-DGTL_INVALID_SLOT values in that sequence.
 * Returns false (and appends nothing) on a null stream, an invalid callee,
 * or args == NULL with arg_count > 0.
 */
DGTL_API bool DGTL_CALL dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, uint32_t result_slot, const uint32_t* args, size_t arg_count) DGTL_NOEXCEPT;

/*
 * dgtl_emit_function_params — attach the formal parameter list to the
 * function entry LABEL that was just emitted.
 *
 * DGTL IR has no PARAM/ARG opcode. Parameters are expressed as values
 * DEFINED by the function's entry LABEL node via the multi-result sidecar:
 *
 *   precondition: the LAST node in the stream is a DGTL_OP_LABEL node
 *                 (the function entry label; emit this right after it).
 *   effect:       dgtl_stream_set_result_ids(label_node_idx, param_slots, count)
 *                 i.e. result_ids[i] == slot holding formal parameter i
 *                 (ABI argument position i). The LABEL's own result_id /
 *                 op_a..op_c are left untouched. No new IR node is appended.
 *
 * count == 0 is a valid no-op (returns true). Returns false on a null
 * stream, param_slots == NULL with count > 0, any DGTL_INVALID_SLOT entry,
 * or when the last node is not a LABEL.
 */
DGTL_API bool DGTL_CALL dgtl_emit_function_params(dgtl_stream_t* s, const uint32_t* param_slots, size_t count) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 24C â€” CONVENIENCE EMISSION & INTROSPECTION HELPERS
 * ========================================================================== */

static inline uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, int64_t val) {
    uint32_t lo = (uint32_t)((uint64_t)val & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(((uint64_t)val >> 32) & 0xFFFFFFFFu);
    return dgtl_emit_raw(s, DGTL_OP_CONST, DGTL_FLAG_IS_CONST, lo, hi, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_const_f64(dgtl_stream_t* s, double val) {
    uint64_t u = 0;
    memcpy(&u, &val, sizeof(u));
    uint32_t lo = (uint32_t)(u & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)((u >> 32) & 0xFFFFFFFFu);
    return dgtl_emit_raw(s, DGTL_OP_CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, lo, hi, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_const_f32(dgtl_stream_t* s, float val) {
    uint32_t u = 0;
    memcpy(&u, &val, sizeof(u));
    return dgtl_emit_raw(s, DGTL_OP_CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, u, 0, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_ADD_I32, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_SUB_I32, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_MUL_I32, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_DIV_I32, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_ADD_I64, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_SUB_I64, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return dgtl_emit_raw(s, DGTL_OP_MUL_I64, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_ret(dgtl_stream_t* s, uint32_t val) {
    return dgtl_emit_raw(s, DGTL_OP_RET, DGTL_FLAG_IS_RETURN, val, 0, 0);
}

static inline uint32_t dgtl_emit_ret_void(dgtl_stream_t* s) {
    return dgtl_emit_raw(s, DGTL_OP_RET, DGTL_FLAG_IS_RETURN, 0, 0, 0);
}

static inline uint32_t dgtl_emit_load(dgtl_stream_t* s, uint32_t ptr) {
    return dgtl_emit_raw(s, DGTL_OP_LOAD, DGTL_FLAG_NONE, ptr, 0, DGTL_INVALID_SLOT);
}

static inline uint32_t dgtl_emit_store(dgtl_stream_t* s, uint32_t ptr, uint32_t val) {
    return dgtl_emit_raw(s, DGTL_OP_STORE, DGTL_FLAG_HAS_SIDE_EFFECTS, ptr, val, DGTL_INVALID_SLOT);
}

static inline const char* dgtl_version_string(void) {
    return DGTL_VERSION_STRING;
}

static inline size_t dgtl_stream_node_count(const dgtl_stream_t* s) {
    return dgtl_stream_size(s);
}
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_last_linker_fallback_count(void) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_last_linker_fallback_message(size_t index, char* out_buffer, size_t out_size) DGTL_NOEXCEPT;

/* ============================================================================
 * SECTION 35 â€” MODULAR SOVEREIGN SUBSYSTEM EXTENSIONS
 * ========================================================================== */
#include "types.h"
#include "target.h"
#include "object.h"
#include "linker.h"
#include "jit.h"
#include "module.h"
#include "mir.h"

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ============================================================================
 * SECTION 34 â€” C++20 ERGONOMIC RAII FACADE
 * ========================================================================== */

#ifdef __cplusplus

#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dgtl {

struct Value {
    dgtl_value_t v;
    constexpr Value() noexcept : v{0xFFFFFFFFu} {}
    constexpr explicit Value(dgtl_value_t vv) noexcept : v(vv) {}
    constexpr explicit Value(uint32_t id) noexcept : v{id} {}
    [[nodiscard]] bool valid() const noexcept { return dgtl_value_is_valid(v); }
    [[nodiscard]] constexpr uint32_t id() const noexcept { return v.id; }
    [[nodiscard]] constexpr dgtl_value_t raw() const noexcept { return v; }
};

struct Block {
    dgtl_block_t b;
    constexpr Block() noexcept : b{0xFFFFFFFFu} {}
    constexpr explicit Block(dgtl_block_t bb) noexcept : b(bb) {}
    [[nodiscard]] bool valid() const noexcept { return dgtl_block_is_valid(b); }
    [[nodiscard]] constexpr uint32_t id() const noexcept { return b.id; }
    [[nodiscard]] constexpr dgtl_block_t raw() const noexcept { return b; }
};

class Context;
class Stream;
class TargetMachine;

class Context {
public:
    Context() : ctx_(dgtl_context_create(0, 0)) {
        if (!ctx_) throw std::bad_alloc();
    }
    explicit Context(size_t workers, size_t arena_mb = 64)
        : ctx_(dgtl_context_create(workers, arena_mb)) {
        if (!ctx_) throw std::bad_alloc();
    }
    ~Context() noexcept { if (ctx_) dgtl_context_destroy(ctx_); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&& o) noexcept : ctx_(o.ctx_) { o.ctx_ = nullptr; }
    Context& operator=(Context&& o) noexcept {
        if (this != &o) { if (ctx_) dgtl_context_destroy(ctx_); ctx_ = o.ctx_; o.ctx_ = nullptr; }
        return *this;
    }
    [[nodiscard]] dgtl_context_t* get() const noexcept { return ctx_; }
    void set_verbose(bool v) noexcept { dgtl_context_set_verbose(ctx_, v); }
private:
    dgtl_context_t* ctx_ = nullptr;
};

class Stream {
public:
    explicit Stream(Context& ctx, size_t reserve = 256)
        : s_(dgtl_stream_create(ctx.get(), reserve)) {
        if (!s_) throw std::bad_alloc();
    }
    explicit Stream(size_t reserve = 256)
        : s_(dgtl_stream_create(nullptr, reserve)) {
        if (!s_) throw std::bad_alloc();
    }
    ~Stream() noexcept { if (s_) dgtl_stream_destroy(s_); }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&& o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Stream& operator=(Stream&& o) noexcept {
        if (this != &o) { if (s_) dgtl_stream_destroy(s_); s_ = o.s_; o.s_ = nullptr; }
        return *this;
    }
    [[nodiscard]] dgtl_stream_t* get() const noexcept { return s_; }
    [[nodiscard]] size_t size() const noexcept { return dgtl_stream_size(s_); }
    void reset() noexcept { dgtl_stream_reset(s_); }
    [[nodiscard]] dgtl_text_t print_ir() const noexcept { return dgtl_stream_print_ir(s_); }
    [[nodiscard]] dgtl_text_t print_cfg_dot() const noexcept { return dgtl_stream_print_cfg_dot(s_); }
    [[nodiscard]] dgtl_err_t optimize() noexcept { return dgtl_optimize(s_); }
    [[nodiscard]] dgtl_err_t run_dce() noexcept { return dgtl_run_dce(s_); }
    [[nodiscard]] dgtl_err_t run_gvn() noexcept { return dgtl_run_gvn(s_); }
    [[nodiscard]] dgtl_strike_result_t strike(dgtl_target_t t = DGTL_TARGET_HOST) noexcept {
        return dgtl_strike_stream(s_, t);
    }
    [[nodiscard]] dgtl_text_t disassemble(const TargetMachine& tm) const noexcept;
    [[nodiscard]] dgtl_text_t dump_machine_ir(const TargetMachine& tm) const noexcept;
private:
    dgtl_stream_t* s_ = nullptr;
};

class FuncBuilder {
public:
    FuncBuilder(Stream& stream, const char* name, dgtl_type_t ret = DGTL_TYPE_VOID)
        : fb_(dgtl_func_builder_create(stream.get(), name, ret)) {
        if (!fb_) throw std::bad_alloc();
    }
    ~FuncBuilder() noexcept { if (fb_) dgtl_func_builder_destroy(fb_); }
    FuncBuilder(const FuncBuilder&) = delete;
    FuncBuilder& operator=(const FuncBuilder&) = delete;
    FuncBuilder(FuncBuilder&& o) noexcept : fb_(o.fb_) { o.fb_ = nullptr; }
    FuncBuilder& operator=(FuncBuilder&& o) noexcept {
        if (this != &o) { if (fb_) dgtl_func_builder_destroy(fb_); fb_ = o.fb_; o.fb_ = nullptr; }
        return *this;
    }

    [[nodiscard]] dgtl_func_builder_t* get() const noexcept { return fb_; }

    [[nodiscard]] Value param(dgtl_type_t type) noexcept { return Value(dgtl_func_add_param(fb_, type)); }
    [[nodiscard]] Block append_block(const char* name = "") noexcept { return Block(dgtl_func_append_block(fb_, name)); }
    void set_insert_block(Block bb) noexcept { dgtl_func_set_insert_block(fb_, bb.b); }
    [[nodiscard]] Block get_insert_block() const noexcept { return Block(dgtl_func_get_insert_block(fb_)); }
    void seal_block(Block bb) noexcept { dgtl_func_seal_block(fb_, bb.b); }
    void set_type(Value v, dgtl_type_t t) noexcept { dgtl_func_set_value_type(fb_, v.v, t); }
    [[nodiscard]] dgtl_type_t get_type(Value v) const noexcept { return dgtl_func_get_value_type(fb_, v.v); }

    [[nodiscard]] dgtl_err_t finalize() noexcept { return dgtl_func_builder_finalize(fb_); }
    [[nodiscard]] dgtl_err_t verify(char* buf = nullptr, size_t sz = 0) const noexcept {
        return dgtl_func_builder_verify(fb_, buf, sz);
    }

    /* Core Arithmetic */
    [[nodiscard]] Value add(Value l, Value r) noexcept { return Value(dgtl_build_add(fb_, l.v, r.v)); }
    [[nodiscard]] Value sub(Value l, Value r) noexcept { return Value(dgtl_build_sub(fb_, l.v, r.v)); }
    [[nodiscard]] Value mul(Value l, Value r) noexcept { return Value(dgtl_build_mul(fb_, l.v, r.v)); }
    [[nodiscard]] Value div(Value l, Value r) noexcept { return Value(dgtl_build_div(fb_, l.v, r.v)); }
    [[nodiscard]] Value rem(Value l, Value r) noexcept { return Value(dgtl_build_rem(fb_, l.v, r.v)); }
    [[nodiscard]] Value neg(Value v) noexcept { return Value(dgtl_build_neg(fb_, v.v)); }
    [[nodiscard]] Value band(Value l, Value r) noexcept { return Value(dgtl_build_and(fb_, l.v, r.v)); }
    [[nodiscard]] Value bor(Value l, Value r) noexcept  { return Value(dgtl_build_or(fb_, l.v, r.v)); }
    [[nodiscard]] Value bxor(Value l, Value r) noexcept { return Value(dgtl_build_xor(fb_, l.v, r.v)); }
    [[nodiscard]] Value bnot(Value v) noexcept  { return Value(dgtl_build_not(fb_, v.v)); }
    [[nodiscard]] Value shl(Value v, Value n) noexcept  { return Value(dgtl_build_shl(fb_, v.v, n.v)); }
    [[nodiscard]] Value shr(Value v, Value n) noexcept  { return Value(dgtl_build_shr(fb_, v.v, n.v)); }

    /* Predicates & Select */
    [[nodiscard]] Value icmp(dgtl_icmp_pred_t p, Value l, Value r) noexcept { return Value(dgtl_build_icmp(fb_, p, l.v, r.v)); }
    [[nodiscard]] Value fcmp(dgtl_fcmp_pred_t p, Value l, Value r) noexcept { return Value(dgtl_build_fcmp(fb_, p, l.v, r.v)); }
    [[nodiscard]] Value select(Value c, Value t, Value f) noexcept { return Value(dgtl_build_select(fb_, c.v, t.v, f.v)); }

    /* Memory Access */
    [[nodiscard]] Value load(dgtl_type_t ty, Value ptr) noexcept { return Value(dgtl_build_load(fb_, ty, ptr.v)); }
    void store(Value val, Value ptr) noexcept { (void)dgtl_build_store(fb_, val.v, ptr.v); }
    [[nodiscard]] Value gep(Value base, Value off) noexcept { return Value(dgtl_build_gep(fb_, base.v, off.v)); }
    [[nodiscard]] Value stack_alloc(dgtl_type_t ty, uint32_t align = 0) noexcept { return Value(dgtl_build_alloca(fb_, ty, align)); }
    [[nodiscard]] Value cast(Value v, dgtl_type_t to) noexcept { return Value(dgtl_build_cast(fb_, v.v, to)); }

    /* Constants */
    [[nodiscard]] Value k_i32(int32_t v) noexcept  { return Value(dgtl_build_const_i32(fb_, v)); }
    [[nodiscard]] Value k_i64(int64_t v) noexcept  { return Value(dgtl_build_const_i64(fb_, v)); }
    [[nodiscard]] Value k_f32(float v) noexcept    { return Value(dgtl_build_const_f32(fb_, v)); }
    [[nodiscard]] Value k_f64(double v) noexcept   { return Value(dgtl_build_const_f64(fb_, v)); }
    [[nodiscard]] Value k_bool(bool v) noexcept    { return Value(dgtl_build_const_bool(fb_, v)); }
    [[nodiscard]] Value k_null(dgtl_type_t t) noexcept { return Value(dgtl_build_const_null(fb_, t)); }
    [[nodiscard]] Value str(const char* s) noexcept { return Value(dgtl_build_str(fb_, s)); }

    /* Control Flow & Graph SSA */
    void br(Block target) noexcept { dgtl_build_br(fb_, target.b); }
    void cond_br(Value cond, Block t, Block f) noexcept { dgtl_build_cond_br(fb_, cond.v, t.b, f.b); }
    void ret(Value v) noexcept { dgtl_build_ret(fb_, v.v); }
    void ret_void() noexcept   { dgtl_build_ret_void(fb_); }
    [[nodiscard]] Value phi(dgtl_type_t ty) noexcept { return Value(dgtl_build_phi(fb_, ty)); }
    void phi_incoming(Value phi_val, Value val, Block from) noexcept { dgtl_phi_add_incoming(fb_, phi_val.v, val.v, from.b); }

    /* Calls */
    [[nodiscard]] Value call(dgtl_func_t fn, std::initializer_list<Value> args) noexcept {
        std::vector<dgtl_value_t> a;
        a.reserve(args.size());
        for (const auto& v : args) a.push_back(v.v);
        return Value(dgtl_build_call(fb_, fn, a.data(), a.size()));
    }

    /* Math Extensions */
    [[nodiscard]] Value fma(Value a, Value b, Value c) noexcept { return Value(dgtl_build_fma(fb_, a.v, b.v, c.v)); }
    [[nodiscard]] Value fms(Value a, Value b, Value c) noexcept { return Value(dgtl_build_fms(fb_, a.v, b.v, c.v)); }
    [[nodiscard]] Value matmul(Value a, Value b, Value acc) noexcept { return Value(dgtl_build_matmul(fb_, a.v, b.v, acc.v)); }
    [[nodiscard]] Value softmax(Value x) noexcept { return Value(dgtl_build_softmax(fb_, x.v)); }
    [[nodiscard]] Value layer_norm(Value x, Value g, Value b2) noexcept { return Value(dgtl_build_layer_norm(fb_, x.v, g.v, b2.v)); }
    [[nodiscard]] Value sdpa(Value q, Value k, Value v2, Value mask) noexcept { return Value(dgtl_build_sdpa(fb_, q.v, k.v, v2.v, mask.v)); }

    /* SIMD */
    [[nodiscard]] Value vec_add(Value l, Value r) noexcept { return Value(dgtl_build_vec_add(fb_, l.v, r.v)); }
    [[nodiscard]] Value vec_mul(Value l, Value r) noexcept { return Value(dgtl_build_vec_mul(fb_, l.v, r.v)); }
    [[nodiscard]] Value vec_reduce_add(Value v) noexcept { return Value(dgtl_build_vec_reduce_add(fb_, v.v)); }
    [[nodiscard]] Value gather(Value base, Value idx, Value mask) noexcept { return Value(dgtl_build_gather(fb_, base.v, idx.v, mask.v)); }

    /* Concurrency & Latency */
    [[nodiscard]] Value read_tsc() noexcept { return Value(dgtl_build_read_tsc(fb_)); }
    void prefetch(Value addr, uint32_t hint = 0) noexcept { dgtl_build_prefetch(fb_, addr.v, hint); }
    void clflush(Value addr) noexcept { dgtl_build_clflush(fb_, addr.v); }
    void fence(dgtl_memory_order_t order = DGTL_MO_SEQ_CST) noexcept { dgtl_build_fence(fb_, order); }

private:
    dgtl_func_builder_t* fb_ = nullptr;
};

class Session {
public:
    explicit Session(const dgtl_session_config_t* cfg = nullptr)
        : session_(dgtl_session_create(cfg)) {
        if (!session_) throw std::bad_alloc();
    }
    ~Session() noexcept { if (session_) dgtl_session_destroy(session_); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&& o) noexcept : session_(o.session_) { o.session_ = nullptr; }
    Session& operator=(Session&& o) noexcept {
        if (this != &o) { if (session_) dgtl_session_destroy(session_); session_ = o.session_; o.session_ = nullptr; }
        return *this;
    }
    [[nodiscard]] dgtl_session_t* get() const noexcept { return session_; }

    dgtl_err_t register_symbol(const char* name, uintptr_t addr) noexcept {
        return dgtl_session_register_symbol(session_, name, addr);
    }
    dgtl_err_t compile(Stream& s) noexcept {
        return dgtl_session_compile(session_, s.get());
    }
    [[nodiscard]] void* function_ptr(const char* name) const noexcept {
        return dgtl_session_get_function_ptr(session_, name);
    }
    template <typename Sig>
    [[nodiscard]] Sig* get_function(const char* name) const noexcept {
        return reinterpret_cast<Sig*>(dgtl_session_get_function_ptr(session_, name));
    }
    template <typename Ret, typename... Args>
    [[nodiscard]] Ret invoke(const char* name, Args... args) const {
        auto* fn = get_function<Ret(Args...)>(name);
        if (!fn) throw std::runtime_error(std::string("DGTL: Symbol resolution failed: ") + name);
        return fn(args...);
    }
private:
    dgtl_session_t* session_ = nullptr;
};

class TargetMachine {
public:
    TargetMachine() : tm_(dgtl_target_machine_create_host()) {}
    explicit TargetMachine(dgtl_target_machine_t tm) : tm_(tm) {}
    TargetMachine(const char* triple, const char* cpu = nullptr, const char* features = nullptr,
                  dgtl_opt_level_t opt = DGTL_OPT_O2,
                  dgtl_reloc_model_t reloc = DGTL_RELOC_DEFAULT,
                  dgtl_code_model_t code = DGTL_CODE_MODEL_DEFAULT)
        : tm_(dgtl_target_machine_create(triple, cpu, features, opt, reloc, code)) {}
    ~TargetMachine() noexcept { if (tm_) dgtl_target_machine_destroy(tm_); }
    TargetMachine(const TargetMachine&) = delete;
    TargetMachine& operator=(const TargetMachine&) = delete;
    TargetMachine(TargetMachine&& o) noexcept : tm_(o.tm_) { o.tm_ = nullptr; }
    TargetMachine& operator=(TargetMachine&& o) noexcept {
        if (this != &o) { if (tm_) dgtl_target_machine_destroy(tm_); tm_ = o.tm_; o.tm_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return tm_ != nullptr; }
    [[nodiscard]] dgtl_target_machine_t get() const noexcept { return tm_; }
    [[nodiscard]] const char* triple() const noexcept { return dgtl_target_machine_get_triple(tm_); }
    [[nodiscard]] const char* cpu() const noexcept { return dgtl_target_machine_get_cpu(tm_); }
    [[nodiscard]] bool has_feature(const char* feat) const noexcept { return dgtl_target_machine_has_feature(tm_, feat); }
    [[nodiscard]] uint32_t pointer_size() const noexcept { return dgtl_target_machine_get_pointer_size(tm_); }
    [[nodiscard]] bool is_64bit() const noexcept { return dgtl_target_machine_is_64bit(tm_); }
    [[nodiscard]] bool is_little_endian() const noexcept { return dgtl_target_machine_is_little_endian(tm_); }
    [[nodiscard]] dgtl_target_arch_t arch() const noexcept { return dgtl_target_machine_get_arch(tm_); }
private:
    dgtl_target_machine_t tm_ = nullptr;
};

inline dgtl_text_t Stream::disassemble(const TargetMachine& tm) const noexcept {
    dgtl_text_t txt{};
    (void)dgtl_mir_disassemble_stream(s_, tm.get(), &txt);
    return txt;
}

inline dgtl_text_t Stream::dump_machine_ir(const TargetMachine& tm) const noexcept {
    dgtl_text_t txt{};
    (void)dgtl_mir_dump_machine_ir(s_, tm.get(), &txt);
    return txt;
}

class SectionRef {
public:
    explicit SectionRef(dgtl_section_t sec) noexcept : sec_(sec) {}
    [[nodiscard]] bool is_valid() const noexcept { return sec_ != nullptr; }
    [[nodiscard]] dgtl_section_t get() const noexcept { return sec_; }
    operator dgtl_section_t() const noexcept { return sec_; }
    [[nodiscard]] const char* name() const noexcept { return dgtl_section_get_name(sec_); }
    [[nodiscard]] size_t size() const noexcept { return dgtl_section_get_size(sec_); }
    [[nodiscard]] const uint8_t* data() const noexcept { return dgtl_section_get_data(sec_); }
    [[nodiscard]] uint32_t flags() const noexcept { return dgtl_section_get_flags(sec_); }
    [[nodiscard]] uint64_t bss_size() const noexcept { return dgtl_section_get_bss_size(sec_); }
    dgtl_err_t set_bss_size(uint64_t sz) noexcept { return dgtl_section_set_bss_size(sec_, sz); }
    [[nodiscard]] uint64_t alignment() const noexcept { return dgtl_section_get_alignment(sec_); }
    dgtl_err_t append_data(const uint8_t* data, size_t sz) noexcept { return dgtl_section_append_data(sec_, data, sz); }
private:
    dgtl_section_t sec_ = nullptr;
};

class SymbolRef {
public:
    explicit SymbolRef(dgtl_symbol_t sym) noexcept : sym_(sym) {}
    [[nodiscard]] bool is_valid() const noexcept { return sym_ != nullptr; }
    [[nodiscard]] dgtl_symbol_t get() const noexcept { return sym_; }
    operator dgtl_symbol_t() const noexcept { return sym_; }
    [[nodiscard]] const char* name() const noexcept { return dgtl_symbol_get_name(sym_); }
    [[nodiscard]] uint64_t value() const noexcept { return dgtl_symbol_get_value(sym_); }
    [[nodiscard]] uint64_t size() const noexcept { return dgtl_symbol_get_size(sym_); }
    [[nodiscard]] uint32_t section_index() const noexcept { return dgtl_symbol_get_section_index(sym_); }
private:
    dgtl_symbol_t sym_ = nullptr;
};

class RelocationRef {
public:
    explicit RelocationRef(dgtl_relocation_t rel) noexcept : rel_(rel) {}
    [[nodiscard]] bool is_valid() const noexcept { return rel_ != nullptr; }
    [[nodiscard]] dgtl_relocation_t get() const noexcept { return rel_; }
    operator dgtl_relocation_t() const noexcept { return rel_; }
    [[nodiscard]] uint64_t offset() const noexcept { return dgtl_relocation_get_offset(rel_); }
    [[nodiscard]] const char* symbol_name() const noexcept { return dgtl_relocation_get_symbol_name(rel_); }
    [[nodiscard]] uint32_t symbol_index() const noexcept { return dgtl_relocation_get_symbol_index(rel_); }
    [[nodiscard]] int64_t addend() const noexcept { return dgtl_relocation_get_addend(rel_); }
    [[nodiscard]] uint32_t type() const noexcept { return dgtl_relocation_get_type(rel_); }
    [[nodiscard]] uint32_t section_index() const noexcept { return dgtl_relocation_get_section_index(rel_); }
private:
    dgtl_relocation_t rel_ = nullptr;
};

class ObjectFile {
public:
    explicit ObjectFile(dgtl_object_format_t fmt = DGTL_OBJECT_FMT_PE)
        : obj_(dgtl_object_create(fmt)) {}
    explicit ObjectFile(dgtl_object_t obj) : obj_(obj) {}
    static ObjectFile load_from_file(const char* path) {
        return ObjectFile(dgtl_object_load_from_file(path));
    }
    static ObjectFile load_from_memory(const uint8_t* bytes, size_t size) {
        return ObjectFile(dgtl_object_load_from_memory(bytes, size));
    }
    ~ObjectFile() noexcept { if (obj_) dgtl_object_destroy(obj_); }
    ObjectFile(const ObjectFile&) = delete;
    ObjectFile& operator=(const ObjectFile&) = delete;
    ObjectFile(ObjectFile&& o) noexcept : obj_(o.obj_) { o.obj_ = nullptr; }
    ObjectFile& operator=(ObjectFile&& o) noexcept {
        if (this != &o) { if (obj_) dgtl_object_destroy(obj_); obj_ = o.obj_; o.obj_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return obj_ != nullptr; }
    [[nodiscard]] dgtl_object_t get() const noexcept { return obj_; }
    [[nodiscard]] dgtl_object_format_t format() const noexcept { return dgtl_object_get_format(obj_); }
    [[nodiscard]] size_t section_count() const noexcept { return dgtl_object_get_section_count(obj_); }
    [[nodiscard]] SectionRef section(size_t idx) const noexcept { return SectionRef(dgtl_object_get_section(obj_, idx)); }
    [[nodiscard]] SectionRef find_section(const char* name) const noexcept { return SectionRef(dgtl_object_find_section(obj_, name)); }
    [[nodiscard]] SectionRef add_section(const char* name, uint64_t align = 16, uint32_t flags = DGTL_SEC_FLAG_READ | DGTL_SEC_FLAG_DATA) noexcept {
        return SectionRef(dgtl_object_add_section(obj_, name, align, flags));
    }
    [[nodiscard]] size_t symbol_count() const noexcept { return dgtl_object_get_symbol_count(obj_); }
    [[nodiscard]] SymbolRef symbol(size_t idx) const noexcept { return SymbolRef(dgtl_object_get_symbol(obj_, idx)); }
    [[nodiscard]] SymbolRef find_symbol(const char* name) const noexcept { return SymbolRef(dgtl_object_find_symbol(obj_, name)); }
    [[nodiscard]] SymbolRef add_symbol(const char* name, uint64_t value, uint64_t size, uint32_t sec_idx, uint8_t binding = DGTL_SYM_BIND_GLOBAL, uint8_t type = DGTL_SYM_TYPE_FUNC) noexcept {
        return SymbolRef(dgtl_object_add_symbol(obj_, name, value, size, sec_idx, binding, type));
    }
    [[nodiscard]] size_t relocation_count() const noexcept { return dgtl_object_get_relocation_count(obj_); }
    [[nodiscard]] RelocationRef relocation(size_t idx) const noexcept { return RelocationRef(dgtl_object_get_relocation(obj_, idx)); }
    dgtl_err_t emit_to_file(const char* path) noexcept { return dgtl_object_emit_to_file(obj_, path); }
    dgtl_err_t emit_to_memory(const uint8_t** out_bytes, size_t* out_size) noexcept {
        return dgtl_object_emit_to_memory(obj_, out_bytes, out_size);
    }
private:
    dgtl_object_t obj_ = nullptr;
};

class Module {
public:
    explicit Module(Context& ctx, const char* module_id = "default_module")
        : mod_(dgtl_module_create(ctx.get(), module_id)) {
        if (!mod_) throw std::bad_alloc();
    }
    explicit Module(const char* module_id = "default_module")
        : mod_(dgtl_module_create(nullptr, module_id)) {
        if (!mod_) throw std::bad_alloc();
    }
    explicit Module(dgtl_module_t mod) noexcept : mod_(mod) {}
    ~Module() noexcept { if (mod_) dgtl_module_destroy(mod_); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&& o) noexcept : mod_(o.mod_) { o.mod_ = nullptr; }
    Module& operator=(Module&& o) noexcept {
        if (this != &o) { if (mod_) dgtl_module_destroy(mod_); mod_ = o.mod_; o.mod_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return mod_ != nullptr; }
    [[nodiscard]] dgtl_module_t get() const noexcept { return mod_; }
    [[nodiscard]] const char* id() const noexcept { return dgtl_module_get_id(mod_); }

    dgtl_err_t add_function(const char* name, Stream& body, dgtl_linkage_t linkage = DGTL_LINKAGE_EXTERNAL) noexcept {
        return dgtl_module_add_function(mod_, name, body.get(), linkage);
    }
    dgtl_err_t add_global_data(const char* name, const uint8_t* data, size_t size, size_t alignment = 16, bool is_constant = false, dgtl_linkage_t linkage = DGTL_LINKAGE_EXTERNAL) noexcept {
        return dgtl_module_add_global_data(mod_, name, data, size, alignment, is_constant, linkage);
    }
    [[nodiscard]] size_t function_count() const noexcept { return dgtl_module_get_function_count(mod_); }
    [[nodiscard]] const char* function_name(size_t idx) const noexcept { return dgtl_module_get_function_name(mod_, idx); }
    [[nodiscard]] size_t global_count() const noexcept { return dgtl_module_get_global_count(mod_); }
    [[nodiscard]] const char* global_name(size_t idx) const noexcept { return dgtl_module_get_global_name(mod_, idx); }

    [[nodiscard]] ObjectFile compile_to_object(TargetMachine& tm) {
        return ObjectFile(dgtl_module_compile_to_object(mod_, tm.get()));
    }
    [[nodiscard]] dgtl_text_t disassemble(TargetMachine& tm) const noexcept {
        dgtl_text_t txt{};
        (void)dgtl_mir_disassemble_module(mod_, tm.get(), &txt);
        return txt;
    }
private:
    dgtl_module_t mod_ = nullptr;
};

class Linker {
public:
    explicit Linker(dgtl_object_format_t fmt = DGTL_OBJECT_FMT_PE)
        : linker_(dgtl_linker_create(fmt)) {}
    explicit Linker(dgtl_subsystem_t sub)
        : linker_(dgtl_linker_create(DGTL_OBJECT_FMT_PE)) {
        if (linker_) (void)dgtl_linker_set_subsystem(linker_, sub);
    }
    ~Linker() noexcept { if (linker_) dgtl_linker_destroy(linker_); }
    Linker(const Linker&) = delete;
    Linker& operator=(const Linker&) = delete;
    Linker(Linker&& o) noexcept : linker_(o.linker_) { o.linker_ = nullptr; }
    Linker& operator=(Linker&& o) noexcept {
        if (this != &o) { if (linker_) dgtl_linker_destroy(linker_); linker_ = o.linker_; o.linker_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return linker_ != nullptr; }
    [[nodiscard]] dgtl_linker_t get() const noexcept { return linker_; }
    dgtl_err_t set_entry_point(const char* sym) noexcept { return dgtl_linker_set_entry_point(linker_, sym); }
    dgtl_err_t set_image_base(uint64_t image_base) noexcept { return dgtl_linker_set_image_base(linker_, image_base); }
    dgtl_err_t set_subsystem(dgtl_subsystem_t sub) noexcept { return dgtl_linker_set_subsystem(linker_, sub); }
    dgtl_err_t add_object(const ObjectFile& obj) noexcept { return dgtl_linker_add_object(linker_, obj.get()); }
    dgtl_err_t add_object_file(const char* path) noexcept { return dgtl_linker_add_object_file(linker_, path); }
    dgtl_err_t add_archive(const char* path) noexcept { return dgtl_linker_add_archive(linker_, path); }
    dgtl_err_t add_library_search_path(const char* dir) noexcept { return dgtl_linker_add_library_search_path(linker_, dir); }
    dgtl_err_t link_to_file(dgtl_linker_output_kind_t kind, const char* out_path) noexcept {
        return dgtl_linker_link_to_file(linker_, kind, out_path);
    }
    dgtl_err_t link_to_memory(dgtl_linker_output_kind_t kind, const uint8_t** out_bytes, size_t* out_size) noexcept {
        return dgtl_linker_link_to_memory(linker_, kind, out_bytes, out_size);
    }
private:
    dgtl_linker_t linker_ = nullptr;
};

class JITEngine {
public:
    explicit JITEngine(Context& ctx, TargetMachine& tm)
        : engine_(dgtl_jit_engine_create(ctx.get(), tm.get())) {}
    JITEngine()
        : default_ctx_(), default_tm_(),
          engine_(dgtl_jit_engine_create(default_ctx_.get(), default_tm_.get())) {}
    ~JITEngine() noexcept { if (engine_) dgtl_jit_engine_destroy(engine_); }
    JITEngine(const JITEngine&) = delete;
    JITEngine& operator=(const JITEngine&) = delete;
    JITEngine(JITEngine&& o) noexcept : engine_(o.engine_) { o.engine_ = nullptr; }
    JITEngine& operator=(JITEngine&& o) noexcept {
        if (this != &o) { if (engine_) dgtl_jit_engine_destroy(engine_); engine_ = o.engine_; o.engine_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return engine_ != nullptr; }
    [[nodiscard]] dgtl_jit_engine_t get() const noexcept { return engine_; }
    void set_symbol_resolver(dgtl_symbol_resolver_fn resolver, void* user_data) noexcept {
        dgtl_jit_engine_set_symbol_resolver(engine_, resolver, user_data);
    }
    void register_symbol(const char* name, uintptr_t addr) noexcept {
        dgtl_jit_engine_register_symbol(engine_, name, addr);
    }
    dgtl_err_t add_object(const ObjectFile& obj) noexcept {
        return dgtl_jit_engine_add_object(engine_, obj.get());
    }
    dgtl_err_t add_module(Module& mod) noexcept {
        return dgtl_jit_engine_add_module(engine_, mod.get());
    }
    dgtl_err_t add_stream(Stream& s, const char* entry = "main") noexcept {
        return dgtl_jit_engine_add_stream(engine_, s.get(), entry);
    }
    [[nodiscard]] uintptr_t lookup(const char* name) const noexcept {
        return dgtl_jit_engine_lookup(engine_, name);
    }
    template <typename Sig>
    [[nodiscard]] Sig* get_function(const char* name) const noexcept {
        return reinterpret_cast<Sig*>(lookup(name));
    }
private:
    Context default_ctx_{};
    TargetMachine default_tm_{};
    dgtl_jit_engine_t engine_ = nullptr;
};

namespace sdk {
    using Context       = dgtl::Context;
    using Stream        = dgtl::Stream;
    using FuncBuilder   = dgtl::FuncBuilder;
    using Session       = dgtl::Session;
    using Value         = dgtl::Value;
    using Block         = dgtl::Block;
    using TargetMachine = dgtl::TargetMachine;
    using SectionRef    = dgtl::SectionRef;
    using SymbolRef     = dgtl::SymbolRef;
    using RelocationRef = dgtl::RelocationRef;
    using ObjectFile    = dgtl::ObjectFile;
    using Module        = dgtl::Module;
    using Linker        = dgtl::Linker;
    using JITEngine     = dgtl::JITEngine;
}

} /* namespace dgtl */

#endif /* __cplusplus */

#endif /* DGTL_SDK_H */