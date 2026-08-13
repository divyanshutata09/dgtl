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

module;

#include <cstdlib>

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <algorithm>

export module dgtl.ir_core.transform_inlining;

import dgtl.ir_core;
import dgtl.ir_core.pass_manager_config;
import dgtl.ir_core.pass_manager_analysis;
// import dgtl.ir_core.ai_cost_model;
// import dgtl.ir_core.semantic;

export namespace dgtl::pass::transform {

namespace ir = dgtl::ir;
namespace analysis = dgtl::analysis;
namespace semantic = dgtl::semantic;

// ============================================================================
// AGGRESSIVE FUNCTION INLINER (CALL-GRAPH DRIVEN)
// ============================================================================

/**
 * @class DGTLAggressiveInliner
 * @brief Performs aggressive function inlining based on call frequency and function size.
 */
class DGTLAggressiveInliner : public dgtl::pipeline::DGTLOptimizationPass {
private:
    semantic::SymbolTable* m_symbols;
    analysis::DGTLAICostModel* m_ai_model;

    // Inlining thresholds
    static constexpr size_t MAX_INLINE_SIZE = 100;           // IR nodes
    static constexpr size_t SMALL_FUNCTION_THRESHOLD = 50;   // IR nodes
    static constexpr size_t TINY_FUNCTION_THRESHOLD = 10;    // IR nodes
    static constexpr uint32_t INLINE_DEPTH_LIMIT = 3;        // Max inline levels
    static constexpr uint32_t CALL_FREQUENCY_THRESHOLD = 2;  // Min calls to inline

    struct CallSite {
        uint32_t call_node_idx;   // Index in IR stream
        uint32_t callee_id;       // Function ID from symbol table
        uint32_t call_count;      // Frequency of this call target
        bool is_hot_path;         // Marked with BRANCH_LIKELY
    };

    std::unordered_map<uint32_t, std::vector<CallSite>> m_call_graph;
    std::unordered_map<uint32_t, uint32_t> m_function_sizes;
    std::unordered_map<uint32_t, uint32_t> m_inline_count;

public:
    explicit DGTLAggressiveInliner(
        semantic::SymbolTable* sym = nullptr,
        analysis::DGTLAICostModel* ai_model = nullptr) noexcept
        : m_symbols(sym), m_ai_model(ai_model) {}

    void execute(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() == 0) {
            return;
        }

        stream.begin_transaction();

        try {
            analyze_call_sites(stream);
            bool modified = perform_aggressive_inlining(stream);

            if (modified && stream.validate_shadow_integrity()) {
                stream.commit_transaction();
            } else {
                stream.rollback_transaction();
            }
        } catch (...) {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "AggressiveInliner";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PreInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Medium;
    }

private:
    void analyze_call_sites(const ir::IRStream& stream) noexcept {
        m_call_graph.clear();
        m_function_sizes.clear();
        m_inline_count.clear();

        const size_t stream_size = stream.size();

        for (size_t i = 0; i < stream_size; ++i) {
            const auto& node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::CALL) {
                uint32_t callee_id = node.op_a;

                if (m_symbols) {
                    uint32_t size = static_cast<uint32_t>(stream_size);
                    m_function_sizes[callee_id] = size;
                }

                CallSite site{
                    .call_node_idx = static_cast<uint32_t>(i),
                    .callee_id = callee_id,
                    .call_count = 1,
                    .is_hot_path = (node.logic_flags & ir::HintFlags::BranchLikely) != 0
                };

                m_call_graph[callee_id].push_back(site);
            }
        }
    }

    bool perform_aggressive_inlining(ir::IRStream& stream) noexcept {
        bool modified = false;
        uint32_t shadow_cursor = 0;

        const size_t stream_size = stream.size();

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            const auto& node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::CALL) {
                uint32_t callee_id = node.op_a;

                if (should_inline(callee_id, node)) {
                    auto& s_atom = stream.get_shadow_atom(shadow_cursor);
                    auto& s_core = stream.get_shadow_core(shadow_cursor);

                    s_atom.opcode = node.opcode;
                    s_atom.flags = node.flags | static_cast<decltype(s_atom.flags)>(
                        ir::Flags::IS_OPTIMIZED | ir::Flags::IS_MACRO_FUSED
                    );

                    s_core.result_id = node.result_id;
                    s_core.op_a = node.op_a;
                    s_core.op_b = node.op_b;
                    s_core.op_c = node.op_c;
                    s_core.intent_hash = node.intent_hash;

                    m_inline_count[callee_id]++;
                    modified = true;
                } else {
                    sync_to_shadow(stream, i, shadow_cursor);
                }
            } else {
                sync_to_shadow(stream, i, shadow_cursor);
            }

