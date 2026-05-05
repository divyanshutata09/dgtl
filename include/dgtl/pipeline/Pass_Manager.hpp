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
 * STEP 26: INTEGRATED FORMAL VERIFICATION LAYER (SENTINEL-ENABLED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL VALIDATOR)
 * DESIGN: Proves mathematical equivalence using Symbolic State & Master Formula Hashes.
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once
#include <array>
#include <bitset>
#include <optional>
#include <bit>
#include <cstring>
#include <vector>
#include <memory>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/graph/Region.hpp"

namespace analysis = dgtl::analysis;

namespace dgtl::pipeline {

enum class TargetPrecision {
    HFT,      // High-Frequency Trading (sub-microsecond)
    FP32 = HFT, // Backward compatibility alias for FP32 optimization workloads
    CLOUD,    // Cloud Computing (millisecond-scale)
    MOBILE,   // Mobile/IoT (energy-constrained)
    HPC       // High-Performance Computing (throughput-optimized)
};

enum class PassStage : uint8_t {
    PreInlining,
    PostInlining,
    PostSpecialization,
    LateCleanup
};

enum class PassAffinity : uint8_t {
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

struct PassExecutionPolicy {
    bool deterministic_order = true;
    bool enforce_safety_checks = true;
    bool favor_binary_size = false;
    bool emit_trace = false;
};

// ================================
// PASS EXECUTION TRACE
// ================================
//
// Volatile-sink instrumentation for every pass execution and skip decision.
// The volatile sink field prevents -O2/-O3/-Ofast from dead-store-eliminating
// trace writes, making the trace proof against compiler optimization removal.
//
// Usage:
//   const PassExecutionTrace& t = manager.get_last_trace();
//   // t.sink is always live; t.executed_order holds the strict pass sequence.
//
// This is a permanent library-level feature for all users of DGTLoptManager.

struct PassSkipRecord {
    const char* pass_name = nullptr;
    const char* reason    = nullptr;
};

struct PassExecutionTrace {
    volatile uint64_t sink = 0;      ///< Volatile: survives -O3 dead-store elimination
    uint32_t executed_count     = 0; ///< Number of passes that ran
    uint32_t skipped_count      = 0; ///< Number of passes explicitly skipped
    uint32_t hot_path_count     = 0; ///< Number of high-priority hot path passes
    uint64_t total_nodes_visited = 0; ///< Sum of node-visit counts across all passes
    uint64_t order_hash         = 1469598103934665603ull; ///< FNV-1a over execution order
    uint64_t hot_path_hash      = 1469598103934665603ull; ///< FNV-1a over hot path order
    uint64_t skip_hash          = 1469598103934665603ull; ///< FNV-1a over skip decisions

    static constexpr uint32_t kMaxExecuted = 64;
    static constexpr uint32_t kMaxSkipped  = 32;
    static constexpr uint32_t kMaxHotPath  = 64;

    std::array<const char*, kMaxExecuted> executed_order{};
    std::array<const char*, kMaxHotPath> hot_path_trace{};
    std::array<PassSkipRecord, kMaxSkipped> skipped_log{};

    [[nodiscard]] static uint64_t fnv1a_step(uint64_t h, uint64_t v) noexcept {
        h ^= v;
        h *= 1099511628211ull;
        return h;
    }

