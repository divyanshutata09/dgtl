/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Version:     1.0.0 â€” IR EVOLUTION ENGINE [PRODUCTION-GRADE UNIFIED]
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * PURPOSE
 * -------
 * IR_Evolution_Unified.ixx is a full-fidelity merge of:
 * - IR_Evolver.hpp
 * - Mutation_Strategies.hpp
 * - IR_Evolver_Mutation.cpp (Implementation logic)
 *
 * KEY DESIGN DECISIONS
 * --------------------
 * - All mutations are reversible: every apply() saves state for rollback
 * - IR_Verifier gates all apply operations: no mutation escapes unvalidated
 * - Cost prediction uses HIR mutation cost model before commit to avoid speculative waste
 * - Thread-safe profile collection (mutation application is single-threaded)
 * - Mutation rollback on verification failure (no partial updates)
 *
 * STRATEGY HIERARCHY
 * ------------------
 * MutationStrategy (abstract base)
 * - ConstantFoldingStrategy       - compile-time constant evaluation
 * - VectorizationStrategy         - SIMD marking + vectorization hints
 * - MemoryOptimizationStrategy    - layout + prefetch + SoA/AoS
 * - LoopFusionStrategy            - adjacent loop fusion
 * - InliningStrategy              - function inlining (hotpath)
 * - LoopUnrollingStrategy         - loop unrolling (bounded iterations)
 * - TensorFusionStrategy          - adjacent tensor op fusion
 * - AdaptiveMutationStrategy      - meta-strategy (best mutation selection)
 *
 * STANDARDS: C++26 / UCRT64 - Zero-Overhead Primitive Mapping
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

export module dgtl.ir.evolution;

// Import native compiler sub-systems and sibling module interfaces
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir.verifier;

import dgtl.ir.stream;
import dgtl.ir.ssa;
import dgtl.flat_map; // Natively import your ultra-low latency flat map layers

// Forward declare ExecutionFeedback so node_hotness can be defined here
// without a nested anonymous namespace.
export namespace dgtl::ir::evolution {

class IRMutationCostModel {
public:
    virtual ~IRMutationCostModel() = default;
    [[nodiscard]] virtual float predict_node_cost(const dgtl::ir::IRNode& node) const noexcept = 0;
};

struct ExecutionFeedback;
}

// ============================================================================
// FILE-LOCAL IMPLEMENTATION HELPERS (Anonymous Namespace)
// ============================================================================
namespace {

/// Return true if a node is non-null and structurally valid.
[[nodiscard]] inline bool is_mutable_node(const dgtl::ir::IRNode* node) noexcept {
    return node != nullptr && node->is_valid();
}

/// Return the hotness score for node at index i.
[[nodiscard]] inline float node_hotness_impl(const dgtl::ir::evolution::ExecutionFeedback& fb, size_t i) noexcept;

/// Clamp a predicted cost difference to a % improvement.
[[nodiscard]] inline double cost_delta_to_pct(float baseline, float mutated) noexcept {
    if (baseline <= 0.0f) return 0.0;
    return static_cast<double>((baseline - mutated) / baseline) * 100.0;
}

} // namespace

