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

module;

#include <cstdlib>

export module dgtl.ir_core.runtime_policy;

import dgtl.ir_enums;
import dgtl.ir_core;


// Import native compiler base interfaces and target contract partitions
import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.isel_gen;


/**
 * @brief Runtime Legalization Policy
 *
 * Establishes the legalization policy for the backend. Automatically scans
 * the incoming IR stream during compilation and decides whether an opcode is directly
 * supportable by the target's physical instruction selector or needs to be legalized
 * (routed) into a runtime helper call. Prevents compiler crashes on unhandled/unsupported
 * operations by guaranteeing a safe and fallback-driven execution path.
 */
export namespace dgtl::isa::lowering::runtime_policy {

[[nodiscard]] inline bool has_direct_isel_mapping(ir::OpCode opcode) noexcept {
    return generated::classify_machine_form(opcode) != MachineForm::Unhandled;
}

[[nodiscard]] inline bool should_route_unhandled_to_runtime_call(ir::OpCode opcode) noexcept {
    // Profiling short-circuit: Avoid table probe if machine form classification hits natively
    if (generated::classify_machine_form(opcode) != MachineForm::Unhandled) {
        return false;
    }
    return runtime_contract::has_runtime_symbol(opcode);
}

[[nodiscard]] inline bool is_fully_unhandled_opcode(ir::OpCode opcode) noexcept {
    if (generated::classify_machine_form(opcode) != MachineForm::Unhandled) {
        return false;
    }
    return !runtime_contract::has_runtime_symbol(opcode);
}

} // namespace dgtl::isa::lowering::runtime_policy