/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Common target-independent machine layer.
 * Primary Module Interface aggregating all canonical Machine IR partitions.
 *
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

export module dgtl.mir.machine_ir;

// Export all six partitions for full coverage and zero breakage
export import :types;
export import :target;
export import :function;
export import :analysis;
export import :verifier;
export import :allocation;