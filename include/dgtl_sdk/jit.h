/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Advanced JIT Execution Subsystem
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_JIT_H
#define DGTL_SDK_JIT_H

#include "types.h"
#include "target.h"
#include "object.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic Host Symbol Resolver Callback */
typedef uintptr_t (DGTL_CALL *dgtl_symbol_resolver_fn)(const char* name, void* user_data);

/* JIT Engine Lifecycle */
DGTL_NODISCARD DGTL_API dgtl_jit_engine_t DGTL_CALL dgtl_jit_engine_create(
    dgtl_context_t* ctx,
    dgtl_target_machine_t tm) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL dgtl_jit_engine_destroy(dgtl_jit_engine_t engine) DGTL_NOEXCEPT;

/* Symbol Resolution & Host Integration */
DGTL_API void DGTL_CALL dgtl_jit_engine_set_symbol_resolver(
    dgtl_jit_engine_t engine,
    dgtl_symbol_resolver_fn resolver,
    void* user_data) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL dgtl_jit_engine_register_symbol(
    dgtl_jit_engine_t engine,
    const char* name,
    uintptr_t address) DGTL_NOEXCEPT;

/* Ingestion & Execution */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_jit_engine_add_object(
    dgtl_jit_engine_t engine,
    dgtl_object_t obj) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_jit_engine_add_module(
    dgtl_jit_engine_t engine,
    dgtl_module_t mod) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_jit_engine_add_stream(
    dgtl_jit_engine_t engine,
    dgtl_stream_t* stream,
    const char* entry_symbol) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API uintptr_t DGTL_CALL dgtl_jit_engine_lookup(
    dgtl_jit_engine_t engine,
    const char* name) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_JIT_H */