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

#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace ml {

/**
 * @class MLBackendDispatcher
 * @brief Selects optimal backend for each tensor operation
 * 
 * Optimization for MYZON Rule 79 (model_lib - ML model execution):
 * - Evaluates available backends (TensorRT, cuDNN, DGTL native)
 * - Selects best backend per operation based on:
 *   * Tensor shape and size
 *   * Operation type and complexity
 *   * Backend availability and capability
 *   * Cost model predictions
 * - Implements intelligent fallback chains
 * - Maintains execution correctness across backends
 * 
 * Backend selection priority:
 * 1. TensorRT (NVIDIA, highest performance)
 * 2. cuDNN (NVIDIA fallback)
 * 3. ARM Compute Library (mobile)
 * 4. DGTL native (universal fallback)
 * 
 * Reuses DGTL infrastructure:
 * - AI Cost Model for performance predictions
 * - Multi-emitter system for code generation
 * - Execution dispatcher for runtime dispatch
 * 
 * Part of DGTL v1.0.2 ML backend integration.
 */
class MLBackendDispatcher : public dgtl::pipeline::DGTLOptimizationPass {
public:
    MLBackendDispatcher() noexcept = default;
    ~MLBackendDispatcher() override = default;

    /**
     * @brief Execute backend selection on IR stream
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @enum BackendType
     * @brief Available ML compute backends
     */
    enum class BackendType : uint8_t {
        TENSORRT = 0,        // NVIDIA TensorRT (highest perf)
        CUDNN = 1,           // NVIDIA cuDNN (fallback)
        ARM_COMPUTE = 2,     // ARM Compute Library (mobile)
        ROCM = 3,            // AMD ROCm
        DGTL_NATIVE = 4,     // DGTL built-in implementation
        MKLDNN = 5,          // Intel MKL-DNN (x86)
    };

    /**
     * @enum DispatchStrategy
     * @brief How to make backend decisions
     */
    enum class DispatchStrategy : uint8_t {
        PREFER_SPECIALIZED = 0,  // Use specialized backend if available
        COST_OPTIMIZED = 1,      // Use AI cost model for selection
        PROFILE_DRIVEN = 2,      // Use profiling data
        POWER_AWARE = 3,         // Minimize power consumption
    };

    /**
     * @struct BackendCapability
     * @brief Capability information for a backend
     */
    struct BackendCapability {
        BackendType backend = BackendType::DGTL_NATIVE;
        bool available = false;
        bool supports_int8 = false;
        bool supports_fp16 = false;
        bool supports_fp32 = true;
        bool supports_batching = true;
        uint64_t max_memory = 0;
        double estimated_throughput = 0.0;  // Operations per second
    };

    /**
     * @struct TensorOpMetadata
     * @brief Metadata about a tensor operation
     */
    struct TensorOpMetadata {
        uint32_t op_id = 0;
        uint16_t op_code = 0;         // e.g., 466 for TENSOR_CONV
        std::vector<uint32_t> input_shapes;
        std::vector<uint32_t> output_shapes;
        uint64_t flops = 0;            // Floating-point operations
        uint64_t bytes_moved = 0;      // Memory bandwidth requirement
        bool is_critical_path = false; // On execution critical path
    };

    /**
     * @brief Set dispatch strategy
     */
    void set_dispatch_strategy(DispatchStrategy strategy) noexcept {
        current_strategy = strategy;
    }

    /**
     * @brief Enable/disable specific backend
     */
    void enable_backend(BackendType backend, bool enable) noexcept;

    /**
     * @brief Register backend capability
     * Called by initialization to advertise available backends
     */
    void register_backend(const BackendCapability& capability) noexcept;

    /**
     * @brief Get available backends
     */
    [[nodiscard]] std::vector<BackendType> get_available_backends() const noexcept;

    /**
     * @brief Check if backend can execute operation
     * @param backend Backend to check
     * @param op Tensor operation metadata
     * @return true if backend has required capability
     */
    [[nodiscard]] bool check_backend_capability(
        BackendType backend,
        const TensorOpMetadata& op
    ) const noexcept;

