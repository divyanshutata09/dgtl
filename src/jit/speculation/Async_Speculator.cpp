#include "dgtl/jit/speculation/Async_Speculator.hpp"

#include <memory>

namespace dgtl::jit {

std::vector<SpeculativeVersion> AsyncSpeculator::generate_versions(ir::IRNode* node) {
    std::vector<SpeculativeVersion> versions;
    if (!node) {
        return versions;
    }

    // Thread-local storage keeps returned raw pointers valid until next call on this thread.
    static thread_local std::vector<std::unique_ptr<ir::IRNode>> arena;
    arena.clear();
    arena.reserve(4);

    auto add_variant = [&](const ir::IRNode& n, double confidence) {
        arena.push_back(std::make_unique<ir::IRNode>(n));
        versions.push_back(SpeculativeVersion{arena.back().get(), confidence});
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
        simd.flags |= ir::Flags::REQUIRES_VEC_PORT;
        simd.refresh_intent_hash();
        add_variant(simd, 0.68);
    }

    // Branch-likely specialization where control behavior appears stable.
    {
        ir::IRNode branch_hint = *node;
        branch_hint.flags |= ir::Flags::BRANCH_LIKELY;
        branch_hint.flags &= ~ir::Flags::BRANCH_UNLIKELY;
        branch_hint.refresh_intent_hash();
        add_variant(branch_hint, 0.61);
    }

    return versions;
}

} // namespace dgtl::jit

