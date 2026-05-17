/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 *
 * ============================================================================
 * CONSOLIDATED QUERY OPTIMIZATION MODULE
 * ============================================================================
 *
 * This unified header consolidates all query optimization functionality:
 *   - Query_Plan.hpp                    (Query plan data structures and execution DAG)
 *   - Query_Cardinality_Estimator.hpp   (Cardinality prediction using AI cost model)
 *   - Predicate_Pusher.hpp              (WHERE clause pushdown optimization)
 *   - Join_Order_Optimizer.hpp          (JOIN reordering for cost minimization)
 *   - Query_Optimizer.hpp               (Top-level orchestrator)
 *
 * Implements MYZON Rule 78 query optimization via a cohesive, single-namespace
 * architecture. Supports multi-stage optimization with cost model integration,
 * cardinality estimation, and formal verification.
 */

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <array>
#include <cmath>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager_Config.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/analysis/Liveness_Memory_Dependency_Unified.hpp"
#include "dgtl/ir/rewrite/Pattern_Rewriter.hpp"

namespace dgtl {
namespace pass {
namespace query {

// ============================================================================
// SECTION 1: FUNDAMENTAL QUERY TYPES & STRUCTURES
// ============================================================================

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
    double selectivity = 1.0;                    // Percentage of rows returned
    double distinct_values = 0.0;                // Cardinality for GROUP BY
    std::vector<double> column_selectivity;      // Per-column filtering estimate
};

/**
 * @struct QueryOperation
 * @brief Single operation in query execution plan
 */
struct QueryOperation {
    uint32_t operation_id = 0;
    QueryOperationType type;
    std::vector<uint32_t> input_ids;            // Predecessor operations
    std::vector<uint32_t> output_ids;           // Successor operations
    
    // Cost metrics (populated by cost model)
    double estimated_cost = 0.0;                // Predicted execution cycles
    uint64_t estimated_rows = 0;                // Cardinality estimate
    uint64_t estimated_bytes = 0;               // Output size estimate
    
    // Operation metadata
    std::string predicate;                      // WHERE clause expression (IR reference)
    std::vector<uint32_t> groupby_cols;         // GROUP BY column indices
    std::vector<uint32_t> orderby_cols;         // ORDER BY column indices
    uint64_t limit_rows = UINT64_MAX;           // LIMIT clause value
    
    // Runtime feedback (from profiler)
    uint64_t actual_rows = 0;
    double actual_latency_us = 0.0;
    double actual_selectivity = 1.0;
};

// ============================================================================
// SECTION 2: QUERY PLAN DATA STRUCTURE
// ============================================================================

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

// ============================================================================
// SECTION 3: CARDINALITY ESTIMATOR
// ============================================================================

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

// ============================================================================
// SECTION 4: PREDICATE PUSHER
// ============================================================================

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

    PredicatePusher(const PredicatePusher&) = delete;
    PredicatePusher& operator=(const PredicatePusher&) = delete;

    /**
     * @brief Execute predicate pushdown optimization
     * @param stream IR stream containing query operations
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
     * @enum PredicateType
     * @brief Classifies predicates into categories
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

    /**
     * @struct PushdownStats
     * @brief Statistics from predicate pushdown optimization
     */
    struct PushdownStats {
        uint32_t predicates_analyzed = 0;
        uint32_t predicates_pushed = 0;
        uint64_t estimated_rows_eliminated = 0;
    };

    [[nodiscard]] const PushdownStats& get_stats() const noexcept {
        return stats;
    }

private:
    PushdownStats stats;

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
};

// ============================================================================
// SECTION 5: JOIN ORDER OPTIMIZER
// ============================================================================

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

    JoinOrderOptimizer(const JoinOrderOptimizer&) = delete;
    JoinOrderOptimizer& operator=(const JoinOrderOptimizer&) = delete;

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
     * @struct OptimizationStats
     * @brief Statistics from join order optimization
     */
    struct OptimizationStats {
        uint32_t joins_analyzed = 0;
        uint32_t orderings_evaluated = 0;
        uint32_t orderings_reordered = 0;
        double cost_improvement_percent = 0.0;
    };

    [[nodiscard]] const OptimizationStats& get_stats() const noexcept {
        return stats;
    }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    OptimizationStats stats;

    // Constants
    static constexpr uint32_t MAX_JOINS_FOR_FULL_SEARCH = 10;
    static constexpr uint32_t NESTED_LOOP_THRESHOLD = 1000;
    static constexpr uint32_t HASH_JOIN_THRESHOLD = 100000000;
};

// ============================================================================
// SECTION 6: QUERY OPTIMIZER ORCHESTRATOR
// ============================================================================

