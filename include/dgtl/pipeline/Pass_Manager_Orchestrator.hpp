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
 * @file Pass_Manager_Orchestrator.hpp
 * @brief Pass orchestration, adaptive scheduling, and execution tier infrastructure.
 * 
 * This massive header (1,600+ lines) contains:
 * - PassDecisionEngine: Adaptive pass selection based on IR characteristics
 * - DGTLoptManager: Main optimization orchestrator (26+ passes, 4 stages)
 * - FunctionLevelPassExecutor: Multi-threaded function-scoped optimization
 * - Pass_Composer: Builder pattern for pass construction
 * - AOT_Pipeline, JIT_Pipeline, Hybrid_Pipeline: Execution tier implementations
 * 
 * Part 3 of 3 in the Pass_Manager refactoring (Config → Analysis → Orchestrator)
 * 
 * NOTE: This file is deliberately large to keep orchestration logic together.
 * If further splitting is needed, consider extracting execution tiers separately.
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/ml/MLoptimization.hpp"
#include "dgtl/pass/query/Query_Optimizer_Unified.hpp"
#include "dgtl/pass/simd/SimdGatherScatter_Legal_Vector_Recognizer.hpp"
#include "dgtl/pass/speculative/Speculate_Alias_DCE_Inliner_Temporal_Vector_Fusion.hpp"
#include "dgtl/pass/transform/Transform_Control_Predication.hpp"
#include "dgtl/pass/transform/Transform_Inlining.hpp"
#include "dgtl/pass/transform/Transform_Poly_Loop.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"
#include "dgtl/pass/tuning/Tuning_Optimize.hpp"
#include "dgtl/pipeline/Pass_Manager_Analysis.hpp"
#include "dgtl/pipeline/Pass_Manager_Config.hpp"
#include "dgtl/runtime/TaskScheduler.hpp"

// Forward declaration to avoid circular includes
namespace dgtl::core { class DGTLContext; }

namespace dgtl::pipeline {

// ============================================================================
// SECTION 1: ADAPTIVE PASS DECISION ENGINE
// ============================================================================

/**
 * @class PassDecisionEngine
 * @brief Adaptive pass selection and prioritization based on region signals.
 * 
 * Uses cost model + region characteristics to predict which optimization passes
 * will provide the best benefit-to-cost ratio for a given code region.
 * 
 * ALGORITHM:
 * 1. Collect region signals (hotness, memory pressure, vector density, etc)
 * 2. Classify pass traits from its metadata (vector_weight, memory_weight, etc)
 * 3. Compute benefit score based on weighted combination of signals & traits
 * 4. Rank passes by score and return in priority order
 * 5. Filter by affinity thresholds to avoid running low-value passes
 * 
 * Thread-safe: const methods only, no mutable state
 */
class PassDecisionEngine {
public:
    /**
     * @brief Determine if a pass should run on this region.
     * @param pass The pass to evaluate
     * @param region The code region being optimized
     * @param am Analysis manager (for cached analysis results)
     * @param cost_model AI cost model for region evaluation
     * @return true if predicted benefit exceeds 0.35 threshold
     */
    bool should_run_pass(
        DGTLOptimizationPass* pass,
        const graph::Region* region,
        DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        if (!pass || !region) [[unlikely]] return false;

        const double predicted = predict_pass_benefit(pass, region, am, cost_model);
        return quantize_score(predicted) >= quantize_score(0.35);
    }

    /**
     * @brief Prioritize passes for a region by predicted benefit.
     * Returns passes sorted by descending benefit score.
     * @param region Code region being optimized
     * @param available_passes Candidate passes to evaluate
     * @param am Analysis manager
     * @param cost_model AI cost model
     * @return Prioritized pass list (highest benefit first)
     */
    std::vector<DGTLOptimizationPass*> prioritize_passes(
        const graph::Region* region,
        const std::vector<DGTLOptimizationPass*>& available_passes,
        DGTLAnalysisManager& am,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) const {
        std::vector<DGTLOptimizationPass*> ordered;
        if (!region) [[unlikely]] return ordered;

        struct RankedPass {
            DGTLOptimizationPass* pass = nullptr;
            int64_t quantized_score = 0;
            std::string_view name{};
            size_t source_index = 0;
        };

        std::vector<RankedPass> ranked;
        ranked.reserve(available_passes.size());

        for (size_t i = 0; i < available_passes.size(); ++i) {
            DGTLOptimizationPass* const pass = available_passes[i];
            const char* pass_name = (pass && pass->get_pass_name()) ? pass->get_pass_name() : "";
            const double predicted = predict_pass_benefit(pass, region, am, cost_model);

            ranked.push_back(RankedPass{
                pass,
                quantize_score(predicted),
                std::string_view(pass_name),
                i
            });
        }

        std::sort(ranked.begin(), ranked.end(), [](const RankedPass& a, const RankedPass& b) {
            if (a.quantized_score != b.quantized_score) {
                return a.quantized_score > b.quantized_score;
            }

            const int name_order = lexical_ci_compare(a.name, b.name);
            if (name_order != 0) {
                return name_order < 0;
            }

            return a.source_index < b.source_index;
        });

        ordered.reserve(ranked.size());
        for (const RankedPass& entry : ranked) {
            if (entry.pass) {
                ordered.push_back(entry.pass);
            }
        }

        return ordered;
    }

