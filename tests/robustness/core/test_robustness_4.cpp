/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <atomic>
#include <iostream>
#include <stdexcept>

#include "dgtl/dgtl_all.hpp"

// A mock opt that marks nodes with a "processed" flag
struct optA : public dgtl::pipeline::DGTLOptimizationPass {
    void execute(dgtl::ir::IRStream& s) override {
        for (size_t i = 0; i < s.size(); ++i) {
            auto node = s.get_node(i);
            node.flags |= 0x8000; // Set a high bit flag
            s.set_node(i, node);
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "optA Barrier Marker v1.0.0";
    }
};

// A mock opt that checks if the flag is set
struct optB : public dgtl::pipeline::DGTLOptimizationPass {
    std::atomic<size_t>& fail_count;
    explicit optB(std::atomic<size_t>& failures) : fail_count(failures) {}

    void execute(dgtl::ir::IRStream& s) override {
        for (size_t i = 0; i < s.size(); ++i) {
            auto node = s.get_node(i);
            if (!(node.flags & 0x8000)) {
                fail_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "optB Barrier Verifier v1.0.0";
    }
};

void run_barrier_race_test() {
    constexpr size_t kNodeCount = 131072;
    constexpr size_t kArenaMbPerThread = 128;
    constexpr int kIterations = 4;

    std::cout << "--- ROBUSTNESS TEST 4: Phase-Barrier Sync Integrity ---\n";
    
    // Keep contention pressure high but bounded for regular developer runs.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kNodeCount);

    // Fill the stream with some nodes
    for (size_t i = 0; i < kNodeCount; ++i) {
        stream.push_node(dgtl::ir::OpCode::NOP, 0, static_cast<uint32_t>(i), 0, 0, 0);
    }

    dgtl::pipeline::DGTLoptManager pm;
    std::atomic<size_t> failures{0};

    pm.add_opt(std::make_unique<optA>());
    pm.add_opt(std::make_unique<optB>(failures));

    std::cout << "[Step 1] Queuing Dependent optes (A -> Barrier -> B)... ";
    
    // Execute multiple rounds to catch intermittent timing issues.
    for (int i = 0; i < kIterations; ++i) {
        failures = 0;
        
        pm.execute(stream);

        if (failures.load() > 0) {
            std::cout << "FAILED!\n";
            std::cout << "Detected " << failures << " synchronization leaks in iteration " << i << ".\n";
            throw std::runtime_error("robustness.core4 failed: barrier synchronization leaks detected");
        }
    }

    std::cout << "optED.\n";
    std::cout << "RESULT: TEST 4 SUCCESSFUL. Barrier synchronization is atomic and tight.\n";
}

void run_test_robustness_4() {
    try {
        run_barrier_race_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL EXCEPTION during Barrier Race Test: " << e.what() << "\n";
        throw;
    }
    return;
}