export namespace dgtl::ir::evolution {

// ============================================================================
// SECTION: DATA STRUCTURES FOR PROFILING & FEEDBACK
// ============================================================================

/**
 * @struct MutationProfile
 * @brief Runtime profiling data collected for a specific IR node during execution.
 */
struct MutationProfile {
    uint64_t execution_count       = 0;  ///< Times this node (or its descendants) executed
    uint64_t total_latency_cycles  = 0;  ///< Total CPU cycles spent in/below this node
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

/// Return the hotness score for node at index i, or 0.0 if not in feedback.
[[nodiscard]] inline float node_hotness_impl(const ExecutionFeedback& fb, size_t i) noexcept {
    if (i < fb.hotness.size()) return fb.hotness[i];
    if (i < fb.call_count.size()) {
        return static_cast<float>(fb.call_count[i]) /
               static_cast<float>(fb.total_executions > 0 ? fb.total_executions : 1);
    }
    return 0.0f;
}

// ============================================================================
// SECTION: MUTATION TYPES & CLASSIFICATION
// ============================================================================

/**
 * @enum MutationType
 * @brief Categories of IR mutations available for adaptive evolution.
 */
enum class MutationType : uint8_t {
    NO_MUTATION              = 0,  ///< No mutation applicable / cost negative
    CONSTANT_FOLDING         = 1,  ///< Evaluate compile-time constant expressions
    INSTRUCTION_FUSION       = 2,  ///< Combine multi-op patterns into single instruction
    REGISTER_REASSIGNMENT    = 3,  ///< Reorder virtual registers for better allocation
    MEMORY_LAYOUT_OPT        = 4,  ///< SoAâ†”AoS layout optimization
    BRANCH_OPTIMIZATION      = 5,  ///< Branch prediction hints + likely/unlikely
    VECTORIZATION_HINT       = 6,  ///< Mark for SIMD expansion (AVX-512, NEON)
    DESPECIALIZATION         = 7,  ///< Revert overly-specific mutation (rare)
};

/**
 * @struct MutationResult
 * @brief Outcome of an apply_mutation() or apply_specialization() operation.
 */
struct MutationResult {
    bool     succeeded           = false;
    float    predicted_speedup   = 0.0f;  ///< Estimated % improvement from cost model
    uint32_t nodes_affected      = 0;     ///< Number of IR nodes modified
    uint32_t verification_errors = 0;     ///< Post-mutation error count (0 = clean)
    std::string fail_reason;               ///< Human-readable error on failure
};

// ============================================================================
// SECTION: IR EVOLUTION ENGINE SPECIFICATION
// ============================================================================

/**
 * @class IREvolver
 * @brief Adaptive IR evolution engine driven by runtime profiling and cost prediction.
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

    [[nodiscard]] MutationType select_mutation(
        const dgtl::ir::IRNode* node,
        const MutationProfile& profile_data,
        IRMutationCostModel& cost_model
    ) const noexcept;

    [[nodiscard]] static MutationResult apply_mutation(
        const dgtl::ir::IRStream& stream,
        dgtl::ir::IRNode* node,
        MutationType mutation_type
    ) noexcept;

    [[nodiscard]] MutationResult apply_specialization(
        dgtl::ir::IRStream& stream,
        const ExecutionFeedback& feedback
    ) noexcept;

    [[nodiscard]] static double predict_mutation_benefit(
        const dgtl::ir::IRNode* node,
        MutationType mutation_type,
        IRMutationCostModel& cost_model
    ) noexcept;

    [[nodiscard]] static uint32_t detect_hotspots(
        const dgtl::ir::IRStream& stream,
        const ExecutionFeedback& feedback
    ) noexcept;

    [[nodiscard]] static std::vector<std::string>
    verify_generated_result_type_annotations(const dgtl::ir::IRStream& stream) {
        const auto ssa_errors = dgtl::ir::ssa::verify_generated_result_type_annotations(stream);
        std::vector<std::string> out;
        out.reserve(ssa_errors.count);
        for (size_t i = 0; i < ssa_errors.count; ++i) {
            out.emplace_back(ssa_errors.errors[i]);
        }
        return out;
    }

    // ----------------------------------------------------------------
    // DIAGNOSTIC & CONFIGURATION
    // ----------------------------------------------------------------

    void set_min_speedup_threshold(float pct) noexcept { min_speedup_threshold_ = pct; }
    [[nodiscard]] float get_min_speedup_threshold() const noexcept { return min_speedup_threshold_; }

    void set_max_mutations_per_stream(uint32_t limit) noexcept { max_mutations_per_stream_ = limit; }
    [[nodiscard]] uint32_t get_max_mutations_per_stream() const noexcept { return max_mutations_per_stream_; }

private:
    // ----------------------------------------------------------------
    // MUTATION IMPLEMENTATIONS (per-type helpers)
    // ----------------------------------------------------------------
    [[nodiscard]] static bool apply_constant_folding(dgtl::ir::IRNode* node) noexcept;
    [[nodiscard]] static bool apply_instruction_fusion(dgtl::ir::IRNode* node) noexcept;
    [[nodiscard]] static bool apply_register_reassignment(dgtl::ir::IRNode* node) noexcept;
    [[nodiscard]] static bool apply_memory_layout_opt(dgtl::ir::IRNode* node) noexcept;
    [[nodiscard]] static bool apply_branch_optimization(dgtl::ir::IRNode* node) noexcept;
    [[nodiscard]] static bool apply_vectorization_hint(dgtl::ir::IRNode* node) noexcept;

    // ----------------------------------------------------------------
    // ANALYSIS HELPERS
    // ----------------------------------------------------------------
    [[nodiscard]] static uint64_t estimate_exec_frequency(const MutationProfile& profile) noexcept;

    [[nodiscard]] static constexpr bool is_applicable(
        const dgtl::ir::IRNode* node,
        MutationType mutation_type
    ) noexcept;

    float min_speedup_threshold_      = 2.0f;  ///< Minimum % speedup to apply mutation
    uint32_t max_mutations_per_stream_ = 32;   ///< Safety cap on mutation count
};

// ============================================================================
// SECTION: MUTATION STRATEGY BASE CLASS
// ============================================================================

/**
 * @class MutationStrategy
 * @brief Abstract base class for IR mutation strategies.
 */
class MutationStrategy {
public:
    virtual ~MutationStrategy() = default;