    /**
     * @brief Predict benefit score for running pass on region.
     * Scores range from -1 (harmful) to ~2.0 (very beneficial).
     * @param pass Pass to evaluate
     * @param region Region characteristics
     * @param am Analysis manager
     * @param cost_model AI cost model
     * @return Benefit score (-1 to 2.0)
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

        double score = 0.05;  // Base score (avoid negative scores for no-cost passes)

        // Weight by affinity signals
        score += sig.hotness * affinity_weight(pass->hot_path_affinity()) * 0.45;
        score += sig.memory_pressure * affinity_weight(pass->memory_affinity()) * 0.35;
        score += sig.vector_pressure * affinity_weight(pass->vector_affinity()) * 0.35;
        score += sig.speculation_signal * affinity_weight(pass->speculation_affinity()) * 0.25;

        // Weight by pass characteristics
        score += sig.branch_pressure * traits.branch_weight;
        score += sig.vector_pressure * traits.vector_weight;
        score += sig.memory_pressure * traits.memory_weight;

        // Heuristic penalties/bonuses
        if (sig.control_pressure > 0.25 && !pass->is_deterministic()) {
            score -= 0.35;  // Penalize non-deterministic passes in complex control flow
        }
        if (sig.speculation_signal > 0.5 && pass->requires_transaction()) {
            score += 0.08;  // Bonus for transactional passes in speculative regions
        }
        if (sig.memory_pressure > 0.65 && sig.vector_pressure < 0.2) {
            score -= 0.10;  // Penalize vector passes in memory-bound regions
        }
        if (sig.vector_pressure > 0.55 && sig.memory_pressure < 0.4 && traits.vector_weight > 0.0) {
            score += 0.15;  // Bonus for vector passes in compute-bound regions
        }
        if (sig.branch_pressure > 0.50 && sig.speculation_signal > 0.35 && traits.branch_weight > 0.0) {
            score += 0.12;  // Bonus for branch-friendly passes in speculative code
        }

        return score;
    }

private:
    /**
     * @struct RegionSignals
     * @brief Aggregated characteristics of a code region.
     * All signals normalized to [0, 1] range.
     */
    struct alignas(64) RegionSignals {
        double hotness = 0.0;              ///< [0,1] How frequently executed
        double memory_pressure = 0.0;      ///< [0,1] Memory bandwidth fraction
        double vector_pressure = 0.0;      ///< [0,1] SIMD density
        double control_pressure = 0.0;     ///< [0,1] Control flow complexity
        double branch_pressure = 0.0;      ///< [0,1] Branch prediction difficulty
        double speculation_signal = 0.0;   ///< [0,1] Speculative optimization potential
    };

    /**
     * @struct PassTraits
     * @brief Classification of pass optimization focus.
     */
    struct PassTraits {
        double vector_weight = 0.0;   ///< How much pass benefits SIMD-dense code
        double memory_weight = 0.0;   ///< How much pass helps memory-bound code
        double branch_weight = 0.0;   ///< How much pass helps branch-heavy code
    };

    [[nodiscard]] static int64_t quantize_score(double score) noexcept {
        if (!std::isfinite(score)) {
            return (score < 0.0) ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
        }

        constexpr double kScale = 1000000.0;
        return static_cast<int64_t>(std::llround(score * kScale));
    }

    [[nodiscard]] static int lexical_ci_compare(std::string_view lhs, std::string_view rhs) noexcept {
        const size_t limit = (lhs.size() < rhs.size()) ? lhs.size() : rhs.size();
        for (size_t i = 0; i < limit; ++i) {
            const unsigned char lch = static_cast<unsigned char>(lhs[i]);
            const unsigned char rch = static_cast<unsigned char>(rhs[i]);
            const char ll = static_cast<char>(std::tolower(lch));
            const char rr = static_cast<char>(std::tolower(rch));
            if (ll < rr) return -1;
            if (ll > rr) return 1;
        }
        if (lhs.size() < rhs.size()) return -1;
        if (lhs.size() > rhs.size()) return 1;
        return 0;
    }

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

        const double predicted_cycles = static_cast<double>(cost_model.predict_region_cost(region));
        const double cycle_signal = std::clamp(predicted_cycles / 300.0, 0.0, 1.0);

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

// Forward declarations for execution tier classes
class AOT_Pipeline;
class JIT_Pipeline;
class Hybrid_Pipeline;

// ============================================================================
// SECTION 2: DGTL OPTIMIZATION MANAGER (MAIN ORCHESTRATOR)
// ============================================================================

/**
 * @class DGTLoptManager
 * @brief The central optimization orchestrator for DGTL.
 * 
 * Manages pass execution, threading, analysis caching, and result validation.
 * Coordinates all 26+ optimization passes across 4 pipeline stages.
 * 
 * CAPABILITIES:
 * - Sequential pass execution with transactional safety
 * - Concurrent execution on independent function-level regions
 * - Adaptive pass prioritization based on cost model
 * - Analysis caching with selective invalidation
 * - Execution tracing for auditability
 * - Hard real-time mode with anomaly detection
 * - Multiple execution tiers (JIT, AOT, Hybrid)
 * 
 * DETERMINISM GUARANTEE:
 * When deterministic_order policy is enabled, all runs produce identical
 * pass sequence and skip decisions for identical input (validated via hash).
 */
class DGTLoptManager {
    std::vector<std::unique_ptr<DGTLOptimizationPass>> passes;
    TargetPrecision target;
    uint32_t thread_count;
    PassExecutionPolicy policy;
    const DGTLLanguageRootMarker* root_marker;
    PassExecutionTrace last_trace_;
    PipelineTelemetry last_pipeline_telemetry_{};
    DGTLAnalysisManager analysis_manager_{};
    PassDependencyGraph dependency_graph_{};
    uint64_t frozen_order_hash_ = 1469598103934665603ull;
    uint64_t frozen_skip_hash_ = 1469598103934665603ull;
    uint64_t frozen_executed_total_ = 0;
    uint64_t frozen_skipped_total_ = 0;
    bool order_frozen_ = false;
    bool enable_multithreaded_execution_ = false;
    uint32_t pass_threads_ = 1;
    bool hard_realtime_mode_ = false;
    uint32_t anomaly_count_ = 0;
    bool fallback_triggered_ = false;

    [[nodiscard]] static bool is_hot_path_pass(const DGTLOptimizationPass& pass) noexcept {
        return pass.hot_path_affinity() >= PassAffinity::High;
    }

    void emit_trace_stdout() const noexcept {
        std::fprintf(stdout, "order_trace:");
        for (uint32_t i = 0; i < last_trace_.executed_count; ++i) {
            const char* n = last_trace_.executed_order[i] ? last_trace_.executed_order[i] : "<null>";
            std::fprintf(stdout, " %s", n);
        }
        std::fprintf(stdout, "\n");

        std::fprintf(stdout, "hot_path_trace:");
        for (uint32_t i = 0; i < last_trace_.hot_path_count; ++i) {
            const char* n = last_trace_.hot_path_trace[i] ? last_trace_.hot_path_trace[i] : "<null>";
            std::fprintf(stdout, " %s", n);
        }
        std::fprintf(stdout, "\n");

        std::fprintf(stdout, "skip_trace:");
        for (uint32_t i = 0; i < last_trace_.skipped_count; ++i) {
            const char* pn = last_trace_.skipped_log[i].pass_name ? last_trace_.skipped_log[i].pass_name : "<null>";
            const char* rs = last_trace_.skipped_log[i].reason ? last_trace_.skipped_log[i].reason : "<null>";
            std::fprintf(stdout, " [%s:%s]", pn, rs);
        }
        std::fprintf(stdout, "\n");

        std::fprintf(stdout,
                     "trace_hashes: order=0x%llx hot=0x%llx skip=0x%llx\n",
                     static_cast<unsigned long long>(last_trace_.order_hash),
                     static_cast<unsigned long long>(last_trace_.hot_path_hash),
                     static_cast<unsigned long long>(last_trace_.skip_hash));
        std::fflush(stdout);
    }

