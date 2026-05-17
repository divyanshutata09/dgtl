/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Version:    1.0.0 â€” IR EVOLUTION ENGINE IMPLEMENTATION [PRODUCTION-GRADE]
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/ir/evolver/IR_Evolver_Mutation.hpp"
#include "dgtl/ir/base/IR_Verifier.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"

#include <algorithm>
#include <string>

namespace dgtl::ir::evolution {

// ============================================================================
// SECTION: LOCAL HELPERS
// ============================================================================

namespace {

/// Return true if a node is non-null and structurally valid (is_valid() exists on IRNode).
[[nodiscard]] inline bool is_mutable_node(const dgtl::ir::IRNode* node) noexcept {
    return node != nullptr && node->is_valid();
}

/// Return the hotness score for node at index i, or 0.0 if not in feedback.
[[nodiscard]] inline float node_hotness(const ExecutionFeedback& fb, size_t i) noexcept {
    if (i < fb.hotness.size()) return fb.hotness[i];
    if (i < fb.call_count.size()) {
        // Normalize call count to 0-1 range using a 1000-execution anchor
        return static_cast<float>(fb.call_count[i]) /
               static_cast<float>(fb.total_executions > 0 ? fb.total_executions : 1);
    }
    return 0.0f;
}

/// Clamp a predicted cost difference to a % improvement.
/// baseline and mutated are absolute costs (higher = slower).
[[nodiscard]] inline double cost_delta_to_pct(float baseline, float mutated) noexcept {
    if (baseline <= 0.0f) return 0.0;
    return static_cast<double>((baseline - mutated) / baseline) * 100.0;
}

} // namespace

// ============================================================================
// SECTION: PRIMARY API IMPLEMENTATIONS
// ============================================================================

/**
 * select_mutation â€” choose the mutation type with the highest predicted benefit.
 *
 * Selection algorithm:
 *   1. Guard against invalid node / invalid profile (return NO_MUTATION)
 *   2. Enumerate all candidate mutation types applicable to this opcode
 *   3. Score each via predict_mutation_benefit
 *   4. Return the type with highest score > min_speedup_threshold_, or NO_MUTATION
 */
MutationType IREvolver::select_mutation(
    const dgtl::ir::IRNode* node,
    const MutationProfile& profile_data,
    dgtl::analysis::DGTLAICostModel& cost_model
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

/**
 * apply_mutation â€” apply a single validated mutation to one node.
 *
 * Protocol:
 *   1. Guard: null node â†’ fail
 *   2. Save full IRNode state (stack copy) for rollback
 *   3. Dispatch to type-specific private helper
 *   4. Run IR_Verifier on stream: if errors > 0, restore saved state and fail
 *   5. Return MutationResult
 */
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

    // Save original state for rollback
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
        // Helper decided not to apply â€” restore is a no-op (node unchanged)
        result.fail_reason = "mutation not applicable to this opcode";
        return result;
    }

    // Post-mutation verification: run flat-stream checks
    dgtl::ir::IRVerifyOptions vopts;
    vopts.max_diagnostics = 8; // fast fail
    const auto diags = dgtl::ir::ir_verify(stream, vopts);
    const uint32_t errors = dgtl::ir::ir_count_errors(diags);

    if (errors > 0) {
        // Rollback: restore original node state exactly
        *node = saved_state;
        result.verification_errors = errors;
        result.fail_reason = "post-mutation verification failed (" +
                             std::to_string(errors) + " error(s))";
        return result;
    }

    result.succeeded         = true;
    result.nodes_affected    = 1;
    result.verification_errors = 0;
    return result;
}

