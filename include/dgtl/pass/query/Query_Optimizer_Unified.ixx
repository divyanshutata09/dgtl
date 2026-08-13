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

module;

#include <cstdlib>

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <array>
#include <cmath>

// 🚀 FIXED: Bind directly to your new core intermediate macro sub-module space
export module dgtl.ir_core.query_optimizer;

// 🚀 FIXED CYCLIC LOOP: Import local core leaf siblings natively via short-form shortcuts
import dgtl.ir_core.pass_manager_config;
import dgtl.ir_core;
import dgtl.ir_node;
import dgtl.ir_enums;

export namespace dgtl {
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
    SCAN = FROM
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

    [[nodiscard]] double get_column_selectivity(size_t index) const noexcept {
        return (index < column_selectivity.size()) ? column_selectivity[index] : selectivity;
    }
};

/**
 * @struct QueryOperation
 * @brief Single operation in query execution plan
 */
struct QueryOperation {
    uint32_t operation_id = 0;
    QueryOperationType type{QueryOperationType::SCAN};
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

    [[nodiscard]] bool has_predicate() const noexcept { return !predicate.empty(); }
    [[nodiscard]] bool is_grouped() const noexcept { return !groupby_cols.empty(); }
    [[nodiscard]] bool is_ordered() const noexcept { return !orderby_cols.empty(); }
    [[nodiscard]] bool is_scan_or_from() const noexcept {
        return type == QueryOperationType::SCAN || type == QueryOperationType::FROM;
    }
    [[nodiscard]] bool has_inputs() const noexcept { return !input_ids.empty(); }
    [[nodiscard]] bool has_outputs() const noexcept { return !output_ids.empty(); }
};

// ============================================================================
// SECTION 2: QUERY PLAN DATA STRUCTURE
// ============================================================================

/**
 * @class QueryPlan
 * @brief Represents optimizable query execution plan
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

    /**
     * @brief Propagate cardinality estimates through the plan
     */
    void propagate_cardinality() noexcept;

private:
    std::vector<QueryOperation> operations;
    std::vector<TableStatistics> table_stats;
    std::vector<std::pair<uint32_t, uint32_t>> join_dependencies;
    uint32_t operation_counter = 0;
    void detect_join_opportunities() noexcept;
};

// ============================================================================
// SECTION 3: CARDINALITY ESTIMATOR
// ============================================================================

/**
 * @class QueryCardinalityEstimator
 * @brief Estimates query result cardinalities using learned cost model
 */
class QueryCardinalityEstimator : public dgtl::analysis::DGTLAICostModel {
public:
    QueryCardinalityEstimator() noexcept = default;
    ~QueryCardinalityEstimator() = default;

    /**
     * @brief Initialize with reference to AI cost model
     */
    explicit QueryCardinalityEstimator(
        dgtl::analysis::DGTLAICostModel& ai_model
    ) noexcept;

