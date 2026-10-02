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
 
/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * DGTL canonical HIR aggregate. This module exposes only target-independent IR
 * representation, schema, analyses, verification, rewriting and pass APIs.
 * Target configuration and code generation are downstream consumers.
 */
module;

export module dgtl.ir;

export import dgtl.ir_enums;
export import dgtl.ir_schema;
export import dgtl.ir_node;
export import dgtl.ir_storage;
export import dgtl.ir_traits;
export import dgtl.op_traits;
export import dgtl.ir_contracts;
export import dgtl.ir_analysis;
export import dgtl.ir_analysis_manager;
export import dgtl.ir_diagnostics;
export import dgtl.ir_pass_contract;
export import dgtl.ir_pass_manager;
export import dgtl.ir_transaction;
export import dgtl.ir.fwd;
export import dgtl.ir.stream;
export import dgtl.ir.ssa;
export import dgtl.ir.verifier;
export import dgtl.ir.io;
export import dgtl.ir.rewrite;
export import dgtl.ir.evolution;
export import dgtl.ir.bridge;
export import dgtl.ir.gen;
export import dgtl.ir.gen_wrappers;
export import dgtl.ir.debug;
export import dgtl.ir_function;
export import dgtl.ir_control_flow;
export import dgtl.ir_formal_verifier;
export import dgtl.hir.semantic;
