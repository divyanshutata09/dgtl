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

/**
 * @file Pass_Manager_Analysis.ixx
 * @brief Analysis preservation tracking and intelligent caching infrastructure.
 * * This header defines:
 * - Analysis preservation flags and dependency tracking
 * - Pass dependency graph for scheduling and cache reuse
 * - Zero-overhead analysis ID generation
 * - Analysis cache manager with selective invalidation
 * * Part 2 of 3 in the Pass_Manager refactoring (Config â†’ Analysis â†’ Orchestrator)
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

export module dgtl.ir.pipeline.pass_manager_analysis;

// Import local leaf partitions via short-form shortcuts
import dgtl.ir;
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.hashing;  // Natively import your high-performance hashing primitives
import dgtl.flat_map; // Natively import your high-performance flat map layer
import dgtl.sync;     // Natively import your ultra-low latency sync primitives
import dgtl.ir.pipeline.pass_manager_config;

export namespace dgtl::pipeline {

// ============================================================================
// SECTION 1: ANALYSIS PRESERVATION TRACKING
// ============================================================================

/**
 * @enum PreservedAnalysisKind
 * @brief Bit flags for selective analysis preservation across pass boundaries.
 */
enum class PreservedAnalysisKind : uint32_t {
    None                = 0u,
    DominatorTree       = (1u << 0),    // CFG-based dominance (preserved by non-CFG-mutating passes)
    LoopInfo            = (1u << 1),    // Loop nesting and trip-count info
    Liveness            = (1u << 2),    // Live variable analysis
    AliasAnalysis       = (1u << 3),    // Pointer alias information (preserved by non-memory-touching passes)
    MemoryDependence    = (1u << 4),    // Memory ordering dependencies
    ValueNumbering      = (1u << 5),    // Available expressions (GVN/PRE internal)
    PostDominatorTree   = (1u << 6),    // Post-dominance for reverse cfg analysis
    ControlDependence   = (1u << 7),    // Control dependencies for speculation
    DataFlowFactoring   = (1u << 8),    // Factored data-flow (SSA form preservation)
    RegionInfo          = (1u << 9),    // IR region structuring (preserved by structure-preserving passes)
    CallGraph           = (1u << 10),   // Inter-procedural call graph
    TargetData          = (1u << 11),   // ISA-specific sizing info (always preserved)
    LoopAccessInfo      = (1u << 12),   // Memory access patterns in loops (Polyhedral input)
    All                 = 0xFFFFFFFFu   // All analyses preserved
};

[[nodiscard]] constexpr PreservedAnalysisKind operator|(
    PreservedAnalysisKind lhs,
    PreservedAnalysisKind rhs
) noexcept {
    return static_cast<PreservedAnalysisKind>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)
    );
}

[[nodiscard]] constexpr PreservedAnalysisKind operator&(
    PreservedAnalysisKind lhs,
    PreservedAnalysisKind rhs
) noexcept {
    return static_cast<PreservedAnalysisKind>(
        static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)
    );
}

constexpr PreservedAnalysisKind& operator|=(
    PreservedAnalysisKind& lhs,
    PreservedAnalysisKind rhs
) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

struct PreservedAnalysesInfo {
    uint32_t preserved_mask = 0u;
    uint32_t provided_mask = 0u;

    void merge(uint32_t preserved, uint32_t provided) noexcept {
        preserved_mask |= preserved;
        provided_mask |= provided;
    }

    [[nodiscard]] bool is_preserved(PreservedAnalysisKind analysis) const noexcept {
        return (preserved_mask & static_cast<uint32_t>(analysis)) != 0u;
    }

    [[nodiscard]] bool is_provided(PreservedAnalysisKind analysis) const noexcept {
        return (provided_mask & static_cast<uint32_t>(analysis)) != 0u;
    }

    void reset() noexcept {
        preserved_mask = 0u;
        provided_mask = 0u;
    }
};

// ============================================================================
// SECTION 2: PASS DEPENDENCY GRAPH
// ============================================================================

/**
 * @class PassDependencyGraph
 * @brief Tracks dependencies and preservation relationships between passes.
 */
class PassDependencyGraph {
private:
    struct PassNode {
        DGTLOptimizationPass* pass = nullptr;
        uint32_t index = 0;
        uint32_t preserved_analyses = 0;
        uint32_t provided_analyses = 0;
        std::vector<uint32_t> depends_on;
        std::vector<uint32_t> invalidates;

        [[nodiscard]] bool has_dependencies() const noexcept { return !depends_on.empty(); }
        [[nodiscard]] bool invalidates_passes() const noexcept { return !invalidates.empty(); }
    };

    std::vector<PassNode> graph;

    // FIXED: Replaced standard map with custom FastFlatMap to drop leaky template declarations
    dgtl::utils::FastFlatMap<std::string_view, uint32_t, 256> pass_name_to_index;

public:
    PassDependencyGraph() noexcept = default;