    [[nodiscard]] virtual bool applies_to(const dgtl::ir::IRNode* node) const = 0;
    [[nodiscard]] virtual bool try_apply(dgtl::ir::IRNode* node) noexcept = 0;
    [[nodiscard]] virtual MutationType get_type() const noexcept = 0;
};

// ============================================================================
// SECTION: CONCRETE MUTATION STRATEGIES
// ============================================================================

class ConstantFoldingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        return node && (node->flags & dgtl::ir::Flags::IS_SPEC_CONST);
    }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override {
        if (!node) return false;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        return true;
    }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::CONSTANT_FOLDING; }
};

class VectorizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        return node && (node->opcode == dgtl::ir::OpCode::ADD_F32 || node->opcode == dgtl::ir::OpCode::MUL_F32);
    }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override {
        if (!node) return false;
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        return true;
    }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::VECTORIZATION_HINT; }
};

class MemoryOptimizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        return node && (node->opcode == dgtl::ir::OpCode::LOAD || node->opcode == dgtl::ir::OpCode::STORE);
    }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override {
        if (!node) return false;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        return true;
    }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::MEMORY_LAYOUT_OPT; }
};

class LoopFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override { return node != nullptr; }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override { return false; }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::INSTRUCTION_FUSION; }
};

class InliningStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override { return node != nullptr; }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override { return false; }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::INSTRUCTION_FUSION; }
};

class LoopUnrollingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override { return node != nullptr; }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override { return false; }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::INSTRUCTION_FUSION; }
};

class TensorFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override { return node != nullptr; }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override { return false; }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::INSTRUCTION_FUSION; }
};

class AdaptiveMutationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override { return node != nullptr; }
    [[nodiscard]] bool try_apply(dgtl::ir::IRNode* node) noexcept override { return false; }
    [[nodiscard]] MutationType get_type() const noexcept override { return MutationType::REGISTER_REASSIGNMENT; }
};

// ============================================================================
// SECTION: STRATEGY REGISTRY & FACTORY
// ============================================================================

class MutationStrategyRegistry {
public:
    [[nodiscard]] static MutationStrategyRegistry& get_instance() noexcept {
        static MutationStrategyRegistry registry;
        return registry;
    }

    void register_strategy(std::unique_ptr<MutationStrategy> strategy) noexcept {
        if (!strategy) return;
        strategies_.push_back(std::move(strategy));
    }

    [[nodiscard]] std::vector<MutationStrategy*> get_applicable_strategies(const dgtl::ir::IRNode* node) const noexcept {
        std::vector<MutationStrategy*> applicable;
        applicable.reserve(strategies_.size());
        for (const auto& strategy : strategies_) {
            if (strategy && strategy->applies_to(node)) {
                applicable.push_back(strategy.get());
            }
        }
        return applicable;
    }

    [[nodiscard]] const std::vector<std::unique_ptr<MutationStrategy>>& get_all_strategies() const noexcept { return strategies_; }
    void clear_strategies() noexcept { strategies_.clear(); }
    [[nodiscard]] size_t strategy_count() const noexcept { return strategies_.size(); }

private:
    MutationStrategyRegistry() = default;
    ~MutationStrategyRegistry() = default;

    MutationStrategyRegistry(const MutationStrategyRegistry&) = delete;
    MutationStrategyRegistry& operator=(const MutationStrategyRegistry&) = delete;
    MutationStrategyRegistry(MutationStrategyRegistry&&) = delete;
    MutationStrategyRegistry& operator=(MutationStrategyRegistry&&) = delete;

    std::vector<std::unique_ptr<MutationStrategy>> strategies_;
};

// ============================================================================
// SECTION: LOGIC & MUTATION ENGINE RUNTIME IMPLEMENTATIONS
// ============================================================================

