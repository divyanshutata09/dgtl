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
 * @file Pass_Manager_Config.ixx
 * @brief Configuration types, enums, and base classes for the optimization pass manager.
 * * This header defines:
 * - Pass execution configuration (TargetPrecision, PassStage, PassAffinity)
 * - Execution tracing and telemetry (PassExecutionTrace, PipelineTelemetry)
 * - Base classes (DGTLOptimizationPass, DGTLLanguageRootMarker)
 * * Part 1 of 3 in the Pass_Manager refactoring (Config â†’ Analysis â†’ Orchestrator)
 */

module;

#include <cstdlib>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

// FIXED: Bind to the correct core intermediate macro layer partition space
export module dgtl.ir_core.pass_manager_config;

// FIXED CYCLIC LOOP: Import local core leaf siblings natively via short-form shortcuts
import dgtl.ir_core;
// Standalone global modules
import dgtl.hashing;
import dgtl.ir_node;
import dgtl.ir_enums;

export namespace dgtl::pipeline {

// ============================================================================
// SECTION 1: PASS EXECUTION CONFIGURATION
// ============================================================================

/**
 * @enum TargetPrecision
 * @brief Optimization target precision level for the compiler pipeline.
 * Controls pass selection, aggressiveness, and trade-offs between speed and quality.
 */
enum class TargetPrecision {
    HFT,      // High-Frequency Trading (sub-microsecond)
    FP32 = HFT, // Backward compatibility alias for FP32 optimization workloads
    CLOUD,    // Cloud Computing (millisecond-scale)
    MOBILE,   // Mobile/IoT (energy-constrained)
    HPC       // High-Performance Computing (throughput-optimized)
};

/**
 * @enum PassStage
 * @brief Pipeline stage where a pass participates in the optimization sequence.
 * Enables passes to declare their position in the overall compilation flow.
 */
enum class PassStage : uint8_t {
    PreInlining,
    PostInlining,
    PostSpecialization,
    LateCleanup
};

/**
 * @enum PassAffinity
 * @brief Signal-based priority for pass selection in adaptive scheduling.
 * Used by PassDecisionEngine to prioritize passes based on code characteristics.
 */
enum class PassAffinity : uint8_t {
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3
};

/**
 * @struct PassExecutionPolicy
 * @brief Global execution policy for pass manager behavior.
 * Controls determinism, safety checking, and instrumentation.
 */
struct PassExecutionPolicy {
    bool deterministic_order = true;      ///< Enforce identical pass order every run
    bool enforce_safety_checks = true;    ///< Enable transactional integrity validation
    bool favor_binary_size = false;       ///< Prioritize code size over performance
    bool emit_trace = false;              ///< Emit pass execution trace to stdout
};

// ============================================================================
// SECTION 2: EXECUTION TRACING & TELEMETRY
// ============================================================================

/**
 * @struct PassSkipRecord
 * @brief Single entry in pass skip log.
 * Records which pass was skipped and why (for auditability).
 */
struct PassSkipRecord {
    const char* pass_name = nullptr;  ///< Pass identifier (string literal)
    const char* reason    = nullptr;  ///< Skip reason (string literal)
};

/**
 * @struct PassExecutionTrace
 * @brief Volatile-safe trace of pass execution order and skip decisions.
 * 
 * The volatile sink field prevents -O2/-O3/-Ofast from dead-store-eliminating
 * trace writes, making the trace proof against compiler optimization removal.
 * Every single pass execution and skip decision is logged.
 * 
 * Usage:
 *   const PassExecutionTrace& t = manager.get_last_trace();
 *   // t.sink is always live; t.executed_order holds the strict pass sequence.
 *   // t.skipped_log shows all skip decisions with reasons
 * 
 * This is a permanent library-level feature for all users of DGTLoptManager.
 */
struct PassExecutionTrace {
    volatile uint64_t sink = 0;      ///< Volatile: survives -O3 dead-store elimination

    uint32_t executed_count     = 0; ///< Number of passes that ran (capped at kMaxExecuted)
    uint32_t skipped_count      = 0; ///< Number of passes explicitly skipped (capped at kMaxSkipped)
    uint32_t hot_path_count     = 0; ///< Number of high-priority hot path passes (capped at kMaxHotPath)
    uint64_t executed_total     = 0; ///< Total executed passes (uncapped counter)
    uint64_t skipped_total      = 0; ///< Total skipped decisions (uncapped counter)
    uint64_t hot_path_total     = 0; ///< Total hot-path executions (uncapped counter)
    uint64_t total_nodes_visited = 0; ///< Sum of node-visit counts across all passes

