/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Two-Address Instruction Normalization Pass
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 *DO not use any ir opcodes instead mir opcodes valid 
 */
module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <vector>

export module dgtl.mir.two_address;

import dgtl.mir.machine_ir;
import dgtl.target;

export namespace dgtl::mir::lowering {

using dgtl::machine::BlockId;
using dgtl::machine::InstrId;
using dgtl::machine::MachineFunction;
using dgtl::machine::MachineInstrFlag;
using dgtl::machine::MachineInstruction;
using dgtl::machine::MachineOperand;
using dgtl::machine::MachineOperandKind;
using dgtl::machine::MachineSemanticKind;
using dgtl::machine::MachineForm;
using dgtl::machine::GM_COPY;

class TwoAddressInstructionPass {
public:
    /**
     * @brief Normalizes 3-address instructions into 2-address form for destructive targets (e.g. x86-64).
     * Eliminates redundant copies via commutation where possible, and inserts tied-operand moves when necessary.
     */
    static bool run(MachineFunction& function,
                    const core::TargetMachine& target,
                    bool* success = nullptr) {
        if (success != nullptr) *success = true;
        const auto arch = target.get_arch();

        // 3-address architectures (AArch64, ARM32, RISC-V, CUDA, ROCm, FPGA, WASM)
        // do not require destructive 2-address tied-operand normalization.
        if (arch == core::ArchType::AARCH64_SVE ||
            arch == core::ArchType::ARM32_NEON ||
            arch == core::ArchType::RISCV64_RVV ||
            arch == core::ArchType::CUDA_PTX ||
            arch == core::ArchType::ROCM_AMDGCN ||
            arch == core::ArchType::FPGA_VERILOG ||
            arch == core::ArchType::WASM_SIMD) {
            return false;
        }

        bool modified = false;

        for (const auto& block : function.blocks()) {
            const BlockId block_id = block.id;

            // Snapshot instruction list because we may insert move instructions
            const std::vector<InstrId> inst_ids = block.instructions;

            for (const InstrId inst_id : inst_ids) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;

                // Only normalize binary instructions with at least [Def, Use1, Use2]
                if (!is_two_address_candidate(*inst)) {
                    continue;
                }

                if (inst->operands.size() < 3) continue;

                auto& def_op = inst->operands[0];
                auto& use1_op = inst->operands[1];
                auto& use2_op = inst->operands[2];

                if (def_op.kind != MachineOperandKind::Register ||
                    use1_op.kind != MachineOperandKind::Register) {
                    continue;
                }

                // If already 2-address (dest == use1), no transformation is needed
                if (def_op.reg == use1_op.reg) {
                    continue;
                }

                const bool commutable = is_commutable(*inst);

                // Commutation optimization 1: If dest == use2 and operation is commutable, swap operands!
                if (commutable && use2_op.kind == MachineOperandKind::Register &&
                    def_op.reg == use2_op.reg) {
                    std::swap(inst->operands[1], inst->operands[2]);
                    modified = true;
                    continue;
                }

                // Commutation optimization 2: If use1 is immediate/non-reg but use2 is register,
                // swap so destination can be tied to the register operand.
                if (commutable && use1_op.kind != MachineOperandKind::Register &&
                    use2_op.kind == MachineOperandKind::Register) {
                    std::swap(inst->operands[1], inst->operands[2]);
                    modified = true;
                    if (def_op.reg == inst->operands[1].reg) {
                        continue;
                    }
                }

                // Insert a copy: dest = Move use1 before the instruction
                const auto destination = def_op.reg;
                const auto source = use1_op.reg;
                MachineInstruction& copy_inst = function.create_instruction();
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.opcode = GM_COPY;
                copy_inst.add_operand(MachineOperand::def(destination));
                copy_inst.add_operand(MachineOperand::use(source));

                if (!function.insert_instruction_before(block_id, inst_id, copy_inst.id)) {
                    if (success != nullptr) *success = false;
                    return modified;
                }

                // Tie use1 to dest register
                inst = function.instruction(inst_id);
                if (inst == nullptr || inst->operands.size() < 2u ||
                    inst->operands[0].kind != MachineOperandKind::Register) {
                    if (success != nullptr) *success = false;
                    return modified;
                }
                inst->operands[1] = MachineOperand::use(destination);
                modified = true;
            }
        }

        return modified;
    }

private:
    [[nodiscard]] static bool is_two_address_candidate(const MachineInstruction& inst) noexcept {
        if (inst.semantic_kind == MachineSemanticKind::IntegerDiv ||
            inst.semantic_kind == MachineSemanticKind::IntegerMod) {
            return false;
        }
        switch (inst.form) {
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64:
            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static bool is_commutable(const MachineInstruction& inst) noexcept {
        if (inst.has_flag(MachineInstrFlag::Commutable)) {
            return true;
        }

        switch (inst.semantic_kind) {
            case MachineSemanticKind::IntegerAdd:
            case MachineSemanticKind::IntegerMul:
            case MachineSemanticKind::IntegerAnd:
            case MachineSemanticKind::IntegerOr:
            case MachineSemanticKind::IntegerXor:
            case MachineSemanticKind::FloatAdd:
            case MachineSemanticKind::FloatMul:
                return true;
            default:
                return false;
        }
    }
};

} // namespace dgtl::mir::lowering