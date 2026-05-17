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
// dgtl_main.cpp
// C++23/26 cross-platform replacement for dgtl.ps1 orchestration script
// Implements all orchestration, build, validation, and policy gates

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <cstdlib>
#include <map>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <optional>
#include <sstream>
#include <fstream>
#include <regex>
#include <ctime>

namespace fs = std::filesystem;
using namespace std::literals;

// Global configuration
struct Config {
    fs::path root;
    fs::path build_dir;
    fs::path tools_dir;
    fs::path include_dir;
    fs::path src_dir;
    std::string gpp_path;
};

// Global context
Config g_config;

// Security Utility: Ensures paths with spaces don't break shell commands
[[nodiscard]] std::string quote_path(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

// Utility functions
[[nodiscard]] int run_command(std::string_view cmd, bool verbose = true) {
    if (verbose) {
        std::cout << "[orchestrator] $ " << cmd << std::endl;
    }
    
    std::string exec_cmd(cmd);
#ifdef _WIN32
    // Fix for Windows cmd.exe /c quote stripping behavior:
    // When multiple quotes exist in the string, cmd strips the first and last quote.
    // Wrapping the entire command block in outer quotes ensures paths with spaces parse correctly.
    exec_cmd = "\"" + exec_cmd + "\"";
#endif

    return std::system(exec_cmd.c_str());
}

[[nodiscard]] bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec;
}

// Fixed: Native file_time_type prevents cross-filesystem epoch crashes
[[nodiscard]] auto get_file_mtime(const fs::path& p) -> fs::file_time_type {
    if (!file_exists(p)) return fs::file_time_type::min();
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : ftime;
}

[[nodiscard]] auto latest_mtime(const std::vector<fs::path>& paths) -> fs::file_time_type {
    auto latest = fs::file_time_type::min();
    std::error_code ec;
    for (const auto& p : paths) {
        if (!file_exists(p)) continue;
        if (fs::is_directory(p, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
                if (entry.is_regular_file(ec)) {
                    latest = std::max(latest, get_file_mtime(entry.path()));
                }
            }
        } else {
            latest = std::max(latest, get_file_mtime(p));
        }
    }
    return latest;
}

void ensure_directory(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        fs::create_directories(p, ec);
    }
}

void touch_file(const fs::path& p) {
    ensure_directory(p.parent_path());
    std::ofstream f(p, std::ios::app);
    f.close();
}

int sync_sdk_public_headers() {
    const fs::path sdk_src_dir = g_config.include_dir / "dgtl_sdk";
    const fs::path dgtl_src_dir = g_config.include_dir / "dgtl";
    const fs::path sdk_include_root = g_config.build_dir / "DGTL_SDK" / "include";
    const fs::path sdk_include_ns = sdk_include_root / "dgtl_sdk";
    const fs::path sdk_utils_root = sdk_include_root / "dgtl" / "utils";

    const std::vector<std::string> sdk_headers = {
        "dgtl_c_api.h",
        "dgtl_core.h",
        "dgtl_simd.h",
        "dgtl.h"
    };

    ensure_directory(sdk_include_root);
    ensure_directory(sdk_include_ns);
    ensure_directory(sdk_utils_root / "container");
    ensure_directory(sdk_utils_root / "hashing");

    for (const auto& header : sdk_headers) {
        const fs::path src = sdk_src_dir / header;
        if (!file_exists(src)) {
            std::cerr << "[sdk] Missing public header: " << src.string() << std::endl;
            return 1;
        }

        std::error_code ec_root;
        fs::copy_file(src, sdk_include_root / header, fs::copy_options::overwrite_existing, ec_root);
        if (ec_root) {
            std::cerr << "[sdk] Failed to copy header to SDK include root: "
                      << (sdk_include_root / header).string() << " (" << ec_root.message() << ")" << std::endl;
            return 1;
        }

        std::error_code ec_ns;
        fs::copy_file(src, sdk_include_ns / header, fs::copy_options::overwrite_existing, ec_ns);
        if (ec_ns) {
            std::cerr << "[sdk] Failed to copy header to SDK include namespace: "
                      << (sdk_include_ns / header).string() << " (" << ec_ns.message() << ")" << std::endl;
            return 1;
        }
    }

    const std::vector<std::pair<fs::path, fs::path>> sdk_compat_headers = {
        {dgtl_src_dir / "utils" / "container" / "Flat_Map.hpp", sdk_utils_root / "container" / "Flat_Map.hpp"},
        {dgtl_src_dir / "utils" / "hashing" / "Hashing.hpp", sdk_utils_root / "hashing" / "Hashing.hpp"}
    };

    for (const auto& [src, dst] : sdk_compat_headers) {
        if (!file_exists(src)) {
            std::cerr << "[sdk] Missing compatibility header: " << src.string() << std::endl;
            return 1;
        }
        std::error_code ec_copy;
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec_copy);
        if (ec_copy) {
            std::cerr << "[sdk] Failed to copy compatibility header: "
                      << dst.string() << " (" << ec_copy.message() << ")" << std::endl;
            return 1;
        }
    }

    std::cout << "[sdk] Synced public headers to " << sdk_include_root.string() << std::endl;
    return 0;
}

