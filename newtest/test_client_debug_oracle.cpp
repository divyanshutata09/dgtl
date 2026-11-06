/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Golden Reference Oracle & Stack Health Audit
 * Owner:       Divyanshu Tata
 */

#include "dgtl_sdk/dgtl.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "========================================================================\n";
    std::cout << "  DGTL 2.1.0 CLIENT DEBUG ORACLE - 12-LAYER FULL STACK AUDIT            \n";
    std::cout << "========================================================================\n\n";

    const char* myzon_code = 
        "fn main() -> i32 {\n"
        "    let a = 40;\n"
        "    let b = 2;\n"
        "    return a + b;\n"
        "}\n";

    std::cout << "[Audit] Ingesting MYZON Source Code:\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << myzon_code;
    std::cout << "------------------------------------------------------------------------\n\n";

    std::cout << "[Audit] Executing One-Line Verification across all 12 DGTL layers (Opt Level 2)...\n";

    // One-line audit across all 12 DGTL layers against expected exit code 42
    auto report = dgtl::debug::verify_against_golden(myzon_code, "", 42, DGTL_LANG_MYZON, 2);

    std::cout << "\n[Audit Summary]\n";
    std::cout << "  Verdict       : " << (report.is_ok() ? "\033[1;32mPASSED (HEALTHY)\033[0m" : "\033[1;31mFAILED (NEEDS IMPROVEMENT)\033[0m") << "\n";
    std::cout << "  DGTL Exit Code: " << report.exit_code() << "\n";
    std::cout << "  Golden Exit   : " << report->reference_exit_code << "\n";
    std::cout << "  IR Node Count : " << report->ir_node_count << "\n";
    std::cout << "  Logic Hash    : 0x" << std::hex << report->logic_hash << std::dec << "\n";
    std::cout << "  Total Latency : " << report->total_compilation_us << " us\n\n";

    std::cout << "[12 Architectural Layers Breakdown]:\n";
    std::cout << "  1. Polyglot Frontend Ingestion      : " << (report->layer_1_frontend.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  2. Transactional SoA IRStream/Arena : " << (report->layer_2_ir_stream.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  3. SMT Formal Equivalence & Sentinel: " << (report->layer_3_smt_formal.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  4. 26 Optimization Passes Pipeline  : " << (report->layer_4_ir_passes.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  5. Polyhedral Loop Transformation   : " << (report->layer_5_polyhedral.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  6. MIR Tree-Munch ISel Pattern Match: " << (report->layer_6_mir_isel.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  7. Universal Register Allocation    : " << (report->layer_7_regalloc.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  8. Machine IR Pipeline & Peephole   : " << (report->layer_8_mir_pipeline.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << "  9. 9-ISA Native Machine Codegen     : " << (report->layer_9_isa_codegen.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << " 10. Sovereign Binary Object Emission : " << (report->layer_10_object_file.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << " 11. Standalone Sovereign PE Linker   : " << (report->layer_11_sovereign_linker.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n";
    std::cout << " 12. In-Memory JIT Execution Engine   : " << (report->layer_12_jit_execution.status == DGTL_LAYER_PASS ? "PASS" : "FAIL") << "\n\n";

    if (report.is_ok()) {
        std::cout << "[PASS] Entire DGTL is 100% healthy across all 12 layers!\n";
        std::cout << "Return code: " << report.exit_code() << "\n";
    } else {
        std::cerr << "[FAIL] DGTL Needs Improvement at: Layer " << report.failing_layer() << "\n";
        std::cerr << "Root Cause: " << report.root_cause() << "\n";
        if (report->suggested_fix) {
            std::cerr << "Suggested Fix: " << report->suggested_fix << "\n";
        }
    }

    std::cout << "========================================================================\n";
    return report.is_ok() ? 0 : 1;
}
