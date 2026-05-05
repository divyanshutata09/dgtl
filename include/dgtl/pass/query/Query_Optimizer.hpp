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

#include <memory>
#include <cstdint>
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pass/query/Query_Plan.hpp"
#include "dgtl/pass/query/Query_Cardinality_Estimator.hpp"
#include "dgtl/pass/query/Predicate_Pusher.hpp"
#include "dgtl/pass/query/Join_Order_Optimizer.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace query {

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

    /**
     * @brief Execute query optimization on IR stream
     * @param stream IR stream containing query operations
     * @param verifier Optional formal verifier for safety checks
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
