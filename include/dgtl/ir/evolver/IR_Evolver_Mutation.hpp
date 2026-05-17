/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Version:    1.0.0 — IR EVOLUTION ENGINE [PRODUCTION-GRADE]
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * PURPOSE
 * -------
 * IR_Evolution_Unified.hpp is a full-fidelity merge of:
 *   - IR_Evolver.hpp
 *   - Mutation_Strategies.hpp
 *
 * IR_Evolver.hpp implements adaptive, profile-guided IR specialization. Based on
 * runtime feedback (hotness, call counts, branch patterns), the engine selects and
 * applies targeted mutations to improve code quality. Integration with IR_Verifier
 * and AI_Cost_Model ensures correctness and predictable benefit.
 *
 * KEY DESIGN DECISIONS
 * --------------------
 *  - All mutations are reversible: every apply() saves state for rollback
 *  - IR_Verifier gates all apply operations: no mutation escapes unvalidated
 *  - Cost prediction uses AI_Cost_Model before commit to avoid speculative waste
 *  - Thread-safe profile collection (mutation application is single-threaded)
 *  - Mutation rollback on verification failure (no partial updates)
 *
 * STRATEGY HIERARCHY
 * ------------------
 * MutationStrategy (abstract base)
 *   - ConstantFoldingStrategy       - compile-time constant evaluation
 *   - VectorizationStrategy         - SIMD marking + vectorization hints
 *   - MemoryOptimizationStrategy    - layout + prefetch + SoA/AoS
 *   - LoopFusionStrategy            - adjacent loop fusion
 *   - InliningStrategy              - function inlining (hotpath)
 *   - LoopUnrollingStrategy         - loop unrolling (bounded iterations)
 *   - TensorFusionStrategy          - adjacent tensor op fusion
 *   - AdaptiveMutationStrategy      - meta-strategy (best mutation selection)
 *
 * STANDARDS: C++26 / UCRT64 - Zero-Overhead Primitive Mapping
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/ir/base/IR_Node.hpp"
#include "dgtl/ir/base/IR_SSA.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/base/IR_Verifier.hpp"

// Forward declaration — full definition in dgtl/analysis/AI_Cost_Model.hpp.
// Included here via forward-decl only to break the circular chain:
//   Global_IR.hpp -> IR_Evolver.hpp -> AI_Cost_Model.hpp -> Global_IR.hpp
namespace dgtl::analysis { class DGTLAICostModel; }

namespace dgtl::ir::evolution {

// ============================================================================
// SECTION: DATA STRUCTURES FOR PROFILING & FEEDBACK
// ============================================================================

/**
 * @struct MutationProfile
 * @brief Runtime profiling data collected for a specific IR node during execution.
 *
 * All counts are sampled — they represent execution frequency during the profiling
 * epoch, not exhaustive totals. The verifier uses these to prioritize hotspot
 * optimizations.
 */
struct MutationProfile {
    uint64_t execution_count       = 0;    ///< Times this node (or its descendants) executed
    uint64_t total_latency_cycles  = 0;    ///< Total CPU cycles spent in/below this node
    float    avg_latency           = 0.0f; ///< Latency per execution (total / count)
    uint32_t hotness_rank          = 0;    ///< Rank in hotspot table (0 = hottest)
    bool     is_loop_critical      = false;///< True if inside a loop with high iteration count
    uint32_t branch_mispredictions = 0;    ///< Observed branch prediction failures

    /// Validation: profile is credible if count > 0 and latency is plausible
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return execution_count > 0 && avg_latency >= 0.0f && avg_latency < 1e9f;
    }
};

/**
 * @struct ExecutionFeedback
 * @brief Aggregate runtime feedback across an IR stream or function.
 *
 * Collected by the runtime profiler; fed to apply_specialization() for
 * stream-wide optimization decisions.
 */
struct ExecutionFeedback {
    std::vector<float>    hotness;              ///< Hotness score per node (0.0-1.0)
    std::vector<uint64_t> call_count;           ///< Execution count per instruction
    uint32_t              total_executions = 0; ///< Total function invocations
    float                 avg_branch_prediction_rate = 0.0f; ///< 0.0-1.0
    uint32_t              speculations_made = 0;    ///< Adaptive predictions attempted
    uint32_t              speculations_verified = 0;///< Predictions that proved correct

