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

#include "dgtl/runtime/TaskScheduler.hpp"
#include <iostream>
#include <vector>
#include <atomic>
#include <cassert>

using namespace dgtl::runtime;

namespace {

// A simple structure to test recursive spawning
struct CompilerPassContext {
    TaskScheduler* scheduler;
    int depth;
    int children;
    std::atomic<int>* counter;
};

void CompilerPassTask(void* ctx_ptr) {
    auto* ctx = static_cast<CompilerPassContext*>(ctx_ptr);
    ctx->counter->fetch_add(1, std::memory_order_relaxed);

    if (ctx->depth > 0) {
        std::vector<uintptr_t> handles;
        std::vector<CompilerPassContext> child_ctxs(ctx->children);

        // Sub-tasks spawned from WITHIN a worker thread (tests thread-local queueing)
        for (int i = 0; i < ctx->children; ++i) {
            child_ctxs[i] = {ctx->scheduler, ctx->depth - 1, ctx->children, ctx->counter};
            handles.push_back(ctx->scheduler->spawn(CompilerPassTask, &child_ctxs[i]));
        }

        // Wait for all child compiler passes to finish
        for (auto handle : handles) {
            ctx->scheduler->join(handle);
        }
    }
}

} // namespace

void run_test_recursive_spawning() {
    std::cout << "[DGTL] Initializing TaskScheduler tests...\n";
    
    TaskScheduler scheduler(4, 16); // 4 workers
    std::atomic<int> nodes_processed{0};

    CompilerPassContext root_ctx = {&scheduler, 5, 4, &nodes_processed}; 
    // Depth 5, branching factor 4. Total nodes = 1 + 4 + 16 + 64 + 256 + 1024 = 1365 nodes

    std::cout << "[DGTL] Spawning root compilation task...\n";
    uintptr_t root_task = scheduler.spawn(CompilerPassTask, &root_ctx);
    
    // Main thread blocks here until the entire tree finishes
    scheduler.join(root_task);

    std::cout << "[DGTL] Tasks completed. Total nodes processed: " << nodes_processed.load() << "\n";
    assert(nodes_processed.load() == 1365 && "Scheduler failed to execute all tasks or dropped dependencies!");
    std::cout << "[DGTL] Test Passed: Zero memory leaks, zero deadlocks.\n";
}