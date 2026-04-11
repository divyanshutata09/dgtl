/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 38: SPECULATIVE ALIAS HOISTER (LICM+)
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED CROSS-STREAM MOVE)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <vector>
#include <stdexcept>
#include "../ir/Global_IR.hpp"
#include "Memory_Dependency_Smasher.hpp"
#include "../analysis/AI_Cost_Model.hpp"

namespace dgtl::opt {

class DGTLSpeculativeAliasHoister {
    DGTLMemorySmasher& smasher;
    DGTLAICostModel& ai_model;

public:
    DGTLSpeculativeAliasHoister(DGTLMemorySmasher& s, DGTLAICostModel& ai) 
        : smasher(s), ai_model(ai) {}

    /**
     * @brief DGTLoptManager Bridge.
     * Uses your IRStream Transactional API for local block safety.
     */
    void execute(ir::IRStream& stream) {
        if (stream.size() == 0) [[unlikely]] return;
        
        stream.begin_transaction();
        // Local optimization logic using BIR 2.0 accessors
        if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
            return;
        }
        stream.commit_transaction();
    }

    /**
     * @brief The "Zero-Corruption" Hoisting Strike.
     * Moves loads from loop_body to pre_header using Shadow Splicing.
     */
    void hoist_speculative_invariants(ir::IRStream& loop_body, ir::IRStream& pre_header) {
        if (loop_body.size() == 0) return;

        // PILLAR 2: Start Dual-Stream Transaction
        loop_body.begin_transaction();
        pre_header.begin_transaction();

        const size_t body_count = loop_body.size();
        for (uint32_t i = 0; i < body_count; ++i) {
            // Accessing Stream A via your read-only accessor
            ir::IRNode node = loop_body.get_node_readonly(i);

            if (node.opcode == ir::OpCode::LOAD && !(node.flags & ir::Flags::IS_VOLATILE)) {
                bool should_hoist = false;

                // FIX: opting index 'i' instead of the 'node' object
                if (smasher.is_guaranteed_invariant(loop_body, i)) {
                    should_hoist = true;
                } else {
                    // Check probability of index 'i' aliasing within the current loop stream
                    float alias_prob = smasher.predict_alias_probability(loop_body, i, i);
                    if (alias_prob < 0.0001f) {
                        inject_runtime_alias_guard_transactional(pre_header, node);
                        should_hoist = true;
                    }
                }

                if (should_hoist) {
                    move_to_preheader_transactional(loop_body, i, pre_header);
                } else {
                    // Not hoisted: Ensure Shadow Tier B still contains the original node
                    sync_shadow_node(loop_body, i, node);
                }
            } else {
                // Regular instruction: Sync to Shadow Tier B
                sync_shadow_node(loop_body, i, node);
            }
        }

        // Update Shadow Stream counts
        loop_body.set_shadow_count(body_count);

        // PILLAR 3: Validate both streams before atomic swap
        if (!loop_body.validate_shadow_integrity() || !pre_header.validate_shadow_integrity()) {
            loop_body.rollback_transaction();
            pre_header.rollback_transaction();
            return;
        }

        loop_body.commit_transaction();
        pre_header.commit_transaction();
    }

private:
    /**
     * @brief Writes a synchronization node to the Shadow Tier.
     */
    void sync_shadow_node(ir::IRStream& s, uint32_t idx, const ir::IRNode& node) {
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash};
    }

    void inject_runtime_alias_guard_transactional(ir::IRStream& pre, const ir::IRNode& load_node) {
        uint32_t cursor = pre.size(); // Use current shadow size for insertion
        if (cursor >= pre.capacity()) return;

        ir::IRAtom& s_atom = pre.get_shadow_atom(cursor);
        ir::IRCore& s_core = pre.get_shadow_core(cursor);

        s_atom = {ir::OpCode::GUARD_ALIAS, ir::Flags::REQUIRES_DEpass_TARGET};
        s_core = {ir::INVALID_SLOT, load_node.op_a, 0, 0, 0};
        
        ir::IRNode temp = pre.get_node(cursor); // Construct temp from shadow for hash
        temp.refresh_intent_hash();
        s_core.intent_hash = temp.intent_hash;
        
        pre.set_shadow_count(cursor + 1);
    }

    void move_to_preheader_transactional(ir::IRStream& body, uint32_t body_idx, ir::IRStream& pre) {
        uint32_t pre_cursor = pre.size();
        if (pre_cursor >= pre.capacity()) return;

        ir::IRNode original = body.get_node_readonly(body_idx);

        // 1. Move Node to Preheader Shadow Tier
        ir::IRAtom& p_atom = pre.get_shadow_atom(pre_cursor);
        ir::IRCore& p_core = pre.get_shadow_core(pre_cursor);
        p_atom = {original.opcode, original.flags};
        p_core = {original.result_id, original.op_a, original.op_b, original.op_c, original.intent_hash};
        
        ir::IRNode p_temp = pre.get_node(pre_cursor);
        p_temp.refresh_intent_hash();
        p_core.intent_hash = p_temp.intent_hash;
        pre.set_shadow_count(pre_cursor + 1);

        // 2. Replace Loop Body Node in Shadow Tier with REG_MOVE
        ir::IRAtom& b_atom = body.get_shadow_atom(body_idx);
        ir::IRCore& b_core = body.get_shadow_core(body_idx);
        b_atom = {ir::OpCode::REG_MOVE, (uint16_t)(original.flags | ir::Flags::IS_HOISTED)};
        b_core = {original.result_id, original.result_id, 0, 0, 0};
        
        ir::IRNode b_temp = body.get_node(body_idx);
        b_temp.refresh_intent_hash();
        b_core.intent_hash = b_temp.intent_hash;
    }
};

} // namespace dgtl::opt
