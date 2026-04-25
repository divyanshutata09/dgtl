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

/* * DGTL Backend vNext - Pass Decision Engine
 * PILLAR 5: ADAPTIVE PASS SELECTION & PRESSURE DAMPENING
 * DESIGN: TAM-Synchronized / Arena-Optimized / Speculative-Aware
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string_view>
#include <vector>
#include "dgtl/graph/Region.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pipeline/Analysis_Manager.hpp"

namespace analysis = dgtl::analysis;

namespace dgtl::pipeline {

/**
 * @class PassDecisionEngine
 * @brief Decides which optimization passes to run based on region analysis.
 * Replaces static pipelines with intelligent, adaptive pass selection.
 * Backed by Titan-Stable cost metrics to ensure zero-variance execution.
 */
class PassDecisionEngine {
public:
    /**
     * @brief Determine if a specific pass should run on a given region.
     * @param pass The optimization pass to consider
     * @param region The region being optimized
     * @param am The Analysis Manager (TAM) for lazy fact retrieval
     * @param cost_model The AI cost model for predictions
     * @return true if the pass should be executed
     */
    bool should_run_pass(
        DGTLOptimizationPass* pass,
        const graph::Region* region,
        DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        if (!pass || !region) [[unlikely]] return false;
        
        // Threshold: 0.35 is the minimum delta for JIT/AOT profitability in DGTL 1.0.
        // Regions below this threshold are better left in their canonical form.
        const double predicted = predict_pass_benefit(pass, region, am, cost_model);
        return predicted >= 0.35;
    }

    /**
     * @brief Get the priority order for passes on a region.
     * @param region The region to optimize
     * @param available_passes All available passes
     * @param am The Analysis Manager (TAM)
     * @param cost_model The AI cost model
     * @return Ordered list of passes by predicted benefit
     */
    std::vector<DGTLOptimizationPass*> prioritize_passes(
        const graph::Region* region,
        const std::vector<DGTLOptimizationPass*>& available_passes,
        DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        std::vector<DGTLOptimizationPass*> ordered;
        if (!region) [[unlikely]] return ordered;
        
        ordered = available_passes;
        std::sort(ordered.begin(), ordered.end(), [&](DGTLOptimizationPass* a, DGTLOptimizationPass* b) {
            return predict_pass_benefit(a, region, am, cost_model) > 
                   predict_pass_benefit(b, region, am, cost_model);
        });
        
        return ordered;
    }

    /**
     * @brief Predict the benefit of running a pass on a region.
     * @param pass The pass to evaluate
     * @param region The target region
     * @param am The Analysis Manager (TAM)
     * @param cost_model The AI cost model
     * @return Estimated performance benefit score
     */
    double predict_pass_benefit(
        DGTLOptimizationPass* pass,
        const graph::Region* region,
        DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        if (!pass || !region) [[unlikely]] return -1.0;

        const RegionSignals sig = collect_region_signals(*region, am, cost_model);
        const PassTraits traits = classify_pass(pass);

        double score = 0.05; // Base exploration bias for Titan-Stable

        // --- AFFINITY SCALING (Pillar 2: Transactional weight) ---
        score += sig.hotness * affinity_weight(pass->hot_path_affinity()) * 0.45;
        score += sig.memory_pressure * affinity_weight(pass->memory_affinity()) * 0.35;
        score += sig.vector_pressure * affinity_weight(pass->vector_affinity()) * 0.35;
        score += sig.speculation_signal * affinity_weight(pass->speculation_affinity()) * 0.25;

        // --- SEMANTIC WEIGHTING ---
        score += sig.branch_pressure * traits.branch_weight;
        score += sig.vector_pressure * traits.vector_weight;
        score += sig.memory_pressure * traits.memory_weight;

        // --- PRODUCTION ARCHITECTURE GUARDS ---

        // Titan-Stable Guard: Favor deterministic behavior in high-control regions to prevent jitter
        if (sig.control_pressure > 0.25 && !pass->is_deterministic()) {
            score -= 0.35;
        }

        // PILLAR 2: Boost transactional passes when speculation is high
        if (sig.speculation_signal > 0.5 && pass->requires_transaction()) {
            score += 0.08;
        }

        // Memory-Saturated Dampening: Penalize aggressive SIMD in memory-choked code
        if (sig.memory_pressure > 0.65 && sig.vector_pressure < 0.2) {
            score -= 0.10;
        }

        // SIMD Hot-Path Boost: High vector density + low memory friction
        if (sig.vector_pressure > 0.55 && sig.memory_pressure < 0.4 && traits.vector_weight > 0.0) {
            score += 0.15;
        }

        // Speculative Guard: Boost branch optimization in hot speculative paths
        if (sig.branch_pressure > 0.50 && sig.speculation_signal > 0.35 && traits.branch_weight > 0.0) {
            score += 0.12;
        }

        return score;
    }

private:
    struct alignas(64) RegionSignals {
        double hotness = 0.0;
        double memory_pressure = 0.0;
        double vector_pressure = 0.0;
        double control_pressure = 0.0;
        double branch_pressure = 0.0;
        double speculation_signal = 0.0;
    };

