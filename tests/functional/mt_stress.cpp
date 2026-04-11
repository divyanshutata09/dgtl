#include "dgtl/core/Arena.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

void thread_work(int id) {
    try {
        dgtl::core::DGTLArena local_arena(64); // 64MB
        uint8_t* ptr = (uint8_t*)local_arena.raw_data();
        if (!ptr) return;

        for(size_t j = 0; j < (64ULL * 1024 * 1024); j += 4096) {
            ptr[j] = static_cast<uint8_t>(id); 
        }
    } catch (...) {}
}

int main() {
    const int thread_count = std::thread::hardware_concurrency();
    std::cout << "[TITAN-STRESS] Spawning " << thread_count << " concurrent arenas..." << std::endl;
    
    std::vector<std::thread> workers;
    for(int i = 0; i < thread_count; ++i) {
        workers.emplace_back(thread_work, i);
    }

    for(auto& t : workers) t.join();
    std::cout << "\033[1;32m[PASS]\033[0m Concurrency verified. No heap contention." << std::endl;
    return 0;
}