    /// Validation: arrays must match stream size
    [[nodiscard]] bool is_aligned_to_stream_size(size_t stream_size) const noexcept {
        return (hotness.empty() || hotness.size() == stream_size) &&
               (call_count.empty() || call_count.size() == stream_size) &&
               total_executions > 0;
    }
};

// ============================================================================
// SECTION: MUTATION TYPES & CLASSIFICATION
// ============================================================================

/**
 * @enum MutationType
 * @brief Categories of IR mutations available for adaptive evolution.
 *
 * Each mutation type targets a different class of optimization: constants,
 * memory patterns, vectorization, control flow, etc.
 */
enum class MutationType : uint8_t {
    NO_MUTATION              = 0,  ///< No mutation applicable / cost negative
    CONSTANT_FOLDING         = 1,  ///< Evaluate compile-time constant expressions
    INSTRUCTION_FUSION       = 2,  ///< Combine multi-op patterns into single instruction
    REGISTER_REASSIGNMENT    = 3,  ///< Reorder virtual registers for better allocation
    MEMORY_LAYOUT_OPT        = 4,  ///< SoA↔AoS layout optimization
    BRANCH_OPTIMIZATION      = 5,  ///< Branch prediction hints + likely/unlikely
    VECTORIZATION_HINT       = 6,  ///< Mark for SIMD expansion (AVX-512, NEON)
    DESPECIALIZATION         = 7,  ///< Revert overly-specific mutation (rare)
};

/**
 * @struct MutationResult
 * @brief Outcome of an apply_mutation() or apply_specialization() operation.
 *
 * Distinguishes successful mutations from failures, and optionally provides
 * a before/after IR_Verifier report for debugging.
 */
struct MutationResult {
    bool     succeeded           = false;
    float    predicted_speedup   = 0.0f;  ///< Estimated % improvement from cost model
    uint32_t nodes_affected      = 0;     ///< Number of IR nodes modified
    uint32_t verification_errors = 0;     ///< Post-mutation error count (0 = clean)
    std::string fail_reason;               ///< Human-readable error on failure
};

// ============================================================================
// SECTION: IR EVOLUTION ENGINE
// ============================================================================

/**
 * @class IREvolver
 * @brief Adaptive IR evolution engine driven by runtime profiling and cost prediction.
 *
 * The IREvolver selects and applies mutations to IR streams based on:
 *   1. Runtime hotness data (MutationProfile, ExecutionFeedback)
 *   2. Cost model predictions (AI_Cost_Model)
 *   3. Correctness gates (IR_Verifier post-mutation)
 *
 * All mutations are reversible. If verification fails post-mutation, the change
 * is rolled back and a new mutation is attempted.
 *
 * NOT THREAD-SAFE: Mutation application is single-threaded. Profile collection
 * (ExecutionFeedback) may proceed concurrently via atomic counters, but once
 * specialization begins, the stream is locked to one thread.
 *
 * EXAMPLE:
 * @code
 *   dgtl::ir::evolution::IREvolver evolver;
 *   dgtl::ir::evolution::ExecutionFeedback feedback = profiler.collect(...);
 *
 *   // Evolve the stream
 *   auto result = evolver.apply_specialization(stream, feedback);
 *   if (result.succeeded) {
 *       log("Applied " + std::to_string(result.nodes_affected) +
 *           " mutations, " + std::to_string(result.predicted_speedup) + "% predicted");
 *   } else {
 *       log("Evolution failed: " + result.fail_reason);
 *   }
 * @endcode
 */
class IREvolver {
public:
    IREvolver() = default;
    ~IREvolver() = default;

    IREvolver(const IREvolver&) = delete;
    IREvolver& operator=(const IREvolver&) = delete;

    // ----------------------------------------------------------------
    // PRIMARY INTERFACE
    // ----------------------------------------------------------------

    /**
     * @brief Select the best mutation for a given IR node based on profiling.
     *
     * The selection process:
     *   1. Validate profile data (count > 0, latency plausible)
     *   2. Query cost model for each applicable mutation type
     *   3. Return the mutation with highest predicted speedup (or NO_MUTATION if negative)
     *
     * @param node The IR node to evolve
     * @param profile_data Runtime execution frequency and latency for this node
     * @param cost_model AI cost model for benefit prediction
     * @return Selected mutation type (NO_MUTATION if no benefit predicted)
     */
    [[nodiscard]] MutationType select_mutation(
        const dgtl::ir::IRNode* node,
        const MutationProfile& profile_data,
        dgtl::analysis::DGTLAICostModel& cost_model
    ) const noexcept;