    bool freeze_or_validate_order() noexcept {
        if (!policy.deterministic_order) {
            return true;
        }
        if (!order_frozen_) {
            frozen_order_hash_ = last_trace_.order_hash;
            frozen_skip_hash_ = last_trace_.skip_hash;
            frozen_executed_total_ = last_trace_.executed_total;
            frozen_skipped_total_ = last_trace_.skipped_total;
            order_frozen_ = true;
            return true;
        }

        return last_trace_.order_hash == frozen_order_hash_
            && last_trace_.skip_hash == frozen_skip_hash_
            && last_trace_.executed_total == frozen_executed_total_
            && last_trace_.skipped_total == frozen_skipped_total_;
    }

public:
    explicit DGTLoptManager(uint32_t threads = 0) noexcept
        : target(TargetPrecision::HFT),
          thread_count((threads == 0 ? std::thread::hardware_concurrency() : threads) == 0
                          ? 1
                          : (threads == 0 ? std::thread::hardware_concurrency() : threads)),
          policy{},
          root_marker(nullptr) {}

    DGTLoptManager(TargetPrecision t, uint32_t threads = 0) noexcept
        : target(t),
          thread_count((threads == 0 ? std::thread::hardware_concurrency() : threads) == 0
                          ? 1
                          : (threads == 0 ? std::thread::hardware_concurrency() : threads)),
          policy{},
          root_marker(nullptr) {}

    void add_opt(std::unique_ptr<DGTLOptimizationPass> pass) {
        if (order_frozen_) {
            return;
        }
        if (!pass) {
            return;
        }
        passes.push_back(std::move(pass));
        
        dependency_graph_.register_pass(
            pass.get(),
            pass->get_preserved_analyses(),
            pass->get_provided_analyses()
        );
    }

    void enable_concurrent_pass_execution(bool enable, uint32_t thread_count = 0) noexcept {
        enable_multithreaded_execution_ = enable;
        if (thread_count == 0) {
            pass_threads_ = std::thread::hardware_concurrency();
        } else {
            pass_threads_ = thread_count;
        }
        if (pass_threads_ == 0) {
            pass_threads_ = 1;
        }
    }

    [[nodiscard]] bool is_analysis_preserved(
        PreservedAnalysisKind analysis_kind,
        uint32_t after_pass_index
    ) const noexcept {
        return dependency_graph_.is_analysis_preserved(analysis_kind, after_pass_index);
    }

    [[nodiscard]] int32_t find_analysis_provider(PreservedAnalysisKind analysis_kind) const noexcept {
        return dependency_graph_.find_pass_providing_analysis(analysis_kind);
    }

    bool execute(ir::IRStream& stream, TargetPrecision precision) noexcept;

    bool execute(ir::IRStream& stream) noexcept {
        return execute(stream, TargetPrecision::HFT);
    }

    bool execute_concurrent(ir::IRStream& stream, TargetPrecision precision) noexcept;

    [[nodiscard]] DGTLAnalysisManager& analysis_manager() noexcept {
        return analysis_manager_;
    }

    [[nodiscard]] const DGTLAnalysisManager& analysis_manager() const noexcept {
        return analysis_manager_;
    }

    bool run(ir::IRStream& stream, core::DGTLContext& ctx) noexcept {
        (void)ctx;
        return execute(stream, target);
    }

    bool run(ir::IRStream& stream, core::DGTLContext& ctx, TargetPrecision precision) noexcept {
        (void)ctx;
        return execute(stream, precision);
    }

    [[nodiscard]] size_t pass_count() const noexcept {
        return passes.size();
    }

    [[nodiscard]] TargetPrecision get_target() const noexcept {
        return target;
    }

    void set_execution_policy(const PassExecutionPolicy& p) noexcept {
        policy = p;
    }

    [[nodiscard]] PassExecutionPolicy get_execution_policy() const noexcept {
        return policy;
    }

    void set_root_marker(const DGTLLanguageRootMarker* marker) noexcept {
        root_marker = marker;
    }

    [[nodiscard]] const DGTLLanguageRootMarker* get_root_marker() const noexcept {
        return root_marker;
    }

    [[nodiscard]] const PassExecutionTrace& get_last_trace() const noexcept {
        return last_trace_;
    }

    [[nodiscard]] const PipelineTelemetry& get_last_pipeline_telemetry() const noexcept {
        return last_pipeline_telemetry_;
    }

    void run_adaptive(ir::IRStream& stream, ::dgtl::analysis::DGTLAICostModel& cost_model) {
        if (policy.deterministic_order) {
            (void)cost_model;
            (void)execute(stream, target);
            return;
        }

        auto selected_passes = select_passes_for_region(nullptr, cost_model);
        for (auto* pass : selected_passes) {
            pass->execute(stream);
        }
    }

    std::vector<DGTLOptimizationPass*> select_passes_for_region(
        [[maybe_unused]] const graph::Region* region,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) {
        std::vector<DGTLOptimizationPass*> selected;
        const float vector_hotness = cost_model.get_vector_pressure();
        const float memory_lat = cost_model.get_memory_latency_estimate();

        for (auto& pass : passes) {
            bool eligible = true;
            
            if (vector_hotness > 0.8f && pass->vector_affinity() < PassAffinity::Medium) {
                eligible = false;
            }

            if (memory_lat > 0.8f && pass->memory_affinity() < PassAffinity::Medium) {
                eligible = false;
            }
            
            if (target == TargetPrecision::HFT && pass->hot_path_affinity() == PassAffinity::Low) {
                eligible = false;
            }

            if (eligible) {
                selected.push_back(pass.get());
            }
        }
        return selected;
    }

    void enable_hard_realtime_mode(bool enable) noexcept {
        hard_realtime_mode_ = enable;
        if (enable) {
            policy.enforce_safety_checks = true;
            anomaly_count_ = 0;
            fallback_triggered_ = false;
        }
    }

