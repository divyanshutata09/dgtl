/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 28: SPECULATIVE DEAD CODE ELIMINATION (TRANSACTIONAL)
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED SHADOW STREAMS)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "ir/Global_IR.hpp"
#include "core/Arena.hpp"
#include "pipeline/Pass_Manager.hpp" 
#include "analysis/AI_Cost_Model.hpp"

namespace dgtl::opt {

/**
 * @class DGTLSpeculativeDCE
 * @brief Performs speculative path pruning and CFG reordering on the Shadow Tier.
 * Inherits from DGTLOptimizationPass for polymorphic execution in the TPM.
 */
class DGTLSpeculativeDCE : public DGTLOptimizationPass {
    struct BlockProfile {
        uint32_t start_idx;
        uint32_t len;
        float probability; 
    };

    const DGTLAICostModel* internal_model = nullptr;

public:
    DGTLSpeculativeDCE() = default;
    explicit DGTLSpeculativeDCE(const DGTLAICostModel& model) : internal_model(&model) {}

    /**
     * @brief DGTLPassManager Entry Point.
     * Implements the "Double-Buffered Safety Net" for BIR 2.0.
     */
    void execute(ir::IRStream& stream) override {
        if (stream.size() == 0) [[unlikely]] return;

        // PILLAR 2: Enter Transactional Mode
        stream.begin_transaction();

        try {
            // If no model provided, we perform basic liveness/integrity check
            bool success = false;
            if (internal_model) {
                success = prune_and_reorder_transactional(stream, *internal_model);
            } else {
                success = stream.validate_shadow_integrity();
            }

            // PILLAR 3: THE SENTINEL - Local verification before PassManager final gate
            if (!success || !stream.validate_shadow_integrity()) {
                stream.rollback_transaction();
            }
        } catch (...) {
            stream.rollback_transaction();
        }
    }

    /**
     * @brief Speculative Bridge for Loop Unswitching.
     * Operates directly on an existing transaction started by another pass.
     */
    bool execute_speculative(ir::IRStream& stream) {
        if (!stream.is_transactional() || stream.size() == 0) [[unlikely]] return false;

        if (internal_model) {
            return prune_and_reorder_transactional(stream, *internal_model);
        }
        
        return stream.validate_shadow_integrity();
    }

private:
    /**
     * @brief High-performance CFG Reordering on Shadow Tiers.
     */
    bool prune_and_reorder_transactional(ir::IRStream& stream, const DGTLAICostModel& ai_model) {
        std::vector<BlockProfile> blocks = analyze_cfg_probability(stream, ai_model);
        if (blocks.empty()) return false;
        
        // Partition Hot vs Cold blocks based on probability threshold
        auto frozen_it = std::stable_partition(blocks.begin(), blocks.end(), [](const auto& b) {
            return b.probability > 0.0001f;
        });

        uint32_t shadow_cursor = 0;

        // 1. Write Hot Path to Shadow
        for (auto it = blocks.begin(); it != frozen_it; ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, true);
        }

        // 2. Align to Cache Line for TSO-Spin optimization (Pillar 1/2)
        align_shadow_to_cache_line(stream, shadow_cursor);

        // 3. Write Cold Path to Shadow
        for (auto it = frozen_it; it != blocks.end(); ++it) {
            append_block_to_shadow(stream, shadow_cursor, *it);
            inject_branch_hint_shadow(stream, shadow_cursor, false);
        }

        stream.update_shadow_size(shadow_cursor);
        return shadow_cursor > 0;
    }

    std::vector<BlockProfile> analyze_cfg_probability(const ir::IRStream& s, const DGTLAICostModel& ai) {
        std::vector<BlockProfile> blocks;
        uint32_t last_start = 0;
        const size_t count = s.size();
        
        for (uint32_t i = 0; i < count; ++i) {
            ir::IRNode node = s.get_node_readonly(i); 
            if (node.is_terminator() || i == (count - 1)) {
                float prob = 1.0f; 
                if (node.flags & ir::Flags::BRANCH_UNLIKELY) prob = 0.0000001f;
                else if (node.flags & ir::Flags::BRANCH_LIKELY) prob = 1.0f;

                blocks.push_back({last_start, (i - last_start) + 1, prob});
                last_start = i + 1;
            }
        }
        return blocks; 
    }

    /**
     * @brief PILLAR 3: Finalizes the node hash in the shadow buffer.
     */
    void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
        auto proxy = s.get_shadow_node_proxy(idx);
        proxy.refresh_intent_hash();
        s.get_shadow_core(idx).intent_hash = proxy.intent_hash;
    }

    void append_block_to_shadow(ir::IRStream& s, uint32_t& cursor, const BlockProfile& p) noexcept {
        if (cursor + p.len > s.capacity()) return;

        for (uint32_t i = 0; i < p.len; ++i) {
            ir::IRNode node = s.get_node_readonly(p.start_idx + i);
            
            s.get_shadow_atom(cursor) = {node.opcode, node.flags};
            s.get_shadow_core(cursor) = {node.result_id, node.op_a, node.op_b, node.op_c, 0};
            
            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void align_shadow_to_cache_line(ir::IRStream& s, uint32_t& cursor) noexcept {
        // BIR 2.0 alignment: Ensure blocks start on 4-node boundaries
        uint32_t align_gap = (4 - (cursor & 3)) & 3; 
        for (uint32_t i = 0; i < align_gap && (cursor < s.capacity()); ++i) {
            s.get_shadow_atom(cursor) = {ir::OpCode::NOP, ir::Flags::NONE};
            s.get_shadow_core(cursor) = {0, 0, 0, 0, 0};
            
            finalize_shadow_node(s, cursor);
            cursor++;
        }
    }

    void inject_branch_hint_shadow(ir::IRStream& s, uint32_t cursor, bool likely) noexcept {
        if (cursor == 0) return;
        
        const uint32_t target_idx = cursor - 1;
        auto& s_atom = s.get_shadow_atom(target_idx);
        
        // FIX: Replaced non-existent get_shadow_node_readonly with standard get_node_readonly
        auto node_checker = s.get_node_readonly(target_idx);

        if (node_checker.is_terminator()) {
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

} // namespace dgtl::opt
