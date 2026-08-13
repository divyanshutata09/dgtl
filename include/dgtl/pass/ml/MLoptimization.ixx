/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

// 🚀 FIXED: Bind directly to your new core intermediate macro sub-module space
export module dgtl.ir_core.ml_optimization;

// 🚀 FIXED CYCLIC LOOP: Import local core leaf siblings natively via short-form shortcuts
import dgtl.ir_core.pass_manager_config;

// Import standalone external primitive modules safely
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir_core;

export namespace dgtl {
namespace pass {
namespace ml {

/**
 * @class MLBackendDispatcher
 * @brief Selects optimal backend for each tensor operation
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

        [[nodiscard]] bool has_shapes() const noexcept { return !input_shapes.empty() || !output_shapes.empty(); }
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
     */
    [[nodiscard]] bool check_backend_capability(
        BackendType backend,
        const TensorOpMetadata& op
    ) const noexcept;

    /**
     * @brief Select best backend for tensor operation
     */
    [[nodiscard]] BackendType select_backend(
        const TensorOpMetadata& op,
        const std::vector<BackendType>& allowed_backends = {}
    ) noexcept;

    /**
     * @brief Predict execution cost for operation on backend
     */
    [[nodiscard]] double predict_backend_cost(
        BackendType backend,
        const TensorOpMetadata& op
    ) const noexcept;

    /**
     * @brief Emit backend dispatch call
     */
    [[nodiscard]] std::string emit_backend_call(
        BackendType backend,
        const TensorOpMetadata& op,
        const ir::IRStream& stream
    ) const noexcept;

    /**
     * @brief Generate fallback chain for operation
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
     */
    void set_profiling_data(
        uint32_t op_id,
        BackendType backend,
        uint64_t cycles,
        uint64_t memory_bytes
    ) noexcept;

    /**
     * @brief Access profiling data map end-to-end
     */
    [[nodiscard]] const std::map<std::pair<uint32_t, BackendType>, std::pair<uint64_t, uint64_t>>& get_profiling_data() const noexcept {
        return profiling_data;
    }