    void register_pass(
        DGTLOptimizationPass* pass,
        uint32_t preserved_analyses,
        uint32_t provided_analyses
    ) noexcept {
        if (!pass) return;

        PassNode node{};
        node.pass = pass;
        node.index = static_cast<uint32_t>(graph.size());
        node.preserved_analyses = preserved_analyses;
        node.provided_analyses = provided_analyses;

        const std::string_view name(pass->get_pass_name());
        pass_name_to_index.insert_or_assign(name, node.index);
        graph.push_back(node);
    }

    [[nodiscard]] bool is_analysis_preserved(
        PreservedAnalysisKind analysis_kind,
        uint32_t until_pass_index
    ) const noexcept {
        if (until_pass_index >= graph.size()) return false;

        const uint32_t bit = static_cast<uint32_t>(analysis_kind);
        return std::all_of(graph.begin(), graph.begin() + until_pass_index + 1, [bit](const auto& node) noexcept {
            return (node.preserved_analyses & bit) != 0u;
        });
    }

    [[nodiscard]] int32_t find_pass_providing_analysis(
        PreservedAnalysisKind analysis_kind
    ) const noexcept {
        const uint32_t bit = static_cast<uint32_t>(analysis_kind);
        auto it = std::find_if(graph.rbegin(), graph.rend(), [bit](const auto& node) noexcept {
            return (node.provided_analyses & bit) != 0u;
        });
        return (it != graph.rend()) ? static_cast<int32_t>(std::distance(it, graph.rend()) - 1) : -1;
    }

    [[nodiscard]] bool can_reuse_analysis(uint32_t from_pass_idx, uint32_t to_pass_idx) const noexcept {
        if (from_pass_idx >= graph.size() || to_pass_idx >= graph.size() || from_pass_idx >= to_pass_idx) return false;

        const uint32_t analyses_to_check = graph[from_pass_idx].provided_analyses;
        const uint32_t bit_count = 32;

        for (uint32_t bit = 0; bit < bit_count; ++bit) {
            if ((analyses_to_check & (1u << bit)) == 0u) continue;

            const bool all_preserved = std::all_of(graph.begin() + from_pass_idx + 1, graph.begin() + to_pass_idx + 1, [bit](const auto& node) noexcept {
                return (node.preserved_analyses & (1u << bit)) != 0u;
            });
            if (!all_preserved) return false;
        }
        return true;
    }

    [[nodiscard]] size_t pass_count() const noexcept { return graph.size(); }

    void clear() noexcept {
        graph.clear();
        pass_name_to_index.clear();
    }
};

// ============================================================================
// SECTION 3: ANALYSIS CACHE & TYPE ID
// ============================================================================

using AnalysisID = uintptr_t;

template <typename T>
struct AnalysisTypeKey {
    static inline char dummy;
    static AnalysisID get() noexcept { return reinterpret_cast<AnalysisID>(&dummy); }
};

class DGTLAnalysis {
public:
    virtual ~DGTLAnalysis() = default;

    /**
     * @brief Compute this analysis on the given IR stream.
     * Called once per unique IR version; result is cached.
     * @param stream The IR stream to analyze
     */
    virtual void compute(ir::IRStream& stream) = 0;

    /**
     * @brief Get human-readable name of this analysis.
     * @return String identifier for debugging/tracing
     */
    [[nodiscard]] virtual std::string_view get_analysis_name() const noexcept = 0;
};

// ============================================================================
// SECTION 4: ANALYSIS CACHE TAGS
// ============================================================================

struct DominatorTreeAnalysisTag;
struct LoopInfoAnalysisTag;
struct LivenessAnalysisTag;
struct AliasAnalysisTag;
struct MemoryDependenceAnalysisTag;
struct ValueNumberingAnalysisTag;

// ============================================================================
// SECTION 5: ANALYSIS CACHE MANAGER
// ============================================================================

class DGTLAnalysisManager {
private:
    struct AnalysisCacheEntry {
        std::unique_ptr<DGTLAnalysis> result;
        uint64_t ir_version_at_compute{0};

        [[nodiscard]] bool is_valid_at_version(uint64_t current_ver) const noexcept {
            return ir_version_at_compute == current_ver;
        }
    };

    // FIXED: Standardized Cache allocations via your high-performance FastFlatMap engine
    mutable dgtl::utils::FastFlatMap<AnalysisID, AnalysisCacheEntry, 64> cache;
    
    // MODERNIZED ACCESS: Dropped standard library heavy tracking layers in favor of the FastSharedSpinLock
    mutable dgtl::utils::sync::FastSharedSpinLock manager_mutex;
    uint64_t last_known_ir_version = 0;

public:
    DGTLAnalysisManager() noexcept = default;

    // Non-copyable
    DGTLAnalysisManager(const DGTLAnalysisManager&) = delete;
    DGTLAnalysisManager& operator=(const DGTLAnalysisManager&) = delete;

