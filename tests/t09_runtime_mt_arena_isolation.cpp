/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 09 — Multi-threaded Arena Isolation
 *
 * Judges: 12 concurrent threads each create an independent DGTL_Arena and
 *         IRStream, fill it, and verify shadow integrity. No cross-thread
 *         contamination or integrity faults should occur.
 */


#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
import dgtl.ir_core.stream;
import dgtl.arena;

namespace {

void worker(int id, std::atomic<int>& failures) {
    (void)id;
    try {
        constexpr size_t kArenaMb  = 32;
        constexpr size_t kReserved = 512;
        constexpr uint32_t kNodes  = 256;

        dgtl::core::DGTL_Arena arena(1, kArenaMb);
        dgtl::ir::IRStream stream(arena, kReserved);

        for (uint32_t i = 0; i < kNodes; i += 2) {
            stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE,
                             1000u + i, i, i + 1u, 0u);
            stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE,
                             1001u + i, 1000u + i, i, 0u);
        }

        if (stream.size() != kNodes) {
            failures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!stream.validate_shadow_integrity()) {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (...) {
        failures.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

void run_test_t09_runtime_mt_arena_isolation() {
    std::cout << "[T09] Multi-threaded Arena Isolation (12 workers)\n";

    constexpr int kWorkers = 12;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(kWorkers));

    for (int i = 0; i < kWorkers; ++i) {
        threads.emplace_back(worker, i, std::ref(failures));
    }
    for (auto& t : threads) {
        t.join();
    }

    if (failures.load() != 0) {
        throw std::runtime_error(
            "T09: " + std::to_string(failures.load()) +
            " thread(s) reported arena allocation or shadow integrity failures");
    }

    std::cout << "[T09] PASS - " << kWorkers
              << " concurrent arenas: all isolated and integrity-verified\n";
}