    /**
     * @brief Estimate output cardinality for a FROM operation
     */
    [[nodiscard]] uint64_t estimate_from_cardinality(
        const QueryOperation& op,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after WHERE filtering
     */
    [[nodiscard]] uint64_t estimate_where_cardinality(
        const QueryOperation& op,
        uint64_t input_rows
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after GROUP BY
     */
    [[nodiscard]] uint64_t estimate_group_cardinality(
        const QueryOperation& op,
        uint64_t input_rows,
        const QueryPlan& plan
    ) const noexcept;

    /**
     * @brief Estimate output cardinality for JOIN operation
     */
    [[nodiscard]] uint64_t estimate_join_cardinality(
        uint64_t left_rows,
        uint64_t right_rows,
        double join_selectivity = 1.0
    ) const noexcept;

    /**
     * @brief Estimate output cardinality after ORDER BY
     */
    [[nodiscard]] static uint64_t estimate_order_cardinality(
        uint64_t input_rows
    ) noexcept {
        return input_rows;
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
     */
    [[nodiscard]] double estimate_operation_cost(
        const QueryOperation& op,
        uint64_t input_cardinality
    ) const noexcept;

    /**
     * @brief Update model with runtime feedback
     */
    void update_with_feedback(
        QueryOperation& op,
        uint64_t actual_rows,
        double actual_latency_us
    ) noexcept;

    /**
     * @brief Estimate selectivity of a WHERE clause
     */
    [[nodiscard]] double estimate_selectivity(
        const QueryOperation& where_op
    ) const noexcept;

    /**
     * @brief Learn selectivity from observed data
     */
    void learn_selectivity(
        uint64_t input_rows,
        uint64_t output_rows
    ) noexcept;

    /**
     * @brief Get average observed selectivity across all learned operations
     */
    [[nodiscard]] double get_average_selectivity() const noexcept;

    [[nodiscard]] const std::vector<double>& get_selectivity_history() const noexcept {
        return selectivity_history;
    }

    [[nodiscard]] static constexpr double rows_per_microsecond() noexcept { return ROWS_PER_MICROSECOND; }
    [[nodiscard]] static constexpr double join_cost_multiplier() noexcept { return JOIN_COST_MULTIPLIER; }

private:
    dgtl::analysis::DGTLAICostModel* ai_cost_model = nullptr;
    
    // Learning state
    std::vector<double> selectivity_history;
    double default_selectivity = 0.1;
    double join_selectivity = 0.01;
    
    static constexpr double ROWS_PER_MICROSECOND = 1000.0;
    static constexpr double JOIN_COST_MULTIPLIER = 2.0;
};

// ============================================================================
// SECTION 4: PREDICATE PUSHER
// ============================================================================

/**
 * @class PredicatePusher
 * @brief Pushes WHERE predicates as early as possible in query plan
 */
class PredicatePusher : public dgtl::pipeline::DGTLOptimizationPass {
public:
    PredicatePusher() noexcept = default;
    ~PredicatePusher() override = default;

    PredicatePusher(const PredicatePusher&) = delete;
    PredicatePusher& operator=(const PredicatePusher&) = delete;

    /**
     * @brief Execute predicate pushdown optimization
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Analyze query plan and push predicates
     */
    [[nodiscard]] uint32_t push_predicates(
        QueryPlan& plan,
        analysis::DGTLMemorySmasher* smasher = nullptr
    ) noexcept;

    /**
     * @brief Check if predicate can be pushed below operation
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
        COLUMN_FILTER,      // Direct column comparison
        BETWEEN,            // Range filter
        IN_LIST,            // Set membership
        FUNCTION,           // Function-based
        MULTI_TABLE,        // Cannot push
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
        NESTED_LOOP,        // O(n*m)
        HASH_JOIN,          // O(n+m)
        SORT_MERGE,         // O(n log n + m log m)
        INDEX_LOOKUP,       // O(n log m)
    };

    /**
     * @struct JoinPair
     * @brief Represents a single join operation between two tables
     */
    struct JoinPair {
        uint32_t left_op_id{0};
        uint32_t right_op_id{0};
        uint64_t left_cardinality{0};
        uint64_t right_cardinality{0};
        double estimated_cost = 0.0;
        JoinStrategy strategy = JoinStrategy::HASH_JOIN;

        [[nodiscard]] uint64_t total_input_cardinality() const noexcept {
            return left_cardinality + right_cardinality;
        }
        [[nodiscard]] bool is_valid_pair() const noexcept {
            return left_op_id != right_op_id;
        }
    };

    /**
     * @brief Select best join strategy for two tables
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

    [[nodiscard]] static constexpr uint32_t max_joins_for_full_search() noexcept { return MAX_JOINS_FOR_FULL_SEARCH; }
    [[nodiscard]] static constexpr uint32_t nested_loop_threshold() noexcept { return NESTED_LOOP_THRESHOLD; }
    [[nodiscard]] static constexpr uint32_t hash_join_threshold() noexcept { return HASH_JOIN_THRESHOLD; }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    OptimizationStats stats;

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
 */
class QueryOptimizer : public dgtl::pipeline::DGTLOptimizationPass {
public:
    QueryOptimizer() noexcept = default;
    ~QueryOptimizer() override = default;

    QueryOptimizer(const QueryOptimizer&) = delete;
    QueryOptimizer& operator=(const QueryOptimizer&) = delete;

    /**
     * @brief Execute query optimization on IR stream
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Set the AI cost model to use
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

        [[nodiscard]] std::string_view get_error_message() const noexcept {
            return error_message;
        }
    };

    /**
     * @brief Get results from last optimization run
     */
    [[nodiscard]] const OptimizationResults& get_results() const noexcept {
        return results;
    }

    [[nodiscard]] const PredicatePusher* get_predicate_pusher() const noexcept {
        return predicate_pusher.get();
    }

    [[nodiscard]] const JoinOrderOptimizer* get_join_optimizer() const noexcept {
        return join_optimizer.get();
    }

    [[nodiscard]] const QueryCardinalityEstimator* get_cardinality_estimator() const noexcept {
        return cardinality_estimator.get();
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
    bool optimize_projections = false;
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