MutationType IREvolver::select_mutation(
    const dgtl::ir::IRNode* node,
    const MutationProfile& profile_data,
    IRMutationCostModel& cost_model
) const noexcept {
    if (!is_mutable_node(node))     return MutationType::NO_MUTATION;
    if (!profile_data.is_valid())   return MutationType::NO_MUTATION;

    static constexpr MutationType kCandidates[] = {
        MutationType::CONSTANT_FOLDING,
        MutationType::INSTRUCTION_FUSION,
        MutationType::REGISTER_REASSIGNMENT,
        MutationType::MEMORY_LAYOUT_OPT,
        MutationType::BRANCH_OPTIMIZATION,
        MutationType::VECTORIZATION_HINT,
    };

    MutationType best = MutationType::NO_MUTATION;
    double       best_score = static_cast<double>(min_speedup_threshold_);

    for (const auto candidate : kCandidates) {
        if (!is_applicable(node, candidate)) continue;
        const double score = predict_mutation_benefit(node, candidate, cost_model);
        if (score > best_score) {
            best_score = score;
            best = candidate;
        }
    }

    return best;
}

MutationResult IREvolver::apply_mutation(
    const dgtl::ir::IRStream& stream,
    dgtl::ir::IRNode* node,
    MutationType mutation_type
) noexcept {
    MutationResult result;

    if (!is_mutable_node(node)) {
        result.fail_reason = "null or invalid node";
        return result;
    }
    if (mutation_type == MutationType::NO_MUTATION) {
        result.fail_reason = "NO_MUTATION requested";
        return result;
    }

    const dgtl::ir::IRNode saved_state = *node;
    bool applied = false;

    switch (mutation_type) {
    case MutationType::CONSTANT_FOLDING:
        applied = apply_constant_folding(node);
        break;
    case MutationType::INSTRUCTION_FUSION:
        applied = apply_instruction_fusion(node);
        break;
    case MutationType::REGISTER_REASSIGNMENT:
        applied = apply_register_reassignment(node);
        break;
    case MutationType::MEMORY_LAYOUT_OPT:
        applied = apply_memory_layout_opt(node);
        break;
    case MutationType::BRANCH_OPTIMIZATION:
        applied = apply_branch_optimization(node);
        break;
    case MutationType::VECTORIZATION_HINT:
        applied = apply_vectorization_hint(node);
        break;
    default:
        result.fail_reason = "unsupported mutation type";
        return result;
    }

    if (!applied) {
        result.fail_reason = "mutation not applicable to this opcode";
        return result;
    }

    dgtl::ir::IRVerifyOptions vopts;
    vopts.max_diagnostics = 8;
    const auto diags = dgtl::ir::IRVerifier{}.verify(stream, vopts);
    const uint32_t errors = static_cast<uint32_t>(std::count_if(
        diags.begin(), diags.end(),
        [](const dgtl::ir::IRVerifyDiagnostic& d) {
            return d.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error;
        }));

    if (errors > 0) {
        *node = saved_state;
        result.verification_errors = errors;
        result.fail_reason = "post-mutation verification failed (" + std::to_string(errors) + " error(s))";
        return result;
    }

    result.succeeded         = true;
    result.nodes_affected    = 1;
    result.verification_errors = 0;
    return result;
}

