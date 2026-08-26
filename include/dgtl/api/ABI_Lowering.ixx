/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

export module dgtl.internal.api_abi_lowering;

// Re-export the ABI lowering abstractions straight from the consolidated passes layer
export import dgtl.ir_core;
export import dgtl.ir_core.abi_lowering;