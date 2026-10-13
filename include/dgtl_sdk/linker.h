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

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_object(dgtl_linker_t linker, dgtl_object_t obj) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_session(dgtl_linker_t linker, void* session) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_object_file(dgtl_linker_t linker, const char* path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_archive(dgtl_linker_t linker, const char* archive_path) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_library_search_path(dgtl_linker_t linker, const char* dir) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_linker_add_library(dgtl_linker_t linker, const char* lib_name) DGTL_NOEXCEPT;

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

#ifndef DGTL_LINKER_CLASS_DEFINED
#define DGTL_LINKER_CLASS_DEFINED
namespace dgtl {

class Linker {
public:
    explicit Linker(dgtl_object_format_t fmt = DGTL_OBJECT_FMT_PE)
        : linker_(dgtl_linker_create(fmt)) {}
    explicit Linker(dgtl_subsystem_t sub)
        : linker_(dgtl_linker_create(DGTL_OBJECT_FMT_PE)) {
        if (linker_) (void)dgtl_linker_set_subsystem(linker_, sub);
    }
    ~Linker() noexcept { if (linker_) dgtl_linker_destroy(linker_); }
    Linker(const Linker&) = delete;
    Linker& operator=(const Linker&) = delete;
    Linker(Linker&& o) noexcept : linker_(o.linker_) { o.linker_ = nullptr; }
    Linker& operator=(Linker&& o) noexcept {
        if (this != &o) { if (linker_) dgtl_linker_destroy(linker_); linker_ = o.linker_; o.linker_ = nullptr; }
        return *this;
    }
    [[nodiscard]] bool is_valid() const noexcept { return linker_ != nullptr; }
    [[nodiscard]] dgtl_linker_t get() const noexcept { return linker_; }
    dgtl_err_t set_entry_point(const char* sym) noexcept { return dgtl_linker_set_entry_point(linker_, sym); }
    dgtl_err_t set_image_base(uint64_t image_base) noexcept { return dgtl_linker_set_image_base(linker_, image_base); }
    dgtl_err_t set_subsystem(dgtl_subsystem_t sub) noexcept { return dgtl_linker_set_subsystem(linker_, sub); }
    dgtl_err_t add_object(dgtl_object_t obj) noexcept { return dgtl_linker_add_object(linker_, obj); }
    dgtl_err_t add_session(void* session) noexcept { return dgtl_linker_add_session(linker_, session); }
    dgtl_err_t add_object_file(const char* path) noexcept { return dgtl_linker_add_object_file(linker_, path); }
    dgtl_err_t add_archive(const char* path) noexcept { return dgtl_linker_add_archive(linker_, path); }
    dgtl_err_t add_library_search_path(const char* dir) noexcept { return dgtl_linker_add_library_search_path(linker_, dir); }
    dgtl_err_t add_library(const char* lib_name) noexcept { return dgtl_linker_add_library(linker_, lib_name); }
    dgtl_err_t link_to_file(dgtl_linker_output_kind_t kind, const char* out_path) noexcept {
        return dgtl_linker_link_to_file(linker_, kind, out_path);
    }
    dgtl_err_t link_to_memory(dgtl_linker_output_kind_t kind, const uint8_t** out_bytes, size_t* out_size) noexcept {
        return dgtl_linker_link_to_memory(linker_, kind, out_bytes, out_size);
    }
private:
    dgtl_linker_t linker_ = nullptr;
};

} // namespace dgtl
#endif /* DGTL_LINKER_CLASS_DEFINED */

#endif /* __cplusplus */

#endif /* DGTL_SDK_LINKER_H */