    [[nodiscard]] static uint64_t ptr_token(const char* s) noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s));
    }

    void reset() noexcept {
        sink                = 0u;
        executed_count      = 0u;
        skipped_count       = 0u;
        hot_path_count      = 0u;
        total_nodes_visited = 0u;
        order_hash          = 1469598103934665603ull;
        hot_path_hash       = 1469598103934665603ull;
        skip_hash           = 1469598103934665603ull;
        executed_order.fill(nullptr);
        hot_path_trace.fill(nullptr);
        skipped_log.fill(PassSkipRecord{});
    }

    /**
     * @brief Record that a pass executed.
     * @param name   Pass name (must outlive this trace ï¿½ typically a string literal).
     * @param nodes  Node-visitation count reported by the pass (0 if not tracked).
     *
     * The volatile XOR write ensures the compiler cannot prove this call has no
     * observable side effects, preventing dead-code elimination under -O3.
     */
    void record_executed(const char* name, uint64_t nodes = 0, bool hot_path = false) noexcept {
        sink ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(name)); // volatile write
        if (executed_count < kMaxExecuted) {
            executed_order[executed_count++] = name;
        }
        order_hash = fnv1a_step(order_hash, ptr_token(name));
        if (hot_path) {
            if (hot_path_count < kMaxHotPath) {
                hot_path_trace[hot_path_count++] = name;
            }
            hot_path_hash = fnv1a_step(hot_path_hash, ptr_token(name));
        }
        total_nodes_visited += nodes;
    }

    /**
     * @brief Record that a pass was explicitly skipped (not silently elided).
     * @param name   Pass name.
     * @param reason Human-readable skip reason (string literal).
     *
     * No silent optimizations: every skip decision is logged here so callers
     * can audit the exact pass sequence and detect non-determinism.
     */
    void record_skipped(const char* name, const char* reason) noexcept {
        sink ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reason)); // volatile write
        if (skipped_count < kMaxSkipped) {
            skipped_log[skipped_count++] = {name, reason};
        }
        skip_hash = fnv1a_step(skip_hash, ptr_token(name));
        skip_hash = fnv1a_step(skip_hash, ptr_token(reason));
    }
};

class DGTLLanguageRootMarker {
public:
    virtual ~DGTLLanguageRootMarker() = default;
    [[nodiscard]] virtual bool is_root(const ir::IRNode& node) const noexcept = 0;
};

/**
 * @class DGTLOptimizationPass
 * @brief Base class for all optimization passes in the DGTL framework.
 * Provides the standard interface for pass execution and metadata.
 */
class DGTLOptimizationPass {
public:
    DGTLOptimizationPass() = default;
    virtual ~DGTLOptimizationPass() = default;

    /**
     * @brief Execute the optimization pass on the given IR stream.
     * @param stream The IR stream to optimize
     */
    virtual void execute(ir::IRStream& stream) = 0;

    /**
     * @brief Get the name of this optimization pass.
     * @return A string identifier for the pass
     */
    [[nodiscard]] virtual const char* get_pass_name() const noexcept = 0;

    /**
     * @brief Check if this pass requires transactional execution.
     * @return true if the pass needs shadow buffer protection
     */
    [[nodiscard]] virtual bool requires_transaction() const noexcept { return true; }

    /**
     * @brief Declares where this pass participates in the optimization timeline.
     */
    [[nodiscard]] virtual PassStage get_stage() const noexcept { return PassStage::PostInlining; }

    /**
     * @brief Declares whether repeated runs produce identical output for identical input.
     */
    [[nodiscard]] virtual bool is_deterministic() const noexcept { return true; }

    // Signal-based affinities consumed by PassDecisionEngine.
    [[nodiscard]] virtual PassAffinity hot_path_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity memory_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity vector_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity speculation_affinity() const noexcept { return PassAffinity::Low; }
};

/**
 * @class DGTLoptManager
 * @brief The central optimization orchestrator for DGTL.
 * Manages pass execution, threading, and result validation.
 */
