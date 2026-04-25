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

/* * DGTL Affine SCoP Detection and Region Annotation
 * STEP 1: POLYHEDRAL LOOP NEST DISCOVERY
 * PILLAR 1: LEGAL TRANSFORMATION PREREQUISITE
 * PURPOSE: Detect Static-Control Polyhedra (SCoPs) for affine parallelization
 */

#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/graph/Region.hpp"

namespace dgtl::analysis::polyhedral {

/**
 * @enum ScopClassification
 * @brief Classification of a region's affinity for polyhedral transforms.
 */
enum class ScopClassification : uint8_t {
    INVALID = 0,                  // Not yet analyzed
    AFFINE_CANDIDATE = 1,         // Affine bounds, linear subscripts, analyzable deps
    AFFINE_CANDIATE = AFFINE_CANDIDATE, // Backward compatibility for legacy typo usage
    NON_AFFINE_LOOP = 2,          // Non-affine loop bounds or strides
    NON_AFFINE_SUBSCRIPT = 3,     // Non-linear or non-affine memory accesses
    NON_AFFINE_CONTROL = 4,       // Data-dependent control flow
    IRREGULAR_MEMORY = 5,         // Irreducible pointer/indirect accesses
    TOO_COMPLEX = 6,              // Exceeds profitability bounds
};

/**
 * @struct ScopMetadata
 * @brief Per-region affinity metadata for polyhedral decisions.
 */
struct ScopMetadata {
    ScopClassification classification = ScopClassification::INVALID;
    
    // Loop nest structure
    uint32_t loop_depth = 0;                    // Detected loop nesting depth
    std::vector<uint32_t> induction_vars;      // IR slot IDs of loop counters
    std::vector<int64_t> lower_bounds;         // Constant or affine bounds
    std::vector<int64_t> upper_bounds;
    std::vector<int64_t> strides;
    
    // Affinity flags
    bool has_affine_bounds = false;            // All loop bounds are affine
    bool has_affine_subscripts = false;        // Array subscripts are linear combinations
    bool has_analyzable_dependencies = false;  // Data dependencies form a polyhedron
    bool has_irregular_control = false;        // Unpredictable branches
    
    // Profitability signals
    uint32_t load_count = 0;                   // LOAD/GATHER operations
    uint32_t store_count = 0;                  // STORE operations
    double reuse_distance = 0.0;               // Estimated temporal reuse
    double memory_footprint_bytes = 0.0;       // Working set size estimate
    
    // Transformation eligibility
    bool can_tile = false;                     // Legal tiling candidate
    bool can_fuse = false;                     // Fusible with adjacent regions
    bool can_parallelize = false;              // Safe for parallel emission
    
    // Confidence and telemetry
    uint32_t confidence_score = 0;             // 0-100 detection confidence
    std::string rejection_reason;              // Why it was rejected, if applicable
};

/**
 * @class ScopDetector
 * @brief Detects affine Static-Control Polyhedra (SCoPs) in execution regions.
 * 
 * SCoPs are loop nests with:
 * - Affine loop bounds (constant or linear in outer loop counters)
 * - Affine array subscripts (linear combinations of induction variables)
 * - Analyzable data dependencies
 * 
 * Non-affine patterns are rejected early with reason codes to prevent
 * illegal polyhedral transformations.
 */
class ScopDetector {
public:
    /**
     * @brief Analyze a region for SCoP affinity and add metadata.
     * @param region Target execution region to classify.
     * @return ScopMetadata with classification and transformation eligibility.
     */
    [[nodiscard]] ScopMetadata analyze_region(const graph::Region& region) const;

    /**
     * @brief Batch analyze all regions in execution graph.
     * Annotates each region in-place with polyhedral metadata.
     * @param regions Regions to classify.
     * @return Vector of ScopMetadata keyed by region index.
     */
    [[nodiscard]] std::vector<ScopMetadata> analyze_regions(
        const std::vector<std::unique_ptr<graph::Region>>& regions) const;

    /**
     * @brief Check if a region can be safely tiled under constraints.
     * @param region Region to check.
     * @param metadata Pre-computed ScopMetadata for region.
     * @return true if tiling is legal and profitable.
     */
    [[nodiscard]] bool is_tileable(
        const graph::Region& region,
        const ScopMetadata& metadata) const;

    /**
     * @brief Check if two affine regions can be legally fused.
     * @param producer Producer region.
     * @param consumer Consumer region.
     * @return true if fusion preserves semantics and improves locality.
     */
    [[nodiscard]] bool can_fuse_affine(
        const graph::Region& producer,
        const graph::Region& consumer,
        const ScopMetadata& prod_meta,
        const ScopMetadata& cons_meta) const;

    /**
     * @brief Detect loop nesting depth in a region.
     * Counts backward branches and counter-like patterns.
     * @param region Region to analyze.
     * @return Estimated loop depth (0 = no loops, 1+ = nested loops).
     */
    [[nodiscard]] uint32_t detect_loop_depth(const graph::Region& region) const;

    /**
     * @brief Check if all memory accesses in region are affine.
     * @param region Region to analyze.
     * @param metadata ScopMetadata with induction variable info.
     * @return true if all LOAD/STORE/GATHER use linear subscripts.
     */
    [[nodiscard]] bool check_affine_subscripts(
        const graph::Region& region,
        const ScopMetadata& metadata) const;

    /**
     * @brief Estimate temporal and spatial reuse in a region.
     * Used by profitability heuristics to decide if transformation is worthwhile.
     * @param region Region to analyze.
     * @return Estimated reuse distance in memory references.
     */
    [[nodiscard]] double estimate_reuse_distance(const graph::Region& region) const;

private:
    static constexpr uint32_t kMaxLoopDepth = 8;     // Support up to 8D loop nests
    static constexpr size_t kMaxRegionSize = 512;    // Skip regions > 512 nodes
    static constexpr double kMinReuseDistanceForTile = 1.5; // Heuristic threshold

    /**
     * @brief Detect induction variables (loop counters) in region.
     */
    [[nodiscard]] std::vector<uint32_t> detect_induction_variables(
        const graph::Region& region) const;

    /**
     * @brief Check if a vreg slot is loop-counter-like (incremented each iteration).
     */
    [[nodiscard]] bool is_induction_variable(
        const graph::Region& region,
        uint32_t vreg_slot) const;

    /**
     * @brief Extract loop bounds from region (if affine).
     */
    [[nodiscard]] bool extract_affine_bounds(
        const graph::Region& region,
        const std::vector<uint32_t>& induction_vars,
        std::vector<int64_t>& lower,
        std::vector<int64_t>& upper,
        std::vector<int64_t>& strides) const;

    /**
     * @brief Check for data-dependent control flow (non-affine).
     */
    [[nodiscard]] bool has_irregular_control(const graph::Region& region) const;

    /**
     * @brief Estimate memory footprint for profitability checks.
     */
    [[nodiscard]] double estimate_memory_footprint(
        const graph::Region& region,
        const ScopMetadata& metadata) const;
};

} // namespace dgtl::analysis::polyhedral
