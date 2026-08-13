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

#ifndef DGTL_SDK_SESSION_H
#define DGTL_SDK_SESSION_H

#include "dgtl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    dgtl_session_mode_t mode;
    dgtl_session_opt_level_t opt_level;
    uint64_t timeout_us;
    int enable_temporal_fusion;
    int enable_polyhedral_scop;
    int enable_speculation;
    int enable_vectorization;
    uint32_t thread_count;
    uint32_t reserved[8];
} dgtl_session_config_t;

typedef struct dgtl_host_symbol {
    const char* name;
    uint32_t slot;
    uintptr_t address;
    uint32_t flags;
} dgtl_host_symbol_t;

/* ---------- Session Configuration & Lifecycle ---------- */
DGTL_API dgtl_session_config_t dgtl_session_config_default(void);
DGTL_API const char*           dgtl_session_mode_name(dgtl_session_mode_t mode);

DGTL_API dgtl_session_t* dgtl_session_create(const dgtl_session_config_t* config);
DGTL_API void            dgtl_session_destroy(dgtl_session_t* session);

/* ---------- Host Symbol Registration ---------- */
DGTL_API dgtl_status_t dgtl_session_register_host_symbol(dgtl_session_t* session,
                                                         const char* symbol_name,
                                                         uintptr_t address);
DGTL_API dgtl_status_t dgtl_session_register_host_symbol_slot(dgtl_session_t* session,
                                                              uint32_t slot,
                                                              uintptr_t address);
DGTL_API dgtl_status_t dgtl_session_register_host_symbols(dgtl_session_t* session,
                                                          const dgtl_host_symbol_t* symbols,
                                                          size_t symbol_count);
DGTL_API void          dgtl_session_clear_host_symbols(dgtl_session_t* session);

/* ---------- Compilation & Execution ---------- */
DGTL_API dgtl_status_t dgtl_session_compile_stream(dgtl_session_t* session,
                                                   dgtl_stream_t* stream);
DGTL_API uint64_t      dgtl_session_get_symbol_address(dgtl_session_t* session,
                                                  const char* symbol_name);
DGTL_API uint64_t      dgtl_session_get_symbol_address_by_slot(dgtl_session_t* session,
                                                         uint32_t slot);

DGTL_API dgtl_status_t dgtl_session_emit_object(dgtl_session_t* session,
                                                const char* output_path);
DGTL_API dgtl_status_t dgtl_session_emit_binary(dgtl_session_t* session,
                                                const char* output_path);

/* Entry Execution Interfaces */
DGTL_API dgtl_status_t dgtl_session_run_entry_i64(dgtl_session_t* session,
                                                  const char* entry_symbol,
                                                  int64_t* out_result);
DGTL_API dgtl_status_t dgtl_session_run_entry_f64(dgtl_session_t* session,
                                                  const char* entry_symbol,
                                                  double* out_result);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_SESSION_H */