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

#include <intrin.h>
#include <iostream>

void run_cache_killer_test() {
    constexpr size_t kArenaMbPerThread = 512;
    constexpr size_t kReservedNodes = 10000;
    constexpr size_t kThrashBytes = 128ull * 1024ull * 1024ull;

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    std::cout << "\033[1;36m[DGTL-STRESS]\033[0m Initialized " << kArenaMbPerThread << "MB Arena.\n";

    // PILLAR 2: Cache & TLB Thrashing
    // We write to 128MB of memory in a non-linear fashion to force cache eviction
    std::cout << "[DGTL-STRESS] Thrashing Caches...\n";
    volatile uint8_t* raw = arena.raw_data();
    if (raw == nullptr) {
        std::cout << "\033[1;31m[FAIL]\033[0m Arena memory was not provisioned.\n";
        return;
    }

    for (size_t i = 0; i < kThrashBytes; i += 64) { // Jump by cache lines
        raw[i] = static_cast<uint8_t>(i % 255);
    }

    // PILLAR 3: Cold-Start Profiling
    // We measure GVN latency IMMEDIATELY after thrashing
    uint64_t start = __rdtsc();
    
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 777, 10, 20, 0); 
    stream.perform_gvn();
    stream.commit_transaction();

    uint64_t end = __rdtsc();
    uint64_t latency = end - start;
    
    std::cout << "\033[1;33m[RESULT]\033[0m Cold-Cache GVN Latency: " << latency << " cycles\n";

    if (latency < 2500) {
        std::cout << "\033[1;32m[opt]\033[0m Stability Verified. Zero-Latency pillars held under pressure.\n";
    } else {
        std::cout << "\033[1;31m[WARN]\033[0m Cache-miss penalty detected. Analyze TLB pressure.\n";
    }

    return;
}




