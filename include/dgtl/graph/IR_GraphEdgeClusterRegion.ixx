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

module;

#include <cstdlib>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <string>
#include <vector>

// 🚀 FIXED: Bind directly to your new core macro sub-module space
export module dgtl.ir_core.graph_region;

import dgtl.ir_node;
// 🚀 FIXED: Set to your exact renamed string identifier
import dgtl.ir_core.stream;

import dgtl.target;
import dgtl.flat_map; // 🚀 Natively import your ultra-low latency flat map layers
import dgtl.sync;     // 🚀 Natively import your spin locks

export namespace dgtl::graph {

// Forward declaration so FlatRegionSet can cleanly resolve the pointer signature
class Region;

template <typename T, std::size_t Alignment>
class AlignedAllocator {
public:
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <typename U>
    constexpr explicit AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

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
    [[maybe_unused]] std::string rejection_reason;
};

// 🚀 PRODUCTION TYPE EXTENSION: Flat non-allocating set replacement for region tracking
struct FlatRegionSet {
    Region* storage[32]{};
    size_t count = 0;

    bool contains(const Region* val) const noexcept {
        for(size_t i = 0; i < count; ++i) if(storage[i] == val) return true;
        return false;
    }

    bool insert(Region* val) noexcept {
        if (contains(val)) return false;
        if (count < 32) { storage[count++] = val; return true; }
        return false;
    }
};

struct FlatVRegSet {
    uint32_t storage[64]{};
    size_t count = 0;

    bool contains(uint32_t val) const noexcept {
        for(size_t i = 0; i < count; ++i) if(storage[i] == val) return true;
        return false;
    }

    bool insert(uint32_t val) noexcept {
        if (contains(val)) return false;
        if (count < 64) { storage[count++] = val; return true; }
        return false;
    }
};

/**
 * @class Region
 * @brief A group of IR nodes that can be optimized together.
 */
class Region {
public:
    std::vector<dgtl::ir::IRNode, AlignedAllocator<dgtl::ir::IRNode, 32>> nodes;
    std::vector<Region*> dependencies;
    
    // 🚀 MODERNIZE: Replaced std::unordered_set with HFT-grade FlatRegionSet
    FlatRegionSet dependents;

    bool is_hot = false;
    bool vectorizable = false;
    bool speculative_candidate = false;
    bool memory_bound = false;
    bool compute_bound = false;

    double estimated_cycles = 0.0;
    double cache_miss_rate = 0.0;

    ScopAnnotation scop_annotation;

    [[nodiscard]] size_t node_count() const noexcept { return nodes.size(); }
    [[nodiscard]] bool has_dependencies() const noexcept { return !dependencies.empty(); }
    [[nodiscard]] size_t dependency_count() const noexcept { return dependencies.size(); }
    [[nodiscard]] const ScopAnnotation& get_scop_annotation() const noexcept { return scop_annotation; }

    [[nodiscard]] const FlatRegionSet& get_dependents() const noexcept { return dependents; }
    void add_dependent(Region* region) noexcept { dependents.insert(region); }

    void add_node(const dgtl::ir::IRNode& node);
    void add_dependency(Region* dep) {
        if (dep) {
            dependencies.push_back(dep);
            dep->add_dependent(this);
        }
    }

    [[nodiscard]] bool can_fuse_with(const Region& other) const;
    [[nodiscard]] FlatVRegSet get_live_ins() const;
    [[nodiscard]] FlatVRegSet get_live_outs() const;
    [[nodiscard]] bool validate() const;

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
    BRANCH_FUSION,
    Vertical = KERNEL_FUSION
};

/**
 * @class FusionCluster
 * @brief A cluster of regions that can be fused together.
 */
class FusionCluster {
public:
    std::vector<Region*> regions;
    FusionType fusion_type = FusionType::Vertical;
    double fusion_benefit = 0.0;

    [[nodiscard]] bool can_add_region(Region* region) const {
        if (!region || !region->validate()) return false;
        if (std::find(regions.begin(), regions.end(), region) != regions.end()) return false;
        if (regions.empty()) return true;

        size_t current_nodes = std::accumulate(regions.begin(), regions.end(), size_t(0),
                                            [](size_t sum, const Region* r) {
                                                return sum + (r ? r->node_count() : 0);
                                            });

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
        if (!std::all_of(regions.begin(), regions.end(), [](const Region* r) { return r && r->validate(); })) return false;
        return calculate_benefit() > 0.05;
    }
};

/**
 * @class ExecutionGraph
 * @brief High-level execution graph that groups IR nodes into regions.
 */
class ExecutionGraph {
private:
    mutable dgtl::utils::sync::FastSharedSpinLock graph_lock;

public:
    static constexpr std::size_t kPreferredNodeAlignment = 32;

    std::vector<std::unique_ptr<Region>> regions;

    [[nodiscard]] size_t region_count() const noexcept { return regions.size(); }
    [[nodiscard]] bool empty() const noexcept { return regions.empty(); }
    [[nodiscard]] const Region* get_region(size_t index) const noexcept {
        return (index < regions.size()) ? regions[index].get() : nullptr;
    }

    void build_from_ir(::dgtl::ir::IRStream& global_ir);
    void schedule(const dgtl::core::TargetMachine& target);
    [[nodiscard]] std::vector<std::vector<Region*>> find_fusion_candidates() const;
    [[nodiscard]] bool validate() const;

    [[nodiscard]] static constexpr std::size_t preferred_node_alignment() noexcept {
        return kPreferredNodeAlignment;
    }

    [[nodiscard]] static double region_schedule_score(const Region& region, const dgtl::core::TargetMachine& target) {
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