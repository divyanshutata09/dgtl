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
 * dgtl.h — DGTL SDK Public Entry Point
 *
 * This is the single include for C and C++ consumers of the DGTL SDK.
 * It exposes the stable C ABI surface for compatibility IR construction, backend
 * emission, runtime execution, advanced IR inspection, and SIMD helpers.
 *
 * For C++ clients, prefer dgtl::api::IRBuilder as the primary entrypoint.
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
#define DGTL_VERSION_MINOR  2
#define DGTL_VERSION_PATCH  0
#define DGTL_VERSION_STRING "1.2.0"

DGTL_API const char* dgtl_version_string(void);

/* ---------- Opaque Handles ---------- */
typedef struct dgtl_context  dgtl_context_t;
typedef struct dgtl_stream   dgtl_stream_t;
typedef struct dgtl_function dgtl_function_t;

/* ---------- OpCode Enum Flags ---------- */
#ifndef DGTL_FLAG_NONE
#define DGTL_FLAG_NONE           0x0000u
#define DGTL_FLAG_IS_CONST       0x0001u
#define DGTL_FLAG_IS_SPEC_CONST  0x0100u
#endif

/* ---------- OpCode Enums for Generic Emission ---------- */
/* Canonical opcode enum — kept in sync with dgtl_core.h.                  */
/* The hex-based values below are the stable ABI encoding; the guard macro */
/* DGTL_OPCODE_DEFINED prevents redefinition when dgtl_core.h is included  */
/* afterwards.                                                              */
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

/* ---------- Backend Targets ---------- */
#ifndef DGTL_TARGET_DEFINED
#define DGTL_TARGET_DEFINED
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
#endif

DGTL_API const char* dgtl_target_name(dgtl_target_t target);
DGTL_API const char* dgtl_target_extension(dgtl_target_t target);

/* ---------- Error Handling ---------- */
#ifndef DGTL_STATUS_DEFINED
#define DGTL_STATUS_DEFINED
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

/* ---------- Context Lifecycle ---------- */
DGTL_API dgtl_context_t*  dgtl_context_create(void);
DGTL_API void             dgtl_context_destroy(dgtl_context_t* ctx);

/* ---------- Stream Lifecycle ---------- */
DGTL_API dgtl_stream_t*   dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes);
DGTL_API void              dgtl_stream_destroy(dgtl_stream_t* stream);
DGTL_API size_t            dgtl_stream_node_count(const dgtl_stream_t* stream);
DGTL_API uint64_t          dgtl_stream_logic_hash(const dgtl_stream_t* stream);

/* ---------- Generic OpCode Emission ---------- */
/* Ensure C-ABI parameters match dgtl_core.h */
DGTL_API uint32_t dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t opcode, uint32_t op_a, uint32_t op_b, uint64_t imm, uint32_t flags);
DGTL_API uint32_t dgtl_emit_op_with_result(dgtl_stream_t* s, uint32_t result_slot, dgtl_opcode_t opcode, uint32_t op_a, uint32_t op_b, uint64_t imm, uint32_t flags);

/* ---------- IR Emission (Building the Stream) ---------- */
DGTL_API uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value);
DGTL_API uint32_t dgtl_emit_const_f64(dgtl_stream_t* s, double value);
DGTL_API uint32_t dgtl_emit_const_f32(dgtl_stream_t* s, float value);

/* Memory Operations */
DGTL_API uint32_t dgtl_emit_load(dgtl_stream_t* s, uint32_t ptr_slot);
DGTL_API uint32_t dgtl_emit_store(dgtl_stream_t* s, uint32_t ptr_slot, uint32_t val_slot);

