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

        constexpr std::array<PassStage, 4> stage_order = {
            PassStage::PreInlining,
            PassStage::PostInlining,
            PassStage::PostSpecialization,
            PassStage::LateCleanup
        };

        for (PassStage stage : stage_order) {
            for (auto& pass : passes) {
                if (pass->get_stage() != stage) {
                    continue;
                }
                if (policy.deterministic_order && !pass->is_deterministic()) {
                    return false;
                }
                
                // Transactional Guard: Auto-open shadow buffer if pass requires it
                bool opened_transaction = false;
                if (pass->requires_transaction() && !stream.is_active_transaction()) {
                    stream.begin_transaction();
                    opened_transaction = true;
                }

                pass->execute(stream);

                if (opened_transaction && stream.is_active_transaction()) {
                    // Sentinel Check before commit
                    if (policy.enforce_safety_checks && !stream.validate_shadow_integrity()) {
                        stream.rollback_transaction();
                    } else {
                        stream.commit_transaction();
                    }
                }
            }
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
     * @brief Run adaptive optimization pipeline based on cost model and regions.
     */
    void run_adaptive(ir::IRStream& stream, ::dgtl::analysis::DGTLAICostModel& cost_model) {
        // Implementation: Iterates through regions and applies specific pass vectors
        // based on cost-model thresholds.
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
};

} // namespace dgtl::pipeline
