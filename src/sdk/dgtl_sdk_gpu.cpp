/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public SDK GPU & Platform C-ABI Module
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl_sdk/dgtl_gpu.h"
#include "dgtl_sdk/dgtl.h"

extern "C" {

DGTL_API const char* dgtl_sdk_gpu_version(void) {
    return "DGTL-GPU-Sovereign-v1.0.0";
}

}
