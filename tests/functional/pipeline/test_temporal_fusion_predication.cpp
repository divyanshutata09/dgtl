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

void run_test_temporal_fusion_predication() {
    constexpr size_t kArenaMbPerThread = 64;
    constexpr size_t kReservedNodes = 32;

    std::cout << "--- TEMPORAL FUSION + MASKED PREDICATION ---\n";

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);

    {
        dgtl::ir::IRStream stream(arena, kReservedNodes);
        dgtl::pipeline::probes::build_temporal_fusion_probe(stream);
        dgtl::pipeline::probes::apply_temporal_fusion(stream);
        const bool found_temporal = dgtl::pipeline::probes::stream_has_opcode(stream, dgtl::ir::OpCode::FMA_WIDEN_F16_F32);
        if (!found_temporal) {
            throw std::runtime_error("temporal fusion did not produce FMA_WIDEN_F16_F32");
        }
    }

    {
        dgtl::ir::IRStream stream(arena, kReservedNodes);
        dgtl::pipeline::probes::build_masked_predication_probe(stream);
        dgtl::pipeline::probes::apply_masked_predication(stream);
        const bool found_predicated = dgtl::pipeline::probes::stream_has_opcode(stream, dgtl::ir::OpCode::SIMD_BSL_PREDICATE);
        if (!found_predicated) {
            throw std::runtime_error("masked predication lowering did not produce SIMD_BSL_PREDICATE");
        }
    }

    std::cout << "PASS: temporal fusion and masked predication lowering validated.\n";
}
