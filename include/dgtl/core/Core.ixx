/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Canonical Core Aggregate Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

export module dgtl.core;

export import dgtl.arena;
export import dgtl.target;
export import dgtl.compiler_context;
export import dgtl.numa;
export import dgtl.virtual_memory;
export import dgtl.memory_capability;
export import dgtl.memory_manager;
export import dgtl.ir_core.abi;
