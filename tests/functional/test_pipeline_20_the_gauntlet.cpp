#include <iostream>
#include "dgtl/ir/Global_IR.hpp"

int main() {
    // Add this to force immediate printing to terminal
    std::ios_base::sync_with_stdio(false);
    std::cout << "--- [DGTL-TEST] 20_the_gauntlet ---" << std::endl;

    try {
        // YOUR TEST LOGIC HERE
        // e.g., run_gauntlet_checks();
        
        std::cout << "GAUNTLET STATUS: SUCCESS" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "GAUNTLET CRASHED: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}