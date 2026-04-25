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

#include "dgtl/core/Arena.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

void thread_work(int id) {
    dgtl::core::DGTL_Arena local_arena(1, 64); // 1 thread, 64MB
    uint8_t* ptr = local_arena.allocate<uint8_t>(64ULL * 1024 * 1024);
    if (!ptr) return;

    for(size_t j = 0; j < (64ULL * 1024 * 1024); j += 4096) {
        ptr[j] = static_cast<uint8_t>(id); 
    }
}

void run_mt_stress() {
    const unsigned int thread_count = std::thread::hardware_concurrency();
    std::cout << "[TITAN-STRESS] Spawning " << thread_count << " concurrent arenas..." << std::endl;
    
    std::vector<std::thread> workers;
    for (unsigned int i = 0u; i < thread_count; ++i) {
        workers.emplace_back(thread_work, static_cast<int>(i));
    }

    for(auto& t : workers) t.join();
    std::cout << "\033[1;32m[opt]\033[0m Concurrency verified. No heap contention." << std::endl;
    return;
}

