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

#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>
#include <memory>

// Core DGTL Headers
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pass::transform {

/**
 * @struct IR_Op
 * @brief Zero-latency adapter for BIR 2.1 packed atoms.
 * Optimized for cache-line alignment and fast dependency checking.
 */
struct IR_Op {
    uint32_t original_stream_idx; 
    ir::OpCode opcode;
    std::vector<uint32_t> reads;
    std::vector<uint32_t> writes;
    bool has_side_effects;

    IR_Op(uint32_t idx, const ir::IRNode& node)
        : original_stream_idx(idx), opcode(node.opcode) {
        
        // Pillar 3: Respect side-effect barriers for volatile/atomic ops
        has_side_effects = (node.flags & ir::Flags::HAS_SIDE_EFFECTS);

        // Extract Operand dependencies for RAW/WAR/WAW analysis
        if (node.op_a != ir::INVALID_SLOT) reads.push_back(node.op_a);
        if (node.op_b != ir::INVALID_SLOT) reads.push_back(node.op_b);
        if (node.op_c != ir::INVALID_SLOT) reads.push_back(node.op_c);
        
        if (node.result_id != ir::INVALID_SLOT) {
            writes.push_back(node.result_id);
        }
    }
};

/**
 * @class Instruction_Reordering_Pass
 * @brief Transactional reordering pass synchronized with Step 29 (Smasher) and Step 2 (AI Cost).
 */
class Instruction_Reordering_Pass : public pipeline::DGTLOptimizationPass {
protected:
    analysis::DGTLMemorySmasher& m_smasher;
    analysis::DGTLAICostModel* m_ai_model;

public:
    explicit Instruction_Reordering_Pass(
        analysis::DGTLMemorySmasher& smasher,
        analysis::DGTLAICostModel* ai = nullptr)
        noexcept
        : m_smasher(smasher), m_ai_model(ai) {}

    /**
     * @brief PILLAR 2: Transactional Execution
     * Stages reordering in the Shadow Tier before Sentinel validation.
     */
    void execute(ir::IRStream& stream) override {
        const size_t stream_size = stream.size();
        if (stream_size < 2) return;

        // 1. Prepare Adapter Block for optimization analysis
        std::vector<IR_Op> block;
        block.reserve(stream_size);
        for (uint32_t i = 0; i < (uint32_t)stream_size; ++i) {
            block.emplace_back(i, stream.get_node_readonly(i));
        }

        // 2. Perform Heuristic Reordering
        run_optimization(block, stream);

        // 3. PILLAR 2: Shadow-Tier Writeback (Zero-Corruption Staging)
        stream.begin_transaction();
        for (uint32_t i = 0; i < (uint32_t)block.size(); ++i) {
            const ir::IRNode& source_node = stream.get_node_readonly(block[i].original_stream_idx);
            apply_to_shadow(stream, i, source_node);
        }

        // 4. PILLAR 3: Sentinel Integrity Guard (CRC-32/Formal Validation)
        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "InstructionReordering";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

protected:
    void run_optimization(std::vector<IR_Op>& block, const ir::IRStream& stream) {
        bool changed = true;
        size_t limit = block.size() * 2; 
        
        while (changed && limit--) {
            changed = false;
            for (size_t i = 0; i + 1 < block.size(); ++i) {
                if (should_swap(block[i], block[i+1], stream)) {
                    std::swap(block[i], block[i+1]);
                    changed = true;
                }
            }
        }
    }

    bool should_swap(const IR_Op& A, const IR_Op& B, const ir::IRStream& stream) {
        // --- HARD CONSTRAINTS (Safety) ---
        
        // 1. Register Data Dependency (RAW, WAR, WAW)
        for (auto r : B.reads) for (auto w : A.writes) if (r == w) return false;
        for (auto w : B.writes) for (auto r : A.reads) if (w == r) return false;
        for (auto w1 : B.writes) for (auto w2 : A.writes) if (w1 == w2) return false;

        // 2. Side Effect Barrier (Volatiles/Atomics)
        if (A.has_side_effects || B.has_side_effects) return false;

        // 3. PILLAR 1: Memory Smasher Verification
        // Uses global indices for deep disambiguation
        if (!m_smasher.can_reorder(stream, A.original_stream_idx, B.original_stream_idx)) {
            return false;
        }

        // --- HEURISTICS (Profitability) ---
        float cost_a = get_instruction_cost(A, stream);
        float cost_b = get_instruction_cost(B, stream);

        // Bubble-up high-latency instructions (like LOAD) to hide cycles
        return (cost_b > cost_a); 
    }

    float get_instruction_cost(const IR_Op& op, const ir::IRStream& stream) {
        if (m_ai_model) {
            // STABILITY FIX: Capture node in local const to safely bind to AI Model reference
            const ir::IRNode node = stream.get_node_readonly(op.original_stream_idx);
            return m_ai_model->predict_node_cost(node);
        }
        // Fallback: Scalar heuristics
        return (op.opcode == ir::OpCode::LOAD) ? 10.0f : 1.0f;
    }

    void apply_to_shadow(ir::IRStream& s, uint32_t shadow_idx, const ir::IRNode& node) {
        ir::IRAtom& s_atom = s.get_shadow_atom(shadow_idx);
        ir::IRCore& s_core = s.get_shadow_core(shadow_idx);

        // Sync Packed Atom
        s_atom.opcode = node.opcode;
        s_atom.flags = node.flags;

        // Sync Core Data
        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;
        s_core.intent_hash = node.intent_hash;
    }
};

} // namespace dgtl::pass::transform
