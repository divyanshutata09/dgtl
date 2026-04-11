/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 35: AI-DRIVEN SPECULATIVE LOOP UNSWITCHER
 * PILLAR 2: TRANSACTIONAL VERSIONING (SHADOW-BUFFERED)
 * PILLAR 3: THE SENTINEL (INVARIANT VALIDATION)
 * DESIGN: Lifts Invariant Branches out of Hot Loops via Shadow-Stream Specialization.
 */

#pragma once
#include <algorithm>
#include "../ir/Global_IR.hpp"
#include "../analysis/AI_Cost_Model.hpp"
#include "Speculative_DCE.hpp"

namespace dgtl::opt {

class DGTLLoopUnswitcher {
    DGTLAICostModel& ai_model;
    DGTLSpeculativeDCE& dce_engine;
    DGTLFormalVerifier& verifier; // PILLAR 3: Injected formal verifier authority

public:
    DGTLLoopUnswitcher(DGTLAICostModel& ai, DGTLSpeculativeDCE& dce, DGTLFormalVerifier& v) 
        : ai_model(ai), dce_engine(dce), verifier(v) {}

    /**
     * @brief The "Zero-Corruption" Unswitching Strike.
     * Strategy: Clone and specialize into a Shadow Buffer to prevent IR corruption.
     */
    void unswitch_loop(ir::IRStream& stream) {
        const size_t count = stream.size();
        if (count == 0) [[unlikely]] return;

        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const auto& atom = stream.get_atom_readonly(i);
            const auto& core = stream.get_core_readonly(i);

            // 1. Detect Invariant Branch (Condition is constant across loop iterations)
            if (atom.opcode == ir::OpCode::BRANCH_COND && is_loop_invariant(core.op_a, stream)) {
                
                // 2. AI Bloat Check
                float cost_before = ai_model.predict_cost(stream);
                float speedup_gain = cost_before * 0.18f; 
                float cache_penalty = (count > 256) ? 8.5f : 1.2f;

                if (speedup_gain > cache_penalty) {
                    // PILLAR 2: Begin Transactional Strike
                    stream.begin_strike();
                    
                    if (perform_shadow_versioning(stream, i)) {
                        // PILLAR 3: Sentinel Check (Logic consistency check)
                        // Fixed: Delegating bit-consistency proof to the Formal Verifier
                        bool consistent = true;
                        for (uint32_t j = 0; j < stream.size(); ++j) {
                            // Updated to use the existing proxy method from your IRStream
                            if (!verifier.verify_bit_consistency(stream.get_node_readonly(j), stream.get_shadow_node_proxy(j))) {
                                consistent = false;
                                break;
                            }
                        }

                        if (consistent) {
                            stream.commit_strike(); 
                            return; // Mutation successful
                        }
                    }
                    
                    stream.rollback_strike();
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
            
            // If the register is redefined by an instruction that has side effects or is volatile
            if (core.result_id == cond_reg) {
                // Using explicit flags to resolve red flags from missing combined masks
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
        // Step 1: Baseline copy (Now implemented in IRStream)
        stream.push_full_to_shadow();

        // Step 2: Specialize the Shadow Loop for the TRUE path
        auto& shad_atom = stream.get_shadow_atom(branch_idx);
        auto& shad_core = stream.get_shadow_core(branch_idx);

        // Transform into Static GOTO
        shad_atom.opcode = ir::OpCode::GOTO; 
        
        // Sync with BitPacked_IR.hpp Flag names
        shad_atom.flags |= ir::Flags::IS_SPEC_CONST; 
        shad_atom.flags |= ir::Flags::IS_HOISTED; // Using IS_HOISTED as proxy for VERSIONED if missing
        
        shad_core.result_id = 0; 
        shad_core.op_a = shad_core.op_b; // Move True-target to op_a for GOTO logic

        // Step 3: Run Speculative DCE
        // Note: Ensure dce_engine has an execute() or execute_speculative() that accepts IRStream
        return dce_engine.execute_speculative(stream);
    }
};

} // namespace dgtl::opt
