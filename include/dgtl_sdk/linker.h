/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Sovereign In-Memory Linker Driver
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_LINKER_H
#define DGTL_SDK_LINKER_H

#include "types.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dgtl_linker_output_kind {
    DGTL_LINK_EXECUTABLE = 0,
    DGTL_LINK_SHARED_LIB = 1,
    DGTL_LINK_STATIC_LIB = 2,
    DGTL_LINK_RELOCATABLE= 3
} dgtl_linker_output_kind_t;

typedef enum dgtl_subsystem {
    DGTL_SUBSYSTEM_CONSOLE = 0,
    DGTL_SUBSYSTEM_WINDOWS = 1,
    DGTL_SUBSYSTEM_NATIVE  = 2
} dgtl_subsystem_t;

/* Linker Lifecycle & Configuration */
DGTL_NODISCARD DGTL_API dgtl_linker_t DGTL_CALL dgtl_linker_create(dgtl_object_format_t format) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_linker_destroy(dgtl_linker_t linker) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_set_entry_point(dgtl_linker_t linker, const char* symbol) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_set_image_base(dgtl_linker_t linker, uint64_t image_base) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_set_subsystem(dgtl_linker_t linker, dgtl_subsystem_t subsys) DGTL_NOEXCEPT;

/* Adding Inputs */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_object(dgtl_linker_t linker, dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_object_file(dgtl_linker_t linker, const char* path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_archive(dgtl_linker_t linker, const char* archive_path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_library_search_path(dgtl_linker_t linker, const char* dir) DGTL_NOEXCEPT;

/* Linking & Emission */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_link_to_memory(
    dgtl_linker_t linker,
    dgtl_linker_output_kind_t kind,
    const uint8_t** out_bytes,
    size_t* out_size) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_link_to_file(
    dgtl_linker_t linker,
    dgtl_linker_output_kind_t kind,
    const char* output_path) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_LINKER_H */