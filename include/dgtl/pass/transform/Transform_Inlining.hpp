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

/*
 * Aggressive function inlining:
 * - Inlines hot functions based on call frequency and size heuristics
 * - Call-graph driven decision making with profile-guided optimization
 * - Prevents infinite expansion via recursion-depth limits
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/ir/rewrite/Pattern_Rewriter.hpp"

namespace dgtl::pass::transform {

namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;
namespace analysis = dgtl::analysis;
namespace semantic = dgtl::semantic;

// ============================================================================
// AGGRESSIVE FUNCTION INLINER (CALL-GRAPH DRIVEN)
// ============================================================================

/**
 * @class DGTLAggressiveInliner
 * @brief Performs aggressive function inlining based on call frequency and function size.
 *
 * DESIGN PHILOSOPHY:
 * - AGGRESSIVE: Inlines all hot functions by default (vs. CONSERVATIVE speculative inliner)
 * - PRE-INLINING: Runs early in pipeline to enable downstream optimizations
 * - PROFILE-DRIVEN: Uses call frequency and heuristics to avoid code bloat
 *
 * INLINING HEURISTICS:
 * - Inline if: size < 50 IR nodes AND call_count >= 2
 * - Inline if: size < 100 IR nodes AND is_hot_path (BRANCH_LIKELY)
 * - Inline if: size < 10 IR nodes (always inline tiny functions)
 * - Skip if: already_inlined > 3 times (prevent infinite expansion)
 *
 * PILLAR 2: Transactional inlining with shadow-buffered splicing
 * PILLAR 3: Sentinel validation of inlined IR structure
 */
class DGTLAggressiveInliner : public pipeline::DGTLOptimizationPass {
private:
    semantic::SymbolTable* m_symbols;
    analysis::DGTLAICostModel* m_ai_model;

    // Inlining thresholds
    static constexpr size_t MAX_INLINE_SIZE = 100;           // IR nodes
    static constexpr size_t SMALL_FUNCTION_THRESHOLD = 50;   // IR nodes
    static constexpr size_t TINY_FUNCTION_THRESHOLD = 10;    // IR nodes
    static constexpr uint32_t INLINE_DEPTH_LIMIT = 3;        // Max inline levels
    static constexpr uint32_t CALL_FREQUENCY_THRESHOLD = 2;  // Min calls to inline

    /**
     * @struct CallSite
     * @brief Represents a single call site in the IR stream.
     */
    struct CallSite {
        uint32_t call_node_idx;   // Index in IR stream
        uint32_t callee_id;       // Function ID from symbol table
        uint32_t call_count;      // Frequency of this call target
        bool is_hot_path;         // Marked with BRANCH_LIKELY
    };

    // Call graph: callee_id â†’ vector of call sites
    std::unordered_map<uint32_t, std::vector<CallSite>> m_call_graph;

    // Function sizes: callee_id â†’ IR node count
    std::unordered_map<uint32_t, uint32_t> m_function_sizes;

    // Inline counts: callee_id â†’ times already inlined
    std::unordered_map<uint32_t, uint32_t> m_inline_count;

public:
    /**
     * @brief Construct aggressive inliner with symbol table and AI cost model.
     * @param sym Symbol table for function metadata (optional)
     * @param ai_model AI cost predictor for profitability heuristics (optional)
     */
    explicit DGTLAggressiveInliner(
        semantic::SymbolTable* sym = nullptr,
        analysis::DGTLAICostModel* ai_model = nullptr) noexcept
        : m_symbols(sym), m_ai_model(ai_model) {}

    /**
     * @brief Execute aggressive inlining optimization pass.
     * ALGORITHM:
     * 1. Analyze call graph and function sizes
     * 2. Apply inlining heuristics to each call site
     * 3. Mark calls for inlining with flags
     * 4. PILLAR 3: Validate shadow integrity before commit
     * @param stream Target IR stream
     */
    void execute(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() == 0) {
            return;
        }

        stream.begin_transaction();

