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
 * STEP 28: SPECULATIVE DEAD CODE ELIMINATION (TRANSACTIONAL)
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED SHADOW STREAMS)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp" 
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Verified_Formal.hpp"

namespace dgtl::pass {

// Force explicit mapping for the IR namespace to prevent resolution errors
namespace ir = dgtl::ir;
namespace analysis = dgtl::analysis;
namespace pipeline = dgtl::pipeline;

/**
 * @class DGTLSpeculativeDCE
 * @brief Performs speculative path pruning and CFG reordering on the Shadow Tier.
 * Inherits from DGTLOptimization to integrate with the Task Pipeline Manager.
 */
class DGTLSpeculativeDCE : public pipeline::DGTLOptimizationPass {
    struct BlockProfile {
        uint32_t start_idx;
        uint32_t len;
        float probability; 
    };

    const analysis::DGTLAICostModel* internal_model = nullptr;

public:
    [[nodiscard]] const char* get_pass_name() const noexcept override { return "SpeculativeDCE"; }
    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override { return pipeline::PassStage::LateCleanup; }
    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override { return pipeline::PassAffinity::Medium; }
    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override { return pipeline::PassAffinity::High; }
    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override { return pipeline::PassAffinity::Low; }
    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override { return pipeline::PassAffinity::High; }
    DGTLSpeculativeDCE() = default;
    explicit DGTLSpeculativeDCE(const analysis::DGTLAICostModel& model) : internal_model(&model) {}

    /**
     * @brief Entry Point for the Pipeline Manager.
     * Implements the "Double-Buffered Safety Net" for BIR 1.0.0.
     */
    void execute(ir::IRStream& stream) override {
        if (stream.size() == 0) [[unlikely]] return;

        if (!internal_model) {
            (void)stream.validate_shadow_integrity();
            return;
        }

        // PILLAR 2: Enter Transactional Mode
        stream.begin_transaction();

        const bool success = prune_and_reorder_transactional(stream, *internal_model);

        // PILLAR 3: THE SENTINEL - Final validation before commit
        if (!success) {
            stream.rollback_transaction();
        } else if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
        } else {
            stream.commit_transaction();
        }
    }

private:
    /**
     * @brief High-performance CFG Reordering.
     * Reorganizes the Shadow Tier so Hot Paths are contiguous.
     */
    bool prune_and_reorder_transactional(ir::IRStream& stream, const analysis::DGTLAICostModel& ai_model) {
        std::vector<BlockProfile> blocks = analyze_cfg_probability(stream, ai_model);
        if (blocks.empty()) return false;
        
        // Partition blocks: Hot blocks first, Cold blocks (prob < 0.01%) last
        auto frozen_it = std::stable_partition(blocks.begin(), blocks.end(), [](const auto& b) {
            return b.probability > 0.0001f;
        });

        uint32_t shadow_cursor = 0;

        // 1. Write Hot Path to Shadow Buffer
        for (auto it = blocks.begin(); it != frozen_it; ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, true);
        }

        // 2. Align to Cache Line (4-node stride for 128-byte cache lines)
        align_shadow_to_cache_line(stream, shadow_cursor);

        // 3. Write Cold Path
        for (auto it = frozen_it; it != blocks.end(); ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, false);
        }

        stream.update_shadow_size(shadow_cursor);
        return shadow_cursor > 0;
    }

    std::vector<BlockProfile> analyze_cfg_probability(const ir::IRStream& s, const analysis::DGTLAICostModel& ai) {
        (void)ai;
        std::vector<BlockProfile> blocks;
        uint32_t last_start = 0;
        const size_t count = s.size();
        
        for (uint32_t i = 0; i < count; ++i) {
            ir::IRNode node = s.get_node_readonly(i); 
            
            bool is_term = (node.opcode == ir::OpCode::GOTO || 
                            node.opcode == ir::OpCode::BRANCH || 
                            node.opcode == ir::OpCode::RET ||
                            node.opcode == ir::OpCode::BRANCH_COND);

            if (is_term || i == (count - 1)) {
                float prob = 1.0f; 
                if (node.flags & ir::Flags::BRANCH_UNLIKELY) prob = 0.0000001f;
                else if (node.flags & ir::Flags::BRANCH_LIKELY) prob = 1.0f;

                blocks.push_back({last_start, (i - last_start) + 1, prob});
                last_start = i + 1;
            }
        }
        return blocks; 
    }

    void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        core.intent_hash = ir::IRNode::calculate_hash(
            atom.opcode,
            core.result_id,
            core.op_a,
            core.op_b,
            core.op_c
        );
    }

    void append_block_to_shadow(ir::IRStream& s, uint32_t& cursor, const BlockProfile& p) noexcept {
        if (cursor + p.len > s.capacity()) return;

        for (uint32_t i = 0; i < p.len; ++i) {
            ir::IRNode node = s.get_node_readonly(p.start_idx + i);
            
            ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
            ir::IRCore& s_core = s.get_shadow_core(cursor);

            s_atom.opcode = node.opcode;
            s_atom.flags = node.flags;
            s_core.result_id = node.result_id;
            s_core.op_a = node.op_a;
            s_core.op_b = node.op_b;
            s_core.op_c = node.op_c;
            
            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void align_shadow_to_cache_line(ir::IRStream& s, uint32_t& cursor) noexcept {
        // BIR 1.0.0: Align to 4-atom (16-byte) boundaries for vector dispatch
        uint32_t align_gap = (4 - (cursor & 3)) & 3; 
        for (uint32_t i = 0; i < align_gap && (cursor < s.capacity()); ++i) {
            ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
            ir::IRCore& s_core = s.get_shadow_core(cursor);

            s_atom.opcode = ir::OpCode::NOP;
            s_atom.flags = ir::Flags::NONE;
            s_core.result_id = ir::INVALID_SLOT;
            s_core.op_a = 0;
            s_core.op_b = 0;
            s_core.op_c = 0;
            
            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void inject_branch_hint_shadow(ir::IRStream& s, uint32_t cursor, bool likely) noexcept {
        if (cursor == 0) return;
        
        const uint32_t target_idx = cursor - 1;
        auto& s_atom = s.get_shadow_atom(target_idx);
        
        // Internal check for terminator type
        bool is_term = (s_atom.opcode == ir::OpCode::GOTO || 
                        s_atom.opcode == ir::OpCode::BRANCH || 
                        s_atom.opcode == ir::OpCode::BRANCH_COND);

        if (is_term) {
            if (likely) {
                s_atom.flags |= ir::Flags::BRANCH_LIKELY;
                s_atom.flags &= ~ir::Flags::BRANCH_UNLIKELY;
            } else {
                s_atom.flags |= ir::Flags::BRANCH_UNLIKELY;
                s_atom.flags &= ~ir::Flags::BRANCH_LIKELY;
            }
            finalize_shadow_node(s, target_idx);
        }
    }
};

} // namespace dgtl::pass




