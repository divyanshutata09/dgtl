/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Target Aggregation Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <memory>
#include <string_view>

export module dgtl.isa.spirv.target;

import dgtl.mir.machine_ir;
export import dgtl.isa.spirv.instruction_info;
export import dgtl.isa.spirv.reg_map;
export import dgtl.isa.spirv.abi;
export import dgtl.isa.spirv.frame_lowering;
export import dgtl.isa.spirv.machine_form_lowering;
export import dgtl.isa.spirv.encoder;

export namespace dgtl::isa::spirv {

inline constexpr std::string_view kTargetName = "spirv";
inline constexpr std::string_view kTargetTripleDefault = "spirv64-unknown-unknown";

[[nodiscard]] inline const dgtl::machine::TargetSubtarget& get_spirv_target_subtarget() noexcept {
    static const SPIRVTargetRegisterInfo reg_info{};
    static const SPIRVTargetInstructionInfo instr_info{};
    static const SPIRVFrameInfo frame_info{};
    static const SPIRVABI spirv_abi{};
    static const emitter::SPIRVCodeEmitter code_emitter{};
    static const dgtl::machine::TargetSubtarget subtarget{
        reg_info, instr_info, frame_info, spirv_abi, &code_emitter
    };
    return subtarget;
}

} // namespace dgtl::isa::spirv
