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

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/ir/rewrite/Pattern_Rewriter.hpp"

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

/**
 * @class TensorFusion
 * @brief Fuses tensor operations to reduce intermediate allocations
 *
 * Optimization for MYZON Rule 79 (model_lib - ML model layers):
 * - Detects fusible tensor operation sequences
 * - Combines operations to reduce memory overhead
 * - Maintains numerical correctness through validation
 *
 * Typical fusion patterns:
 * - Conv + ReLU -> ConvReLU (single kernel)
 * - BatchNorm + ReLU -> BatchNormReLU
 * - Linear + ReLU -> LinearReLU
 * - Add + ReLU -> AddReLU
 *
 * Reuses DGTL infrastructure:
 * - Tensor detection (has_tensor_operations, is_tensor_op)
 * - Multi-emitter system for kernel emission
 * - AI Cost Model for fusion benefit prediction
 * - Formal verification for numerical safety
 *
 * Part of DGTL v1.0.2 ML backend integration.
 */
class TensorFusion : public dgtl::pipeline::DGTLOptimizationPass {
public:
    TensorFusion() noexcept = default;
    ~TensorFusion() override = default;

    /**
     * @brief Execute tensor fusion on IR stream
     */
    void execute(ir::IRStream& stream) override;

    // =========================================================================
    // TENSOR OPCODE ALIASES
    // The local TensorOpCode enum has been removed. It had drifted from
    // IR_Core.hpp and contained 9 incorrect opcode numbers (e.g. TENSOR_RELU
    // was 467 here but TENSOR_CONV is 467 in IR_Core).
    //
    // Use dgtl::dialect constants directly. Aliases below preserve readability
    // at call sites within this class without breaking the layer contract.
    // =========================================================================
    static constexpr ir::OpCode kReshape    = ir::OpCode::TENSOR_RESHAPE;
    static constexpr ir::OpCode kReduce     = ir::OpCode::TENSOR_REDUCE;
    static constexpr ir::OpCode kTranspose  = ir::OpCode::TENSOR_TRANSPOSE;
    static constexpr ir::OpCode kSlice      = ir::OpCode::TENSOR_SLICE;
    static constexpr ir::OpCode kConcat     = ir::OpCode::TENSOR_CONCAT;
    static constexpr ir::OpCode kBroadcast  = ir::OpCode::TENSOR_BROADCAST;
    static constexpr ir::OpCode kGEMM       = ir::OpCode::TENSOR_GEMM;
    static constexpr ir::OpCode kMatMul     = ir::OpCode::TENSOR_GEMM;
    static constexpr ir::OpCode kConv       = ir::OpCode::TENSOR_CONV;
    static constexpr ir::OpCode kPool       = ir::OpCode::TENSOR_POOL;
    static constexpr ir::OpCode kSoftmax    = ir::OpCode::TENSOR_SOFTMAX;
    static constexpr ir::OpCode kReLU       = ir::OpCode::TENSOR_RELU;
    static constexpr ir::OpCode kBatchNorm  = ir::OpCode::TENSOR_BATCHNORM;
    static constexpr ir::OpCode kGather     = ir::OpCode::TENSOR_GATHER;
    static constexpr ir::OpCode kScatter    = ir::OpCode::TENSOR_SCATTER_D;

    /**
     * @struct FusibleSequence
     * @brief Represents a sequence of tensor operations that can be fused
     */
    struct FusibleSequence {
        std::vector<ir::OpCode> operations;
        std::vector<uint32_t> operation_ids;  // IR operation IDs
        uint64_t total_intermediate_memory = 0;
        uint64_t memory_saved_by_fusion = 0;
        double estimated_speedup = 1.0;
        bool is_valid = false;
    };

