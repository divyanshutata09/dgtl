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
 * STEP 13: THE DGTL ANALYSIS MANAGER (TAM)
 * -------------------------------------------------------------------------
 * GOAL: Lazy computation and caching of IR properties.
 * SYNC: Pillar 2 (Transactions) - Invalidation on IR Mutation.
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once
#include <unordered_map>
#include <memory>
#include <string_view>
#include <cstdint>
#include <type_traits>
#include <shared_mutex>
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::pipeline {

/**
 * @brief Zero-overhead Type ID Generator.
 * Replaces std::type_index to support -fno-rtti environments and microsecond-scale lookups.
 */
using AnalysisID = uintptr_t;

template <typename T>
struct AnalysisTypeKey {
    static inline char dummy;
    static AnalysisID get() noexcept {
        return reinterpret_cast<AnalysisID>(&dummy);
    }
};

/**
 * @class DGTLAnalysis
 * @brief Base concept for all analysis providers (DomTree, LoopInfo, AliasAnalysis).
 * Every analysis in the DGTL pipeline must implement this interface.
 */
class DGTLAnalysis {
public:
    virtual ~DGTLAnalysis() = default;
    
    /**
     * @brief Perform the actual IR analysis.
     * @param stream The current IR stream to analyze.
     */
    virtual void compute(ir::IRStream& stream) = 0;

    [[nodiscard]] virtual std::string_view get_analysis_name() const noexcept = 0;
};

/**
 * @class DGTLAnalysisManager
 * @brief Caches and manages the lifecycle of IR analysis results.
 * Optimized for UCRT64 to ensure zero-latency fact retrieval in HFT pipelines.
 * Synchronized with the Pillar 2 Transactional IR system.
 */
class DGTLAnalysisManager {
private:
    struct AnalysisCacheEntry {
        std::unique_ptr<DGTLAnalysis> result;
        uint64_t ir_version_at_compute;
    };

    // Fast lookup via pointer-identity keys
    std::unordered_map<AnalysisID, AnalysisCacheEntry> cache;
    mutable std::shared_mutex manager_mutex;
    uint64_t last_known_ir_version = 0;

public:
    DGTLAnalysisManager() noexcept = default;

    // RAII Guard: Analysis results are tied to specific pipeline contexts
    DGTLAnalysisManager(const DGTLAnalysisManager&) = delete;
    DGTLAnalysisManager& operator=(const DGTLAnalysisManager&) = delete;

    DGTLAnalysisManager(DGTLAnalysisManager&& other) noexcept {
        std::unique_lock lock(other.manager_mutex);
        cache = std::move(other.cache);
        last_known_ir_version = other.last_known_ir_version;
        other.last_known_ir_version = 0;
    }

    /**
     * @brief Retrieves an analysis result. Computes it if missing or stale.
     * Uses a double-checked locking pattern for high-performance concurrent access.
     * @tparam AnalysisT The Analysis class (e.g., DominatorTreeAnalysis)
     * @param stream The current IR stream to analyze.
     */
    template <typename AnalysisT>
    [[nodiscard]] const AnalysisT& get_result(ir::IRStream& stream) {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");

        const uint64_t current_version = stream.get_version();
        const AnalysisID id = AnalysisTypeKey<AnalysisT>::get();

        {
            // Shared Read Access
            std::shared_lock read_lock(manager_mutex);
            if (current_version == last_known_ir_version) [[likely]] {
                auto it = cache.find(id);
                if (it != cache.end()) {
                    return *static_cast<const AnalysisT*>(it->second.result.get());
                }
            }
        }

        // Exclusive Write Access (Mutation or Stale Cache)
        std::unique_lock write_lock(manager_mutex);

        // 1. Version Gate: If IR version incremented, previous facts are mathematically void
        if (current_version != last_known_ir_version) [[unlikely]] {
            cache.clear();
            last_known_ir_version = current_version;
        }

        // 2. Re-check presence after acquiring write lock
        auto it = cache.find(id);
        if (it != cache.end()) {
            return *static_cast<const AnalysisT*>(it->second.result.get());
        }

        // 3. Lazy Strike: Construct and compute the analysis
        auto result = std::make_unique<AnalysisT>();
        result->compute(stream);
        
        const AnalysisT& ref = *result;
        cache.emplace(id, AnalysisCacheEntry{ std::move(result), current_version });
        
        return ref;
    }

    /**
     * @brief Total Invalidation. 
     * Mandatory call during DGTLoptManager commit cycles to maintain Pillar 3 integrity.
     */
    void invalidate_all() noexcept {
        std::unique_lock lock(manager_mutex);
        cache.clear();
        last_known_ir_version = 0;
    }

    /**
     * @brief Check if an analysis is already warmed up for the current IR state.
     */
    template <typename AnalysisT>
    [[nodiscard]] bool is_cached() const noexcept {
        std::shared_lock lock(manager_mutex);
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    /**
     * @brief Version-aware cache check for a specific stream snapshot.
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
     * @brief Targeted removal of a specific analysis.
     * Used when a pass preserves specific properties (e.g., CFG) but invalidates others (e.g., Liveness).
     */
    template <typename AnalysisT>
    void invalidate_specific() noexcept {
        std::unique_lock lock(manager_mutex);
        cache.erase(AnalysisTypeKey<AnalysisT>::get());
    }
};

} // namespace dgtl::pipeline
