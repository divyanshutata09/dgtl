#pragma once

#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pass {

class DGTLMaskedPredicationLowering final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.size() < 3) {
            return;
        }

        stream.begin_transaction();

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

        bool changed = false;
        for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(stream.size()); ++i) {
            const auto br = stream.get_node_readonly(i);
            const auto then_node = stream.get_node_readonly(i + 1);
            const auto else_node = stream.get_node_readonly(i + 2);

            if (br.opcode != dgtl::ir::OpCode::BRANCH_COND) {
                continue;
            }

            const bool then_ok = then_node.opcode == dgtl::ir::OpCode::ADD_I32 ||
                                 then_node.opcode == dgtl::ir::OpCode::SUB_I32 ||
                                 then_node.opcode == dgtl::ir::OpCode::MUL_I32;
            const bool else_ok = else_node.opcode == dgtl::ir::OpCode::ADD_I32 ||
                                 else_node.opcode == dgtl::ir::OpCode::SUB_I32 ||
                                 else_node.opcode == dgtl::ir::OpCode::MUL_I32;
            if (!then_ok || !else_ok) {
                continue;
            }

            auto& pred_atom = stream.get_shadow_atom(i);
            auto& pred_core = stream.get_shadow_core(i);
            pred_atom.opcode = dgtl::ir::OpCode::SIMD_BSL_PREDICATE;
            pred_atom.flags |= (dgtl::ir::Flags::IS_SIMD_BURST |
                                dgtl::ir::Flags::REQUIRES_VEC_PORT |
                                dgtl::ir::Flags::IS_OPTIMIZED);
            pred_core.result_id = then_node.result_id;
            pred_core.op_a = br.op_a;
            pred_core.op_b = then_node.op_a;
            pred_core.op_c = else_node.op_a;
            pred_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                pred_atom.opcode, pred_core.result_id, pred_core.op_a, pred_core.op_b, pred_core.op_c);

            for (uint32_t j = 1; j <= 2; ++j) {
                auto& dead_atom = stream.get_shadow_atom(i + j);
                auto& dead_core = stream.get_shadow_core(i + j);
                dead_atom.opcode = dgtl::ir::OpCode::NOP;
                dead_atom.flags = dgtl::ir::Flags::IS_DEAD;
                dead_core.op_a = 0;
                dead_core.op_b = 0;
                dead_core.op_c = dgtl::ir::INVALID_SLOT;
                dead_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                    dead_atom.opcode, dead_core.result_id, dead_core.op_a, dead_core.op_b, dead_core.op_c);
            }

            changed = true;
            i += 2;
        }

        if (changed && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLMaskedPredicationLowering";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }
};

} // namespace dgtl::pass
