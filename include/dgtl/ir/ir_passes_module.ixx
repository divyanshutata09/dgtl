/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 */

export module dgtl.ir_passes;

// --- STRUCTURAL INFRASTRUCTURE PIPELINE SYSTEM ---
export import dgtl.ir_core.pass_manager_config;           // include\dgtl\pipeline\Pass_Manager_Config.ixx
export import dgtl.ir_core.pass_manager_analysis;         // include\dgtl\pipeline\Pass_Manager_Analysis.ixx
export import dgtl.ir_core.pass_manager_orchestrator;     // include\dgtl\pipeline\Pass_Manager_Orchestrator.ixx
export import dgtl.ir_core.pass_manager;                  // include\dgtl\pipeline\Pass_Manager.ixx
export import dgtl.ir_core.gpu_scheduling;                // include\dgtl\pipeline\GPU_Scheduling.ixx
export import dgtl.ir_core.performance_probes;            // include\dgtl\pipeline\Performance_Probes.ixx
export import dgtl.ir_core.pipeline_probes;               // include\dgtl\pipeline\Pipeline_Probes.ixx
export import dgtl.ir_core.runtime;                       // include\dgtl\ir\runtime\TLS_Flush_Merge.ixx
