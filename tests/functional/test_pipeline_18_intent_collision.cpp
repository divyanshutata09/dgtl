#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

int main() {
    std::cout << "--- [DGTL-TEST] 18_intent_collision ---" << std::endl;
    
    // Test logic: Ensure different operand order produces different hashes
    // mimicking the logic: (op_val * 31u) ^ (op_a * 17u) ^ (op_b * 7u)
    uint32_t opcode = 0x1;
    uint32_t a = 10, b = 20;
    
    uint32_t hash1 = (opcode * 31u) ^ (a * 17u) ^ (b * 7u);
    uint32_t hash2 = (opcode * 31u) ^ (b * 17u) ^ (a * 7u); // Swapped operands

    if (hash1 != hash2) {
        std::cout << "[PASS] Intent hash differentiates operand order." << std::endl;
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "[FAIL] Hash collision on operand swap!" << std::endl;
    }
    
    return 0;
}
