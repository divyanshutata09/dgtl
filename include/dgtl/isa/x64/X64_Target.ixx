/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   X64 Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.x64.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.x64.x64_instruction_info;
export import dgtl.isa.x64.x64_reg_map;
export import dgtl.isa.x64.x64_abi;
export import dgtl.isa.lowering.x64.frame_lowering;
export import dgtl.isa.x64.x64_machine_form_lowering;
export import dgtl.isa.x64.nasm_emitter;
export import dgtl.isa.x64.x64_graphcoloring_regalloc;

export namespace dgtl::isa::x64 {

inline constexpr std::string_view kTargetName = "x86_64";
inline constexpr std::string_view kTargetTripleDefault = "x86_64-pc-windows-msvc";

struct X64TargetSubtarget {
    emitter::X64TargetRegisterInfo reg_info{};
    X64TargetInstructionInfo instr_info{};
    X64FrameInfo frame_info{};
    Win64ABI win64_abi{};
    SystemVAMD64ABI sysv_abi{};

    [[nodiscard]] const dgtl::machine::TargetABI& abi(bool is_windows = true) const noexcept {
        return is_windows ? static_cast<const dgtl::machine::TargetABI&>(win64_abi)
                          : static_cast<const dgtl::machine::TargetABI&>(sysv_abi);
    }

    [[nodiscard]] dgtl::machine::TargetSubtarget as_subtarget(bool is_windows = true) const noexcept {
        return dgtl::machine::TargetSubtarget{
            reg_info,
            instr_info,
            frame_info,
            abi(is_windows),
            nullptr
        };
    }
};

} // namespace dgtl::isa::x64