        try {
            // Phase 1: Analyze call graph and function sizes
            analyze_call_sites(stream);

            // Phase 2: Perform inlining with transactional safety
            bool modified = perform_aggressive_inlining(stream);

            // PILLAR 3: THE SENTINEL - Validate before commit
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

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PreInlining;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

private:
    /**
     * @brief Scan IR stream for CALL nodes and populate call graph and function sizes.
     * PILLAR 2: Call graph analysis with transactional safety
     * @param stream IR stream to analyze
     */
    void analyze_call_sites(const ir::IRStream& stream) noexcept {
        m_call_graph.clear();
        m_function_sizes.clear();
        m_inline_count.clear();

        const size_t stream_size = stream.size();

        // Scan for CALL nodes and estimate function sizes
        for (size_t i = 0; i < stream_size; ++i) {
            const auto& node = stream.get_node_readonly(i);

            // CALL is distinctly identified by OpCode; trait dispatch not practical here since CALL is unique
            // (IsSpeculative trait + ControlFlow interface without IsTerminator is reserved for CALL)
            if (node.opcode == ir::OpCode::CALL) {
                uint32_t callee_id = node.op_a;  // Function ID in symbol table

                // Estimate function size (simplified: stream size as proxy)
                if (m_symbols) {
                    uint32_t size = static_cast<uint32_t>(stream_size);
                    m_function_sizes[callee_id] = size;
                }

                CallSite site{
                    .call_node_idx = static_cast<uint32_t>(i),
                    .callee_id = callee_id,
                    .call_count = 1,
                    .is_hot_path = (node.flags & ir::HintFlags::BranchLikely) != 0
                };

                m_call_graph[callee_id].push_back(site);
            }
        }
    }

    /**
     * @brief Apply inlining heuristics and mark calls for inlining.
     * PILLAR 2: Shadow-buffered call site transformation
     * @param stream IR stream to inline
     * @return true if any inlining was performed
     */
    bool perform_aggressive_inlining(ir::IRStream& stream) noexcept {
        bool modified = false;
        uint32_t shadow_cursor = 0;

        const size_t stream_size = stream.size();

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            const auto& node = stream.get_node_readonly(i);

            // CALL opcode is distinctive; no trait-based alternative needed
            if (node.opcode == ir::OpCode::CALL) {
                uint32_t callee_id = node.op_a;

                // Check inlining heuristics
                if (should_inline(callee_id, node)) {
                    // Mark call for inlining with optimization flags
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
                    // Keep call as-is
                    sync_to_shadow(stream, i, shadow_cursor);
                }
            } else {
                // Copy non-call instruction
                sync_to_shadow(stream, i, shadow_cursor);
            }

            shadow_cursor++;
        }

        return modified;
    }

    /**
     * @brief Determine if a specific call site should be inlined.
     * Uses AI cost model for intelligent profitability predictions.
     * ALGORITHM:
     * 1. Check recursion depth limit (hard constraint)
     * 2. Predict inlining benefit using AI cost model
     * 3. Apply adaptive thresholds based on target precision
     * 4. Make decision with dynamic cost/benefit tradeoff
     * 
     * @param callee_id Function ID to inline
     * @param call_node The CALL instruction
     * @return true if inlining should proceed
     */
    bool should_inline(uint32_t callee_id, const ir::IRNode& call_node) noexcept {
        // Hard constraint 0: Recursion depth limit (always enforced)
        if (m_inline_count[callee_id] >= INLINE_DEPTH_LIMIT) {
            return false;
        }

        // Get function size
        auto it_size = m_function_sizes.find(callee_id);
        if (it_size == m_function_sizes.end()) {
            return false;
        }

        uint32_t func_size = it_size->second;
        bool is_hot = (call_node.flags & ir::HintFlags::BranchLikely) != 0;

        // Hard constraint 1: Always inline tiny functions (< TINY_FUNCTION_THRESHOLD)
        if (func_size < TINY_FUNCTION_THRESHOLD) {
            return true;
        }

        // Hard constraint 2: Never inline functions larger than MAX_INLINE_SIZE
        if (func_size > MAX_INLINE_SIZE) {
            return false;
        }

        // Get call frequency
        auto it_calls = m_call_graph.find(callee_id);
        uint32_t call_freq = (it_calls != m_call_graph.end())
            ? static_cast<uint32_t>(it_calls->second.size())
            : 1;

        // ===== AI Cost Model-Driven Decision =====
        if (m_ai_model) {
            // Use AI model to estimate inlining benefit
            // The model predicts how much better the code gets if we inline
            // Score range: 0.0 (no benefit) to 1.0 (maximum benefit)
            float ai_benefit = estimate_inlining_benefit_ai(
                callee_id,
                func_size,
                call_freq,
                is_hot,
                call_node
            );

            // Dynamic threshold adjustment based on target precision
            float threshold = compute_inline_threshold(call_freq, is_hot);

            // Decision: inline if AI predicts sufficient benefit
            if (ai_benefit >= threshold) {
                return true;
            }
            return false;
        }

        // Fallback: Use static heuristics if AI model not available
        return use_static_heuristics(func_size, call_freq, is_hot);
    }

private:
    /**
     * @brief Estimate inlining benefit using AI cost model.
     * Predicts whether inlining this call will improve overall performance.
     * 
     * Factors:
     * - Function cost: smaller = more likely to inline
     * - Call frequency: higher = more benefit from inlining
     * - Caller hotness: hot calls = more benefit from removing call overhead
     * - Code size impact: large inlines may hurt I-cache
     * 
     * @return Normalized benefit score [0.0, 1.0]
     */
    [[nodiscard]] float estimate_inlining_benefit_ai(
        uint32_t callee_id,
        uint32_t func_size,
        uint32_t call_freq,
        bool is_hot,
        const ir::IRNode& call_node
    ) noexcept {
        if (!m_ai_model) {
            return 0.0f;
        }

        // Factor 1: Size factor (normalize to [0, 1])
        // Smaller functions have higher benefit
        // Threshold: functions 50% of MAX_INLINE_SIZE get 0.5 score
        const float size_factor = 1.0f / (1.0f + static_cast<float>(func_size) / 50.0f);

        // Factor 2: Call frequency factor (normalize to [0, 1])
        // Higher frequency = more savings from eliminating call overhead
        const float freq_factor = std::clamp(
            static_cast<float>(call_freq) / 10.0f,
            0.0f, 1.0f
        );

        // Factor 3: Hotness factor
        // Hot paths benefit more from inlining (no call overhead in critical path)
        const float hotness_factor = is_hot ? 1.0f : 0.6f;

        // Factor 4: AI cost-based benefit prediction
        // Use AI model to estimate function complexity and inline benefit
        float cost_benefit = predict_ai_inline_benefit(call_node, func_size);

        // Combined score: weighted geometric mean of factors
        // Weights: size=0.25, freq=0.25, hotness=0.20, cost=0.30
        const float combined = 
            0.25f * size_factor +
            0.25f * freq_factor +
            0.20f * hotness_factor +
            0.30f * cost_benefit;

        return std::clamp(combined, 0.0f, 1.0f);
    }

