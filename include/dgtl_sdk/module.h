/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Multi-Function Module Container
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_MODULE_H
#define DGTL_SDK_MODULE_H

#include "types.h"
#include "target.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dgtl_linkage {
    DGTL_LINKAGE_EXTERNAL = 0,
    DGTL_LINKAGE_INTERNAL = 1,
    DGTL_LINKAGE_WEAK     = 2,
    DGTL_LINKAGE_APPEND   = 3
} dgtl_linkage_t;

/* Module Lifecycle */
DGTL_NODISCARD DGTL_API dgtl_module_t DGTL_CALL dgtl_module_create(dgtl_context_t* ctx, const char* module_id) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_module_destroy(dgtl_module_t mod) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_module_get_id(dgtl_module_t mod) DGTL_NOEXCEPT;

/* Functions & Globals Registration */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_module_add_function(
    dgtl_module_t mod,
    const char* name,
    dgtl_stream_t* body,
    dgtl_linkage_t linkage) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_module_add_global_data(
    dgtl_module_t mod,
    const char* name,
    const uint8_t* data,
    size_t size,
    size_t alignment,
    bool is_constant,
    dgtl_linkage_t linkage) DGTL_NOEXCEPT;

/* Whole-Module Codegen */
DGTL_NODISCARD DGTL_API dgtl_object_t DGTL_CALL dgtl_module_compile_to_object(
    dgtl_module_t mod,
    dgtl_target_machine_t tm) DGTL_NOEXCEPT;

/* Module Introspection */
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_module_get_function_count(dgtl_module_t mod) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_module_get_function_name(dgtl_module_t mod, size_t index) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_module_get_global_count(dgtl_module_t mod) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_module_get_global_name(dgtl_module_t mod, size_t index) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_MODULE_H */