/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:      DGTL (Deep General-purpose Transformation Layer)
 * Owner:        Divyanshu Tata
 * Repository:   github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 (DGTL Edition)
 * THE UNIVERSAL IR LINKER (UIL)
 * -------------------------------------------------------------------------
 * ORDER OF INCLUSION IS CRITICAL FOR C++26 TEMPLATE RESOLUTION.
 */

export module dgtl.ir_core;

// --- FOUNDATIONAL BASE LAYERS & STORAGE ---
// 🚀 DELETED BURDEN: Removed dgtl.ir_core.fwd to eliminate type-tracking ambiguity crashes completely.
export import dgtl.ir_core.stream;                  // include\dgtl\ir\base\IR_Stream.ixx
export import dgtl.ir_core.ssa;                           // include\dgtl\ir\base\IR_SSA.ixx
export import dgtl.ir_core.io;                            // include\dgtl\ir\base\IR_PrintSerialDumper.ixx
export import dgtl.ir_core.debug;                         // include\dgtl\ir\debug\debug_Metadata.ixx
export import dgtl.ir_core.gen;                           // include\dgtl\ir\gen\DGTL_Gen_Ops.ixx
export import dgtl.ir_core.abi;                           // include\dgtl\core\ABI.ixx
export import dgtl.ir_core.control_flow;                  // include\dgtl\core\ControlFlow.ixx
export import dgtl.ir_core.semantic;                      // include\dgtl\semantic\Symbol_Table.ixx
export import dgtl.ir_core.graph_region;                  // include\dgtl\graph\IR_GraphEdgeClusterRegion.ixx
export import dgtl.ir_core.function;                      // include\dgtl\core\Function.ixx

// --- STRUCTURAL VALIDATION, FORMALS & REWRITERS ---
export import dgtl.ir_core.verifier;                      // include\dgtl\ir\base\IR_Verifier.ixx
export import dgtl.ir_core.evolution;                     // include\dgtl\ir\evolver\IR_Evolver_Mutation.ixx
export import dgtl.ir_core.rewrite;                       // include\dgtl\ir\rewrite\Pattern_Rewriter.ixx
export import dgtl.ir_core.verified_formal;               // include\dgtl\core\Verified_Formal.ixx
export import dgtl.ir_core.formal_verification_smt;       // include\dgtl\formal\Formal_Verification_SMT.ixx

// --- ANALYSES & INTERFACE CONTRACTS ---
export import dgtl.ir_core.analysis_interface;            // include\dgtl\analysis\Analysis_Interface.ixx
export import dgtl.ir_core.ai_cost_model;                 // include\dgtl\analysis\AI_Cost_Model.ixx
export import dgtl.ir_core.polyhedral_scop_unified;       // include\dgtl\analysis\Polyhedral_Scop_Unified.ixx

export import dgtl.ir_core.speculative_value_profiler;    // include\dgtl\analysis\Speculative_Value_Profiler.ixx
export import dgtl.ir_core.liveness_memory_dependency_unified; // include\dgtl\analysis\Liveness_Memory_Dependency_Unified.ixx