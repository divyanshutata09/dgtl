/* * DGTL-MLIR-X v1.0 (DGTL Edition)
 * STEP 13: THE DGTL ANALYSIS MANAGER (TAM)
 * -------------------------------------------------------------------------
 * GOAL: Lazy computation and caching of IR properties.
 * SYNC: Pillar 2 (Transactions) - Invalidation on IR Mutation.
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Caching)
 */

#pragma once
#include <unordered_map>
#include <memory>
#include <string_view>
#include <cstdint>
#include <type_traits>
#include "dgtl/ir/base/Global_IR.hpp"

namespace dgtl::pipeline {

/**
 * @brief Zero-overhead Type ID Generator.
 * Replaces std::type_index to support -fno-rtti environments.
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
 * @brief Base concept for all analysis providers (DomTree, LoopInfo, etc.)
 */
class DGTLAnalysis {
public:
    virtual ~DGTLAnalysis() = default;
    [[nodiscard]] virtual std::string_view get_analysis_name() const noexcept = 0;
};

/**
 * @class DGTLAnalysisManager
 * @brief Caches and manages the lifecycle of IR analysis results.
 * Optimized for UCRT64 to ensure zero-latency fact retrieval in HFT pipelines.
 */
class DGTLAnalysisManager {
private:
    struct AnalysisCacheEntry {
        std::unique_ptr<DGTLAnalysis> result;
        uint64_t ir_version_at_compute;
    };

    // Using uintptr_t as key is faster than std::type_index/string hashing
    std::unordered_map<AnalysisID, AnalysisCacheEntry> cache;
    uint64_t last_known_ir_version = 0;

public:
    DGTLAnalysisManager() noexcept = default;

    // RAII: Strict ownership - copying an analysis manager is a performance sin
    DGTLAnalysisManager(const DGTLAnalysisManager&) = delete;
    DGTLAnalysisManager& operator=(const DGTLAnalysisManager&) = delete;

    DGTLAnalysisManager(DGTLAnalysisManager&&) noexcept = default;
    DGTLAnalysisManager& operator=(DGTLAnalysisManager&&) noexcept = default;

    /**
     * @brief Retrieves an analysis result. Computes it if missing or stale.
     * @tparam AnalysisT The Analysis class (e.g., DominatorTreeAnalysis)
     * @param stream The current IR stream to analyze.
     */
    template <typename AnalysisT>
    [[nodiscard]] const AnalysisT& get_result(ir::IRStream& stream) {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");

        // NOTE: Ensure IRStream has get_version() implemented in Global_IR.hpp
        const uint64_t current_version = stream.get_version();

        // 1. Version Gate: If IR changed, purge all stale facts immediately
        if (current_version != last_known_ir_version) [[unlikely]] {
            invalidate_all();
            last_known_ir_version = current_version;
        }

        const AnalysisID id = AnalysisTypeKey<AnalysisT>::get();
        
        // 2. Optimized Lookup: Use find() to avoid double-hashing
        auto it = cache.find(id);
        if (it != cache.end()) [[likely]] {
            return *static_cast<const AnalysisT*>(it->second.result.get());
        }

        // 3. Lazy Strike: Compute only when requested
        auto result = std::make_unique<AnalysisT>();
        
        // Ensure AnalysisT has a compute(ir::IRStream&) method
        result->compute(stream);
        
        const AnalysisT& ref = *result;
        cache.emplace(id, AnalysisCacheEntry{ std::move(result), current_version });
        
        return ref;
    }

    /**
     * @brief Total Invalidation. Used when a opt Manager transaction commits.
     */
    void invalidate_all() noexcept {
        cache.clear();
    }

    /**
     * @brief Check if an analysis is already warmed up.
     */
    template <typename AnalysisT>
    [[nodiscard]] bool is_cached() const noexcept {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");
        return cache.contains(AnalysisTypeKey<AnalysisT>::get());
    }

    /**
     * @brief Direct removal of a specific analysis if a opt knows it only 
     * invalidated one specific property (e.g., CFG preserved, but Liveness broken).
     */
    template <typename AnalysisT>
    void invalidate_specific() noexcept {
        static_assert(std::is_base_of_v<DGTLAnalysis, AnalysisT>,
                      "AnalysisT must derive from dgtl::pipeline::DGTLAnalysis");
        cache.erase(AnalysisTypeKey<AnalysisT>::get());
    }
};

} // namespace dgtl::pipeline


