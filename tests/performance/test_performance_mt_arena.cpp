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

#include <iostream>
#include <thread>
#include <vector>
#include "dgtl/core/Arena.hpp"

void thread_work(int id) {
    try {
        dgtl::core::DGTL_Arena local_arena(1, 64);
        // Work logic
    } catch (...) { }
}

void run_test_performance_mt_arena() {
    std::vector<std::thread> threads;
    for(int i=0; i<12; ++i) threads.emplace_back(thread_work, i);
    for(auto& t : threads) t.join();
    return;
}

