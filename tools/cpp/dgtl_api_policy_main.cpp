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
    bool verbose{false};
    bool verify_mode{false};
    bool help_requested{false};
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--summary-only") {
            opts.summary_only = true;
        } else if (arg == "--verify") {
            opts.verify_mode = true;
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--root") {
            if (i + 1 >= argc) {
                std::cerr << "[api-policy] Error: Missing value for --root\n";
                return false;
            }
            opts.root = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            opts.help_requested = true;
            return true;
        } else {
            std::cerr << "[api-policy] Error: Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

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

    if (opts.help_requested) {
        std::cout << "Usage: dgtl-api-policy [--root <path>] [--summary-only] [--verify] [--verbose]\n";
        return 0;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(opts.root, ec);
    if (ec || !fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "[api-policy] Invalid root directory: " << opts.root.string() << " (" << ec.message() << ")\n";
        return 1;
    }

    std::vector<std::string> failures;

    // 1. Mandatory Core & Modular SDK Files
    const std::vector<std::string> required_files = {
        // Architecture & Policy Documents
        "docs/api/PUBLIC_API_TIERS_AND_CONTRACT.md",
        "docs/api/PUBLIC_API_RELEASE_POLICY.md",
        "docs/api/FRONTEND_CONFORMANCE_KIT.md",

        // Sovereign Modular Public C-ABI SDK Headers
        "include/dgtl_sdk/types.h",
        "include/dgtl_sdk/target.h",
        "include/dgtl_sdk/object.h",
        "include/dgtl_sdk/linker.h",
        "include/dgtl_sdk/jit.h",
        "include/dgtl_sdk/module.h",
        "include/dgtl_sdk/mir.h",
        "include/dgtl_sdk/dgtl.h",

        // Public SDK Bridge & Tools
        "src/sdk/dgtl_sdk_impl.cpp",
        "tools/cpp/dgtl_config_main.cpp",

        // Internal Compiler API Anchors
        "include/dgtl/api/Api_all.ixx",
        "include/dgtl/linker/driver/Backend_Driver_API.ixx",
        "include/dgtl/api/Runtime_API.ixx",
        "include/dgtl/runtime/TaskScheduler.ixx",
        "tests/functional/frontend/t07_frontend_pipeline_lowerer.cpp",

        // Sovereign Object Librarian & Native Linker Modules
        "include/dgtl/object/Archive.ixx",
        "include/dgtl/linker/pe/PE_Linker.ixx",
        "include/dgtl/linker/pe/PE_Sections.ixx",
        "src/apps/dgtl_ar_main.cpp",
        "src/apps/dgtl_link_main.cpp"
    };

    for (const auto& rel : required_files) {
        if (!fs::exists(root / rel)) {
            add_missing(failures, rel);
        } else if (opts.verbose) {
            std::cout << "[api-policy] [FILE OK] " << rel << "\n";
        }
    }

    std::vector<Rule> rules;

    // --- Tier 1: Internal API & Architecture Invariants ---
    rules.push_back(Rule{"include/dgtl/dgtl.ixx", "(dgtl\\.ir|dgtl\\.ir_stream)", "top-level umbrella must include public API umbrella", true});
    rules.push_back(Rule{"include/dgtl/api/Api_all.ixx", "dgtl/api/FrontendHelper\\.(hpp|ixx)", "public API umbrella must not expose removed frontend helper wrapper", false});
    rules.push_back(Rule{"include/dgtl/api/Api_all.ixx", "dgtl/api/Frontend_Payload\\.(hpp|ixx)", "public API umbrella must not expose removed frontend payload wrapper", false});
    rules.push_back(Rule{"include/dgtl/api/Api_all.ixx", "(:api_runtime|dgtl\\.internal\\.api_runtime|dgtl\\.api\\.runtime)", "public API umbrella must expose runtime scheduler API", true});
    rules.push_back(Rule{"include/dgtl/api/Runtime_API.ixx", "(dgtl\\.task_scheduler|dgtl\\.runtime\\.task_scheduler)", "runtime API header must include TaskScheduler implementation header", true});
    rules.push_back(Rule{"include/dgtl/jit/JIT_Orchestrator.ixx", "dgtl/runtime/Runtime_Intrinsics\\.ixx", "orchestrator must depend on runtime intrinsic facade", true});
    rules.push_back(Rule{"include/dgtl/jit/JIT_Orchestrator.ixx", "Myzon_Bulletins\\.ixx", "orchestrator must not include legacy runtime bulletins directly", false});
    rules.push_back(Rule{"include/dgtl/core/ABI.ixx", "__myzon_", "core ABI contract must not hardcode legacy intrinsic symbols", false});
    rules.push_back(Rule{"include/dgtl/linker/driver/Backend_Driver_API.ixx", "frontend_payload_path", "backend driver must not carry frontend payload path anymore", false});
    rules.push_back(Rule{"include/dgtl/linker/driver/Backend_Driver_API.ixx", "(emit_ir_stream|emit_machine_function)\\(", "Machine function/stream backend emission helper must exist", true});
    rules.push_back(Rule{"src/main.cpp", "--frontend-payload", "DGTL driver must not accept frontend payload CLI option anymore", false});
    rules.push_back(Rule{"src/main.cpp", "build_ir_from_frontend_payload\\(", "DGTL driver must not load IR from frontend payload anymore", false});

    // --- Tier 2: Sovereign Public C-ABI Modular Headers (No Leaked Internal Modules) ---
    const std::vector<std::string> public_sdk_headers = {
        "include/dgtl_sdk/types.h",
        "include/dgtl_sdk/target.h",
        "include/dgtl_sdk/object.h",
        "include/dgtl_sdk/linker.h",
        "include/dgtl_sdk/jit.h",
        "include/dgtl_sdk/module.h",
        "include/dgtl_sdk/mir.h",
        "include/dgtl_sdk/dgtl.h"
    };

    for (const auto& hdr : public_sdk_headers) {
        rules.push_back(Rule{hdr, "import dgtl\\.", "public C-ABI header must not leak internal C++ named module imports", false});
        rules.push_back(Rule{hdr, "namespace dgtl::(core|ir|mir|isa|codegen)", "public C-ABI header must not expose internal compiler namespaces", false});
        rules.push_back(Rule{hdr, "extern \"C\"", "public C-ABI header must provide extern \"C\" linkage guards", true});
    }

    // --- Tier 3: Master Public Umbrella Integration (dgtl.h) ---
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "types\\.h", "master umbrella must include types.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "target\\.h", "master umbrella must include target.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "object\\.h", "master umbrella must include object.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "linker\\.h", "master umbrella must include linker.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "jit\\.h", "master umbrella must include jit.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "module\\.h", "master umbrella must include module.h", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "mir\\.h", "master umbrella must include mir.h", true});

    // C++20 RAII Facade & Compatibility
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "class TargetMachine", "public SDK must expose C++ RAII TargetMachine facade", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "class ObjectFile", "public SDK must expose C++ RAII ObjectFile facade", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "class Linker", "public SDK must expose C++ RAII Linker facade", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "class JITEngine", "public SDK must expose C++ RAII JITEngine facade", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "dgtl_session_config_t", "public SDK must maintain backward compatibility for session config", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "dgtl_emit_call", "public SDK must expose dgtl_emit_call for frontend lowering", true});
    rules.push_back(Rule{"include/dgtl_sdk/dgtl.h", "dgtl_emit_function_params", "public SDK must expose dgtl_emit_function_params for frontend lowering", true});

    // --- Tier 4: Subsystem Contracts & Lifecycle ---
    rules.push_back(Rule{"include/dgtl_sdk/target.h", "dgtl_target_machine_create", "target subsystem must declare dgtl_target_machine_create", true});
    rules.push_back(Rule{"include/dgtl_sdk/target.h", "dgtl_target_machine_create_host", "target subsystem must declare dgtl_target_machine_create_host", true});
    rules.push_back(Rule{"include/dgtl_sdk/object.h", "dgtl_object_create", "object subsystem must declare dgtl_object_create", true});
    rules.push_back(Rule{"include/dgtl_sdk/object.h", "dgtl_object_add_section", "object subsystem must declare dgtl_object_add_section", true});
    rules.push_back(Rule{"include/dgtl_sdk/object.h", "dgtl_object_add_symbol", "object subsystem must declare dgtl_object_add_symbol", true});
    rules.push_back(Rule{"include/dgtl_sdk/object.h", "DGTL_OBJECT_FMT_PE", "object subsystem must define DGTL_OBJECT_FMT_PE format enum", true});
    rules.push_back(Rule{"include/dgtl_sdk/linker.h", "dgtl_linker_create", "linker subsystem must declare dgtl_linker_create", true});
    rules.push_back(Rule{"include/dgtl_sdk/linker.h", "dgtl_linker_link_to_file", "linker subsystem must declare dgtl_linker_link_to_file", true});
    rules.push_back(Rule{"include/dgtl_sdk/jit.h", "dgtl_jit_engine_create", "jit subsystem must declare dgtl_jit_engine_create", true});
    rules.push_back(Rule{"include/dgtl_sdk/jit.h", "dgtl_jit_engine_lookup", "jit subsystem must declare dgtl_jit_engine_lookup", true});
    rules.push_back(Rule{"include/dgtl_sdk/module.h", "dgtl_module_create", "module subsystem must declare dgtl_module_create", true});
    rules.push_back(Rule{"include/dgtl_sdk/mir.h", "dgtl_mir_", "mir subsystem must declare machine IR inspection functions", true});
    rules.push_back(Rule{"include/dgtl_sdk/types.h", "dgtl_err_t", "types header must define universal dgtl_err_t", true});

    // --- Tier 5: Discovery Tool & Orchestrator Integration ---
    rules.push_back(Rule{"tools/cpp/dgtl_config_main.cpp", "--version", "dgtl-config must support --version flag", true});
    rules.push_back(Rule{"tools/cpp/dgtl_config_main.cpp", "--cflags", "dgtl-config must support --cflags flag", true});
    rules.push_back(Rule{"tools/cpp/dgtl_config_main.cpp", "--ldflags", "dgtl-config must support --ldflags flag", true});
    rules.push_back(Rule{"tools/cpp/dgtl_config_main.cpp", "--libs", "dgtl-config must support --libs flag", true});
    rules.push_back(Rule{"tools/cpp/dgtl_config_main.cpp", "--components", "dgtl-config must support --components flag", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "sync_sdk_public_headers", "main orchestrator must synchronize modular public headers", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "dgtl-config", "main orchestrator must stage dgtl-config discovery tool", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "verify_public_api_artifacts", "main orchestrator must verify public API artifacts", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "api-status", "main orchestrator must expose api-status command", true});
    rules.push_back(Rule{"tools/cpp/dgtl_orchestrator_main.cpp", "api-status", "secondary orchestrator must expose api-status command", true});
    rules.push_back(Rule{"tests/functional/frontend/t07_frontend_pipeline_lowerer.cpp", "run_test_t07_frontend_pipeline_lowerer", "aggregate suite must include frontend pipeline lowerer test", true});

    // --- Tier 6: Sovereign Object Librarian & Native Linker Invariants ---
    rules.push_back(Rule{"include/dgtl/object/Archive.ixx", "parse_pe_binary", "sovereign archiver must implement PE executable container inspection", true});
    rules.push_back(Rule{"include/dgtl/linker/pe/PE_Linker.ixx", "ucrtbase\\.dll", "PE linker must provide ucrtbase.dll dynamic import mapping", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "dgtl\\.object\\.archive", "main orchestrator must bootstrap dgtl.object.archive module", true});
    rules.push_back(Rule{"tools/cpp/dgtl_main.cpp", "dgtl\\.linker\\.pe", "main orchestrator must bootstrap dgtl.linker.pe module", true});
    rules.push_back(Rule{"tools/cpp/dgtl_orchestrator_main.cpp", "dgtl\\.object\\.archive", "secondary orchestrator must bootstrap dgtl.object.archive module", true});
    rules.push_back(Rule{"tools/cpp/dgtl_orchestrator_main.cpp", "dgtl\\.linker\\.pe", "secondary orchestrator must bootstrap dgtl.linker.pe module", true});

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
        } else if (opts.verbose) {
            std::cout << "[api-policy] [RULE OK] " << rule.relative_path << " -> " << rule.description << "\n";
        }
    }

    if (!failures.empty()) {
        std::cout << "[api-policy] FAIL (" << failures.size() << " policy violations detected)\n";
        for (const auto& f : failures) {
            std::cout << "  - " << f << "\n";
        }
        return 1;
    }

    if (opts.summary_only) {
        std::cout << "[api-policy] PASS\n";
    } else {
        std::cout << "[api-policy] PASS (Verified " << required_files.size()
                  << " files and " << rules.size()
                  << " policy rules across Sovereign Public SDK and Internal tiers)\n";
    }

    return 0;
}
