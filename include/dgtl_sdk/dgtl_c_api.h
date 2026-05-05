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
 * dgtl_c_api.h — Extended C ABI for Advanced SDK Users
 *
 * Provides lower-level access to DGTL internals:
 * - Custom IR node emission
 * - Fine-grained optimization control
 * - Performance profiling
 * - Memory introspection
 * - Direct backend emission
 */

#ifndef DGTL_C_API_H
#define DGTL_C_API_H

#include "dgtl.h"
#include "dgtl_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * ADVANCED STREAM INTROSPECTION
 * ======================================================================== */

/**
 * @brief Get performance metrics for a compiled stream.
 */
typedef struct dgtl_perf_metrics {
    uint64_t total_compilation_time_us;
    uint64_t optimization_time_us;
    uint64_t emission_time_us;
    uint32_t peak_register_count;
    uint32_t peak_memory_usage_bytes;
    uint64_t estimated_cycle_count;
} dgtl_perf_metrics_t;

DGTL_API int dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out);

/**
 * @brief Verify stream integrity (validates all node hashes and dependencies).
 */
DGTL_API int dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* error_buffer, size_t error_len);

/**
 * @brief Clear all dead nodes from stream (post-optimization cleanup).
 */
DGTL_API dgtl_status_t dgtl_stream_compact(dgtl_stream_t* s);

/* ========================================================================
 * ADVANCED IR EMISSION
 * ======================================================================== */

/**
 * @brief Emit a float FMA constant.
 */
DGTL_API uint32_t dgtl_emit_const_f32(dgtl_stream_t* s, float value);

/**
 * @brief Emit a float FMA constant (64-bit).
 */
DGTL_API uint32_t dgtl_emit_const_f64(dgtl_stream_t* s, double value);

/**
 * @brief Emit a raw binary constant (opaque data).
 */
DGTL_API uint32_t dgtl_emit_const_binary(dgtl_stream_t* s, 
                                          const void* data, size_t len);

/**
 * @brief Emit a phi node for SSA form.
 */
DGTL_API uint32_t dgtl_emit_phi(dgtl_stream_t* s,
                                 uint32_t* incoming_slots,
                                 size_t incoming_count);

/**
 * @brief Emit a label (target for branches).
 */
DGTL_API uint32_t dgtl_emit_label(dgtl_stream_t* s, const char* label_name);

/**
 * @brief Emit a conditional jump.
 */
DGTL_API void dgtl_emit_branch_cond(dgtl_stream_t* s,
                                     uint32_t condition,
                                     uint32_t target_label);

/**
 * @brief Emit an unconditional jump.
 */
DGTL_API void dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label);

/* ========================================================================
 * ADVANCED OPTIMIZATION CONTROL
 * ======================================================================== */

/**
 * @brief Run a specific pass by name.
 */
DGTL_API dgtl_status_t dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name);

/**
 * @brief Query whether a pass is registered.
 */
DGTL_API int dgtl_has_pass(const char* pass_name);

/**
 * @brief List all registered optimization passes.
 */
DGTL_API const char** dgtl_list_passes(size_t* out_count);

/**
 * @brief Enable/disable optimization for a stream.
 */
DGTL_API dgtl_status_t dgtl_set_optimization_enabled(dgtl_stream_t* s, int enabled);

/**
 * @brief Get current optimization level (0-3, where 3 is maximum).
 */
DGTL_API int dgtl_get_optimization_level(const dgtl_stream_t* s);

/**
 * @brief Set optimization level (0-3).
 */
DGTL_API dgtl_status_t dgtl_set_optimization_level(dgtl_stream_t* s, int level);

/* ========================================================================
 * BACKEND EMISSION CONTROL
 * ======================================================================== */

/**
 * @brief Emit with custom backend options.
 */
