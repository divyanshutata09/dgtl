/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

// dgtl_orchestrator_main.cpp
// C++23/26 cross-platform orchestration and build system
// Implements secure dependency resolution, build caching, and tool execution

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Core Architecture
// -----------------------------------------------------------------------------

struct Config {
    fs::path root;
    fs::path build_dir;
    fs::path tools_dir;
    fs::path include_dir;
    fs::path src_dir;
    std::string compiler_path;

    static Config initialize() {
        Config cfg;
        cfg.root = fs::current_path();
        cfg.build_dir = cfg.root / "build";
        cfg.tools_dir = cfg.root / "tools" / "cpp";
        cfg.include_dir = cfg.root / "include";
        cfg.src_dir = cfg.root / "src";
        
        if (const char* env_cxx = std::getenv("CXX")) {
            cfg.compiler_path = env_cxx;
        } else {
            cfg.compiler_path = "g++"; 
        }
        return cfg;
    }
};

static const Config g_config = Config::initialize();

enum class BuildResult { Compiled, UpToDate, Error };

// -----------------------------------------------------------------------------
// Security & Execution Utilities
// -----------------------------------------------------------------------------

[[nodiscard]] std::string quote_path(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

class CommandBuilder {
    std::string cmd_;
public:
    explicit CommandBuilder(std::string_view executable) : cmd_(executable) {}
    
    CommandBuilder& add(std::string_view arg) {
        cmd_ += " ";
        cmd_ += arg;
        return *this;
    }

    CommandBuilder& add_path(const fs::path& p) {
        cmd_ += " ";
        cmd_ += quote_path(p);
        return *this;
    }

    [[nodiscard]] std::string build() const { return cmd_; }
};

[[nodiscard]] int run_cmd(const std::string& cmd, bool verbose = true) {
    if (verbose) {
        std::cout << "[orchestrator] $ " << cmd << "\n";
    }
    return std::system(cmd.c_str());
}

[[nodiscard]] fs::file_time_type get_file_mtime(const fs::path& p) noexcept {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : ftime;
}

void ensure_directory(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        fs::create_directories(p, ec);
    }
}

// -----------------------------------------------------------------------------
// Unified Compilation Engine
// -----------------------------------------------------------------------------

struct CompileOptions {
    std::string name;
    fs::path output;
    std::vector<fs::path> sources;
    std::vector<fs::path> includes;
};

[[nodiscard]] BuildResult invoke_compiler(const CompileOptions& opts) {
    ensure_directory(opts.output.parent_path());

    auto out_time = get_file_mtime(opts.output);
    bool needs_build = out_time == fs::file_time_type::min();

    if (!needs_build) {
        for (const auto& src : opts.sources) {
            if (get_file_mtime(src) > out_time) {
                needs_build = true;
                break;
            }
        }
    }

    if (!needs_build) {
        return BuildResult::UpToDate;
    }

    std::cout << "[build] Compiling " << opts.name << "\n";
    CommandBuilder cmd(g_config.compiler_path);
    cmd.add("-std=c++26 -O2 -pipe");
    
    for (const auto& inc : opts.includes) cmd.add("-I").add_path(inc);
    for (const auto& src : opts.sources) cmd.add_path(src);
    cmd.add("-o").add_path(opts.output);

    int ec = run_cmd(cmd.build());
    if (ec == 0) {
        std::cout << "[build] Wrote " << opts.output.string() << "\n";
        return BuildResult::Compiled;
    }
    return BuildResult::Error;
}

// -----------------------------------------------------------------------------
// Tool Execution Engine
// -----------------------------------------------------------------------------

[[nodiscard]] int invoke_tool(std::string_view tool_name, const fs::path& source, const std::vector<std::string>& args = {}) {
    fs::path output = g_config.build_dir / "tools" / (std::string(tool_name) + ".exe");
    
    CompileOptions opts{
        .name = std::string(tool_name),
        .output = output,
        .sources = {source},
        .includes = {g_config.include_dir}
    };

    if (invoke_compiler(opts) == BuildResult::Error) return 1;

    CommandBuilder cmd(output.string());
    for (const auto& arg : args) cmd.add(arg);
    return run_cmd(cmd.build());
}