// Tool compilation helpers
int compile_tool(std::string_view tool_name, const fs::path& source, const fs::path& output) {
    ensure_directory(output.parent_path());
    
    bool needs_build = true;
    if (file_exists(output)) {
        auto exe_time = get_file_mtime(output);
        auto src_time = get_file_mtime(source);
        if (src_time <= exe_time) {
            needs_build = false;
        }
    }

    if (!needs_build) {
        std::cout << "[build] Reusing up-to-date " << output.filename().string() << std::endl;
        return 0;
    }

    std::cout << "[build] Compiling " << tool_name << std::endl;
    std::string cmd = std::string(g_config.gpp_path) + " -std=c++26 -O2 -pipe " +
                      quote_path(source) + " -o " + quote_path(output);
    int ec = run_command(cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << output.string() << std::endl;
    }
    return ec;
}

// Tool invocation with auto-compilation
int invoke_tool(std::string_view tool_name, const fs::path& source, const fs::path& output, 
                const std::vector<std::string>& args = {}) {
    int ec = compile_tool(tool_name, source, output);
    if (ec != 0) return ec;

    std::string cmd = quote_path(output);
    for (const auto& arg : args) {
        cmd += " " + arg;
    }
    return run_command(cmd);
}

// Policy gates
int invoke_header_compliance() {
    auto header_tool = g_config.build_dir / "tools" / "dgtl-header-check.exe";
    auto header_source = g_config.tools_dir / "dgtl_header_check_main.cpp";
    return invoke_tool("dgtl-header-check", header_source, header_tool, 
                      {"--root", quote_path(g_config.root), "--summary-only", "--relative-paths"});
}

int invoke_api_policy() {
    auto policy_tool = g_config.build_dir / "tools" / "dgtl-api-policy.exe";
    auto policy_source = g_config.tools_dir / "dgtl_api_policy_main.cpp";
    return invoke_tool("dgtl-api-policy", policy_source, policy_tool,
                      {"--root", quote_path(g_config.root), "--summary-only"});
}

int invoke_duplication_gate() {
    auto dup_tool = g_config.build_dir / "tools" / "dgtl-dup-gate.exe";
    auto dup_source = g_config.tools_dir / "dgtl_duplication_gate_main.cpp";
    return invoke_tool("dgtl-duplication-gate", dup_source, dup_tool,
                      {"--root", quote_path(g_config.root), "--summary-only"});
}

int invoke_dgtl_gen(const std::vector<std::string>& args = {}) {
    auto gen_tool = g_config.build_dir / "tools" / "dgtl-gen.exe";
    auto gen_source = g_config.tools_dir / "dgtl_gen_main.cpp";
    return invoke_tool("dgtl-gen", gen_source, gen_tool, args);
}

int invoke_dgtl_gen_validate(const std::vector<std::string>& args = {}) {
    auto validate_tool = g_config.build_dir / "tools" / "dgtl-gen-validate.exe";
    auto validate_source = g_config.tools_dir / "validate_dgtl_gen_main.cpp";
    return invoke_tool("dgtl-gen-validate", validate_source, validate_tool, args);
}

// Binary building
int build_binary(std::string_view binary_name, const fs::path& output, const fs::path& entry_point,
                 const std::vector<fs::path>& exclude_paths = {}, bool strict = false) {
    ensure_directory(output.parent_path());

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -O2 -pipe -I" + quote_path(g_config.include_dir);

    if (strict) {
        cmd += " -Wall -Wextra -Wpedantic -Werror";
    }

    // Normalize excludes and always exclude the chosen entrypoint so it is not compiled twice.
    std::vector<fs::path> normalized_excludes;
    normalized_excludes.reserve(exclude_paths.size() + 1);
    normalized_excludes.push_back(fs::absolute(entry_point).lexically_normal());
    for (const auto& exc : exclude_paths) {
        normalized_excludes.push_back(fs::absolute(exc).lexically_normal());
    }

    std::error_code ec_dir;
    // Collect all source files from src/ except excluded ones.
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") continue;

        const fs::path candidate = fs::absolute(entry.path()).lexically_normal();
        bool excluded = false;
        for (const auto& exc : normalized_excludes) {
            if (candidate == exc) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        cmd += " " + quote_path(entry.path());
    }

    // Add entry point
    cmd += " " + quote_path(entry_point) + " -o " + quote_path(output);

    std::cout << "[build] Compiling " << binary_name << std::endl;
    int ec = run_command(cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << output.string() << std::endl;
    }
    return ec;
}

int ensure_binary(std::string_view binary_name, const fs::path& output, const fs::path& entry_point,
                  const std::vector<fs::path>& exclude_paths = {}, bool strict = false) {
    if (!file_exists(output)) {
        return build_binary(binary_name, output, entry_point, exclude_paths, strict);
    }

    auto exe_time = get_file_mtime(output);
    auto src_time = latest_mtime({g_config.src_dir, g_config.include_dir});

    if (src_time > exe_time) {
        std::cout << "[build] Source changed - rebuilding " << binary_name << std::endl;
        return build_binary(binary_name, output, entry_point, exclude_paths, strict);
    }

    std::cout << "[build] Reusing up-to-date " << binary_name << std::endl;
    return 0;
}

void append_core_sources_for_test_build(std::string& cmd) {
    const std::vector<fs::path> excluded_src = {
        fs::absolute(g_config.src_dir / "main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal()
    };

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") {
            continue;
        }

        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        bool skip = false;
        for (const auto& ex : excluded_src) {
            if (abs == ex) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            cmd += " " + quote_path(entry.path());
        }
    }
}

