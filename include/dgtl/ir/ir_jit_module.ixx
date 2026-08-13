/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 */

export module dgtl.ir_jit;

// --- DRIVERS, ENGINE KERNELS & EXECUTIONS ---
export import dgtl.ir_core.backend_driver_shared_utils;   // include\dgtl\backend\driver\Backend_Driver_Shared_Utils.ixx
export import dgtl.ir_core.backend_driver_api;            // include\dgtl\backend\driver\Backend_Driver_API.ixx
export import dgtl.ir_core.async_dopt_tiered;             // include\dgtl\jit\Async_Dopt_Tiered.ixx
export import dgtl.ir_core.hardware_scheduler;            // include\dgtl\jit\Hardware_Scheduler.ixx
export import dgtl.ir_core.jit_executor;                  // include\dgtl\jit\JIT_Executor.ixx
export import dgtl.ir_core.jit_orchestrator;              // include\dgtl\jit\JIT_Orchestrator.ixx
export import dgtl.ir_core.code_cache;                    // include\dgtl\execution\cache\Code_Cache.ixx
export import dgtl.ir_core.execution_dispatcher;          // include\dgtl\execution\dispatcher\Execution_Dispatcher.ixx
export import dgtl.ir_core.binary_loader;                 // include\dgtl\execution\format\Binary_Loader_object.ixx
export import dgtl.ir_core.linker_backend;                 // include\dgtl\backend\linker\Linker_Backend.ixx