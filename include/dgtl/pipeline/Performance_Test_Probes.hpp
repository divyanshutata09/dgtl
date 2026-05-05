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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 26: INTEGRATED FORMAL VERIFICATION LAYER (SENTINEL-ENABLED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL VALIDATOR)
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once

#include <immintrin.h>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pipeline::probes {

/**
 * @class SIMDIntMulFoldingPass
 * @brief High-throughput constant folding for integer multiplication using AVX-256.
 * Integrated with the Titan-Stable transactional IR.
 */
class SIMDIntMulFoldingPass final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    __attribute__((target("avx2")))
    void execute(dgtl::ir::IRStream& stream) override {
        const size_t stream_size = stream.size();
        if (stream_size == 0) return;

        // PILLAR 2: Open Shadow Buffer for transactional mutation
        stream.begin_transaction();
        
        // Synchronization: Ensure Shadow is mirrored before vectorized folding
        stream.push_full_to_shadow();

        size_t i = 0;
        // Process in 8-lane chunks for AVX2 compatibility
        for (; i + 8 <= stream_size; i += 8) {
            bool all_mul_i32 = true;
            for (size_t lane = 0; lane < 8; ++lane) {
                const auto node = stream.get_node_readonly(i + lane);
                if (node.opcode != dgtl::ir::OpCode::MUL_I32 ||
                    node.op_a == dgtl::ir::INVALID_SLOT ||
                    node.op_b == dgtl::ir::INVALID_SLOT) {
                    all_mul_i32 = false;
                    break;
                }
            }

            if (!all_mul_i32) {
                continue;
            }

            // Load from Primary (Read-Only)
            __m256i a_vals = _mm256_set_epi32(
                stream.get_node_readonly(i + 7).op_a,
                stream.get_node_readonly(i + 6).op_a,
                stream.get_node_readonly(i + 5).op_a,
                stream.get_node_readonly(i + 4).op_a,
                stream.get_node_readonly(i + 3).op_a,
                stream.get_node_readonly(i + 2).op_a,
                stream.get_node_readonly(i + 1).op_a,
                stream.get_node_readonly(i).op_a);

            __m256i b_vals = _mm256_set_epi32(
                stream.get_node_readonly(i + 7).op_b,
                stream.get_node_readonly(i + 6).op_b,
                stream.get_node_readonly(i + 5).op_b,
                stream.get_node_readonly(i + 4).op_b,
                stream.get_node_readonly(i + 3).op_b,
                stream.get_node_readonly(i + 2).op_b,
                stream.get_node_readonly(i + 1).op_b,
                stream.get_node_readonly(i).op_b);

            const __m256i res = _mm256_mullo_epi32(a_vals, b_vals);

            // Rewrite folded multiplies to LOAD_IMM constants.
            for (size_t j = 0; j < 8; ++j) {
                auto& atom = stream.get_shadow_atom(i + j);
                auto& core = stream.get_shadow_core(i + j);

                atom.opcode = dgtl::ir::OpCode::LOAD_IMM;
                atom.flags = static_cast<uint16_t>(dgtl::ir::Flags::IS_CONST |
                                                   dgtl::ir::Flags::IS_SPEC_CONST |
                                                   dgtl::ir::Flags::IS_OPTIMIZED);

                core.op_a = static_cast<uint32_t>(_mm256_extract_epi32(res, static_cast<int>(j)));
                core.op_b = 0;
                core.op_c = dgtl::ir::INVALID_SLOT;
                core.intent_hash = dgtl::ir::IRNode::calculate_hash(atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c);
            }
        }

        // Scalar Fallback for tail nodes
        for (; i < stream_size; ++i) {
            const auto p_node = stream.get_node_readonly(i);
            if (p_node.opcode != dgtl::ir::OpCode::MUL_I32 ||
                p_node.op_a == dgtl::ir::INVALID_SLOT ||
                p_node.op_b == dgtl::ir::INVALID_SLOT) {
                continue;
            }

            auto& s_core = stream.get_shadow_core(i);
            s_core.op_a = p_node.op_a * p_node.op_b;
            s_core.op_b = 0;
            s_core.op_c = dgtl::ir::INVALID_SLOT;
            
            auto& atom = stream.get_shadow_atom(i);
            atom.opcode = dgtl::ir::OpCode::LOAD_IMM;
            atom.flags = static_cast<uint16_t>(dgtl::ir::Flags::IS_CONST |
                                               dgtl::ir::Flags::IS_SPEC_CONST |
                                               dgtl::ir::Flags::IS_OPTIMIZED);
            s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c);
        }

        // PILLAR 3: Sentinel Validation before committing changes
        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "SIMDIntMulFoldingPass";
    }

    // Affinities for the Pass Decision Engine
    [[nodiscard]] PassAffinity vector_affinity() const noexcept override { return PassAffinity::High; }
    [[nodiscard]] PassAffinity hot_path_affinity() const noexcept override { return PassAffinity::Medium; }
};

} // namespace dgtl::pipeline::probes
