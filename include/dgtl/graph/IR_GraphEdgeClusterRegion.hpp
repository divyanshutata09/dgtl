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

/*
 * DGTL Graph Layer - Unified Graph Edge/Cluster/Region Module
 * Consolidates:
 *   - Dependency_Edge.hpp
 *   - Execution_Graph.hpp
 *   - Fusion_Cluster.hpp
 *   - Region.hpp
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

#include "dgtl/core/Target.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/base/IR_Node.hpp"

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
    UNANALYZED = 0,
    AFFINE_CANDIDATE = 1,
    NON_AFFINE = 2,
};

/**
 * @struct ScopAnnotation
 * @brief Polyhedral SCoP metadata attached to each region.
 */
struct ScopAnnotation {
    ScopAffinity affinity = ScopAffinity::UNANALYZED;
    uint32_t loop_depth = 0;
    bool can_tile = false;
    bool can_fuse = false;
    bool can_parallelize = false;
    uint32_t confidence_score = 0;
    std::string rejection_reason;
};

/**
 * @class Region
 * @brief A group of IR nodes that can be optimized together.
 */
class Region {
public:
    std::vector<dgtl::ir::IRNode, AlignedAllocator<dgtl::ir::IRNode, 32>> nodes;
    std::vector<Region*> dependencies;
    std::unordered_set<Region*> dependents;

    bool is_hot = false;
    bool vectorizable = false;
    bool speculative_candidate = false;
    bool memory_bound = false;
    bool compute_bound = false;

    double estimated_cycles = 0.0;
    double cache_miss_rate = 0.0;

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

/**
 * @enum DependencyType
 * @brief Types of dependencies between regions.
 */
enum class DependencyType : uint8_t {
    DATA,
    CONTROL,
    MEMORY,
    REGISTER,
    SPECULATIVE
};

/**
 * @class DependencyEdge
 * @brief Represents a typed dependency between two regions.
 */
class DependencyEdge {
public:
    Region* from = nullptr;
    Region* to = nullptr;
    DependencyType type;
    double latency_penalty = 0.0;

    DependencyEdge(Region* f, Region* t, DependencyType dt) noexcept
        : from(f), to(t), type(dt) {}

    [[nodiscard]] bool can_relax() const noexcept {
        if (!from || !to || from == to) {
            return false;
        }

        switch (type) {
            case DependencyType::CONTROL:
            case DependencyType::MEMORY:
                return false;
            case DependencyType::SPECULATIVE:
            case DependencyType::REGISTER:
                return true;
            case DependencyType::DATA:
            default:
                return latency_penalty < 1.0;
        }
    }

    [[nodiscard]] double get_cost() const noexcept {
        const double base = [&]() noexcept {
            switch (type) {
                case DependencyType::DATA: return 1.0;
                case DependencyType::CONTROL: return 2.0;
                case DependencyType::MEMORY: return 2.5;
                case DependencyType::REGISTER: return 0.75;
                case DependencyType::SPECULATIVE: return 0.5;
                default: return 1.0;
            }
        }();

        return std::max(0.0, base + latency_penalty);
    }
};

/**
 * @enum FusionType
 * @brief Types of fusion optimizations.
 */
enum class FusionType {
    VECTOR_FUSION,
    LOOP_FUSION,
    KERNEL_FUSION,
    MEMORY_FUSION,
    BRANCH_FUSION
};

/**
 * @class FusionCluster
 * @brief A cluster of regions that can be fused together.
 */
class FusionCluster {
public:
    std::vector<Region*> regions;
    FusionType fusion_type;
    double fusion_benefit = 0.0;

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

    void add_region(Region* region) {
        if (!can_add_region(region)) return;
        if (std::find(regions.begin(), regions.end(), region) != regions.end()) return;
        regions.push_back(region);
        fusion_benefit = calculate_benefit();
    }

    void apply_fusion() {
        for (Region* r : regions) {
            if (!r) continue;
            r->speculative_candidate = r->speculative_candidate || (fusion_type == FusionType::KERNEL_FUSION);
            r->vectorizable = r->vectorizable || (fusion_type == FusionType::VECTOR_FUSION);
            r->memory_bound = r->memory_bound || (fusion_type == FusionType::MEMORY_FUSION);
        }
        fusion_benefit = calculate_benefit();
    }

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

    [[nodiscard]] bool validate_fusion() const {
        if (regions.size() < 2) return false;
        for (const Region* r : regions) {
            if (!r || !r->validate()) return false;
        }
        return calculate_benefit() > 0.05;
    }
};

/**
 * @class ExecutionGraph
 * @brief High-level execution graph that groups IR nodes into regions.
 */
class ExecutionGraph {
public:
    static constexpr std::size_t kPreferredNodeAlignment = 32;

    std::vector<std::unique_ptr<Region>> regions;

    void build_from_ir(dgtl::ir::IRStream& global_ir);
    void schedule(const dgtl::core::TargetMachine& target);
    [[nodiscard]] std::vector<std::vector<Region*>> find_fusion_candidates() const;
    [[nodiscard]] bool validate() const;

    [[nodiscard]] static constexpr std::size_t preferred_node_alignment() noexcept {
        return kPreferredNodeAlignment;
    }

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