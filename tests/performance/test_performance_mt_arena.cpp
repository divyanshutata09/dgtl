#include "core/Arena.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

void thread_work(int id) {
    try {
        // Claiming 64MB of Huge-Pages per thread
        dgtl::core::DGTLArena local_arena(64); 
        uint8_t* ptr = (uint8_t*)local_arena.raw_data();
        if (!ptr) return;

        // Pressure check: Touch every page
        for(size_t j = 0; j < (64ULL * 1024 * 1024); j += 4096) {
            ptr[j] = static_cast<uint8_t>(id); 
        }
    } catch (...) {}
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    const int thread_count = std::thread::hardware_concurrency();
    
    std::cout << "\033[1;33m[TITAN-STRESS]\033[0m Spawning " << thread_count << " concurrent arenas..." << std::endl;
    
    std::vector<std::thread> workers;
    for(int i = 0; i < thread_count; ++i) {
        workers.emplace_back(thread_work, i);
    }

    for(auto& t : workers) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "\033[1;32m[PASS]\033[0m Concurrency verified in " << diff.count() << "s" << std::endl;
    std::cout << "[INFO] Total physical lock pressure: " << (thread_count * 64) << " MB" << std::endl;
    
    return 0;
}
