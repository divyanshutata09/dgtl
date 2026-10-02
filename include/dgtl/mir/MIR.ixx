/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Canonical MIR Aggregate Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * DGTL canonical MIR aggregate. Exposes the complete target-independent
 * Machine IR representation, machine compilation pipeline, passes, and views.
 *
 * Note: dgtl.mir.abi_lowering is intentionally NOT re-exported here.
 * Target ABI interfaces live in dgtl.isa.<target> and target descriptors.
 */
module;

#include <cstdlib>

export module dgtl.mir;

// Core Machine IR Model (aggregates :types, :target, :function, :analysis, :verifier, :allocation)
export import dgtl.mir.machine_ir;

// Semantic Opcode Views & Wrappers
export import dgtl.mir.op_wrappers;

// Instruction Selection & Legalization
export import dgtl.mir.legalizer;
export import dgtl.mir.instruction_selector;
export import dgtl.mir.isel_gen;

// MIR Lowering & Optimization Passes
export import dgtl.mir.phi_lowering;
export import dgtl.mir.two_address;
export import dgtl.mir.stack_spiller;
export import dgtl.mir.strike_live_manager;

// Runtime Contracts & Policies
export import dgtl.mir.runtime_policy;
export import dgtl.mir.runtime_contract;

// Complete End-to-End Pipeline Driver
export import dgtl.mir.pipeline;