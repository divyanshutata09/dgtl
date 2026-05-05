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

#include <array>
#include <cmath>
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/pass/query/Query_Plan.hpp"

namespace dgtl {
namespace pass {
namespace query {

/**
 * @class QueryCardinalityEstimator
 * @brief Estimates query result cardinalities using learned cost model
 * 
 * Extends DGTL's AI Cost Model for query-specific cardinality prediction.
 * Implements three-tier estimation strategy:
 * 1. Base model: Table statistics + operation type
 * 2. Selectivity model: WHERE clause impact
 * 3. Join model: Cross-table cardinality product
 */
class QueryCardinalityEstimator : public dgtl::analysis::DGTLAICostModel {
public:
    QueryCardinalityEstimator() noexcept = default;
    ~QueryCardinalityEstimator() = default;

    /**
     * @brief Initialize with reference to AI cost model
     * Reuses DGTL's proven cost prediction infrastructure
     */
    explicit QueryCardinalityEstimator(
        dgtl::analysis::DGTLAICostModel& ai_model
    ) noexcept;

    /**
     * @brief Estimate output cardinality for a FROM operation
     * Reads table statistics, returns table row count
     */
    [[nodiscard]] uint64_t estimate_from_cardinality(
        const QueryOperation& op,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after WHERE filtering
     * Uses selectivity model: output_rows = input_rows * selectivity
     */
    [[nodiscard]] uint64_t estimate_where_cardinality(
        const QueryOperation& op,
        uint64_t input_rows
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after GROUP BY
     * Uses distinct value estimation
     */
    [[nodiscard]] uint64_t estimate_group_cardinality(
        const QueryOperation& op,
        uint64_t input_rows,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Estimate output cardinality for JOIN operation
     * Uses join selectivity: output_rows = left_rows * right_rows * join_selectivity
     */
    [[nodiscard]] uint64_t estimate_join_cardinality(
        uint64_t left_rows,
        uint64_t right_rows,
        double join_selectivity = 1.0
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after ORDER BY
     * Order does not change cardinality, returns input unchanged
     */
    [[nodiscard]] uint64_t estimate_order_cardinality(
        uint64_t input_rows
    ) const noexcept {
        return input_rows;  // ORDER preserves cardinality
    }

    /**
     * @brief Estimate output cardinality after LIMIT
     */
    [[nodiscard]] uint64_t estimate_limit_cardinality(
        uint64_t input_rows,
        uint64_t limit_value
    ) const noexcept {
        return std::min(input_rows, limit_value);
    }

    /**
     * @brief Estimate total cost for executing operation
     * Uses AI cost model for cycle prediction
     */
    [[nodiscard]] double estimate_operation_cost(
        const QueryOperation& op,
        uint64_t input_cardinality
    ) const noexcept;

    /**
     * @brief Update model with runtime feedback
     * Called after operation executes to refine selectivity estimates
     */
    void update_with_feedback(
        QueryOperation& op,
        uint64_t actual_rows,
        double actual_latency_us
    ) noexcept;

    /**
     * @brief Estimate selectivity of a WHERE clause
     * Range: 0.0 (filters all) to 1.0 (filters none)
     * Uses heuristic: assumes ~10% selectivity by default, learns from feedback
     */
    [[nodiscard]] double estimate_selectivity(
        const QueryOperation& where_op
    ) const noexcept;

    /**
     * @brief Learn selectivity from observed data
     * Called during profiling phase to improve future estimates
     */
    void learn_selectivity(
        uint64_t input_rows,
        uint64_t output_rows
    ) noexcept;

    /**
     * @brief Get average observed selectivity across all learned operations
     */
    [[nodiscard]] double get_average_selectivity() const noexcept;

private:
    dgtl::analysis::DGTLAICostModel* ai_cost_model = nullptr;
    
    // Learning state
    std::vector<double> selectivity_history;  // Observed selectivities
    double default_selectivity = 0.1;          // 10% default (conservative)
    double join_selectivity = 0.01;            // 1% default for joins
    
    // Constants for cost prediction
    static constexpr double ROWS_PER_MICROSECOND = 1000.0;  // Throughput estimate
    static constexpr double JOIN_COST_MULTIPLIER = 2.0;      // Joins are expensive
};

} // namespace query
} // namespace pass
} // namespace dgtl
