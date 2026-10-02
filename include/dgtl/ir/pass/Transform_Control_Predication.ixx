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

module;

#include <cstdlib>

#include <cstdint>
#include <cstddef>

export module dgtl.ir.pass.transform_control_predication;

import dgtl.ir;
import dgtl.ir.pipeline.pass_manager_config;

// import dgtl.ir.gen;

export namespace dgtl::ir::pass {

namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;

// ============================================================================
// MASKED PREDICATION LOWERING (SIMD BRANCH ELIMINATION)
// ============================================================================

/**
 * @class DGTLMaskedPredicationLowering
 * @brief Lowers conditional branches into masked SIMD select operations (BSL).
 */
class DGTLMaskedPredicationLowering final : public pipeline::DGTLOptimizationPass {
public:
    void execute(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 3) {
            return;
        }

        stream.begin_transaction();

        // Phase 1: Full sync to shadow tier
        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto node = stream.get_node_readonly(i);
            auto& atom = stream.get_shadow_atom(i);
            auto& core = stream.get_shadow_core(i);

            atom.opcode = node.opcode;
            atom.flags = node.flags;
            core.result_id = node.result_id;
            core.op_a = node.op_a;
            core.op_b = node.op_b;
            core.op_c = node.op_c;
            core.intent_hash = node.intent_hash;
        }

        // Phase 2: Pattern matching and transformation
        bool changed = false;
        for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(stream.size()); ++i) {
            const auto br = stream.get_node_readonly(i);
            const auto then_node = stream.get_node_readonly(i + 1);
            const auto else_node = stream.get_node_readonly(i + 2);

            // Step 1: Detect branch
            const bool is_branch = dgtl::ir::gen::op_has_trait(br.opcode, dgtl::ir::gen::GenTrait::IsTerminator) &&
                                   dgtl::ir::gen::op_implements_interface(br.opcode, dgtl::ir::gen::GenInterface::ControlFlow);
            if (!is_branch) {
                continue;
            }

            // Step 2: Verify payload operations (arithmetic only)
            const bool then_ok = is_predicable_op(then_node.opcode);
            const bool else_ok = is_predicable_op(else_node.opcode);

            if (!then_ok || !else_ok) {
                continue;
            }

            // Step 3: Transform branch to predicated select
            auto& pred_atom = stream.get_shadow_atom(i);
            auto& pred_core = stream.get_shadow_core(i);

            pred_atom.opcode = ir::OpCode::SIMD_BSL_PREDICATE;
            pred_atom.flags |= static_cast<decltype(pred_atom.flags)>(
                ir::Flags::IS_SIMD_BURST |
                ir::Flags::IS_OPTIMIZED
            );
            pred_core.logic_flags |= ir::HintFlags::RequiresVecPort;

            pred_core.result_id = then_node.result_id;
            pred_core.op_a = br.op_a;           // Condition
            pred_core.op_b = then_node.op_a;    // True-path operand
            pred_core.op_c = else_node.op_a;    // False-path operand

            pred_core.intent_hash = ir::IRNode::calculate_hash(
                pred_atom.opcode, pred_core.result_id, pred_core.op_a,
                pred_core.op_b, pred_core.op_c
            );

            // Step 4: Mark original branches as dead
            for (uint32_t j = 1; j <= 2; ++j) {
                auto& dead_atom = stream.get_shadow_atom(i + j);
                auto& dead_core = stream.get_shadow_core(i + j);

                dead_atom.opcode = ir::OpCode::NOP;
                dead_atom.flags = ir::Flags::IS_DEAD;
                dead_core.result_id = ir::INVALID_SLOT;
                dead_core.op_a = 0;
                dead_core.op_b = 0;
                dead_core.op_c = 0;

                dead_core.intent_hash = ir::IRNode::calculate_hash(
                    dead_atom.opcode, dead_core.result_id, dead_core.op_a,
                    dead_core.op_b, dead_core.op_c
                );
            }

            changed = true;
            i += 2;  // Skip ahead past the dead instructions
        }

        // Phase 3: PILLAR 3 - Sentinel validation and commit
        if (changed && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "MaskedPredicationLowering";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

private:
    static bool is_predicable_op(ir::OpCode op) noexcept {
        const bool arithmetic = dgtl::ir::gen::op_implements_interface(op, dgtl::ir::gen::GenInterface::Arithmetic);
        const bool bitwise = dgtl::ir::gen::op_implements_interface(op, dgtl::ir::gen::GenInterface::BitwiseOp);
        const bool terminator = dgtl::ir::gen::op_has_trait(op, dgtl::ir::gen::GenTrait::IsTerminator);
        return (arithmetic || bitwise) && !terminator;
    }
};

} // namespace dgtl::ir::pass

export namespace dgtl::ir::pass::transform {
    using namespace dgtl::ir::pass;
    using dgtl::ir::pass::DGTLMaskedPredicationLowering;
}

export namespace dgtl::pass {
    using namespace dgtl::ir::pass;
}

export namespace dgtl::pass::transform {
    using namespace dgtl::ir::pass;
}