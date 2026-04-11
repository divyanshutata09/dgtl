#include <iostream>
#include <vector>
#include <atomic>
#include "detail/ir_impl/BitPacked_IR.hpp"
#include "Include/core/Pass_Manager.hpp"

using namespace dgtl;

// A mock pass that marks nodes with a "processed" flag
struct PassA {
    void run(ir::IRStream& s) {
        for(size_t i = 0; i < s.count; ++i) {
            s.buffer[i].flags |= 0x8000; // Set a high bit flag
        }
    }
};

// A mock pass that checks if the flag is set
struct PassB {
    std::atomic<size_t>& fail_count;
    void run(ir::IRStream& s) {
        for(size_t i = 0; i < s.count; ++i) {
            if (!(s.buffer[i].flags & 0x8000)) {
                fail_count.fetch_add(1);
            }
        }
    }
};

void run_barrier_race_test() {
    std::cout << "--- ROBUSTNESS TEST 4: Phase-Barrier Sync Integrity ---\n";
    
    const size_t NODE_COUNT = 524288; // Half a million nodes
    std::vector<ir::IRNode> mem(NODE_COUNT);
    ir::IRStream stream = { mem.data(), NODE_COUNT, NODE_COUNT };

    pipeline::DGTLPassManager pm;
    std::atomic<size_t> failures{0};

    PassA a;
    PassB b{failures};

    std::cout << "[Step 1] Queuing Dependent Passes (A -> Barrier -> B)... ";
    pm.add_pass(a);
    pm.add_pass(b);
    
    // Execute multiple times to catch intermittent timing issues
    for(int i = 0; i < 10; ++i) {
        failures = 0;
        // Reset flags
        for(size_t j = 0; j < NODE_COUNT; ++j) mem[j].flags = 0;
        
        pm.execute(stream);

        if (failures.load() > 0) {
            std::cout << "FAILED!\n";
            std::cout << "Detected " << failures << " synchronization leaks in iteration " << i << ".\n";
            return;
        }
    }

    std::cout << "PASSED.\n";
    std::cout << "RESULT: TEST 4 SUCCESSFUL. Barrier synchronization is atomic and tight.\n";
}

int main() {
    run_barrier_race_test();
    return 0;
}
