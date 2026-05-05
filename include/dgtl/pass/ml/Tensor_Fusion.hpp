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
#include <memory>
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl {
namespace pass {
namespace ml {

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
 * - Conv + ReLU → ConvReLU (single kernel)
 * - BatchNorm + ReLU → BatchNormReLU
 * - Linear + ReLU → LinearReLU
 * - Add + ReLU → AddReLU
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

    /**
     * @enum TensorOpCode
     * @brief DGTL tensor operation codes (defined in core ISA)
     */
    enum class TensorOpCode : uint16_t {
        TENSOR_RESHAPE = 460,
        TENSOR_TRANSPOSE = 461,
        TENSOR_SLICE = 462,
        TENSOR_GATHER = 463,
        TENSOR_SCATTER = 464,
        TENSOR_MATMUL = 465,
        TENSOR_CONV = 466,
        TENSOR_RELU = 467,
        TENSOR_SOFTMAX = 468,
        TENSOR_BATCHNORM = 469,
        // More opcodes available (470-479)
    };

    /**
     * @struct FusibleSequence
     * @brief Represents a sequence of tensor operations that can be fused
     */
    struct FusibleSequence {
        std::vector<TensorOpCode> operations;
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
        TensorOpCode first,
        TensorOpCode second
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

private:
    dgtl::analysis::DGTLAICostModel* ai_model = nullptr;
    FusionStats stats;

    // Fusion pattern flags
    bool fuse_conv_relu = true;
    bool fuse_batchnorm = true;
    bool fuse_linear_relu = true;

    // Minimum memory savings to consider fusion (in bytes)
    static constexpr uint64_t MIN_MEMORY_SAVINGS = 4096;  // 4KB threshold
};

} // namespace ml
} // namespace pass
} // namespace dgtl