int build_test_suite_binary(const fs::path& output,
                           const fs::path& entry_point,
                           const std::vector<fs::path>& test_sources) {
    ensure_directory(output.parent_path());

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -O2 -pipe -I" + quote_path(g_config.include_dir);
    append_core_sources_for_test_build(cmd);

    for (const auto& src : test_sources) {
        cmd += " " + quote_path(src);
    }
    cmd += " " + quote_path(entry_point) + " -o " + quote_path(output);

    std::cout << "[build] Compiling dgtl_tests" << std::endl;
    int ec = run_command(cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << output.string() << std::endl;
    }
    return ec;
}

int ensure_test_suite_binary(const fs::path& output,
                             const fs::path& entry_point,
                             const std::vector<fs::path>& test_sources) {
    std::vector<fs::path> deps = {
        g_config.src_dir,
        g_config.include_dir,
        entry_point
    };
    for (const auto& src : test_sources) {
        deps.push_back(src);
    }

    if (!file_exists(output)) {
        return build_test_suite_binary(output, entry_point, test_sources);
    }

    const auto exe_time = get_file_mtime(output);
    const auto src_time = latest_mtime(deps);
    if (src_time > exe_time) {
        std::cout << "[build] Source changed - rebuilding dgtl_tests" << std::endl;
        return build_test_suite_binary(output, entry_point, test_sources);
    }

    std::cout << "[build] Reusing up-to-date dgtl_tests" << std::endl;
    return 0;
}

// Fixed: Memory optimized line-by-line reading instead of full file buffer load
[[nodiscard]] bool source_has_main(const fs::path& source) {
    std::ifstream in(source);
    if (!in) return false;
    
    std::string line;
    static const std::regex kMainRegex(R"(\bint\s+main\s*\()", std::regex::optimize);
    while (std::getline(in, line)) {
        if (line.find("main") != std::string::npos && std::regex_search(line, kMainRegex)) {
            return true;
        }
    }
    return false;
}

int build_standalone_test_binary(const fs::path& output, const fs::path& entry_point) {
    ensure_directory(output.parent_path());

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -O2 -pipe -I" + quote_path(g_config.include_dir);
    append_core_sources_for_test_build(cmd);

    cmd += " " + quote_path(entry_point) + " -o " + quote_path(output);

    std::cout << "[build] Compiling " << output.filename().string() << std::endl;
    int ec = run_command(cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << output.string() << std::endl;
    }
    return ec;
}

int ensure_standalone_test_binary(const fs::path& output, const fs::path& entry_point) {
    std::vector<fs::path> deps = {g_config.src_dir, g_config.include_dir, entry_point};

    if (!file_exists(output)) {
        return build_standalone_test_binary(output, entry_point);
    }

    const auto exe_time = get_file_mtime(output);
    const auto src_time = latest_mtime(deps);
    if (src_time > exe_time) {
        std::cout << "[build] Source changed - rebuilding " << output.filename().string() << std::endl;
        return build_standalone_test_binary(output, entry_point);
    }

    std::cout << "[build] Reusing up-to-date " << output.filename().string() << std::endl;
    return 0;
}

[[nodiscard]] fs::path make_obj_path(const fs::path& obj_dir, const fs::path& src_path) {
    fs::path rel = fs::relative(src_path, g_config.src_dir).lexically_normal();
    std::string stem = rel.string();
    std::replace(stem.begin(), stem.end(), '\\', '_');
    std::replace(stem.begin(), stem.end(), '/', '_');
    if (stem.size() >= 4 && stem.substr(stem.size() - 4) == ".cpp") {
        stem = stem.substr(0, stem.size() - 4);
    }
    return obj_dir / (stem + ".o");
}

int compile_object_if_needed(const fs::path& src, const fs::path& obj, bool strict = false) {
    const auto src_time = get_file_mtime(src);
    const auto include_time = latest_mtime({g_config.include_dir});
    const auto obj_time = get_file_mtime(obj);

    const bool needs_compile = !file_exists(obj) || src_time > obj_time || include_time > obj_time;
    if (!needs_compile) {
        return 0;
    }

    ensure_directory(obj.parent_path());
    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -O2 -pipe -I" + quote_path(g_config.include_dir);
    if (strict) {
        cmd += " -Wall -Wextra -Wpedantic -Werror";
    }
    cmd += " -c " + quote_path(src) + " -o " + quote_path(obj);
    return run_command(cmd, false); // Silence raw object commands for cleaner output
}

