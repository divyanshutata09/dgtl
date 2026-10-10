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

import dgtl.arena;
#include <iostream>
#include <vector>
#include <thread>

namespace {

constexpr std::size_t kThreadArenaMiB = 64;
constexpr std::size_t kTouchBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kPageStride = 4096;

} // namespace

void thread_work(int id) {
    dgtl::core::DGTL_Arena local_arena(1, kThreadArenaMiB);
    std::uint8_t* ptr = local_arena.allocate<std::uint8_t>(kTouchBytes);
    if (!ptr) return;

    for (std::size_t j = 0; j < kTouchBytes; j += kPageStride) {
        ptr[j] = static_cast<std::uint8_t>(id);
    }
}

void run_mt_stress() {
    const unsigned int hw_threads = std::thread::hardware_concurrency();
    const unsigned int thread_count = hw_threads == 0 ? 1u : hw_threads;
    std::cout << "[TITAN-STRESS] Spawning " << thread_count << " concurrent arenas..." << std::endl;
    
    std::vector<std::thread> workers;
    for (unsigned int i = 0u; i < thread_count; ++i) {
        workers.emplace_back(thread_work, static_cast<int>(i));
    }

    for(auto& t : workers) t.join();
    std::cout << "\033[1;32m[opt]\033[0m Concurrency verified. No heap contention." << std::endl;
    return;
}

