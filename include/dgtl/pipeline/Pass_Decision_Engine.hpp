/* * DGTL Backend vNext - Pass Decision Engine
 * Adaptive pass selection based on region characteristics
 */

#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "dgtl/graph/Region.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "Pass_Manager.hpp"

namespace analysis = dgtl::analysis;

namespace dgtl::pipeline {

/**
 * @class PassDecisionEngine
 * @brief Decides which optimization passes to run based on region analysis.
 * Replaces static pipeline with intelligent, adaptive pass selection.
 */
class PassDecisionEngine {
public:
    /**
     * @brief Determine if a specific pass should run on a given region.
     * @param pass The optimization pass to consider
     * @param region The region being optimized
     * @param cost_model The AI cost model for predictions
     * @return true if the pass should be executed
     */
    bool should_run_pass(
        DGTLOptimizationPass* pass,
        const graph::Region* region,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        if (!pass || !region) return false;
        const double predicted = predict_pass_benefit(pass, region, cost_model);
        return predicted >= 0.35;
    }

    /**
     * @brief Get the priority order for passes on a region.
     * @param region The region to optimize
     * @param available_passes All available passes
     * @param cost_model The AI cost model
     * @return Ordered list of passes by priority
     */
    std::vector<DGTLOptimizationPass*> prioritize_passes(
        const graph::Region* region,
        const std::vector<DGTLOptimizationPass*>& available_passes,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        std::vector<DGTLOptimizationPass*> ordered;
        if (!region) return ordered;
        ordered = available_passes;
        std::sort(ordered.begin(), ordered.end(), [&](DGTLOptimizationPass* a, DGTLOptimizationPass* b) {
            return predict_pass_benefit(a, region, cost_model) > predict_pass_benefit(b, region, cost_model);
        });
        return ordered;
    }

    /**
     * @brief Predict the benefit of running a pass on a region.
     * @param pass The pass to evaluate
     * @param region The target region
     * @param cost_model The AI cost model
     * @return Estimated performance benefit (negative for regression)
     */
    double predict_pass_benefit(
        DGTLOptimizationPass* pass,
        const graph::Region* region,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        if (!pass || !region) return -1.0;

        const RegionSignals sig = collect_region_signals(*region, cost_model);

        double score = 0.05;
        score += sig.hotness * affinity_weight(pass->hot_path_affinity()) * 0.45;
        score += sig.memory_pressure * affinity_weight(pass->memory_affinity()) * 0.35;
        score += sig.vector_pressure * affinity_weight(pass->vector_affinity()) * 0.35;
        score += sig.speculation_signal * affinity_weight(pass->speculation_affinity()) * 0.25;

        // Favor deterministic behavior in high-control regions.
        if (sig.control_pressure > 0.25 && !pass->is_deterministic()) {
            score -= 0.35;
        }

        // Prefer transactional passes when speculation pressure is high.
        if (sig.speculation_signal > 0.5 && pass->requires_transaction()) {
            score += 0.08;
        }

        // Penalize over-aggressive transforms in memory-saturated code.
        if (sig.memory_pressure > 0.65 && sig.vector_pressure < 0.2) {
            score -= 0.05;
        }

        return score;
    }

private:
    struct RegionSignals {
        double hotness = 0.0;
        double memory_pressure = 0.0;
        double vector_pressure = 0.0;
        double control_pressure = 0.0;
        double speculation_signal = 0.0;
    };

    double affinity_weight(PassAffinity affinity) const {
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
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        RegionSignals sig{};

        const double predicted_cycles = static_cast<double>(cost_model.predict_region_cost(region));
        const double cycle_signal = std::clamp(predicted_cycles / 300.0, 0.0, 1.0);

        sig.hotness = region.is_hot ? 1.0 : std::max(cycle_signal, std::clamp(region.estimated_cycles / 300.0, 0.0, 1.0));
        sig.memory_pressure = std::clamp((region.memory_bound ? 0.55 : 0.0) + region.memory_density() * 0.35 + region.cache_miss_rate * 0.25, 0.0, 1.0);
        sig.vector_pressure = std::clamp((region.vectorizable ? 0.5 : 0.0) + region.simd_density() * 0.5, 0.0, 1.0);
        sig.control_pressure = std::clamp(region.control_density() + region.live_range_pressure() * 0.2, 0.0, 1.0);
        sig.speculation_signal = std::clamp((region.speculative_candidate ? 0.55 : 0.0) + (region.compute_bound ? 0.2 : 0.0) + cycle_signal * 0.25, 0.0, 1.0);

        return sig;
    }
};

} // namespace dgtl::pipeline