int build_tools_optimized(const fs::path& prod_main,
                          const fs::path& smoke_main,
                          const fs::path& opt_main,
                          const fs::path& llc_main,
                          const fs::path& verify_main,
                          const fs::path& opt_exe,
                          const fs::path& llc_exe,
                          const fs::path& verify_exe) {
    const fs::path obj_dir = g_config.build_dir / "obj" / "build-tools";
    ensure_directory(obj_dir);

    std::vector<fs::path> excluded = {
        fs::absolute(prod_main).lexically_normal(),
        fs::absolute(smoke_main).lexically_normal(),
        fs::absolute(opt_main).lexically_normal(),
        fs::absolute(llc_main).lexically_normal(),
        fs::absolute(verify_main).lexically_normal()
    };

    std::vector<fs::path> common_sources;
    std::error_code ec_dir;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") {
            continue;
        }

        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        bool skip = false;
        for (const auto& ex : excluded) {
            if (abs == ex) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            common_sources.push_back(entry.path());
        }
    }
    std::sort(common_sources.begin(), common_sources.end());

    std::vector<fs::path> common_objs;
    common_objs.reserve(common_sources.size());
    int compiled_count = 0;
    for (const auto& src : common_sources) {
        const fs::path obj = make_obj_path(obj_dir, src);
        const auto before = get_file_mtime(obj);
        int ec = compile_object_if_needed(src, obj, false);
        if (ec != 0) {
            return ec;
        }
        if (get_file_mtime(obj) > before) {
            ++compiled_count;
        }
        common_objs.push_back(obj);
    }
    std::cout << "[build-tools] Shared objects: " << compiled_count
              << " compiled, " << (common_objs.size() - compiled_count)
              << " reused" << std::endl;

    auto link_tool = [&](std::string_view name, const fs::path& entry_main, const fs::path& output) -> int {
        const fs::path entry_obj = make_obj_path(obj_dir, entry_main);
        int ec = compile_object_if_needed(entry_main, entry_obj, false);
        if (ec != 0) {
            return ec;
        }

        bool needs_link = !file_exists(output);
        const auto output_time = get_file_mtime(output);
        if (!needs_link) {
            if (get_file_mtime(entry_obj) > output_time) {
                needs_link = true;
            } else {
                for (const auto& obj : common_objs) {
                    if (get_file_mtime(obj) > output_time) {
                        needs_link = true;
                        break;
                    }
                }
            }
        }

        if (!needs_link) {
            std::cout << "[build] Reusing up-to-date " << name << std::endl;
            return 0;
        }

        std::string cmd = g_config.gpp_path;
        cmd += " -std=c++26 -O2 -pipe";
        for (const auto& obj : common_objs) {
            cmd += " " + quote_path(obj);
        }
        cmd += " " + quote_path(entry_obj) + " -o " + quote_path(output);

        std::cout << "[build] Linking " << name << std::endl;
        ec = run_command(cmd);
        if (ec == 0) {
            std::cout << "[build] Wrote " << output.string() << std::endl;
        }
        return ec;
    };

    int ec = 0;
    ec |= link_tool("dgtl-opt", opt_main, opt_exe);
    if (ec != 0) return ec;
    ec |= link_tool("dgtl-llc", llc_main, llc_exe);
    if (ec != 0) return ec;
    ec |= link_tool("dgtl-verify", verify_main, verify_exe);
    return ec;
}

int build_sdk_static_library() {
    const fs::path sdk_obj_dir = g_config.build_dir / "sdk_obj";
    const fs::path sdk_lib_dir = g_config.build_dir / "DGTL_SDK" / "lib" / "x64";
    const fs::path sdk_lib = sdk_lib_dir / "dgtl.lib";

    if (sync_sdk_public_headers() != 0) {
        return 1;
    }

    ensure_directory(sdk_obj_dir);
    ensure_directory(sdk_lib_dir);

    const fs::path prod_main = fs::absolute(g_config.src_dir / "main.cpp").lexically_normal();
    const fs::path smoke_main = fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal();
    const fs::path opt_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal();
    const fs::path llc_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal();
    const fs::path verify_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal();

    const std::vector<fs::path> excluded = {prod_main, smoke_main, opt_main, llc_main, verify_main};

    std::vector<fs::path> sdk_sources;
    std::error_code ec_dir;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") {
            continue;
        }

        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        bool skip = false;
        for (const auto& ex : excluded) {
            if (abs == ex) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            sdk_sources.push_back(entry.path());
        }
    }
    std::sort(sdk_sources.begin(), sdk_sources.end());

    std::vector<fs::path> sdk_objs;
    sdk_objs.reserve(sdk_sources.size());
    for (const auto& src : sdk_sources) {
        const fs::path obj = make_obj_path(sdk_obj_dir, src);
        int ec = compile_object_if_needed(src, obj, false);
        if (ec != 0) {
            return ec;
        }
        sdk_objs.push_back(obj);
    }

    bool needs_archive = !file_exists(sdk_lib);
    const auto lib_time = get_file_mtime(sdk_lib);
    if (!needs_archive) {
        for (const auto& obj : sdk_objs) {
            if (get_file_mtime(obj) > lib_time) {
                needs_archive = true;
                break;
            }
        }
    }

    if (!needs_archive) {
        std::cout << "[build] Reusing up-to-date " << sdk_lib.string() << std::endl;
        return 0;
    }

    std::string cmd = "ar rcs " + quote_path(sdk_lib);
    for (const auto& obj : sdk_objs) {
        cmd += " " + quote_path(obj);
    }

    std::cout << "[build] Archiving dgtl.lib" << std::endl;
    int ec = run_command(cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << sdk_lib.string() << std::endl;
    }
    return ec;
}

