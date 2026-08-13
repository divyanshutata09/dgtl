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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>

namespace fs = std::filesystem;

namespace {

struct Rule {
    std::string relative_path;
    std::string pattern;
    std::string description;
    bool must_match{true};
};

struct Options {
    fs::path root{"."};
    bool summary_only{false};
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--summary-only") {
            opts.summary_only = true;
        } else if (arg == "--root") {
            if (i + 1 >= argc) {
                std::cerr << "[api-policy] Error: Missing value for --root\n";
                return false;
            }
            opts.root = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: dgtl-api-policy [--root <path>] [--summary-only]\n";
            return false;
        } else {
            std::cerr << "[api-policy] Error: Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

// UPGRADED: Production-grade file I/O with size pre-allocation for maximum throughput
[[nodiscard]] std::string read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::in | std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return {};
    }
    
    const auto size = in.tellg();
    if (size <= 0) {
        return {};
    }
    
    std::string content;
    content.resize(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(content.data(), size);
    return content;
}

// UPGRADED: Safe const std::string& instead of string_view to prevent iterator UB.
// Added std::regex::optimize to handle large source files efficiently.
[[nodiscard]] bool regex_contains(const std::string& text, const std::string& pattern) {
    if (text.empty() || pattern.empty()) return false;
    try {
        const std::regex re(pattern, std::regex::ECMAScript | std::regex::optimize);
        return std::regex_search(text, re);
    } catch (const std::regex_error& e) {
        std::cerr << "[api-policy] Fatal Regex Error (" << e.what() << ") on pattern: " << pattern << "\n";
        return false;
    }
}

void add_missing(std::vector<std::string>& failures, const std::string& path) {
    failures.push_back("missing required file: " + path);
}

void add_contains_failure(std::vector<std::string>& failures,
                          const std::string& path,
                          const std::string& description) {
    failures.push_back("missing expected policy pattern '" + description + "' in " + path);
}

void add_not_contains_failure(std::vector<std::string>& failures,
                              const std::string& path,
                              const std::string& description) {
    failures.push_back("forbidden policy pattern '" + description + "' found in " + path);
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        return 1;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(opts.root, ec);
    if (ec || !fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "[api-policy] Invalid root directory: " << opts.root.string() << " (" << ec.message() << ")\n";
        return 1;
    }

    std::vector<std::string> failures;

    const std::vector<std::string> required_files = {
        "docs/api/PUBLIC_API_TIERS_AND_CONTRACT.md",
        "docs/api/PUBLIC_API_RELEASE_POLICY.md",
        "docs/api/FRONTEND_CONFORMANCE_KIT.md",
        "include/dgtl/api/Api_all.ixx",
        "include/dgtl_sdk/dgtl.h",
        "include/dgtl_sdk/dgtl_c_api.h",
        "include/dgtl_sdk/dgtl_core.h",
        "include/dgtl_sdk/dgtl_capabilities.h",
        "include/dgtl_sdk/dgtl_session.h",
        "include/dgtl/backend/driver/Backend_Driver_API.ixx",
        "include/dgtl/api/Runtime.ixx",
        "include/dgtl/runtime/TaskScheduler.ixx",
        "tests/functional/frontend/t07_frontend_pipeline_lowerer.cpp"
    };

    for (const auto& rel : required_files) {
        if (!fs::exists(root / rel)) {
            add_missing(failures, rel);
        }
    }

    // RULESET RETAINED EXACTLY AS REQUESTED
    const std::vector<Rule> rules = {
        {"include/dgtl/dgtl_all.ixx", "dgtl.ir_stream", "top-level umbrella must include public API umbrella", true},
        {"include/dgtl/api/Api_all.ixx", "dgtl/api/FrontendHelper\\.(hpp|ixx)", "public API umbrella must not expose removed frontend helper wrapper", false},
        {"include/dgtl/api/Api_all.ixx", "dgtl/api/Frontend_Payload\\.(hpp|ixx)", "public API umbrella must not expose removed frontend payload wrapper", false},
        {"include/dgtl/api/Api_all.ixx", "(:api_runtime|dgtl\\.internal\\.api_runtime)", "public API umbrella must expose runtime scheduler API", true},
        {"include/dgtl/api/Runtime.ixx", "dgtl.task_scheduler", "runtime API header must include TaskScheduler implementation header", true},
        
        {"include/dgtl/jit/JIT_Orchestrator.ixx", "dgtl/runtime/Runtime_Intrinsics\\.ixx", " orchestrator must depend on runtime intrinsic facade", true},
        {"include/dgtl/jit/JIT_Orchestrator.ixx", "Myzon_Bulletins\\.ixx", " orchestrator must not include legacy runtime bulletins directly", false},
        {"include/dgtl/core/ABI.ixx", "__myzon_", "core ABI contract must not hardcode legacy intrinsic symbols", false},

        {"include/dgtl/backend/driver/Backend_Driver_API.ixx", "frontend_payload_path", "backend driver must not carry frontend payload path anymore", false},
        {"include/dgtl/backend/driver/Backend_Driver_API.ixx", "emit_ir_stream\\(", "IRStream backend emission helper must exist", true},
        {"src/main.cpp", "--frontend-payload", "DGTL driver must not accept frontend payload CLI option anymore", false},
        {"src/main.cpp", "build_ir_from_frontend_payload\\(", "DGTL driver must not load IR from frontend payload anymore", false},

        {"include/dgtl_sdk/dgtl.h", "dgtl_capabilities\\.h", "public SDK umbrella must expose capability contract header", true},
        {"include/dgtl_sdk/dgtl.h", "dgtl_session\\.h", "public SDK umbrella must expose session contract header", true},
        {"tools/cpp/dgtl_main.cpp", "api-status", "main orchestrator must expose api-status command", true},
        {"tools/cpp/dgtl_orchestrator_main.cpp", "api-status", "secondary orchestrator must expose api-status command", true},

        {"tests/test_suite_main.cpp", "run_test_t07_frontend_pipeline_lowerer", "aggregate suite must include frontend pipeline lowerer test", true}
    };

    for (const auto& rule : rules) {
        const fs::path file = root / rule.relative_path;
        if (!fs::exists(file)) {
            failures.push_back("cannot validate pattern '" + rule.description + "' because file is missing: " + rule.relative_path);
            continue;
        }

        const std::string content = read_all(file);
        if (content.empty()) {
            failures.push_back("failed to read contents of: " + rule.relative_path);
            continue;
        }

        const bool matched = regex_contains(content, rule.pattern);
        
        if (rule.must_match && !matched) {
            add_contains_failure(failures, rule.relative_path, rule.description);
        } else if (!rule.must_match && matched) {
            add_not_contains_failure(failures, rule.relative_path, rule.description);
        }
    }

    if (!failures.empty()) {
        std::cout << "[api-policy] FAIL\n";
        for (const auto& f : failures) {
            std::cout << "  - " << f << "\n";
        }
        return 1;
    }

    if (!opts.summary_only) {
        std::cout << "[api-policy] PASS\n";
    }

    return 0;
}