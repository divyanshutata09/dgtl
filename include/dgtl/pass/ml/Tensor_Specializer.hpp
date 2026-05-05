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
#include <map>
#include <cstdint>
#include <memory>
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace ml {

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

} // namespace ml
} // namespace pass
} // namespace dgtl