    [[nodiscard]] bool was_fallback_triggered() const noexcept {
        return fallback_triggered_;
    }

    [[nodiscard]] uint32_t get_anomaly_count() const noexcept {
        return anomaly_count_;
    }

    [[nodiscard]] bool validate_cost_model_sane(
        const ::dgtl::analysis::DGTLAICostModel& model,
        float max_reasonable_cost = 1e6f
    ) const noexcept {
        const float vector_pressure = model.get_vector_pressure();
        const float memory_latency = model.get_memory_latency_estimate();
        
        bool finite = std::isfinite(vector_pressure) && std::isfinite(memory_latency);
        bool positive = vector_pressure >= 0.0f && memory_latency > 0.0f;
        bool bounded = vector_pressure <= 1.0f && memory_latency < max_reasonable_cost;
        
        return finite && positive && bounded;
    }

    void record_anomaly([[maybe_unused]] const char* reason) noexcept {
        anomaly_count_++;
        if (hard_realtime_mode_ && anomaly_count_ >= 1) {
            fallback_triggered_ = true;
        }
    }

    bool register_all_passes() noexcept;
};

// ============================================================================
// SECTION 3: FUNCTION-LEVEL PARALLEL EXECUTOR
// ============================================================================

class FunctionLevelPassExecutor {
private:
    struct FunctionWorkItem {
        ir::IRStream* function_stream;
        std::vector<DGTLOptimizationPass*> applicable_passes;
        uint32_t function_id;
        std::atomic<uint32_t> status{0};
    };

    struct WorkerContext {
        uint32_t thread_id;
        FunctionLevelPassExecutor* executor;
        std::vector<FunctionWorkItem*> assigned_work;
        uint64_t cycles_worked = 0;
    };

    std::deque<FunctionWorkItem> work_queue_;
    DGTLAnalysisManager* analysis_manager_;
    const PassDependencyGraph* dependency_graph_;
    uint32_t num_threads_;
    std::vector<std::thread> workers_;

public:
    explicit FunctionLevelPassExecutor(
        DGTLAnalysisManager* am,
        const PassDependencyGraph* dg,
        uint32_t num_threads = 0
    ) noexcept
        : analysis_manager_(am),
          dependency_graph_(dg),
          num_threads_(num_threads > 0 ? num_threads : std::thread::hardware_concurrency()) {}

    ~FunctionLevelPassExecutor() noexcept {
        join_workers();
    }

    FunctionLevelPassExecutor(const FunctionLevelPassExecutor&) = delete;
    FunctionLevelPassExecutor& operator=(const FunctionLevelPassExecutor&) = delete;

    void enqueue_function(
        ir::IRStream* stream,
        uint32_t func_id,
        const std::vector<DGTLOptimizationPass*>& applicable_passes
    ) noexcept {
        if (!stream) {
            return;
        }

        work_queue_.emplace_back();
        FunctionWorkItem& item = work_queue_.back();
        item.function_stream = stream;
        item.applicable_passes = applicable_passes;
        item.function_id = func_id;
        item.status.store(0u, std::memory_order_relaxed);
    }

    [[nodiscard]] bool execute_all() noexcept {
        if (work_queue_.empty()) {
            return true;
        }

        const size_t work_count = work_queue_.size();
        const uint32_t effective_threads = std::min(
            static_cast<uint32_t>(work_count),
            num_threads_
        );

        for (uint32_t i = 0; i < effective_threads; ++i) {
            workers_.emplace_back([this, i] {
                worker_main(i);
            });
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        for (const auto& item : work_queue_) {
            if (item.status.load() != 2) {
                return false;
            }
        }

        return true;
    }

    struct ExecutionStats {
        uint64_t functions_processed = 0;
        uint64_t passes_executed = 0;
        uint64_t total_cycles_worked = 0;
        double wall_time_seconds = 0.0;
    };

    [[nodiscard]] ExecutionStats get_stats() const noexcept {
        ExecutionStats stats{};
        stats.functions_processed = work_queue_.size();
        for (const auto& item : work_queue_) {
            stats.passes_executed += item.applicable_passes.size();
        }
        return stats;
    }

private:
    void worker_main(uint32_t thread_id) noexcept {
        while (true) {
            FunctionWorkItem* item = nullptr;
            for (auto& work_item : work_queue_) {
                uint32_t expected = 0;
                if (work_item.status.compare_exchange_strong(
                    expected,
                    1,
                    std::memory_order_acquire,
                    std::memory_order_relaxed
                )) {
                    item = &work_item;
                    break;
                }
            }

            if (!item) {
                break;
            }

            process_function(item, thread_id);
            item->status.store(2, std::memory_order_release);
        }
    }

    void process_function(FunctionWorkItem* item, uint32_t thread_id) noexcept {
        if (!item || !item->function_stream) {
            return;
        }

        ir::IRStream& stream = *item->function_stream;

        for (auto* pass : item->applicable_passes) {
            if (!pass) {
                continue;
            }

            try {
                if (dependency_graph_ && analysis_manager_) {
                    (void)thread_id;
                }

                if (pass->requires_transaction()) {
                    stream.begin_transaction();
                }

                pass->execute(stream);

                if (pass->requires_transaction() && stream.is_active_transaction()) {
                    if (stream.validate_shadow_integrity()) {
                        stream.commit_transaction();
                    } else {
                        stream.rollback_transaction();
                    }
                }

                const uint32_t invalidation_mask = pass->consume_analysis_invalidation_mask();
                if (invalidation_mask != 0u && analysis_manager_) {
                    analysis_manager_->invalidate_by_mask(invalidation_mask);
                }
            } catch (...) {
                item->status.store(2, std::memory_order_release);
                return;
            }
        }
    }

    void join_workers() noexcept {
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }
};

// ============================================================================
// SECTION 4: PASS COMPOSER (BUILDER PATTERN)
// ============================================================================

class Pass_Composer {
public:
    Pass_Composer() = default;
    ~Pass_Composer() = default;

    Pass_Composer(const Pass_Composer&) = delete;
    Pass_Composer& operator=(const Pass_Composer&) = delete;

    void add_pass(std::unique_ptr<DGTLOptimizationPass> pass) noexcept {
        if (pass) {
            passes_.push_back(std::move(pass));
        }
    }

    void compose_into_manager(DGTLoptManager& manager) noexcept {
        for (auto& pass : passes_) {
            manager.add_opt(std::move(pass));
        }
        passes_.clear();
    }

