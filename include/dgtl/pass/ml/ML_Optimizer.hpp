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
#include "dgtl/pass/ml/Tensor_Fusion.hpp"
#include "dgtl/pass/ml/Tensor_Specializer.hpp"
#include "dgtl/pass/ml/ML_Backend_Dispatcher.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace ml {

/**
 * @class MLOptimizer
 * @brief Top-level optimization pass for MYZON model operations (Rule 79)
 * 
 * Orchestrates multi-stage ML model optimization:
 * 1. Extract model structure from IR (layer definitions, tensor operations)
 * 2. Optimize tensor operations (fusion, specialization)
 * 3. Select optimal execution backends
 * 4. Emit optimized model back to IR
 * 
 * Reuses DGTL infrastructure:
 * - AI Cost Model for performance predictions
 * - Pass Decision Engine for pass selection
 * - Multi-emitter system for backend code generation
 * - Tensor detection and ISA support
 * 
 * Part of DGTL v1.0.2 production-grade ML optimization suite.
 */
class MLOptimizer : public dgtl::pipeline::DGTLOptimizationPass {
public:
    /**
     * @brief Initialize ML optimizer with dependencies
     */
    MLOptimizer() noexcept = default;
    ~MLOptimizer() override = default;

    /**
     * @brief Execute ML optimization on IR stream
     * @param stream IR stream containing model operations
     * @param verifier Optional formal verifier for safety checks
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Set the AI cost model to use
     * Must be called before execute() to enable cost-based optimization
     */
    void set_cost_model(dgtl::analysis::DGTLAICostModel* model) noexcept;

    /**
     * @brief Enable/disable specific optimization phases
     */
    void enable_tensor_fusion(bool enable) noexcept {
        optimize_fusion = enable;
    }

    void enable_tensor_specialization(bool enable) noexcept {
        optimize_specialization = enable;
    }

    void enable_backend_selection(bool enable) noexcept {
        optimize_backend_dispatch = enable;
    }

    /**
     * @brief Enable runtime profiling for specialization
     * Collects shape data during model execution for specialization
     */
    void enable_runtime_profiling(bool enable) noexcept {
        runtime_profiling = enable;
    }

    /**
     * @brief Perform complete ML optimization pipeline
     * 
     * Main entry point for optimization:
     * 1. Extract model structure
     * 2. Run sub-passes (fusion, specialization, dispatch)
     * 3. Validate optimized model
     * 4. Emit back to IR
     * 
     * @param stream Input IR stream
     * @return true if optimization succeeded, false if errors encountered
     */
    [[nodiscard]] bool optimize_model(ir::IRStream& stream) noexcept;

    /**
     * @enum OptimizationPhase
     * @brief Stages of ML model optimization
     */
    enum class OptimizationPhase : uint8_t {
        EXTRACT_MODEL = 0,      // Parse model from IR
        ANALYZE_LAYERS = 1,     // Understand layer structure
        FUSE_TENSORS = 2,       // Combine operations
        SPECIALIZE_SHAPES = 3,  // Create specialized kernels
        SELECT_BACKENDS = 4,    // Choose execution engines
        GENERATE_DISPATCH = 5,  // Create dispatch code
        VALIDATION = 6,         // Verify correctness
        EMIT_MODEL = 7,         // Write back to IR
    };

    /**
     * @brief Get current optimization phase
     */
    [[nodiscard]] OptimizationPhase get_current_phase() const noexcept {
        return current_phase;
    }

    /**
     * @brief Set dispatch strategy for backend selection
     */
    void set_backend_dispatch_strategy(
        MLBackendDispatcher::DispatchStrategy strategy
    ) noexcept;

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "MLOptimizer";
    }

    /**
     * @struct OptimizationResults
     * @brief Results and metrics from ML optimization
     */
    struct OptimizationResults {
        bool success = false;
        double latency_before_ms = 0.0;
        double latency_after_ms = 0.0;
        double latency_improvement_percent = 0.0;
        
        uint32_t tensor_ops_fused = 0;
        uint32_t kernels_specialized = 0;
        uint32_t backends_dispatched = 0;
        
        uint64_t memory_before_bytes = 0;
        uint64_t memory_after_bytes = 0;
        
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
    std::unique_ptr<TensorFusion> tensor_fusion;
    std::unique_ptr<TensorSpecializer> tensor_specializer;
    std::unique_ptr<MLBackendDispatcher> backend_dispatcher;

    // Cost model (shared from DGTL pipeline)
    dgtl::analysis::DGTLAICostModel* cost_model = nullptr;

    // Control flags
    bool optimize_fusion = true;
    bool optimize_specialization = true;
    bool optimize_backend_dispatch = true;
    bool runtime_profiling = false;

    // State
    OptimizationPhase current_phase = OptimizationPhase::EXTRACT_MODEL;
    OptimizationResults results;

    // Helper methods for each phase
    void extract_model_structure(ir::IRStream& stream) noexcept;
    void analyze_layer_properties(const ir::IRStream& stream) noexcept;
    void validate_model(const ir::IRStream& stream) noexcept;
    void emit_optimized_model(ir::IRStream& stream) noexcept;
};

} // namespace ml
} // namespace pass
} // namespace dgtl