    /**
     * @brief Apply a single mutation to an IR node with full verification.
     *
     * Mutation contract:
     *   - Save original IRNode state before applying change
     *   - Apply mutation via specialized method (e.g., apply_constant_folding)
     *   - Call IR_Verifier on parent stream to detect breakage
     *   - On verification failure, restore original state and return false
     *   - On success, the mutated node remains in the stream
     *
     * @param stream The stream containing the node (for verification)
     * @param node The IR node to mutate (must belong to stream)
     * @param mutation_type The type of mutation to apply
     * @return MutationResult with success/failure + diagnostics
     */
    [[nodiscard]] MutationResult apply_mutation(
        const dgtl::ir::IRStream& stream,
        dgtl::ir::IRNode* node,
        MutationType mutation_type
    ) noexcept;

    /**
     * @brief Specialize an IR stream for specific runtime conditions.
     *
     * Stream-wide specialization process:
     *   1. Validate ExecutionFeedback alignment (hotness/call_count match stream size)
     *   2. Iterate through stream, scoring each node (hotness * opcode cost factor)
     *   3. For top-K hottest nodes, attempt targeted mutations
     *   4. Accumulate results across all mutations
     *   5. Final verification pass: ir_is_valid(stream)
     *   6. On critical failure, roll back all mutations (restore from backup)
     *
     * @param stream The IR stream to specialize (modified in-place)
     * @param feedback Runtime execution feedback (call counts, hotness)
     * @return MutationResult: success count, total predicted speedup, errors
     */
    [[nodiscard]] MutationResult apply_specialization(
        dgtl::ir::IRStream& stream,
        const ExecutionFeedback& feedback
    ) noexcept;

    /**
     * @brief Predict the benefit of a mutation via the cost model.
     *
     * Uses AI_Cost_Model to estimate the % speedup (or slowdown if negative)
     * that a specific mutation would provide for a specific node.
     *
     * @param node The node to potentially mutate
     * @param mutation_type The type of mutation to consider
     * @param cost_model The AI cost model for prediction
     * @return Predicted speedup in % (positive = faster, negative = slower)
     */
    [[nodiscard]] double predict_mutation_benefit(
        const dgtl::ir::IRNode* node,
        MutationType mutation_type,
        dgtl::analysis::DGTLAICostModel& cost_model
    ) const noexcept;

    /**
     * @brief Detect and rank hotspots (high-impact IR regions) for optimization.
     *
     * Scans the stream for nodes with high execution frequency or loop criticality.
     * Returns the count of distinct hotspot regions found.
     *
     * @param stream The IR stream to analyze
     * @param feedback Execution feedback (hotness scores)
     * @return Number of distinct hotspot regions detected
     */
    [[nodiscard]] uint32_t detect_hotspots(
        const dgtl::ir::IRStream& stream,
        const ExecutionFeedback& feedback
    ) const noexcept;

    // ----------------------------------------------------------------
    // DIAGNOSTIC & CONFIGURATION
    // ----------------------------------------------------------------

    /// Set minimum predicted speedup (%) required to apply a mutation.
    /// Default: 2.0 (only mutate if >= 2% improvement predicted).
    void set_min_speedup_threshold(float pct) noexcept { min_speedup_threshold_ = pct; }

    /// Get current minimum speedup threshold.
    [[nodiscard]] float get_min_speedup_threshold() const noexcept {
        return min_speedup_threshold_;
    }

    /// Set maximum number of mutations to attempt on a single stream.
    /// Default: 32 (prevent mutation storms on deeply nested code).
    void set_max_mutations_per_stream(uint32_t limit) noexcept {
        max_mutations_per_stream_ = limit;
    }

    /// Get current per-stream mutation limit.
    [[nodiscard]] uint32_t get_max_mutations_per_stream() const noexcept {
        return max_mutations_per_stream_;
    }

private:
    // ----------------------------------------------------------------
    // MUTATION IMPLEMENTATIONS (per-type helpers)
    // ----------------------------------------------------------------

    /**
     * @brief Apply constant folding mutation.
     *
     * For arithmetic opcodes (ADD_I32, MUL_I32, etc.) whose operands are
     * compile-time constants, evaluate the operation and replace with LOAD_IMM.
     *
     * Example: { ADD_I32, op_a=5, op_b=3 } -> { LOAD_IMM, result=8 }
     */
    [[nodiscard]] bool apply_constant_folding(dgtl::ir::IRNode* node) noexcept;

