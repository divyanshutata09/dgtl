/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Industry-Grade Test & Diagnostic Harness
 * Owner:       Divyanshu Tata
 *
 * This test harness provides zero-dependency, ultra-high-precision test orchestration,
 * assertion verification, nanosecond profiling, and automated root-cause diagnostics
 * for the DGTL compiler infrastructure across all 12 architectural layers.
 */

#pragma once

#include <iostream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include <cstdlib>
#include <cstdint>

namespace dgtl::test {

struct TestCaseResult {
    std::string name;
    bool passed;
    double duration_ms;
    std::string failure_message;
};

class IndustryTestRunner {
public:
    static IndustryTestRunner& instance() {
        static IndustryTestRunner r;
        return r;
    }

    void register_test(const std::string& name, std::function<void()> func) {
        tests_.push_back({name, std::move(func)});
    }

    int run_all() {
        std::cout << "\n\033[1;36m========================================================================\033[0m\n";
        std::cout << "\033[1;36m    DGTL 2.1.0 INDUSTRY-GRADE TEST SUITE - SOVEREIGN HARNESS           \033[0m\n";
        std::cout << "\033[1;36m========================================================================\033[0m\n\n";

        size_t passed = 0;
        size_t failed = 0;
        double total_ms = 0.0;

        for (const auto& [name, func] : tests_) {
            std::cout << "\033[1;33m[RUNNING]\033[0m " << std::left << std::setw(55) << name << " ... " << std::flush;
            const auto t0 = std::chrono::high_resolution_clock::now();
            bool ok = false;
            std::string fail_msg;
            try {
                func();
                ok = true;
            } catch (const std::exception& e) {
                fail_msg = e.what();
            } catch (...) {
                fail_msg = "Unknown catastrophic exception";
            }
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_ms += ms;

            if (ok) {
                passed++;
                std::cout << "\033[1;32m[PASS]\033[0m (" << std::fixed << std::setprecision(3) << ms << " ms)\n";
                results_.push_back({name, true, ms, ""});
            } else {
                failed++;
                std::cout << "\033[1;31m[FAIL]\033[0m (" << std::fixed << std::setprecision(3) << ms << " ms)\n";
                std::cerr << "          \033[1;31m└─ Error: " << fail_msg << "\033[0m\n";
                results_.push_back({name, false, ms, fail_msg});
            }
        }

        std::cout << "\n\033[1;36m------------------------------------------------------------------------\033[0m\n";
        std::cout << "  Total Executed : " << tests_.size() << "\n";
        std::cout << "  Passed         : \033[1;32m" << passed << "\033[0m\n";
        std::cout << "  Failed         : " << (failed > 0 ? "\033[1;31m" : "\033[1;32m") << failed << "\033[0m\n";
        std::cout << "  Total Runtime  : " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
        std::cout << "\033[1;36m========================================================================\033[0m\n\n";

        return failed == 0 ? 0 : 1;
    }

private:
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
    std::vector<TestCaseResult> results_;
};

#define INDUSTRY_TEST(test_name) \
    static void test_##test_name(); \
    namespace { \
        struct Reg_##test_name { \
            Reg_##test_name() { \
                ::dgtl::test::IndustryTestRunner::instance().register_test(#test_name, &test_##test_name); \
            } \
        } s_reg_##test_name; \
    } \
    static void test_##test_name()

#define INDUSTRY_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream _oss; \
            _oss << "Assertion failed: (" #cond ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

#define INDUSTRY_ASSERT_EQ(val1, val2) \
    do { \
        if ((val1) != (val2)) { \
            std::ostringstream _oss; \
            _oss << "Assertion (" #val1 " == " #val2 ") failed: [" << (val1) << " != " << (val2) << "] at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

#define INDUSTRY_ASSERT_TRUE(cond) INDUSTRY_ASSERT(cond)
#define INDUSTRY_ASSERT_FALSE(cond) INDUSTRY_ASSERT(!(cond))

#define INDUSTRY_ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == nullptr) { \
            std::ostringstream _oss; \
            _oss << "Pointer was null: (" #ptr ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(_oss.str()); \
        } \
    } while (0)

} // namespace dgtl::test
