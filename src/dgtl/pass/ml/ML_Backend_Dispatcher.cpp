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

#include "dgtl/pass/ml/ML_Backend_Dispatcher.hpp"
#include <algorithm>
#include <limits>

namespace dgtl {
namespace pass {
namespace ml {

// Helper: Check if opcode is a tensor operation
static inline bool is_tensor_operation(const ir::OpCode& opcode) noexcept {
    // Check if this is a TENSOR_* operation
    return opcode >= ir::OpCode::TENSOR_RESHAPE && opcode <= ir::OpCode::TENSOR_SOFTMAX;
}

void MLBackendDispatcher::execute(
    ir::IRStream& stream
) {
    stats = DispatcherStats();

    // Scan for tensor operations and select backends
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        // Check if this is a tensor operation that needs backend selection
        if (is_tensor_operation(node.opcode)) {
            // Backend selection happens implicitly during lowering
            stats.tensor_ops_analyzed++;
        }
    }
}

void MLBackendDispatcher::enable_backend(BackendType backend, bool enable) noexcept {
    backend_enabled[backend] = enable;
}

void MLBackendDispatcher::register_backend(const BackendCapability& capability) noexcept {
    available_backends.push_back(capability);
}

std::vector<MLBackendDispatcher::BackendType> MLBackendDispatcher::get_available_backends() const noexcept {
    std::vector<BackendType> backends;
    for (const auto& cap : available_backends) {
        if (cap.available && backend_enabled.at(cap.backend)) {
            backends.push_back(cap.backend);
        }
    }
    return backends;
}

bool MLBackendDispatcher::check_backend_capability(
    BackendType backend,
    const TensorOpMetadata& op
) const noexcept {
    // Check if backend can execute this operation
    for (const auto& cap : available_backends) {
        if (cap.backend == backend && cap.available) {
            // Check operation compatibility using op metadata
            if (op.op_id > 1000000) {
                return false;  // Invalid operation ID
            }
            return true;
        }
    }
    return false;
}

MLBackendDispatcher::BackendType MLBackendDispatcher::select_backend(
    const TensorOpMetadata& op,
    const std::vector<BackendType>& allowed_backends
) noexcept {
    std::vector<BackendType> candidates = allowed_backends.empty() ? 
                                          get_available_backends() : 
                                          allowed_backends;

    double best_score = -std::numeric_limits<double>::max();
    BackendType best_backend = BackendType::DGTL_NATIVE;

    for (const auto& backend : candidates) {
        if (check_backend_capability(backend, op)) {
            double score = compute_backend_score(backend, op);
            if (score > best_score) {
                best_score = score;
                best_backend = backend;
            }
        }
    }

    // Update statistics
    switch (best_backend) {
        case BackendType::TENSORRT:
            stats.tensorrt_selected++;
            break;
        case BackendType::CUDNN:
            stats.cudnn_selected++;
            break;
        case BackendType::ARM_COMPUTE:
            stats.arm_compute_selected++;
            break;
        case BackendType::ROCM:
            stats.rocm_selected++;
            break;
        case BackendType::DGTL_NATIVE:
            stats.dgtl_native_selected++;
            break;
        default:
            break;
    }

    stats.tensor_ops_analyzed++;
    return best_backend;
}

double MLBackendDispatcher::predict_backend_cost(
    BackendType backend,
    const TensorOpMetadata& op
) const noexcept {
    // Predict execution cost for operation on backend
    double base_cost = static_cast<double>(op.flops) / 1000.0;  // In microseconds

    // Backend-specific cost multipliers
    double multiplier = 1.0;
    switch (backend) {
        case BackendType::TENSORRT:
            multiplier = 0.8;  // Best performance
            break;
        case BackendType::CUDNN:
            multiplier = 0.9;
            break;
        case BackendType::ARM_COMPUTE:
            multiplier = 1.2;  // Mobile device
            break;
        case BackendType::ROCM:
            multiplier = 0.95;
            break;
        case BackendType::DGTL_NATIVE:
            multiplier = 1.5;  // General fallback
            break;
        default:
            multiplier = 1.0;
            break;
    }

    return base_cost * multiplier;
}

std::string MLBackendDispatcher::emit_backend_call(
    BackendType backend,
    const TensorOpMetadata& op,
    const ir::IRStream& stream
) const noexcept {
    // Generate code to call backend implementation
    // Validate stream contains op_id
    if (op.op_id >= stream.size()) {
        return "";  // Invalid operation ID
    }
    
    std::string code;

    switch (backend) {
        case BackendType::TENSORRT:
            code = "// Call TensorRT for op " + std::to_string(op.op_id) + "\n";
            break;
        case BackendType::CUDNN:
            code = "// Call cuDNN for op " + std::to_string(op.op_id) + "\n";
            break;
        case BackendType::DGTL_NATIVE:
            code = "// Call DGTL native for op " + std::to_string(op.op_id) + "\n";
            break;
        default:
            code = "// Dispatch op " + std::to_string(op.op_id) + "\n";
            break;
    }

    return code;
}

std::vector<MLBackendDispatcher::BackendType> MLBackendDispatcher::generate_fallback_chain(
    const TensorOpMetadata& op
) const noexcept {
    // Generate priority-ordered list of backends to try
    std::vector<BackendType> chain;

    // Preferred: TensorRT > cuDNN > ARM/ROCm > DGTL Native
    if (check_backend_capability(BackendType::TENSORRT, op)) {
        chain.push_back(BackendType::TENSORRT);
    }
    if (check_backend_capability(BackendType::CUDNN, op)) {
        chain.push_back(BackendType::CUDNN);
    }
    if (check_backend_capability(BackendType::ARM_COMPUTE, op)) {
        chain.push_back(BackendType::ARM_COMPUTE);
    }
    if (check_backend_capability(BackendType::ROCM, op)) {
        chain.push_back(BackendType::ROCM);
    }

    // Always include DGTL as fallback
    chain.push_back(BackendType::DGTL_NATIVE);

    return chain;
}

void MLBackendDispatcher::set_profiling_data(
    uint32_t op_id,
    BackendType backend,
    uint64_t cycles,
    uint64_t memory_bytes
) noexcept {
    auto key = std::make_pair(op_id, backend);
    profiling_data[key] = std::make_pair(cycles, memory_bytes);
}

std::vector<std::pair<uint32_t, MLBackendDispatcher::BackendType>> 
MLBackendDispatcher::get_dispatch_decisions(
    const ir::IRStream& stream
) noexcept {
    // Get backend selection for all tensor operations in stream
    std::vector<std::pair<uint32_t, BackendType>> decisions;

    // Scan IR stream for tensor operations and select backends
    for (size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i);
        // Check if this is a tensor operation
        if (is_tensor_operation(node.opcode)) {
            TensorOpMetadata op_meta;
            op_meta.op_id = static_cast<uint32_t>(i);
            op_meta.op_code = static_cast<uint16_t>(node.opcode);
            
            BackendType selected = select_backend(op_meta);
            decisions.push_back({op_meta.op_id, selected});
        }
    }

