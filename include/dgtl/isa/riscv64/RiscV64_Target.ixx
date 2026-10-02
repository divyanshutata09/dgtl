/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   RISC-V 64 Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.riscv64.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.riscv64.riscv64_instruction_info;
export import dgtl.isa.riscv64.riscv64_reg_map;
export import dgtl.isa.riscv64.riscv64_abi;
export import dgtl.isa.riscv64.frame_lowering;
export import dgtl.isa.riscv64.riscv64_machine_form_lowering;
export import dgtl.isa.riscv64.riscv64_emitter;

export namespace dgtl::isa::riscv64 {

inline constexpr std::string_view kTargetName = "riscv64";
inline constexpr std::string_view kTargetTripleDefault = "riscv64-unknown-linux-gnu";

struct RiscV64TargetSubtarget {
    RiscV64TargetRegisterInfo reg_info{};
    RiscV64TargetInstructionInfo instr_info{};
    RiscV64FrameInfo frame_info{};
    RiscV64LP64DABI lp64d_abi{};

    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept {
        return lp64d_abi;
    }

    [[nodiscard]] dgtl::machine::TargetSubtarget as_subtarget() const noexcept {
        return dgtl::machine::TargetSubtarget{
            reg_info, instr_info, frame_info, lp64d_abi, nullptr
        };
    }
};

} // namespace dgtl::isa::riscv64
