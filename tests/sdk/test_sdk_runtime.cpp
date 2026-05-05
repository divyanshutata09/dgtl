/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

#include "dgtl_sdk/dgtl.h"
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TaskPayload {
    std::atomic<uint32_t>* counter;
};

void runtime_increment_task(void* raw) {
    auto* payload = reinterpret_cast<TaskPayload*>(raw);
    payload->counter->fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

void run_test_sdk_runtime_scheduler() {
    dgtl_runtime_context_t* runtime = dgtl_runtime_context_create(2, 32);
    require(runtime != nullptr, "runtime context creation failed");

    // Test 1: Simple single task
    std::atomic<uint32_t> counter{0u};
    TaskPayload payload{&counter};
    uint64_t handle = dgtl_runtime_spawn_task(runtime, runtime_increment_task, &payload);
    require(handle != 0ull, "spawn task failed");
    dgtl_runtime_join_task(runtime, handle);
    require(counter.load(std::memory_order_relaxed) == 1u, "task did not execute");

    // Test 2: Multiple tasks (64 tasks with proper synchronization)
    constexpr size_t kTaskCount = 64;
    std::vector<TaskPayload> payloads(kTaskCount);
    std::vector<uint64_t> handles(kTaskCount);
    std::atomic<uint32_t> multi_counter{0u};

    for (size_t i = 0; i < kTaskCount; ++i) {
        payloads[i].counter = &multi_counter;
        handles[i] = dgtl_runtime_spawn_task(runtime, runtime_increment_task, &payloads[i]);
        require(handles[i] != 0ull, "spawn task failed");
    }

    // Join all tasks
    for (uint64_t h : handles) {
        dgtl_runtime_join_task(runtime, h);
    }

    require(multi_counter.load(std::memory_order_relaxed) == kTaskCount,
            "not all tasks executed");

    dgtl_runtime_yield(runtime);
    dgtl_runtime_context_destroy(runtime);
}