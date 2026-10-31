/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Unified Multicall Binary (dgtl-tools)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <iomanip>

import dgtl.tools.common;
import dgtl.testing.filecheck;

namespace fs = std::filesystem;

// External Tool Main Declarations (Pure C++26 Entry Points)
int dgtl_ar_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_as_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_link_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_llc_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_opt_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_objdump_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_inspect_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_debugger_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_verify_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);
int dgtl_engine_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);

// Sovereign FileCheck Entry Point
int dgtl_filecheck_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as) {
    std::vector<std::string> expanded = dgtl::tools::ArgExpander::expand(raw_args);
    std::string check_file;
    std::string input_file;
    bool verbose = false;

    for (size_t i = 0; i < expanded.size(); ++i) {
        const auto& arg = expanded[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << invoked_as << " <check_file> [input_file] [options]\n\n"
                      << "Direct FileCheck-style IR verification utility for DGTL.\n\n"
                      << "Options:\n"
                      << "  -v, --verbose    Enable verbose matching diagnostics\n"
                      << "  -h, --help       Show this help message\n";
            return 0;
        }
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
            continue;
        }
        if (arg.starts_with("-")) {
            std::cerr << "error[dgtl-filecheck]: unrecognized flag '" << arg << "'\n";
            return 1;
        }
        if (check_file.empty()) {
            check_file = arg;
        } else if (input_file.empty()) {
            input_file = arg;
        }
    }

    if (check_file.empty()) {
        std::cerr << "error[dgtl-filecheck]: no check file specified\n"
                  << "Usage: " << invoked_as << " <check_file> [input_file]\n";
        return 1;
    }

    std::string check_source = dgtl::tools::FileUtils::read_all_text(check_file);
    if (check_source.empty() && !fs::exists(check_file)) {
        std::cerr << "error[dgtl-filecheck]: unable to open check file '" << check_file << "'\n";
        return 1;
    }

    std::string input_text;
    if (input_file.empty() || input_file == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        input_text = ss.str();
    } else {
        input_text = dgtl::tools::FileUtils::read_all_text(input_file);
        if (input_text.empty() && !fs::exists(input_file)) {
            std::cerr << "error[dgtl-filecheck]: unable to open input file '" << input_file << "'\n";
            return 1;
        }
    }

    auto directives = dgtl::testing::FileCheckParser::parse(check_source);
    auto match_result = dgtl::testing::FileCheckMatcher::verify(directives, input_text);

    if (!match_result.success) {
        std::cerr << "FileCheck failed in " << check_file << ":" << match_result.directive_line << ": "
                  << match_result.error_message << "\n";
        if (verbose && match_result.failed_line > 0) {
            std::cerr << "  failed matching at line " << match_result.failed_line << "\n";
        }
        return 1;
    }

    if (verbose) {
        std::cout << "FileCheck: all " << directives.size() << " directives passed successfully.\n";
    }
    return 0;
}