    uint64_t order_hash         = 1469598103934665603ull; ///< FNV-1a hash over execution order
    uint64_t hot_path_hash      = 1469598103934665603ull; ///< FNV-1a hash over hot path order
    uint64_t skip_hash          = 1469598103934665603ull; ///< FNV-1a hash over skip decisions

    static constexpr uint32_t kMaxExecuted = 64;   ///< Capacity of executed_order array
    static constexpr uint32_t kMaxSkipped  = 32;   ///< Capacity of skipped_log array
    static constexpr uint32_t kMaxHotPath  = 64;   ///< Capacity of hot_path_trace array

    std::array<const char*, kMaxExecuted> executed_order{};   ///< Strict pass execution sequence
    std::array<const char*, kMaxHotPath> hot_path_trace{};     ///< Hot-path pass sequence
    std::array<PassSkipRecord, kMaxSkipped> skipped_log{};     ///< All skip decisions with reasons

    [[nodiscard]] static uint64_t fnv1a_step(uint64_t h, uint64_t v) noexcept {
        return dgtl::utils::Hashing::combine(h, v);
    }

    [[nodiscard]] static uint64_t stable_token(const char* s) noexcept {
        if (!s) {
            return 0u;
        }
        return dgtl::utils::Hashing::fnv1a(s, std::strlen(s));
    }

    void reset() noexcept {
        sink                = 0u;
        executed_count      = 0u;
        skipped_count       = 0u;
        hot_path_count      = 0u;
        executed_total      = 0u;
        skipped_total       = 0u;
        hot_path_total      = 0u;
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
     * @param name      Pass name (must outlive this trace â€” typically a string literal).
     * @param nodes     Node-visitation count reported by the pass (0 if not tracked).
     * @param hot_path  Whether this pass is on the critical optimization path
     *
     * The volatile XOR write ensures the compiler cannot prove this call has no
     * observable side effects, preventing dead-code elimination under -O3.
     */
    void record_executed(const char* name, uint64_t nodes = 0, bool hot_path = false) noexcept {
        const uint64_t token = stable_token(name);
        sink ^= token; // volatile write
        ++executed_total;
        if (executed_count < kMaxExecuted) {
            executed_order[executed_count] = name;
            ++executed_count;
        }
        order_hash = fnv1a_step(order_hash, token);
        if (hot_path) {
            ++hot_path_total;
            if (hot_path_count < kMaxHotPath) {
                hot_path_trace[hot_path_count] = name;
                ++hot_path_count;
            }
            hot_path_hash = fnv1a_step(hot_path_hash, token);
        }
        total_nodes_visited += nodes;
    }

    /**
     * @brief Record that a pass was explicitly skipped (not silently elided).
     * @param name      Pass name.
     * @param reason    Human-readable skip reason (string literal).
     *
     * No silent optimizations: every skip decision is logged here so callers
     * can audit the exact pass sequence and detect non-determinism.
     */
    void record_skipped(const char* name, const char* reason) noexcept {
        const uint64_t name_token = stable_token(name);
        const uint64_t reason_token = stable_token(reason);
        sink ^= (name_token ^ reason_token); // volatile write
        ++skipped_total;
        if (skipped_count < kMaxSkipped) {
            skipped_log[skipped_count] = {name, reason};
            ++skipped_count;
        }
        skip_hash = fnv1a_step(skip_hash, name_token);
        skip_hash = fnv1a_step(skip_hash, reason_token);
    }
};

/**
 * @struct PipelineTelemetry
 * @brief Performance metrics collected during pass pipeline execution.
 * Records wall-clock time, pass counts, and node-visit statistics.
 */
struct PipelineTelemetry {
    uint64_t nodes_processed = 0;   ///< Total IR nodes processed by all passes
    uint64_t passes_run = 0;        ///< Number of passes executed
    uint64_t passes_skipped = 0;    ///< Number of passes skipped
    uint64_t hot_swaps = 0;         ///< Number of hot-path schedule changes
    double wall_seconds = 0.0;      ///< Wall-clock time for entire pipeline
};

// ============================================================================
// SECTION 3: BASE CLASSES FOR PASSES
// ============================================================================

/**
 * @class DGTLLanguageRootMarker
 * @brief Abstract interface for language-specific IR root identification.
 * Used by passes that need to distinguish language constructs from generic IR.
 */
class DGTLLanguageRootMarker {
public:
    virtual ~DGTLLanguageRootMarker() = default;
    [[nodiscard]] virtual bool is_root(const ir::IRNode& node) const noexcept = 0;
};

/**
 * @class DGTLOptimizationPass
 * @brief Base class for all optimization passes in the DGTL framework.
 * Provides the standard interface for pass execution, metadata, and traits.
 * 
 * All passes must:
 * 1. Inherit from this class
 * 2. Implement execute(stream) for optimization logic
 * 3. Implement get_pass_name() for identification
 * 4. Declare preservation/provided analyses for cache reuse
 * 5. Declare affinity signals for adaptive scheduling
 */
class DGTLOptimizationPass {
public:
    DGTLOptimizationPass() = default;
    virtual ~DGTLOptimizationPass() = default;

