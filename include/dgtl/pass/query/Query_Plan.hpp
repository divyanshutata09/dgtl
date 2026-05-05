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
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl {
namespace pass {
namespace query {

/**
 * @enum QueryOperationType
 * @brief Classification of query operations (MYZON Rule 78)
 */
enum class QueryOperationType : uint8_t {
    FROM = 0,           // Data source selection
    WHERE = 1,          // Predicate filtering
    GROUP = 2,          // Grouping aggregation
    ORDER = 3,          // Result ordering
    SELECT = 4,         // Projection
    JOIN = 5,           // Multi-table join
    LIMIT = 6,          // Result limitation
    WINDOW = 7,         // Window function
    UNION = 8,          // Set union
    INTERSECT = 9,      // Set intersection
};

/**
 * @struct TableStatistics
 * @brief Estimated table characteristics for cost model
 */
struct TableStatistics {
    uint64_t row_count = 0;
    uint64_t avg_row_size = 0;
    double selectivity = 1.0;           // Percentage of rows returned
    double distinct_values = 0.0;       // Cardinality for GROUP BY
    std::vector<double> column_selectivity;  // Per-column filtering estimate
};

/**
 * @struct QueryOperation
 * @brief Single operation in query execution plan
 */
struct QueryOperation {
    uint32_t operation_id = 0;
    QueryOperationType type;
    std::vector<uint32_t> input_ids;    // Predecessor operations
    std::vector<uint32_t> output_ids;   // Successor operations
    
    // Cost metrics (populated by cost model)
    double estimated_cost = 0.0;        // Predicted execution cycles
    uint64_t estimated_rows = 0;        // Cardinality estimate
    uint64_t estimated_bytes = 0;       // Output size estimate
    
    // Operation metadata
    std::string predicate;              // WHERE clause expression (IR reference)
    std::vector<uint32_t> groupby_cols; // GROUP BY column indices
    std::vector<uint32_t> orderby_cols; // ORDER BY column indices
    uint64_t limit_rows = UINT64_MAX;   // LIMIT clause value
    
    // Runtime feedback (from profiler)
    uint64_t actual_rows = 0;
    double actual_latency_us = 0.0;
    double actual_selectivity = 1.0;
};

/**
 * @class QueryPlan
 * @brief Represents optimizable query execution plan
 * 
 * Extracted from MYZON query_lib (Rule 78):
 * - Parses FROM/WHERE/GROUP/ORDER/SELECT structure
 * - Builds DAG of query operations
 * - Stores statistics and cost estimates
 * - Tracks optimization opportunities
 */
class QueryPlan {
public:
    QueryPlan() noexcept;
    ~QueryPlan() = default;

    /**
     * @brief Initialize plan from IR stream containing query operations
     */
    void initialize_from_stream(const ir::IRStream& stream) noexcept;

    /**
     * @brief Add a query operation to the plan
     * @param op_type Type of operation (FROM, WHERE, etc.)
     * @param inputs Input operation IDs
     * @return Operation ID for reference
     */
    [[nodiscard]] uint32_t add_operation(
        QueryOperationType op_type,
        const std::vector<uint32_t>& inputs = {}
    ) noexcept;

    /**
     * @brief Get operation by ID
     */
    [[nodiscard]] QueryOperation* get_operation(uint32_t op_id) noexcept;
    [[nodiscard]] const QueryOperation* get_operation(uint32_t op_id) const noexcept;

    /**
     * @brief Get all operations of specific type
     */
    [[nodiscard]] std::vector<QueryOperation*> get_operations_by_type(
        QueryOperationType type
    ) noexcept;

    /**
     * @brief Get topological sort order (execution order)
     */
    [[nodiscard]] std::vector<uint32_t> get_execution_order() const noexcept;

    /**
     * @brief Update table statistics (used by cardinality estimator)
     */
    void set_table_stats(uint32_t table_id, const TableStatistics& stats) noexcept {
        if (table_id < table_stats.size()) {
            table_stats[table_id] = stats;
        }
    }

    /**
     * @brief Get cardinality estimate after operation
     */
    [[nodiscard]] uint64_t estimate_cardinality_after(uint32_t op_id) const noexcept;

    /**
     * @brief Mark operation as has JOIN dependency
     */
    void mark_join_dependency(uint32_t op_id_left, uint32_t op_id_right) noexcept {
        join_dependencies.emplace_back(op_id_left, op_id_right);
    }

    /**
     * @brief Get all WHERE operations
     */
    [[nodiscard]] std::vector<uint32_t> get_where_operations() const noexcept;

    /**
     * @brief Get all JOIN operations and their dependencies
     */
    [[nodiscard]] const std::vector<std::pair<uint32_t, uint32_t>>& get_join_dependencies() const noexcept {
        return join_dependencies;
    }

    /**
     * @brief Validate plan integrity
     * @return true if plan is valid, false if inconsistent
     */
    [[nodiscard]] bool validate() const noexcept;

    /**
     * @brief Emit optimized plan back to IR stream
     */
    void emit_to_stream(ir::IRStream& stream) const noexcept;

    /**
     * @brief Get total estimated cost of plan
     */
    [[nodiscard]] double get_total_cost() const noexcept;

    /**
     * @brief Clear all operations and reset
     */
    void clear() noexcept {
        operations.clear();
        table_stats.clear();
        join_dependencies.clear();
        operation_counter = 0;
    }

private:
    std::vector<QueryOperation> operations;
    std::vector<TableStatistics> table_stats;
    std::vector<std::pair<uint32_t, uint32_t>> join_dependencies;
    uint32_t operation_counter = 0;

    // Helper methods
    void propagate_cardinality() noexcept;
    void detect_join_opportunities() noexcept;
};

} // namespace query
} // namespace pass
} // namespace dgtl
