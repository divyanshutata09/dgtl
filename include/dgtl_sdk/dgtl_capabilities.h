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

#ifndef DGTL_SDK_CAPABILITIES_H
#define DGTL_SDK_CAPABILITIES_H

#include "dgtl.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DGTL_CAPABILITY_SCHEMA_VERSION 1u

typedef struct dgtl_capabilities {
    uint32_t schema_version;
    uint32_t supports_jit;
    uint32_t supports_aot;
    uint32_t supports_hybrid;
    uint32_t supports_host_symbol_name_binding;
    uint32_t supports_host_symbol_slot_binding;
    uint32_t supports_object_emission;
    uint32_t supports_binary_emission;
    uint32_t supports_in_process_execution;
    uint32_t supports_cfg_introspection;
    uint32_t supports_per_pass_control;
    uint32_t supports_speculative_constants;
    uint32_t max_call_arity;
    uint64_t backend_target_mask;
    uint64_t optimization_profile_mask;
    uint32_t reserved[7];
} dgtl_capabilities_t;

DGTL_API dgtl_capabilities_t dgtl_get_capabilities(void);
DGTL_API int dgtl_capabilities_support_target(const dgtl_capabilities_t* caps,
                                               dgtl_target_t target);

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_CAPABILITIES_H */