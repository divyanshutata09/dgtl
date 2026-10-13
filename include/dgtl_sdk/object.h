/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Universal Object File & Binary Subsystem
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_OBJECT_H
#define DGTL_SDK_OBJECT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dgtl_object_format {
    DGTL_OBJECT_FMT_PE    = 0,
    DGTL_OBJECT_FMT_COFF  = 1,
    DGTL_OBJECT_FMT_ELF   = 2,
    DGTL_OBJECT_FMT_ELF64 = 2,
    DGTL_OBJECT_FMT_MACHO = 3,
    DGTL_OBJECT_FMT_WASM  = 4
} dgtl_object_format_t;

typedef enum dgtl_section_flags {
    DGTL_SEC_FLAG_NONE       = 0,
    DGTL_SEC_FLAG_READ       = (1 << 0),
    DGTL_SEC_FLAG_WRITE      = (1 << 1),
    DGTL_SEC_FLAG_EXECUTE    = (1 << 2),
    DGTL_SEC_FLAG_CODE       = (1 << 3),
    DGTL_SEC_FLAG_DATA       = (1 << 4),
    DGTL_SEC_FLAG_BSS        = (1 << 5)
} dgtl_section_flags_t;

typedef enum dgtl_symbol_binding {
    DGTL_SYM_BIND_LOCAL  = 0,
    DGTL_SYM_BIND_GLOBAL = 1,
    DGTL_SYM_BIND_WEAK   = 2
} dgtl_symbol_binding_t;

typedef enum dgtl_symbol_type {
    DGTL_SYM_TYPE_NOTYPE = 0,
    DGTL_SYM_TYPE_FUNC   = 1,
    DGTL_SYM_TYPE_OBJECT = 2,
    DGTL_SYM_TYPE_SECTION= 3
} dgtl_symbol_type_t;

/* Object Lifecycle & Basic Queries */
DGTL_NODISCARD DGTL_API dgtl_object_t DGTL_CALL dgtl_object_create(dgtl_object_format_t format) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_object_t DGTL_CALL dgtl_object_load_from_file(const char* path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_object_t DGTL_CALL dgtl_object_load_from_memory(const uint8_t* bytes, size_t size) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_object_destroy(dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_object_format_t DGTL_CALL dgtl_object_get_format(dgtl_object_t obj) DGTL_NOEXCEPT;

/* Section Operations */
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_object_get_section_count(dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_section_t DGTL_CALL dgtl_object_get_section(dgtl_object_t obj, size_t index) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_section_t DGTL_CALL dgtl_object_find_section(dgtl_object_t obj, const char* name) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_section_t DGTL_CALL dgtl_object_add_section(dgtl_object_t obj, const char* name, uint64_t align, uint32_t flags) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_section_get_name(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_section_get_size(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const uint8_t* DGTL_CALL dgtl_section_get_data(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_section_get_flags(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_section_get_bss_size(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_API dgtl_err_t DGTL_CALL dgtl_section_set_bss_size(dgtl_section_t sec, uint64_t size) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_section_get_alignment(dgtl_section_t sec) DGTL_NOEXCEPT;
DGTL_API dgtl_err_t DGTL_CALL dgtl_section_append_data(dgtl_section_t sec, const uint8_t* data, size_t size) DGTL_NOEXCEPT;

/* Symbol Operations */
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_object_get_symbol_count(dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_symbol_t DGTL_CALL dgtl_object_get_symbol(dgtl_object_t obj, size_t index) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_symbol_t DGTL_CALL dgtl_object_find_symbol(dgtl_object_t obj, const char* name) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_symbol_t DGTL_CALL dgtl_object_add_symbol(
    dgtl_object_t obj,
    const char* name,
    uint64_t value,
    uint64_t size,
    uint32_t section_idx,
    uint8_t binding,
    uint8_t type) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_symbol_get_name(dgtl_symbol_t sym) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_symbol_get_value(dgtl_symbol_t sym) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_symbol_get_size(dgtl_symbol_t sym) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_symbol_get_section_index(dgtl_symbol_t sym) DGTL_NOEXCEPT;

/* Relocation Operations */
DGTL_NODISCARD DGTL_API size_t DGTL_CALL dgtl_object_get_relocation_count(dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_relocation_t DGTL_CALL dgtl_object_get_relocation(dgtl_object_t obj, size_t index) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint64_t DGTL_CALL dgtl_relocation_get_offset(dgtl_relocation_t rel) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_relocation_get_symbol_name(dgtl_relocation_t rel) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_relocation_get_symbol_index(dgtl_relocation_t rel) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL dgtl_relocation_get_addend(dgtl_relocation_t rel) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_relocation_get_type(dgtl_relocation_t rel) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_relocation_get_section_index(dgtl_relocation_t rel) DGTL_NOEXCEPT;

/* Binary Synthesis */
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_object_emit_to_memory(
    dgtl_object_t obj,
    const uint8_t** out_bytes,
    size_t* out_size) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_object_emit_to_file(
    dgtl_object_t obj,
    const char* path) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_OBJECT_H */