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
 * dgtl_simd.h — DGTL SIMD/FMA Helper Macros and Types (C ABI)
 *
 * Provides target-aware SIMD intrinsic helpers and legalizer access
 * for SDK consumers building vectorized kernels through DGTL.
 */

#ifndef DGTL_SIMD_H
#define DGTL_SIMD_H

#include "dgtl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- SIMD Target Architecture ---------- */
typedef enum dgtl_simd_arch {
    DGTL_SIMD_X86_AVX512     = 0,
    DGTL_SIMD_ARM_NEON       = 1,
    DGTL_SIMD_WASM_SIMD128   = 2,
    DGTL_SIMD_GENERIC        = 3,
    DGTL_SIMD_X86_AVX2       = 4
} dgtl_simd_arch_t;

/* ---------- SIMD Lane Configuration ---------- */
#define DGTL_SIMD_MAX_LANES_AVX512   16  /* 512-bit / 32-bit = 16 lanes */
#define DGTL_SIMD_MAX_LANES_AVX2       8  /* 256-bit / 32-bit = 8 lanes  */
#define DGTL_SIMD_MAX_LANES_NEON      4  /* 128-bit / 32-bit = 4 lanes  */
#define DGTL_SIMD_MAX_LANES_WASM128   4  /* 128-bit / 32-bit = 4 lanes  */

/* ---------- SIMD-Aware IR Emission ---------- */

/**
 * Emit a fused-multiply-add: result = (a * b) + c
 * Flags automatically set: DGTL_FLAG_FMA_FUSED | DGTL_FLAG_IS_FP
 */
DGTL_API uint32_t dgtl_emit_fma(dgtl_stream_t* s,
                                 uint32_t a, uint32_t b, uint32_t c);

/**
 * Emit a gather load from base + index vector.
 */
DGTL_API uint32_t dgtl_emit_gather(dgtl_stream_t* s,
                                    uint32_t base_slot,
                                    uint32_t index_slot,
                                    uint16_t flags);

/**
 * Emit a scatter store to base + index vector.
 */
DGTL_API uint32_t dgtl_emit_scatter(dgtl_stream_t* s,
                                     uint32_t base_slot,
                                     uint32_t index_slot,
                                     uint32_t value_slot,
                                     uint16_t flags);

/* ---------- SIMD Legalization ---------- */

/**
 * Run the SIMD legalizer over a stream for a given target architecture.
 * This performs the 4-node sliding-window vectorization pass.
 */
DGTL_API dgtl_status_t dgtl_legalize_simd(dgtl_stream_t* s,
                                            dgtl_simd_arch_t arch);

/* ---------- Convenience Macros ---------- */

/**
 * Helper: Emit a constant, multiply by another constant, add a third.
 * Typical use: linear address calculation  addr = base + index * stride
 */
#define DGTL_EMIT_LINEAR_ADDR(stream, base, index, stride)           \
    dgtl_emit_add_i32((stream), (base),                              \
        dgtl_emit_mul_i32((stream), (index),                         \
            dgtl_emit_const_i64((stream), (uint64_t)(stride))))

/**
 * Helper: Emit FMA-style a*b+c using the fused instruction.
 */
#define DGTL_EMIT_FMA(stream, a, b, c)  dgtl_emit_fma((stream), (a), (b), (c))

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SIMD_H */
