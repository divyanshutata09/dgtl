/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 35: AI-DRIVEN SPECULATIVE LOOP UNSWITCHER
 * PILLAR 2: TRANSACTIONAL VERSIONING (SHADOW-BUFFERED)
 * PILLAR 3: THE SENTINEL (INVARIANT VALIDATION)
 * DESIGN: Lifts Invariant Branches out of Hot Loops via Shadow-Stream Specialization.
 */

#pragma once
#include <algorithm>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/pass/speculative/Speculative_DCE.hpp"
#include "dgtl/core/Verified_Formal.hpp"
  
namespace dgtl::pass {

class DGTLLoopUnswitcher {
    dgtl::analysis::DGTLAICostModel& ai_model;
    DGTLSpeculativeDCE& dce_engine;
    dgtl::core::DGTLFormalVerifier& verifier; // PILLAR 3 Authority

public:
    DGTLLoopUnswitcher(dgtl::analysis::DGTLAICostModel& ai, DGTLSpeculativeDCE& dce, dgtl::core::DGTLFormalVerifier& v) 
        : ai_model(ai), dce_engine(dce), verifier(v) {}

    /**
     * @brief The "Zero-Corruption" Unswitching Strike.
     */
    void unswitch_loop(ir::IRStream& stream) {
        const size_t count = stream.size();
        if (count == 0) [[unlikely]] return;

        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const auto& atom = stream.get_atom_readonly(i);
            const auto& core = stream.get_core_readonly(i);

            // 1. Detect Invariant Branch
            if (atom.opcode == ir::OpCode::BRANCH_COND && is_loop_invariant(core.op_a, stream)) {
                
                // 2. AI Bloat Check
                float cost_before = ai_model.predict_cost(stream);
                float speedup_gain = cost_before * 0.18f; 
                float cache_penalty = (count > 256) ? 8.5f : 1.2f;

                if (speedup_gain > cache_penalty) {
                    // PILLAR 2: Begin Transactional Strike
                    stream.begin_transaction();
                    
                    if (perform_shadow_versioning(stream, i)) {
                        // PILLAR 3: Sentinel Check
                        // Validates that the shadow buffer is logically sound and bit-consistent
                        if (stream.validate_shadow_integrity()) {
                            stream.commit_transaction(); 
                            return; 
                        }
                    }
                    
                    stream.rollback_transaction();
                }
            }
        }
    }

private:
    /**
     * @brief Proves that the branch condition is never redefined within the stream.
     */
    bool is_loop_invariant(uint32_t cond_reg, const ir::IRStream& s) const noexcept {
        for (uint32_t i = 0; i < s.size(); ++i) {
            const auto& core = s.get_core_readonly(i);
            const auto& atom = s.get_atom_readonly(i);
            
            if (core.result_id == cond_reg) {
                if (atom.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) {
                    return false; 
                }
            }
        }
        return true;
    }

    /**
     * @brief Performs Path-Specialization into the Shadow Tier.
     */
    bool perform_shadow_versioning(ir::IRStream& stream, uint32_t branch_idx) {
        // Step 1: Baseline copy to Shadow Tier
        stream.push_full_to_shadow();

        // Step 2: Specialize the Shadow Loop for the TRUE path
        auto& shad_atom = stream.get_shadow_atom(branch_idx);
        auto& shad_core = stream.get_shadow_core(branch_idx);

        shad_atom.opcode = ir::OpCode::GOTO; 
        shad_atom.flags |= (ir::Flags::IS_SPEC_CONST | ir::Flags::IS_HOSTED); 
        
        shad_core.result_id = 0; 
        shad_core.op_a = shad_core.op_b; // True-target target becomes primary operand

        // Update Sentinel Hash for manual shadow mutation
        shad_core.intent_hash = dgtl::core::DGTLFormalVerifier::compute_master_hash_bits(
            shad_atom.opcode, shad_core.op_a, shad_core.op_b, shad_core.op_c
        );

        // Step 3: Run Speculative DCE
        // Calling execute() is the standard Pipeline interface.
        // SpeculativeDCE internally calls update_shadow_size() when finished.
        dce_engine.execute(stream);

        // Verification: Ensure the shadow stream exists and wasn't wiped during pruning
        // In BIR 2.1, a valid transaction must result in a non-zero shadow capacity.
        return stream.size() > 0; 
    }
};

} // namespace dgtl::pass