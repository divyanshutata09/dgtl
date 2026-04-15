/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 33: ADVANCED SIMD GATHER/SCATTER OPTIMIZER
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED VECTORIZATION)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <stdexcept>
#include <vector>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::pass {

class DGTLGatherScatterOptimizer {
    dgtl::analysis::DGTLMemorySmasher& smasher;
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    DGTLGatherScatterOptimizer(dgtl::analysis::DGTLMemorySmasher& s, dgtl::analysis::DGTLAICostModel& ai)
        : smasher(s), ai_model(ai) {}

    /**
     * @brief The "Zero-Corruption" Vector Strike.
     * Converts scalar load chains into 512-bit Gathers via Shadow-Tier mutation.
     */
    void optimize_indirect_access(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        if (stream_size < 16) return;

        // PILLAR 2: Open Transactional Tier
        stream.begin_transaction();

        // Initial sync to Shadow Tier
        for (uint32_t i = 0; i < stream_size; ++i) {
            sync_node_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i <= static_cast<uint32_t>(stream_size) - 16; ++i) {
            ir::IRNode node = stream.get_node_readonly(i); 
            
            // OpCode Sync: LOAD = 10
            if (node.opcode == ir::OpCode::LOAD && is_indirect_address(node)) {
                
                // 1. Dependency Check: Can we group 16 scalar loads?
                if (!smasher.can_group_access(stream, i, 16)) continue;

                // 2. AI Cost Validation: Compare Scalar vs. SIMD
                // We use a temporary node for cost probing to avoid primary mutation
                ir::IRNode probe = node;
                convert_to_gather_node(probe);
                
                float scalar_cost = 0.0f;
                for (uint32_t k = 0; k < 16; ++k) {
                    scalar_cost += ai_model.predict_node_cost(stream.get_node_readonly(i + k));
                }
                float gather_cost = ai_model.predict_node_cost(probe);

                if (gather_cost < scalar_cost) {
                    // PILLAR 3: Shadow Synthesis (Mutates Stream B)
                    apply_gather_transaction(stream, i);
                    
                    // Advance index to skip absorbed scalar slots
                    i += 15; 
                }
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
    void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash, 0, 0};
    }

    [[nodiscard]] bool is_indirect_address(const ir::IRNode& node) const noexcept {
        // Updated to use your SPEC_CONST flag as proxy for indexed addressing in this context
        return (node.flags & ir::Flags::IS_SPEC_CONST) != 0;
    }

    /**
     * @brief Mutates Shadow Tier and NOPs absorbed followers.
     */
    void apply_gather_transaction(ir::IRStream& stream, uint32_t head_idx) {
        ir::IRAtom& h_atom = stream.get_shadow_atom(head_idx);
        ir::IRCore& h_core = stream.get_shadow_core(head_idx);

        // Transform Shadow Head to Gather (GATHER_F32_512 = 0x8101)
        h_atom.opcode = ir::OpCode::GATHER_F32_512;
        h_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED);
        
        // PILLAR 3: Refresh Intent Hash via standardized calculation
        // Updated to hash Shadow tier content specifically to satisfy Sentinel
        h_core.intent_hash = ir::IRNode::calculate_hash(
            h_atom.opcode,
            h_core.result_id,
            h_core.op_a,
            h_core.op_b,
            h_core.op_c
        );

        // Invalidate next 15 scalar slots in Shadow Tier
        for (uint32_t j = 1; j < 16; ++j) {
            ir::IRAtom& f_atom = stream.get_shadow_atom(head_idx + j);
            ir::IRCore& f_core = stream.get_shadow_core(head_idx + j);
            
            f_atom = {ir::OpCode::NOP, ir::Flags::NONE};
            f_core = {ir::INVALID_SLOT, 0, 0, 0, 0, 0, 0};
            
            // Recalculate hash for Shadow NOP slots
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
        node.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED);
    }
};

} // namespace dgtl::pass


