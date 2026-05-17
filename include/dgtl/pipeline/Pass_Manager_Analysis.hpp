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
 * @file Pass_Manager_Analysis.hpp
 * @brief Analysis preservation tracking and intelligent caching infrastructure.
 * 
 * This header defines:
 * - Analysis preservation flags and dependency tracking
 * - Pass dependency graph for scheduling and cache reuse
 * - Zero-overhead analysis ID generation
 * - Analysis cache manager with selective invalidation
 * 
 * Part 2 of 3 in the Pass_Manager refactoring (Config → Analysis → Orchestrator)
 */

#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager_Config.hpp"

namespace dgtl::pipeline {

// ============================================================================
// SECTION 1: ANALYSIS PRESERVATION TRACKING
// ============================================================================

/**
 * @enum PreservedAnalysisKind
 * @brief Bit flags for selective analysis preservation across pass boundaries.
 * 
 * Each bit indicates an analysis that remains valid after a pass executes.
 * Passes declare which analyses they preserve to enable downstream cache reuse.
 * Pass manager uses this to avoid redundant re-computation.
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
    All                 = 0xFFFFFFFFu   // All analyses preserved (conservative; only use for read-only passes)
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

/**
 * @struct PreservedAnalysesInfo
 * @brief Aggregates preservation declarations from all passes in a chain.
 * Used by the pass manager to avoid recomputing analyses between passes.
 */
struct PreservedAnalysesInfo {
    uint32_t preserved_mask = 0u;   ///< Mask of preserved analyses in chain
    uint32_t provided_mask = 0u;    ///< Mask of analyses provided/computed in chain

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
 * 
 * Enables intelligent scheduling and cache reuse across the optimization pipeline.
 * Pass manager uses this to:
 * - Determine correct execution order
 * - Decide which analyses can be reused vs. must be recomputed
 * - Identify pass conflicts and dependencies
 */
class PassDependencyGraph {
private:
    /**
     * @struct PassNode
     * @brief Internal representation of a pass in the dependency graph.
     */
    struct PassNode {
        DGTLOptimizationPass* pass = nullptr;
        uint32_t index = 0;
        uint32_t preserved_analyses = 0;
        uint32_t provided_analyses = 0;
        std::vector<uint32_t> depends_on;   ///< Indices of passes this depends on
        std::vector<uint32_t> invalidates;  ///< Indices of passes whose output this invalidates
    };

    std::vector<PassNode> graph;
    std::unordered_map<std::string_view, uint32_t> pass_name_to_index;

public:
    /**
     * @brief Register a pass and its preservation/dependency relationships.
     * @param pass  The pass to register
     * @param preserved_analyses  Bitmask of analyses preserved by this pass
     * @param provided_analyses   Bitmask of analyses computed by this pass
     */
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
        pass_name_to_index[name] = node.index;
        graph.push_back(node);
    }

    /**
     * @brief Query if an analysis is preserved by a pass chain up to index `until`.
     * @param analysis_kind The analysis to check
     * @param until_pass_index Check only passes up to this index (inclusive)
     * @return true if the analysis remains valid through entire chain
     */
    [[nodiscard]] bool is_analysis_preserved(
        PreservedAnalysisKind analysis_kind,
        uint32_t until_pass_index
    ) const noexcept {
        if (until_pass_index >= graph.size()) {
            return false;
        }

        const uint32_t bit = static_cast<uint32_t>(analysis_kind);
        for (uint32_t i = 0; i <= until_pass_index; ++i) {
            if ((graph[i].preserved_analyses & bit) == 0u) {
                return false;  // Analysis invalidated by pass[i]
            }
        }
        return true;
    }

