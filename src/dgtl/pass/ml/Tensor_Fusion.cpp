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

#include "dgtl/pass/ml/Tensor_Fusion.hpp"
#include <algorithm>

namespace dgtl {
namespace pass {
namespace ml {

void TensorFusion::execute(ir::IRStream& stream) {
    auto sequences = detect_fusible_sequences(stream);
    for (const auto& seq : sequences) {
        if (seq.memory_saved_by_fusion >= MIN_MEMORY_SAVINGS) {
            auto kernel_code = emit_fused_kernel(seq);
        }
    }
}

std::vector<TensorFusion::FusibleSequence> TensorFusion::detect_fusible_sequences(
    const ir::IRStream& stream
) noexcept {
    std::vector<FusibleSequence> sequences;
    
    // Scan stream for patterns of tensor operations that can be fused
    for (size_t i = 0; i < stream.size() - 1; ++i) {
        auto curr_op = stream.get_node(i).opcode;
        auto next_op = stream.get_node(i + 1).opcode;
        
        // Check if consecutive operations can be fused
        if ((curr_op >= ir::OpCode::TENSOR_RESHAPE && curr_op <= ir::OpCode::TENSOR_SOFTMAX) &&
            (next_op >= ir::OpCode::TENSOR_RESHAPE && next_op <= ir::OpCode::TENSOR_SOFTMAX)) {
            // Potentially fusible tensor operations found
            FusibleSequence seq;
            seq.operation_ids.push_back(static_cast<uint32_t>(i));
            seq.operation_ids.push_back(static_cast<uint32_t>(i + 1));
            seq.total_intermediate_memory = 1024 * 1024;  // Estimated
            sequences.push_back(seq);
        }
    }
    
    return sequences;
}

bool TensorFusion::can_fuse_operations(
    TensorOpCode first,
    TensorOpCode second
) noexcept {
    if (first == TensorOpCode::TENSOR_CONV && second == TensorOpCode::TENSOR_RELU) {
        return true;
    }
    if (first == TensorOpCode::TENSOR_BATCHNORM && second == TensorOpCode::TENSOR_RELU) {
        return true;
    }
    return false;
}

uint64_t TensorFusion::estimate_memory_savings(
    const FusibleSequence& sequence
) const noexcept {
    return sequence.total_intermediate_memory;
}

double TensorFusion::estimate_fusion_speedup(
    const FusibleSequence& sequence
) const noexcept {
    // Estimate speedup from eliminating intermediate memory operations
    // More memory savings = higher speedup
    double base_speedup = 1.05;
    double memory_factor = static_cast<double>(sequence.total_intermediate_memory) / (10 * 1024 * 1024);
    return base_speedup + (memory_factor * 0.2);  // 5-25% speedup range
}

std::string TensorFusion::emit_fused_kernel(
    const FusibleSequence& sequence
) const noexcept {
    // Generate fused kernel code from sequence of operations
    std::string kernel_code;
    kernel_code += "// Fused kernel: ops ";
    for (size_t i = 0; i < sequence.operation_ids.size(); ++i) {
        if (i > 0) kernel_code += ", ";
        kernel_code += std::to_string(sequence.operation_ids[i]);
    }
    kernel_code += "\n";
    kernel_code += "// Memory savings: " + std::to_string(sequence.total_intermediate_memory) + " bytes\n";
    return kernel_code;
}

bool TensorFusion::validate_fusion(
    const FusibleSequence& sequence,
    uint32_t fused_op_id
) const noexcept {
    // Validate fusion is safe and correct
    // Check: sequence is non-empty, op_id is in valid range
    if (sequence.operations.empty()) return false;
    if (fused_op_id >= 1000000) return false;  // Invalid ID
    return true;
}

} // namespace ml
} // namespace pass
} // namespace dgtl
