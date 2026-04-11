#include "core/Arena.hpp"
#include <iostream>
#include <vector>
#include <chrono>

void run_stress_test() {
    std::cout << "\033[1;33m[STRESS]\033[0m Starting Massive Allocation Cycle..." << std::endl;
    try {
        // Stress: 100 Arenas of 64MB each (~6.4GB total physical pressure)
        std::vector<dgtl::core::DGTLArena> pressure_cooker;
        
        for(int i = 0; i < 100; ++i) {
            pressure_cooker.emplace_back(64); 
            uint8_t* ptr = (uint8_t*)pressure_cooker.back().raw_data();
            
            if (!ptr) {
                std::cout << "[FAIL] Failed to lock page at iteration " << i << ". Ensure you are Admin." << std::endl;
                return;
            }

            // Touch every page to force physical backing
            size_t cap = 64ULL * 1024 * 1024;
            for(size_t j = 0; j < cap; j += 4096) {
                ptr[j] = 0xAA; 
            }
        }
        std::cout << "\033[1;32m[PASS]\033[0m Locked 6400MB via Huge-Pages." << std::endl;
    } catch (...) {
        std::cout << "[CRITICAL] Memory Manager Faulted under pressure." << std::endl;
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    run_stress_test();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "[INFO] Stress test completed in " << diff.count() << "s" << std::endl;
    return 0;
}
