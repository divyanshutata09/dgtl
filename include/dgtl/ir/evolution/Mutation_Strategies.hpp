/* * DGTL Backend vNext - Mutation Strategies
 * Strategies for evolving IR based on different optimization goals
 */

#pragma once
#include <functional>
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
    virtual bool applies_to(const dgtl::ir::IRNode* node) const = 0;

    /**
     * @brief Apply the mutation strategy to the node.
     */
    virtual void apply(dgtl::ir::IRNode* node) = 0;

    /**
     * @brief Get the type of mutation this strategy performs.
     */
    virtual MutationType get_type() const = 0;
};

/**
 * @class ConstantFoldingStrategy
 * @brief Strategy for constant folding optimizations.
 */
class ConstantFoldingStrategy : public MutationStrategy {
public:
    bool applies_to(const dgtl::ir::IRNode* node) const override;
    void apply(dgtl::ir::IRNode* node) override;
    MutationType get_type() const override { return MutationType::CONSTANT_FOLDING; }
};

/**
 * @class VectorizationStrategy
 * @brief Strategy for vectorization mutations.
 */
class VectorizationStrategy : public MutationStrategy {
public:
    bool applies_to(const dgtl::ir::IRNode* node) const override;
    void apply(dgtl::ir::IRNode* node) override;
    MutationType get_type() const override { return MutationType::VECTORIZATION_HINT; }
};

/**
 * @class MemoryOptimizationStrategy
 * @brief Strategy for memory layout optimizations.
 */
class MemoryOptimizationStrategy : public MutationStrategy {
public:
    bool applies_to(const dgtl::ir::IRNode* node) const override;
    void apply(dgtl::ir::IRNode* node) override;
    MutationType get_type() const override { return MutationType::MEMORY_LAYOUT_OPT; }
};

/**
 * @class MutationStrategyRegistry
 * @brief Registry of available mutation strategies.
 */
class MutationStrategyRegistry {
public:
    static MutationStrategyRegistry& get_instance();

    void register_strategy(std::unique_ptr<MutationStrategy> strategy);
    std::vector<MutationStrategy*> get_applicable_strategies(const dgtl::ir::IRNode* node) const;

private:
    std::vector<std::unique_ptr<MutationStrategy>> strategies_;
};

} // namespace dgtl::ir::evolution