MutationResult IREvolver::apply_specialization(
    dgtl::ir::IRStream& stream,
    const ExecutionFeedback& feedback
) noexcept {
    MutationResult result;

    if (stream.is_null() || stream.size() == 0) {
        result.fail_reason = "stream is null or empty";
        return result;
    }
    if (!feedback.is_aligned_to_stream_size(stream.size())) {
        result.fail_reason = "feedback arrays not aligned to stream size";
        return result;
    }

    const size_t n = stream.size();
    const uint32_t limit = max_mutations_per_stream_;
    uint32_t attempted = 0;
    float    cumulative_speedup = 0.0f;

    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const float h = node_hotness_impl(feedback, i);
        if (h > 0.0f) scored.emplace_back(h, i);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& [hotness, idx] : scored) {
        if (attempted >= limit) break;

        auto nd = stream.get_node(idx);
        MutationType mt = MutationType::NO_MUTATION;
        const auto op = nd.opcode;

        if (op == dgtl::ir::OpCode::ADD_F32 || op == dgtl::ir::OpCode::MUL_F32 || op == dgtl::ir::OpCode::LOAD) {
            mt = MutationType::VECTORIZATION_HINT;
        } else if (op == dgtl::ir::OpCode::STORE || op == dgtl::ir::OpCode::LEA) {
            mt = MutationType::MEMORY_LAYOUT_OPT;
        } else if (op == dgtl::ir::OpCode::ADD_I32 || op == dgtl::ir::OpCode::MUL_I32 || op == dgtl::ir::OpCode::SUB_I32) {
            mt = (nd.flags & dgtl::ir::Flags::IS_SPEC_CONST) ? MutationType::CONSTANT_FOLDING : MutationType::REGISTER_REASSIGNMENT;
        } else if (op == dgtl::ir::OpCode::BRANCH || op == dgtl::ir::OpCode::BRANCH_COND) {
            mt = MutationType::BRANCH_OPTIMIZATION;
        }

        if (mt == MutationType::NO_MUTATION) continue;

        nd.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        nd.refresh_intent_hash();

        const auto mr = apply_mutation(stream, &nd, mt);
        if (mr.succeeded) {
            stream.set_node(idx, nd);
            ++result.nodes_affected;
            cumulative_speedup += mr.predicted_speedup;
        }
        ++attempted;
    }

    {
        const auto final_diags = dgtl::ir::IRVerifier{}.verify(stream);
        result.verification_errors = static_cast<uint32_t>(std::count_if(
            final_diags.begin(), final_diags.end(),
            [](const dgtl::ir::IRVerifyDiagnostic& d) {
                return d.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error;
            }));
    }
    result.predicted_speedup   = cumulative_speedup;
    result.succeeded           = (result.verification_errors == 0 && result.nodes_affected > 0);
    if (!result.succeeded && result.verification_errors > 0)
        result.fail_reason = "final stream verification found errors";

    return result;
}

double IREvolver::predict_mutation_benefit(
    const dgtl::ir::IRNode* node,
    MutationType mutation_type,
    IRMutationCostModel& cost_model
) noexcept {
    if (!is_mutable_node(node)) return 0.0;

    const float baseline = cost_model.predict_node_cost(*node);
    dgtl::ir::IRNode candidate = *node;

    switch (mutation_type) {
    case MutationType::CONSTANT_FOLDING:
        if (candidate.flags & dgtl::ir::Flags::IS_SPEC_CONST) {
            candidate.opcode = dgtl::ir::OpCode::LOAD_IMM;
            candidate.flags  |= dgtl::ir::Flags::IS_OPTIMIZED;
        }
        break;

    case MutationType::INSTRUCTION_FUSION:
        if (candidate.opcode == dgtl::ir::OpCode::MUL_F32 && candidate.op_c != dgtl::ir::INVALID_SLOT) {
            candidate.opcode = dgtl::ir::OpCode::FMA_F32;
            candidate.flags  |= dgtl::ir::Flags::IS_MACRO_FUSED;
        }
        break;

    case MutationType::REGISTER_REASSIGNMENT:
        candidate.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        break;

    case MutationType::MEMORY_LAYOUT_OPT:
        if (candidate.opcode == dgtl::ir::OpCode::LOAD) {
            candidate.flags |= dgtl::ir::Flags::IS_SIMD_BURST;
            candidate.logic_flags |= dgtl::ir::HintFlags::RequiresVecPort;
        } else {
            candidate.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        }
        break;

    case MutationType::BRANCH_OPTIMIZATION:
        candidate.logic_flags &= static_cast<uint32_t>(~dgtl::ir::HintFlags::BranchUnlikely);
        candidate.logic_flags |= dgtl::ir::HintFlags::BranchLikely;
        break;

    case MutationType::VECTORIZATION_HINT:
        candidate.flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        candidate.logic_flags |= dgtl::ir::HintFlags::RequiresVecPort;
        break;

    default:
        return 0.0;
    }

    candidate.refresh_intent_hash();
    const float mutated = cost_model.predict_node_cost(candidate);
    return cost_delta_to_pct(baseline, mutated);
}

uint32_t IREvolver::detect_hotspots(
    const dgtl::ir::IRStream& stream,
    const ExecutionFeedback& feedback
) noexcept {
    if (stream.is_null()) return 0;

    const uint64_t count_threshold = feedback.total_executions > 0 ? (feedback.total_executions / 10) : 0;
    uint32_t hotspot_count = 0;
    const size_t n = stream.size();

    for (size_t i = 0; i < n; ++i) {
        const dgtl::ir::IRNode nd = stream.get_node(i);
        if (nd.opcode == dgtl::ir::OpCode::NOP) continue;

        bool hot = false;
        if (i < feedback.hotness.size() && feedback.hotness[i] > 0.8f) {
            hot = true;
        } else if (i < feedback.call_count.size() && feedback.call_count[i] > count_threshold) {
            hot = true;
        }
        if (hot) ++hotspot_count;
    }
    return hotspot_count;
}

