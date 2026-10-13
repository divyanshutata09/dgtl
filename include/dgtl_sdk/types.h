/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Core Types & Handles
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_TYPES_H
#define DGTL_SDK_TYPES_H

/* ============================================================================
 * SECTION 0 â€” PLATFORM DETECTION, VISIBILITY & ATTRIBUTE MACROS
 * ========================================================================== */

#ifndef DGTL_API
#  if defined(_WIN32) || defined(_WIN64)
#    if defined(DGTL_BUILD_SHARED)
#      define DGTL_API   __declspec(dllexport)
#    elif defined(DGTL_IMPORT_SHARED)
#      define DGTL_API   __declspec(dllimport)
#    else
#      define DGTL_API
#    endif
#    define DGTL_CALL    __cdecl
#    define DGTL_WEAK
#  elif defined(__GNUC__) || defined(__clang__)
#    define DGTL_API     __attribute__((visibility("default")))
#    define DGTL_CALL
#    define DGTL_WEAK    __attribute__((weak))
#  else
#    define DGTL_API
#    define DGTL_CALL
#    define DGTL_WEAK
#  endif
#endif

#ifndef DGTL_WARN_UNUSED
#  if defined(__GNUC__) || defined(__clang__)
#    define DGTL_WARN_UNUSED     __attribute__((warn_unused_result))
#    define DGTL_DEPRECATED(m)   __attribute__((deprecated(m)))
#    define DGTL_NONNULL(...)    
#    define DGTL_PURE            __attribute__((pure))
#    define DGTL_CONST_FN        __attribute__((const))
#    define DGTL_LIKELY(x)       __builtin_expect(!!(x), 1)
#    define DGTL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#    define DGTL_RETURNS_NONNULL __attribute__((returns_nonnull))
#  elif defined(_MSC_VER)
#    define DGTL_WARN_UNUSED     _Check_return_
#    define DGTL_DEPRECATED(m)   __declspec(deprecated(m))
#    define DGTL_NONNULL(...)
#    define DGTL_PURE
#    define DGTL_CONST_FN
#    define DGTL_LIKELY(x)       (x)
#    define DGTL_UNLIKELY(x)     (x)
#    define DGTL_RETURNS_NONNULL
#  else
#    define DGTL_WARN_UNUSED
#    define DGTL_DEPRECATED(m)
#    define DGTL_NONNULL(...)
#    define DGTL_PURE
#    define DGTL_CONST_FN
#    define DGTL_LIKELY(x)       (x)
#    define DGTL_UNLIKELY(x)     (x)
#    define DGTL_RETURNS_NONNULL
#  endif
#endif

#ifndef DGTL_NOEXCEPT
#  ifdef __cplusplus
#    define DGTL_NOEXCEPT    noexcept
#    define DGTL_NODISCARD   [[nodiscard]]
#  else
#    define DGTL_NOEXCEPT
#    define DGTL_NODISCARD   DGTL_WARN_UNUSED
#  endif
#endif

/* ============================================================================
 * SECTION 1 â€” C STANDARD INCLUDES & ABI CONSTANTS
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef DGTL_VERSION_MAJOR
#  define DGTL_VERSION_MAJOR      2u
#  define DGTL_VERSION_MINOR      1u
#  define DGTL_VERSION_PATCH      0u
#  define DGTL_VERSION_SUFFIX     "-SOVEREIGN"
#  define DGTL_VERSION_STRING     "2.1.0-SOVEREIGN"
#  define DGTL_ABI_VERSION        4u
#  define DGTL_ABI_MIN_COMPAT     3u
#endif

#ifndef DGTL_INVALID_SLOT
#  define DGTL_INVALID_SLOT       (0xFFFFFFFFu)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SECTION 2 â€” VALUE & COMPILER ID HANDLES
 * ========================================================================== */

#ifndef DGTL_SDK_H
typedef struct { uint32_t id; } dgtl_value_t;
typedef struct { uint32_t id; } dgtl_block_t;
typedef struct { uint32_t id; } dgtl_type_t;
typedef struct { uint32_t id; } dgtl_func_t;
typedef struct { uint32_t id; } dgtl_str_t;
typedef struct { uint32_t id; } dgtl_pass_t;

#ifdef __cplusplus
#  define DGTL_INVALID_VALUE  (dgtl_value_t{0xFFFFFFFFu})
#  define DGTL_INVALID_BLOCK  (dgtl_block_t{0xFFFFFFFFu})
#  define DGTL_INVALID_TYPE   (dgtl_type_t {0xFFFFFFFFu})
#  define DGTL_INVALID_FUNC   (dgtl_func_t {0xFFFFFFFFu})
#  define DGTL_INVALID_STR    (dgtl_str_t  {0xFFFFFFFFu})
#  define DGTL_INVALID_PASS   (dgtl_pass_t {0xFFFFFFFFu})
#else
#  define DGTL_INVALID_VALUE  ((dgtl_value_t){0xFFFFFFFFu})
#  define DGTL_INVALID_BLOCK  ((dgtl_block_t){0xFFFFFFFFu})
#  define DGTL_INVALID_TYPE   ((dgtl_type_t ){0xFFFFFFFFu})
#  define DGTL_INVALID_FUNC   ((dgtl_func_t ){0xFFFFFFFFu})
#  define DGTL_INVALID_STR    ((dgtl_str_t  ){0xFFFFFFFFu})
#  define DGTL_INVALID_PASS   ((dgtl_pass_t ){0xFFFFFFFFu})
#endif