    /**
     * @brief Get dispatch decisions for all tensor operations in stream
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

    [[nodiscard]] bool is_backend_enabled(BackendType b) const noexcept {
        auto it = backend_enabled.find(b);
        return (it != backend_enabled.end()) ? it->second : true;
    }
    [[nodiscard]] size_t available_backend_count() const noexcept { return available_backends.size(); }

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

        [[nodiscard]] size_t sequence_length() const noexcept { return operations.size(); }
        [[nodiscard]] const std::vector<uint32_t>& get_operation_ids() const noexcept { return operation_ids; }
    };

    /**
     * @brief Detect all fusible tensor operation sequences
     */
    [[nodiscard]] std::vector<FusibleSequence> detect_fusible_sequences(
        const ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Check if two consecutive tensor operations can be fused
     */
    [[nodiscard]] static bool can_fuse_operations(
        ir::OpCode first,
        ir::OpCode second
    ) noexcept;

    /**
     * @brief Estimate memory saved by fusing a sequence
     */
    [[nodiscard]] uint64_t estimate_memory_savings(
        const FusibleSequence& sequence
    ) const noexcept;

    /**
     * @brief Estimate speedup from fusing operations
     */
    [[nodiscard]] double estimate_fusion_speedup(
        const FusibleSequence& sequence
    ) const noexcept;

    /**
     * @brief Fuse a sequence of tensor operations
     */
    [[nodiscard]] uint32_t fuse_operations(
        ir::IRStream& stream,
        FusibleSequence& sequence
    ) noexcept;

    /**
     * @brief Validate fusion correctness
     */
    [[nodiscard]] bool validate_fusion(
        const FusibleSequence& original,
        uint32_t fused_op_id
    ) const noexcept;

    /**
     * @brief Generate fused kernel code
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
    [[maybe_unused]] static constexpr uint64_t MIN_MEMORY_SAVINGS = 4096;  // 4KB threshold
};

/**
 * @class TensorSpecializer
 * @brief Creates shape-specialized kernels for common tensor sizes
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
            return std::accumulate(dimensions.begin(), dimensions.end(), 1ULL, std::multiplies<uint64_t>());
        }
    };

    /**
     * @struct ShapeObservation
     * @brief Records tensor shape occurrence and profiling data
     */
    struct ShapeObservation {
        [[maybe_unused]] TensorShape shape;
        uint64_t occurrence_count = 0;
        uint64_t total_cycles = 0;      // Cycles spent processing this shape
        double cycles_per_element = 0.0; // Efficiency metric
        uint32_t specialized_kernel_id = 0;  // ID of generated kernel
        bool is_specialized = false;
    };

    /**
     * @brief Enable runtime profiling for shape collection
     */
    void enable_profiling(bool enable) noexcept {
        runtime_profiling_enabled = enable;
    }

    /**
     * @brief Collect tensor shape observation during execution
     */
    void observe_shape(
        uint32_t op_id,
        const TensorShape& shape,
        uint64_t cycles
    ) noexcept;

    /**
     * @brief Analyze collected shape observations
     */
    [[nodiscard]] uint32_t analyze_shapes(
        uint32_t min_frequency = 10
    ) noexcept;

    /**
     * @brief Get most common shapes for a tensor operation
     */
    [[nodiscard]] std::vector<ShapeObservation> get_common_shapes(
        uint32_t op_id,
        uint32_t limit = 5
    ) const noexcept;

    /**
     * @brief Access shape observations map end-to-end
     */
    [[nodiscard]] const std::map<uint32_t, std::vector<ShapeObservation>>& get_shape_observations() const noexcept {
        return shape_observations;
    }

    /**
     * @brief Access kernel cache map end-to-end
     */
    [[nodiscard]] const std::map<std::pair<uint32_t, std::vector<uint32_t>>, uint32_t>& get_kernel_cache() const noexcept {
        return kernel_cache;
    }

    /**
     * @brief Generate specialized kernel for specific shape
     */
    [[nodiscard]] uint32_t emit_specialized_kernel(
        uint32_t op_id,
        const TensorShape& shape,
        ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Cache specialization for future reuse
     */
    void cache_specialization(
        uint32_t op_id,
        const TensorShape& shape,
        uint32_t kernel_id
    ) noexcept;

    /**
     * @brief Retrieve cached specialization
     */
    [[nodiscard]] uint32_t get_cached_specialization(
        uint32_t op_id,
        const TensorShape& shape
    ) const noexcept;

    /**
     * @brief Generate generic fallback kernel
     */
    [[nodiscard]] uint32_t emit_generic_kernel(
        uint32_t op_id,
        ir::IRStream& stream
    ) noexcept;

    /**
     * @brief Select best kernel for shape at runtime
     */
    [[nodiscard]] uint32_t select_best_kernel(
        uint32_t op_id,
        const TensorShape& shape
    ) const noexcept;

    /**
     * @brief Estimate speedup from specialization
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

    [[nodiscard]] static constexpr uint32_t max_specializations_per_op() noexcept { return MAX_SPECIALIZATIONS_PER_OP; }
    [[nodiscard]] static constexpr uint64_t max_specialization_memory_budget() noexcept { return MAX_SPECIALIZATION_MEMORY_BUDGET; }

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
     */
    void execute(ir::IRStream& stream) override;

    /**
     * @brief Set the AI cost model to use
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
     */
    void enable_runtime_profiling(bool enable) noexcept {
        runtime_profiling = enable;
    }

    /**
     * @brief Perform complete ML optimization pipeline
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

    [[nodiscard]] const TensorFusion* get_tensor_fusion() const noexcept {
        return tensor_fusion.get();
    }

    [[nodiscard]] const TensorSpecializer* get_tensor_specializer() const noexcept {
        return tensor_specializer.get();
    }

    [[nodiscard]] const MLBackendDispatcher* get_backend_dispatcher() const noexcept {
        return backend_dispatcher.get();
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