namespace {

struct ToolDescriptor {
    std::string_view canonical_name;
    std::vector<std::string_view> aliases;
    dgtl::tools::ToolMainFn entry_fn;
    std::string_view description;
};

const std::vector<ToolDescriptor>& get_tool_registry() {
    static const std::vector<ToolDescriptor> registry = {
        {"ar",        {"dgtl-ar", "archive", "archiver"},                dgtl_ar_tool_main,        "DGTL Sovereign Static Library Archiver"},
        {"as",        {"dgtl-as", "asm", "assembler"},                  dgtl_as_tool_main,        "DGTL Sovereign Native Machine Code Assembler"},
        {"link",      {"dgtl-link", "ld", "linker"},                    dgtl_link_tool_main,      "DGTL Sovereign Universal AOT Linker (PE/ELF/Mach-O/WASM)"},
        {"llc",       {"dgtl-llc", "codegen", "compiler"},              dgtl_llc_tool_main,       "DGTL Sovereign Low-Level Compiler / Machine Emitter"},
        {"opt",       {"dgtl-opt", "optimizer"},                        dgtl_opt_tool_main,       "DGTL Sovereign IR Transformation & Optimization Driver"},
        {"objdump",   {"dgtl-objdump", "disasm"},                       dgtl_objdump_tool_main,   "DGTL Object File Disassembler & Section Inspector"},
        {"inspect",   {"dgtl-inspect"},                                 dgtl_inspect_tool_main,   "DGTL Deep Binary & Section Forensic Inspector"},
        {"debugger",  {"dgtl-debugger", "debug", "trace"},              dgtl_debugger_tool_main,  "DGTL Enterprise Native Process & Forensic Debugger"},
        {"verify",    {"dgtl-verify", "smt", "formal"},                 dgtl_verify_tool_main,    "DGTL Mathematical Equivalence & SMT-LIB2 Verifier"},
        {"engine",    {"dgtl-engine", "jit", "runner"},                 dgtl_engine_tool_main,    "DGTL Sovereign JIT Engine & Native Code Runner"},
        {"filecheck", {"dgtl-filecheck", "check"},                      dgtl_filecheck_tool_main, "DGTL FileCheck-Style IR Verification Framework"}
    };
    return registry;
}

const ToolDescriptor* find_tool(std::string_view query) {
    std::string normalized = dgtl::tools::normalize_tool_name(query);
    const auto& registry = get_tool_registry();

    for (const auto& tool : registry) {
        if (tool.canonical_name == normalized) {
            return &tool;
        }
        for (const auto& alias : tool.aliases) {
            if (alias == normalized || dgtl::tools::normalize_tool_name(alias) == normalized) {
                return &tool;
            }
        }
    }
    return nullptr;
}

void print_tools_help(std::string_view prog_name) {
    using Terminal = dgtl::tools::Terminal;
    std::cout << Terminal::bold() << Terminal::cyan()
              << "================================================================================\n"
              << "              DGTL UNIFIED MULTICALL DEVELOPER TOOLCHAIN (dgtl-tools)          \n"
              << "================================================================================\n"
              << Terminal::reset();
    std::cout << "Enterprise C++26 sovereign compiler toolchain consolidated into a single binary.\n\n"
              << "USAGE:\n"
              << "  " << Terminal::bold() << prog_name << " <tool_name> [args...]\n" << Terminal::reset()
              << "  (or invoke directly via hard link / symlink e.g. 'dgtl-link.exe [args...]')\n\n"
              << Terminal::bold() << "AVAILABLE INTEGRATED TOOLS:\n" << Terminal::reset();

    const auto& registry = get_tool_registry();
    for (const auto& tool : registry) {
        std::cout << "  " << Terminal::green() << Terminal::bold() << std::left << std::setw(12) 
                  << tool.canonical_name << Terminal::reset()
                  << "  " << tool.description << "\n";
    }

    std::cout << "\nFor tool-specific options, execute: "
              << Terminal::bold() << prog_name << " <tool_name> --help\n\n" << Terminal::reset();
}

} // namespace

int main(int argc, char* argv[]) {
    dgtl::tools::Terminal::init(true);

    if (argc < 1) {
        return 1;
    }

    // Resolve invocation context
    std::string argv0_stem = fs::path(argv[0]).stem().string();
    std::string normalized_stem = dgtl::tools::normalize_tool_name(argv0_stem);

    // Multicall Mode 1: Invoked directly as a specific tool via hardlink/symlink/launcher
    // e.g., 'dgtl-ar.exe', 'dgtl-link.exe', 'dgtl_llc'
    if (normalized_stem != "tools" && normalized_stem != "dgtl-tools" &&
        normalized_stem != "dgtl_tools" && normalized_stem != "main" &&
        normalized_stem != "smoke_main") {
        
        const ToolDescriptor* tool = find_tool(normalized_stem);
        if (tool != nullptr) {
            std::vector<std::string_view> raw_args;
            raw_args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
            for (int i = 1; i < argc; ++i) {
                raw_args.push_back(argv[i]);
            }
            return tool->entry_fn(raw_args, normalized_stem);
        }
    }

    // Multicall Mode 2: Invoked as the multicall binary with subcommand as argv[1]
    // e.g., 'dgtl-tools link [args...]', 'dgtl-tools opt [args...]'
    if (argc < 2) {
        print_tools_help(argv[0]);
        return 0;
    }

    std::string_view first_arg = argv[1];
    if (first_arg == "-h" || first_arg == "--help" || first_arg == "help") {
        print_tools_help(argv[0]);
        return 0;
    }
    if (first_arg == "-v" || first_arg == "--version" || first_arg == "version") {
        std::cout << "dgtl-tools: DGTL Unified Sovereign Toolchain v2.1.0 (TataG Systems)\n"
                  << "Consolidated multicall binary architecture (Zero Macro Toggles / C++26 Modules)\n";
        return 0;
    }

    const ToolDescriptor* tool = find_tool(first_arg);
    if (tool != nullptr) {
        std::vector<std::string_view> raw_args;
        raw_args.reserve(argc > 2 ? static_cast<size_t>(argc - 2) : 0);
        for (int i = 2; i < argc; ++i) {
            raw_args.push_back(argv[i]);
        }
        return tool->entry_fn(raw_args, tool->canonical_name);
    }

    std::cerr << dgtl::tools::Terminal::red() << dgtl::tools::Terminal::bold()
              << "error[dgtl-tools]: unknown tool or subcommand '" << first_arg << "'\n\n"
              << dgtl::tools::Terminal::reset();
    print_tools_help(argv[0]);
    return 1;
}