// Command handlers
void show_usage() {
    std::cout << R"(
================================================================================
                         DGTL C++ Build Orchestrator
                        Modern Cross-Platform Compilation
================================================================================

USAGE:
  dgtl <command> [args...]

QUICK START:
  dgtl build              Build production binary (optimized release)
  dgtl test               Run functional test suite
  dgtl header-check       Verify header compliance gates
  dgtl gen-build          Regenerate IR definitions and rebuild
  dgtl package            Create SDK distribution archive

================================================================================
BUILD COMMANDS:
================================================================================
  build, build-release    Build production binary -> build/dgtl_release.exe
  build-smoke             Build smoke/conformance test binary
  build-strict            Build with strict validation (Wall, Wextra, Werror)
  build-tools             Compile optimizer tools (dgtl-opt, dgtl-llc, dgtl-verify)
  build-verify            Compile verification tool (dgtl-verify)
  clean, clean-all        Remove all build outputs and logs
  crun                    Full cycle: clean -> build -> run conformance tests

================================================================================
EXECUTION COMMANDS:
================================================================================
  run, run-all            Run full conformance suite (all backends)
  run-backend <name>      Run single backend verification
                          Options: arm64, x64, nasm, wasm, arm32, riscv64,
                                   cuda, rocm, macho, elfobj, staticlib,
                                   coffobj, cofflib
  bench-backend <name>    Benchmark backend performance
                          Usage: dgtl bench-backend x64 [profile] [pressure]
    brun                  Build and run performance benchmark suite
                                                    Retained sources: benchmark_full_production_stress.cpp and benchmark_hardest_e2e_gate.cpp
    test                  Execute the primary relevant test suite
  srun                    Strict conformance test (build + run)

================================================================================
CODE GENERATION & VALIDATION:
================================================================================
  gen                     Regenerate DGTL-Gen output from IR definitions
  gen-validate            Validate DGTL-Gen output integrity
  gen-build               Full cycle: gen -> validate -> build
  header-check            Verify proprietary header compliance gates
  update-golden [strict]  Update baseline expectations for golden outputs

================================================================================
BACKEND & DISTRIBUTION:
================================================================================
  compile, compile-all    Compile frontend payloads to all 13 backends
                          Output directory: build/out/
  package                 Create SDK distribution package (DGTL_v*.zip)
                          Includes: headers, static library, binaries

================================================================================
COMMAND ALIASES (Backward Compatibility):
================================================================================
  build-release           -> build
  build-smoke             -> smoke-build
  build-strict            -> strict
  run-all                 -> run
  run-backend <name>      -> run <name>
  bench-backend <name>    -> bench <name>
  compile-all             -> compile
  clean-all               -> clean
  clean-build-and-run     -> crun
  run-strict-conformance  -> srun

================================================================================
POLYHEDRAL PARALLELIZATION FLAGS (P0 Features):
================================================================================
Usage with any command:
  dgtl run --polyhedral-mode <mode> --polyhedral-tile <n> --polyhedral-parallel <policy>

Options:
  --polyhedral-mode <off|safe|aggressive>
    off             Disable polyhedral analysis (default)
    safe            Enable with conservative transform policy
    aggressive      Enable aggressive loop tiling and fusion

  --polyhedral-tile <n>
    0               Auto-tune tile size (recommended)
    8..256          Fixed tile size (power of 2)

  --polyhedral-parallel <auto|off|on>
    auto            Auto-detect parallelization opportunity
    off             Disable parallel lowering
    on              Force parallel code generation

Example: dgtl run --polyhedral-mode safe --polyhedral-tile 32 --polyhedral-parallel auto

================================================================================
ENVIRONMENT & CONFIGURATION:
================================================================================
  Root Directory:         Auto-detected from current location
  Build Directory:        ./build/
  Source Directory:       ./src/
  Include Directory:      ./include/
  Tools Directory:        ./tools/cpp/
  Compiler:              GCC g++ (auto-detected from toolchain)
                         Fallback: C:\msys64\ucrt64\bin\g++.exe (MSYS2 UCRT64)

Configuration Search Order:
  1. .vscode/c_cpp_properties.json (workspace config)
  2. MSYS2 UCRT64 installation
  3. PATH environment variable

================================================================================
USAGE EXAMPLES:
================================================================================

  Full Release Build Cycle:
    dgtl clean              # Clean old artifacts
    dgtl build              # Compile production binary
    dgtl test               # Run primary test suite
    dgtl run-all            # Full conformance validation
    dgtl package            # Create distribution archive

  Development Workflow:
    dgtl gen-build          # IR generation + validation + build
    dgtl run-backend x64    # Quick x64 backend validation
    dgtl brun               # Performance benchmark on the two retained benchmark suites

  Pre-Release Heavy Gates (Recommended Order):
    1. dgtl clean-all
    2. dgtl build
    3. dgtl test
    4. dgtl run-all
    5. dgtl build-strict
    6. dgtl srun
    7. dgtl header-check
    8. dgtl brun
    9. dgtl package

  Backend-Specific:
    dgtl bench-backend x64 fusion         # Benchmark fusion profile
    dgtl bench-backend arm64 simd pressure 64  # SIMD profile with pressure
    dgtl run-backend wasm --polyhedral-mode safe  # WASM with polyhedral

================================================================================
EXIT CODES:
================================================================================
  0       Success
  1       Command failed (compilation, validation, or runtime error)
  Exit code propagated from subcommands for diagnostic purposes.

================================================================================

For detailed documentation, see: docs/ or ./README.md
Report issues at: https://github.com/TataG/DGTL

)" << std::endl;
}