    /**
     * @brief Detect all fusible tensor operation sequences
     * @param stream IR stream containing tensor operations
     * @return Vector of detected fusible sequences
     */
    [[nodiscard]] std::vector<FusibleSequence> detect_fusible_sequences(
        const ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Check if two consecutive tensor operations can be fused
     * @param first First operation code
     * @param second Second operation code
     * @return true if fusion is legal and beneficial
     */
    [[nodiscard]] static bool can_fuse_operations(
        ir::OpCode first,
        ir::OpCode second
    ) noexcept;

    /**
     * @brief Estimate memory saved by fusing a sequence
     * @param sequence Tensor operations to fuse
     * @return Bytes saved by eliminating intermediate tensors
     */
    [[nodiscard]] uint64_t estimate_memory_savings(
        const FusibleSequence& sequence
    ) const noexcept;

    /**
     * @brief Estimate speedup from fusing operations
     * Uses DGTL's AI Cost Model for cycle predictions
     * @param sequence Sequence to fuse
     * @return Speedup factor (e.g., 1.15 = 15% faster)
     */
    [[nodiscard]] double estimate_fusion_speedup(
        const FusibleSequence& sequence
    ) const noexcept;

    /**
     * @brief Fuse a sequence of tensor operations
     * @param stream IR stream to modify
     * @param sequence Sequence to fuse
     * @return ID of fused operation, or 0 if fusion failed
     */
    [[nodiscard]] uint32_t fuse_operations(
        ir::IRStream& stream,
        FusibleSequence& sequence
    ) noexcept;

    /**
     * @brief Validate fusion correctness
     * Ensures fused operation produces same results as original sequence
     * @param original Original operation sequence
     * @param fused Fused operation
     * @return true if fusion is numerically correct
     */
    [[nodiscard]] bool validate_fusion(
        const FusibleSequence& original,
        uint32_t fused_op_id
    ) const noexcept;

    /**
     * @brief Generate fused kernel code
     * Emits code for the fused operation to appropriate backend
     * @param sequence Sequence to fuse
     * @return Generated kernel code/assembly
     */
    [[nodiscard]] std::string emit_fused_kernel(
        const FusibleSequence& sequence
    ) const noexcept;

    /**
     * @brief Set cost model for speedup prediction
     */
    void set_cost_model(dgtl::analysis::DGTLAICostModel* model) noexcept {
        ai_model = model;
    }

    /**
     * @brief Enable/disable specific fusion patterns
     */
    void enable_conv_relu_fusion(bool enable) noexcept {
        fuse_conv_relu = enable;
    }

    void enable_batchnorm_fusion(bool enable) noexcept {
        fuse_batchnorm = enable;
    }

    void enable_linear_relu_fusion(bool enable) noexcept {
        fuse_linear_relu = enable;
    }

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "TensorFusion";
    }

    /**
     * @struct FusionStats
     * @brief Statistics from tensor fusion pass
     */
    struct FusionStats {
        uint32_t sequences_detected = 0;
        uint32_t sequences_fused = 0;
        uint64_t total_memory_saved = 0;
        double average_speedup = 1.0;
        uint32_t fusion_failures = 0;
    };

    [[nodiscard]] const FusionStats& get_stats() const noexcept {
        return stats;
    }

    [[nodiscard]] uint32_t consume_analysis_invalidation_mask() noexcept override {
        const uint32_t mask = pending_invalidation_mask;
        pending_invalidation_mask = 0u;
        return mask;
    }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    FusionStats stats;

    // Fusion pattern flags
    bool fuse_conv_relu = true;
    bool fuse_batchnorm = true;
    bool fuse_linear_relu = true;
    uint32_t pending_invalidation_mask = 0u;

    // Minimum memory savings to consider fusion (in bytes)
    static constexpr uint64_t MIN_MEMORY_SAVINGS = 4096;  // 4KB threshold
};

/**
 * @class TensorSpecializer
 * @brief Creates shape-specialized kernels for common tensor sizes
 *
 * Optimization for MYZON Rule 79 (model_lib - ML model execution):
 * - Profiles tensor shapes at runtime
 * - Generates specialized kernels for most common sizes
 * - Reduces branch mispredictions and improves cache efficiency
 * - Maintains generic fallback for uncommon shapes
 *
 * Specialization pipeline:
 * 1. Enable runtime profiling (collect shape observations)
 * 2. Analyze shape frequency distribution
 * 3. Generate specialized kernels for top-K shapes
 * 4. Cache specializations for reuse
 * 5. Select best kernel at runtime
 *
 * Reuses DGTL infrastructure:
 * - Runtime profiler for shape tracking
 * - Multi-emitter system for kernel generation
 * - AI Cost Model for specialization benefit prediction
 *
 * Part of DGTL v1.0.2 ML backend integration.
 */
class TensorSpecializer : public dgtl::pipeline::DGTLOptimizationPass {
public:
    TensorSpecializer() noexcept = default;
    ~TensorSpecializer() override = default;

    /**
     * @brief Execute tensor specialization on IR stream
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @struct TensorShape
     * @brief Represents tensor dimensions and metadata
     */
    struct TensorShape {
        std::vector<uint32_t> dimensions;  // e.g., {batch_size, height, width, channels}
        uint64_t total_elements = 0;
        uint8_t rank = 0;  // Number of dimensions

        [[nodiscard]] bool operator==(const TensorShape& other) const noexcept {
            return dimensions == other.dimensions;
        }

        [[nodiscard]] uint64_t total_size() const noexcept {
            uint64_t size = 1;
            for (auto dim : dimensions) {
                size *= dim;
            }
            return size;
        }
    };

    /**
     * @struct ShapeObservation
     * @brief Records tensor shape occurrence and profiling data
     */
    struct ShapeObservation {
        TensorShape shape;
        uint64_t occurrence_count = 0;
        uint64_t total_cycles = 0;      // Cycles spent processing this shape
        double cycles_per_element = 0.0; // Efficiency metric
        uint32_t specialized_kernel_id = 0;  // ID of generated kernel
        bool is_specialized = false;
    };

    /**
     * @brief Enable runtime profiling for shape collection
     * Must be called before execute() to gather profiling data
     */
    void enable_profiling(bool enable) noexcept {
        runtime_profiling_enabled = enable;
    }