    return decisions;
}

double MLBackendDispatcher::compute_backend_score(
    BackendType backend,
    const TensorOpMetadata& op
) const noexcept {
    // Score backends based on cost, capability, and availability
    double cost = predict_backend_cost(backend, op);
    
    // Lower cost = higher score
    double score = 1000.0 / (cost + 1.0);

    // Boost score for specialized backends
    switch (backend) {
        case BackendType::TENSORRT:
            score *= 1.3;  // Prefer TensorRT
            break;
        case BackendType::CUDNN:
            score *= 1.2;
            break;
        case BackendType::DGTL_NATIVE:
            score *= 0.8;  // Fallback only
            break;
        default:
            break;
    }

    return score;
}

bool MLBackendDispatcher::meets_precision_requirements(
    BackendType backend,
    uint8_t required_precision
) const noexcept {
    // Check if backend supports required precision
    // Precision levels: 1=fp32, 2=fp16, 4=int8, 8=mixed
    
    for (const auto& cap : available_backends) {
        if (cap.backend == backend) {
            if (required_precision == 1 && cap.supports_fp32) return true;
            if (required_precision == 2 && cap.supports_fp16) return true;
            if (required_precision == 4 && cap.supports_int8) return true;
            return false;  // Backend doesn't support required precision
        }
    }
    
    // Unknown backend, assume fp32 support
    return required_precision == 1;
}

} // namespace ml
} // namespace pass
} // namespace dgtl
