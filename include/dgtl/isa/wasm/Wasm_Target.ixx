/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : UNIVERSAL TARGET EXPANSION
 * STEP 17.5: CROSS-PLATFORM MACH-O & WASM STUBS (V5)
 * PILLAR 3 INTEGRATION: EMITTER-LEVEL SENTINEL
 * DESIGN: Validated IR-to-Binary mapping with Bit-Level Invariant Checks.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
export module dgtl.isa.wasm.target;
import dgtl.mir.machine_ir;
import dgtl.mir.abi_lowering;
import dgtl.mir.runtime_contract;
export import dgtl.isa.wasm.wasm_instruction_info;
export import dgtl.isa.wasm.reg_map;
export import dgtl.isa.wasm.wasm_abi;
export import dgtl.isa.wasm.frame_lowering;
export import dgtl.isa.wasm.wasm_machine_form_lowering;
export import dgtl.isa.wasm.encoder;

export namespace dgtl::isa::wasm {



/**
 * @struct WasmHeader
 * @brief Canonical WebAssembly binary preamble.
 */
struct WasmHeader {
    uint32_t magic = 0x6d736100; // "\0asm" in little-endian
    uint32_t version = 0x1;
};



inline constexpr std::string_view kTargetName = "wasm";
inline constexpr std::string_view kTargetTripleDefault = "wasm32-unknown-wasi";

struct WasmTargetSubtarget {
    WasmTargetRegisterInfo reg_info{};
    WasmTargetInstructionInfo instr_info{};
    WasmFrameInfo frame_info{};
    WasmABI wasm_abi{};

    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept {
        return wasm_abi;
    }

    [[nodiscard]] dgtl::machine::TargetSubtarget as_subtarget() const noexcept {
        return dgtl::machine::TargetSubtarget{
            reg_info, instr_info, frame_info, wasm_abi, nullptr
        };
    }
};

[[nodiscard]] inline const dgtl::machine::TargetSubtarget& get_wasm_target_subtarget() noexcept {
    static const WasmTargetRegisterInfo reg_info{};
    static const WasmTargetInstructionInfo instr_info{};
    static const WasmFrameInfo frame_info{};
    static const WasmABI wasm_abi{};
    static const dgtl::machine::TargetSubtarget subtarget{
        reg_info, instr_info, frame_info, wasm_abi, nullptr
    };
    return subtarget;
}

} // namespace dgtl::isa::wasm
