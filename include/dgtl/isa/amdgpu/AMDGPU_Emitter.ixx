/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm / AMDGPU High-Performance Code Emitter
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>

export module dgtl.isa.amdgpu.emitter;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::amdgpu {

class ROCmEmitter final : public dgtl::machine::MachineEmitter {
public:
    [[nodiscard]] dgtl::machine::EmissionKind kind() const noexcept override {
        return dgtl::machine::EmissionKind::Assembly;
    }

    [[nodiscard]] dgtl::machine::EmissionResult
    emit(const dgtl::machine::MachineFunction& function) override {
        dgtl::machine::EmissionResult result{};
        std::ostringstream asm_out;

        asm_out << "; DGTL Generated AMDGPU GCN/CDNA Assembly\n";
        asm_out << ".amdgcn_target \"amdgcn-amd-amdhsa--gfx90a\"\n";
        asm_out << ".text\n";
        asm_out << ".globl " << function.name() << "\n";
        asm_out << ".p2align 8\n";
        asm_out << ".type " << function.name() << ",@function\n";
        asm_out << function.name() << ":\n";

        for (const auto& block : function.blocks()) {
            asm_out << ".L__bb_" << block.id << ":\n";
            for (auto instr_id : block.instructions) {
                const auto* inst = function.instruction(instr_id);
                if (!inst) continue;
                asm_out << "    ; form: " << dgtl::machine::machine_form_name(inst->form) << "\n";
            }
        }

        asm_out << "    s_endpgm\n";
        asm_out << ".Lfunc_end0:\n";
        asm_out << "    .size " << function.name() << ", .Lfunc_end0-" << function.name() << "\n";

        std::string str = asm_out.str();
        result.bytes.assign(str.begin(), str.end());
        result.success = true;
        return result;
    }
};

} // namespace dgtl::isa::amdgpu