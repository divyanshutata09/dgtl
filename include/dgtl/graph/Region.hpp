/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL Backend vNext - Region Definition
 * Execution regions for region-based optimization
 * Extension: Polyhedral SCoP metadata for affine parallelization
 */

#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <string>

// Updated paths to reflect the move to /base/
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"

namespace dgtl::graph {

template <typename T, std::size_t Alignment>
class AlignedAllocator {
public:
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <typename U>
    constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > (static_cast<std::size_t>(-1) / sizeof(T))) {
            throw std::bad_alloc();
        }

        void* ptr = nullptr;
#if defined(_WIN32)
        ptr = _aligned_malloc(count * sizeof(T), Alignment);
        if (!ptr) {
            throw std::bad_alloc();
        }
#else
        if (posix_memalign(&ptr, Alignment, count * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
#endif
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept {
#if defined(_WIN32)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
};

template <typename T, typename U, std::size_t Alignment>
[[nodiscard]] constexpr bool operator==(const AlignedAllocator<T, Alignment>&,
                                        const AlignedAllocator<U, Alignment>&) noexcept {
    return true;
}

template <typename T, typename U, std::size_t Alignment>
[[nodiscard]] constexpr bool operator!=(const AlignedAllocator<T, Alignment>&,
                                        const AlignedAllocator<U, Alignment>&) noexcept {
    return false;
}

/**
 * @enum ScopAffinity
 * @brief Region classification for polyhedral parallelization.
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
 */
class Region {
public:
    // Now correctly recognizes dgtl::ir::IRNode from dgtl/ir/base/IR_Nodes.hpp
    std::vector<dgtl::ir::IRNode, AlignedAllocator<dgtl::ir::IRNode, 32>> nodes;
    std::vector<Region*> dependencies;
    std::unordered_set<Region*> dependents;

    // Region properties for optimization decisions
    bool is_hot = false;                    
    bool vectorizable = false;              
    bool speculative_candidate = false;     
    bool memory_bound = false;              
    bool compute_bound = false;             

    double estimated_cycles = 0.0;          // AI cost model prediction
    double cache_miss_rate = 0.0;           // Predicted cache behavior

    // === POLYHEDRAL SCoP METADATA (P0-002) ===
    ScopAnnotation scop_annotation;         

    void add_node(const dgtl::ir::IRNode& node);
    void add_dependency(Region* dep);

    [[nodiscard]] bool can_fuse_with(const Region& other) const;
    [[nodiscard]] std::unordered_set<uint32_t> get_live_ins() const;
    [[nodiscard]] std::unordered_set<uint32_t> get_live_outs() const;
    [[nodiscard]] bool validate() const;

    [[nodiscard]] size_t node_count() const noexcept;
    [[nodiscard]] double simd_density() const noexcept;
    [[nodiscard]] double memory_density() const noexcept;
    [[nodiscard]] double control_density() const noexcept;
    [[nodiscard]] double live_range_pressure() const;
};

} // namespace dgtl::graph