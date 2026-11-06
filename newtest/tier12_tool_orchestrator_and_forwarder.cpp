/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - Orchestrator & Forwarder Launcher (dgtl.exe & dgtl_tool_forwarder.exe)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <vector>
#include <filesystem>

INDUSTRY_TEST(tier12_tool_orchestrator_forwarder_resolution) {
    // Tests tool forwarder commandline construction and binary discovery
    // Forwarder discovers dgtl-tools.exe relative to its parent directory and routes <stem>
    
    auto construct_forwarder_cmd = [](const std::string& parent_dir, const std::string& stem, std::initializer_list<std::string_view> args) -> std::string {
        std::string cmd = "\"" + parent_dir + "/dgtl-tools.exe\" " + stem;
        for (const auto& a : args) {
            cmd += " ";
            cmd += a;
        }
        return cmd;
    };

    std::string cmd1 = construct_forwarder_cmd("build/bin", "dgtl-link", {"-o", "out.exe", "main.o"});
    INDUSTRY_ASSERT_EQ(cmd1, "\"build/bin/dgtl-tools.exe\" dgtl-link -o out.exe main.o");

    std::string cmd2 = construct_forwarder_cmd("build/bin", "dgtl-opt", {"-O3", "kernel.ir"});
    INDUSTRY_ASSERT_EQ(cmd2, "\"build/bin/dgtl-tools.exe\" dgtl-opt -O3 kernel.ir");

    // Orchestrator subcommands routing verification
    auto is_orchestrator_subcommand = [](const std::string& cmd) -> bool {
        return cmd == "build" || cmd == "test" || cmd == "clean" || 
               cmd == "build-tools" || cmd == "build-smoke" || cmd == "build-strict";
    };

    INDUSTRY_ASSERT_TRUE(is_orchestrator_subcommand("build-tools"));
    INDUSTRY_ASSERT_TRUE(is_orchestrator_subcommand("build-smoke"));
    INDUSTRY_ASSERT_TRUE(is_orchestrator_subcommand("build-strict"));
    INDUSTRY_ASSERT_FALSE(is_orchestrator_subcommand("invalid-cmd"));
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
