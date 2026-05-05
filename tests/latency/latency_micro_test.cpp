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

void run_latency_micro_test() {
    constexpr size_t kArenaMbPerThread = 10;
    constexpr size_t kReservedNodes = 1000;
    constexpr size_t kWarmupIters = 5;
    constexpr uint32_t kTxnResult = 100;

    // 10MB Arena Strike Zone
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    // Warm up the cache/TLB to ensure stable cycle counts
    for (size_t i = 0; i < kWarmupIters; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, i, 10, 20, 0);
        stream.perform_gvn();
    }

    // Capture start cycles
    uint64_t start = __rdtsc();
    
    // Target Operation: Transactional GVN
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, kTxnResult, 10, 20, 0);
    stream.perform_gvn();
    stream.commit_transaction();

    // Capture end cycles
    uint64_t end = __rdtsc();
    
    std::cout << "\n\033[1;33m[DGTL-PROFILER]\033[0m GVN Transaction Latency: " << (end - start) << " cycles\n";
    return;
}




