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

#include "dgtl/dgtl_all.hpp"

#include <thread>
#include <vector>

void thread_work(int id) {
    (void)id;

    try {
        constexpr size_t kArenaMbPerThread = 64;
        dgtl::core::DGTL_Arena local_arena(1, kArenaMbPerThread);
        // Work logic
    } catch (...) {}
}

void run_test_performance_mt_arena() {
    constexpr int kWorkerCount = 12;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(kWorkerCount));

    for (int i = 0; i < kWorkerCount; ++i) {
        threads.emplace_back(thread_work, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    return;
}

