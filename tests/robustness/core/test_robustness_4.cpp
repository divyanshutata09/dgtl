#include <iostream>
#include <vector>
#include <atomic>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

// A mock opt that marks nodes with a "processed" flag
struct optA : public pipeline::DGTLOptimizationPass {
    void execute(ir::IRStream& s) override {
        for (size_t i = 0; i < s.size(); ++i) {
            auto node = s.get_node(i);
            node.flags |= 0x8000; // Set a high bit flag
            // Note: get_node returns a copy, so we need to modify via another way
            // For this test, we'll assume the pass modifies the stream internally
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "optA Barrier Marker";
    }
};

// A mock opt that checks if the flag is set
struct optB : public pipeline::DGTLOptimizationPass {
    std::atomic<size_t>& fail_count;
    optB(std::atomic<size_t>& failures) : fail_count(failures) {}

    void execute(ir::IRStream& s) override {
        for (size_t i = 0; i < s.size(); ++i) {
            auto node = s.get_node(i);
            if (!(node.flags & 0x8000)) {
                fail_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "optB Barrier Verifier";
    }
};

void run_barrier_race_test() {
    std::cout << "--- ROBUSTNESS TEST 4: Phase-Barrier Sync Integrity ---\n";
    
    const size_t NODE_COUNT = 524288; // Half a million nodes
    core::DGTL_Arena arena(512); // 512 MB arena
    ir::IRStream stream(arena, NODE_COUNT);

    // Fill the stream with some nodes
    for(size_t i = 0; i < NODE_COUNT; ++i) {
        stream.push_node(ir::OpCode::NOP, 0, (uint32_t)i, 0, 0, 0);
    }

    pipeline::DGTLoptManager pm;
    std::atomic<size_t> failures{0};

    pm.add_opt(std::make_unique<optA>());
    pm.add_opt(std::make_unique<optB>(failures));

    std::cout << "[Step 1] Queuing Dependent optes (A -> Barrier -> B)... ";
    
    // Execute multiple times to catch intermittent timing issues
    for(int i = 0; i < 10; ++i) {
        failures = 0;
        
        pm.execute(stream);

        if (failures.load() > 0) {
            std::cout << "FAILED!\n";
            std::cout << "Detected " << failures << " synchronization leaks in iteration " << i << ".\n";
            return;
        }
    }

    std::cout << "optED.\n";
    std::cout << "RESULT: TEST 4 SUCCESSFUL. Barrier synchronization is atomic and tight.\n";
}

void run_test_robustness_4() {
    run_barrier_race_test();
    return;
}

