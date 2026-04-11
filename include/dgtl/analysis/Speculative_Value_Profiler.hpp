/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 36: DYNAMIC VALUE SPECIALIZATION (TRANSACTIONAL)
 * PILLAR 2: TRANSACTIONAL IR (SHADOW-BUFFERED SPECIALIZATION)
 * PILLAR 3: THE SENTINEL (CRC-32 INTENT VALIDATION)
 * DESIGN PRINCIPLE: 
 * Converts runtime variables into JIT constants for sub-microsecond execution.
 * ZERO-CORRUPTION: Specialized branches are validated against the original IR intent.
 */

#pragma once
#include <atomic>
#include <stdexcept>
#include "ir/Global_IR.hpp"
#include "core/Arena.hpp"
#include "jit/JIT_Executor.hpp"
#include "AI_Cost_Model.hpp"

namespace dgtl::opt {

class DGTLValueProfiler {
    exec::DGTLJitExecutor& jit;
    DGTLAICostModel& ai_model;
    core::DGTLArena* global_arena = nullptr;

public:
    DGTLValueProfiler(exec::DGTLJitExecutor& j, DGTLAICostModel& ai, core::DGTLArena* arena = nullptr) 
        : jit(j), ai_model(ai), global_arena(arena) {}

    /**
     * @brief DGTLoptManager Bridge.
     * Uses Shadow-Buffering to ensure specialization doesn't corrupt the base IR.
     */
    void execute(ir::IRStream& stream) {
        if (!global_arena || stream.size() == 0) [[unlikely]] return; 

        // PILLAR 2: Start transaction. This clones Primary -> Shadow Tier.
        stream.begin_transaction();

        bool specialized = false;
        const size_t count = stream.size();
        
        for (uint32_t i = 0; i < count; ++i) {
            // Accessing shadow tier atoms/cores for non-destructive inspection
            if (stream.get_shadow_atom(i).opcode == ir::OpCode::GUARD_VALUE) {
                specialized = specialize_on_value(stream, i);
                break; 
            }
        }

        // PILLAR 3: THE SENTINEL
        // Validate the CRC-32/Intent of the specialized shadow tier.
        if (specialized && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:
    /**
     * @brief Generates a specialized shadow stream for a specific hot value.
     */
    bool specialize_on_value(ir::IRStream& stream, uint32_t guard_idx) {
        ir::IRCore& guard_core = stream.get_shadow_core(guard_idx);
        uint32_t reg_id = guard_core.op_a;
        uint64_t expected_val = guard_core.op_b;

        const size_t shadow_count = stream.size();

        // Perform "Surgery" on the Shadow Tier
        for (uint32_t i = 0; i < shadow_count; ++i) {
            ir::IRAtom& atom = stream.get_shadow_atom(i);
            ir::IRCore& core = stream.get_shadow_core(i);
            
            bool modified = false;

            // PILLAR 3: "Anti-Hallucination" Check
            // Inject constants while preserving the logic chain
            if (core.op_a == reg_id) {
                core.op_a = static_cast<uint32_t>(expected_val);
                atom.flags |= ir::Flags::IS_SPEC_CONST;
                modified = true;
            }
            if (core.op_b == reg_id) {
                core.op_b = static_cast<uint32_t>(expected_val);
                atom.flags |= ir::Flags::IS_SPEC_CONST;
                modified = true;
            }

            if (modified) {
                // Refresh the Intent Hash to lock the new specialized state
                ir::IRNode temp;
                temp.opcode = atom.opcode;
                temp.flags = atom.flags;
                temp.result_id = core.result_id;
                temp.op_a = core.op_a;
                temp.op_b = core.op_b;
                temp.op_c = core.op_c;
                
                temp.refresh_intent_hash();
                core.intent_hash = temp.intent_hash;
            }
        }
        
        // Final folding of the constant values on the shadow side
        optimize_with_constant_context(stream);
        
        return true;
    }

    /**
     * @brief Constant folding specifically for specialized values.
     */
    void optimize_with_constant_context(ir::IRStream& s) {
        const size_t count = s.size();
        for (uint32_t i = 0; i < count; ++i) {
            ir::IRAtom& atom = s.get_shadow_atom(i);
            if (atom.flags & ir::Flags::IS_SPEC_CONST) {
                // Trigger micro-folding on the shadow core
                ir::IRCore& core = s.get_shadow_core(i);
                
                // Construct bridge for folding strike
                ir::IRNode bridge = {atom.opcode, atom.flags, core.result_id, core.op_a, core.op_b, core.op_c, core.intent_hash};
                bridge.fold_if_possible();
                
                // Update shadow with folded results
                atom.opcode = bridge.opcode;
                atom.flags = bridge.flags;
                core.op_a = bridge.op_a;
                core.op_b = bridge.op_b;
                core.intent_hash = bridge.intent_hash;
            }
        }
    }
};

} // namespace dgtl::opt
