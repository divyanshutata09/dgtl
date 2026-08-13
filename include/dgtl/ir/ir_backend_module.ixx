/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 */

export module dgtl.ir_backend;

// --- CORE FUNCTIONAL ARCHITECTURE & LOWERING ---
export import dgtl.ir_core.bridge;                        // include\dgtl\ir\interop\IR_Analysis_Bridge.ixx
export import dgtl.ir_core.abi_lowering;                  // include\dgtl\backend\lowering\ABI_Lowering.ixx
export import dgtl.ir_core.stack_spiller;                 // include\dgtl\backend\lowering\Stack_Spiller.ixx

// --- ISA ASSEMBLER TARGET ENCODERS & MACHINE EMITTERS ---
export import dgtl.ir_core.wasm_encoder;                  // include\dgtl\isa\target\Wasm_Encoder.ixx
export import dgtl.ir_core.wasm_target;                   // include\dgtl\isa\target\Wasm_Target.ixx
export import dgtl.ir_core.nasm_emitter;                  // include\dgtl\isa\emitter\NASM_Emitter.ixx
export import dgtl.ir_core.macho_emitter;                 // include\dgtl\isa\emitter\MachO_Emitter.ixx
export import dgtl.ir_core.arm32neon_emitter;             // include\dgtl\isa\emitter\ARM32NEON_Emitter.ixx
export import dgtl.ir_core.arm64_emitter;                 // include\dgtl\isa\emitter\ARM64_Emitter.ixx
export import dgtl.ir_core.riscv64_emitter;               // include\dgtl\isa\emitter\RiscV64_Emitter.ixx
export import dgtl.ir_core.wasmsimd_fma_emitter;          // include\dgtl\isa\emitter\WasmSIMD_FMA_Emitter.ixx

// --- TARGET LEGALIZATION & TARGET INSTRUCTION SELECTION (ISEL) ---
export import dgtl.ir_core.runtime_contract;              // include\dgtl\isa\lowering\Backend_Runtime_Contract.ixx
export import dgtl.ir_core.lowering;                      // include\dgtl\isa\lowering\Instruction_Selector.ixx
export import dgtl.ir_core.legalizer;                     // include\dgtl\isa\lowering\Legalizer.ixx
export import dgtl.ir_core.runtime_policy;                // include\dgtl\isa\lowering\Runtime_Legalization_Policy.ixx
export import dgtl.ir_core.x64_lowering;                  // include\dgtl\isa\lowering\X64_Machine_Form_Lowering.ixx
export import dgtl.ir_core.isel_gen;                      // include\dgtl\isa\lowering\gen\X64_ISel_Generated.ixx
export import dgtl.ir_core.isel_rules;                    // include\dgtl\isa\lowering\gen\X64_ISel_Rules_Generated.ixx

// --- MULTI-TARGET REGISTER ALLOCATION LAYERS ---
export import dgtl.ir_core.x64_reg_map;                   // include\dgtl\isa\regalloc\X64_Reg_Map.ixx
export import dgtl.ir_core.strike_live_manager;           // include\dgtl\isa\regalloc\Strike_Live_Manager.ixx
export import dgtl.ir_core.x64_graphcoloring_regalloc;    // include\dgtl\isa\regalloc\X64_GraphColoring_RegAlloc.ixx
export import dgtl.ir_core.arm64_regalloc;                // include\dgtl\isa\regalloc\ARM64_RegAlloc.ixx