    /**
     * @brief Apply instruction fusion mutation.
     *
     * Combines multiple instruction patterns into single fused operations:
     *   - LEA + LOAD -> single prefetch
     *   - ADD + LOAD -> load-add fusion
     *   - LOOP pattern (LABEL->GOTO) -> single unrolled block
     */
    [[nodiscard]] bool apply_instruction_fusion(dgtl::ir::IRNode* node) noexcept;

    /**
     * @brief Apply register reassignment.
     *
     * Reorders virtual register IDs to improve register allocator efficiency:
     *   - Place high-pressure vregs in non-callee-saved registers
     *   - Cluster related vregs for better cache locality
     */
    [[nodiscard]] bool apply_register_reassignment(dgtl::ir::IRNode* node) noexcept;

    /**
     * @brief Apply memory layout optimization.
     *
     * For LOAD/STORE heavy code:
     *   - Detect Array-of-Structures patterns -> convert to Structure-of-Arrays
     *   - Detect strided access -> add prefetch hints
     */
    [[nodiscard]] bool apply_memory_layout_opt(dgtl::ir::IRNode* node) noexcept;

    /**
     * @brief Apply branch prediction optimization.
     *
     * For BRANCH/BRANCH_COND opcodes:
     *   - Insert LIKELY/UNLIKELY hints based on branch misprediction history
     *   - Reorder basic blocks to improve fall-through prediction
     */
    [[nodiscard]] bool apply_branch_optimization(dgtl::ir::IRNode* node) noexcept;

    /**
     * @brief Apply vectorization hints.
     *
     * For scalar arithmetic patterns convertible to SIMD:
     *   - Mark with IS_SIMD_BURST flag
     *   - Suggest GATHER_F32_512 replacement for strided loads
     *   - Enable vector port in flags
     */
    [[nodiscard]] bool apply_vectorization_hint(dgtl::ir::IRNode* node) noexcept;

    // ----------------------------------------------------------------
    // ANALYSIS HELPERS
    // ----------------------------------------------------------------

    /**
     * @brief Estimate execution frequency for a node from profile data.
     *
     * Combines multiple signals: direct execution count, loop context,
     * branch prediction success rate.
     *
     * @return Estimated frequency (higher = hotter)
     */
    [[nodiscard]] uint64_t estimate_exec_frequency(const MutationProfile& profile) const noexcept;

    /**
     * @brief Check if a node is eligible for a specific mutation type.
     *
     * Pre-filter for mutation applicability before cost model evaluation.
     */
    [[nodiscard]] static constexpr bool is_applicable(
        const dgtl::ir::IRNode* node,
        MutationType mutation_type
    ) noexcept;

    // ----------------------------------------------------------------
    // CONFIGURATION
    // ----------------------------------------------------------------

    float min_speedup_threshold_      = 2.0f;  ///< Minimum % speedup to apply mutation
    uint32_t max_mutations_per_stream_ = 32;   ///< Safety cap on mutation count
};

/*
 * ---------------------------------------------------------------------------
 * Verbatim strategy subsystem sections merged from Mutation_Strategies.hpp
 * ---------------------------------------------------------------------------
 */

// ============================================================================
// SECTION: MUTATION STRATEGY BASE CLASS
// ============================================================================

/**
 * @class MutationStrategy
 * @brief Abstract base class for IR mutation strategies.
 *
 * Each concrete strategy implements a specific class of transformations
 * (constant folding, vectorization, etc.). The strategy pattern allows
 * new mutation types to be added without modifying the core evolver.
 *
 * CONTRACT
 * --------
 *  applies_to()  — Fast predicate: "would this strategy apply to this node?"
 *  try_apply()   — Attempt the mutation; return true on success
 *  get_type()    — Return the MutationType enum value
 *
 * IMPLEMENTATION NOTES
 * --------------------
 *  - All subclasses must be copyable (used in registry vector)
 *  - apply() implementations must be const-correct
 *  - If node is null, safely return false (defensive)
 *  - Mutations MUST call node->refresh_intent_hash() before returning
 *  - If flags were not modified, skip hash refresh (optimization)
 */
class MutationStrategy {
public:
    virtual ~MutationStrategy() = default;

