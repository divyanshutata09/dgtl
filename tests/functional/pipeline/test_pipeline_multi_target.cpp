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

#include "dgtl/dgtl_all.hpp"

#include <iostream>
#include <vector>

void setup_test_stream(dgtl::ir::IRStream& stream, uint32_t lane_width) {
    stream.begin_transaction();
    // Create a full target-width independent add window so SIMD legalizer can fuse.
    for (uint32_t i = 0u; i < lane_width; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32,
                         dgtl::ir::Flags::NONE,
                         100u + i,
                         i,
                         i + 1u,
                         0u);
    }
    stream.commit_transaction();
}

void run_test_pipeline_multi_target() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 16;

    struct Case {
        dgtl::pass::TargetArch arch;
        uint32_t expected;
        uint32_t lane_width;
        const char* name;
    };
    const std::vector<Case> cases = {
        { dgtl::pass::TargetArch::X86_AVX512,  0x204, 16u, "X86_AVX512" },
        { dgtl::pass::TargetArch::ARM_NEON,    0x304,  4u, "ARM_NEON" },
        { dgtl::pass::TargetArch::WASM_SIMD128, 0x404, 4u, "WASM_SIMD128" }
    };

    bool all_opt = true;

    for (const auto& c : cases) {
        // Keep this test deterministic: 1 thread, explicit per-thread MB size.
        dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
        dgtl::ir::IRStream stream(arena, kReservedNodes);
        setup_test_stream(stream, c.lane_width);

        dgtl::pass::DGTLSIMDLegalizer legalizer(c.arch);
        legalizer.legalize(stream);

        const uint32_t result_op = static_cast<uint32_t>(stream.get_node(0).opcode);
        if (result_op == c.expected) {
            std::cout << "[opt] " << c.name << " fused to 0x"
                      << std::hex << c.expected << std::dec << "\n";
        } else {
            std::cout << "[FAIL] " << c.name << " expected 0x" << std::hex << c.expected 
                      << " but got 0x" << result_op << std::dec << "\n";
            all_opt = false;
        }
    }

    if (all_opt) {
        std::cout << "GLOBAL_SUCCESS\n";
    }
    return;
}