    struct PassTraits {
        double vector_weight = 0.0;
        double memory_weight = 0.0;
        double branch_weight = 0.0;
    };

    double affinity_weight(PassAffinity affinity) const noexcept {
        switch (affinity) {
            case PassAffinity::None: return 0.0;
            case PassAffinity::Low: return 0.35;
            case PassAffinity::Medium: return 0.7;
            case PassAffinity::High: return 1.0;
            default: return 0.0;
        }
    }

    RegionSignals collect_region_signals(
        const graph::Region& region,
        [[maybe_unused]] DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        RegionSignals sig{};

        // Integrate TAM feedback: If AM is warm, use cached data
        const double predicted_cycles = static_cast<double>(cost_model.predict_region_cost(region));
        const double cycle_signal = std::clamp(predicted_cycles / 300.0, 0.0, 1.0);

        // PILLAR 3: Signal Collection (Zero-Latency Mapping)
        sig.hotness = region.is_hot ? 1.0 : std::clamp(region.estimated_cycles / 300.0, 0.0, 1.0);
        sig.memory_pressure = std::clamp((region.memory_bound ? 0.55 : 0.0) + region.memory_density() * 0.35 + region.cache_miss_rate * 0.25, 0.0, 1.0);
        sig.vector_pressure = std::clamp((region.vectorizable ? 0.5 : 0.0) + region.simd_density() * 0.5, 0.0, 1.0);
        sig.control_pressure = std::clamp(region.control_density() + region.live_range_pressure() * 0.2, 0.0, 1.0);
        sig.branch_pressure = std::clamp(region.control_density() * 0.75 + cycle_signal * 0.15 + (region.is_hot ? 0.10 : 0.0), 0.0, 1.0);
        sig.speculation_signal = std::clamp((region.speculative_candidate ? 0.55 : 0.0) + cycle_signal * 0.25, 0.0, 1.0);

        return sig;
    }

    [[nodiscard]] PassTraits classify_pass(const DGTLOptimizationPass* pass) const noexcept {
        PassTraits traits{};
        if (!pass) return traits;

        const std::string_view name(pass->get_pass_name());
        
        // Semantic Identification (Non-RTTI)
        if (contains_token(name, "SIMD") || contains_token(name, "Vector")) {
            traits.vector_weight += 0.12;
        }
        if (contains_token(name, "Memory") || contains_token(name, "Store") || contains_token(name, "Alias")) {
            traits.memory_weight += 0.12;
        }
        if (contains_token(name, "Predication") || contains_token(name, "Unswitch") || 
            contains_token(name, "Branch")) {
            traits.branch_weight += 0.14;
        }
        if (contains_token(name, "Fusion")) {
            traits.vector_weight += 0.04;
        }

        return traits;
    }

    [[nodiscard]] static bool contains_token(std::string_view value, std::string_view needle) noexcept {
        if (needle.empty() || value.size() < needle.size()) {
            return false;
        }

        auto lower_ascii = [](unsigned char ch) noexcept -> char {
            return static_cast<char>(std::tolower(ch));
        };

        for (size_t i = 0; i + needle.size() <= value.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                const char lhs = lower_ascii(static_cast<unsigned char>(value[i + j]));
                const char rhs = lower_ascii(static_cast<unsigned char>(needle[j]));
                if (lhs != rhs) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }

        return false;
    }
};

} // namespace dgtl::pipeline