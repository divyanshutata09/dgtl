#pragma once

#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

namespace dgtl::pass {

class DGTLTemporalVectorFusion final : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.size() < 2) {
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
        for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(stream.size()); ++i) {
            const auto widen = stream.get_node_readonly(i);
            const auto fma = stream.get_node_readonly(i + 1);
            if (widen.opcode != dgtl::ir::OpCode::FCVTL_F16_TO_F32) {
                continue;
            }
            if (fma.opcode != dgtl::ir::OpCode::FMA_F32) {
                continue;
            }
            if (fma.op_a != widen.result_id) {
                continue;
            }

            auto& widen_atom = stream.get_shadow_atom(i);
            auto& widen_core = stream.get_shadow_core(i);
            widen_atom.opcode = dgtl::ir::OpCode::NOP;
            widen_atom.flags = dgtl::ir::Flags::IS_DEAD;
            widen_core.op_a = 0;
            widen_core.op_b = 0;
            widen_core.op_c = dgtl::ir::INVALID_SLOT;
            widen_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                widen_atom.opcode, widen_core.result_id, widen_core.op_a, widen_core.op_b, widen_core.op_c);

            auto& fma_atom = stream.get_shadow_atom(i + 1);
            auto& fma_core = stream.get_shadow_core(i + 1);
            fma_atom.opcode = dgtl::ir::OpCode::FMA_WIDEN_F16_F32;
            fma_atom.flags |= (dgtl::ir::Flags::IS_MACRO_FUSED |
                               dgtl::ir::Flags::IS_OPTIMIZED |
                               dgtl::ir::Flags::IS_SIMD_BURST);
            fma_core.op_a = widen.op_a;
            fma_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                fma_atom.opcode, fma_core.result_id, fma_core.op_a, fma_core.op_b, fma_core.op_c);
            changed = true;
        }

        if (changed && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLTemporalVectorFusion";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }
};

} // namespace dgtl::pass
