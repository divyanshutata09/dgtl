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
#include <stdexcept>

void run_test_release_ir_modes() {
    constexpr size_t kArenaMbPerThread = 64;
    constexpr size_t kReservedNodes = 64;

    std::cout << "--- RELEASE MODE REGRESSION: int(default) vs float(FMA) ---\n";

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);

    // Integer-first release mode should remain scalar integer math.
    dgtl::ir::IRStream int_stream(arena, kReservedNodes);
    dgtl::pipeline::probes::build_release_int_probe(int_stream);
    dgtl::pipeline::probes::run_common_optimizations(int_stream);
    const bool saw_mul_i32 = dgtl::pipeline::probes::stream_has_opcode(int_stream, dgtl::ir::OpCode::MUL_I32);
    if (!saw_mul_i32) {
        throw std::runtime_error("integer release mode no longer emits MUL_I32 chain");
    }

    // Float mode should preserve fusion availability (MUL+ADD -> FMA).
    dgtl::ir::IRStream float_stream(arena, kReservedNodes);
    dgtl::pipeline::probes::build_release_float_probe(float_stream);
    dgtl::pipeline::probes::run_common_optimizations(float_stream);
    dgtl::pipeline::probes::apply_float_fusion_probe(float_stream);
    const bool saw_fma = dgtl::pipeline::probes::stream_has_opcode(float_stream, dgtl::ir::OpCode::FMA_F32);
    if (!saw_fma) {
        throw std::runtime_error("float release mode lost FMA fusion");
    }

    std::cout << "PASS: integer default and float/FMA mode behavior validated.\n";
}
