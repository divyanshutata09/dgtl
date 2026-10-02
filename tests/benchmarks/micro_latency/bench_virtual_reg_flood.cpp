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

import dgtl;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir_enums;
import dgtl.ir_node;

#include <iostream>

void run_test_pipeline_19_register_exhaustion() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kNodeLimit = 10;
    constexpr size_t kPushAttempts = 20;

    std::cout << "--- [DGTL-TEST] 19_register_exhaustion ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kNodeLimit);

    // Push more nodes than capacity and ensure the stream clamps safely.
    for (size_t i = 0; i < kPushAttempts; ++i) {
        stream.push_node(dgtl::ir::OpCode::NOP,
                         dgtl::ir::Flags::NONE,
                         static_cast<uint32_t>(i),
                         0,
                         0,
                         0,
                         0);
    }

    if (stream.size() <= kNodeLimit) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAIL: Stream size (" << stream.size()
                  << ") exceeded capacity (" << kNodeLimit << ")\n";
        return;
    }
    return;
}




