/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 32: AI-GUIDED SPECULATIVE INLINER (TRANSACTIONAL)
 * PILLAR 2: TRANSACTIONAL IR (SHADOW-BUFFERED SPLICING)
 * PILLAR 3: THE SENTINEL (L1-I CACHE & INVARIANT GUARD)
 */

#pragma once
#include <vector>
#include <stdexcept>
#include "../ir/Global_IR.hpp"
#include "../analysis/AI_Cost_Model.hpp
#include "../core/Symbol_Table.hpp"

namespace dgtl::opt {

class DGTLSpeculativeInliner {
    DGTLAICostModel& ai_model;
    core::DGTLSymbolTable& symbols;
    
    static constexpr size_t L1_I_CACHE_LIMIT = 32768; 
    static constexpr size_t BYTES_PER_NODE = 16;

public:
    DGTLSpeculativeInliner(DGTLAICostModel& ai, core::DGTLSymbolTable& sym) 
        : ai_model(ai), symbols(sym) {}

    /**
     * @brief DGTLPassManager Bridge.
     * Uses your IRStream Transactional API to prevent in-place corruption.
     */
    void execute(ir::IRStream& caller_stream) {
        if (caller_stream.size() == 0) [[unlikely]] return;

        // PILLAR 2: Start Shadow Transaction (Stream B)
        caller_stream.begin_transaction();

        try {
            bool modified = process_inlining_shadow(caller_stream);

            // PILLAR 3: THE SENTINEL
            // Validate via your Bit-Level Invariant Check
            if (modified && caller_stream.validate_shadow_integrity()) {
                caller_stream.commit_transaction();
            } else {
                caller_stream.rollback_transaction();
            }
        } catch (...) {
            caller_stream.rollback_transaction();
            throw;
        }
    }

private:
    /**
     * @brief BIR 2.0 Shadow Splicing logic.
     */
    bool process_inlining_shadow(ir::IRStream& stream) {
        bool any_change = false;
        uint32_t shadow_cursor = 0;
        const size_t original_count = stream.size();

        for (uint32_t i = 0; i < original_count; ++i) {
            // Using your get_node_readonly for safe access to Stream A
            ir::IRNode node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::CALL) {
                uint32_t callee_id = node.op_a;
                const ir::IRStream& callee = symbols.get_function_ir(callee_id);

                if (callee.size() > 0 && should_inline(stream, callee)) {
                    append_inlined_callee(stream, shadow_cursor, callee);
                    any_change = true;
                    continue; 
                }
            }

            // Copy to Shadow Tier (Stream B)
            if (shadow_cursor < stream.capacity()) {
                ir::IRAtom& s_atom = stream.get_shadow_atom(shadow_cursor);
                ir::IRCore& s_core = stream.get_shadow_core(shadow_cursor);
                
                s_atom = {node.opcode, node.flags};
                s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash};
                shadow_cursor++;
            }
        }

        stream.set_shadow_count(shadow_cursor);
        return any_change;
    }

    bool should_inline(const ir::IRStream& caller, const ir::IRStream& callee) {
        float benefit = ai_model.predict_inlined_benefit(caller, callee);
        if (is_cache_risky(caller, callee)) return false;
        return benefit > 0.5f; 
    }

    bool is_cache_risky(const ir::IRStream& caller, const ir::IRStream& callee) {
        size_t total_bytes = (caller.size() + callee.size()) * BYTES_PER_NODE;
        return total_bytes > static_cast<size_t>(L1_I_CACHE_LIMIT * 0.8); 
    }

    /**
     * @brief Alpha-Renaming during Shadow Splice.
     */
    void append_inlined_callee(ir::IRStream& s, uint32_t& cursor, const ir::IRStream& callee) {
        // vreg_offset derived from your get_max_id accessor
        uint32_t vreg_offset = s.get_max_id() + 1;

        for (uint32_t j = 0; j < callee.size(); ++j) {
            ir::IRNode inlined_node = callee.get_node_readonly(j);
            
            // Alpha-Renaming: Shift IDs to avoid collisions in the caller's namespace
            if (inlined_node.result_id != ir::INVALID_SLOT) inlined_node.result_id += vreg_offset;
            if (inlined_node.op_a != ir::INVALID_SLOT) inlined_node.op_a += vreg_offset;
            if (inlined_node.op_b != ir::INVALID_SLOT) inlined_node.op_b += vreg_offset;
            if (inlined_node.op_c != ir::INVALID_SLOT) inlined_node.op_c += vreg_offset;

            // PILLAR 3: Recalculate Intent Hash for the new specialized state
            inlined_node.refresh_intent_hash();

            if (cursor < s.capacity()) {
                ir::IRAtom& s_atom = s.get_shadow_atom(cursor);
                ir::IRCore& s_core = s.get_shadow_core(cursor);
                
                s_atom = {inlined_node.opcode, inlined_node.flags};
                s_core = {inlined_node.result_id, inlined_node.op_a, inlined_node.op_b, inlined_node.op_c, inlined_node.intent_hash};
                cursor++;
            }
        }
    }
};

} // namespace dgtl::opt
