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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 33: ADVANCED SIMD GATHER/SCATTER OPTIMIZER
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED VECTORIZATION)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <stdexcept>
#include <algorithm>
#include <vector>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::pass {

class DGTLGatherScatterOptimizer {
    dgtl::analysis::DGTLMemorySmasher& smasher;
    dgtl::analysis::DGTLAICostModel& ai_model;
    static constexpr uint32_t GatherLaneCount = 16;
    static constexpr float GatherCostSlack = 0.92f;
    static constexpr float MaxAliasRisk = 0.30f;
    static constexpr uint32_t CacheLineSize = 64; // Alignment target for world-class throughput

public:
    DGTLGatherScatterOptimizer(dgtl::analysis::DGTLMemorySmasher& s, dgtl::analysis::DGTLAICostModel& ai) noexcept
        : smasher(s), ai_model(ai) {}

    /**
     * @brief The "Zero-Corruption" Vector Strike.
     * Converts scalar load chains into 512-bit Gathers via Shadow-Tier mutation.
     */
    void optimize_indirect_access(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        if (stream_size < GatherLaneCount) return;

        // PILLAR 2: Open Transactional Tier
        stream.begin_transaction();

        // Initial sync to Shadow Tier
        // Performance Update: Software prefetching for the stream buffer
        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            if (i % 8 == 0 && (i + 16) < stream_size) [[likely]] {
                stream.prefetch_node(i + 16);
            }
            sync_node_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i <= static_cast<uint32_t>(stream_size) - GatherLaneCount; ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);

            // OpCode Sync: LOAD = 10
            if (node.opcode != ir::OpCode::LOAD || !is_indirect_address(node)) {
                continue;
            }
            if (!is_gather_candidate_window(stream, i)) continue;

            // 1. Dependency Check: Can we group 16 scalar loads?
            if (!smasher.can_group_access(stream, i, GatherLaneCount)) continue;

            // 2. AI Cost Validation: Compare Scalar vs. SIMD
            ir::IRNode probe = node;
            convert_to_gather_node(probe);

            float scalar_cost = 0.0f;
            for (uint32_t k = 0; k < GatherLaneCount; ++k) {
                scalar_cost += ai_model.predict_node_cost(stream.get_node_readonly(i + k));
            }
            float gather_cost = estimate_gather_cost(stream, i, probe);

            // Optimization: Favor Gathers that align with Cache-Line boundaries
            gather_cost *= is_cache_line_aligned(node.op_a) ? 0.85f : 1.0f;

            if (gather_cost <= scalar_cost * GatherCostSlack) {
                // PILLAR 3: Shadow Synthesis (Mutates Stream B)
                apply_gather_transaction(stream, i);

                // Advance index to skip absorbed scalar slots
                i += (GatherLaneCount - 1);
            }
        }

        // PILLAR 3 Sentinel: Validate Shadow integrity before swapping
        if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
            return;
        }

        stream.commit_transaction();
    }