/**
 * apply_specialization â€” specialize the entire stream based on hotness feedback.
 *
 * Algorithm:
 *   1. Validate stream + feedback (alignment, total_executions > 0)
 *   2. Build a scratch vector of (index, hotness) pairs and sort descending
 *   3. For top-K hottest nodes (capped at max_mutations_per_stream_), attempt
 *      apply_mutation with VECTORIZATION_HINT (hottest) or MEMORY_LAYOUT_OPT
 *   4. After all mutations, run a final ir_is_valid check
 *   5. On failure, report but do NOT roll back individual mutations (caller may still use)
 */
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

    // Score each node by hotness
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const float h = node_hotness(feedback, i);
        if (h > 0.0f) scored.emplace_back(h, i);
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Apply mutations to hottest nodes up to the limit
    for (const auto& [hotness, idx] : scored) {
        if (attempted >= limit) break;

        auto& nd = stream.get_node_ref(idx);

        // Choose mutation type based on opcode class
        MutationType mt = MutationType::NO_MUTATION;
        const auto op = nd.opcode;

        if (op == dgtl::ir::OpCode::ADD_F32 ||
            op == dgtl::ir::OpCode::MUL_F32 ||
            op == dgtl::ir::OpCode::LOAD) {
            mt = MutationType::VECTORIZATION_HINT;
        } else if (op == dgtl::ir::OpCode::STORE ||
                   op == dgtl::ir::OpCode::LEA) {
            mt = MutationType::MEMORY_LAYOUT_OPT;
        } else if (op == dgtl::ir::OpCode::ADD_I32 ||
                   op == dgtl::ir::OpCode::MUL_I32 ||
                   op == dgtl::ir::OpCode::SUB_I32) {
            mt = (nd.flags & dgtl::ir::Flags::IS_SPEC_CONST) ?
                 MutationType::CONSTANT_FOLDING :
                 MutationType::REGISTER_REASSIGNMENT;
        } else if (op == dgtl::ir::OpCode::BRANCH     ||
                   op == dgtl::ir::OpCode::BRANCH_COND) {
            mt = MutationType::BRANCH_OPTIMIZATION;
        }

        if (mt == MutationType::NO_MUTATION) continue;

        // Mark hotspot membership in flags (no hash change needed for IS_TILING_OPTIMIZED)
        nd.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        nd.refresh_intent_hash();

        const auto mr = apply_mutation(stream, &nd, mt);
        if (mr.succeeded) {
            ++result.nodes_affected;
            cumulative_speedup += mr.predicted_speedup;
        }
        ++attempted;
    }

    // Final stream-level verification
    result.verification_errors = dgtl::ir::ir_count_errors(dgtl::ir::ir_verify(stream));
    result.predicted_speedup   = cumulative_speedup;
    result.succeeded           = (result.verification_errors == 0 && result.nodes_affected > 0);
    if (!result.succeeded && result.verification_errors > 0)
        result.fail_reason = "final stream verification found errors";

    return result;
}

/**
 * predict_mutation_benefit â€” estimate % speedup from applying a mutation.
 *
 * Strategy: simulate the mutation on a stack-local copy, query cost_model
 * for both versions, return the cost-delta as a % improvement.
 *
 * A positive result means the mutation reduces predicted cost.
 * A negative result means the mutation would be harmful.
 */
double IREvolver::predict_mutation_benefit(
    const dgtl::ir::IRNode* node,
    MutationType mutation_type,
    dgtl::analysis::DGTLAICostModel& cost_model
) const noexcept {
    if (!is_mutable_node(node)) return 0.0;

    const float baseline = cost_model.predict_node_cost(*node);
    dgtl::ir::IRNode candidate = *node;

    switch (mutation_type) {
    case MutationType::CONSTANT_FOLDING:
        if (candidate.flags & dgtl::ir::Flags::IS_SPEC_CONST) {
            // Simulate: a constant folded node becomes LOAD_IMM (low cost)
            candidate.opcode = dgtl::ir::OpCode::LOAD_IMM;
            candidate.flags  |= dgtl::ir::Flags::IS_OPTIMIZED;
        }
        break;

    case MutationType::INSTRUCTION_FUSION:
        // Only simulate FMA fusion if all three operands are valid
        if (candidate.opcode == dgtl::ir::OpCode::MUL_F32 &&
            candidate.op_c != dgtl::ir::INVALID_SLOT) {
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
            candidate.flags |= dgtl::ir::HintFlags::RequiresVecPort;
        } else {
            candidate.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        }
        break;

    case MutationType::BRANCH_OPTIMIZATION:
        // Simulate: clear unlikely hint, set likely (branch falls through more often)
        candidate.flags &= static_cast<uint16_t>(~dgtl::ir::HintFlags::BranchUnlikely);
        candidate.flags |= dgtl::ir::HintFlags::BranchLikely;
        break;

    case MutationType::VECTORIZATION_HINT:
        candidate.flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        candidate.flags |= dgtl::ir::HintFlags::RequiresVecPort;
        break;

    default:
        return 0.0;
    }

    candidate.refresh_intent_hash();
    const float mutated = cost_model.predict_node_cost(candidate);
    return cost_delta_to_pct(baseline, mutated);
}

/**
 * detect_hotspots â€” count distinct hot regions in the stream.
 *
 * A node is hot if:
 *   - hotness[i] > 0.8f  (in top 20% of execution frequency), OR
 *   - call_count[i] > (total_executions / 10) (in top 10% by absolute count)
 *
 * Returns the count of distinct hot nodes (not contiguous regions).
 */
