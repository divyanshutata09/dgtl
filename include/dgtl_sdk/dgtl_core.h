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

/* ---------- IR OpCodes (mirrored from internal IR_Types) ---------- */
typedef enum dgtl_opcode {
    DGTL_OP_NOP       = 0x00,
    DGTL_OP_CONST     = 0x01,
    DGTL_OP_ADD       = 0x02,
    DGTL_OP_SUB       = 0x03,
    DGTL_OP_MUL       = 0x04,
    DGTL_OP_DIV       = 0x05,
    DGTL_OP_MOD       = 0x06,
    DGTL_OP_AND       = 0x07,
    DGTL_OP_OR        = 0x08,
    DGTL_OP_XOR       = 0x09,
    DGTL_OP_SHL       = 0x0A,
    DGTL_OP_SHR       = 0x0B,
    DGTL_OP_NEG       = 0x0C,
    DGTL_OP_NOT       = 0x0D,
    DGTL_OP_LOAD      = 0x10,
    DGTL_OP_STORE     = 0x11,
    DGTL_OP_CALL      = 0x20,
    DGTL_OP_RET       = 0x21,
    DGTL_OP_LABEL     = 0x30,
    DGTL_OP_JMP       = 0x31,
    DGTL_OP_BR_IF     = 0x32,
    DGTL_OP_CMP_EQ    = 0x40,
    DGTL_OP_CMP_NE    = 0x41,
    DGTL_OP_CMP_LT    = 0x42,
    DGTL_OP_CMP_LE    = 0x43,
    DGTL_OP_CMP_GT    = 0x44,
    DGTL_OP_CMP_GE    = 0x45,
    DGTL_OP_FMA       = 0x50,
    DGTL_OP_FADD      = 0x51,
    DGTL_OP_FMUL      = 0x52,
    DGTL_OP_FSUB      = 0x53,
    DGTL_OP_FDIV      = 0x54,
    DGTL_OP_PHI       = 0x60,
    DGTL_OP_GATHER    = 0x70,
    DGTL_OP_SCATTER   = 0x71,
    DGTL_OP_SENTINEL  = 0xFF
} dgtl_opcode_t;

/* ---------- IR Node Flags ---------- */
#define DGTL_FLAG_NONE       0x0000
#define DGTL_FLAG_IS_CONST   0x0001
#define DGTL_FLAG_IS_FP      0x0002
#define DGTL_FLAG_HAS_SIDE_EFFECT 0x0004
#define DGTL_FLAG_VOLATILE   0x0008
#define DGTL_FLAG_DEAD       0x0010
#define DGTL_FLAG_FMA_FUSED  0x0020
#define DGTL_FLAG_SPECULATED 0x0040
#define DGTL_FLAG_PREDICATED 0x0080

/* ---------- Optimization Profiles ---------- */
typedef enum dgtl_opt_profile {
    DGTL_OPT_PROFILE_DEFAULT    = 0,
    DGTL_OPT_PROFILE_FOLD       = 1,
    DGTL_OPT_PROFILE_FUSION     = 2,
    DGTL_OPT_PROFILE_SIMD       = 3,
    DGTL_OPT_PROFILE_PRESSURE   = 4,
    DGTL_OPT_PROFILE_PREDICATION = 5
} dgtl_opt_profile_t;

/* ---------- Extended Stream API ---------- */
DGTL_API uint32_t dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op,
                                uint32_t lhs, uint32_t rhs,
                                uint64_t immediate, uint16_t flags);

DGTL_API dgtl_status_t dgtl_optimize_with_profile(dgtl_stream_t* s,
                                                    dgtl_opt_profile_t profile);

/* ---------- Node Inspection ---------- */
typedef struct dgtl_node_info {
    dgtl_opcode_t opcode;
    uint32_t      slot;
    uint32_t      lhs;
    uint32_t      rhs;
    uint64_t      immediate;
    uint16_t      flags;
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
