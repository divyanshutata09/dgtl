/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 * Component:   Public SDK Boundary C-API Lane
 * Purpose:     Stable Client ABI Ingestion Layer for High-Performance Languages
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#ifndef DGTL_C_API_H
#define DGTL_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── SYSTEM ANCHORS & SYMBOL EXPORT CONTROLS ─── */
#ifndef DGTL_API
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
    #ifdef DGTL_DLL
        #ifdef DGTL_EXPORTS
            #define DGTL_API __declspec(dllexport)
        #else
            #define DGTL_API __declspec(dllimport)
        #endif
    #else
        #define DGTL_API
    #endif
    #define DGTL_CALL __cdecl
#else
    #if __GNUC__ >= 4
        #define DGTL_API __attribute__((visibility("default")))
    #else
        #define DGTL_API
    #endif
    #define DGTL_CALL
#endif
#endif

#ifndef DGTL_CALL
#define DGTL_CALL
#endif

/* ─── STRICT OPAQUE HANDLES (Defends Boundary Isolation) ─── */
typedef struct dgtl_context  dgtl_context_t;
typedef struct dgtl_stream   dgtl_stream_t;
typedef struct dgtl_session  dgtl_session_t;

typedef uint16_t dgtl_op_t;
typedef uint32_t dgtl_value_t;

/* ---------- OpCode Enum Flags ---------- */
#ifndef DGTL_FLAG_NONE
#define DGTL_FLAG_NONE           0x00u
#define DGTL_FLAG_IS_CONST       0x01u
#define DGTL_FLAG_IS_SPEC_CONST  0x02u
#endif

#ifndef DGTL_SDK_H
/* --- Core Target Runtime Enums (dgtl_core.h Mirror) --- */
typedef enum dgtl_target {
    DGTL_TARGET_ARM64      = 0,
    DGTL_TARGET_X64        = 1,
    DGTL_TARGET_NASM       = 2,
    DGTL_TARGET_WASM       = 3,
    DGTL_TARGET_ELF_OBJ    = 4,
    DGTL_TARGET_STATIC_LIB = 5,
    DGTL_TARGET_COFF_OBJ   = 6,
    DGTL_TARGET_COFF_LIB   = 7,
    DGTL_TARGET_MACHO      = 8,
    DGTL_TARGET_COUNT      = 9
} dgtl_target_t;

/* --- Status Response Primitive Payload --- */
typedef struct dgtl_status {
    int  ok;           /* 1 = success, 0 = failure */
    int  error_code;
    char message[256];
} dgtl_status_t;

typedef struct dgtl_strike_result {
    int      ok;
    size_t   code_size;
    uint64_t logic_hash;
    char     message[256];
} dgtl_strike_result_t;
#endif

/* ========================================================================
 * ADVANCED STREAM INTROSPECTION & CONTROL FLOW MATRIX
 * ======================================================================== */

typedef struct dgtl_perf_metrics {
    uint64_t total_compilation_time_us;
    uint64_t optimization_time_us;
    uint64_t emission_time_us;
    uint32_t peak_register_count;
    uint32_t peak_memory_usage_bytes;
    uint64_t estimated_cycle_count;
} dgtl_perf_metrics_t;

typedef enum dgtl_cfg_edge_kind {
    DGTL_CFG_EDGE_FALLTHROUGH      = 0,
    DGTL_CFG_EDGE_UNCONDITIONAL    = 1,
    DGTL_CFG_EDGE_CONDITIONAL_TRUE  = 2,
    DGTL_CFG_EDGE_CONDITIONAL_FALSE = 3
} dgtl_cfg_edge_kind_t;

typedef struct dgtl_cfg_block {
    uint32_t id;
    size_t begin;
    size_t end;
    uint32_t primary_label;
    int32_t is_entry;
} dgtl_cfg_block_t;

typedef struct dgtl_cfg_edge {
    uint32_t from;
    uint32_t to;
    dgtl_cfg_edge_kind_t kind;
} dgtl_cfg_edge_t;

DGTL_API int32_t DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_block_count(const dgtl_stream_t* s);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_edge_count(const dgtl_stream_t* s);

DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_block(const dgtl_stream_t* s, size_t index, dgtl_cfg_block_t* out);
DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_edge(const dgtl_stream_t* s, size_t index, dgtl_cfg_edge_t* out);

DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_successors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_predecessors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);

DGTL_API int32_t       DGTL_CALL dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* error_buffer, size_t error_len);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_compact(dgtl_stream_t* s);

/* ========================================================================
 * CACHE-ALIGNED STREAM LOWERING ARCHITECTURE (HIGH-VELOCITY SYSTEM TUNNEL)
 * ======================================================================== */

DGTL_API uint32_t DGTL_CALL dgtl_intern_string_handle_utf8(const char* utf8_data, size_t len);
DGTL_API void     DGTL_CALL dgtl_emit_constant_str(dgtl_stream_t* s, dgtl_value_t dest_reg, const char* value, size_t length);

DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f32(dgtl_stream_t* s, float value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f64(dgtl_stream_t* s, double value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_binary(dgtl_stream_t* s, const void* data, size_t len);

/* Explicit 64-bit Floating-Point Binary Operations */
DGTL_API uint32_t DGTL_CALL dgtl_emit_fadd_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fsub_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fmul_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fdiv_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* AI/ML & Spatial Math Emission Hooks */
DGTL_API uint32_t DGTL_CALL dgtl_emit_matmul_bf16(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sdpa_f16(dgtl_stream_t* s, uint32_t q, uint32_t k, uint32_t v);
DGTL_API uint32_t DGTL_CALL dgtl_emit_softmax_fast(dgtl_stream_t* s, uint32_t input);
DGTL_API uint32_t DGTL_CALL dgtl_emit_layer_norm(dgtl_stream_t* s, uint32_t input, uint32_t gamma, uint32_t beta);
DGTL_API uint32_t DGTL_CALL dgtl_emit_fast_inv_sqrt(dgtl_stream_t* s, uint32_t input);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec3_cross(dgtl_stream_t* s, uint32_t a, uint32_t b);
DGTL_API uint32_t DGTL_CALL dgtl_emit_vec4_dot(dgtl_stream_t* s, uint32_t a, uint32_t b);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mat4_mul(dgtl_stream_t* s, uint32_t a, uint32_t b);

DGTL_API uint32_t DGTL_CALL dgtl_emit_phi(dgtl_stream_t* s, uint32_t* incoming_slots, size_t incoming_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_label(dgtl_stream_t* s, const char* label_name);
DGTL_API uint32_t DGTL_CALL dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot);
#ifndef DGTL_SDK_H
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slot(dgtl_stream_t* s, uint32_t slot, const char* name);
#endif

/* --- High-Velocity Scalar Ingestion Hook bypassing internal string evaluations --- */
#ifndef DGTL_SDK_H
DGTL_API uint32_t DGTL_CALL dgtl_emit_op_with_result(dgtl_stream_t* s, uint32_t dest_reg, uint16_t opcode, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint32_t flags);
#endif

DGTL_API void     DGTL_CALL dgtl_emit_branch_cond(dgtl_stream_t* s, uint32_t condition, uint32_t target_label);
DGTL_API void     DGTL_CALL dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label);

/* --- Sovereign Rule 82: Predictive branch hint stream tunnel --- */
DGTL_API void     DGTL_CALL dgtl_emit_branch_predictive(dgtl_stream_t* s, dgtl_value_t condition_reg, uint32_t true_label_id, uint32_t false_label_id, uint8_t runtime_weight);

/* --- Sovereign Rule 132: Bare-metal execution boundary routing hook --- */
DGTL_API void     DGTL_CALL dgtl_emit_hardware_affinity(dgtl_stream_t* s, dgtl_value_t mask_register_id);

DGTL_API uint32_t DGTL_CALL dgtl_emit_call_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);

/* ========================================================================
 * ADVANCED OPTIMIZATION AND SESSION TIMINGS
 * ======================================================================== */

DGTL_API dgtl_status_t DGTL_CALL dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name);
DGTL_API int32_t       DGTL_CALL dgtl_has_pass(const char* pass_name);
DGTL_API const char** DGTL_CALL dgtl_list_passes(size_t* out_count);

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_enabled(dgtl_stream_t* s, int32_t enabled);
DGTL_API int32_t       DGTL_CALL dgtl_get_optimization_level(const dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_level(dgtl_stream_t* s, int32_t level);

/* ========================================================================
 * LINKER BACKEND EMISSION CONTROL
 * ======================================================================== */

typedef struct dgtl_emit_options {
    int32_t inline_threshold;
    int32_t unroll_factor;
    int32_t enable_cache_optimization;
    int32_t enable_branch_prediction;
    int32_t vectorize_threshold;
    int32_t reserved[11];
} dgtl_emit_options_t;

DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream_with_options(dgtl_stream_t* s, dgtl_target_t target, const dgtl_emit_options_t* options);
DGTL_API dgtl_emit_options_t  DGTL_CALL dgtl_emit_options_default(dgtl_target_t target);
DGTL_API size_t                DGTL_CALL dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target);

/* ========================================================================
 * INTERSIDE DATA TRACKING & MEMORY OPERATIONS
 * ======================================================================== */

DGTL_API size_t        DGTL_CALL dgtl_stream_memory_usage(const dgtl_stream_t* s);
DGTL_API size_t        DGTL_CALL dgtl_context_memory_usage(const dgtl_context_t* ctx);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_reserve(dgtl_stream_t* s, size_t additional_nodes);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_shrink_to_fit(dgtl_stream_t* s);

/* ========================================================================
 * PROFILING, SPECOPT AND ANALYSIS RUNTIMES
 * ======================================================================== */

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_enable_tracing(dgtl_stream_t* s);

typedef struct dgtl_trace_event {
    uint64_t timestamp_us;
    uint32_t pass_id;
    uint32_t node_count;
    char     pass_name[64];
} dgtl_trace_event_t;

DGTL_API dgtl_trace_event_t* DGTL_CALL dgtl_stream_get_trace_events(const dgtl_stream_t* s, size_t* out_count);
DGTL_API uint64_t            DGTL_CALL dgtl_stream_estimate_cycles(const dgtl_stream_t* s);
DGTL_API int32_t             DGTL_CALL dgtl_stream_get_optimization_score(const dgtl_stream_t* s);

/* ========================================================================
 * BLOB SERIALIZATION INTERCHANGE PACKETS
 * ======================================================================== */

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t blob_size);
DGTL_API void           DGTL_CALL dgtl_free_serialized_blob(void* blob);

/* ========================================================================
 * DIAGNOSTICS & SYSTEM LOGS HOOKS
 * ======================================================================== */

DGTL_API void        DGTL_CALL dgtl_stream_dump_ir(const dgtl_stream_t* s, void* custom_file_handle_opt);
DGTL_API const char* DGTL_CALL dgtl_get_last_error(void);
DGTL_API void        DGTL_CALL dgtl_clear_error(void);
DGTL_API void        DGTL_CALL dgtl_set_verbose(int32_t enabled);
DGTL_API const char* DGTL_CALL dgtl_get_build_config(void);

DGTL_API size_t      DGTL_CALL dgtl_last_linker_fallback_count(void);
DGTL_API size_t      DGTL_CALL dgtl_last_linker_fallback_message(size_t index, char* out_buffer, size_t out_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_C_API_H */