    /**
     * @brief Check if this strategy applies to the given node.
     *
     * This is a fast predicate used for filtering — should be O(1).
     * Return false for null nodes (defensive).
     *
     * @param node The node to evaluate (may be nullptr)
     * @return true if this strategy can potentially improve this node
     */
    [[nodiscard]] virtual bool applies_to(const dgtl::ir::IRNode* node) const = 0;

    /**
     * @brief Attempt to apply the mutation to the node.
     *
     * IMPORTANT CONTRACT:
     *   - Modify the node in-place (flags, operands as appropriate to strategy)
     *   - If any modification was made, MUST call node->refresh_intent_hash()
     *   - Return true if mutation was applied successfully
     *   - Return false if mutation was skipped or invalid
     *   - For null nodes, return false (defensive)
     *
     * The caller (IREvolver) will verify the result with IR_Verifier.
     * If verification fails, the IREvolver rolls back the mutation.
     *
     * @param node The node to mutate (modified in-place)
     * @return true if mutation was applied; false if skipped or invalid
     */
    [[nodiscard]] virtual bool try_apply(dgtl::ir::IRNode* node) noexcept = 0;

    /**
     * @brief Get the type of mutation this strategy performs.
     *
     * Used for bookkeeping and diagnostics.
     *
     * @return MutationType enum value
     */
    [[nodiscard]] virtual MutationType get_type() const noexcept = 0;
};

// ============================================================================
// SECTION: CONCRETE MUTATION STRATEGIES
// ============================================================================

/**
 * @class ConstantFoldingStrategy
 * @brief Evaluate compile-time constant expressions to eliminate arithmetic.
 *
 * Applies to: arithmetic ops (ADD_I32, MUL_I32, etc.) with constant operands,
 *             nodes marked with IS_SPEC_CONST flag.
 *
 * Example transformation:
 *   { opcode: ADD_I32, op_a: 5_vreg, op_b: 3_vreg, flags: IS_SPEC_CONST }
 *   -> { opcode: LOAD_IMM, result_id: new_vreg, op_a/op_b: 0x0000000000000008 }
 *
 * When both operands are literal compile-time constants (detected via IRNode
 * flags or cost model), the operation is evaluated and replaced with a single
 * LOAD_IMM, saving latency and register pressure.
 */
class ConstantFoldingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::CONSTANT_FOLDING;
    }
};

/**
 * @class VectorizationStrategy
 * @brief Mark scalar operations for SIMD vectorization (AVX-512, NEON).
 *
 * Applies to: LOAD, scalar float arithmetic (ADD_F32, MUL_F32),
 *             vectorizable gather patterns.
 *
 * Transformation: Sets flags IS_SIMD_BURST and REQUIRES_VEC_PORT to signal
 *                 backend that this operation should be expanded to 512-bit
 *                 vector form or fused into existing SIMD blocks.
 *
 * Example:
 *   { opcode: ADD_F32, ... } -> { ADD_F32, flags: IS_SIMD_BURST | REQUIRES_VEC_PORT }
 *
 * The backend may then convert scalar ADD_F32 into VEC_ADD_F32_512 if adjacent
 * operations are also marked for vectorization (creating a SIMD block).
 */
class VectorizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::VECTORIZATION_HINT;
    }
};

/**
 * @class MemoryOptimizationStrategy
 * @brief Optimize memory access patterns: layout transformation, prefetch hints.
 *
 * Applies to: LOAD, STORE, LEA (memory addressing ops).
 *
 * Transformations:
 *   - Strided LOAD patterns -> Insert prefetch hints + align to cache line
 *   - Dense LOAD/STORE sequences -> Mark for SoA/AoS conversion analysis
 *   - LOAD marked IS_SIMD_BURST -> Set REQUIRES_VEC_PORT
 *
 * Example:
 *   { opcode: LOAD, ... } -> { LOAD, flags: IS_SIMD_BURST | REQUIRES_VEC_PORT }
 *   { opcode: LEA, ... } -> { LEA, flags: IS_OPTIMIZED }
 *
 * The backend uses these hints to:
 *   1. Insert PREFETCH instructions before the LOAD
 *   2. Align base addresses to cache-line boundaries
 *   3. Group related loads for better bandwidth utilization
 */
class MemoryOptimizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::MEMORY_LAYOUT_OPT;
    }
};

