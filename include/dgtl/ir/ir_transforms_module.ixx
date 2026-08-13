/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 */

export module dgtl.ir_transforms;

// --- SCALAR & VECTOR TRANSFORM PASS MATRIX ---
export import dgtl.ir_core.escape_analysis;               // include\dgtl\pass\analysis\Escape_Analysis.ixx
export import dgtl.ir_core.simd_gather_scatter;           // include\dgtl\pass\simd\SimdGScatter_LegalVecRecognizer.ixx
export import dgtl.ir_core.speculate_alias_dce_inliner;   // include\dgtl\pass\speculative\SpecAliasDCE_InlineVecFusion.ixx
export import dgtl.ir_core.transform_control_predication; // include\dgtl\pass\transform\Transform_Control_Predication.ixx
export import dgtl.ir_core.transform_inlining;             // include\dgtl\pass\transform\Transform_Inlining.ixx
export import dgtl.ir_core.transform_poly_loop;            // include\dgtl\pass\transform\Transform_Poly_Loop.ixx
export import dgtl.ir_core.transform_scalar_core;          // include\dgtl\pass\transform\Transform_Scalar_Core.ixx
export import dgtl.ir_core.ml_optimization;               // include\dgtl\pass\ml\MLoptimization.ixx
export import dgtl.ir_core.query_optimizer;               // include\dgtl\pass\query\Query_Optimizer_Unified.ixx
export import dgtl.ir_core.tuning_optimize;               // include\dgtl\pass\tuning\Tuning_Optimize.ixx
export import dgtl.ir_core.error_recovery;                // include\dgtl\pass\error_handling\Enhanced_Error_Recovery.ixx