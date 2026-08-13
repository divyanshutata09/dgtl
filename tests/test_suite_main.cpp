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

#include <exception>
#include <iostream>
#include <stdexcept>

// --- test entry-points (defined in t01–t11 source files) ---
void run_test_t01_ir_stream_correctness();
void run_test_t02_ir_transaction_rollback();
void run_test_t03_optimizer_gvn_const_fold();
void run_test_t04_optimizer_fusion();
void run_test_t05_optimizer_volatile_guard();
void run_test_t06_codegen_arm64_emission();
void run_test_t07_frontend_pipeline_lowerer();
void run_test_t08_sdk_c_api_roundtrip();
void run_test_t09_runtime_mt_arena_isolation();
void run_test_t10_optimizer_strike_throughput();
void run_test_t11_pipeline_production_benchmark();
void run_test_t12_ir_cfg();
void run_test_t13_sdk_session_capabilities();

namespace {

void run_named_test(const char* name, void (*fn)()) {
    if (fn == nullptr) {
        throw std::runtime_error("test entrypoint pointer was null");
    }
    std::cout << "[DGTL] START " << name << "\n";
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "[DGTL] FAIL  " << name << " — " << ex.what() << "\n";
        throw; // propagate so main returns 1
    }
    std::cout << "[DGTL] PASS  " << name << "\n";
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    try {
        std::cout << "[DGTL] ====== DGTL Aggregate Test Suite (13 tests) ======\n";

        run_named_test("t01.ir_stream_correctness",   &run_test_t01_ir_stream_correctness);
        run_named_test("t02.ir_transaction_rollback",  &run_test_t02_ir_transaction_rollback);
        run_named_test("t03.optimizer_gvn_const_fold", &run_test_t03_optimizer_gvn_const_fold);
        run_named_test("t04.optimizer_fusion",         &run_test_t04_optimizer_fusion);
        run_named_test("t05.optimizer_volatile_guard", &run_test_t05_optimizer_volatile_guard);
        run_named_test("t06.codegen_arm64_emission",   &run_test_t06_codegen_arm64_emission);
        run_named_test("t07.frontend_lowerer",         &run_test_t07_frontend_pipeline_lowerer);
        run_named_test("t08.sdk_c_api_roundtrip",      &run_test_t08_sdk_c_api_roundtrip);
        run_named_test("t09.runtime_mt_arena",         &run_test_t09_runtime_mt_arena_isolation);
        run_named_test("t10.optimizer_strike",         &run_test_t10_optimizer_strike_throughput);
        run_named_test("t11.pipeline_production",      &run_test_t11_pipeline_production_benchmark);
        run_named_test("t12.ir_cfg",                   &run_test_t12_ir_cfg);
        run_named_test("t13.sdk_session_capabilities", &run_test_t13_sdk_session_capabilities);

        std::cout << "[DGTL] ====== All 13 tests PASSED ======\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[DGTL] ====== Suite FAILED: " << ex.what() << " ======\n";
        return 1;
    }
}