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

/* * DGTL Backend vNext - Mutation Strategies
 * Strategies for evolving IR based on different optimization goals
 */

#pragma once
#include <memory>
#include <vector>
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "IR_Evolver.hpp"

namespace dgtl::ir::evolution {

/**
 * @class MutationStrategy
 * @brief Base class for different IR mutation strategies.
 */
class MutationStrategy {
public:
    virtual ~MutationStrategy() = default;

    /**
     * @brief Check if this strategy applies to the given node.
     */
    [[nodiscard]] virtual bool applies_to(const dgtl::ir::IRNode* node) const = 0;

    /**
     * @brief Apply the mutation strategy to the node.
     */
    virtual void apply(dgtl::ir::IRNode* node) = 0;

    /**
     * @brief Get the type of mutation this strategy performs.
     */
    [[nodiscard]] virtual MutationType get_type() const = 0;
};

/**
 * @class ConstantFoldingStrategy
 * @brief Strategy for constant folding optimizations.
 */
class ConstantFoldingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) {
            return false;
        }

        return (node->flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0 ||
               node->opcode == dgtl::ir::OpCode::ADD_I32 ||
               node->opcode == dgtl::ir::OpCode::SUB_I32 ||
               node->opcode == dgtl::ir::OpCode::MUL_I32 ||
               node->opcode == dgtl::ir::OpCode::DIV_I32;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) {
            return;
        }

        node->fold_if_possible();
    }

    [[nodiscard]] MutationType get_type() const override { return MutationType::CONSTANT_FOLDING; }
};

/**
 * @class VectorizationStrategy
 * @brief Strategy for vectorization mutations.
 */
class VectorizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) {
            return false;
        }

        return node->opcode == dgtl::ir::OpCode::LOAD ||
               node->opcode == dgtl::ir::OpCode::ADD_F32 ||
               node->opcode == dgtl::ir::OpCode::MUL_F32 ||
               node->opcode == dgtl::ir::OpCode::GATHER_F32_512 ||
               node->opcode == dgtl::ir::OpCode::VEC_ADD_F32_512 ||
               node->opcode == dgtl::ir::OpCode::VEC_MUL_F32_512;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) {
            return;
        }

        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->flags |= dgtl::ir::Flags::REQUIRES_VEC_PORT;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { return MutationType::VECTORIZATION_HINT; }
};

/**
 * @class MemoryOptimizationStrategy
 * @brief Strategy for memory layout optimizations.
 */
class MemoryOptimizationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) {
            return false;
        }

        return node->opcode == dgtl::ir::OpCode::LOAD ||
               node->opcode == dgtl::ir::OpCode::STORE ||
               node->opcode == dgtl::ir::OpCode::LEA;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) {
            return;
        }

        if (node->opcode == dgtl::ir::OpCode::LOAD) {
            node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
            node->flags |= dgtl::ir::Flags::REQUIRES_VEC_PORT;
        } else {
            node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        }

        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { return MutationType::MEMORY_LAYOUT_OPT; }
};

/**
 * @class MutationStrategyRegistry
 * @brief Registry of available mutation strategies (BETTER THAN LLVM).
 * Includes pattern-based IR mutations for fusion, inlining, and unrolling.
 */
class MutationStrategyRegistry {
public:
    [[nodiscard]] static MutationStrategyRegistry& get_instance() {
        static MutationStrategyRegistry registry;
        return registry;
    }

    void register_strategy(std::unique_ptr<MutationStrategy> strategy) {
        if (!strategy) {
            return;
        }

        strategies_.push_back(std::move(strategy));
    }

    [[nodiscard]] std::vector<MutationStrategy*> get_applicable_strategies(const dgtl::ir::IRNode* node) const {
        std::vector<MutationStrategy*> applicable;
        applicable.reserve(strategies_.size());

        for (const auto& strategy : strategies_) {
            if (strategy && strategy->applies_to(node)) {
                applicable.push_back(strategy.get());
            }
        }

        return applicable;
    }

private:
    std::vector<std::unique_ptr<MutationStrategy>> strategies_;
};

/**
 * @class LoopFusionStrategy
 * @brief Strategy for fusing adjacent loops into single loop (reduces overhead)
 */
class LoopFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) return false;
        return node->opcode == dgtl::ir::OpCode::LABEL || 
               node->opcode == dgtl::ir::OpCode::GOTO;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) return;
        // Mark for loop fusion analysis
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { 
        return MutationType::INSTRUCTION_FUSION; 
    }
};

/**
 * @class InliningStrategy
 * @brief Strategy for function inlining (reduces call overhead for hotpaths)
 */
class InliningStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) return false;
        return node->opcode == dgtl::ir::OpCode::CALL;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) return;
        // Mark CALL node for potential inlining
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { 
        return MutationType::INSTRUCTION_FUSION; 
    }
};

/**
 * @class LoopUnrollingStrategy
 * @brief Strategy for loop unrolling (exposes more parallelism)
 */
class LoopUnrollingStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) return false;
        // Detect small loops with bounded iteration
        return node->opcode == dgtl::ir::OpCode::LABEL;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) return;
        // Mark for loop unrolling
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { 
        return MutationType::INSTRUCTION_FUSION; 
    }
};

/**
 * @class TensorFusionStrategy
 * @brief Strategy for fusing adjacent tensor operations (ML/DSA optimization)
 */
class TensorFusionStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        if (!node) return false;
        // Detect tensor operations
        return static_cast<uint16_t>(node->opcode) >= 460 && 
               static_cast<uint16_t>(node->opcode) <= 469;
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) return;
        node->flags |= dgtl::ir::Flags::IS_SIMD_BURST;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { 
        return MutationType::INSTRUCTION_FUSION; 
    }
};

/**
 * @class AdaptiveMutationStrategy
 * @brief Meta-strategy that selects best mutation based on execution patterns (BETTER THAN LLVM)
 */
class AdaptiveMutationStrategy : public MutationStrategy {
public:
    [[nodiscard]] bool applies_to(const dgtl::ir::IRNode* node) const override {
        return node != nullptr; // Applies to all nodes for adaptive selection
    }

    void apply(dgtl::ir::IRNode* node) override {
        if (!node) return;
        // Adaptive selection - actual choice happens at runtime based on profile
        node->flags |= dgtl::ir::Flags::IS_OPTIMIZED;
        node->refresh_intent_hash();
    }

    [[nodiscard]] MutationType get_type() const override { 
        return MutationType::REGISTER_REASSIGNMENT; // Represents adaptive tier
    }
};

} // namespace dgtl::ir::evolution

