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
 * dgtl.h — DGTL SDK Public Entry Point
 *
 * This is the single include for C and C++ consumers of the DGTL SDK.
 * It exposes the stable C ABI surface for pipeline ingestion, backend
 * emission, and payload serialization.
 *
 * Usage:
 *   #include "dgtl.h"
 *
 *   int main() {
 *       dgtl_context_t* ctx = dgtl_context_create();
 *       dgtl_stream_t*  s   = dgtl_stream_create(ctx, 256);
 *
 *       uint32_t c1 = dgtl_emit_const_i64(s, 100);
 *       uint32_t c2 = dgtl_emit_const_i64(s, 200);
 *       uint32_t r  = dgtl_emit_add_i32(s, c1, c2);
 *       dgtl_emit_return(s, r);
 *
 *       dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);
 *       // res.ok == true, res.code_size > 0
 *
 *       dgtl_stream_destroy(s);
 *       dgtl_context_destroy(ctx);
 *   }
 *
 * Link against: dgtl.lib (Windows) or libdgtl.a (Linux/macOS)
 */

#ifndef DGTL_SDK_H
#define DGTL_SDK_H

#include <stddef.h>
#include <stdint.h>

/* ---------- Export / Visibility ---------- */
#if defined(DGTL_SHARED_BUILD)
  #if defined(_WIN32)
    #define DGTL_API __declspec(dllexport)
  #else
    #define DGTL_API __attribute__((visibility("default")))
  #endif
#elif defined(DGTL_SHARED_IMPORT)
  #if defined(_WIN32)
    #define DGTL_API __declspec(dllimport)
  #else
    #define DGTL_API
  #endif
#else
  #define DGTL_API  /* static library — no decoration */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Version ---------- */
#define DGTL_VERSION_MAJOR  1
#define DGTL_VERSION_MINOR  1
#define DGTL_VERSION_PATCH  0
#define DGTL_VERSION_STRING "1.0.0"

DGTL_API const char* dgtl_version_string(void);

/* ---------- Opaque Handles ---------- */
typedef struct dgtl_context  dgtl_context_t;
typedef struct dgtl_stream   dgtl_stream_t;
typedef struct dgtl_function dgtl_function_t;

/* ---------- Backend Targets ---------- */
typedef enum dgtl_target {
    DGTL_TARGET_ARM64     = 0,
    DGTL_TARGET_X64       = 1,
    DGTL_TARGET_NASM      = 2,
    DGTL_TARGET_WASM      = 3,
    DGTL_TARGET_ELF_OBJ   = 4,
    DGTL_TARGET_STATIC_LIB = 5,
    DGTL_TARGET_COFF_OBJ  = 6,
    DGTL_TARGET_COFF_LIB  = 7,
    DGTL_TARGET_MACHO     = 8,
    DGTL_TARGET_COUNT     = 9
} dgtl_target_t;

DGTL_API const char* dgtl_target_name(dgtl_target_t target);
DGTL_API const char* dgtl_target_extension(dgtl_target_t target);

/* ---------- Error Handling ---------- */
typedef struct dgtl_status {
    int  ok;           /* 1 = success, 0 = failure */
    int  error_code;
    char message[256];
} dgtl_status_t;

/* ---------- Context Lifecycle ---------- */
DGTL_API dgtl_context_t*  dgtl_context_create(void);
DGTL_API void             dgtl_context_destroy(dgtl_context_t* ctx);

/* ---------- Stream Lifecycle ---------- */
DGTL_API dgtl_stream_t*   dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes);
DGTL_API void              dgtl_stream_destroy(dgtl_stream_t* stream);
DGTL_API size_t            dgtl_stream_node_count(const dgtl_stream_t* stream);
DGTL_API uint64_t          dgtl_stream_logic_hash(const dgtl_stream_t* stream);

/* ---------- IR Emission (Building the Stream) ---------- */
DGTL_API uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value);
DGTL_API uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API void     dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot);

/* ---------- Optimization ---------- */
DGTL_API dgtl_status_t dgtl_optimize(dgtl_stream_t* s);

/* ---------- Backend Strike (Compile to Target) ---------- */
typedef struct dgtl_strike_result {
    int      ok;
    size_t   code_size;
    uint64_t logic_hash;
    char     message[256];
} dgtl_strike_result_t;

DGTL_API dgtl_strike_result_t dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target);

/* ---------- File Emission ---------- */
DGTL_API dgtl_status_t dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target,
                                          const char* output_path);

/* ---------- Payload Serialization ---------- */
DGTL_API dgtl_status_t dgtl_write_payload(const dgtl_stream_t* s, const char* path);
DGTL_API dgtl_status_t dgtl_read_payload(dgtl_stream_t* s, const char* path);

/* ---------- Runtime Scheduler API ---------- */
typedef struct dgtl_runtime_context dgtl_runtime_context_t;
typedef void (*dgtl_runtime_task_fn)(void* context);

DGTL_API dgtl_runtime_context_t* dgtl_runtime_context_create(size_t worker_threads,
                                                            size_t arena_mb_per_worker);
DGTL_API void dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx);

DGTL_API uint64_t dgtl_runtime_spawn_task(dgtl_runtime_context_t* ctx,
                                          dgtl_runtime_task_fn fn,
                                          void* arg);
DGTL_API void dgtl_runtime_join_task(dgtl_runtime_context_t* ctx,
                                     uint64_t task_handle);
DGTL_API void dgtl_runtime_yield(dgtl_runtime_context_t* ctx);

DGTL_API void dgtl_yield(void);
DGTL_API uint64_t dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg);
DGTL_API void dgtl_join_task(uint64_t task_handle);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* DGTL_SDK_H */