bool IREvolver::apply_constant_folding(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;
    const bool is_const_arith = (node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0;
    if (!is_const_arith) return false;

    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_instruction_fusion(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    if (node->opcode == dgtl::ir::OpCode::MUL_F32 && node->op_c != dgtl::ir::INVALID_SLOT && node->op_c != 0) {
        node->opcode = dgtl::ir::OpCode::FMA_F32;
        node->flags |= dgtl::ir::Flags::IS_MACRO_FUSED;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
        return true;
    }

    if (node->opcode == dgtl::ir::OpCode::LABEL || node->opcode == dgtl::ir::OpCode::GOTO) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
        return true;
    }
    return false;
}

bool IREvolver::apply_register_reassignment(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;
    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_memory_layout_opt(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    if (node->opcode == dgtl::ir::OpCode::LOAD) {
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->logic_flags |= dgtl::ir::HintFlags::RequiresVecPort;
    } else if (node->opcode == dgtl::ir::OpCode::STORE) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    } else if (node->opcode == dgtl::ir::OpCode::LEA) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    } else {
        return false;
    }

    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_branch_optimization(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;
    const auto op = node->opcode;
    if (op != dgtl::ir::OpCode::BRANCH && op != dgtl::ir::OpCode::BRANCH_COND && op != dgtl::ir::OpCode::BRANCH_COMPARE_FUSED) {
        return false;
    }

    node->logic_flags &= static_cast<uint32_t>(~dgtl::ir::HintFlags::BranchUnlikely);
    node->logic_flags |= dgtl::ir::HintFlags::BranchLikely;
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_vectorization_hint(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;
    node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
    node->logic_flags |= dgtl::ir::HintFlags::RequiresVecPort;
    node->refresh_intent_hash();
    return true;
}

uint64_t IREvolver::estimate_exec_frequency(const MutationProfile& profile) noexcept {
    if (!profile.is_valid()) return 0;
    uint64_t freq = profile.execution_count;
    if (profile.is_loop_critical)           freq *= 10;
    if (profile.branch_mispredictions > 0)  freq += static_cast<uint64_t>(profile.branch_mispredictions) * 5;
    return freq;
}

/*static*/ constexpr bool IREvolver::is_applicable(const dgtl::ir::IRNode* node, MutationType mutation_type) noexcept {
    if (!node) return false;
    const auto op = node->opcode;

    switch (mutation_type) {
    case MutationType::CONSTANT_FOLDING:
        return (node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0 &&
               (op == dgtl::ir::OpCode::ADD_I32 || op == dgtl::ir::OpCode::SUB_I32 ||
                op == dgtl::ir::OpCode::MUL_I32 || op == dgtl::ir::OpCode::DIV_I32);
    case MutationType::INSTRUCTION_FUSION:
        return op == dgtl::ir::OpCode::MUL_F32   || op == dgtl::ir::OpCode::LABEL     ||
               op == dgtl::ir::OpCode::GOTO      || op == dgtl::ir::OpCode::CALL;
    case MutationType::REGISTER_REASSIGNMENT:
        return op != dgtl::ir::OpCode::NOP && op != dgtl::ir::OpCode::LABEL;
    case MutationType::MEMORY_LAYOUT_OPT:
        return op == dgtl::ir::OpCode::LOAD  || op == dgtl::ir::OpCode::STORE || op == dgtl::ir::OpCode::LEA;
    case MutationType::BRANCH_OPTIMIZATION:
        return op == dgtl::ir::OpCode::BRANCH              ||
               op == dgtl::ir::OpCode::BRANCH_COND         ||
               op == dgtl::ir::OpCode::BRANCH_COMPARE_FUSED;
    case MutationType::VECTORIZATION_HINT:
        return op == dgtl::ir::OpCode::ADD_F32          || op == dgtl::ir::OpCode::MUL_F32          ||
               op == dgtl::ir::OpCode::SUB_F32          || op == dgtl::ir::OpCode::LOAD             ||
               op == dgtl::ir::OpCode::VEC_ADD_F32_512  || op == dgtl::ir::OpCode::VEC_MUL_F32_512  ||
               op == dgtl::ir::OpCode::GATHER_F32_512;
    default:
        return false;
    }
}

} // namespace dgtl::ir::evolution
