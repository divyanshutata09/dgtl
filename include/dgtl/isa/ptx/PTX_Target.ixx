/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   PTX / CUDA Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.ptx.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.ptx.cuda_instruction_info;
export import dgtl.isa.ptx.cuda_reg_map;
export import dgtl.isa.ptx.cuda_abi;
export import dgtl.isa.ptx.frame_lowering;
export import dgtl.isa.ptx.cuda_machine_form_lowering;
export import dgtl.isa.ptx.emitter;

export namespace dgtl::isa::ptx {

inline constexpr std::string_view kTargetName = "nvptx64";
inline constexpr std::string_view kTargetTripleDefault = "nvptx64-nvidia-cuda";

struct PTXTargetSubtarget {
    CUDATargetRegisterInfo reg_info{};
    CUDATargetInstructionInfo instr_info{};
    CUDAFrameInfo frame_info{};
    CUDAABI cuda_abi{};
    CUDAEmitter emitter{};

    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept {
        return cuda_abi;
    }

    [[nodiscard]] dgtl::machine::TargetSubtarget as_subtarget() const noexcept {
        return dgtl::machine::TargetSubtarget{
            reg_info, instr_info, frame_info, cuda_abi, &emitter
        };
    }
};

[[nodiscard]] inline const dgtl::machine::TargetSubtarget& get_ptx_target_subtarget() noexcept {
    static const PTXTargetSubtarget subtarget_holder{};
    static const dgtl::machine::TargetSubtarget subtarget = subtarget_holder.as_subtarget();
    return subtarget;
}

} // namespace dgtl::isa::ptx
