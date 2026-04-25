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
#include <string_view>

void run_test_pipeline1();
void run_test_pipeline_multi_target();
void run_test_release_ir_modes();
void run_test_arm64_fcvtl_widening();
void run_test_arm64_fcvtn_narrowing();
void run_test_temporal_fusion_predication();
void run_test_frontend_bridge_smoke();
void run_test_frontend_link_api_emit();
void run_test_frontend_pipeline_lowerer();
void run_test_frontend_payload_io();
void run_test_poly_matmul_scop_tiling();
void run_test_robustness_1();
void run_test_robustness_2();
void run_test_robustness_3();
void run_test_robustness_4();
void run_test_inliner_safety();
void run_test_speculative_inliner();
void run_test_performance_mt_arena();
void run_test_performance_mt_codegen();
void run_test_opt_strike();

namespace {

bool parse_full_mode(int argc, char** argv) {
    if (argc <= 1) {
        return false;
    }

    const std::string_view arg(argv[1]);
    if (arg == "--full") {
        return true;
    }

    throw std::runtime_error("unknown argument; expected --full");
}

void run_named_test(const char* name, void (*fn)()) {
    std::cout << "[DGTL test] START " << name << "\n";
    fn();
    std::cout << "[DGTL test] PASS  " << name << "\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool run_full_suite = parse_full_mode(argc, argv);

        std::cout << "[DGTL test] Running "
                  << (run_full_suite ? "full" : "sanity")
                  << " aggregate suite\n";

        run_named_test("functional.pipeline1", &run_test_pipeline1);
        run_named_test("functional.multi_target", &run_test_pipeline_multi_target);
        run_named_test("functional.release_ir_modes", &run_test_release_ir_modes);
        run_named_test("functional.arm64_fcvtl_widening", &run_test_arm64_fcvtl_widening);
        run_named_test("functional.arm64_fcvtn_narrowing", &run_test_arm64_fcvtn_narrowing);
        run_named_test("functional.temporal_fusion_predication", &run_test_temporal_fusion_predication);
        run_named_test("functional.frontend_bridge_smoke", &run_test_frontend_bridge_smoke);
        run_named_test("functional.frontend_link_api_emit", &run_test_frontend_link_api_emit);
        run_named_test("functional.frontend_pipeline_lowerer", &run_test_frontend_pipeline_lowerer);
        run_named_test("functional.frontend_payload_io", &run_test_frontend_payload_io);
        run_named_test("functional.poly_matmul_scop_tiling", &run_test_poly_matmul_scop_tiling);
        run_named_test("robustness.core1", &run_test_robustness_1);
        run_named_test("robustness.core3", &run_test_robustness_3);
        run_named_test("robustness.inliner_safety", &run_test_inliner_safety);
        run_named_test("robustness.speculative_inliner", &run_test_speculative_inliner);
        run_named_test("performance.mt_arena", &run_test_performance_mt_arena);

        if (run_full_suite) {
            run_named_test("robustness.core2", &run_test_robustness_2);
            run_named_test("robustness.core4", &run_test_robustness_4);
            run_named_test("performance.mt_codegen", &run_test_performance_mt_codegen);
            run_named_test("performance.opt_strike", &run_test_opt_strike);
        }

        std::cout << "[DGTL test] Aggregate suite completed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[DGTL test] FAIL: " << ex.what() << "\n";
        return 1;
    }
}