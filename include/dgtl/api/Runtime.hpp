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

#pragma once

//
// dgtl/api/Runtime.hpp
//
// Public C++ API tier for the DGTL native task scheduler.
//
// Consumers (e.g. MYZON's compile-time interpreter) include this header
// to access dgtl::runtime::TaskScheduler without reaching into the internal
// include tree directly.  This is the canonical stable surface:
//
//   #include "dgtl/api/Runtime.hpp"
//
//   dgtl::runtime::TaskScheduler sched;          // default: hw_concurrency workers
//   auto h = sched.spawn(my_fn, my_ctx);         // launch a task
//   sched.yield();                               // cooperative yield on current thread
//   sched.join(h);                               // wait for completion
//
// The implementation lives in src/runtime/TaskScheduler.cpp and must be
// compiled into any binary that links against this API.
//

#include "dgtl/runtime/TaskScheduler.hpp"

// Bring the scheduler into the dgtl::api namespace as an alias so callers
// can use either dgtl::api::TaskScheduler or dgtl::runtime::TaskScheduler.
namespace dgtl::api {
    using TaskScheduler = dgtl::runtime::TaskScheduler;
    using TaskEntryFn   = dgtl::runtime::TaskEntryFn;
} // namespace dgtl::api
