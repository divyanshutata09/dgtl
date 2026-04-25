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

/* * DGTL Backend vNext - Execution Graph Layer
 * Unified Execution Graph for Region-Based Optimization
 * Above IR level for advanced scheduling and fusion
 */

#pragma once
#include <limits>
#include <vector>
#include <memory>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Target.hpp"
#include "Region.hpp"

namespace dgtl::graph {

/**
 * @class ExecutionGraph
 * @brief High-level execution graph that groups IR nodes into regions
 * for advanced optimization beyond LLVM's instruction-level approach.
 *
 * Key innovation: Region-based execution model vs LLVM's instruction-based.
 */
class ExecutionGraph {
public:
    static constexpr std::size_t kPreferredNodeAlignment = 32;

    std::vector<std::unique_ptr<Region>> regions;

    /**
     * @brief Build execution graph from global IR by analyzing dependencies
     * and grouping nodes into optimizable regions.
     */
    void build_from_ir(dgtl::ir::IRStream& global_ir);

    /**
     * @brief Schedule regions for optimal execution on target hardware.
     */
    void schedule(const dgtl::core::TargetMachine& target);

    /**
     * @brief Get regions that can be fused together.
     */
    [[nodiscard]] std::vector<std::vector<Region*>> find_fusion_candidates() const;

    /**
     * @brief Validate graph integrity after transformations.
     */
    [[nodiscard]] bool validate() const;

    [[nodiscard]] static constexpr std::size_t preferred_node_alignment() noexcept {
        return kPreferredNodeAlignment;
    }

    /**
     * @brief Return a lightweight schedule score for diagnostics and testing.
     */
    [[nodiscard]] double region_schedule_score(const Region& region, const dgtl::core::TargetMachine& target) const {
        if (!region.validate()) {
            return std::numeric_limits<double>::infinity();
        }

        double score = region.estimated_cycles;
        score += region.memory_density() * static_cast<double>(target.memory_latency_bias()) * 20.0;
        if (target.prefers_vector_heavy_schedule()) {
            score -= region.simd_density() * 10.0;
        }
        score += region.live_range_pressure() * 5.0;
        return score;
    }
};

} // namespace dgtl::graph


