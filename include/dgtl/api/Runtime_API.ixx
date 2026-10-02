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

#include <cstdint>
#include <cstddef>

export module dgtl.api.runtime;

// Import core types and hardware description layers natively
import dgtl.ir;
import dgtl.runtime.task_scheduler;
import dgtl.runtime.numa;

// Bring the scheduler into the dgtl::api namespace as an alias so callers
// can use either dgtl::api::TaskScheduler or dgtl::runtime::TaskScheduler.
export namespace dgtl::api {
    using TaskScheduler = dgtl::runtime::TaskScheduler;
    using TaskEntryFn = dgtl::runtime::TaskEntryFn;
    using ParallelLoopFn = dgtl::runtime::ParallelLoopFn;
    using RuntimeStats = dgtl::runtime::RuntimeStats;
    namespace numa = dgtl::runtime::numa;
}