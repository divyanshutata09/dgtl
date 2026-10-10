/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Target Machine & Hardware Model
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_TARGET_H
#define DGTL_SDK_TARGET_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dgtl_reloc_model {
    DGTL_RELOC_DEFAULT = 0,
    DGTL_RELOC_STATIC  = 1,
    DGTL_RELOC_PIC     = 2,
    DGTL_RELOC_DYNAMIC = 3
} dgtl_reloc_model_t;

typedef enum dgtl_code_model {
    DGTL_CODE_MODEL_DEFAULT = 0,
    DGTL_CODE_MODEL_SMALL   = 1,
    DGTL_CODE_MODEL_KERNEL  = 2,
    DGTL_CODE_MODEL_MEDIUM  = 3,
    DGTL_CODE_MODEL_LARGE   = 4
} dgtl_code_model_t;

typedef enum dgtl_calling_conv {
    DGTL_CC_C          = 0,
    DGTL_CC_SYSTEM_V   = 1,
    DGTL_CC_WIN64      = 2,
    DGTL_CC_FAST       = 3,
    DGTL_CC_DEVICE     = 4
} dgtl_calling_conv_t;

typedef enum dgtl_target_arch {
    DGTL_ARCH_X86_64 = 0,
    DGTL_ARCH_ARM64  = 1,
    DGTL_ARCH_ARM32  = 2,
    DGTL_ARCH_WASM   = 3,
    DGTL_ARCH_RISCV64= 4,
    DGTL_ARCH_CUDA   = 5,
    DGTL_ARCH_ROCM   = 6,
    DGTL_ARCH_FPGA   = 7
} dgtl_target_arch_t;

#ifndef DGTL_SDK_H
typedef enum dgtl_opt_level {
    DGTL_OPT_O0      = 0,
    DGTL_OPT_O1      = 1,
    DGTL_OPT_O2      = 2,
    DGTL_OPT_O3      = 3,
    DGTL_OPT_OSTRIKE = 4,
    DGTL_OPT_OS      = 5,
    DGTL_OPT_OZ      = 6
} dgtl_opt_level_t;
#endif

/* Target Feature Strings */
#define DGTL_FEATURE_AVX2        "avx2"
#define DGTL_FEATURE_AVX512      "avx512"
#define DGTL_FEATURE_BMI2        "bmi2"
#define DGTL_FEATURE_NEON        "neon"
#define DGTL_FEATURE_SVE         "sve"
#define DGTL_FEATURE_RVV         "rvv"
#define DGTL_FEATURE_WASM_SIMD   "wasm_simd"
#define DGTL_FEATURE_FMA         "fma"
#define DGTL_FEATURE_ATOMICS     "atomics"
#define DGTL_FEATURE_AMX         "amx"

/* Target Machine Lifecycle & Query */
DGTL_NODISCARD DGTL_API dgtl_target_machine_t DGTL_CALL dgtl_target_machine_create(
    const char* triple,
    const char* cpu,
    const char* features,
    dgtl_opt_level_t opt_level,
    dgtl_reloc_model_t reloc_model,
    dgtl_code_model_t code_model) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_target_machine_t DGTL_CALL dgtl_target_machine_create_host(void) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL dgtl_target_machine_destroy(dgtl_target_machine_t tm) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_target_machine_get_triple(dgtl_target_machine_t tm) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_target_machine_get_cpu(dgtl_target_machine_t tm) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_target_machine_has_feature(dgtl_target_machine_t tm, const char* feature) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint32_t DGTL_CALL dgtl_target_machine_get_pointer_size(dgtl_target_machine_t tm) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_target_machine_is_64bit(dgtl_target_machine_t tm) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_target_machine_is_little_endian(dgtl_target_machine_t tm) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API dgtl_target_arch_t DGTL_CALL dgtl_target_machine_get_arch(dgtl_target_machine_t tm) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_TARGET_H */