/**
 * @class LoopFusionStrategy
 * @brief Fuse adjacent loop bodies to reduce loop overhead.
 *
 * Applies to: LABEL (loop header), GOTO (loop branch).
 *
 * Detects patterns like:
 *   LABEL L1; ... GOTO L1;  (inner loop)
 *   LABEL L2; ... GOTO L2;  (adjacent inner loop, same bounds)
 *
 * Transformation: Marks both loops with IS_OPTIMIZED flag, signaling backend
 *                 to attempt fusion (combine bodies into single loop structure).
 *
 * Loop fusion reduces loop overhead by:
 *   - Eliminating one loop header and branch
 *   - Reducing pipeline flushes
 *   - Improving cache locality (single working set)
 */
class LoopFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::INSTRUCTION_FUSION;
    }
};

/**
 * @class InliningStrategy
 * @brief Inline function calls in hot paths to eliminate call overhead.
 *
 * Applies to: CALL opcodes.
 *
 * Transformation: Mark CALL nodes with IS_OPTIMIZED flag. Backend determines
 *                 if the callee is available and small enough to inline.
 *
 * Inlining benefits:
 *   - Eliminates CALL/RET overhead (~10-20 cycles)
 *   - Exposes more optimization opportunities (cross-function)
 *   - Improves branch prediction (less context switch)
 *
 * Conservative: Only attempts inlining if:
 *   - Callee size < threshold (e.g., 64 instructions)
 *   - Node is in a loop (execution count high)
 *   - Predicted speedup > threshold (e.g., 5%)
 */
class InliningStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::INSTRUCTION_FUSION;
    }
};

/**
 * @class LoopUnrollingStrategy
 * @brief Unroll loops with bounded iteration count to expose parallelism.
 *
 * Applies to: LABEL (loop header, small iteration count).
 *
 * Transformation: Mark loop with IS_OPTIMIZED flag. Backend estimates iteration
 *                 count via SSA analysis or loop header constants; if < threshold
 *                 (e.g., 8), unrolls to duplicate body K times.
 *
 * Unrolling benefits:
 *   - Reduces loop branch misprediction (fewer branches total)
 *   - Increases instruction-level parallelism (scheduler sees more ops)
 *   - Improves register reuse (multiple iterations' registers can overlap)
 *
 * Conservative: Only unroll if:
 *   - Iteration count is provably small (< 16)
 *   - Loop body is not too large (< 32 instructions)
 *   - Expected speedup > threshold (typically 10%)
 */
class LoopUnrollingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::INSTRUCTION_FUSION;
    }
};

/**
 * @class TensorFusionStrategy
 * @brief Fuse adjacent tensor operations to eliminate intermediate stores.
 *
 * Applies to: Tensor ops (TENSOR_RESHAPE, TENSOR_REDUCE, TENSOR_TRANSPOSE, etc.).
 *
 * Example patterns:
 *   TENSOR_RESHAPE -> TENSOR_TRANSPOSE -> could fuse into single RESHAPE operation
 *   TENSOR_REDUCE -> TENSOR_BROADCAST -> could fuse if communicating through registers
 *
 * Transformation: Marks tensor ops with IS_SIMD_BURST flag, signaling backend
 *                 to analyze for fusion. Backend constructs a tensor dataflow
 *                 graph and looks for opportunities to combine operations.
 *
 * Tensor fusion benefits (ML/DSA workloads):
 *   - Eliminates intermediate tensor materialization
 *   - Reduces memory bandwidth (keep data in registers/L1)
 *   - Exposes more instruction parallelism
 *   - Critical for efficient tensor cores (NVIDIA TensorRT pattern)
 */
class TensorFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override;
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override;
    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::INSTRUCTION_FUSION;
    }
};

/**
 * @class AdaptiveMutationStrategy
 * @brief Meta-strategy that selects the best mutation based on execution patterns.
 *
 * Unlike concrete strategies, AdaptiveMutationStrategy applies to ALL nodes and
 * uses cost model predictions to select the single best mutation type for each node.
 *
 * This strategy is the primary entry point for IREvolver::select_mutation(). It
 * queries all other strategies, gathers cost predictions, and recommends the
 * mutation with the highest predicted speedup.
 *
 * Applies to: All nodes (universal strategy).
 *
 * Implementation notes:
 *   - try_apply() returns false; actual mutation is selected at IREvolver level
 *   - Serves as a placeholder / lookup point in the strategy registry
 *   - Could be extended to also apply a "meta-mutation" (e.g., despecialization)
 */
class AdaptiveMutationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        return node != nullptr; // Applies universally
    }

    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override {
        if (!node) return false;
        // Meta-strategy: no actual mutation applied here.
        // Selection happens at IREvolver level via cost model.
        return false;
    }

    [[nodiscard]] MutationType get_type() const noexcept override {
        return MutationType::REGISTER_REASSIGNMENT; // Represents adaptive tier
    }
};

// ============================================================================
// SECTION: STRATEGY REGISTRY & FACTORY
// ============================================================================

/**
 * @class MutationStrategyRegistry
 * @brief Singleton registry of available IR mutation strategies.
 *
 * RESPONSIBILITIES
 * ----------------
 *  - Maintain a collection of all registered mutation strategies
 *  - Support querying: "which strategies apply to this node?"
 *  - Provide access to the singleton instance via get_instance()
 *
 * THREAD SAFETY
 * ---------------
 * NOT THREAD-SAFE: Registration and querying must be serialized by the caller.
 * Typically: register all strategies at startup (single-threaded), then query
 * during evolution phase (single-threaded mutation application).
 *
 * USAGE
 * -----
 * @code
 *   auto& registry = MutationStrategyRegistry::get_instance();
 *   registry.register_strategy(std::make_unique<ConstantFoldingStrategy>());
 *   registry.register_strategy(std::make_unique<VectorizationStrategy>());
 *   // ... register remaining strategies
 *
 *   // Later, query applicable strategies for a node
 *   auto applicable = registry.get_applicable_strategies(node);
 *   for (auto strategy : applicable) {
 *       if (strategy->try_apply(node)) break;
 *   }
 * @endcode
 */
class MutationStrategyRegistry {
public:
    /// Access the singleton instance.
    [[nodiscard]] static MutationStrategyRegistry& get_instance() noexcept {
        static MutationStrategyRegistry registry;
        return registry;
    }

    /**
     * @brief Register a new mutation strategy.
     *
     * Takes ownership of the strategy (unique_ptr).
     * Safe to call with nullptr (no-op).
     *
     * @param strategy The strategy to register (may be nullptr, no-op)
     */
    void register_strategy(std::unique_ptr<MutationStrategy> strategy) noexcept {
        if (!strategy) return;
        strategies_.push_back(std::move(strategy));
    }

    /**
     * @brief Get all strategies that apply to a given node.
        *
        * Queries each registered strategy's applies_to() predicate.
        * Returns a vector of raw pointers (valid for lifetime of registry).
        *
        * IMPORTANT: Returned pointers are owned by the registry and become invalid
        *            if the registry is destroyed. Typically, registry is static and
        *            outlives all users, so this is safe in practice.
        *
        * @param node The node to query strategies for (may be nullptr)
        * @return Vector of applicable strategy pointers (empty if none apply)
     */
    [[nodiscard]] std::vector<MutationStrategy*> get_applicable_strategies(
        const dgtl::ir::IRNode* node
    ) const noexcept {
        std::vector<MutationStrategy*> applicable;
        applicable.reserve(strategies_.size());
        for (const auto& strategy : strategies_) {
            if (strategy && strategy->applies_to(node)) {
                applicable.push_back(strategy.get());
            }
        }
        return applicable;
    }

    /**
     * @brief Get all registered strategies (for diagnostics / iteration).
     */
    [[nodiscard]] const std::vector<std::unique_ptr<MutationStrategy>>&
    get_all_strategies() const noexcept {
        return strategies_;
    }

    /// Clear all registered strategies (for testing or re-initialization).
    void clear_strategies() noexcept {
        strategies_.clear();
    }

    /// Get the number of registered strategies.
    [[nodiscard]] size_t strategy_count() const noexcept {
        return strategies_.size();
    }

private:
    MutationStrategyRegistry() = default;
    ~MutationStrategyRegistry() = default;

    // Prevent copying/moving of singleton
    MutationStrategyRegistry(const MutationStrategyRegistry&) = delete;
    MutationStrategyRegistry& operator=(const MutationStrategyRegistry&) = delete;
    MutationStrategyRegistry(MutationStrategyRegistry&&) = delete;
    MutationStrategyRegistry& operator=(MutationStrategyRegistry&&) = delete;

    std::vector<std::unique_ptr<MutationStrategy>> strategies_;
};

} // namespace dgtl::ir::evolution