typedef struct dgtl_emit_options {
    int inline_threshold;          /* Number of nodes to inline */
    int unroll_factor;             /* Loop unroll factor */
    int enable_cache_optimization; /* Enable cache-aware optimizations */
    int enable_branch_prediction;  /* Enable CPU branch hint injection */
    int vectorize_threshold;       /* Minimum vector ops before vectorizing */
    int reserved[11];              /* Reserved for future expansion */
} dgtl_emit_options_t;

DGTL_API dgtl_strike_result_t dgtl_strike_stream_with_options(
    dgtl_stream_t* s,
    dgtl_target_t target,
    const dgtl_emit_options_t* options);

/**
 * @brief Get default emit options for a target.
 */
DGTL_API dgtl_emit_options_t dgtl_emit_options_default(dgtl_target_t target);

/**
 * @brief Get estimated code size before actual emission (for planning).
 */
DGTL_API size_t dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target);

/* ========================================================================
 * MEMORY MANAGEMENT
 * ======================================================================== */

/**
 * @brief Query memory usage of a stream.
 */
DGTL_API size_t dgtl_stream_memory_usage(const dgtl_stream_t* s);

/**
 * @brief Query memory usage of a context.
 */
DGTL_API size_t dgtl_context_memory_usage(const dgtl_context_t* ctx);

/**
 * @brief Pre-allocate memory for N more nodes (avoid rehashes).
 */
DGTL_API dgtl_status_t dgtl_stream_reserve(dgtl_stream_t* s, size_t additional_nodes);

/**
 * @brief Shrink stream to fit current size (release unused memory).
 */
DGTL_API dgtl_status_t dgtl_stream_shrink_to_fit(dgtl_stream_t* s);

/* ========================================================================
 * PROFILING & TRACING
 * ======================================================================== */

/**
 * @brief Enable low-overhead execution tracing for a stream.
 */
DGTL_API dgtl_status_t dgtl_stream_enable_tracing(dgtl_stream_t* s);

/**
 * @brief Disable tracing and return captured events.
 */
typedef struct dgtl_trace_event {
    uint64_t timestamp_us;
    uint32_t pass_id;
    uint32_t node_count;
    char     pass_name[64];
} dgtl_trace_event_t;

DGTL_API dgtl_trace_event_t* dgtl_stream_get_trace_events(const dgtl_stream_t* s,
                                                           size_t* out_count);

/**
 * @brief Estimate cycles for a stream using AI cost model.
 */
DGTL_API uint64_t dgtl_stream_estimate_cycles(const dgtl_stream_t* s);

/**
 * @brief Get heuristic optimization score (0-100).
 */
DGTL_API int dgtl_stream_get_optimization_score(const dgtl_stream_t* s);

/* ========================================================================
 * SERIALIZATION EXTENSIONS
 * ======================================================================== */

/**
 * @brief Serialize stream to binary blob (format-independent).
 */
DGTL_API dgtl_status_t dgtl_stream_serialize(const dgtl_stream_t* s,
                                              void** out_blob,
                                              size_t* out_size);

/**
 * @brief Deserialize stream from binary blob.
 */
DGTL_API dgtl_status_t dgtl_stream_deserialize(dgtl_stream_t* s,
                                                const void* blob,
                                                size_t blob_size);

/**
 * @brief Free serialized blob (previously allocated by dgtl_stream_serialize).
 */
DGTL_API void dgtl_free_serialized_blob(void* blob);

/* ========================================================================
 * DIAGNOSTIC FUNCTIONS
 * ======================================================================== */

/**
 * @brief Print stream to human-readable text (useful for debugging).
 */
DGTL_API void dgtl_stream_dump_ir(const dgtl_stream_t* s, FILE* out);

/**
 * @brief Get detailed error information after failed operations.
 */
DGTL_API const char* dgtl_get_last_error(void);

/**
 * @brief Clear error state.
 */
DGTL_API void dgtl_clear_error(void);

/**
 * @brief Enable/disable verbose logging to stderr.
 */
DGTL_API void dgtl_set_verbose(int enabled);

/**
 * @brief Get DGTL build configuration string.
 */
DGTL_API const char* dgtl_get_build_config(void);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_C_API_H */