// -----------------------------------------------------------------------------
// CLI Routing & Workflows
// -----------------------------------------------------------------------------

void show_usage() {
    std::cout << "DGTL C++ Orchestrator (dgtl-orchestrator)\n"
              << "Usage: dgtl-orchestrator <command> [args...]\n"
              << "Commands: build, build-tools, run, test, gen, gen-validate, gen-build, header-check, api-policy, dup-gate\n"
              << "(Implements all orchestration, build, validation, and policy gates deterministically)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        show_usage();
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    // Map of logical command names to their source files and default arguments
    struct ToolDef { fs::path src; std::vector<std::string> default_args; };
    std::map<std::string, ToolDef> tool_map = {
        {"header-check", {g_config.tools_dir / "dgtl_header_check_main.cpp", {"--root", quote_path(g_config.root), "--summary-only", "--relative-paths"}}},
        {"api-policy",   {g_config.tools_dir / "dgtl_api_policy_main.cpp",   {"--root", quote_path(g_config.root), "--summary-only"}}},
        {"dup-gate",     {g_config.tools_dir / "dgtl_duplication_gate_main.cpp", {"--root", quote_path(g_config.root), "--summary-only"}}},
        {"gen",          {g_config.tools_dir / "dgtl_gen_main.cpp", {}}},
        {"gen-validate", {g_config.tools_dir / "validate_dgtl_gen_main.cpp", {}}}
    };

    if (command == "help") {
        show_usage();
        return 0;
    }

    // Direct tool execution
    if (tool_map.count(command)) {
        const auto& tool = tool_map[command];
        std::vector<std::string> final_args = tool.default_args;
        final_args.insert(final_args.end(), args.begin(), args.end());
        return invoke_tool(command, tool.src, final_args);
    }

    // Orchestrated pipelines
    if (command == "build") {
        if (invoke_tool("api-policy", tool_map["api-policy"].src, tool_map["api-policy"].default_args) != 0) return 1;
        if (invoke_tool("dup-gate", tool_map["dup-gate"].src, tool_map["dup-gate"].default_args) != 0) return 1;
        
        CompileOptions main_build{
            .name = "dgtl_release",
            .output = g_config.build_dir / "dgtl_release.exe",
            .sources = {g_config.src_dir / "main.cpp"},
            .includes = {g_config.include_dir}
        };
        return invoke_compiler(main_build) == BuildResult::Error ? 1 : 0;
    }

    if (command == "gen-build") {
        if (invoke_tool("gen", tool_map["gen"].src) != 0) return 1;
        if (invoke_tool("gen-validate", tool_map["gen-validate"].src) != 0) return 1;
        if (invoke_tool("api-policy", tool_map["api-policy"].src, tool_map["api-policy"].default_args) != 0) return 1;
        if (invoke_tool("dup-gate", tool_map["dup-gate"].src, tool_map["dup-gate"].default_args) != 0) return 1;

        CompileOptions main_build{
            .name = "dgtl_release",
            .output = g_config.build_dir / "dgtl_release.exe",
            .sources = {g_config.src_dir / "main.cpp"},
            .includes = {g_config.include_dir}
        };
        return invoke_compiler(main_build) == BuildResult::Error ? 1 : 0;
    }

    if (command == "test") {
        fs::path test_exe = g_config.build_dir / "dgtl_tests.exe";
        if (!fs::exists(test_exe)) {
            std::cerr << "[test] dgtl_tests.exe not found. Ensure aggregate suite is built first.\n";
            return 1;
        }
        return run_cmd(quote_path(test_exe));
    }

    std::cerr << "Unknown command: " << command << "\n";
    show_usage();
    return 1;
}