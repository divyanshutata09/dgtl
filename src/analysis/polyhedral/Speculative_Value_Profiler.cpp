/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

module;

#include <cstdlib>
#include <atomic>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdint>

module dgtl.ir_core.speculative_value_profiler;

import dgtl.arena;
import dgtl.ir_node;
import dgtl.ir_enums;

import dgtl.ir_core.stream;
import dgtl.ir_core.ai_cost_model;
import dgtl.ir_core.jit_executor;

namespace dgtl::analysis {

DGTLValueProfiler::DGTLValueProfiler(dgtl::runtime::DGTLExecutor& j,
                                     DGTLAICostModel& ai,
                                     core::DGTL_Arena* arena)
    : executor_(j), ai_model(ai), global_arena(arena) {}

DGTLValueProfiler::~DGTLValueProfiler() {}

void DGTLValueProfiler::execute(ir::IRStream& stream) {
    if (!global_arena || stream.size() == 0) [[unlikely]] return; 

    const uintptr_t stream_id = static_cast<uintptr_t>(stream.get_version());
    
    // 🚀 FIXED: Retained the seamless 'const auto' type deduction matching your executor contract
    const auto runtime_feedback = executor_.collect_feedback(stream_id);
    float estimated_cost = 0.0f;
    for (size_t i = 0; i < stream.size(); ++i) {
        estimated_cost += ai_model.predict_node_cost(stream.get_node_readonly(i));
    }
    if (!should_attempt_specialization(estimated_cost, runtime_feedback)) {
        return;
    }

    stream.begin_transaction();
    bool total_specialized = false;
    const size_t count = stream.size();
    
    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
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

    if (total_specialized && stream.validate_shadow_integrity()) {
        stream.commit_transaction();
    } else {
        stream.rollback_transaction();
    }
}

// 🚀 FIXED: Instantiated explicitly via template type matching to maintain perfect structural tracking
template <typename FeedbackT>
bool DGTLValueProfiler::should_attempt_specialization(
    float estimated_cost,
    const FeedbackT& feedback) noexcept {
    constexpr float kMinPredictedCost = 4.0f;
    constexpr double kMaxBranchMissForSpec = 0.20;

    if (estimated_cost < kMinPredictedCost) return false;
    if (feedback.execution_count > 0 && feedback.branch_mispredict_rate > kMaxBranchMissForSpec) {
        return false;
    }
    return true;
}

bool DGTLValueProfiler::specialize_on_value(ir::IRStream& stream, uint32_t guard_idx) {
    ir::IRCore& guard_core = stream.get_shadow_core(guard_idx);
    const uint32_t reg_id = guard_core.op_a;
    const uint32_t expected_val = guard_core.op_b;

    if (reg_id == ir::INVALID_SLOT || expected_val == ir::INVALID_SLOT) {
        return false;
    }

    const size_t shadow_count = stream.size();
    bool logic_changed = false;

    for (uint32_t i = 0; i < (uint32_t)shadow_count; ++i) {
        if (i == guard_idx) continue;

        ir::IRCore& core = stream.get_shadow_core(i);
        bool modified = false;
        
        if (core.op_a == reg_id) { core.op_a = expected_val; modified = true; }
        if (core.op_b == reg_id) { core.op_b = expected_val; modified = true; }
        if (core.op_c == reg_id) { core.op_c = expected_val; modified = true; }

        if (modified) {
            ir::IRAtom& atom_ref = stream.get_shadow_atom(i);
            uint16_t flags = ir::AtomSplitter::get_flags(atom_ref);
            ir::OpCode op = ir::AtomSplitter::get_opcode(atom_ref);

            if (op == ir::OpCode::NOP) continue;

            flags |= ir::Flags::IS_SPEC_CONST;
            atom_ref = ir::AtomSplitter::pack(op, flags);

            core.intent_hash = ir::IRNode::calculate_hash(
                op, core.result_id, core.op_a, core.op_b, core.op_c
            );
            logic_changed = true;
        }
    }
    
    if (logic_changed) {
        ir::IRAtom& guard_atom = stream.get_shadow_atom(guard_idx);
        guard_atom = ir::AtomSplitter::pack(ir::OpCode::NOP, ir::Flags::NONE);
        guard_core.intent_hash = 0; 

        optimize_with_constant_context(stream);
    }

    return logic_changed;
}

void DGTLValueProfiler::optimize_with_constant_context(ir::IRStream& s) {
    const size_t count = s.size();
    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        ir::IRAtom& atom_ref = s.get_shadow_atom(i);
        const ir::OpCode op = ir::AtomSplitter::get_opcode(atom_ref);
        if (op == ir::OpCode::NOP) continue;

        uint16_t flags = ir::AtomSplitter::get_flags(atom_ref);
        
        if (flags & ir::Flags::IS_SPEC_CONST) {
            ir::IRCore& core = s.get_shadow_core(i);
            
            ir::IRNode bridge;
            bridge.opcode = op;
            bridge.flags = flags;
            bridge.result_id = core.result_id;
            bridge.op_a = core.op_a;
            bridge.op_b = core.op_b;
            bridge.op_c = core.op_c;
            bridge.intent_hash = core.intent_hash;

            bridge.fold_if_possible();
            atom_ref = ir::AtomSplitter::pack(bridge.opcode, bridge.flags);
            core.op_a = bridge.op_a;
            core.op_b = bridge.op_b;
            core.op_c = bridge.op_c;
            core.intent_hash = bridge.intent_hash;
        }
    }
}

} // namespace dgtl::analysis