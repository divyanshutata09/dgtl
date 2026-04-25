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

/* * DGTL Backend vNext - IR Evolution Layer
 * Self-evolving IR based on runtime feedback
 */

#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::ir::evolution {

/**
 * @enum MutationType
 * @brief Types of IR mutations for evolution.
 */
enum class MutationType : uint8_t {
    CONSTANT_FOLDING,
    INSTRUCTION_FUSION,
    REGISTER_REASSIGNMENT,
    MEMORY_LAYOUT_OPT,
    BRANCH_OPTIMIZATION,
    VECTORIZATION_HINT
};

/**
 * @class IREvolver
 * @brief Evolves IR nodes based on runtime feedback and cost model predictions.
 * Enables the IR to self-specialize and adapt to workload patterns.
 */
class IREvolver {
public:
    /**
     * @brief Mutate an IR node based on runtime feedback.
     * @param node The IR node to evolve
     */
    void mutate(dgtl::ir::IRNode* node);

    /**
     * @brief Specialize an IR node for specific runtime conditions.
     * @param node The IR node to specialize
     * @param feedback Runtime execution feedback
     */
    void specialize(dgtl::ir::IRNode* node, const dgtl::analysis::RuntimeFeedback& feedback);

    /**
     * @brief Check if a mutation would be beneficial.
     * @param node The node to potentially mutate
     * @param mutation_type The type of mutation to consider
     * @param cost_model The AI cost model for prediction
     * @return Predicted benefit of the mutation
     */
    [[nodiscard]] double predict_mutation_benefit(
        const dgtl::ir::IRNode* node,
        MutationType mutation_type,
        dgtl::analysis::DGTLAICostModel& cost_model
    ) const;

private:
    /**
     * @brief Apply constant folding mutation.
     */
    void apply_constant_folding(dgtl::ir::IRNode* node);

    /**
     * @brief Apply instruction fusion mutation.
     */
    void apply_instruction_fusion(dgtl::ir::IRNode* node);

    /**
     * @brief Apply register reassignment for better allocation.
     */
    void apply_register_reassignment(dgtl::ir::IRNode* node);

    /**
     * @brief Apply memory layout optimization.
     */
    void apply_memory_layout_opt(dgtl::ir::IRNode* node);
};

} // namespace dgtl::ir::evolution