/* Integer 32-bit Arithmetic */
DGTL_API uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Integer 64-bit Arithmetic */
DGTL_API uint32_t dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_div_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_mod_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Floating-Point Arithmetic — 32-bit & 64-bit */
DGTL_API uint32_t dgtl_emit_fadd(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fsub(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fmul(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fdiv(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

DGTL_API uint32_t dgtl_emit_fadd_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fsub_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fmul_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_fdiv_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Bitwise / shift — I32 */
DGTL_API uint32_t dgtl_emit_and_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_or_i32 (dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_xor_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_shl_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_shr_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Bitwise / shift — I64 */
DGTL_API uint32_t dgtl_emit_and_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_or_i64 (dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_xor_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_shl_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t dgtl_emit_shr_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* Control Flow & Calls */
DGTL_API uint32_t dgtl_emit_label(dgtl_stream_t* s, const char* label_name);
DGTL_API uint32_t dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot);
DGTL_API uint32_t dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot);
DGTL_API uint32_t dgtl_emit_function_label_with_params(dgtl_stream_t* s, uint32_t function_slot, const uint32_t* params, size_t param_count);
DGTL_API void     dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label_slot);
DGTL_API void     dgtl_emit_branch_cond(dgtl_stream_t* s, uint32_t cond_slot, uint32_t target_label_slot);
DGTL_API uint32_t dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t dgtl_emit_call_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API void     dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot);

/* String Handling */
DGTL_API uint32_t dgtl_intern_string_handle_utf8(const char* data, size_t len);
DGTL_API uint32_t dgtl_emit_string_const_handle(dgtl_stream_t* s, uint32_t string_handle);
DGTL_API uint32_t dgtl_emit_string_const_utf8(dgtl_stream_t* s, const char* utf8_literal);
DGTL_API uint32_t dgtl_emit_string_length(dgtl_stream_t* s, uint32_t string_slot);
DGTL_API uint32_t dgtl_emit_string_compare(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t dgtl_emit_string_concat(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t dgtl_emit_string_substring(dgtl_stream_t* s,
                                              uint32_t string_slot,
                                              uint32_t start_slot,
                                              uint32_t length_slot);

/* ---------- Optimization ---------- */
DGTL_API dgtl_status_t dgtl_optimize(dgtl_stream_t* s);
DGTL_API dgtl_status_t dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name);

/* ---------- Backend Strike (Compile to Target) ---------- */
DGTL_API dgtl_strike_result_t dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target);

/* ---------- File & Direct Memory Binary Emission ---------- */
DGTL_API dgtl_status_t dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target,
                                          const char* output_path);
DGTL_API uint32_t dgtl_emit_string_const_rodata_rip(dgtl_stream_t* s, const char* str, size_t len);
DGTL_API dgtl_status_t dgtl_emit_pe_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API dgtl_status_t dgtl_emit_elf_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API dgtl_status_t dgtl_emit_macho_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size);
DGTL_API const char* dgtl_try_lookup_string_handle(uint32_t handle, size_t* out_len);
DGTL_API void dgtl_register_string_handle(uint32_t handle, const char* data, size_t len);
DGTL_API void dgtl_free_binary_buffer(void* buf);

/* ---------- Stream Serialization & Verification ---------- */
DGTL_API dgtl_status_t dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size);
DGTL_API dgtl_status_t dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t size);
DGTL_API void          dgtl_free_serialized_blob(void* blob);
DGTL_API int32_t       dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* err_buf, size_t err_buf_size);
DGTL_API int32_t       dgtl_verify_pass_equivalence(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass_name);
DGTL_API size_t        dgtl_export_smtlib2_script(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass_name, char* out_buf, size_t out_buf_size);

/* ---------- Callee-Slot Symbol Binding ---------- */
#ifndef DGTL_CALLEE_BINDING_DEFINED
#define DGTL_CALLEE_BINDING_DEFINED
typedef struct dgtl_callee_binding {
    uint32_t    slot;        /* stable call-slot ID, as returned by dgtl_emit_call */
    const char* symbol_name; /* NUL-terminated symbol the slot resolves to */
} dgtl_callee_binding_t;
#endif

DGTL_API dgtl_status_t dgtl_stream_bind_callee_slot(dgtl_stream_t*  stream,
                                                     uint32_t        slot,
                                                     const char*     symbol_name);

DGTL_API dgtl_status_t dgtl_stream_bind_callee_slots(dgtl_stream_t*               stream,
                                                      const dgtl_callee_binding_t* bindings,
                                                      size_t                       binding_count);

/* ---------- Linker Fallback Diagnostics ---------- */
DGTL_API size_t dgtl_last_linker_fallback_count(void);
DGTL_API size_t dgtl_last_linker_fallback_message(size_t index,
                                                  char* out_buffer,
                                                  size_t out_buffer_size);

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

/* ---------- NUMA Capabilities & Control ---------- */
DGTL_API uint32_t dgtl_numa_node_count(void);
DGTL_API uint32_t dgtl_numa_current_node(void);
DGTL_API int      dgtl_numa_pin_thread_to_node(uint32_t node_id);
DGTL_API int      dgtl_numa_pin_thread_to_core(uint32_t core_id);
DGTL_API void*    dgtl_numa_alloc_on_node(size_t size, uint32_t node_id);
DGTL_API void     dgtl_numa_free_on_node(void* ptr, size_t size);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "dgtl_core.h"
#include "dgtl_c_api.h"
#include "dgtl_capabilities.h"
#include "dgtl_session.h"
#include "dgtl_simd.h"
#include "dgtl_gpu.h"

#endif /* DGTL_SDK_H */