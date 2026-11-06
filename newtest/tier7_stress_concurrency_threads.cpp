/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 7: Heterogeneous Multi-Threaded Compilation Stress
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"
#include <thread>
#include <vector>
#include <atomic>

INDUSTRY_TEST(tier7_concurrency_16_threads_heterogeneous_workloads) {
    const int kNumThreads = 16;
    std::vector<std::thread> workers;
    std::atomic<int> pass_count{0};

    // 16 distinct mathematical functions and their exact expected return codes
    const struct Workload {
        const char* myzon_code;
        int64_t expected_exit;
    } kWorkloads[16] = {
        {"fn main() -> i32 { return 2 * 3 * 4; }", 24},
        {"fn main() -> i32 { return 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10; }", 55},
        {"fn main() -> i32 { return 1 << 6; }", 64},
        {"fn main() -> i32 { return 100 - 27; }", 73},
        {"fn main() -> i32 { return 9 * 9; }", 81},
        {"fn main() -> i32 { return 1000 / 10; }", 100},
        {"fn main() -> i32 { return 5 * 5 * 5; }", 125},
        {"fn main() -> i32 { return 256 >> 1; }", 128},
        {"fn main() -> i32 { return 200 - 56; }", 144},
        {"fn main() -> i32 { return 13 * 13; }", 169},
        {"fn main() -> i32 { return 14 * 14; }", 196},
        {"fn main() -> i32 { return 15 * 15; }", 225},
        {"fn main() -> i32 { return 16 * 16; }", 256},
        {"fn main() -> i32 { return 10 * 10 * 3; }", 300},
        {"fn main() -> i32 { return 20 * 20; }", 400},
        {"fn main() -> i32 { return 1024 / 2; }", 512}
    };

    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([t, &kWorkloads, &pass_count]() {
            auto report = dgtl::debug::verify_against_golden(
                kWorkloads[t].myzon_code, 
                "", 
                42, 
                DGTL_LANG_MYZON, 
                2
            );
            if (report.is_ok() && report.exit_code() == 42) {
                pass_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    INDUSTRY_ASSERT_EQ(pass_count.load(), kNumThreads);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
