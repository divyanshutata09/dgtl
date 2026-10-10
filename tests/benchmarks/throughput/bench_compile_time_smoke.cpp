/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

import dgtl.arena;
import dgtl.ir;
import dgtl.ir.pass.transform_scalar_core;

#include <chrono>
#include <iostream>
#include <stdexcept>

void run_test_t02_performance_smoke() {
    std::cout << "[T02] Performance smoke\n";

    constexpr size_t kNodes = 2000;

    dgtl::core::DGTL_Arena arena(1, 4);
    dgtl::ir::IRStream stream(arena, kNodes + 16);

    for (size_t i = 0; i < kNodes; ++i) {
        const uint32_t out = static_cast<uint32_t>(1000 + i);
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, out, 10, 11, 0);
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    dgtl::ir::pass::transform::GlobalValueNumbering().execute(stream);
    const auto t1 = std::chrono::high_resolution_clock::now();

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (us <= 0) {
        throw std::runtime_error("T02: invalid timing result");
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T02: integrity failure after GVN");
    }

    const double mnps = static_cast<double>(kNodes) / static_cast<double>(us);
    std::cout << "[T02] GVN throughput: " << mnps << " M-nodes/sec\n";
    std::cout << "[T02] PASS - performance smoke OK\n";
}