int handle_build() {
    if (invoke_api_policy() != 0) return 1;
    if (invoke_duplication_gate() != 0) return 1;
    
    auto prod_main = g_config.src_dir / "main.cpp";
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    
    auto prod_exe = g_config.build_dir / "dgtl_release.exe";
    std::vector<fs::path> exclude = {smoke_main, opt_main, llc_main, verify_main};
    
    int ec = ensure_binary("dgtl_release", prod_exe, prod_main, exclude);
    if (ec != 0) return ec;
    return build_sdk_static_library();
}

int handle_build_tools() {
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto prod_main = g_config.src_dir / "main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    
    auto opt_exe = g_config.build_dir / "dgtl-opt.exe";
    auto llc_exe = g_config.build_dir / "dgtl-llc.exe";
    auto verify_exe = g_config.build_dir / "dgtl-verify.exe";
    
    int ec = build_tools_optimized(prod_main, smoke_main, opt_main, llc_main, verify_main,
                                   opt_exe, llc_exe, verify_exe);
    if (ec != 0) return ec;
    return build_sdk_static_library();
}

int handle_smoke_build() {
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto prod_main = g_config.src_dir / "main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    
    auto smoke_exe = g_config.build_dir / "dgtl_main.exe";
    return ensure_binary("dgtl_main", smoke_exe, smoke_main, {prod_main, opt_main, llc_main, verify_main});
}

int handle_strict() {
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto prod_main = g_config.src_dir / "main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    
    auto strict_exe = g_config.build_dir / "dgtl_main_strict.exe";
    return ensure_binary("dgtl_main_strict", strict_exe, smoke_main, {prod_main, opt_main, llc_main, verify_main}, true);
}

int handle_test() {
    if (invoke_api_policy() != 0) return 1;
    if (invoke_duplication_gate() != 0) return 1;
    if (invoke_header_compliance() != 0) return 1;
    if (sync_sdk_public_headers() != 0) return 1;

    const fs::path tests_root = g_config.root / "tests";
    const fs::path test_exe = g_config.build_dir / "dgtl_tests.exe";
    const fs::path test_success_stamp = g_config.build_dir / "tests" / "dgtl_test.success";
    const fs::path test_main = tests_root / "test_suite_main.cpp";
    const std::vector<fs::path> excluded_brun_tests = {
        fs::absolute(tests_root / "performance" / "benchmarks" / "benchmark_full_production_stress.cpp").lexically_normal(),
        fs::absolute(tests_root / "performance" / "benchmarks" / "benchmark_hardest_e2e_gate.cpp").lexically_normal()
    };

    const std::vector<fs::path> aggregate_test_sources = {
        g_config.root / "tests" / "t01_ir_stream_correctness.cpp",
        g_config.root / "tests" / "t02_ir_transaction_rollback.cpp",
        g_config.root / "tests" / "t03_optimizer_gvn_const_fold.cpp",
        g_config.root / "tests" / "t04_optimizer_fusion.cpp",
        g_config.root / "tests" / "t05_optimizer_volatile_guard.cpp",
        g_config.root / "tests" / "t06_codegen_arm64_emission.cpp",
        g_config.root / "tests" / "t07_frontend_pipeline_lowerer.cpp",
        g_config.root / "tests" / "t08_sdk_c_api_roundtrip.cpp",
        g_config.root / "tests" / "t09_runtime_mt_arena_isolation.cpp",
        g_config.root / "tests" / "t10_optimizer_strike_throughput.cpp",
        g_config.root / "tests" / "t11_pipeline_production_benchmark.cpp",
        g_config.root / "tests" / "t12_ir_cfg.cpp"
    };

    for (const auto& src : aggregate_test_sources) {
        if (!file_exists(src)) {
            std::cerr << "[test] Missing test source: " << src.string() << std::endl;
            return 1;
        }
    }
    if (!file_exists(test_main)) {
        std::cerr << "[test] Missing aggregate test entry: " << test_main.string() << std::endl;
        return 1;
    }

    std::vector<fs::path> standalone_tests;
    std::error_code ec_dir;
    for (const auto& entry : fs::recursive_directory_iterator(tests_root, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") {
            continue;
        }

        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        if (abs == fs::absolute(test_main).lexically_normal()) {
            continue;
        }

        bool excluded = false;
        for (const auto& ex : excluded_brun_tests) {
            if (abs == ex) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            continue;
        }

        if (source_has_main(abs)) {
            standalone_tests.push_back(entry.path());
        }
    }

    std::sort(standalone_tests.begin(), standalone_tests.end());

    int ec = ensure_test_suite_binary(test_exe, test_main, aggregate_test_sources);
    if (ec != 0) {
        return ec;
    }

    std::vector<fs::path> standalone_exes;
    standalone_exes.reserve(standalone_tests.size());
    for (const auto& src : standalone_tests) {
        const fs::path out = g_config.build_dir / "tests" / (src.stem().string() + ".exe");
        ec = ensure_standalone_test_binary(out, src);
        if (ec != 0) {
            return ec;
        }
        standalone_exes.push_back(out);
    }

    std::vector<fs::path> test_run_deps = {g_config.src_dir, g_config.include_dir, test_main, test_exe};
    for (const auto& src : aggregate_test_sources) {
        test_run_deps.push_back(src);
    }
    for (const auto& src : standalone_tests) {
        test_run_deps.push_back(src);
    }
    for (const auto& exe : standalone_exes) {
        test_run_deps.push_back(exe);
    }

    const auto deps_time = latest_mtime(test_run_deps);
    if (file_exists(test_success_stamp) && get_file_mtime(test_success_stamp) >= deps_time) {
        std::cout << "[test] Reusing last successful test run (no source changes detected)" << std::endl;
        return 0;
    }

    std::cout << "[test] Running aggregate C++ suite" << std::endl;
    ec = run_command(quote_path(test_exe));
    if (ec != 0) {
        return ec;
    }

    for (const auto& src : standalone_tests) {
        const fs::path out = g_config.build_dir / "tests" / (src.stem().string() + ".exe");
        std::cout << "[test] Running standalone " << src.filename().string() << std::endl;
        ec = run_command(quote_path(out));
        if (ec != 0) {
            return ec;
        }
    }

    touch_file(test_success_stamp);

    return 0;
}