    // Move-constructible
    DGTLAnalysisManager(DGTLAnalysisManager&& other) noexcept {
        if (this == &other) return;
        // Adjusted safely using the non-reentrant direct writer fallback since we own both references
        other.manager_mutex.lock();
        manager_mutex.lock();
        
        cache = std::move(other.cache);
        last_known_ir_version = other.last_known_ir_version;
        other.cache.clear();
        other.last_known_ir_version = 0;
        
        manager_mutex.unlock();
        other.manager_mutex.unlock();
    }

    template <typename AnalysisT>
    [[nodiscard]] const AnalysisT& get_result(ir::IRStream& stream) {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");

        const uint64_t current_version = stream.get_version();
        const AnalysisID id = AnalysisTypeKey<AnalysisT>::get();

        {
            // MODERNIZED ACCESS: Reentrancy-safe HFT-grade shared reader guard
            dgtl::utils::sync::SharedLockGuard read_lock(manager_mutex);
            if (current_version == last_known_ir_version) [[likely]] {
                auto* val = cache.find(id);
                if (val != nullptr) return *static_cast<const AnalysisT*>(val->result.get());
            }
        }

        // MODERNIZED ACCESS: Priority anti-starvation exclusive writer guard
        dgtl::utils::sync::UniqueLockGuard write_lock(manager_mutex);

        if (current_version != last_known_ir_version) [[unlikely]] {
            cache.clear();
            last_known_ir_version = current_version;
        }

        auto* val = cache.find(id);
        if (val != nullptr) return *static_cast<const AnalysisT*>(val->result.get());

        auto result = std::make_unique<AnalysisT>();
        result->compute(stream);

        const AnalysisT& ref = *result;
        cache.insert_or_assign(id, AnalysisCacheEntry{ std::move(result), current_version });

        return ref;
    }

    void invalidate_all() noexcept {
        dgtl::utils::sync::UniqueLockGuard lock(manager_mutex);
        cache.clear();
        last_known_ir_version = 0;
    }

    template <typename AnalysisT>
    [[nodiscard]] bool is_cached() const noexcept {
        dgtl::utils::sync::SharedLockGuard lock(manager_mutex);
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    template <typename AnalysisT>
    [[nodiscard]] bool is_cached_for(const ir::IRStream& stream) const noexcept {
        dgtl::utils::sync::SharedLockGuard lock(manager_mutex);
        if (stream.get_version() != last_known_ir_version) return false;
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    template <typename AnalysisT>
    void invalidate_specific() noexcept {
        dgtl::utils::sync::UniqueLockGuard lock(manager_mutex);
        cache.erase(AnalysisTypeKey<AnalysisT>::get());
    }

    void invalidate_by_mask(uint32_t mask) noexcept {
        constexpr uint32_t kNone             = 0u;
        constexpr uint32_t kDominatorTree    = (1u << 0);
        constexpr uint32_t kLoopInfo         = (1u << 1);
        constexpr uint32_t kLiveness         = (1u << 2);
        constexpr uint32_t kAliasAnalysis    = (1u << 3);
        constexpr uint32_t kMemoryDependence = (1u << 4);
        constexpr uint32_t kValueNumbering   = (1u << 5);
        constexpr uint32_t kAllKnown =
            kDominatorTree | kLoopInfo | kLiveness | kAliasAnalysis | kMemoryDependence | kValueNumbering;

        if (mask == kNone) {
            return;
        }

        if (mask == 0xFFFFFFFFu || (mask & kAllKnown) == kAllKnown) {
            invalidate_all();
            return;
        }

        dgtl::utils::sync::UniqueLockGuard lock(manager_mutex);
        if ((mask & kDominatorTree) != 0u) cache.erase(AnalysisTypeKey<DominatorTreeAnalysisTag>::get());
        if ((mask & kLoopInfo) != 0u)      cache.erase(AnalysisTypeKey<LoopInfoAnalysisTag>::get());
        if ((mask & kLiveness) != 0u)      cache.erase(AnalysisTypeKey<LivenessAnalysisTag>::get());
        if ((mask & kAliasAnalysis) != 0u)   cache.erase(AnalysisTypeKey<AliasAnalysisTag>::get());
        if ((mask & kMemoryDependence) != 0u) cache.erase(AnalysisTypeKey<MemoryDependenceAnalysisTag>::get());
        if ((mask & kValueNumbering) != 0u)   cache.erase(AnalysisTypeKey<ValueNumberingAnalysisTag>::get());
    }
};

} // namespace dgtl::pipeline

export namespace dgtl::ir::pipeline {
    using namespace dgtl::pipeline;
    using PreservedAnalysisKind = dgtl::pipeline::PreservedAnalysisKind;
    using PassDependencyGraph = dgtl::pipeline::PassDependencyGraph;
    using DGTLAnalysisManager = dgtl::pipeline::DGTLAnalysisManager;
}