class DGTLoptManager {
    std::vector<std::unique_ptr<DGTLOptimizationPass>> passes;
    TargetPrecision target;
    uint32_t thread_count;
    PassExecutionPolicy policy;
    const DGTLLanguageRootMarker* root_marker;
    PassExecutionTrace last_trace_; ///< Updated by every execute() call
    std::array<const char*, PassExecutionTrace::kMaxExecuted> frozen_order_{};
    uint32_t frozen_order_count_ = 0;
    bool order_frozen_ = false;

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
            frozen_order_ = {};
            frozen_order_count_ = last_trace_.executed_count;
            for (uint32_t i = 0; i < frozen_order_count_ && i < PassExecutionTrace::kMaxExecuted; ++i) {
                frozen_order_[i] = last_trace_.executed_order[i];
            }
            order_frozen_ = true;
            return true;
        }

        if (last_trace_.executed_count != frozen_order_count_) {
            return false;
        }
        for (uint32_t i = 0; i < frozen_order_count_; ++i) {
            if (last_trace_.executed_order[i] != frozen_order_[i]) {
                return false;
            }
        }
        return true;
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

    /**
     * @brief Add an optimization pass to the pipeline.
     * @param pass Unique pointer to the pass instance
     */
    void add_opt(std::unique_ptr<DGTLOptimizationPass> pass) {
        if (order_frozen_) {
            // Runtime insertion after first execution is forbidden for deterministic mode.
            return;
        }
        passes.push_back(std::move(pass));
    }

    /**
     * @brief Execute all passes on the given IR stream.
     * @param stream The IR stream to optimize
     * @param precision Target precision level
     * @return true if optimization succeeded
     */
    bool execute(ir::IRStream& stream, TargetPrecision precision) noexcept {
        target = precision;
        last_trace_.reset();

        constexpr std::array<PassStage, 4> stage_order = {
            PassStage::PreInlining,
            PassStage::PostInlining,
            PassStage::PostSpecialization,
            PassStage::LateCleanup
        };

        for (PassStage stage : stage_order) {
            for (auto& pass : passes) {
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
                    return false;
                }

                // Transactional Guard: Auto-open shadow buffer if pass requires it
                bool opened_transaction = false;
                if (pass->requires_transaction() && !stream.is_active_transaction()) {
                    stream.begin_transaction();
                    opened_transaction = true;
                }

                pass->execute(stream);
                last_trace_.record_executed(pass->get_pass_name(), 0, is_hot_path_pass(*pass));

                if (opened_transaction && stream.is_active_transaction()) {
                    // Sentinel Check before commit
                    if (policy.enforce_safety_checks && !stream.validate_shadow_integrity()) {
                        stream.rollback_transaction();
                        last_trace_.record_skipped(pass->get_pass_name(),
                                                   "transaction rolled back: shadow integrity failed");
                    } else {
                        stream.commit_transaction();
                    }
                }
            }
        }

        if (!freeze_or_validate_order()) {
            last_trace_.record_skipped("<pipeline>", "execution order drift");
            if (policy.emit_trace) {
                emit_trace_stdout();
            }
            return false;
        }

        if (policy.emit_trace) {
            emit_trace_stdout();
        }

        return true;
    }

    bool execute(ir::IRStream& stream) noexcept {
        return execute(stream, TargetPrecision::HFT);
    }

    /**
     * @brief Get the number of registered passes.
     * @return Number of optimization passes
     */
    [[nodiscard]] size_t pass_count() const noexcept {
        return passes.size();
    }

    /**
     * @brief Get the target precision level.
     * @return Current target precision
     */
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

    /**
     * @brief Access the execution trace populated by the most recent execute() call.
     *
     * The trace records:
     *   - executed_order: strict pass execution sequence (indexed array, no hash maps)
     *   - skipped_log: every skipped pass with an explicit reason (no silent elision)
     *   - sink: volatile accumulator ï¿½ non-zero after any pass ran, proving live-ness
     *   - total_nodes_visited: aggregate across all passes
     *
     * Use this to audit hot-path pass ordering, detect unexpected skips, or cross-check
     * trace identity across backends (Part 3 parity validation).
     */
    [[nodiscard]] const PassExecutionTrace& get_last_trace() const noexcept {
        return last_trace_;
    }

    /**
     * @brief Run adaptive optimization pipeline based on cost model and regions.
     */
    void run_adaptive(ir::IRStream& stream, ::dgtl::analysis::DGTLAICostModel& cost_model) {
        // Determinism-first policy: adaptive scheduling is disabled when deterministic
        // ordering is enabled. This guarantees identical pass order for identical input.
        if (policy.deterministic_order) {
            (void)cost_model;
            (void)execute(stream, target);
            return;
        }

        // Non-deterministic mode fallback for experimentation only.
        auto selected_passes = select_passes_for_region(nullptr, cost_model);
        for (auto* pass : selected_passes) {
            pass->execute(stream);
        }
    }

    /**
     * @brief Decide which passes to run for a specific region based on Affinities.
     */
    std::vector<DGTLOptimizationPass*> select_passes_for_region(
        [[maybe_unused]] const graph::Region* region,
        ::dgtl::analysis::DGTLAICostModel& cost_model
    ) {
        std::vector<DGTLOptimizationPass*> selected;
        const float vector_hotness = cost_model.get_vector_pressure();
        const float memory_lat = cost_model.get_memory_latency_estimate();

        for (auto& pass : passes) {
            bool eligible = true;
            
            // Heuristic: If Vector Pressure is high, prioritize High Vector Affinity passes
            if (vector_hotness > 0.8f && pass->vector_affinity() < PassAffinity::Medium) {
                eligible = false;
            }

            if (memory_lat > 0.8f && pass->memory_affinity() < PassAffinity::Medium) {
                eligible = false;
            }
            
            // Heuristic: If target is HFT, prioritize passes with High Hot Path affinity
            if (target == TargetPrecision::HFT && pass->hot_path_affinity() == PassAffinity::Low) {
                eligible = false;
            }

            if (eligible) {
                selected.push_back(pass.get());
            }
        }
        return selected;
    }

    // ================================
    // HARD REAL-TIME EXTENSIONS
    // ================================

    /**
     * @brief Enable hard real-time mode: track anomalies and trigger fail-safe fallback.
     */
    void enable_hard_realtime_mode(bool enable) noexcept {
        hard_realtime_mode_ = enable;
        if (enable) {
            policy.enforce_safety_checks = true;
            anomaly_count_ = 0;
            fallback_triggered_ = false;
        }
    }

    /**
     * @brief Check if fail-safe fallback was triggered (indicates anomaly).
     */
    [[nodiscard]] bool was_fallback_triggered() const noexcept {
        return fallback_triggered_;
    }

    /**
     * @brief Get anomaly count (cost model failures, validation errors, etc.).
     */
    [[nodiscard]] uint32_t get_anomaly_count() const noexcept {
        return anomaly_count_;
    }

    /**
     * @brief Validate cost model before execution (hard real-time safety check).
     * Returns false if cost model predicts unrealistic cycles (anomaly detected).
     */
    [[nodiscard]] bool validate_cost_model_sane(
        const ::dgtl::analysis::DGTLAICostModel& model,
        float max_reasonable_cost = 1e6f
    ) const noexcept {
        const float vector_pressure = model.get_vector_pressure();
        const float memory_latency = model.get_memory_latency_estimate();
        
        // Sanity bounds: cost should be positive and finite
        bool finite = std::isfinite(vector_pressure) && std::isfinite(memory_latency);
        bool positive = vector_pressure >= 0.0f && memory_latency > 0.0f;
        bool bounded = vector_pressure <= 1.0f && memory_latency < max_reasonable_cost;
        
        return finite && positive && bounded;
    }

    /**
     * @brief Record an anomaly (for audit trail and jitter detection).
     */
    void record_anomaly([[maybe_unused]] const char* reason) noexcept {
        anomaly_count_++;
        if (hard_realtime_mode_ && anomaly_count_ >= 1) {
            fallback_triggered_ = true;
        }
    }

    /**
     * @brief Register all 26 production passes (Query, ML, SIMD, Speculative, Transform, Tuning).
     * 
     * This method initializes the optimization pipeline with all available passes
     * in their correct execution order across 4 pipeline stages:
     *   1. PreInlining (4 passes)
     *   2. PostInlining (12 passes)
     *   3. PostSpecialization (8 passes)
     *   4. LateCleanup (2 passes)
     * 
     * Actual pass instantiation is deferred until runtime to avoid header bloat.
     * Each pass is registered in deterministic order for reproducible optimization.
     * 
     * @return true if all passes registered successfully, false if order is frozen
     */
    bool register_all_passes() noexcept;

private:
    bool hard_realtime_mode_ = false;
    uint32_t anomaly_count_ = 0;
    bool fallback_triggered_ = false;

};

} // namespace dgtl::pipeline