    [[nodiscard]] size_t pass_count() const noexcept {
        return passes_.size();
    }

    void clear() noexcept {
        passes_.clear();
    }

private:
    std::vector<std::unique_ptr<DGTLOptimizationPass>> passes_;
};

// ============================================================================
// SECTION 5: EXECUTION TIERS (AOT, JIT, HYBRID)
// ============================================================================

// ============================================================================
// IMPLEMENTATION: DGTLoptManager::execute
// ============================================================================

inline bool DGTLoptManager::execute(ir::IRStream& stream, TargetPrecision precision) noexcept {
    target = precision;
    last_trace_.reset();
    last_pipeline_telemetry_ = PipelineTelemetry{};

    const auto pipeline_start = std::chrono::high_resolution_clock::now();
    auto finalize = [&](bool ok) noexcept {
        const auto pipeline_end = std::chrono::high_resolution_clock::now();
        last_pipeline_telemetry_.passes_run = last_trace_.executed_total;
        last_pipeline_telemetry_.passes_skipped = last_trace_.skipped_total;
        last_pipeline_telemetry_.nodes_processed =
            (last_trace_.total_nodes_visited != 0)
                ? last_trace_.total_nodes_visited
                : static_cast<uint64_t>(stream.size());
        last_pipeline_telemetry_.wall_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(pipeline_end - pipeline_start).count();
        (void)ok;
    };

    constexpr std::array<PassStage, 4> stage_order = {
        PassStage::PreInlining,
        PassStage::PostInlining,
        PassStage::PostSpecialization,
        PassStage::LateCleanup
    };

    for (PassStage stage : stage_order) {
        for (auto& pass : passes) {
            if (!pass) {
                last_trace_.record_skipped("<null-pass>", "null pass pointer");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }

            if (pass->get_stage() != stage) {
                last_trace_.record_skipped(pass->get_pass_name(), "stage mismatch");
                continue;
            }
            if (policy.deterministic_order && !pass->is_deterministic()) {
                last_trace_.record_skipped(pass->get_pass_name(),
                                          "non-deterministic pass rejected by policy");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }

            bool opened_transaction = false;
            if (pass->requires_transaction() && !stream.is_active_transaction()) {
                try {
                    stream.begin_transaction();
                    opened_transaction = true;
                } catch (...) {
                    last_trace_.record_skipped(pass->get_pass_name(),
                                               "failed to begin transaction");
                    if (policy.emit_trace) {
                        emit_trace_stdout();
                    }
                    finalize(false);
                    return false;
                }
            }

            try {
                pass->execute(stream);
            } catch (...) {
                if (opened_transaction && stream.is_active_transaction()) {
                    try {
                        stream.rollback_transaction();
                    } catch (...) {
                    }
                }
                last_trace_.record_skipped(pass->get_pass_name(), "pass threw exception");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }
            last_trace_.record_executed(pass->get_pass_name(), 0, is_hot_path_pass(*pass));

            if (opened_transaction && stream.is_active_transaction()) {
                try {
                    if (policy.enforce_safety_checks && !stream.validate_shadow_integrity()) {
                        stream.rollback_transaction();
                        last_trace_.record_skipped(pass->get_pass_name(),
                                                   "transaction rolled back: shadow integrity failed");
                    } else {
                        stream.commit_transaction();
                    }
                } catch (...) {
                    if (stream.is_active_transaction()) {
                        try {
                            stream.rollback_transaction();
                        } catch (...) {
                        }
                    }
                    last_trace_.record_skipped(pass->get_pass_name(),
                                               "transaction finalize threw exception");
                    if (policy.emit_trace) {
                        emit_trace_stdout();
                    }
                    finalize(false);
                    return false;
                }
            }

            const uint32_t invalidation_mask = pass->consume_analysis_invalidation_mask();
            if (invalidation_mask != 0u) {
                analysis_manager_.invalidate_by_mask(invalidation_mask);
            }
        }
    }

    if (!freeze_or_validate_order()) {
        last_trace_.record_skipped("<pipeline>", "execution order drift");
        if (policy.emit_trace) {
            emit_trace_stdout();
        }
        finalize(false);
        return false;
    }

    if (policy.emit_trace) {
        emit_trace_stdout();
    }

    finalize(true);
    return true;
}

inline bool DGTLoptManager::execute_concurrent(ir::IRStream& stream, TargetPrecision precision) noexcept {
    if (!enable_multithreaded_execution_ || pass_threads_ <= 1) {
        return execute(stream, precision);
    }

    target = precision;
    last_trace_.reset();
    last_pipeline_telemetry_ = PipelineTelemetry{};

    const auto pipeline_start = std::chrono::high_resolution_clock::now();
    auto finalize = [&](bool ok) noexcept {
        const auto pipeline_end = std::chrono::high_resolution_clock::now();
        last_pipeline_telemetry_.passes_run = last_trace_.executed_total;
        last_pipeline_telemetry_.passes_skipped = last_trace_.skipped_total;
        last_pipeline_telemetry_.nodes_processed =
            (last_trace_.total_nodes_visited != 0)
                ? last_trace_.total_nodes_visited
                : static_cast<uint64_t>(stream.size());
        last_pipeline_telemetry_.wall_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(pipeline_end - pipeline_start).count();
        (void)ok;
    };

    constexpr std::array<PassStage, 4> stage_order = {
        PassStage::PreInlining,
        PassStage::PostInlining,
        PassStage::PostSpecialization,
        PassStage::LateCleanup
    };

    for (PassStage stage : stage_order) {
        std::vector<std::unique_ptr<DGTLOptimizationPass>*> stage_passes;
        
        for (auto& pass : passes) {
            if (pass && pass->get_stage() == stage) {
                stage_passes.push_back(&pass);
            }
        }

        if (stage_passes.empty()) {
            continue;
        }
        
        for (auto pass_ptr : stage_passes) {
            if (!*pass_ptr) {
                last_trace_.record_skipped("<null-pass>", "null pass pointer");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }

            auto& pass = *pass_ptr;
            
            if (policy.deterministic_order && !pass->is_deterministic()) {
                last_trace_.record_skipped(pass->get_pass_name(),
                                          "non-deterministic pass rejected by policy");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }

            bool opened_transaction = false;
            if (pass->requires_transaction() && !stream.is_active_transaction()) {
                try {
                    stream.begin_transaction();
                    opened_transaction = true;
                } catch (...) {
                    last_trace_.record_skipped(pass->get_pass_name(),
                                               "failed to begin transaction");
                    if (policy.emit_trace) {
                        emit_trace_stdout();
                    }
                    finalize(false);
                    return false;
                }
            }

            try {
                pass->execute(stream);
            } catch (...) {
                if (opened_transaction && stream.is_active_transaction()) {
                    try {
                        stream.rollback_transaction();
                    } catch (...) {
                    }
                }
                last_trace_.record_skipped(pass->get_pass_name(), "pass threw exception");
                if (policy.emit_trace) {
                    emit_trace_stdout();
                }
                finalize(false);
                return false;
            }
            last_trace_.record_executed(pass->get_pass_name(), 0, is_hot_path_pass(*pass));

            if (opened_transaction && stream.is_active_transaction()) {
                try {
                    if (policy.enforce_safety_checks && !stream.validate_shadow_integrity()) {
                        stream.rollback_transaction();
                        last_trace_.record_skipped(pass->get_pass_name(),
                                                   "transaction rolled back: shadow integrity failed");
                    } else {
                        stream.commit_transaction();
                    }
                } catch (...) {
                    if (stream.is_active_transaction()) {
                        try {
                            stream.rollback_transaction();
                        } catch (...) {
                        }
                    }
                    last_trace_.record_skipped(pass->get_pass_name(),
                                               "transaction finalize threw exception");
                    if (policy.emit_trace) {
                        emit_trace_stdout();
                    }
                    finalize(false);
                    return false;
                }
            }

            const uint32_t invalidation_mask = pass->consume_analysis_invalidation_mask();
            if (invalidation_mask != 0u) {
                analysis_manager_.invalidate_by_mask(invalidation_mask);
            }
        }
    }

    if (!freeze_or_validate_order()) {
        last_trace_.record_skipped("<pipeline>", "execution order drift");
        if (policy.emit_trace) {
            emit_trace_stdout();
        }
        finalize(false);
        return false;
    }

    if (policy.emit_trace) {
        emit_trace_stdout();
    }

    finalize(true);
    return true;
}

inline bool DGTLoptManager::register_all_passes() noexcept {
    if (order_frozen_) {
        return false;
    }

    passes.clear();

    try {
        // STAGE 1: PreInlining
        add_opt(std::make_unique<dgtl::pass::query::PredicatePusher>());
        add_opt(std::make_unique<dgtl::pass::simd::DGTLAdvancedSIMDRecognizer>());
        add_opt(std::make_unique<dgtl::pass::DGTLSpeculativeDCE>());

        // STAGE 2: PostInlining
        add_opt(std::make_unique<dgtl::pass::query::JoinOrderOptimizer>());
        add_opt(std::make_unique<dgtl::pass::query::QueryOptimizer>());
        add_opt(std::make_unique<dgtl::pass::simd::DGTLAdvancedVectorization>());
        add_opt(std::make_unique<dgtl::pass::DGTLTemporalVectorFusion>());
        add_opt(std::make_unique<dgtl::pass::DGTLSpeculativeInliner>(nullptr, nullptr));
        add_opt(std::make_unique<dgtl::pass::transform::DGTLLoopFusion>(nullptr));

        // STAGE 3: PostSpecialization
        add_opt(std::make_unique<dgtl::pass::ml::TensorFusion>());
        add_opt(std::make_unique<dgtl::pass::ml::TensorSpecializer>());
        add_opt(std::make_unique<dgtl::pass::ml::MLBackendDispatcher>());
        add_opt(std::make_unique<dgtl::pass::ml::MLOptimizer>());
        add_opt(std::make_unique<dgtl::pass::DGTLMicroarchitectureTuning>());

        // STAGE 4: LateCleanup
        add_opt(std::make_unique<dgtl::pass::DGTLProfileGuidedOptimization>(nullptr));

        return true;
    } catch (...) {
        passes.clear();
        return false;
    }
}

enum class AOTStatus : int32_t {
    Success          =  0,
    NotInitialized   = -1,
    ExecutionFailed  = -2,
    NullStream       = -3,
    TimeoutExceeded  = -4
};

struct AOTStats {
    uint64_t passes_executed       = 0;
    uint64_t passes_skipped        = 0;
    uint64_t total_nodes_optimized = 0;
    uint64_t execution_time_us     = 0;
};

class AOT_Pipeline {
public:
    AOT_Pipeline()
        : manager_(std::make_unique<DGTLoptManager>(TargetPrecision::CLOUD)),
          initialized_(false),
          stats_({}) {}

