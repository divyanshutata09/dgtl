#include <iostream>
#include <intrin.h>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

void run_latency_micro_test() {
    // 10MB Arena Strike Zone
    dgtl::core::DGTL_Arena arena(10); 
    dgtl::ir::IRStream stream(arena, 1000);

    // Warm up the cache/TLB to ensure stable cycle counts
    for(int i=0; i<5; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, i, 10, 20, 0);
        stream.perform_gvn();
    }

    // Capture start cycles
    uint64_t start = __rdtsc();
    
    // Target Operation: Transactional GVN
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100, 10, 20, 0); 
    stream.perform_gvn();
    stream.commit_transaction();

    // Capture end cycles
    uint64_t end = __rdtsc();
    
    std::cout << "\n\033[1;33m[DGTL-PROFILER]\033[0m GVN Transaction Latency: " << (end - start) << " cycles" << std::endl;
    return;
}