    /**
     * @brief Collect tensor shape observation during execution
     * Called by runtime to record shapes being executed
     * @param op_id Operation ID
     * @param shape Observed tensor shape
     * @param cycles CPU cycles for this operation
     */
    void observe_shape(
        uint32_t op_id,
        const TensorShape& shape,
        uint64_t cycles
    ) noexcept;

    /**
     * @brief Analyze collected shape observations
     * Identifies most common shapes for specialization
     * @param min_frequency Minimum occurrences to consider (default 10)
     * @return Number of shapes to specialize
     */
    [[nodiscard]] uint32_t analyze_shapes(
        uint32_t min_frequency = 10
    ) noexcept;

    /**
     * @brief Get most common shapes for a tensor operation
     * @param op_id Operation ID
     * @param limit Maximum number of shapes to return
     * @return Top-K shapes by frequency
     */
    [[nodiscard]] std::vector<ShapeObservation> get_common_shapes(
        uint32_t op_id,
        uint32_t limit = 5
    ) const noexcept;

    /**
     * @brief Generate specialized kernel for specific shape
     * Creates optimized code path for fixed dimensions
     * @param op_id Operation ID to specialize
     * @param shape Shape to create kernel for
     * @param stream IR stream context
     * @return Generated kernel ID, or 0 if generation failed
     */
    [[nodiscard]] uint32_t emit_specialized_kernel(
        uint32_t op_id,
        const TensorShape& shape,
        ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Cache specialization for future reuse
     * Stores generated kernel so it can be reused across multiple sites
     * @param op_id Operation ID
     * @param shape Tensor shape
     * @param kernel_id Generated kernel ID
     */
    void cache_specialization(
        uint32_t op_id,
        const TensorShape& shape,
        uint32_t kernel_id
    ) noexcept;

    /**
     * @brief Retrieve cached specialization
     * @param op_id Operation ID
     * @param shape Requested shape
     * @return Kernel ID if cached, 0 if not found
     */
    [[nodiscard]] uint32_t get_cached_specialization(
        uint32_t op_id,
        const TensorShape& shape
    ) const noexcept;

    /**
     * @brief Generate generic fallback kernel
     * For shapes not explicitly specialized
     * @param op_id Operation ID
     * @param stream IR stream context
     * @return Fallback kernel ID
     */
    [[nodiscard]] uint32_t emit_generic_kernel(
        uint32_t op_id,
        ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Select best kernel for shape at runtime
     * Dispatches to specialized kernel if available, else generic
     * @param op_id Operation ID
     * @param shape Observed shape
     * @return Kernel ID to use
     */
    [[nodiscard]] uint32_t select_best_kernel(
        uint32_t op_id,
        const TensorShape& shape
    ) const noexcept;

    /**
     * @brief Estimate speedup from specialization
     * @param observation Shape observation with profiling data
     * @return Speedup factor (e.g., 1.25 = 25% faster)
     */
    [[nodiscard]] double estimate_specialization_speedup(
        const ShapeObservation& observation
    ) const noexcept;

    /**
     * @brief Set cost model for specialization benefit prediction
     */
    void set_cost_model(dgtl::analysis::DGTLAICostModel* model) noexcept {
        ai_model = model;
    }

    /**
     * @brief Get pass name
     */
    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "TensorSpecializer";
    }

    /**
     * @struct SpecializationStats
     * @brief Statistics from tensor specialization pass
     */
    struct SpecializationStats {
        uint32_t shapes_observed = 0;
        uint32_t shapes_common = 0;      // Shapes above frequency threshold
        uint32_t kernels_generated = 0;
        uint32_t kernels_cached = 0;
        double average_specialization_speedup = 1.0;
        uint64_t total_specialization_memory = 0;  // Memory used for specialized kernels
    };

    [[nodiscard]] const SpecializationStats& get_stats() const noexcept {
        return stats;
    }

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    SpecializationStats stats;
    bool runtime_profiling_enabled = false;

    // Profiling data: op_id -> vector of shape observations
    std::map<uint32_t, std::vector<ShapeObservation>> shape_observations;

    // Kernel cache: (op_id, shape) -> kernel_id
    std::map<std::pair<uint32_t, std::vector<uint32_t>>, uint32_t> kernel_cache;

    // Constants
    static constexpr uint32_t MAX_SPECIALIZATIONS_PER_OP = 5;
    static constexpr uint64_t MAX_SPECIALIZATION_MEMORY_BUDGET = 1024 * 1024 * 100;  // 100 MB
};

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

    [[nodiscard]] uint32_t consume_analysis_invalidation_mask() noexcept override {
        const uint32_t mask = pending_invalidation_mask;
        pending_invalidation_mask = 0u;
        return mask;
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
    uint32_t pending_invalidation_mask = 0u;

    // Helper methods for each phase
    void extract_model_structure(ir::IRStream& stream) noexcept;
    void analyze_layer_properties(const ir::IRStream& stream) noexcept;
    void validate_model(const ir::IRStream& stream) noexcept;
    void emit_optimized_model(ir::IRStream& stream) noexcept;
};

} // namespace ml
} // namespace pass
} // namespace dgtl