    /**
     * @brief Get the latest pass that provides/computes a specific analysis.
     * @param analysis_kind The analysis to search for
     * @return Index of the pass providing it, or -1 if not found/available
     */
    [[nodiscard]] int32_t find_pass_providing_analysis(
        PreservedAnalysisKind analysis_kind
    ) const noexcept {
        const uint32_t bit = static_cast<uint32_t>(analysis_kind);
        for (int32_t i = static_cast<int32_t>(graph.size()) - 1; i >= 0; --i) {
            if ((graph[i].provided_analyses & bit) != 0u) {
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief Check if pass B should reuse analysis from pass A.
     * True if all analyses A provides are preserved through B.
     * @param from_pass_idx Index of pass providing the analysis
     * @param to_pass_idx Index of pass that might reuse it
     * @return true if reuse is safe
     */
    [[nodiscard]] bool can_reuse_analysis(uint32_t from_pass_idx, uint32_t to_pass_idx) const noexcept {
        if (from_pass_idx >= graph.size() || to_pass_idx >= graph.size() || from_pass_idx >= to_pass_idx) {
            return false;
        }

        const uint32_t analyses_to_check = graph[from_pass_idx].provided_analyses;
        const uint32_t bit_count = 32;

        for (uint32_t bit = 0; bit < bit_count; ++bit) {
            if ((analyses_to_check & (1u << bit)) == 0u) {
                continue;  // This analysis not provided by from_pass
            }

            // Check if this bit is preserved through the chain [from+1, to]
            for (uint32_t i = from_pass_idx + 1; i <= to_pass_idx; ++i) {
                if ((graph[i].preserved_analyses & (1u << bit)) == 0u) {
                    return false;  // Analysis invalidated by pass[i]
                }
            }
        }
        return true;
    }

    [[nodiscard]] size_t pass_count() const noexcept {
        return graph.size();
    }

    void clear() noexcept {
        graph.clear();
        pass_name_to_index.clear();
    }
};

// ============================================================================
// SECTION 3: ANALYSIS CACHE & TYPE ID
// ============================================================================

/**
 * @brief Zero-overhead Type ID Generator for analysis providers.
 * Uses address-of static dummy to generate unique ID per analysis type.
 */
using AnalysisID = uintptr_t;

/**
 * @struct AnalysisTypeKey
 * @brief Generates compile-time unique ID for each analysis type.
 * Template specialization ensures one ID per type at compile-time.
 */
template <typename T>
struct AnalysisTypeKey {
    static inline char dummy;
    static AnalysisID get() noexcept {
        return reinterpret_cast<AnalysisID>(&dummy);
    }
};

/**
 * @class DGTLAnalysis
 * @brief Base concept for all analysis providers (DomTree, LoopInfo, AliasAnalysis, etc).
 * Analyses compute IR properties and are cached by the analysis manager.
 */
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

/**
 * @brief Tag types for selective analysis cache invalidation.
 * Each tag corresponds to one analysis type that can be invalidated.
 * Used in invalidate_by_mask() to selectively evict cache entries.
 */
struct DominatorTreeAnalysisTag;
struct LoopInfoAnalysisTag;
struct LivenessAnalysisTag;
struct AliasAnalysisTag;
struct MemoryDependenceAnalysisTag;
struct ValueNumberingAnalysisTag;

// ============================================================================
// SECTION 5: ANALYSIS CACHE MANAGER
// ============================================================================

/**
 * @class DGTLAnalysisManager
 * @brief Caches and manages lifecycle of IR analysis results.
 * 
 * Features:
 * - Version-gated caching (invalidate on IR change)
 * - Type-safe analysis requests via templates
 * - Selective analysis invalidation by mask
 * - Thread-safe access with shared_mutex (read-heavy optimization)
 * 
 * Usage:
 *   auto& dom_tree = manager.get_result<DominatorTreeAnalysis>(stream);
 *   // Dom tree is automatically computed on first access, cached on subsequent calls
 *   
 *   manager.invalidate_by_mask(AnalysisInvalidation::DominatorTree);
 *   // Next get_result() will recompute DOM tree only, not other analyses
 */
class DGTLAnalysisManager {
private:
    struct AnalysisCacheEntry {
        std::unique_ptr<DGTLAnalysis> result;
        uint64_t ir_version_at_compute;
    };

    std::unordered_map<AnalysisID, AnalysisCacheEntry> cache;
    mutable std::shared_mutex manager_mutex;
    uint64_t last_known_ir_version = 0;

public:
    DGTLAnalysisManager() noexcept = default;

    // Non-copyable
    DGTLAnalysisManager(const DGTLAnalysisManager&) = delete;
    DGTLAnalysisManager& operator=(const DGTLAnalysisManager&) = delete;

    // Move-constructible
    DGTLAnalysisManager(DGTLAnalysisManager&& other) noexcept {
        std::unique_lock lock(other.manager_mutex);
        cache = std::move(other.cache);
        last_known_ir_version = other.last_known_ir_version;
        other.last_known_ir_version = 0;
    }

    /**
     * @brief Get or compute the result of analysis AnalysisT.
     * Type-safe, automatically creates and caches result.
     * 
     * @tparam AnalysisT Derived class from DGTLAnalysis
     * @param stream The IR stream to analyze
     * @return Const reference to analysis result (valid for stream lifetime)
     */
    template <typename AnalysisT>
    [[nodiscard]] const AnalysisT& get_result(ir::IRStream& stream) {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");

        const uint64_t current_version = stream.get_version();
        const AnalysisID id = AnalysisTypeKey<AnalysisT>::get();

        {
            std::shared_lock read_lock(manager_mutex);
            if (current_version == last_known_ir_version) [[likely]] {
                auto it = cache.find(id);
                if (it != cache.end()) {
                    return *static_cast<const AnalysisT*>(it->second.result.get());
                }
            }
        }

        std::unique_lock write_lock(manager_mutex);

        if (current_version != last_known_ir_version) [[unlikely]] {
            cache.clear();
            last_known_ir_version = current_version;
        }

        auto it = cache.find(id);
        if (it != cache.end()) {
            return *static_cast<const AnalysisT*>(it->second.result.get());
        }

        auto result = std::make_unique<AnalysisT>();
        result->compute(stream);

        const AnalysisT& ref = *result;
        cache.emplace(id, AnalysisCacheEntry{ std::move(result), current_version });

        return ref;
    }

    /**
     * @brief Invalidate all cached analyses.
     * Called when IR undergoes major restructuring that invalidates all properties.
     */
    void invalidate_all() noexcept {
        std::unique_lock lock(manager_mutex);
        cache.clear();
        last_known_ir_version = 0;
    }

    /**
     * @brief Check if analysis AnalysisT is currently cached.
     * @tparam AnalysisT The analysis type to check
     * @return true if cached in memory
     */
    template <typename AnalysisT>
    [[nodiscard]] bool is_cached() const noexcept {
        std::shared_lock lock(manager_mutex);
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    /**
     * @brief Check if analysis AnalysisT is cached AND valid for current stream version.
     * @tparam AnalysisT The analysis type to check
     * @param stream The IR stream to validate against
     * @return true if cached and valid
     */
    template <typename AnalysisT>
    [[nodiscard]] bool is_cached_for(const ir::IRStream& stream) const noexcept {
        std::shared_lock lock(manager_mutex);
        if (stream.get_version() != last_known_ir_version) {
            return false;
        }
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    /**
     * @brief Invalidate a specific analysis by type.
     * @tparam AnalysisT The analysis type to evict
     */
    template <typename AnalysisT>
    void invalidate_specific() noexcept {
        std::unique_lock lock(manager_mutex);
        cache.erase(AnalysisTypeKey<AnalysisT>::get());
    }

    /**
     * @brief Invalidate analyses by bitmask.
     * Selectively evicts cache entries for the specified analyses.
     * @param mask Bitmask where bit 0..5 map to DomTree/LoopInfo/Liveness/Alias/MemDep/VN.
     */
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

        // Selective eviction: only remove entries specified in mask
        std::unique_lock lock(manager_mutex);
        if ((mask & kDominatorTree) != 0u) {
            cache.erase(AnalysisTypeKey<DominatorTreeAnalysisTag>::get());
        }
        if ((mask & kLoopInfo) != 0u) {
            cache.erase(AnalysisTypeKey<LoopInfoAnalysisTag>::get());
        }
        if ((mask & kLiveness) != 0u) {
            cache.erase(AnalysisTypeKey<LivenessAnalysisTag>::get());
        }
        if ((mask & kAliasAnalysis) != 0u) {
            cache.erase(AnalysisTypeKey<AliasAnalysisTag>::get());
        }
        if ((mask & kMemoryDependence) != 0u) {
            cache.erase(AnalysisTypeKey<MemoryDependenceAnalysisTag>::get());
        }
        if ((mask & kValueNumbering) != 0u) {
            cache.erase(AnalysisTypeKey<ValueNumberingAnalysisTag>::get());
        }
    }
};

} // namespace dgtl::pipeline
