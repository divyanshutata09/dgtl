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

/* * DGTL Backend vNext - Fusion Clusters
 * Groups of regions that can be fused for optimization
 */

#pragma once
#include <cstddef>
#include <vector>
#include <algorithm>
#include "Region.hpp"

namespace dgtl::graph {

/**
 * @enum FusionType
 * @brief Types of fusion optimizations.
 */
enum class FusionType {
    VECTOR_FUSION,      // SIMD vectorization across regions
    LOOP_FUSION,        // Combine adjacent loops
    KERNEL_FUSION,      // Fuse compute kernels
    MEMORY_FUSION,      // Combine memory operations
    BRANCH_FUSION       // Fuse conditional branches
};

/**
 * @class FusionCluster
 * @brief A cluster of regions that can be fused together.
 */
class FusionCluster {
public:
    std::vector<Region*> regions;
    FusionType fusion_type;
    double fusion_benefit = 0.0;  // Estimated performance gain

    /**
     * @brief Check if adding a region to this cluster is beneficial.
     */
    [[nodiscard]] bool can_add_region(Region* region) const {
        if (!region || !region->validate()) return false;
        if (std::find(regions.begin(), regions.end(), region) != regions.end()) return false;
        if (regions.empty()) return true;

        size_t current_nodes = 0;
        for (const Region* r : regions) {
            current_nodes += r ? r->node_count() : 0;
        }

        constexpr size_t kMaxClusterNodes = 256;
        if (current_nodes + region->node_count() > kMaxClusterNodes) {
            return false;
        }

        if (fusion_type == FusionType::VECTOR_FUSION) {
            return region->vectorizable || region->simd_density() > 0.2;
        }
        if (fusion_type == FusionType::MEMORY_FUSION) {
            return region->memory_bound || region->memory_density() > 0.35;
        }
        return true;
    }

    /**
     * @brief Add a region to the fusion cluster.
     */
    void add_region(Region* region) {
        if (!can_add_region(region)) return;
        if (std::find(regions.begin(), regions.end(), region) != regions.end()) return;
        regions.push_back(region);
        fusion_benefit = calculate_benefit();
    }

    /**
     * @brief Apply the fusion transformation.
     */
    void apply_fusion() {
        // Keep transformation lightweight at header layer: normalize region flags.
        for (Region* r : regions) {
            if (!r) continue;
            r->speculative_candidate = r->speculative_candidate || (fusion_type == FusionType::KERNEL_FUSION);
            r->vectorizable = r->vectorizable || (fusion_type == FusionType::VECTOR_FUSION);
            r->memory_bound = r->memory_bound || (fusion_type == FusionType::MEMORY_FUSION);
        }
        fusion_benefit = calculate_benefit();
    }

    /**
     * @brief Calculate the benefit of this fusion.
     */
    [[nodiscard]] double calculate_benefit() const {
        if (regions.size() < 2) return 0.0;

        double simd = 0.0;
        double mem = 0.0;
        double ctrl = 0.0;
        for (const Region* r : regions) {
            if (!r) continue;
            simd += r->simd_density();
            mem += r->memory_density();
            ctrl += r->control_density();
        }

        const double n = static_cast<double>(regions.size());
        const double simd_avg = simd / n;
        const double mem_avg = mem / n;
        const double ctrl_avg = ctrl / n;

        switch (fusion_type) {
            case FusionType::VECTOR_FUSION:
                return std::max(0.0, simd_avg * 0.6 + (1.0 - ctrl_avg) * 0.2 + 0.2);
            case FusionType::MEMORY_FUSION:
                return std::max(0.0, mem_avg * 0.55 + (1.0 - ctrl_avg) * 0.2 + 0.15);
            case FusionType::KERNEL_FUSION:
                return std::max(0.0, simd_avg * 0.25 + mem_avg * 0.25 + (1.0 - ctrl_avg) * 0.3 + 0.1);
            case FusionType::LOOP_FUSION:
                return std::max(0.0, (1.0 - ctrl_avg) * 0.35 + 0.15);
            case FusionType::BRANCH_FUSION:
                return std::max(0.0, (1.0 - ctrl_avg) * 0.25 + 0.05);
            default:
                return 0.0;
        }
    }

    /**
     * @brief Validate that fusion is safe and beneficial.
     */
    [[nodiscard]] bool validate_fusion() const {
        if (regions.size() < 2) return false;
        for (const Region* r : regions) {
            if (!r || !r->validate()) return false;
        }
        return calculate_benefit() > 0.05;
    }
};

} // namespace dgtl::graph