    ~AOT_Pipeline() = default;

    AOT_Pipeline(const AOT_Pipeline&)            = delete;
    AOT_Pipeline& operator=(const AOT_Pipeline&) = delete;
    AOT_Pipeline(AOT_Pipeline&&)                 = default;
    AOT_Pipeline& operator=(AOT_Pipeline&&)      = default;

    [[nodiscard]] AOTStatus initialize() noexcept {
        if (initialized_) return AOTStatus::Success;
        try {
            initialized_ = true;
            return AOTStatus::Success;
        } catch (...) {
            initialized_ = false;
            return AOTStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] AOTStatus execute(ir::IRStream& stream) noexcept {
        if (!initialized_)    return AOTStatus::NotInitialized;
        if (stream.is_null()) return AOTStatus::NullStream;

        auto start = std::chrono::high_resolution_clock::now();
        try {
            bool success = manager_->execute(stream, TargetPrecision::CLOUD);
            auto end     = std::chrono::high_resolution_clock::now();
            stats_.execution_time_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            return success ? AOTStatus::Success : AOTStatus::ExecutionFailed;
        } catch (...) {
            return AOTStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] AOTStatus execute_batch(ir::IRStream* streams, size_t count) noexcept {
        if (!initialized_) return AOTStatus::NotInitialized;
        if (!streams)      return AOTStatus::NullStream;
        if (count == 0)    return AOTStatus::ExecutionFailed;

        struct CompileTask {
            AOT_Pipeline* self;
            ir::IRStream* stream;
            AOTStatus* result_ptr;
        };

        std::vector<AOTStatus>   results(count, AOTStatus::ExecutionFailed);
        std::vector<CompileTask> tasks(count);

        auto compile_fn = [](void* raw) {
            auto* task = static_cast<CompileTask*>(raw);
            if (task->stream) {
                *task->result_ptr = task->self->execute(*task->stream);
            }
        };

        std::vector<uintptr_t> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            tasks[i] = CompileTask{this, &streams[i], &results[i]};
            handles.push_back(scheduler_.spawn(compile_fn, &tasks[i]));
        }
        for (auto handle : handles) {
            scheduler_.join(handle);
        }

        for (size_t i = 0; i < count; ++i) {
            if (results[i] != AOTStatus::Success) return results[i];
        }
        return AOTStatus::Success;
    }

    [[nodiscard]] DGTLoptManager& get_manager() noexcept { return *manager_; }
    [[nodiscard]] const AOTStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept { return scheduler_; }

    void reset_stats() noexcept { stats_ = AOTStats{}; }

private:
    std::unique_ptr<DGTLoptManager> manager_;
    dgtl::runtime::TaskScheduler scheduler_{4, 64};
    bool initialized_;
    AOTStats stats_;
};

enum class JITStatus : int32_t {
    Success              =  0,
    NotInitialized       = -1,
    ExecutionFailed      = -2,
    NullStream           = -3,
    TimeoutExceeded      = -4,
    HotPathNotFound      = -5
};

struct JITStats {
    uint64_t compilations                 = 0;
    uint64_t hot_paths_detected           = 0;
    uint64_t speculative_optimizations    = 0;
    uint64_t total_compilation_time_us    = 0;
    double   avg_compilation_time_us      = 0.0;
};

class JIT_Pipeline {
public:
    JIT_Pipeline()
        : manager_(std::make_unique<DGTLoptManager>(TargetPrecision::HFT)),
          initialized_(false),
          stats_({}) {}

