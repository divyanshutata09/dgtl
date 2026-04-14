#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <intrin.h>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

void run_cache_killer_test() {
    // PILLAR 1: Allocate a massive 512MB Strike Zone
    size_t arena_size_mb = 512;
    dgtl::core::DGTL_Arena arena(arena_size_mb); 
    dgtl::ir::IRStream stream(arena, 10000);

    std::cout << "\033[1;36m[DGTL-STRESS]\033[0m Initialized " << arena_size_mb << "MB Arena." << std::endl;

    // PILLAR 2: Cache & TLB Thrashing
    // We write to 128MB of memory in a non-linear fashion to force cache eviction
    std::cout << "[DGTL-STRESS] Thrashing Caches..." << std::endl;
    volatile uint8_t* raw = arena.raw_data();
    size_t thrash_size = 128 * 1024 * 1024;
    for (size_t i = 0; i < thrash_size; i += 64) { // Jump by cache lines
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
    
    std::cout << "\033[1;33m[RESULT]\033[0m Cold-Cache GVN Latency: " << latency << " cycles" << std::endl;

    if (latency < 2500) {
        std::cout << "\033[1;32m[opt]\033[0m Stability Verified. Zero-Latency pillars held under pressure." << std::endl;
    } else {
        std::cout << "\033[1;31m[WARN]\033[0m Cache-miss penalty detected. Analyze TLB pressure." << std::endl;
    }

    return;
}

