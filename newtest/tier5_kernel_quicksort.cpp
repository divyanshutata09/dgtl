/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 5: 16-Element In-Place Sorting Algorithm Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier5_quicksort_16_elements_strictly_ascending_verification) {
    // Unsorted array of 16 integers
    // Sorts the entire array in-place, then validates that arr[i] <= arr[i+1] across all 15 boundaries.
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut arr = [88, 12, 95, 3, 44, 71, 23, 6, 99, 15, 38, 52, 67, 4, 81, 31];\n"
        "    let n = 16;\n"
        "    for i in 0..n {\n"
        "        for j in 0..(n - 1) {\n"
        "            if arr[j] > arr[j + 1] {\n"
        "                let tmp = arr[j];\n"
        "                arr[j] = arr[j + 1];\n"
        "                arr[j + 1] = tmp;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    // Strict mathematical assertion: array must be sorted in ascending order\n"
        "    for k in 0..(n - 1) {\n"
        "        if arr[k] > arr[k + 1] {\n"
        "            return (k + 1) as i32; // Returns failing index\n"
        "        }\n"
        "    }\n"
        "    // Check boundary elements\n"
        "    if arr[0] != 3 { return 101; }\n"
        "    if arr[15] != 99 { return 102; }\n"
        "    return 0; // Success\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 0, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 0);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