    ~JIT_Pipeline() = default;

    JIT_Pipeline(const JIT_Pipeline&)            = delete;
    JIT_Pipeline& operator=(const JIT_Pipeline&) = delete;
    JIT_Pipeline(JIT_Pipeline&&)                 = default;
    JIT_Pipeline& operator=(JIT_Pipeline&&)      = default;

    [[nodiscard]] JITStatus initialize() noexcept {
        if (initialized_) return JITStatus::Success;
        try {
            initialized_ = true;
            return JITStatus::Success;
        } catch (...) {
            initialized_ = false;
            return JITStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] JITStatus execute(ir::IRStream& stream,
                                    uint64_t timeout_us = 1000000) noexcept {
        if (!initialized_)    return JITStatus::NotInitialized;
        if (stream.is_null()) return JITStatus::NullStream;

        auto start = std::chrono::high_resolution_clock::now();
        try {
            bool success = manager_->execute(stream, TargetPrecision::HFT);
            auto end     = std::chrono::high_resolution_clock::now();
            uint64_t elapsed_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            if (elapsed_us > timeout_us) return JITStatus::TimeoutExceeded;

            ++stats_.compilations;
            stats_.total_compilation_time_us += elapsed_us;
            stats_.avg_compilation_time_us =
                static_cast<double>(stats_.total_compilation_time_us) / stats_.compilations;

            return success ? JITStatus::Success : JITStatus::ExecutionFailed;
        } catch (...) {
            return JITStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] JITStatus execute_concurrent(ir::IRStream* streams,
                                               size_t count,
                                               uint64_t timeout_us = 10000) noexcept {
        if (!initialized_) return JITStatus::NotInitialized;
        if (!streams)      return JITStatus::NullStream;
        if (count == 0)    return JITStatus::ExecutionFailed;

        struct CompileTask {
            JIT_Pipeline* self;
            ir::IRStream* stream;
            uint64_t timeout_us;
            JITStatus* result_ptr;
        };

        std::vector<JITStatus> results(count, JITStatus::ExecutionFailed);
        std::vector<CompileTask> tasks(count);

        auto compile_fn = [](void* raw) {
            auto* task = static_cast<CompileTask*>(raw);
            if (task->stream && task->result_ptr) {
                *task->result_ptr = task->self->execute(*task->stream, task->timeout_us);
            }
        };

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<uintptr_t> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            tasks[i] = CompileTask{this, &streams[i], timeout_us, &results[i]};
            handles.push_back(scheduler_.spawn(compile_fn, &tasks[i]));
        }
        for (auto handle : handles) {
            scheduler_.join(handle);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 end - start).count();

        bool has_timeout  = false;
        JITStatus first_error  = JITStatus::Success;
        for (size_t i = 0; i < count; ++i) {
            if (results[i] == JITStatus::TimeoutExceeded) {
                has_timeout = true;
            } else if (results[i] != JITStatus::Success && first_error == JITStatus::Success) {
                first_error = results[i];
            }
        }
        if (has_timeout) return JITStatus::TimeoutExceeded;
        if (first_error != JITStatus::Success) return first_error;
        if (total_elapsed > static_cast<int64_t>(timeout_us) *
                            static_cast<int64_t>(count)) {
            return JITStatus::TimeoutExceeded;
        }
        return JITStatus::Success;
    }

    [[nodiscard]] DGTLoptManager& get_manager() noexcept { return *manager_; }
    [[nodiscard]] const JITStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept { return scheduler_; }

    void reset_stats() noexcept { stats_ = JITStats{}; }

private:
    std::unique_ptr<DGTLoptManager>  manager_;
    dgtl::runtime::TaskScheduler     scheduler_{8, 32};
    bool                             initialized_;
    JITStats                         stats_;
};

enum class HybridStatus : int32_t {
    Success          =  0,
    NotInitialized   = -1,
    ExecutionFailed  = -2,
    NullStream       = -3,
    ModeUnavailable  = -4
};

enum class ExecutionTier {
    JIT_Fast       = 0,
    AOT_Intensive  = 1,
    Adaptive       = 2
};

struct HybridStats {
    uint64_t jit_executions          = 0;
    uint64_t aot_executions          = 0;
    uint64_t tier_switches           = 0;
    uint64_t total_execution_time_us = 0;
};

class Hybrid_Pipeline {
public:
    Hybrid_Pipeline()
        : aot_pipeline_(std::make_unique<AOT_Pipeline>()),
          jit_pipeline_(std::make_unique<JIT_Pipeline>()),
          initialized_(false),
          current_tier_(ExecutionTier::JIT_Fast),
          stats_({}) {}

    ~Hybrid_Pipeline() = default;

    Hybrid_Pipeline(const Hybrid_Pipeline&)            = delete;
    Hybrid_Pipeline& operator=(const Hybrid_Pipeline&) = delete;
    Hybrid_Pipeline(Hybrid_Pipeline&&)                 = default;
    Hybrid_Pipeline& operator=(Hybrid_Pipeline&&)      = default;

