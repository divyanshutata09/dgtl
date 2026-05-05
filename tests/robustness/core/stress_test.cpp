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

#include <chrono>
#include <iostream>
#include <vector>

namespace {

void run_stress_test_impl() {
    constexpr size_t kArenaCount = 100;
    constexpr size_t kArenaMbPerThread = 64;
    constexpr size_t kTouchBytes = 64ull * 1024ull * 1024ull;
    constexpr size_t kPageStep = 4096;

    std::cout << "\033[1;33m[STRESS]\033[0m Starting Massive Allocation Cycle...\n";
    try {
        // Stress: 100 Arenas of 64MB each (~6.4GB total physical pressure)
        std::vector<dgtl::core::DGTL_Arena> pressure_cooker;
        pressure_cooker.reserve(kArenaCount);
        
        for (size_t i = 0; i < kArenaCount; ++i) {
            pressure_cooker.emplace_back(1, kArenaMbPerThread);
            auto* ptr = pressure_cooker.back().raw_data();
            
            if (!ptr) {
                std::cout << "[FAIL] Failed to lock page at iteration " << i << ". Ensure you are Admin.\n";
                return;
            }

            // Touch every page to force physical backing
            for (size_t j = 0; j < kTouchBytes; j += kPageStep) {
                ptr[j] = 0xAA; 
            }
        }
        std::cout << "\033[1;32m[opt]\033[0m Locked 6400MB via Huge-Pages.\n";
    } catch (...) {
        std::cout << "[CRITICAL] Memory Manager Faulted under pressure.\n";
    }
}

} // namespace

void run_stress_test() {
    auto start = std::chrono::high_resolution_clock::now();
    run_stress_test_impl();
    auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> diff = end - start;
    std::cout << "[INFO] Stress test completed in " << diff.count() << "s\n";
    return;
}