int handle_gen(const std::vector<std::string>& args = {}) {
    return invoke_dgtl_gen(args);
}

int handle_gen_validate(const std::vector<std::string>& args = {}) {
    return invoke_dgtl_gen_validate(args);
}

int handle_gen_build() {
    if (invoke_dgtl_gen() != 0) return 1;
    if (invoke_dgtl_gen_validate() != 0) return 1;
    if (invoke_api_policy() != 0) return 1;
    if (invoke_duplication_gate() != 0) return 1;
    return handle_build();
}

int handle_header_check() {
    return invoke_header_compliance();
}

int handle_clean() {
    std::cout << "[clean] Removing build artifacts" << std::endl;
    std::vector<fs::path> cleanup_paths = {
        g_config.build_dir / "dgtl_release.exe",
        g_config.build_dir / "dgtl_main.exe",
        g_config.build_dir / "dgtl_main_strict.exe",
        g_config.build_dir / "dgtl_tests.exe",
        g_config.build_dir / "dgtl_tests_full.exe",
    };
    std::error_code ec;
    for (const auto& p : cleanup_paths) {
        if (fs::exists(p, ec)) {
            fs::remove(p, ec);
            std::cout << "[clean] Removed " << p.string() << std::endl;
        }
    }
    return 0;
}

[[nodiscard]] std::string detect_sdk_version() {
    const fs::path sdk_header = g_config.include_dir / "dgtl_sdk" / "dgtl.h";
    std::ifstream in(sdk_header);
    if (!in) {
        return "unknown";
    }

    std::string line;
    const std::regex version_re(R"(#define\s+DGTL_VERSION_STRING\s+\"([^\"]+)\")", std::regex::optimize);
    std::smatch match;
    while (std::getline(in, line)) {
        if (std::regex_search(line, match, version_re) && match.size() > 1) {
            return match[1].str();
        }
    }
    return "unknown";
}

