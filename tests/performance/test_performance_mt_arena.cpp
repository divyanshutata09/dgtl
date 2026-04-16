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