private:
    void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash, 0, 0};
    }

    [[nodiscard]] bool is_indirect_address(const ir::IRNode& node) const noexcept {
        return (node.flags & ir::Flags::IS_SPEC_CONST) != 0 &&
               ir::IRTraits::is_memory_op(node.opcode) &&
               !ir::IRTraits::has_side_effects(node.opcode, node.flags) &&
               (node.flags & ir::Flags::IS_SIMD_BURST) == 0 &&
               node.op_a != ir::INVALID_SLOT;
    }

    [[nodiscard]] bool is_cache_line_aligned(uint32_t address) const noexcept {
        return (address % CacheLineSize) == 0;
    }

    [[nodiscard]] bool is_gather_candidate_window(const ir::IRStream& stream, uint32_t head_idx) const noexcept {
        const ir::IRNode head = stream.get_node_readonly(head_idx);
        uint32_t min_addr = head.op_a;
        uint32_t max_addr = head.op_a;

        for (uint32_t j = 0; j < GatherLaneCount; ++j) {
            const ir::IRNode node = stream.get_node_readonly(head_idx + j);
            if (node.opcode != ir::OpCode::LOAD || !is_indirect_address(node)) {
                return false;
            }
            if ((node.flags & ir::Flags::IS_FUSED) != 0 || (node.flags & ir::Flags::IS_DEAD) != 0) {
                return false;
            }
            min_addr = std::min(min_addr, node.op_a);
            max_addr = std::max(max_addr, node.op_a);
        }

        // World-Class Constraint: Ensure indices fit within a single TLB page where possible
        return (max_addr - min_addr) <= 4096; 
    }

    [[nodiscard]] float estimate_gather_cost(const ir::IRStream& stream, uint32_t head_idx, const ir::IRNode& probe) noexcept {
        float gather_cost = ai_model.predict_node_cost(probe);
        float alias_risk = 0.0f;

        for (uint32_t j = 1; j < GatherLaneCount; ++j) {
            alias_risk = std::max(alias_risk, smasher.predict_alias_probability(stream, head_idx, head_idx + j));
            if (alias_risk > MaxAliasRisk) {
                return gather_cost + 1000.0f;
            }
        }

        const ir::IRNode tail = stream.get_node_readonly(head_idx + GatherLaneCount - 1);
        const uint32_t address_span = (tail.op_a > probe.op_a) ? (tail.op_a - probe.op_a) : (probe.op_a - tail.op_a);
        
        // Penalty for spanning across too many cache lines (Thrashing check)
        const float span_penalty = std::min(12.0f, static_cast<float>(address_span / CacheLineSize) * 1.2f);
        const float alias_penalty = alias_risk * 15.0f;

        return gather_cost + span_penalty + alias_penalty;
    }

    /**
     * @brief Mutates Shadow Tier and NOPs absorbed followers.
     */
    void apply_gather_transaction(ir::IRStream& stream, uint32_t head_idx) {
        ir::IRAtom& h_atom = stream.get_shadow_atom(head_idx);
        ir::IRCore& h_core = stream.get_shadow_core(head_idx);

        // Transform Shadow Head to Gather (GATHER_F32_512 = 0x8101)
        h_atom.opcode = ir::OpCode::GATHER_F32_512;
        
        // Inject NON_TEMPORAL hint if address span is huge to avoid L1 cache pollution
        const uint32_t head_address = stream.get_node_readonly(head_idx).op_a;
        const uint32_t tail_address = stream.get_node_readonly(head_idx + GatherLaneCount - 1).op_a;
        const uint32_t span = (tail_address > head_address)
            ? (tail_address - head_address)
            : (head_address - tail_address);
        if (span > 1024) h_atom.flags |= ir::Flags::NON_TEMPORAL_HINT;

        h_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED | ir::Flags::IS_OPTIMIZED);
        
        // PILLAR 3: Refresh Intent Hash via standardized calculation
        h_core.intent_hash = ir::IRNode::calculate_hash(
            h_atom.opcode,
            h_core.result_id,
            h_core.op_a,
            h_core.op_b,
            h_core.op_c
        );

        // Invalidate next 15 scalar slots in Shadow Tier
        for (uint32_t j = 1; j < GatherLaneCount; ++j) {
            ir::IRAtom& f_atom = stream.get_shadow_atom(head_idx + j);
            ir::IRCore& f_core = stream.get_shadow_core(head_idx + j);
            
            f_atom = {ir::OpCode::NOP, ir::Flags::NONE};
            f_core = {ir::INVALID_SLOT, 0, 0, 0, 0, 0, 0};
            
            // Recalculate hash for Shadow NOP slots to satisfy Sentinel
            f_core.intent_hash = ir::IRNode::calculate_hash(
                f_atom.opcode,
                f_core.result_id,
                f_core.op_a,
                f_core.op_b,
                f_core.op_c
            );
        }
    }

    void convert_to_gather_node(ir::IRNode& node) noexcept {
        node.opcode = ir::OpCode::GATHER_F32_512;
        node.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED | ir::Flags::IS_OPTIMIZED);
    }
};

} // namespace dgtl::pass
