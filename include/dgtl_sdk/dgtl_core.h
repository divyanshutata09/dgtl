/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * dgtl_core.h — DGTL IR and Logic Definitions (C ABI)
 *
 * Extended types and constants for consumers that need finer control
 * over DGTL's IR representation, node types, and optimization flags.
 */

#ifndef DGTL_CORE_H
#define DGTL_CORE_H

#include "dgtl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- IR OpCodes (stable SDK opcode set translated by bridge) ---------- */
#ifndef DGTL_OPCODE_DEFINED
#define DGTL_OPCODE_DEFINED
typedef enum dgtl_opcode {
    DGTL_OP_NOP               = 0x00,
    DGTL_OP_CONST             = 0x01,
    DGTL_OP_ADD               = 0x02,
    DGTL_OP_SUB               = 0x03,
    DGTL_OP_MUL               = 0x04,
    DGTL_OP_DIV               = 0x05,
    DGTL_OP_MOD               = 0x06,
    DGTL_OP_AND               = 0x07,
    DGTL_OP_OR                = 0x08,
    DGTL_OP_XOR               = 0x09,
    DGTL_OP_SHL               = 0x0A,
    DGTL_OP_SHR               = 0x0B,
    DGTL_OP_NEG               = 0x0C,
    DGTL_OP_NOT               = 0x0D,
    DGTL_OP_CONST_F64         = 0x0E,
    DGTL_OP_LOAD              = 0x10,
    DGTL_OP_STORE             = 0x11,
    DGTL_OP_ATOMIC_LOAD       = 0x12,
    DGTL_OP_ATOMIC_STORE      = 0x13,
    DGTL_OP_ATOMIC_FENCE      = 0x14,
    DGTL_OP_PTR_CAST          = 0x15,
    DGTL_OP_PTR_DIFF          = 0x16,
    DGTL_OP_BOOL_SELECT       = 0x17,
    DGTL_OP_STRING_CONST      = 0x18,
    DGTL_OP_STRING_LENGTH     = 0x19,
    DGTL_OP_STRING_COMPARE    = 0x1A,
    DGTL_OP_STRING_CONCAT     = 0x1B,
    DGTL_OP_STRING_SUBSTRING  = 0x1C,
    DGTL_OP_CALL              = 0x20,
    DGTL_OP_RET               = 0x21,
    DGTL_OP_LABEL             = 0x30,
    DGTL_OP_JMP               = 0x31,
    DGTL_OP_BR_IF             = 0x32,
    DGTL_OP_CMP_EQ            = 0x40,
    DGTL_OP_CMP_NE            = 0x41,
    DGTL_OP_CMP_LT            = 0x42,
    DGTL_OP_CMP_LE            = 0x43,
    DGTL_OP_CMP_GT            = 0x44,
    DGTL_OP_CMP_GE            = 0x45,
    DGTL_OP_FMA               = 0x50,
    DGTL_OP_FADD              = 0x51,
    DGTL_OP_FMUL              = 0x52,
    DGTL_OP_FSUB              = 0x53,
    DGTL_OP_FDIV              = 0x54,
    DGTL_OP_FADD_F64          = 0x55,
    DGTL_OP_FMUL_F64          = 0x56,
    DGTL_OP_FSUB_F64          = 0x57,
    DGTL_OP_FDIV_F64          = 0x58,
    DGTL_OP_PHI               = 0x60,
    DGTL_OP_GATHER            = 0x70,
    DGTL_OP_SCATTER           = 0x71,
    /* --- 64-bit integer variants (stable ABI, round-trip safe) --- */
    DGTL_OP_ADD_I64           = 0xA0,
    DGTL_OP_SUB_I64           = 0xA1,
    DGTL_OP_MUL_I64           = 0xA2,
    DGTL_OP_DIV_I64           = 0xA3,
    DGTL_OP_MOD_I64           = 0xA4,
    DGTL_OP_AND_I64           = 0xA5,
    DGTL_OP_OR_I64            = 0xA6,
    DGTL_OP_XOR_I64           = 0xA7,
    DGTL_OP_SHL_I64           = 0xA8,
    DGTL_OP_SHR_I64           = 0xA9,
    /* --- Extended stable bridge ops synced to latest IR/ISEL --- */
    DGTL_OP_SELECT            = 0xB0,
    DGTL_OP_GEP               = 0xB1,
    DGTL_OP_PTR_TO_INT        = 0xB2,
    DGTL_OP_INT_TO_PTR        = 0xB3,
    DGTL_OP_READ_TSC          = 0xB4,
    DGTL_OP_BR_LIKELY         = 0xB5,
    DGTL_OP_BR_UNLIKELY       = 0xB6,
    DGTL_OP_PREFETCH_T0       = 0xB7,
    DGTL_OP_CLFLUSH           = 0xB8,
    /* --- AI/ML & Spatial Math Extensions --- */
    DGTL_OP_MATMUL_BF16       = 0xC0,
    DGTL_OP_MATMUL_FP8        = 0xC1,
    DGTL_OP_SDPA_F16          = 0xC2,
    DGTL_OP_SOFTMAX_FAST      = 0xC3,
    DGTL_OP_LAYER_NORM        = 0xC4,
    DGTL_OP_ROPE_EMBED        = 0xC5,
    DGTL_OP_FAST_INV_SQRT     = 0xC6,
    DGTL_OP_VEC3_CROSS        = 0xC7,
    DGTL_OP_VEC4_DOT          = 0xC8,
    DGTL_OP_MAT4_MUL          = 0xC9,
    DGTL_OP_QUAT_SLERP        = 0xCA,
    DGTL_OP_RAY_AABB_ISECT    = 0xCB,
    /* --- Concurrency & Execution Context Primitives --- */
    DGTL_OP_ASYNC_SPAWN       = 0xD0,
    DGTL_OP_ASYNC_AWAIT       = 0xD1,
    DGTL_OP_FIBER_YIELD       = 0xD2,
    DGTL_OP_BARRIER_SYNC      = 0xD3,
    DGTL_OP_CHAN_SEND         = 0xD4,
    DGTL_OP_CHAN_RECV         = 0xD5,
    DGTL_OP_LOCK_ACQUIRE      = 0xD6,
    DGTL_OP_LOCK_RELEASE      = 0xD7,
    DGTL_OP_MEMORY_FENCE      = 0xD8,
    DGTL_OP_THREAD_SPAWN      = 0xD9,
    DGTL_OP_SENTINEL          = 0xFF
} dgtl_opcode_t;
#endif

