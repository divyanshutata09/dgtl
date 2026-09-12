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
module;

#include <memory>
#include <vector>

module dgtl.ir_core.async_dopt_tiered;

namespace dgtl::jit {

std::vector<SpeculativeVersion> AsyncSpeculator::generate_versions(ir::IRNode* node) {
    std::vector<SpeculativeVersion> versions;
    if (!node) {
        return versions;
    }

    // Zero-allocation thread-local arena for speculative variant lifetime
    static thread_local std::vector<ir::IRNode> arena;
    arena.clear();
    arena.reserve(4);

    auto add_variant = [&](const ir::IRNode& n, double confidence) {
        arena.push_back(n);
        versions.push_back(SpeculativeVersion{&arena.back(), confidence});
    };

    // Baseline variant (identity fallback).
    {
        ir::IRNode baseline = *node;
        baseline.refresh_intent_hash();
        add_variant(baseline, 0.55);
    }

    // Constant-specialized variant when safe.
    {
        ir::IRNode folded = *node;
        folded.flags |= ir::Flags::IS_SPEC_CONST;
        folded.fold_if_possible();
        folded.refresh_intent_hash();
        add_variant(folded, 0.72);
    }

    // Vector-biased speculative variant for throughput paths.
    {
        ir::IRNode simd = *node;
        simd.flags |= ir::Flags::IS_SIMD_BURST;
        simd.logic_flags |= ir::HintFlags::RequiresVecPort;
        simd.refresh_intent_hash();
        add_variant(simd, 0.68);
    }

    // Branch-likely specialization where control behavior appears stable.
    {
        ir::IRNode branch_hint = *node;
        branch_hint.logic_flags |= ir::HintFlags::BranchLikely;
        branch_hint.logic_flags &= ~ir::HintFlags::BranchUnlikely;
        branch_hint.refresh_intent_hash();
        add_variant(branch_hint, 0.61);
    }

    return versions;
}

} // namespace dgtl::jit