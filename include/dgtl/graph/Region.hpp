/* * DGTL Backend vNext - Region Definition
 * Execution regions for region-based optimization
 * Extension: Polyhedral SCoP metadata for affine parallelization
 */

#pragma once
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <string>
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"

namespace dgtl::graph {

/**
 * @enum ScopAffinity
 * @brief Region classification for polyhedral parallelization.
 * Forward declaration; full enum is in analysis/polyhedral/Scop_Detector.hpp
 */
enum class ScopAffinity : uint8_t {
    UNANALYZED = 0,       // Not yet classified
    AFFINE_CANDIDATE = 1, // Eligible for polyhedral transforms
    NON_AFFINE = 2,       // Ineligible (non-linear, data-dependent, etc.)
};

/**
 * @struct ScopAnnotation
 * @brief Polyhedral SCoP metadata attached to each region.
 */
struct ScopAnnotation {
    ScopAffinity affinity = ScopAffinity::UNANALYZED;
    
    uint32_t loop_depth = 0;                // Detected loop nesting depth
    bool can_tile = false;                  // Tiling is legal and profitable
    bool can_fuse = false;                  // Fusible with adjacent affine regions
    bool can_parallelize = false;           // Safe for parallel lowering
    
    uint32_t confidence_score = 0;          // 0-100 analysis confidence
    std::string rejection_reason;           // Why marked NON_AFFINE, if applicable
};

/**
 * @class Region
 * @brief A group of IR nodes that can be optimized together.
 * Represents a unit of execution that may be vectorized, fused, or speculated.
 * Extended with polyhedral SCoP metadata for affine parallelization.
 */
class Region {
public:
    std::vector<dgtl::ir::IRNode> nodes;
    std::vector<Region*> dependencies;
    std::unordered_set<Region*> dependents;

    // Region properties for optimization decisions
    bool is_hot = false;                    // Frequently executed
    bool vectorizable = false;              // Can be vectorized
    bool speculative_candidate = false;     // Good for speculation
    bool memory_bound = false;              // Memory access heavy
    bool compute_bound = false;             // Compute intensive

    double estimated_cycles = 0.0;          // AI cost model prediction
    double cache_miss_rate = 0.0;           // Predicted cache behavior

    // === POLYHEDRAL SCoP METADATA (P0-002) ===
    ScopAnnotation scop_annotation;         // SCoP affinity and transformation eligibility

    /**
     * @brief Add a node to this region.
     */
    void add_node(const dgtl::ir::IRNode& node);

    /**
     * @brief Add dependency on another region.
     */
    void add_dependency(Region* dep);

    /**
     * @brief Check if this region can be fused with another.
     */
    bool can_fuse_with(const Region& other) const;

    /**
     * @brief Get all input/output registers for register allocation.
     */
    std::unordered_set<uint32_t> get_live_ins() const;
    std::unordered_set<uint32_t> get_live_outs() const;

    /**
     * @brief Validate region consistency.
     */
    bool validate() const;

    /**
     * @brief Lightweight region signals used by adaptive scheduling.
     */
    [[nodiscard]] size_t node_count() const noexcept;
    [[nodiscard]] double simd_density() const noexcept;
    [[nodiscard]] double memory_density() const noexcept;
    [[nodiscard]] double control_density() const noexcept;
    [[nodiscard]] double live_range_pressure() const;
};

} // namespace dgtl::graph

