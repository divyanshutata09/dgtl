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
 * STEP 36: DYNAMIC VALUE SPECIALIZATION (TRANSACTIONAL)
 * PILLAR 2: TRANSACTIONAL IR (SHADOW-BUFFERED SPECIALIZATION)
 * PILLAR 3: THE SENTINEL (CRC-32 INTENT VALIDATION)
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once
#include <atomic>
#include <stdexcept>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/jit/runtime/JIT_Executor.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::analysis {

/**
 * @class DGTLValueProfiler
 * @brief Performs non-destructive IR surgery on the Shadow Tier for JIT specialization.
 */
class DGTLValueProfiler {
    dgtl::isa::DGTLJitExecutor& jit;
    DGTLAICostModel& ai_model;
    core::DGTL_Arena* global_arena = nullptr;

public:
    DGTLValueProfiler(dgtl::isa::DGTLJitExecutor& j, DGTLAICostModel& ai, core::DGTL_Arena* arena = nullptr)
        : jit(j), ai_model(ai), global_arena(arena) {}

    /**
     * @brief Main specialization pass.
     * Processes all available guards in the shadow tier.
     */
    void execute(ir::IRStream& stream) {
        if (!global_arena || stream.size() == 0) [[unlikely]] return; 

        const uintptr_t stream_id = static_cast<uintptr_t>(stream.get_version());
        const dgtl::analysis::RuntimeFeedback runtime_feedback = jit.collect_feedback(stream_id);
        const float estimated_cost = ai_model.predict_cost(stream);
        if (!should_attempt_specialization(estimated_cost, runtime_feedback)) {
            return;
        }

        // PILLAR 2: Start transaction.
        stream.begin_transaction();

        bool total_specialized = false;
        const size_t count = stream.size();
        
        for (uint32_t i = 0; i < (uint32_t)count; ++i) {
            // Get shadow atom by value for check
            ir::IRAtom atom_raw = stream.get_shadow_atom(i);
            if (ir::AtomSplitter::get_opcode(atom_raw) == ir::OpCode::GUARD_VALUE) {
                if (specialize_on_value(stream, i)) {
                    total_specialized = true;
                    if (runtime_feedback.execution_count > 0) {
                        ai_model.update_with_feedback(stream.get_node_readonly(i), runtime_feedback);
                    }
                }
            }
        }

        // PILLAR 3: THE SENTINEL
        // Validate integrity before committing shadow tier to primary
        if (total_specialized && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:
    [[nodiscard]] static bool should_attempt_specialization(
        float estimated_cost,
        const dgtl::analysis::RuntimeFeedback& feedback) noexcept {
        constexpr float kMinPredictedCost = 4.0f;
        constexpr double kMaxBranchMissForSpec = 0.20;

        if (estimated_cost < kMinPredictedCost) {
            return false;
        }
        if (feedback.execution_count > 0 && feedback.branch_mispredict_rate > kMaxBranchMissForSpec) {
            return false;
        }
        return true;
    }

    /**
     * @brief Injects hot values and refreshes structural identity.
     */
    bool specialize_on_value(ir::IRStream& stream, uint32_t guard_idx) {
        // Access shadow core by reference for surgery
        ir::IRCore& guard_core = stream.get_shadow_core(guard_idx);
        const uint32_t reg_id = guard_core.op_a;
        const uint32_t expected_val = guard_core.op_b;

        if (reg_id == ir::INVALID_SLOT || expected_val == ir::INVALID_SLOT) {
            return false;
        }

        const size_t shadow_count = stream.size();
        bool logic_changed = false;

        for (uint32_t i = 0; i < (uint32_t)shadow_count; ++i) {
            if (i == guard_idx) {
                continue;
            }

            ir::IRCore& core = stream.get_shadow_core(i);
            
            bool modified = false;
            // Scan for occurrences of the guarded register
            if (core.op_a == reg_id) { core.op_a = expected_val; modified = true; }
            if (core.op_b == reg_id) { core.op_b = expected_val; modified = true; }
            if (core.op_c == reg_id) { core.op_c = expected_val; modified = true; }

            if (modified) {
                // Get reference to bit-packed atom to update flags
                ir::IRAtom& atom_ref = stream.get_shadow_atom(i);
                uint16_t flags = ir::AtomSplitter::get_flags(atom_ref);
                ir::OpCode op = ir::AtomSplitter::get_opcode(atom_ref);

                if (op == ir::OpCode::NOP) {
                    continue;
                }

                flags |= ir::Flags::IS_SPEC_CONST;
                atom_ref = ir::AtomSplitter::pack(op, flags);

                // Update 64-bit Intent Hash for GVN/Sentinel consistency
                core.intent_hash = ir::IRNode::calculate_hash(
                    op, core.result_id, core.op_a, core.op_b, core.op_c
                );
                logic_changed = true;
            }
        }
        
        // Finalize surgery: The guard itself is now redundant (convert to NOP)
        if (logic_changed) {
            ir::IRAtom& guard_atom = stream.get_shadow_atom(guard_idx);
            guard_atom = ir::AtomSplitter::pack(ir::OpCode::NOP, ir::Flags::NONE);
            guard_core.intent_hash = 0; 

            optimize_with_constant_context(stream);
        }

        return logic_changed;
    }

    /**
     * @brief Constant folding pass on the shadow buffer.
     */
    void optimize_with_constant_context(ir::IRStream& s) {
        const size_t count = s.size();
        for (uint32_t i = 0; i < (uint32_t)count; ++i) {
            ir::IRAtom& atom_ref = s.get_shadow_atom(i);
            const ir::OpCode op = ir::AtomSplitter::get_opcode(atom_ref);
            if (op == ir::OpCode::NOP) {
                continue;
            }

            uint16_t flags = ir::AtomSplitter::get_flags(atom_ref);
            
            if (flags & ir::Flags::IS_SPEC_CONST) {
                ir::IRCore& core = s.get_shadow_core(i);
                
                // Construct a 32-byte aligned temporary node for folding logic
                ir::IRNode bridge;
                bridge.opcode = op;
                bridge.flags = flags;
                bridge.result_id = core.result_id;
                bridge.op_a = core.op_a;
                bridge.op_b = core.op_b;
                bridge.op_c = core.op_c;
                bridge.intent_hash = core.intent_hash;

                // Perform surgery via the node's internal folding logic
                bridge.fold_if_possible();
                atom_ref = ir::AtomSplitter::pack(bridge.opcode, bridge.flags);
                core.op_a = bridge.op_a;
                core.op_b = bridge.op_b;
                core.op_c = bridge.op_c;
                core.intent_hash = bridge.intent_hash;
            }
        }
    }
};

} // namespace dgtl::analysis
