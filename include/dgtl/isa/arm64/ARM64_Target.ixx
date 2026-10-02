/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ARM64 Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.arm64.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.arm64.arm64_instruction_info;
export import dgtl.isa.arm64.arm64_reg_map;
export import dgtl.isa.arm64.arm64_abi;
export import dgtl.isa.arm64.arm64_frame_lowering;
export import dgtl.isa.arm64.arm64_machine_form_lowering;
export import dgtl.isa.arm64.arm64_emitter;

export namespace dgtl::isa::arm64 {

inline constexpr std::string_view kTargetName = "aarch64";
inline constexpr std::string_view kTargetTripleDefault = "aarch64-unknown-linux-gnu";

struct ARM64TargetSubtarget {
    ARM64TargetRegisterInfo reg_info{};
    ARM64TargetInstructionInfo instr_info{};
    ARM64FrameInfo frame_info{};
    AAPCS64ABI aapcs_abi{};

    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept {
        return aapcs_abi;
    }

    [[nodiscard]] dgtl::machine::TargetSubtarget as_subtarget() const noexcept {
        return dgtl::machine::TargetSubtarget{
            reg_info, instr_info, frame_info, aapcs_abi, nullptr
        };
    }
};

} // namespace dgtl::isa::arm64
