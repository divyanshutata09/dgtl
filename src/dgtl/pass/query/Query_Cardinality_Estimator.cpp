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

#include "dgtl/pass/query/Query_Cardinality_Estimator.hpp"
#include <algorithm>

namespace dgtl {
namespace pass {
namespace query {

QueryCardinalityEstimator::QueryCardinalityEstimator(
    dgtl::analysis::DGTLAICostModel& cost_model
) noexcept : ai_cost_model(&cost_model) {
}

uint64_t QueryCardinalityEstimator::estimate_from_cardinality(
    const QueryOperation& from_op,
    const QueryPlan& plan
) const noexcept {
    (void)from_op;  // Analyze FROM operation's table
    (void)plan;     // Use plan statistics
    return 1000;    // Default cardinality estimate
}

uint64_t QueryCardinalityEstimator::estimate_where_cardinality(
    const QueryOperation& where_op,
    uint64_t input_cardinality
) const noexcept {
    (void)where_op;  // Analyze WHERE predicate selectivity
    return std::max(static_cast<uint64_t>(1), input_cardinality / 2);
}

uint64_t QueryCardinalityEstimator::estimate_group_cardinality(
    const QueryOperation& group_op,
    uint64_t input_cardinality,
    const QueryPlan& plan
) const noexcept {
    (void)group_op;  // Analyze GROUP BY expression
    (void)plan;      // Use plan statistics for domain size
    return std::min(static_cast<uint64_t>(5000), input_cardinality);
}

uint64_t QueryCardinalityEstimator::estimate_join_cardinality(
    uint64_t left_cardinality,
    uint64_t right_cardinality,
    double join_selectivity
) const noexcept {
    if (left_cardinality == 0 || right_cardinality == 0) {
        return 0;
    }
    uint64_t result = static_cast<uint64_t>(
        static_cast<double>(left_cardinality) * 
        static_cast<double>(right_cardinality) * 
        join_selectivity
    );
    return std::max(result, static_cast<uint64_t>(1));
}

double QueryCardinalityEstimator::estimate_operation_cost(
    const QueryOperation& op,
    uint64_t input_cardinality
) const noexcept {
    (void)op;  // Analyze operation type for cost formula
    return static_cast<double>(input_cardinality) * 0.1;
}

void QueryCardinalityEstimator::learn_selectivity(
    uint64_t input_rows,
    uint64_t output_rows
) noexcept {
    if (input_rows > 0) {
        double selectivity = static_cast<double>(output_rows) / static_cast<double>(input_rows);
        selectivity = std::max(0.0, std::min(1.0, selectivity));
    }
}

} // namespace query
} // namespace pass
} // namespace dgtl
