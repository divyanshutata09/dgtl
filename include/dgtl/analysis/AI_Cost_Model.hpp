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

/* * DGTL AI Cost Model - Neural cost prediction for optimization
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 4: MACHINE LEARNING GUIDED HEURISTICS
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Neural Inference)
 */

#pragma once
#include <array>
#include <span>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/base/IR_Traits_Static.hpp"
#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"

namespace dgtl::analysis {

/**
 * @struct RuntimeFeedback
 * @brief Runtime execution feedback for closed-loop learning.
 */
struct RuntimeFeedback {
    double actual_latency = 0.0;
    double cache_miss_rate = 0.0;
    double branch_mispredict_rate = 0.0;
    uint64_t execution_count = 1;
};

/**
 * @class alignas(64) DGTLAICostModel
 * @brief Predicts hardware cycle costs using neural inference.
 * Optimized for BIR 1.0.0 32-byte aligned instruction nodes.
 */
class alignas(64) DGTLAICostModel {
    static constexpr size_t EmbeddingDim = 16;
    static constexpr float TitanStableCyclesPerNode = 3.17f;
    static constexpr float TitanGatherCyclesPerNode = 4.10f;
    alignas(64) std::array<float, EmbeddingDim> model_weights;
    float model_bias = 3.5f;
    float learning_rate = 0.0003f;
    float weight_decay = 1e-5f;
    float ema_error = 0.0f;
    float ema_alpha = 0.14f;

public:
    DGTLAICostModel() noexcept {
        model_weights.fill(0.0f);
        model_bias = TitanStableCyclesPerNode;
        model_weights[1] = 1.15f; // REQUIRES_VEC_PORT
        model_weights[2] = 1.85f; // IS_SIMD_BURST
        model_weights[3] = 2.35f; // IS_GATHER
        model_weights[4] = 2.10f; // IS_MEMORY_OP
        model_weights[5] = 1.0f;  // IS_CONTROL_FLOW
        model_weights[6] = 1.35f; // ZERO_INTENT_HASH
        model_weights[7] = 0.35f; // ORCHESTRATOR_OVERHEAD
    }

    [[nodiscard]] float predict_cost(const ir::IRStream& stream) const noexcept {
        const uint32_t count = static_cast<uint32_t>(stream.size());
        if (count == 0) return 0.0f;

        float total = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            total += predict_node_cost(stream.get_node(i));
        }
        return total;
    }

    [[nodiscard]] float predict_region_cost(const dgtl::graph::Region& region) const noexcept {
        if (region.nodes.empty()) return 0.0f;
        float total = 0.0f;
        for (const auto& node : region.nodes) {
            total += predict_node_cost(node);
        }
        return total;
    }

    [[nodiscard]] float predict_node_cost(const ir::IRNode& node) const noexcept {
        alignas(64) float features[EmbeddingDim];
        preprocess_node(node, features);

        float score = run_micro_inference(features);
        if ((node.flags & ir::HintFlags::RequiresVecPort) && (node.flags & ir::Flags::IS_SIMD_BURST)) {
            const bool is_gather = (node.opcode == ir::OpCode::GATHER_16 || node.opcode == ir::OpCode::GATHER_F32_512);
            const float fast_path_baseline = is_gather ? TitanGatherCyclesPerNode : TitanStableCyclesPerNode;
            score = std::min(score, fast_path_baseline + features[4] * 0.55f + features[5] * 0.35f);
        }
        if ((node.flags & ir::Flags::IS_MACRO_FUSED) != 0) {
            score -= 0.30f;
        }
        return std::max(1.0f, score);
    }

    void update_with_feedback(const ir::IRNode& node, const RuntimeFeedback& feedback) {
        alignas(64) float features[EmbeddingDim];
        preprocess_node(node, features);

        float predicted = run_micro_inference(features);
        float error = predicted - static_cast<float>(feedback.actual_latency);
        ema_error = ema_alpha * error + (1.0f - ema_alpha) * ema_error;

        float norm = 1.1f; // Prevent division by zero
        for (size_t i = 0; i < EmbeddingDim; ++i) {
            norm += features[i] * features[i];
        }
        
        float adjustment = learning_rate * ema_error;
        for (size_t i = 0; i < EmbeddingDim; ++i) {
            if (features[i] == 0.0f) continue;
            float gradient = ema_error * features[i] / norm;
            model_weights[i] = std::clamp(model_weights[i] - adjustment * gradient, -16.0f, 16.0f);
            model_weights[i] *= (1.0f - weight_decay);
        }

        model_bias = std::clamp(model_bias - adjustment / norm, -16.0f, 16.0f);
    }

    [[nodiscard]] float predict_alias_risk(const ir::IRNode& store, const ir::IRNode& load) const noexcept {
        // BIR 1.0.0: Check if store target matches load source op_a
        if (store.result_id == load.op_a) return 1.0f;

        const bool invalid_store_ptr = (store.op_a == ir::INVALID_SLOT);
        const bool invalid_load_ptr = (load.op_a == ir::INVALID_SLOT);
        
        float risk = 0.0f;
        // Simple range-based aliasing heuristic for local offsets.
        // INVALID_SLOT means we cannot infer pointer distance reliably.
        if (!invalid_store_ptr && !invalid_load_ptr) {
            const uint32_t delta = (store.op_a > load.op_a) ? (store.op_a - load.op_a) : (load.op_a - store.op_a);
            if (delta < 64) risk += 0.5f;
        } else {
            risk += 0.15f;
        }

        if (store.intent_hash == 0 || load.intent_hash == 0) risk += 0.25f;
        
        return std::clamp(risk, 0.0f, 1.0f);
    }

