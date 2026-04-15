#include "dgtl/ir/evolution/IR_Evolver.hpp"

#include <algorithm>

namespace dgtl::ir::evolution {

void IREvolver::mutate(dgtl::ir::IRNode* node) {
    if (!node) {
        return;
    }

    switch (node->opcode) {
        case dgtl::ir::OpCode::ADD_I32:
        case dgtl::ir::OpCode::MUL_I32:
            apply_constant_folding(node);
            break;
        case dgtl::ir::OpCode::ADD_F32:
        case dgtl::ir::OpCode::MUL_F32:
            apply_instruction_fusion(node);
            break;
        case dgtl::ir::OpCode::LOAD:
        case dgtl::ir::OpCode::STORE:
            apply_memory_layout_opt(node);
            break;
        default:
            apply_register_reassignment(node);
            break;
    }
}

void IREvolver::specialize(dgtl::ir::IRNode* node, const dgtl::analysis::RuntimeFeedback& feedback) {
    if (!node) {
        return;
    }

    // Hot paths with low miss-rate can be marked for aggressive specialization.
    if (feedback.execution_count > 64 && feedback.cache_miss_rate < 0.10) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }

    // Highly stable value-paths become speculative constants.
    if (feedback.branch_mispredict_rate < 0.03 && feedback.actual_latency < 20.0) {
        node->flags |= dgtl::ir::Flags::IS_SPEC_CONST;
        apply_constant_folding(node);
    }

    // Memory-heavy behavior: avoid dangerous rewriting, keep as hosted path.
    if (feedback.cache_miss_rate > 0.25) {
        node->flags |= dgtl::ir::Flags::IS_HOSTED;
    }

    node->refresh_intent_hash();
}

double IREvolver::predict_mutation_benefit(
    const dgtl::ir::IRNode* node,
    MutationType mutation_type,
    dgtl::analysis::DGTLAICostModel& cost_model
) const {
    if (!node) {
        return 0.0;
    }

    const float baseline = cost_model.predict_node_cost(*node);
    dgtl::ir::IRNode candidate = *node;

    switch (mutation_type) {
        case MutationType::CONSTANT_FOLDING:
            if (candidate.flags & dgtl::ir::Flags::IS_SPEC_CONST) {
                candidate.fold_if_possible();
            }
            break;
        case MutationType::INSTRUCTION_FUSION:
            if (candidate.opcode == dgtl::ir::OpCode::ADD_F32) {
                candidate.opcode = dgtl::ir::OpCode::FMA_F32;
                candidate.flags |= dgtl::ir::Flags::IS_MACRO_FUSED;
            }
            break;
        case MutationType::REGISTER_REASSIGNMENT:
            candidate.result_id = (candidate.result_id % 16U) + 1U;
            break;
        case MutationType::MEMORY_LAYOUT_OPT:
            if (candidate.opcode == dgtl::ir::OpCode::LOAD) {
                candidate.flags |= dgtl::ir::Flags::IS_SIMD_BURST;
            }
            break;
        case MutationType::BRANCH_OPTIMIZATION:
            candidate.flags |= dgtl::ir::Flags::BRANCH_LIKELY;
            candidate.flags &= ~dgtl::ir::Flags::BRANCH_UNLIKELY;
            break;
        case MutationType::VECTORIZATION_HINT:
            candidate.flags |= dgtl::ir::Flags::IS_SIMD_BURST;
            candidate.flags |= dgtl::ir::Flags::REQUIRES_VEC_PORT;
            break;
    }

    candidate.refresh_intent_hash();
    const float mutated = cost_model.predict_node_cost(candidate);
    return static_cast<double>(baseline - mutated);
}

void IREvolver::apply_constant_folding(dgtl::ir::IRNode* node) {
    if (!node) {
        return;
    }

    const bool both_operands_equal = node->op_a == node->op_b;
    if (node->opcode == dgtl::ir::OpCode::ADD_I32 && both_operands_equal) {
        node->opcode = dgtl::ir::OpCode::MUL_I32;
        node->op_b = 2;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }
    if ((node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0) {
        node->fold_if_possible();
    }
    node->refresh_intent_hash();
}

void IREvolver::apply_instruction_fusion(dgtl::ir::IRNode* node) {
    if (!node) {
        return;
    }

    if (node->opcode == dgtl::ir::OpCode::MUL_F32) {
        node->opcode = dgtl::ir::OpCode::FMA_F32;
        node->flags |= dgtl::ir::Flags::IS_MACRO_FUSED;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }
    node->refresh_intent_hash();
}

void IREvolver::apply_register_reassignment(dgtl::ir::IRNode* node) {
    if (!node) {
        return;
    }

    // Node-local SSA id rewrites are unsafe because use-sites are not rewritten here.
    // Keep IDs stable to preserve def-use integrity in backend lowering.
    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
}

void IREvolver::apply_memory_layout_opt(dgtl::ir::IRNode* node) {
    if (!node) {
        return;
    }

    if (node->opcode == dgtl::ir::OpCode::LOAD) {
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->flags |= dgtl::ir::Flags::REQUIRES_VEC_PORT;
    }
    if (node->opcode == dgtl::ir::OpCode::STORE) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }
    node->refresh_intent_hash();
}

} // namespace dgtl::ir::evolution