/* ---------- IR Node Flags ---------- */
#ifndef DGTL_FLAG_NONE
#define DGTL_FLAG_NONE             0x0000u
#define DGTL_FLAG_IS_CONST         0x0001u
#define DGTL_FLAG_IS_SPEC_CONST    0x0100u
#endif

#define DGTL_FLAG_IS_FP            0x0002u
#define DGTL_FLAG_HAS_SIDE_EFFECT   0x0004u
#define DGTL_FLAG_VOLATILE         0x0008u
#define DGTL_FLAG_DEAD             0x0010u
#define DGTL_FLAG_FMA_FUSED        0x0020u
#define DGTL_FLAG_SPECULATED       0x0040u
#define DGTL_FLAG_PREDICATED       0x0080u

/* ---------- Optimization Profiles ---------- */
typedef enum dgtl_opt_profile {
    DGTL_OPT_PROFILE_DEFAULT      = 0,
    DGTL_OPT_PROFILE_FOLD         = 1,
    DGTL_OPT_PROFILE_FUSION       = 2,
    DGTL_OPT_PROFILE_SIMD         = 3,
    DGTL_OPT_PROFILE_PRESSURE     = 4,
    DGTL_OPT_PROFILE_PREDICATION  = 5
} dgtl_opt_profile_t;

/* ---------- Extended Stream API ---------- */
DGTL_API uint32_t dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op,
                                uint32_t lhs, uint32_t rhs,
                                uint64_t immediate, uint32_t flags);

DGTL_API uint32_t dgtl_emit_op_with_result(dgtl_stream_t* s,
                                           uint32_t result_slot,
                                           dgtl_opcode_t op,
                                           uint32_t lhs,
                                           uint32_t rhs,
                                           uint64_t immediate,
                                           uint32_t flags);

DGTL_API dgtl_status_t dgtl_optimize_with_profile(dgtl_stream_t* s,
                                                   dgtl_opt_profile_t profile);

/* ---------- Node Inspection ---------- */
typedef struct dgtl_node_info {
    dgtl_opcode_t opcode;
    uint32_t      slot;
    uint32_t      lhs;
    uint32_t      rhs;
    uint64_t      immediate;
    uint32_t      flags;
} dgtl_node_info_t;

DGTL_API int dgtl_stream_get_node(const dgtl_stream_t* s, size_t index,
                                   dgtl_node_info_t* out);

/* ---------- IR Mode ---------- */
typedef enum dgtl_ir_mode {
    DGTL_IR_MODE_INT     = 0,
    DGTL_IR_MODE_FLOAT   = 1,
    DGTL_IR_MODE_FMA     = 2
} dgtl_ir_mode_t;

DGTL_API dgtl_status_t dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_CORE_H */