bool copy_tree_if_exists(const fs::path& src, const fs::path& dst) {
    if (!file_exists(src)) {
        return false;
    }

    std::error_code ec;
    ensure_directory(dst.parent_path());
    fs::copy(src, dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    return !ec;
}

bool copy_file_if_exists(const fs::path& src, const fs::path& dst) {
    if (!file_exists(src)) {
        return false;
    }

    std::error_code ec;
    ensure_directory(dst.parent_path());
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

int verify_package_archive(const fs::path& package_zip, const std::string& package_dir_name) {
    if (!file_exists(package_zip)) {
        std::cerr << "[package] Missing archive for verification: " << package_zip.string() << std::endl;
        return 1;
    }

    const fs::path listing_file = g_config.build_dir / "package" / (package_dir_name + "_listing.txt");
    const std::string list_cmd = "tar -tf " + quote_path(package_zip) + " > " + quote_path(listing_file);
    if (run_command(list_cmd) != 0) {
        std::cerr << "[package] Failed to list archive contents for verification" << std::endl;
        return 1;
    }

    std::ifstream in(listing_file);
    if (!in) {
        std::cerr << "[package] Failed to read archive listing: " << listing_file.string() << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string listing = buffer.str();

    const std::vector<std::string> required_entries = {
        package_dir_name + "/",
        package_dir_name + "/include/",
        package_dir_name + "/lib/x64/dgtl.lib",
        package_dir_name + "/bin/",
        package_dir_name + "/README.md",
        package_dir_name + "/LICENSE",
        package_dir_name + "/PACKAGE_MANIFEST.txt"
    };

    for (const auto& entry : required_entries) {
        if (listing.find(entry) == std::string::npos) {
            std::cerr << "[package] Verification failed: missing entry in archive: " << entry << std::endl;
            return 1;
        }
    }

    std::cout << "[package] Verify OK: archive contains required SDK entries" << std::endl;
    return 0;
}

int handle_package(bool verify_mode = false) {
    if (handle_build() != 0) return 1;
    if (handle_build_tools() != 0) return 1;

    const std::string version = detect_sdk_version();
    const std::string package_dir_name = "DGTL_v" + version;

    const fs::path stage_root = g_config.build_dir / "package" / package_dir_name;
    const fs::path package_zip = g_config.build_dir / (package_dir_name + ".zip");

    std::error_code ec;
    fs::remove_all(stage_root, ec);
    ec.clear();
    fs::remove(package_zip, ec);

    ensure_directory(stage_root);
    ensure_directory(stage_root / "include");
    ensure_directory(stage_root / "lib" / "x64");
    ensure_directory(stage_root / "bin");
    ensure_directory(stage_root / "docs");

    const fs::path sdk_include = g_config.build_dir / "DGTL_SDK" / "include";
    if (!copy_tree_if_exists(sdk_include, stage_root / "include")) {
        std::cerr << "[package] Missing SDK include directory: " << sdk_include.string() << std::endl;
        return 1;
    }

    const fs::path sdk_lib = g_config.build_dir / "DGTL_SDK" / "lib" / "x64" / "dgtl.lib";
    if (!copy_file_if_exists(sdk_lib, stage_root / "lib" / "x64" / "dgtl.lib")) {
        std::cerr << "[package] Missing SDK static library: " << sdk_lib.string() << std::endl;
        return 1;
    }

    const std::vector<fs::path> bin_candidates = {
        g_config.build_dir / "dgtl_release.exe",
        g_config.build_dir / "dgtl-opt.exe",
        g_config.build_dir / "dgtl-llc.exe",
        g_config.build_dir / "dgtl-verify.exe",
        g_config.root / "dgtl.ps1",
        g_config.root / "dgtl.cmd",
        g_config.root / "dgtl.sh"
    };

    for (const auto& src : bin_candidates) {
        if (file_exists(src)) {
            copy_file_if_exists(src, stage_root / "bin" / src.filename());
        }
    }

    copy_file_if_exists(g_config.root / "README.md", stage_root / "README.md");
    copy_file_if_exists(g_config.root / "LICENSE", stage_root / "LICENSE");
    copy_file_if_exists(g_config.root / "docs" / "COMMAND_EXECUTION_GUIDE.md",
                        stage_root / "docs" / "COMMAND_EXECUTION_GUIDE.md");

    const fs::path manifest = stage_root / "PACKAGE_MANIFEST.txt";
    {
        std::ofstream out(manifest, std::ios::out | std::ios::trunc);
        const std::time_t now = std::time(nullptr);
        out << "DGTL SDK Package\n";
        out << "Version: " << version << "\n";
        out << "Generated: " << std::asctime(std::localtime(&now));
        out << "Root: " << g_config.root.string() << "\n\n";
        out << "Included:\n";
        out << "- include/ (public SDK headers)\n";
        out << "- lib/x64/dgtl.lib\n";
        out << "- bin/ (release + tool binaries + wrappers when present)\n";
        out << "- README.md\n";
        out << "- LICENSE\n";
    }

    const fs::path package_parent = stage_root.parent_path();
    const std::string archive_cmd =
        "tar -a -c -f " + quote_path(package_zip) +
        " -C " + quote_path(package_parent) +
        " " + quote_path(stage_root.filename());

    std::cout << "[package] Creating archive " << package_zip.string() << std::endl;
    if (run_command(archive_cmd) != 0) {
        std::cerr << "[package] Failed to create package archive" << std::endl;
        return 1;
    }

    std::cout << "[package] Wrote " << package_zip.string() << std::endl;

    if (verify_mode) {
        return verify_package_archive(package_zip, package_dir_name);
    }

    return 0;
}

int handle_help() {
    show_usage();
    return 0;
}

// Main entry point
int main(int argc, char** argv) {
    // Initialize config
    g_config.root = fs::current_path();
    g_config.build_dir = g_config.root / "build";
    g_config.tools_dir = g_config.root / "tools" / "cpp";
    g_config.include_dir = g_config.root / "include";
    g_config.src_dir = g_config.root / "src";
    
    if (const char* env_cxx = std::getenv("CXX")) {
        g_config.gpp_path = env_cxx;
    } else {
        g_config.gpp_path = "g++"; 
    }

    if (argc < 2) {
        handle_help();
        return 1;
    }

    std::string command = argv[1];
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    // Command routing
    if (command == "help") return handle_help();
    if (command == "build" || command == "build-release") return handle_build();
    if (command == "build-tools") return handle_build_tools();
    if (command == "smoke-build" || command == "build-smoke") return handle_smoke_build();
    if (command == "strict" || command == "build-strict") return handle_strict();
    if (command == "test") {
        if (!args.empty()) {
            std::cerr << "[test] Extra arguments are not supported. Use only: dgtl test" << std::endl;
            return 1;
        }
        return handle_test();
    }
    if (command == "gen") return handle_gen(args);
    if (command == "gen-validate") return handle_gen_validate(args);
    if (command == "gen-build") return handle_gen_build();
    if (command == "header-check") return handle_header_check();
    if (command == "clean" || command == "clean-all") return handle_clean();
    if (command == "run" || command == "run-all") {
        std::cout << "[run] Full conformance not implemented in orchestrator yet" << std::endl;
        return 0;
    }
    if (command == "bench" || command == "bench-backend") {
        std::cout << "[bench] Backend benchmarking not implemented in orchestrator yet" << std::endl;
        return 0;
    }
    if (command == "brun") {
        std::cout << "[brun] Performance suite not implemented in orchestrator yet" << std::endl;
        return 0;
    }
    if (command == "package") {
        bool verify_mode = false;
        for (const auto& arg : args) {
            if (arg == "--verify" || arg == "verify") {
                verify_mode = true;
            } else {
                std::cerr << "[package] Unknown argument: " << arg
                          << ". Supported: --verify" << std::endl;
                return 1;
            }
        }
        return handle_package(verify_mode);
    }
    if (command == "compile" || command == "compile-all") {
        std::cout << "[compile] Backend compilation not implemented in orchestrator yet" << std::endl;
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    handle_help();
    return 1;
}