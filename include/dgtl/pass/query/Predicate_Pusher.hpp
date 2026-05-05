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
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pass/query/Query_Plan.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"

namespace dgtl {
namespace pass {
namespace query {

/**
 * @class PredicatePusher
 * @brief Pushes WHERE predicates as early as possible in query plan
 * 
 * Optimization for MYZON Rule 78 (query_lib):
 * - Identifies WHERE clauses that can be applied before JOIN/GROUP/ORDER
 * - Moves predicates down the plan tree (closer to data source)
 * - Reduces intermediate result sizes
 * - Critical for performance: filtering early = fewer rows to process
 * 
 * Uses DGTL's Memory_Dependency_Smasher to analyze dependencies:
 * - Checks if predicate depends on multiple tables
 * - Only pushes if predicate depends on current table alone
 */
class PredicatePusher : public dgtl::pipeline::DGTLOptimizationPass {
public:
    PredicatePusher() noexcept = default;
    ~PredicatePusher() override = default;

    /**
     * @brief Execute predicate pushdown optimization
     * @param stream IR stream containing query operations
     * @param verifier Formal verifier for dependency analysis
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Analyze query plan and push predicates
     * @param plan Query execution plan to optimize
     * @param smasher Memory dependency analyzer for predicate classification
     * @return Number of predicates successfully pushed
     */
    [[nodiscard]] uint32_t push_predicates(
        QueryPlan& plan,
        analysis::DGTLMemorySmasher* smasher = nullptr
    ) noexcept;

    /**
     * @brief Check if predicate can be pushed below operation
     * Returns false if predicate depends on results of operation
     */
    [[nodiscard]] bool can_push_below(
        const QueryOperation& predicate,
        const QueryOperation& target_op,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Classify predicate into categories
     * Helps determine pushdown opportunities
     */
    enum class PredicateType : uint8_t {
        COLUMN_FILTER,      // Direct column comparison: col > 5
        BETWEEN,            // Range filter: col BETWEEN 1 AND 10
        IN_LIST,            // Set membership: col IN (a, b, c)
        FUNCTION,           // Function-based: SUBSTR(col, 1, 3) = 'ABC'
        MULTI_TABLE,        // Cannot push: table1.col = table2.col
    };

    /**
     * @brief Determine predicate type
     */
    [[nodiscard]] PredicateType classify_predicate(
        const QueryOperation& op
    ) const noexcept;

    /**
     * @brief Get pass name for debugging
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "PredicatePusher";
    }

private:
    /**
     * @brief Find optimal insertion point for predicate
     * Walks up plan tree until reaching dependency
     */
    [[nodiscard]] uint32_t find_insertion_point(
        const QueryOperation& predicate,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Update operation connections after moving predicate
     */
    void update_connections(
        QueryPlan& plan,
        uint32_t predicate_id,
        uint32_t from_op_id,
        uint32_t to_op_id
    ) noexcept;

    // Statistics for optimization
    struct {
        uint32_t predicates_analyzed = 0;
        uint32_t predicates_pushed = 0;
        uint64_t estimated_rows_eliminated = 0;
    } stats;
};

} // namespace query
} // namespace pass
} // namespace dgtl