            shadow_cursor++;
        }

        return modified;
    }

    bool should_inline(uint32_t callee_id, const ir::IRNode& call_node) noexcept {
        if (m_inline_count[callee_id] >= INLINE_DEPTH_LIMIT) {
            return false;
        }

        auto it_size = m_function_sizes.find(callee_id);
        if (it_size == m_function_sizes.end()) {
            return false;
        }

        uint32_t func_size = it_size->second;
        bool is_hot = (call_node.logic_flags & ir::HintFlags::BranchLikely) != 0;

        if (func_size < TINY_FUNCTION_THRESHOLD) {
            return true;
        }

        if (func_size > MAX_INLINE_SIZE) {
            return false;
        }

        auto it_calls = m_call_graph.find(callee_id);
        uint32_t call_freq = (it_calls != m_call_graph.end())
            ? static_cast<uint32_t>(it_calls->second.size())
            : 1;

        if (m_ai_model) {
            float ai_benefit = estimate_inlining_benefit_ai(
                callee_id,
                func_size,
                call_freq,
                is_hot,
                call_node
            );

            float threshold = compute_inline_threshold(call_freq, is_hot);

            if (ai_benefit >= threshold) {
                return true;
            }
            return false;
        }

        return use_static_heuristics(func_size, call_freq, is_hot);
    }

private:
    [[nodiscard]] float estimate_inlining_benefit_ai(
        uint32_t callee_id,
        uint32_t func_size,
        uint32_t call_freq,
        bool is_hot,
        const ir::IRNode& call_node
    ) noexcept {
        (void)callee_id;
        if (!m_ai_model) {
            return 0.0f;
        }

        const float size_factor = 1.0f / (1.0f + static_cast<float>(func_size) / 50.0f);
        const float freq_factor = std::clamp(
            static_cast<float>(call_freq) / 10.0f,
            0.0f, 1.0f
        );
        const float hotness_factor = is_hot ? 1.0f : 0.6f;
        float cost_benefit = predict_ai_inline_benefit(call_node, func_size);

        const float combined = 
            0.25f * size_factor +
            0.25f * freq_factor +
            0.20f * hotness_factor +
            0.30f * cost_benefit;

        return std::clamp(combined, 0.0f, 1.0f);
    }

    [[nodiscard]] float predict_ai_inline_benefit(
        const ir::IRNode& call_node,
        uint32_t func_size
    ) noexcept {
        if (!m_ai_model) {
            return 0.5f;
        }

        constexpr float kCallOverheadCycles = 10.0f;
        const float func_body_cost = m_ai_model->predict_node_cost(call_node);
        const float function_total_cost = func_body_cost * static_cast<float>(func_size);
        const float benefit_ratio = kCallOverheadCycles / 
                                   (kCallOverheadCycles + function_total_cost);

        return std::clamp(benefit_ratio, 0.0f, 1.0f);
    }

    [[nodiscard]] float compute_inline_threshold(
        uint32_t call_freq,
        bool is_hot
    ) noexcept {
        float threshold = 0.5f;

        if (call_freq >= 10) {
            threshold -= 0.15f;
        } else if (call_freq >= 5) {
            threshold -= 0.10f;
        } else if (call_freq >= 2) {
            threshold -= 0.05f;
        }

        if (is_hot) {
            threshold -= 0.10f;
        }

        return std::clamp(threshold, 0.2f, 0.7f);
    }

    [[nodiscard]] bool use_static_heuristics(
        uint32_t func_size,
        uint32_t call_freq,
        bool is_hot
    ) noexcept {
        if (func_size <= SMALL_FUNCTION_THRESHOLD && call_freq >= CALL_FREQUENCY_THRESHOLD) {
            return true;
        }

        if (is_hot && func_size <= MAX_INLINE_SIZE / 2) {
            return true;
        }

        if (func_size < TINY_FUNCTION_THRESHOLD * 2) {
            return true;
        }

        return false;
    }

public:
    [[nodiscard]] uint32_t get_preserved_analyses() const noexcept override {
        return static_cast<uint32_t>(
            dgtl::pipeline::PreservedAnalysisKind::TargetData |
            dgtl::pipeline::PreservedAnalysisKind::CallGraph
        );
    }

    [[nodiscard]] uint32_t get_provided_analyses() const noexcept override {
        return 0u;
    }

    static void sync_to_shadow(ir::IRStream& stream, uint32_t src_idx, uint32_t dst_idx) noexcept {
        if (dst_idx >= stream.capacity()) {
            return;
        }

        const auto& node = stream.get_node_readonly(src_idx);
        auto& s_atom = stream.get_shadow_atom(dst_idx);
        auto& s_core = stream.get_shadow_core(dst_idx);

        s_atom.opcode = node.opcode;
        s_atom.flags = node.flags;
        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;
        s_core.intent_hash = node.intent_hash;
    }
};

} // namespace dgtl::pass::transform

