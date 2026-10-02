/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

// Global Module Fragment
#include <cstdint>

export module dgtl.ir.fwd;

// Re-export canonical stream and CFG primitives from dgtl.ir.stream to avoid
// conflicting module-linkage forward declarations across named modules.
export import dgtl.ir.stream;