static inline bool dgtl_value_is_valid(dgtl_value_t v) { return v.id != 0xFFFFFFFFu; }
static inline bool dgtl_block_is_valid(dgtl_block_t b) { return b.id != 0xFFFFFFFFu; }
static inline bool dgtl_type_is_valid(dgtl_type_t  t) { return t.id != 0xFFFFFFFFu && t.id != 0u; }
static inline bool dgtl_func_is_valid(dgtl_func_t  f) { return f.id != 0xFFFFFFFFu; }
static inline bool dgtl_pass_is_valid(dgtl_pass_t  p) { return p.id != 0xFFFFFFFFu && p.id != 0u; }
#endif /* DGTL_SDK_H */

/* ============================================================================
 * SECTION 3 â€” OPAQUE STRUCT / SUBSYSTEM HANDLES
 * ========================================================================== */

#ifndef DGTL_SDK_H
/* Compilation & JIT context */
typedef struct dgtl_context          dgtl_context_t;
typedef struct dgtl_stream           dgtl_stream_t;
typedef struct dgtl_session          dgtl_session_t;
typedef struct dgtl_runtime_context  dgtl_runtime_context_t;
typedef struct dgtl_func_builder     dgtl_func_builder_t;
#endif

/* Target Machine & Hardware Model */
typedef struct dgtl_target_machine*  dgtl_target_machine_t;

/* Multi-Function Module */
typedef struct dgtl_module*          dgtl_module_t;

/* Universal Object File Subsystem */
typedef struct dgtl_object*          dgtl_object_t;
typedef struct dgtl_section*         dgtl_section_t;
typedef struct dgtl_symbol*          dgtl_symbol_t;
typedef struct dgtl_relocation*      dgtl_relocation_t;

/* Sovereign In-Memory Linker Subsystem */
typedef struct dgtl_linker*          dgtl_linker_t;

/* Advanced JIT Execution Subsystem */
typedef struct dgtl_jit_engine*      dgtl_jit_engine_t;

/* Machine IR Introspection */
typedef struct dgtl_machine_func*    dgtl_machine_func_t;

/* Pass Manager Pipeline */
typedef struct dgtl_pass_manager*    dgtl_pass_manager_t;

/* ============================================================================
 * SECTION 4 â€” ERROR CODES & DIAGNOSTIC RECORDS
 * ========================================================================== */

#ifndef DGTL_SDK_H
typedef enum dgtl_severity {
    DGTL_SEV_INFO    = 0,
    DGTL_SEV_WARNING = 1,
    DGTL_SEV_ERROR   = 2,
    DGTL_SEV_FATAL   = 3
} dgtl_severity_t;

typedef struct dgtl_err {
    int32_t         code;
    dgtl_severity_t severity;
} dgtl_err_t;

typedef dgtl_err_t dgtl_status_t;

#ifdef __cplusplus
#  define DGTL_OK          (dgtl_err_t{0, DGTL_SEV_INFO})
#  define DGTL_ERR(c)      (dgtl_err_t{(int32_t)(c), DGTL_SEV_ERROR})
#  define DGTL_WARN(c)     (dgtl_err_t{(int32_t)(c), DGTL_SEV_WARNING})
extern "C++" {
inline bool operator==(const dgtl_err& a, const dgtl_err& b) noexcept {
    return a.code == b.code;
}
inline bool operator!=(const dgtl_err& a, const dgtl_err& b) noexcept {
    return a.code != b.code;
}
}
#else
#  define DGTL_OK          ((dgtl_err_t){0, DGTL_SEV_INFO})
#  define DGTL_ERR(c)      ((dgtl_err_t){(int32_t)(c), DGTL_SEV_ERROR})
#  define DGTL_WARN(c)     ((dgtl_err_t){(int32_t)(c), DGTL_SEV_WARNING})
#endif

#define DGTL_IS_OK(e)      ((e).code == 0)
#define DGTL_IS_ERR(e)     ((e).code != 0)

typedef struct dgtl_diag {
    dgtl_severity_t severity;
    uint32_t        node_index;
    uint32_t        source_line;
    uint16_t        source_col;
    uint16_t        code;
    const char*     message;
} dgtl_diag_t;

typedef struct dgtl_text {
    char*  data;
    size_t length;
} dgtl_text_t;

DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_err_message(dgtl_err_t err) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_get_last_error(void) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_free_text(dgtl_text_t text) DGTL_NOEXCEPT;
#endif /* DGTL_SDK_H */

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_TYPES_H */