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

#include <iostream>
#include <stdexcept>

#include "dgtl/core/Arena.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"

using namespace dgtl;

void run_test_temporal_fusion_predication() {
    std::cout << "--- TEMPORAL FUSION + MASKED PREDICATION ---\n";

    core::DGTL_Arena arena(1, 64);

    {
        ir::IRStream stream(arena, 32);
        pipeline::probes::build_temporal_fusion_probe(stream);
        pipeline::probes::apply_temporal_fusion(stream);
        const bool found_temporal = pipeline::probes::stream_has_opcode(stream, ir::OpCode::FMA_WIDEN_F16_F32);
        if (!found_temporal) {
            throw std::runtime_error("temporal fusion did not produce FMA_WIDEN_F16_F32");
        }
    }

    {
        ir::IRStream stream(arena, 32);
        pipeline::probes::build_masked_predication_probe(stream);
        pipeline::probes::apply_masked_predication(stream);
        const bool found_predicated = pipeline::probes::stream_has_opcode(stream, ir::OpCode::SIMD_BSL_PREDICATE);
        if (!found_predicated) {
            throw std::runtime_error("masked predication lowering did not produce SIMD_BSL_PREDICATE");
        }
    }

    std::cout << "PASS: temporal fusion and masked predication lowering validated.\n";
}