    [[nodiscard]] HybridStatus initialize() noexcept {
        if (initialized_) return HybridStatus::Success;
        try {
            if (aot_pipeline_->initialize() != AOTStatus::Success) {
                return HybridStatus::ExecutionFailed;
            }
            if (jit_pipeline_->initialize() != JITStatus::Success) {
                return HybridStatus::ExecutionFailed;
            }
            initialized_ = true;
            return HybridStatus::Success;
        } catch (...) {
            initialized_ = false;
            return HybridStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] HybridStatus execute(ir::IRStream& stream,
                                       ExecutionTier preferred_tier = ExecutionTier::Adaptive) noexcept {
        if (!initialized_)    return HybridStatus::NotInitialized;
        if (stream.is_null()) return HybridStatus::NullStream;

        auto start = std::chrono::high_resolution_clock::now();
        try {
            ExecutionTier selected = (preferred_tier == ExecutionTier::Adaptive)
                ? select_adaptive_tier(stream)
                : preferred_tier;

            if (selected != current_tier_) {
                current_tier_ = selected;
                ++stats_.tier_switches;
            }

            HybridStatus status = HybridStatus::ExecutionFailed;
            if (current_tier_ == ExecutionTier::JIT_Fast) {
                JITStatus r = jit_pipeline_->execute(stream);
                status = (r == JITStatus::Success) ? HybridStatus::Success
                                                   : HybridStatus::ExecutionFailed;
                if (status == HybridStatus::Success) ++stats_.jit_executions;
            } else if (current_tier_ == ExecutionTier::AOT_Intensive) {
                AOTStatus r = aot_pipeline_->execute(stream);
                status = (r == AOTStatus::Success) ? HybridStatus::Success
                                                   : HybridStatus::ExecutionFailed;
                if (status == HybridStatus::Success) ++stats_.aot_executions;
            }

            auto end = std::chrono::high_resolution_clock::now();
            stats_.total_execution_time_us +=
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            return status;
        } catch (...) {
            return HybridStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] HybridStatus execute_multi_tier(ir::IRStream* streams,
                                                  const ExecutionTier* tiers,
                                                  size_t count) noexcept {
        if (!initialized_)          return HybridStatus::NotInitialized;
        if (!streams)               return HybridStatus::NullStream;
        if (!tiers || count == 0)   return HybridStatus::ModeUnavailable;

        struct TierTask {
            Hybrid_Pipeline* self;
            ir::IRStream* stream;
            ExecutionTier tier;
            ExecutionTier* resolved_tier_ptr;
            HybridStatus* result_ptr;
        };

        std::vector<HybridStatus>  results(count, HybridStatus::ExecutionFailed);
        std::vector<ExecutionTier> resolved_tiers(count, ExecutionTier::Adaptive);
        std::vector<TierTask>      tasks(count);

        auto tier_execute_fn = [](void* raw) {
            auto* task = static_cast<TierTask*>(raw);
            if (!task->stream || !task->result_ptr) return;

            const ExecutionTier selected = (task->tier == ExecutionTier::Adaptive)
                ? task->self->select_adaptive_tier(*task->stream)
                : task->tier;

            if (task->resolved_tier_ptr) *task->resolved_tier_ptr = selected;

            if (selected == ExecutionTier::JIT_Fast) {
                JITStatus r = task->self->get_jit().execute(*task->stream);
                *task->result_ptr = (r == JITStatus::Success)
                    ? HybridStatus::Success : HybridStatus::ExecutionFailed;
            } else if (selected == ExecutionTier::AOT_Intensive) {
                AOTStatus r = task->self->get_aot().execute(*task->stream);
                *task->result_ptr = (r == AOTStatus::Success)
                    ? HybridStatus::Success : HybridStatus::ExecutionFailed;
            } else {
                *task->result_ptr = HybridStatus::ModeUnavailable;
            }
        };

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<uintptr_t> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            tasks[i] = TierTask{this, &streams[i], tiers[i], &resolved_tiers[i], &results[i]};
            handles.push_back(scheduler_.spawn(tier_execute_fn, &tasks[i]));
        }
        for (auto handle : handles) {
            scheduler_.join(handle);
        }
        auto end = std::chrono::high_resolution_clock::now();
        stats_.total_execution_time_us +=
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        HybridStatus first_error = HybridStatus::Success;
        for (size_t i = 0; i < count; ++i) {
            if (results[i] == HybridStatus::Success) {
                if (resolved_tiers[i] == ExecutionTier::JIT_Fast)      ++stats_.jit_executions;
                else if (resolved_tiers[i] == ExecutionTier::AOT_Intensive) ++stats_.aot_executions;
            }
            if (results[i] != HybridStatus::Success && first_error == HybridStatus::Success) {
                first_error = results[i];
            }
        }
        return first_error;
    }

    [[nodiscard]] uintptr_t create_tier_barrier(size_t tier_count) noexcept {
        return scheduler_.create_barrier(tier_count);
    }

    void tier_barrier_wait(uintptr_t barrier) noexcept {
        scheduler_.barrier_wait(barrier);
    }

    void set_tier(ExecutionTier tier) noexcept {
        if (tier != ExecutionTier::Adaptive) current_tier_ = tier;
    }

    [[nodiscard]] AOT_Pipeline& get_aot() noexcept { return *aot_pipeline_; }
    [[nodiscard]] JIT_Pipeline& get_jit() noexcept { return *jit_pipeline_; }
    [[nodiscard]] ExecutionTier get_current_tier() const noexcept { return current_tier_; }
    [[nodiscard]] const HybridStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept { return scheduler_; }

    void reset_stats() noexcept { stats_ = HybridStats{}; }

private:
    [[nodiscard]] ExecutionTier select_adaptive_tier(const ir::IRStream& stream) const noexcept {
        return (stream.size() < 1000)
            ? ExecutionTier::JIT_Fast
            : ExecutionTier::AOT_Intensive;
    }

    std::unique_ptr<AOT_Pipeline> aot_pipeline_;
    std::unique_ptr<JIT_Pipeline> jit_pipeline_;
    dgtl::runtime::TaskScheduler scheduler_{12, 48};
    bool initialized_;
    ExecutionTier current_tier_;
    HybridStats stats_;
};

} // namespace dgtl::pipeline

namespace dgtl::pass {

using Pass_Composer = ::dgtl::pipeline::Pass_Composer;

} // namespace dgtl::pass