uint32_t IREvolver::detect_hotspots(
    const dgtl::ir::IRStream& stream,
    const ExecutionFeedback& feedback
) const noexcept {
    if (stream.is_null()) return 0;

    const uint64_t count_threshold =
        feedback.total_executions > 0 ? (feedback.total_executions / 10) : 0;

    uint32_t hotspot_count = 0;
    const size_t n = stream.size();

    for (size_t i = 0; i < n; ++i) {
        const dgtl::ir::IRNode nd = stream.get_node(i);
        if (nd.opcode == dgtl::ir::OpCode::NOP) continue; // tombstones not hot

        bool hot = false;
        if (i < feedback.hotness.size() && feedback.hotness[i] > 0.8f) {
            hot = true;
        } else if (i < feedback.call_count.size() &&
                   feedback.call_count[i] > count_threshold) {
            hot = true;
        }
        if (hot) ++hotspot_count;
    }
    return hotspot_count;
}

// ============================================================================
// SECTION: MUTATION HELPER IMPLEMENTATIONS
// ============================================================================

/**
 * apply_constant_folding â€” mark IS_SPEC_CONST nodes for downstream folding.
 *
 * We do NOT perform arithmetic evaluation here because we cannot safely read
 * the actual constant values from op_a/op_b (they may be vreg IDs, not immediates,
 * unless IS_SPEC_CONST is set). Actual constant evaluation is performed by the
 * constant propagation pass that has access to the value table.
 *
 * What we do here: promote the node's flag to IS_OPTIMIZED so the folding pass
 * knows to prioritize it. This is a valid marker mutation.
 */
bool IREvolver::apply_constant_folding(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    // Only applicable to arithmetic ops with constant operands
    const bool is_const_arith = (node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0;
    if (!is_const_arith) return false;

    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

/**
 * apply_instruction_fusion â€” fuse MUL+ADD pairs into FMA.
 *
 * CRITICAL: We only fuse MUL_F32 â†’ FMA_F32 when op_c is already a valid
 * virtual register ID. Fusing without a valid addend would leave op_c as
 * INVALID_SLOT, which IR_Verifier::check_opcode_constraints() would catch
 * and flag as a constraint violation (Pass 7 FMA rule: all three operands required).
 */
bool IREvolver::apply_instruction_fusion(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    if (node->opcode == dgtl::ir::OpCode::MUL_F32 &&
        node->op_c != dgtl::ir::INVALID_SLOT &&
        node->op_c != 0) {
        // Safe to fuse: addend is present
        node->opcode = dgtl::ir::OpCode::FMA_F32;
        node->flags |= dgtl::ir::Flags::IS_MACRO_FUSED;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
        return true;
    }

    // LABEL / GOTO: mark for loop-fusion analysis in backend
    if (node->opcode == dgtl::ir::OpCode::LABEL ||
        node->opcode == dgtl::ir::OpCode::GOTO) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
        return true;
    }

    return false;
}

/**
 * apply_register_reassignment â€” hint allocator for better register placement.
 *
 * NOTE: We do NOT rewrite result_id or operand IDs here because doing so on a
 * single node without rewriting all use-sites would break def-use integrity.
 * That transformation requires a full renaming pass. Here we mark IS_OPTIMIZED
 * so the register allocator's hint pass deprioritizes spilling this node.
 */
bool IREvolver::apply_register_reassignment(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

/**
 * apply_memory_layout_opt â€” add prefetch/SIMD hints to memory ops.
 *
 * LOAD â†’ IS_SIMD_BURST + REQUIRES_VEC_PORT: signal backend to convert to vector load
 * STORE â†’ IS_OPTIMIZED: signal backend to consider non-temporal store
 * LEA  â†’ IS_OPTIMIZED: signal backend to combine with load addressing
 */
bool IREvolver::apply_memory_layout_opt(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    if (node->opcode == dgtl::ir::OpCode::LOAD) {
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->flags |= dgtl::ir::HintFlags::RequiresVecPort;
    } else if (node->opcode == dgtl::ir::OpCode::STORE) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    } else if (node->opcode == dgtl::ir::OpCode::LEA) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    } else {
        return false; // opcode not memory-related
    }

    node->refresh_intent_hash();
    return true;
}

/**
 * apply_branch_optimization â€” set branch likelihood hints for better prediction.
 *
 * Only applies to BRANCH / BRANCH_COND opcodes.
 * Sets BRANCH_LIKELY and clears BRANCH_UNLIKELY.
 *
 * NOTE: This is a conservative default (assume taken). Profile-guided callers
 * should first check the misprediction count from MutationProfile and only
 * set BRANCH_LIKELY if the taken rate is > 70%.
 */