    /**
     * @brief Use AI model to predict the actual runtime benefit of inlining.
     * Predicts reduction in cycles from eliminating call/return overhead.
     */
    [[nodiscard]] float predict_ai_inline_benefit(
        const ir::IRNode& call_node,
        uint32_t func_size
    ) noexcept {
        if (!m_ai_model) {
            return 0.5f;  // Conservative default
        }

        // Estimate call overhead: ~10 cycles (conservative for x64)
        constexpr float kCallOverheadCycles = 10.0f;

        // Estimate function body cost per node
        const float func_body_cost = m_ai_model->predict_node_cost(call_node);

        // Benefit ratio: call_overhead / (call_overhead + function_cost)
        // Higher if function is small relative to call overhead
        const float function_total_cost = func_body_cost * static_cast<float>(func_size);
        const float benefit_ratio = kCallOverheadCycles / 
                                   (kCallOverheadCycles + function_total_cost);

        return std::clamp(benefit_ratio, 0.0f, 1.0f);
    }

    /**
     * @brief Compute dynamic inlining threshold based on call characteristics.
     * Adapts to frequency and hotness to balance code size and performance.
     */
    [[nodiscard]] float compute_inline_threshold(
        uint32_t call_freq,
        bool is_hot
    ) noexcept {
        // Base threshold: 0.5 (moderate confidence required)
        float threshold = 0.5f;

        // Lower threshold for high-frequency calls (more benefit from inlining)
        if (call_freq >= 10) {
            threshold -= 0.15f;  // 0.35
        } else if (call_freq >= 5) {
            threshold -= 0.10f;  // 0.40
        } else if (call_freq >= 2) {
            threshold -= 0.05f;  // 0.45
        }

        // Lower threshold for hot path (critical path optimization)
        if (is_hot) {
            threshold -= 0.10f;
        }

        // Ensure threshold stays in reasonable bounds
        return std::clamp(threshold, 0.2f, 0.7f);
    }

    /**
     * @brief Static fallback heuristics when AI model is unavailable.
     * Provides deterministic behavior for testing/benchmarking.
     */
    [[nodiscard]] bool use_static_heuristics(
        uint32_t func_size,
        uint32_t call_freq,
        bool is_hot
    ) noexcept {
        // Heuristic 1: Small hot functions with multiple call sites
        if (func_size <= SMALL_FUNCTION_THRESHOLD && call_freq >= CALL_FREQUENCY_THRESHOLD) {
            return true;
        }

        // Heuristic 2: Hot path calls in medium-sized functions
        if (is_hot && func_size <= MAX_INLINE_SIZE / 2) {
            return true;
        }

        // Heuristic 3: Very small functions
        if (func_size < TINY_FUNCTION_THRESHOLD * 2) {
            return true;
        }

        return false;
    }

public:
    [[nodiscard]] uint32_t get_preserved_analyses() const noexcept override {
        // This pass modifies function bodies and call sites, so most analyses become invalid.
        // However, we preserve structural information if available.
        return static_cast<uint32_t>(
            pipeline::PreservedAnalysisKind::TargetData |  // ISA-specific data never changes
            pipeline::PreservedAnalysisKind::CallGraph     // Call graph structure preserved (same calls, just inlined)
        );
    }

    [[nodiscard]] uint32_t get_provided_analyses() const noexcept override {
        // This pass doesn't provide new analyses; it consumes existing ones
        return 0u;
    }


    /**
     * @brief Copy node from main tier to shadow tier with preservation of all metadata.
     * PILLAR 2: Shadow-tier state isolation
     * @param stream IR stream
     * @param src_idx Source index in main tier
     * @param dst_idx Destination index in shadow tier
     */
    void sync_to_shadow(ir::IRStream& stream, uint32_t src_idx, uint32_t dst_idx) const noexcept {
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
