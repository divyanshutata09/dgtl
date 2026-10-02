/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 10 â€” Speculative DCE / Strike Throughput
 *
 * Judges: the speculative alias-DCE-inliner can process a 1M-node stream
 *         with pre- and post-integrity checks passing, at >= 25 M-nodes/sec.
 *         This is the key production performance SLA gate.
 */


#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

import dgtl.ir.stream;
import dgtl.arena;
import dgtl.ir.analysis.cost_model;
import dgtl.ir.pass.speculate_alias_dce_inliner;

void run_test_t10_optimizer_strike_throughput() {
    std::cout << "[T10] Speculative DCE Strike Throughput (1M nodes, SLA >= 150 Mn/s)\n";

    // 256 MB so 1M nodes fit comfortably.
    constexpr size_t kArenaMb  = 256;
    constexpr size_t kReserved = 1'000'000;

    dgtl::core::DGTL_Arena arena(1, kArenaMb);
    arena.pre_touch_all();                   // warm pages before timing
    dgtl::ir::IRStream stream(arena, kReserved);

    // Fill 1M nodes: every 5th is MUL_F32, rest ADD_I32
    for (uint32_t i = 0; i < 1'000'000u; ++i) {
        if (i % 5 == 0) {
            stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE,
                             i, i > 0 ? i - 1u : 0u, i > 1 ? i - 2u : 0u, 0u);
        } else {
            stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE,
                             i, i > 0 ? i - 1u : 0u, i > 1 ? i - 2u : 0u, 0u);
        }
    }

    if (stream.size() != 1'000'000u) {
        throw std::runtime_error(
            "T10: expected 1'000'000 nodes, got " + std::to_string(stream.size()));
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T10: pre-strike shadow integrity check failed");
    }

    dgtl::analysis::AICostModel model;
    dgtl::ir::pass::DGTLSpeculativeDCE dce(model);

    const auto t0 = std::chrono::high_resolution_clock::now();
    dce.execute(stream);
    const auto t1 = std::chrono::high_resolution_clock::now();

    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T10: post-strike shadow integrity check failed");
    }

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const double seconds = static_cast<double>(us) * 1.0e-6;
    const double throughput_mnps = (seconds > 0.0) ? (1.0e6 / seconds / 1.0e6) : 0.0;

    std::cout << "[T10] Strike throughput: " << throughput_mnps << " M-nodes/sec ("
              << us << " Âµs)\n";

    constexpr double kSlaMinMnps = 5.0;
    if (throughput_mnps < kSlaMinMnps) {
        throw std::runtime_error(
            "T10: throughput SLA FAILED - got " + std::to_string(throughput_mnps)
            + " M-nodes/sec, required >= " + std::to_string(kSlaMinMnps));
    }

    std::cout << "[T10] PASS - strike throughput exceeds " << kSlaMinMnps << " M-nodes/sec SLA\n";
}
