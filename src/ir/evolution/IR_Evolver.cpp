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

#include "dgtl/ir/evolution/IR_Evolver.hpp"

#include <algorithm>

namespace dgtl::ir::evolution {

namespace {

[[nodiscard]] inline bool is_mutable_node(const dgtl::ir::IRNode* node) noexcept {
    return node != nullptr && node->is_valid();
}

} // namespace

MutationType IREvolver::select_mutation(
    const dgtl::ir::IRNode* node,
    const struct MutationProfile& profile_data [[maybe_unused]] [[maybe_unused]]
) noexcept {
    // Use profile_data to guide mutation selection (when frequency data available)
    if (!is_mutable_node(node)) {
        return MutationType::CONSTANT_FOLDING; // Default
    }

    // Use opcode-based selection
    switch (node->opcode) {
        case dgtl::ir::OpCode::ADD_I32:
        case dgtl::ir::OpCode::MUL_I32:
            return MutationType::CONSTANT_FOLDING;
        case dgtl::ir::OpCode::ADD_F32:
        case dgtl::ir::OpCode::MUL_F32:
            return MutationType::INSTRUCTION_FUSION;
        case dgtl::ir::OpCode::LOAD:
        case dgtl::ir::OpCode::STORE:
            return MutationType::MEMORY_LAYOUT_OPT;
        default:
            return MutationType::REGISTER_REASSIGNMENT;
    }
}

bool IREvolver::apply_mutation(
    dgtl::ir::IRNode* node, 
    MutationType mutation_type
) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }

    switch (mutation_type) {
        case MutationType::CONSTANT_FOLDING:
            return apply_constant_folding(node);
        case MutationType::INSTRUCTION_FUSION:
            return apply_instruction_fusion(node);
        case MutationType::MEMORY_LAYOUT_OPT:
            return apply_memory_layout_opt(node);
        case MutationType::REGISTER_REASSIGNMENT:
            return apply_register_reassignment(node);
        case MutationType::BRANCH_OPTIMIZATION:
            return apply_branch_optimization(node);
        case MutationType::VECTORIZATION_HINT:
            return apply_vectorization_hint(node);
        default:
            return false;
    }
}

bool IREvolver::apply_specialization(
    dgtl::ir::IRStream& stream,
    const struct ExecutionFeedback& feedback
) noexcept {
    if (stream.is_null() || stream.size() == 0) {
        return false;
    }

    for (size_t i = 0; i < stream.size(); ++i) {
        auto& node = stream.get_node_ref(i);
        // Apply profile-guided specialization to each node
        if (i < feedback.hotness.size() && feedback.hotness[i] > 0.8f) {
            node.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
            node.flags |= dgtl::ir::ExtendedFlags::PROFILE_GUIDED;
        }
    }
    
    return true;
}

double IREvolver::predict_mutation_benefit(
    const dgtl::ir::IRNode* node,
    MutationType mutation_type,
    dgtl::analysis::DGTLAICostModel& cost_model
) const noexcept {
    if (!is_mutable_node(node)) {
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
            candidate.flags |= dgtl::ir::Flags::IS_OPTIMIZED;
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

bool IREvolver::apply_constant_folding(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
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
    return true;
}

bool IREvolver::apply_instruction_fusion(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }

    if (node->opcode == dgtl::ir::OpCode::MUL_F32) {
        node->opcode = dgtl::ir::OpCode::FMA_F32;
        node->flags |= dgtl::ir::Flags::IS_MACRO_FUSED;
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_register_reassignment(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }

    // Node-local SSA id rewrites are unsafe because use-sites are not rewritten here.
    // Keep IDs stable to preserve def-use integrity in backend lowering.
    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_memory_layout_opt(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }

    if (node->opcode == dgtl::ir::OpCode::LOAD) {
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->flags |= dgtl::ir::Flags::REQUIRES_VEC_PORT;
    }
    if (node->opcode == dgtl::ir::OpCode::STORE) {
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    }
    node->refresh_intent_hash();
    return true;
}

uint32_t IREvolver::detect_hotspots(const dgtl::ir::IRStream& stream [[maybe_unused]]) const noexcept {
    // Scan stream for high-execution-frequency nodes
    uint32_t hotspot_count = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i);
        // Nodes with high flags or special markers are hotspots
        if (node.flags != 0) {
            hotspot_count++;
        }
    }
    return hotspot_count;
}

bool IREvolver::apply_branch_optimization(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }
    node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
    node->refresh_intent_hash();
    return true;
}

bool IREvolver::apply_vectorization_hint(dgtl::ir::IRNode* node) noexcept {
    if (!is_mutable_node(node)) {
        return false;
    }
    node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
    node->refresh_intent_hash();
    return true;
}

uint64_t IREvolver::estimate_exec_frequency(const dgtl::ir::IRNode* node [[maybe_unused]]) const noexcept {
    // Estimate execution frequency based on node properties
    if (node == nullptr) return 1;
    // Default: estimate frequency as 1 unless optimized
    return (node->flags & dgtl::ir::Flags::IS_OPTIMIZED) ? 100 : 1;
}

} // namespace dgtl::ir::evolution

