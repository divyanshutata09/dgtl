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

#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pass/query/Query_Plan.hpp"
#include "dgtl/pass/query/Query_Cardinality_Estimator.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace query {

/**
 * @class JoinOrderOptimizer
 * @brief Optimizes JOIN operation ordering using cost model
 * 
 * Optimization for MYZON Rule 78 (join operations in query_lib):
 * - Reorders JOIN operations to minimize total cost
 * - Uses dynamic programming for multi-table JOINs
 * - Selects join strategy (nested loop vs. hash vs. sort-merge)
 * - Leverages DGTL's AI Cost Model for cycle predictions
 * 
 * Algorithm:
 * 1. Extract all JOIN operations and tables
 * 2. For each possible ordering:
 *    - Estimate cardinality after each join
 *    - Compute total cost using AI model
 * 3. Select ordering with minimum cost
 * 4. Rewrite plan with new join order
 */
class JoinOrderOptimizer : public dgtl::pipeline::DGTLOptimizationPass {
public:
    /**
     * @brief Initialize with cost model
     */
    explicit JoinOrderOptimizer(
        dgtl::analysis::DGTLAICostModel* cost_model = nullptr
    ) noexcept : ai_model(cost_model) {}

    ~JoinOrderOptimizer() override = default;

    /**
     * @brief Execute join order optimization
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Analyze and optimize join ordering in query plan
     * @param plan Query plan containing JOIN operations
     * @param cardinality_est Cardinality estimator for predictions
     * @return Number of join orderings explored
     */
    [[nodiscard]] uint32_t optimize_join_order(
        QueryPlan& plan,
        QueryCardinalityEstimator& cardinality_est
    ) noexcept;

    /**
     * @enum JoinStrategy
     * @brief Different ways to execute a join
     */
    enum class JoinStrategy : uint8_t {
        NESTED_LOOP,        // O(n*m) - simple, good for small tables
        HASH_JOIN,          // O(n+m) - best for large tables, needs memory
        SORT_MERGE,         // O(n log n + m log m) - good for sorted data
        INDEX_LOOKUP,       // O(n log m) - requires index on right table
    };

    /**
     * @struct JoinPair
     * @brief Represents a single join operation between two tables
     */
    struct JoinPair {
        uint32_t left_op_id;
        uint32_t right_op_id;
        uint64_t left_cardinality;
        uint64_t right_cardinality;
        double estimated_cost = 0.0;
        JoinStrategy strategy = JoinStrategy::HASH_JOIN;
    };

    /**
     * @brief Select best join strategy for two tables
     * Uses cardinalities and available statistics
     */
    [[nodiscard]] JoinStrategy select_join_strategy(
        const JoinPair& join,
        QueryCardinalityEstimator& cardinality_est
    ) const noexcept;

    /**
     * @brief Estimate cost of specific join strategy
     */
    [[nodiscard]] double estimate_join_strategy_cost(
        const JoinPair& join,
        JoinStrategy strategy
    ) const noexcept;

    /**
     * @brief Generate all possible join orderings (for small numbers of joins)
     * Limits to n <= 10 to avoid exponential explosion
     */
    [[nodiscard]] std::vector<std::vector<JoinPair>> generate_join_orderings(
        const std::vector<JoinPair>& joins
    ) const noexcept;

    /**
     * @brief Compute total cost for a specific join ordering
     */
    [[nodiscard]] double compute_ordering_cost(
        const std::vector<JoinPair>& ordering,
        QueryCardinalityEstimator& cardinality_est
    ) const noexcept;

    /**
     * @brief Select best ordering from candidates
     */
    [[nodiscard]] std::vector<JoinPair> select_best_ordering(
        const std::vector<std::vector<JoinPair>>& candidates,
        QueryCardinalityEstimator& cardinality_est
    ) noexcept;

    /**
     * @brief Rewrite query plan with new join order
     */
    void apply_join_ordering(
        QueryPlan& plan,
        const std::vector<JoinPair>& new_ordering
    ) noexcept;

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "JoinOrderOptimizer";
    }

    /**
     * @brief Get statistics from last optimization run
     */
    struct OptimizationStats {
        uint32_t joins_analyzed = 0;
        uint32_t orderings_evaluated = 0;
        uint32_t orderings_reordered = 0;
        double cost_improvement_percent = 0.0;  // Improvement over default ordering
    };

    [[nodiscard]] const OptimizationStats& get_stats() const noexcept {
        return stats;
    }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    OptimizationStats stats;

    // Constants
    static constexpr uint32_t MAX_JOINS_FOR_FULL_SEARCH = 10;
    static constexpr uint32_t NESTED_LOOP_THRESHOLD = 1000;   // Max right table size
    static constexpr uint32_t HASH_JOIN_THRESHOLD = 100000000; // Max total size
};

} // namespace query
} // namespace pass
} // namespace dgtl
