/* * DGTL-MLIR-X v1.0 (DGTL Edition)
 * STEP 12: THE DGTL PASS MANAGER (TPM)
 * -------------------------------------------------------------------------
 * PILLAR 2: TRANSACTIONAL ORCHESTRATION (Atomic IR Mutation)
 * PILLAR 3: THE FINAL GATE (Formal Invariant Enforcement)
 * INTEGRATION: DGTL ANALYSIS CACHING (TAM)
 * STANDARDS: C++26 / UCRT64 (Zero-Exception, High-Frequency Trading Grade)
 */

#pragma once
#include <vector>
#include <memory>
#include <utility>
#include <string_view>
#include <chrono>
#include <expected> // C++26: Functional error handling to kill exception overhead

// Core Architectural Includes
#include "../core/Arena.hpp"
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"
#include "Analysis_Manager.hpp"

namespace dgtl::pipeline {

/**
 * @class DGTLOptimizationPass
 * @brief Abstract base for all high-performance optimization strikes.
 */
class DGTLOptimizationPass {
public:
    virtual ~DGTLOptimizationPass() = default;

    /**
     * @brief Core strike logic.
     * @param stream The IR stream to mutate.
     * @param am The Analysis Manager for requesting cached IR facts.
     */
    virtual void execute(ir::IRStream& stream, DGTLAnalysisManager& am) = 0;

    [[nodiscard]] virtual std::string_view get_pass_name() const noexcept = 0;
};

/**
 * @class DGTLPassManager
 * @brief The Central Orchestrator for the DGTL Compiler Stack.
 * Orchestrates Strikes (Passes) and Caches Facts (Analyses).
 */
class DGTLPassManager {
private:
    struct PipelineStrike {
        std::unique_ptr<DGTLOptimizationPass> pass;
        bool is_enabled{true};
        double last_execution_ms{0.0};
    };

    std::vector<PipelineStrike> strikes;
    core::DGTLArena& global_arena;
    core::DGTLFormalVerifier sentinel; 
    DGTLAnalysisManager analysis_manager; // The TAM Integration

public:
    /**
     * @brief Initialize with a reference to the Master Hardware Arena.
     */
    explicit DGTLPassManager(core::DGTLArena& arena) noexcept 
        : global_arena(arena) {}

    // RAII: Strict ownership (No copies allowed in high-performance pipelines)
    DGTLPassManager(const DGTLPassManager&) = delete;
    DGTLPassManager& operator=(const DGTLPassManager&) = delete;
    
    DGTLPassManager(DGTLPassManager&&) noexcept = default;
    DGTLPassManager& operator=(DGTLPassManager&&) noexcept = default;

    /**
     * @brief Registers a new optimization pass strike.
     */
    template <typename T, typename... Args>
    void add_pass(Args&&... args) {
        strikes.push_back({
            std::make_unique<T>(std::forward<Args>(args)...)
        });
    }

    /**
     * @brief Orchestrates the execution of all enabled strikes with integrated analysis.
     * @return true if the pipeline executed safely, false on formal verification failure.
     */
    [[nodiscard]] bool run(ir::IRStream& primary_stream) noexcept {
        if (primary_stream.empty()) [[unlikely]] return true;

        for (auto& entry : strikes) {
            if (!entry.is_enabled) [[unlikely]] continue;

            // Checkpoint for zero-latency memory rollback
            const size_t arena_checkpoint = global_arena.get_checkpoint();
            const auto start_time = std::chrono::high_resolution_clock::now();

            // PILLAR 2: Open Transactional Strike Zone
            primary_stream.begin_transaction();

            // Execute Strike without the bloat of try-catch
            // The Analysis Manager (TAM) is passed here to allow lazy analysis retrieval
            entry.pass->execute(primary_stream, analysis_manager);

            // PILLAR 3: The Final Gate (Formal Verification)
            if (validate_strike(primary_stream)) [[likely]] {
                // Success: Atomic Commit. IR version increments -> TAM cache purges automatically.
                primary_stream.commit_transaction();
            } else {
                // Logic Breach: Rollback IR and Arena Memory
                primary_stream.rollback_transaction();
                global_arena.rollback(arena_checkpoint);
            }

            const auto end_time = std::chrono::high_resolution_clock::now();
            entry.last_execution_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        }
        return true;
    }

    /**
     * @brief Force clear all passes and analysis caches.
     */
    void clear() noexcept {
        strikes.clear();
        analysis_manager.invalidate_all();
    }

    /**
     * @brief Access the Analysis Manager directly.
     */
    [[nodiscard]] DGTLAnalysisManager& get_analysis_manager() noexcept {
        return analysis_manager;
    }

    /**
     * @brief Performance Telemetry access.
     */
    [[nodiscard]] const std::vector<PipelineStrike>& get_telemetry() const noexcept {
        return strikes;
    }

private:
    /**
     * @brief Deep Validation of the Shadow Buffer.
     */
    [[nodiscard]] bool validate_strike(ir::IRStream& stream) const noexcept {
        if (!stream.is_transactional()) [[unlikely]] return false;

        const size_t shadow_count = stream.size(); 
        for (uint32_t i = 0; i < shadow_count; ++i) {
            ir::IRNode shadow_node = stream.get_node_readonly(i);

            // Pillar 3: Formal Proof of Intent
            const uint32_t expected_hash = core::DGTLFormalVerifier::compute_master_hash(shadow_node);
            if (shadow_node.intent_hash != expected_hash) [[unlikely]] {
                return false; 
            }

            // Pillar 3: Bit-Consistency Gate
            if (!sentinel.verify_bit_consistency(shadow_node, shadow_node)) [[unlikely]] {
                return false;
            }
        }

        return stream.validate_shadow_integrity();
    }
};

} // namespace dgtl::pipeline