/**
 * @class QueryOptimizer
 * @brief Top-level optimization pass for MYZON query operations (Rule 78)
 *
 * Orchestrates multi-stage query optimization:
 * 1. Extract query plan from IR (FROM/WHERE/GROUP/ORDER/SELECT)
 * 2. Apply predicate pushdown (WHERE clauses early)
 * 3. Optimize join order (minimize cost)
 * 4. Estimate cardinalities and costs
 * 5. Emit optimized plan back to IR
 *
 * Reuses DGTL infrastructure:
 * - AI Cost Model for cycle predictions
 * - Pass Decision Engine for pass selection
 * - Analysis infrastructure for dependency tracking
 * - Formal verification for safety checks
 *
 * Part of DGTL v1.0.2 production-grade optimization suite.
 */
class QueryOptimizer : public dgtl::pipeline::DGTLOptimizationPass {
public:
    /**
     * @brief Initialize query optimizer with dependencies
     */
    QueryOptimizer() noexcept = default;
    ~QueryOptimizer() override = default;

    QueryOptimizer(const QueryOptimizer&) = delete;
    QueryOptimizer& operator=(const QueryOptimizer&) = delete;

    /**
     * @brief Execute query optimization on IR stream
     * @param stream IR stream containing query operations
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Set the AI cost model to use
     * Must be called before execute() to enable cost-based optimization
     */
    void set_cost_model(dgtl::analysis::DGTLAICostModel* model) noexcept {
        cost_model = model;
    }

    /**
     * @brief Enable/disable specific optimization phases
     */
    void enable_predicate_pushdown(bool enable) noexcept {
        optimize_predicates = enable;
    }

    void enable_join_ordering(bool enable) noexcept {
        optimize_joins = enable;
    }

    void enable_projection_pushdown(bool enable) noexcept {
        optimize_projections = enable;
    }

    /**
     * @brief Perform complete query optimization pipeline
     *
     * Main entry point for optimization:
     * 1. Extract query structure
     * 2. Run sub-passes (predicates, joins, etc.)
     * 3. Validate optimized plan
     * 4. Emit back to IR
     *
     * @param stream Input IR stream
     * @return true if optimization succeeded, false if errors encountered
     */
    [[nodiscard]] bool optimize_query(ir::IRStream& stream) noexcept;

    /**
     * @enum OptimizationPhase
     * @brief Stages of query optimization
     */
    enum class OptimizationPhase : uint8_t {
        EXTRACT_PLAN = 0,       // Parse query from IR
        ANALYZE_STATISTICS = 1, // Gather table stats
        PREDICATE_PUSHDOWN = 2, // Push WHERE early
        JOIN_ORDERING = 3,      // Optimize JOINs
        PROJECTION_PUSHDOWN = 4,// Push SELECT early
        COST_ESTIMATION = 5,    // Estimate costs
        VALIDATION = 6,         // Verify correctness
        EMIT_PLAN = 7,          // Write back to IR
    };

    /**
     * @brief Get current optimization phase
     */
    [[nodiscard]] OptimizationPhase get_current_phase() const noexcept {
        return current_phase;
    }

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "QueryOptimizer";
    }

    /**
     * @struct OptimizationResults
     * @brief Results and metrics from query optimization
     */
    struct OptimizationResults {
        bool success = false;
        double cost_before = 0.0;
        double cost_after = 0.0;
        double improvement_percent = 0.0;
        
        uint32_t predicates_pushed = 0;
        uint32_t joins_reordered = 0;
        uint32_t projections_pushed = 0;
        
        std::string error_message;
    };

    /**
     * @brief Get results from last optimization run
     */
    [[nodiscard]] const OptimizationResults& get_results() const noexcept {
        return results;
    }

private:
    // Sub-pass instances
    std::unique_ptr<PredicatePusher> predicate_pusher;
    std::unique_ptr<JoinOrderOptimizer> join_optimizer;
    
    // Cost model (shared from DGTL pipeline)
    dgtl::analysis::DGTLAICostModel* cost_model = nullptr;
    
    // Cardinality estimator
    std::unique_ptr<QueryCardinalityEstimator> cardinality_estimator;
    
    // Control flags
    bool optimize_predicates = true;
    bool optimize_joins = true;
    bool optimize_projections = false;  // Advanced optimization
    bool enable_profiling = true;
    
    // State
    OptimizationPhase current_phase = OptimizationPhase::EXTRACT_PLAN;
    OptimizationResults results;

    // Helper methods for each phase
    [[nodiscard]] QueryPlan extract_query_plan(const ir::IRStream& stream) noexcept;
    void analyze_statistics(QueryPlan& plan, const ir::IRStream& stream) noexcept;
    void validate_plan(const QueryPlan& plan) noexcept;
    void emit_optimized_plan(const QueryPlan& plan, ir::IRStream& stream) noexcept;
};

} // namespace query
} // namespace pass
} // namespace dgtl
