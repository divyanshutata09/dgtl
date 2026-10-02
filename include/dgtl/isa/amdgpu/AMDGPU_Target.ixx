/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   AMDGPU / ROCm Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.amdgpu.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.amdgpu.rocm_instruction_info;
export import dgtl.isa.amdgpu.rocm_reg_map;
export import dgtl.isa.amdgpu.rocm_abi;
export import dgtl.isa.amdgpu.frame_lowering;
export import dgtl.isa.amdgpu.rocm_machine_form_lowering;
export import dgtl.isa.amdgpu.emitter;

export namespace dgtl::isa::amdgpu {

inline constexpr std::string_view kTargetName = "amdgcn";
inline constexpr std::string_view kTargetTripleDefault = "amdgcn-amd-amdhsa";

[[nodiscard]] inline const dgtl::machine::TargetSubtarget& get_amdgpu_target_subtarget() noexcept {
    static const ROCmTargetRegisterInfo reg_info{};
    static const ROCmTargetInstructionInfo instr_info{};
    static const ROCmFrameInfo frame_info{};
    static const ROCmABI rocm_abi{};
    static const dgtl::machine::TargetSubtarget subtarget{
        reg_info, instr_info, frame_info, rocm_abi, nullptr
    };
    return subtarget;
}

} // namespace dgtl::isa::amdgpu