    /**
     * @brief Select best backend for tensor operation
     * Core algorithm: evaluates all backends, selects optimal
     * @param op Operation metadata
     * @param allowed_backends Subset of backends to consider (or empty for all)
     * @return Selected backend, or DGTL_NATIVE if none suitable
     */
    [[nodiscard]] BackendType select_backend(
        const TensorOpMetadata& op,
        const std::vector<BackendType>& allowed_backends = {}
    ) noexcept;

    /**
     * @brief Predict execution cost for operation on backend
     * Uses AI cost model combined with backend knowledge
     * @param backend Backend to predict for
     * @param op Operation metadata
     * @return Predicted cost (cycles or latency)
     */
    [[nodiscard]] double predict_backend_cost(
        BackendType backend,
        const TensorOpMetadata& op
    ) const noexcept;

    /**
     * @brief Emit backend dispatch call
     * Generates code to call appropriate backend implementation
     * @param backend Backend to emit for
     * @param op Operation metadata
     * @param stream IR stream for code emission
     * @return Generated dispatch code
     */
    [[nodiscard]] std::string emit_backend_call(
        BackendType backend,
        const TensorOpMetadata& op,
        const ir::IRStream& stream
    ) const noexcept;

    /**
     * @brief Generate fallback chain for operation
     * Creates try-fallback sequence: preferred -> cuDNN -> DGTL_NATIVE
     * @param op Operation metadata
     * @return Ordered list of backends to try
     */
    [[nodiscard]] std::vector<BackendType> generate_fallback_chain(
        const TensorOpMetadata& op
    ) const noexcept;

    /**
     * @brief Set cost model for backend selection
     */
    void set_cost_model(dgtl::analysis::DGTLAICostModel* model) noexcept {
        ai_model = model;
    }

    /**
     * @brief Set profiling data (cycles/memory usage per backend)
     * Used by profile-driven dispatch strategy
     */
    void set_profiling_data(
        uint32_t op_id,
        BackendType backend,
        uint64_t cycles,
        uint64_t memory_bytes
    ) noexcept;

    /**
     * @brief Get dispatch decisions for all tensor operations in stream
     * @param stream IR stream
     * @return List of (op_id, selected_backend) pairs
     */
    [[nodiscard]] std::vector<std::pair<uint32_t, BackendType>> get_dispatch_decisions(
        const ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "MLBackendDispatcher";
    }

    /**
     * @struct DispatcherStats
     * @brief Statistics from backend dispatcher
     */
    struct DispatcherStats {
        uint32_t tensor_ops_analyzed = 0;
        uint32_t tensorrt_selected = 0;
        uint32_t cudnn_selected = 0;
        uint32_t arm_compute_selected = 0;
        uint32_t rocm_selected = 0;
        uint32_t dgtl_native_selected = 0;
        
        double average_cost_reduction = 0.0;  // vs. default backend
        uint32_t fallback_chain_activations = 0;
        uint32_t dispatch_failures = 0;
    };

    [[nodiscard]] const DispatcherStats& get_stats() const noexcept {
        return stats;
    }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    DispatcherStats stats;

    DispatchStrategy current_strategy = DispatchStrategy::COST_OPTIMIZED;
    
    // Backend capabilities
    std::vector<BackendCapability> available_backends;
    std::map<BackendType, bool> backend_enabled;

    // Profiling data: (op_id, backend) -> (cycles, memory)
    std::map<std::pair<uint32_t, BackendType>, std::pair<uint64_t, uint64_t>> profiling_data;

    // Helper methods
    [[nodiscard]] double compute_backend_score(
        BackendType backend,
        const TensorOpMetadata& op
    ) const noexcept;

    [[nodiscard]] bool meets_precision_requirements(
        BackendType backend,
        uint8_t required_precision
    ) const noexcept;
};

} // namespace ml
} // namespace pass
} // namespace dgtl