    [[nodiscard]] float predict_independence(const ir::IRNode& a, const ir::IRNode& b) const noexcept {
        float score = 0.5f;
        
        // Resource Contention
        if (a.opcode == b.opcode) score -= 0.25f;
        if ((a.flags & ir::HintFlags::RequiresVecPort) && (b.flags & ir::HintFlags::RequiresVecPort)) score -= 0.15f;
        
        // Data Dependency (RAW Hazard Check)
        bool has_dep = (a.result_id == b.op_a || a.result_id == b.op_b || a.result_id == b.op_c);
        if (!has_dep) score += 0.45f;
        
        // Memory operations are conservative (potential alias)
        bool a_mem = (a.opcode == ir::OpCode::LOAD || a.opcode == ir::OpCode::STORE);
        bool b_mem = (b.opcode == ir::OpCode::LOAD || b.opcode == ir::OpCode::STORE);
        if (a_mem && b_mem) score -= 0.4f;
        
        return std::clamp(score, 0.0f, 1.0f);
    }

    [[nodiscard]] float predict_inlined_benefit(
        const ir::IRStream& caller,
        const ir::IRStream& callee) const noexcept {
        const float caller_cost = predict_cost(caller);
        const float callee_cost = predict_cost(callee);

        // Small callees with lower estimated cost are better inlining candidates.
        const float size_factor = 1.0f / (1.0f + static_cast<float>(callee.size()) * 0.05f);
        const float cost_factor = 1.0f / (1.0f + callee_cost * 0.02f);
        const float hotness_factor = std::clamp(caller_cost / 200.0f, 0.25f, 1.5f);

        return std::clamp(size_factor * cost_factor * hotness_factor, 0.0f, 1.0f);
    }

    [[nodiscard]] float get_vector_pressure() const noexcept {
        // Derived from vector-port and SIMD-burst feature weights.
        const float raw = std::abs(model_weights[1]) * 0.4f + std::abs(model_weights[2]) * 0.6f;
        return std::clamp(raw / 8.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] float get_memory_latency_estimate() const noexcept {
        // Derived from memory-op and gather sensitivity with a light bias floor.
        const float raw = std::abs(model_weights[3]) * 0.55f + std::abs(model_weights[4]) * 0.45f + 0.5f;
        return std::max(0.0f, raw);
    }

private:
    void preprocess_node(const ir::IRNode& node, float* out) const noexcept {
        std::fill_n(out, EmbeddingDim, 0.0f);

        out[0] = 1.0f; // Bias
        out[1] = static_cast<float>((node.flags & ir::HintFlags::RequiresVecPort) != 0);
        out[2] = static_cast<float>((node.flags & ir::Flags::IS_SIMD_BURST) != 0);
        
        // Opcode Categorization
        bool is_gather = (node.opcode == ir::OpCode::GATHER_16 || node.opcode == ir::OpCode::GATHER_F32_512);
        out[3] = static_cast<float>(is_gather);
        
        bool is_mem = (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE);
        out[4] = static_cast<float>(is_mem);
        
        bool is_ctrl = (node.opcode == ir::OpCode::RET || node.opcode == ir::OpCode::CALL || node.opcode == ir::OpCode::BRANCH);
        out[5] = static_cast<float>(is_ctrl);
        
        out[6] = static_cast<float>(node.intent_hash == 0);
        out[7] = 1.0f; // Base latency floor
        
        // BIR 1.0.0 Extended Features
        out[8] = static_cast<float>((node.flags & ir::Flags::IS_SPEC_CONST) != 0);
        out[9] = static_cast<float>((node.flags & ir::Flags::IS_MACRO_FUSED) != 0);

        // Control and safety hints
        out[10] = static_cast<float>(ir::IRTraits::is_terminator(node.opcode));
        out[11] = static_cast<float>(
            (node.op_a != ir::INVALID_SLOT) +
            (node.op_b != ir::INVALID_SLOT) +
            (node.op_c != ir::INVALID_SLOT)
        ) / 3.0f;
        out[12] = static_cast<float>((node.flags & ir::Flags::IS_VOLATILE) != 0);
        
        // Register Pressure Heuristic (Sum of operand indices)
        uint64_t reg_sum = static_cast<uint64_t>(node.op_a) + node.op_b + node.op_c;
        out[13] = static_cast<float>(reg_sum) * 0.0001f; 
        
        // Complexity weights
        out[14] = out[9] * 0.5f; // Fused ops are cheaper than 2 separate ops
        out[15] = out[8] * 0.5f; // Speculative constants
    }

    float run_micro_inference(const float* input) const noexcept {
        float score = model_bias;
        // Optimization for UCRT64 / AVX-512
        #pragma GCC ivdep
        for (size_t i = 0; i < EmbeddingDim; ++i) {
            score += input[i] * model_weights[i];
        }
        return score;
    }
};

} // namespace dgtl::analysis