bool IREvolver::apply_branch_optimization(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    const auto op = node->opcode;
    if (op != dgtl::ir::OpCode::BRANCH &&
        op != dgtl::ir::OpCode::BRANCH_COND &&
        op != dgtl::ir::OpCode::BRANCH_COMPARE_FUSED) {
        return false; // Only branch opcodes
    }

    node->flags &= static_cast<uint16_t>(~dgtl::ir::HintFlags::BranchUnlikely);
    node->flags |= dgtl::ir::HintFlags::BranchLikely;
    node->refresh_intent_hash();
    return true;
}

/**
 * apply_vectorization_hint â€” set SIMD/vector port flags on eligible ops.
 *
 * Sets both IS_SIMD_BURST (instruction is in a SIMD burst region) and
 * REQUIRES_VEC_PORT (backend must schedule on a vector execution port).
 * Both flags together are required for correct AVX-512 lowering.
 */
bool IREvolver::apply_vectorization_hint(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) return false;

    node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
    node->flags |= dgtl::ir::HintFlags::RequiresVecPort;
    node->refresh_intent_hash();
    return true;
}

// ============================================================================
// SECTION: ANALYSIS HELPER IMPLEMENTATIONS
// ============================================================================

/**
 * estimate_exec_frequency â€” synthesize an execution frequency from profile data.
 *
 * Combines:
 *   - Direct execution count (primary signal)
 *   - Loop criticality multiplier (Ã—10 if in a loop-critical region)
 *   - Misprediction weight (higher mispredictions â†’ effectively higher cost)
 */
uint64_t IREvolver::estimate_exec_frequency(const MutationProfile& profile) const noexcept {
    if (!profile.is_valid()) return 0;

    uint64_t freq = profile.execution_count;
    if (profile.is_loop_critical)       freq *= 10;
    if (profile.branch_mispredictions > 0)
        freq += static_cast<uint64_t>(profile.branch_mispredictions) * 5;
    return freq;
}

/**
 * is_applicable â€” fast pre-filter: does mutation_type make sense for this opcode?
 *
 * Called before cost model evaluation to avoid wasting expensive model queries
 * on mutations that can never apply to a given opcode class.
 */
/*static*/ constexpr bool IREvolver::is_applicable(
    const dgtl::ir::IRNode* node,
    MutationType mutation_type
) noexcept {
    if (!node) return false;
    const auto op = node->opcode;

    switch (mutation_type) {
    case MutationType::CONSTANT_FOLDING:
        return (node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0 &&
               (op == dgtl::ir::OpCode::ADD_I32 || op == dgtl::ir::OpCode::SUB_I32 ||
                op == dgtl::ir::OpCode::MUL_I32 || op == dgtl::ir::OpCode::DIV_I32);

    case MutationType::INSTRUCTION_FUSION:
        return op == dgtl::ir::OpCode::MUL_F32   ||
               op == dgtl::ir::OpCode::LABEL     ||
               op == dgtl::ir::OpCode::GOTO      ||
               op == dgtl::ir::OpCode::CALL;

    case MutationType::REGISTER_REASSIGNMENT:
        return op != dgtl::ir::OpCode::NOP &&
               op != dgtl::ir::OpCode::LABEL;

    case MutationType::MEMORY_LAYOUT_OPT:
        return op == dgtl::ir::OpCode::LOAD  ||
               op == dgtl::ir::OpCode::STORE ||
               op == dgtl::ir::OpCode::LEA;

    case MutationType::BRANCH_OPTIMIZATION:
        return op == dgtl::ir::OpCode::BRANCH              ||
               op == dgtl::ir::OpCode::BRANCH_COND         ||
               op == dgtl::ir::OpCode::BRANCH_COMPARE_FUSED;

    case MutationType::VECTORIZATION_HINT:
        return op == dgtl::ir::OpCode::ADD_F32          ||
               op == dgtl::ir::OpCode::MUL_F32          ||
               op == dgtl::ir::OpCode::SUB_F32          ||
               op == dgtl::ir::OpCode::LOAD             ||
               op == dgtl::ir::OpCode::VEC_ADD_F32_512  ||
               op == dgtl::ir::OpCode::VEC_MUL_F32_512  ||
               op == dgtl::ir::OpCode::GATHER_F32_512;

    default:
        return false;
    }
}

} // namespace dgtl::ir::evolution