    /**
     * @brief Execute the optimization pass on the given IR stream.
     * @param stream The IR stream to optimize (modifies in-place)
     */
    virtual void execute(ir::IRStream& stream) = 0;

    /**
     * @brief Get the name of this optimization pass.
     * @return A string identifier for the pass (must be stable across runs)
     */
    [[nodiscard]] virtual const char* get_pass_name() const noexcept = 0;

    /**
     * @brief Check if this pass requires transactional execution.
     * Default: true (all passes protected by shadow buffer).
     * Override to false only for read-only passes (GVN, analysis passes).
     * @return true if the pass needs shadow buffer protection
     */
    [[nodiscard]] virtual bool requires_transaction() const noexcept { return true; }

    /**
     * @brief Declares where this pass participates in the optimization timeline.
     * Used by pass manager for stage-based scheduling.
     */
    [[nodiscard]] virtual PassStage get_stage() const noexcept { return PassStage::PostInlining; }

    /**
     * @brief Declares whether repeated runs produce identical output for identical input.
     * If false, deterministic_order mode will reject the pass.
     */
    [[nodiscard]] virtual bool is_deterministic() const noexcept { return true; }

    // ============================================================================
    // SIGNAL-BASED AFFINITIES (consumed by PassDecisionEngine)
    // ============================================================================

    [[nodiscard]] virtual PassAffinity hot_path_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity memory_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity vector_affinity() const noexcept { return PassAffinity::Medium; }
    [[nodiscard]] virtual PassAffinity speculation_affinity() const noexcept { return PassAffinity::Low; }

    /**
     * @brief Optional hook for transactional passes to report analysis cache invalidation.
     * 
     * Called after transaction commit to signal which analyses became invalid.
     * Returns a bitmask from dgtl::ir::rewrite::AnalysisInvalidation and must
     * clear internal pending state so repeated calls do not duplicate invalidations.
     * 
     * @return Bitmask of invalidated analyses (0 if none)
     */
    [[nodiscard]] virtual uint32_t consume_analysis_invalidation_mask() noexcept { return 0u; }

    /**
     * @brief Declare which analyses are preserved (NOT invalidated) by this pass.
     * 
     * Returns a bitmask of PreservedAnalysisKind values indicating analyses
     * that remain valid after the pass completes. This enables downstream passes
     * to reuse cached analysis results.
     *
     * Default: returns 0 (all analyses invalidated). Passes must override if
     * they preserve specific analyses to enable downstream pass optimizations.
     *
     * Example: A read-only pass like GVN can preserve DomTree and Liveness:
     *   return PreservedAnalysisKind::DominatorTree | PreservedAnalysisKind::Liveness;
     *
     * @return Bitmask of preserved analyses
     */
    [[nodiscard]] virtual uint32_t get_preserved_analyses() const noexcept { return 0u; }

    /**
     * @brief Optional: Report which new analyses this pass populates/computes.
     * 
     * Used for caching and dependency planning. Allows pass manager to know
     * which analyses are available after this pass runs, enabling reuse by
     * downstream passes that don't need to recompute.
     *
     * Default: 0 (no new analyses computed by default).
     *
     * @return Bitmask of analyses provided by this pass
     */
    [[nodiscard]] virtual uint32_t get_provided_analyses() const noexcept { return 0u; }
};

} // namespace dgtl::pipeline
