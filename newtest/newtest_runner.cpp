/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Master Industry-Grade Multi-Tier Test Suite Runner
 * Owner:       Divyanshu Tata
 */

#define DGTL_COMBINED_SUITE 1

#include "industry_test_harness.h"

// ============================================================================
// TIER 1: Architectural Layer Gates & Golden Health Audits
// ============================================================================
#include "test_layer01_polyglot_frontend.cpp"
#include "test_layer02_transactional_irstream.cpp"
#include "test_layer03_smt_formal_verifier.cpp"
#include "test_layer04_opt_pipeline.cpp"
#include "test_layer05_polyhedral_engine.cpp"
#include "test_layer06_instruction_selection.cpp"
#include "test_layer07_universal_regalloc.cpp"
#include "test_layer08_mir_pipeline.cpp"
#include "test_layer09_all_9_isas.cpp"
#include "test_layer10_object_writers.cpp"
#include "test_layer11_sovereign_linkers.cpp"
#include "test_layer12_jit_execution.cpp"
#include "test_polyglot_differential_oracle.cpp"

// ============================================================================
// TIER 2: Exhaustive OpCode Matrix & Type Coverage
// ============================================================================
#include "tier2_opcode_matrix_integer.cpp"
#include "tier2_opcode_matrix_float.cpp"
#include "tier2_opcode_matrix_simd.cpp"
#include "tier2_opcode_matrix_memory.cpp"

// ============================================================================
// TIER 3: Optimization Pass Verifiers
// ============================================================================
#include "tier3_pass_gvn_and_dce.cpp"
#include "tier3_pass_licm_and_inlining.cpp"
#include "tier3_pass_polyhedral_affine.cpp"

// ============================================================================
// TIER 4: Pathological Stress Gates
// ============================================================================
#include "tier4_stress_virtual_registers.cpp"
#include "tier4_stress_cfg_deep_control.cpp"
#include "tier4_stress_arena_megabuffer.cpp"

// ============================================================================
// TIER 5: Real-World Computational Kernels
// ============================================================================
#include "tier5_kernel_sha256.cpp"
#include "tier5_kernel_gemm_matrix.cpp"
#include "tier5_kernel_quicksort.cpp"
#include "tier5_kernel_monte_carlo.cpp"

// ============================================================================
// TIER 6: ABI Calling Convention Torture
// ============================================================================
#include "tier6_abi_torture_calls.cpp"

// ============================================================================
// TIER 7: Multi-Threaded Concurrency Torture
// ============================================================================
#include "tier7_stress_concurrency_threads.cpp"

// ============================================================================
// TIER 8: Security Fuzzing & Malformed Input Hardening
// ============================================================================
#include "tier8_security_fuzzing_malformed_ir.cpp"

// ============================================================================
// TIER 9: Linker Relocations & Format Compliance
// ============================================================================
#include "tier9_linker_relocations_and_tls.cpp"

// ============================================================================
// TIER 10: Stack Unwinding & SEH Exception Gates
// ============================================================================
#include "tier10_stack_unwind_seh_safeguards.cpp"

// ============================================================================
// TIER 11: Production & Client Tools Verification Suite (All Executables)
// ============================================================================
#include "tier11_toolchain_link_pe_conformance.cpp"       // dgtl-link
#include "tier11_toolchain_opt_algebraic_identities.cpp"   // dgtl-opt
#include "tier11_toolchain_as_assembler_spec.cpp"          // dgtl-as
#include "tier11_toolchain_engine_multithread_jit.cpp"     // dgtl-engine
#include "tier11_toolchain_verify_smt_theorems.cpp"        // dgtl-verify
#include "tier11_toolchain_config_discovery.cpp"           // dgtl-config
#include "tier11_toolchain_llc_multitarget.cpp"            // dgtl-llc
#include "tier11_toolchain_ar_archive_lifecycle.cpp"       // dgtl-ar
#include "tier11_toolchain_objdump_disasm.cpp"             // dgtl-objdump
#include "tier11_toolchain_inspect_forensics.cpp"          // dgtl-inspect
#include "tier11_toolchain_filecheck_matcher.cpp"          // dgtl-filecheck
#include "tier11_toolchain_rodata_interning.cpp"           // dgtl-rodata / dgtl-rodata-maker
#include "tier11_toolchain_debugger_audit.cpp"             // dgtl-debugger
#include "tier11_toolchain_multicall_dispatcher.cpp"       // dgtl_release / dgtl-tools

// ============================================================================
// TIER 12: Internal Quality & CI Tooling Verification Suite
// ============================================================================
#include "tier12_tool_dup_gate_analysis.cpp"              // dgtl-dup-gate
#include "tier12_tool_api_policy_contract.cpp"            // dgtl-api-policy
#include "tier12_tool_header_check_compliance.cpp"        // dgtl-header-check
#include "tier12_tool_gen_dsl_validation.cpp"             // dgtl-gen / dgtl-gen-validate
#include "tier12_tool_isel_gen_table.cpp"                 // dgtl-isel-gen
#include "tier12_tool_orchestrator_and_forwarder.cpp"     // dgtl (orchestrator) / dgtl_tool_forwarder

int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}


