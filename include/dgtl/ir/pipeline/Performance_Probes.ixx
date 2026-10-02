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

module;

#include <cstdlib>

// Add global fragment headers here if needed (<chrono>, <atomic>, etc.)

// FIXED: Bind directly to your core intermediate macro sub-module space
export module dgtl.ir.pipeline.performance_probes;

// FIXED CYCLIC LOOP: Import local core leaf siblings natively via short-form shortcuts
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir;
import dgtl.ir.pipeline.pass_manager;

// Import standalone external primitive modules safely


export namespace dgtl::pipeline::probes {

/**
 * @class SIMDIntMulFoldingPass
 * @brief High-throughput constant folding for integer multiplication using AVX-256.
 * Integrated with the Titan-Stable transactional IR.
 */
class SIMDIntMulFoldingPass final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(dgtl::ir::IRStream& stream) override;

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "SIMDIntMulFoldingPass";
    }

    // Affinities for the Pass Decision Engine
    [[nodiscard]] PassAffinity vector_affinity() const noexcept override { return PassAffinity::High; }
    [[nodiscard]] PassAffinity hot_path_affinity() const noexcept override { return PassAffinity::Medium; }
};

} // namespace dgtl::pipeline::probes

export namespace dgtl::ir::pipeline::probes {
    using namespace dgtl::pipeline::probes;
}
