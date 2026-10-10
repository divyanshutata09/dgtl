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
// dgtl_main.cpp
// C++23/26 cross-platform replacement for dgtl.ps1 orchestration script
// Implements all orchestration, build, validation, and policy gates

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <sstream>
#include <fstream>
#include <regex>
#include <ctime>
#include <thread>
#include <atomic>
#include <numeric>

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

enum class ApiMode {
    internal,
    public_sdk,
    both
};

void ensure_directory(const fs::path& p);
std::optional<ApiMode> parse_api_mode(std::vector<std::string>& args,
                                      ApiMode default_mode,
                                      std::string* error_out = nullptr);
int verify_internal_api_artifacts();
int verify_public_api_artifacts();
int handle_api_status(ApiMode mode);
int handle_prepare_api(ApiMode mode);
int build_internal_static_library();

// Security Utility: Ensures paths with spaces don't break shell commands
[[nodiscard]] inline std::string quote_path(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

[[nodiscard]] inline fs::path make_obj_path(const fs::path& obj_dir, const fs::path& src_path) {
    fs::path rel;
    if (src_path.string().find(g_config.src_dir.string()) != std::string::npos) {
        rel = fs::relative(src_path, g_config.src_dir).lexically_normal();
    } else {
        rel = fs::relative(src_path, g_config.root).lexically_normal();
    }
    std::string stem = rel.string();
    std::replace(stem.begin(), stem.end(), '\\', '_');
    std::replace(stem.begin(), stem.end(), '/', '_');
    if (stem.size() >= 4 && stem.substr(stem.size() - 4) == ".cpp") {
        stem = stem.substr(0, stem.size() - 4);
    }
    return obj_dir / (stem + ".o");
}

int compile_object_if_needed(const fs::path& src, const fs::path& obj, bool strict);
int parallel_compile_objects(const std::vector<fs::path>& sources,
                             const fs::path& obj_dir,
                             std::vector<fs::path>& out_objs,
                             int* out_compiled_count,
                             bool strict);

[[nodiscard]] std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

void append_text_file(const fs::path& path, std::string_view text) {
    ensure_directory(path.parent_path());
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << text;
}

// Utility functions
[[nodiscard]] int run_command(std::string_view cmd, bool verbose = true) {
    if (verbose) {
        std::cout << "[orchestrator] $ " << cmd << std::endl;
    }

    const std::string lower_cmd = [&]() {
        std::string out(cmd);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }();
    if (lower_cmd.find("g++") != std::string::npos || lower_cmd.find("clang++") != std::string::npos) {
        const std::string hash = hex64(fnv1a64(cmd));
        const std::string record = hash + "|" + std::string(cmd) + "\n";
        append_text_file(g_config.build_dir / "logs" / "last-compiler-invocations.log", record);
        append_text_file(g_config.build_dir / "logs" / "compiler-invocations.log", record);
    }

    std::string exec_cmd(cmd);
#ifdef _WIN32
    if (!exec_cmd.empty() && exec_cmd.front() == '"') {
        exec_cmd = "\"" + exec_cmd + "\"";
    }
#endif

    return std::system(exec_cmd.c_str());
}

[[nodiscard]] bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec;
}

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

bool copy_file_if_exists(const fs::path& src, const fs::path& dst) {
    if (!file_exists(src)) {
        return false;
    }

    std::error_code ec;
    ensure_directory(dst.parent_path());
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool copy_tree_if_exists(const fs::path& src, const fs::path& dst);

int sync_sdk_public_headers() {
    const fs::path sdk_src_dir = g_config.include_dir / "dgtl_sdk";
    const fs::path sdk_include_root = g_config.build_dir / "DGTL_SDK" / "include";
    const fs::path sdk_include_ns = sdk_include_root / "dgtl_sdk";

    const std::vector<std::string> sdk_headers = {
        "dgtl.h", "types.h", "target.h", "object.h", "linker.h", "jit.h", "module.h", "mir.h"
    };

    ensure_directory(sdk_include_root);
    ensure_directory(sdk_include_ns);

    for (const auto& header : sdk_headers) {
        const fs::path src = sdk_src_dir / header;
        if (!file_exists(src)) {
            std::cerr << "[sdk] Missing public header: " << src.string() << std::endl;
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

    const fs::path api_src = g_config.include_dir / "dgtl" / "api";
    const fs::path api_dst = sdk_include_root / "dgtl" / "api";
    if (file_exists(api_src)) {
        copy_tree_if_exists(api_src, api_dst);
    }

    std::cout << "[sdk] Synced modular public headers to " << sdk_include_ns.string() << std::endl;
    return 0;
}

// Tool compilation helpers
int compile_tool(std::string_view tool_name, const fs::path& source, const fs::path& output) {
    ensure_directory(output.parent_path());
    
    bool needs_build = true;
    std::error_code ec_size;
    if (file_exists(output) && fs::file_size(output, ec_size) > 0) {
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
    std::string cmd = std::string(g_config.gpp_path) + " -std=c++26 -fmodules-ts -O2 -pipe -I" + quote_path(g_config.include_dir) + " " +
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
    return invoke_tool("dgtl-dup-gate", dup_source, dup_tool,
                      {"--root", quote_path(g_config.root), "--summary-only"});
}

int invoke_dgtl_gen(const std::vector<std::string>& args = {}) {
    auto gen_tool = g_config.build_dir / "tools" / "dgtl-gen.exe";
    auto gen_source = g_config.tools_dir / "dgtl_gen_main.cpp";
    return invoke_tool("dgtl-gen", gen_source, gen_tool, args);
}

int invoke_dgtl_isel_gen(const std::vector<std::string>& args = {}) {
    auto isel_tool = g_config.build_dir / "tools" / "dgtl-isel-gen.exe";
    auto isel_source = g_config.tools_dir / "dgtl_isel_gen_main.cpp";
    return invoke_tool("dgtl-isel-gen", isel_source, isel_tool, args);
}

int invoke_dgtl_gen_validate(const std::vector<std::string>& args = {}) {
    auto validate_tool = g_config.build_dir / "tools" / "dgtl-gen-validate.exe";
    auto validate_source = g_config.tools_dir / "validate_dgtl_gen_main.cpp";
    return invoke_tool("dgtl-gen-validate", validate_source, validate_tool, args);
}

int invoke_dgtl_debugger(const std::vector<std::string>& args = {}) {
    auto debugger_tool = g_config.build_dir / "tools" / "dgtl-debugger.exe";
    auto debugger_source = g_config.tools_dir / "dgtl_debugger_main.cpp";
    return invoke_tool("dgtl-debugger", debugger_source, debugger_tool, args);
}

int invoke_dgtl_lit(const std::vector<std::string>& args = {}) {
    auto lit_tool = g_config.build_dir / "tools" / "dgtl-lit.exe";
    auto lit_source = g_config.tools_dir / "dgtl_lit_main.cpp";
    return invoke_tool("dgtl-lit", lit_source, lit_tool, args);
}


// -----------------------------------------------------------------------------
// Permanent Master Solution Graph Models
// -----------------------------------------------------------------------------
struct ModuleUnit {
    fs::path path;
    std::string module_name;
    std::string partition_name;
    bool is_partition = false;
    bool is_master = false;
    std::vector<std::string> imports;
};

void parse_module_metadata(ModuleUnit& unit) {
    std::ifstream file(unit.path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        size_t comment_idx = line.find("//");
        if (comment_idx != std::string::npos) line = line.substr(0, comment_idx);
        
        std::string token;
        std::stringstream ss(line);
        if (!(ss >> token)) continue;

        bool is_export = (token == "export");
        if (is_export) {
            if (!(ss >> token)) continue;
        }

        if (token == "module") {
            if (!(ss >> token)) continue;
            size_t colon_idx = token.find(':');
            size_t semi_idx = token.find(';');
            if (semi_idx != std::string::npos) token = token.substr(0, semi_idx);

            if (colon_idx != std::string::npos) {
                unit.module_name = token.substr(0, colon_idx);
                unit.partition_name = token.substr(colon_idx + 1);
                unit.is_partition = true;
            } else {
                unit.module_name = token;
                unit.is_master = true;
            }
        } else if (token == "import") {
            if (!(ss >> token)) continue;
            size_t semi_idx = token.find(';');
            if (semi_idx != std::string::npos) token = token.substr(0, semi_idx);
            
            while(!token.empty() && (token.back() == ' ' || token.back() == ';')) {
                token.pop_back();
            }
            if (!token.empty()) {
                unit.imports.push_back(token);
            }
        }
    }
}

void resolve_topological_dfs(const std::string& current_key,
                             const std::unordered_map<std::string, ModuleUnit>& module_map,
                             std::unordered_set<std::string>& visited,
                             std::unordered_set<std::string>& active_stack,
                             std::vector<ModuleUnit>& sorted_output) {
    if (visited.count(current_key)) return;
    if (active_stack.count(current_key)) {
        std::cerr << "[orchestrator] Warning: Circular dependency detected at layout target " << current_key << "\n";
        return;
    }

    active_stack.insert(current_key);
    auto it = module_map.find(current_key);
    if (it != module_map.end()) {
        const auto& unit = it->second;

        if (unit.is_master) {
            for (const auto& [key, potential_partition] : module_map) {
                if (potential_partition.is_partition && potential_partition.module_name == unit.module_name) {
                    if (!visited.count(key)) {
                        resolve_topological_dfs(key, module_map, visited, active_stack, sorted_output);
                    }
                }
            }
        }

        for (const auto& raw_import : unit.imports) {
            std::string target_key = raw_import;
            
            if (!raw_import.empty() && raw_import.front() == ':') {
                target_key = unit.module_name + raw_import;
            }
            
            if (!module_map.count(target_key)) {
                for (const auto& [key, potential_unit] : module_map) {
                    if (potential_unit.is_partition && (potential_unit.module_name + ":" + potential_unit.partition_name == target_key)) {
                        target_key = key;
                        break;
                    }
                }
            }

            if (module_map.count(target_key)) {
                resolve_topological_dfs(target_key, module_map, visited, active_stack, sorted_output);
            }
        }
    }

    active_stack.erase(current_key);
    visited.insert(current_key);
    if (it != module_map.end()) {
        sorted_output.push_back(it->second);
    }
}

std::vector<std::string> load_explicit_module_roots(const std::unordered_map<std::string, ModuleUnit>& module_database) {
    std::vector<std::string> roots;
    const std::vector<fs::path> order_files{
        g_config.root / "module_build_order.txt",
        g_config.root / "build_order.txt"
    };

    std::unordered_map<std::string, std::string> path_to_key;
    std::error_code ec;
    for (const auto& [key, unit] : module_database) {
        path_to_key[fs::weakly_canonical(unit.path, ec).generic_string()] = key;
    }

    for (const auto& order_file : order_files) {
        if (!file_exists(order_file)) {
            continue;
        }

        std::ifstream in(order_file);
        if (!in.is_open()) {
            continue;
        }

        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            line = trim(line);
            if (line.empty() || line.front() == '#' || line.front() == '"' || line.find('=') != std::string::npos) {
                continue;
            }

            fs::path entry_path(line);
            const std::string generic = fs::weakly_canonical(entry_path, ec).generic_string();
            if (ec) {
                ec.clear();
                continue;
            }
            const auto found = path_to_key.find(generic);
            if (found != path_to_key.end()) {
                roots.push_back(found->second);
            }
        }

        if (!roots.empty()) {
            break;
        }
    }

    return roots;
}

fs::path module_object_path_for_source(const fs::path& src, const fs::path& mod_obj_dir) {
    fs::path rel = fs::relative(src, g_config.include_dir);
    std::string stem = rel.parent_path().string() + "/" + rel.stem().string();
    std::replace(stem.begin(), stem.end(), '\\', '_');
    std::replace(stem.begin(), stem.end(), '/', '_');
    std::replace(stem.begin(), stem.end(), '.', '_');
    return mod_obj_dir / (stem + ".o");
}

fs::path module_cache_path_for_name(std::string module_name) {
    std::replace(module_name.begin(), module_name.end(), ':', '-');
    return g_config.root / "gcm.cache" / (module_name + ".gcm");
}

int compile_module_source_if_needed(const fs::path& src, const fs::path& mod_obj_dir, bool force_rebuild = false) {
    if (!file_exists(src)) {
        return 0;
    }

    const fs::path obj = module_object_path_for_source(src, mod_obj_dir);
    const auto src_time = get_file_mtime(src);
    const auto obj_time = get_file_mtime(obj);

    std::error_code ec_size;
    const bool needs_compile = force_rebuild || !file_exists(obj) || (fs::file_size(obj, ec_size) == 0) || src_time > obj_time;
    if (!needs_compile) {
        return 0;
    }

    std::string rel_path = fs::relative(src, g_config.root).string();
    std::cout << "[build] Compiling module: " << rel_path << "..." << std::endl;
    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -fmodules-ts -O0 -pipe -I" + quote_path(g_config.include_dir) + " -c " + quote_path(src) + " -o " + quote_path(obj);

    int ec = run_command(cmd, false);
    if (ec != 0) {
        std::cerr << "[build] Failed to compile module: " << rel_path << std::endl;
    }
    return ec;
}

// Module precompilation via dynamic scanning engine topology
int compile_all_modules() {
    const fs::path mod_obj_dir = g_config.build_dir / "obj" / "modules";
    const fs::path module_cache_dir = g_config.root / "gcm.cache";
    ensure_directory(mod_obj_dir);
    const bool cold_module_cache = !file_exists(module_cache_dir);

    std::unordered_map<std::string, ModuleUnit> module_database;
    std::vector<std::string> scan_roots;
    std::error_code ec_scan;

    for (const auto& entry : fs::recursive_directory_iterator(g_config.include_dir, ec_scan)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ixx") {
            ModuleUnit unit;
            unit.path = entry.path();
            parse_module_metadata(unit);

            std::string primary_key;
            if (unit.is_partition) {
                primary_key = unit.module_name + ":" + unit.partition_name;
            } else if (unit.is_master) {
                primary_key = unit.module_name;
            } else {
                primary_key = entry.path().filename().string();
            }

            module_database[primary_key] = unit;
            scan_roots.push_back(primary_key);
        }
    }

    std::sort(scan_roots.begin(), scan_roots.end());

    std::vector<ModuleUnit> deployment_pipeline;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> active_stack;

    const std::vector<std::string> bootstrap_roots{
        "dgtl.ir_enums",
        "dgtl.ir_node",
        "dgtl.ir.stream",
        "dgtl.ir_core.stream",
        "dgtl.op_traits",
        "dgtl.target",
        "dgtl.hir.semantic",
        "dgtl.ir_core.semantic",
        "dgtl.hir.type_system",
        "dgtl.ir_core.type_system",
        "dgtl.backend.driver",
        "dgtl.backend.driver.shared",
        "dgtl.runtime.numa",
        "dgtl.runtime.task_scheduler",
        "dgtl.runtime.intrinsics",
        "dgtl.api",
        "dgtl.api.config",
        "dgtl.ir",
        "dgtl.ir_core.arm32neon_emitter",
        "dgtl.ir_core.riscv64_emitter",
        "dgtl.ir_core.wasmsimd_fma_emitter",
        "dgtl.backend.isa.win64_frame_lowering",
        "dgtl.backend.linker.pe",
        "dgtl.isa.x64.isel_gen",
        "dgtl.isa.x64.isel_rules",
        "dgtl.isa.arm64.isel_gen",
        "dgtl.isa.arm64.isel_rules",
        "dgtl.isa.wasm.isel_gen",
        "dgtl.isa.wasm.isel_rules",
        "dgtl.ir_core.isel_gen",
        "dgtl.ir_core.isel_rules"
    };
    for (const auto& root_key : bootstrap_roots) {
        if (module_database.count(root_key)) {
            resolve_topological_dfs(root_key, module_database, visited, active_stack, deployment_pipeline);
        }
    }

    const auto hinted_roots = load_explicit_module_roots(module_database);
    for (const auto& root_key : hinted_roots) {
        resolve_topological_dfs(root_key, module_database, visited, active_stack, deployment_pipeline);
    }

    for (const auto& root_key : scan_roots) {
        resolve_topological_dfs(root_key, module_database, visited, active_stack, deployment_pipeline);
    }

    bool force_rebuild_all = cold_module_cache;
    auto newest_module_src = fs::file_time_type::min();
    auto oldest_module_obj = fs::file_time_type::max();
    bool has_any_module_obj = false;

    for (const auto& module_unit : deployment_pipeline) {
        const auto src_time = get_file_mtime(module_unit.path);
        if (src_time > newest_module_src) {
            newest_module_src = src_time;
        }

        const fs::path obj_path = module_object_path_for_source(module_unit.path, mod_obj_dir);
        if (file_exists(obj_path)) {
            has_any_module_obj = true;
            const auto obj_time = get_file_mtime(obj_path);
            if (obj_time < oldest_module_obj) {
                oldest_module_obj = obj_time;
            }
        } else {
            force_rebuild_all = true;
        }
    }

    if (!force_rebuild_all && has_any_module_obj && newest_module_src > oldest_module_obj) {
        force_rebuild_all = true;
        std::cout << "[build] Module interface change detected; rebuilding full module closure" << std::endl;
    }

    for (const auto& module_unit : deployment_pipeline) {
        const fs::path& src = module_unit.path;
        bool force_rebuild = force_rebuild_all;
        if (!module_unit.module_name.empty()) {
            const fs::path module_cache = module_cache_path_for_name(
                module_unit.is_partition
                    ? (module_unit.module_name + ":" + module_unit.partition_name)
                    : module_unit.module_name);
            if (!file_exists(module_cache)) {
                force_rebuild = true;
            }
        }

        int ec = compile_module_source_if_needed(src, mod_obj_dir, force_rebuild);
        if (ec != 0) return ec;
    }
    return 0;
}

// Binary building
int build_binary(std::string_view binary_name, const fs::path& output, const fs::path& entry_point,
                 const std::vector<fs::path>& exclude_paths = {}, bool strict = false) {
    if (compile_all_modules() != 0) {
        return 1;
    }
    ensure_directory(output.parent_path());
    const fs::path obj_dir = g_config.build_dir / "obj" / std::string(binary_name);
    ensure_directory(obj_dir);

    std::vector<fs::path> normalized_excludes;
    normalized_excludes.reserve(exclude_paths.size() + 1);
    normalized_excludes.push_back(fs::absolute(entry_point).lexically_normal());
    for (const auto& exc : exclude_paths) {
        normalized_excludes.push_back(fs::absolute(exc).lexically_normal());
    }

    std::vector<fs::path> cpp_sources;
    std::error_code ec_dir;
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

        cpp_sources.push_back(entry.path());
    }

    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        cpp_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(cpp_sources.begin(), cpp_sources.end());

    std::cout << "[build] Compiling " << binary_name << " objects in parallel..." << std::endl;
    std::vector<fs::path> common_objs;
    int compiled_count = 0;
    int ec = parallel_compile_objects(cpp_sources, obj_dir, common_objs, &compiled_count, strict);
    if (ec != 0) return ec;

    const fs::path entry_obj = make_obj_path(obj_dir, entry_point);
    ec = compile_object_if_needed(entry_point, entry_obj, strict);
    if (ec != 0) return ec;

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -fmodules-ts -O2 -pipe";
    if (strict) {
        cmd += " -Wall -Wextra -Wpedantic -Werror";
    }
    for (const auto& obj : common_objs) {
        cmd += " " + quote_path(obj);
    }
    std::error_code ec_mod;
    for (const auto& entry : fs::directory_iterator(g_config.build_dir / "obj" / "modules", ec_mod)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            cmd += " " + quote_path(entry.path());
        }
    }
    cmd += " " + quote_path(entry_obj) + " -o " + quote_path(output);

    std::string final_cmd = cmd;
#ifdef _WIN32
    if (cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / (std::string(binary_name) + "_link.rsp");
        ensure_directory(rsp_path.parent_path());

        const std::size_t prefix_len = g_config.gpp_path.size();
        std::string rsp_args = (cmd.size() > prefix_len) ? cmd.substr(prefix_len) : std::string{};
        if (!rsp_args.empty() && rsp_args.front() == ' ') {
            rsp_args.erase(rsp_args.begin());
        }

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (!rsp_out.is_open()) {
            std::cerr << "[build] Failed to write response file: " << rsp_path.string() << std::endl;
            return 1;
        }
        rsp_out << rsp_args;
        rsp_out.close();

        final_cmd = g_config.gpp_path + " @" + quote_path(rsp_path);
    }
#endif

    std::cout << "[build] Linking " << binary_name << std::endl;
    ec = run_command(final_cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << output.string() << std::endl;
    }
    return ec;
}

int ensure_binary(std::string_view binary_name, const fs::path& output, const fs::path& entry_point,
                  const std::vector<fs::path>& exclude_paths = {}, bool strict = false) {
    std::error_code ec_size;
    if (!file_exists(output) || (fs::file_size(output, ec_size) == 0)) {
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
        fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal()
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
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        cmd += " " + quote_path(gpu_runtime_cpp);
    }
}

int build_test_suite_binary(const fs::path& output,
                            const fs::path& entry_point,
                            const std::vector<fs::path>& test_sources) {
    if (compile_all_modules() != 0) {
        return 1;
    }
    ensure_directory(output.parent_path());
    const fs::path obj_dir = g_config.build_dir / "obj" / "dgtl_tests";
    ensure_directory(obj_dir);

    const std::vector<fs::path> excluded_src = {
        fs::absolute(g_config.src_dir / "main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal(),
        fs::absolute(entry_point).lexically_normal()
    };

    std::vector<fs::path> cpp_sources;
    std::error_code ec_dir;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") continue;
        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        bool skip = false;
        for (const auto& ex : excluded_src) {
            if (abs == ex) { skip = true; break; }
        }
        if (!skip) cpp_sources.push_back(entry.path());
    }
    for (const auto& src : test_sources) {
        cpp_sources.push_back(src);
    }
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        cpp_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(cpp_sources.begin(), cpp_sources.end());

    std::vector<fs::path> common_objs;
    int compiled_count = 0;
    int ec = parallel_compile_objects(cpp_sources, obj_dir, common_objs, &compiled_count, false);
    if (ec != 0) return ec;

    const fs::path entry_obj = make_obj_path(obj_dir, entry_point);
    ec = compile_object_if_needed(entry_point, entry_obj, false);
    if (ec != 0) return ec;

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -fmodules-ts -O2 -pipe";
    for (const auto& obj : common_objs) {
        cmd += " " + quote_path(obj);
    }
    std::error_code ec_mod;
    for (const auto& entry : fs::directory_iterator(g_config.build_dir / "obj" / "modules", ec_mod)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            cmd += " " + quote_path(entry.path());
        }
    }
    cmd += " " + quote_path(entry_obj) + " -o " + quote_path(output);

    std::string final_cmd = cmd;
#ifdef _WIN32
    if (cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / "dgtl_tests_link.rsp";
        ensure_directory(rsp_path.parent_path());

        const std::size_t prefix_len = g_config.gpp_path.size();
        std::string rsp_args = (cmd.size() > prefix_len) ? cmd.substr(prefix_len) : std::string{};
        if (!rsp_args.empty() && rsp_args.front() == ' ') {
            rsp_args.erase(rsp_args.begin());
        }

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (!rsp_out.is_open()) {
            std::cerr << "[build] Failed to write response file: " << rsp_path.string() << std::endl;
            return 1;
        }
        rsp_out << rsp_args;
        rsp_out.close();

        final_cmd = g_config.gpp_path + " @" + quote_path(rsp_path);
    }
#endif

    std::cout << "[build] Linking dgtl_tests" << std::endl;
    ec = run_command(final_cmd);
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
    if (compile_all_modules() != 0) {
        return 1;
    }
    ensure_directory(output.parent_path());
    const fs::path obj_dir = g_config.build_dir / "obj" / "standalone_test";
    ensure_directory(obj_dir);

    const std::vector<fs::path> excluded_src = {
        fs::absolute(g_config.src_dir / "main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal(),
        fs::absolute(entry_point).lexically_normal()
    };

    std::vector<fs::path> cpp_sources;
    std::error_code ec_dir;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec_dir)) {
        if (!fs::is_regular_file(entry.status()) || entry.path().extension() != ".cpp") continue;
        const fs::path abs = fs::absolute(entry.path()).lexically_normal();
        bool skip = false;
        for (const auto& ex : excluded_src) {
            if (abs == ex) { skip = true; break; }
        }
        if (!skip) cpp_sources.push_back(entry.path());
    }
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        cpp_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(cpp_sources.begin(), cpp_sources.end());

    std::vector<fs::path> common_objs;
    int compiled_count = 0;
    int ec = parallel_compile_objects(cpp_sources, obj_dir, common_objs, &compiled_count, false);
    if (ec != 0) return ec;

    const fs::path entry_obj = make_obj_path(obj_dir, entry_point);
    ec = compile_object_if_needed(entry_point, entry_obj, false);
    if (ec != 0) return ec;

    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -fmodules-ts -O3 -pipe";
    for (const auto& obj : common_objs) {
        cmd += " " + quote_path(obj);
    }
    std::error_code ec_mod;
    for (const auto& entry : fs::directory_iterator(g_config.build_dir / "obj" / "modules", ec_mod)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            cmd += " " + quote_path(entry.path());
        }
    }
    cmd += " " + quote_path(entry_obj) + " -o " + quote_path(output);

    std::string final_cmd = cmd;
#ifdef _WIN32
    if (cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / "standalone_test_link.rsp";
        ensure_directory(rsp_path.parent_path());

        const std::size_t prefix_len = g_config.gpp_path.size();
        std::string rsp_args = (cmd.size() > prefix_len) ? cmd.substr(prefix_len) : std::string{};
        if (!rsp_args.empty() && rsp_args.front() == ' ') {
            rsp_args.erase(rsp_args.begin());
        }

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (!rsp_out.is_open()) {
            std::cerr << "[build] Failed to write response file: " << rsp_path.string() << std::endl;
            return 1;
        }
        rsp_out << rsp_args;
        rsp_out.close();

        final_cmd = g_config.gpp_path + " @" + quote_path(rsp_path);
    }
#endif

    std::cout << "[build] Linking " << output.filename().string() << std::endl;
    ec = run_command(final_cmd);
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

int compile_object_if_needed(const fs::path& src, const fs::path& obj, bool strict) {
    const auto src_time = get_file_mtime(src);
    const auto include_time = latest_mtime({g_config.include_dir});
    const auto obj_time = get_file_mtime(obj);

    std::error_code ec_size;
    const bool needs_compile = !file_exists(obj) || (fs::file_size(obj, ec_size) == 0) || src_time > obj_time || include_time > obj_time;
    if (!needs_compile) {
        return 0;
    }

    std::cout << "[build] Compiling: " << src.filename().string() << "..." << std::endl;
    ensure_directory(obj.parent_path());
    std::string cmd = g_config.gpp_path;
    cmd += " -std=c++26 -fmodules-ts -O3 -pipe -I" + quote_path(g_config.include_dir);
    if (strict) {
        cmd += " -Wall -Wextra -Wpedantic -Werror";
    }
    cmd += " -c " + quote_path(src) + " -o " + quote_path(obj);
    return run_command(cmd, false);
}

int parallel_compile_objects(const std::vector<fs::path>& sources,
                             const fs::path& obj_dir,
                             std::vector<fs::path>& out_objs,
                             int* out_compiled_count,
                             bool strict) {
    out_objs.resize(sources.size());
    const unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<bool> compile_failed{false};
    std::atomic<int> compiled_count{0};
    std::atomic<std::size_t> current_work_idx{0};

    auto worker = [&]() {
        while (!compile_failed.load(std::memory_order_relaxed)) {
            std::size_t idx = current_work_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= sources.size()) break;

            const auto& src = sources[idx];
            const fs::path obj = make_obj_path(obj_dir, src);
            const auto before = get_file_mtime(obj);
            int ec = compile_object_if_needed(src, obj, strict);
            if (ec != 0) {
                compile_failed.store(true, std::memory_order_relaxed);
                return;
            }
            if (get_file_mtime(obj) > before) {
                compiled_count.fetch_add(1, std::memory_order_relaxed);
            }
            out_objs[idx] = obj;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    if (compile_failed.load()) return 1;
    if (out_compiled_count) {
        *out_compiled_count = compiled_count.load();
    }
    return 0;
}

int build_tools_optimized(const fs::path& prod_main,
                          const fs::path& smoke_main,
                          const fs::path& opt_main,
                          const fs::path& llc_main,
                          const fs::path& verify_main,
                          const fs::path& opt_exe,
                          const fs::path& llc_exe,
                          const fs::path& verify_exe) {
    if (compile_all_modules() != 0) {
        return 1;
    }
    const fs::path obj_dir = g_config.build_dir / "obj" / "build-tools";
    ensure_directory(obj_dir);

    const fs::path link_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal();
    const fs::path engine_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal();
    const fs::path ar_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal();
    const fs::path objdump_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal();
    const fs::path as_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal();

    std::vector<fs::path> excluded = {
        fs::absolute(prod_main).lexically_normal(),
        fs::absolute(smoke_main).lexically_normal(),
        fs::absolute(opt_main).lexically_normal(),
        fs::absolute(llc_main).lexically_normal(),
        fs::absolute(verify_main).lexically_normal(),
        fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal(),
        link_main,
        engine_main,
        ar_main,
        objdump_main,
        as_main
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
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        common_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(common_sources.begin(), common_sources.end());

    std::vector<fs::path> common_objs;
    int compiled_count = 0;
    int ec = parallel_compile_objects(common_sources, obj_dir, common_objs, &compiled_count, false);
    if (ec != 0) {
        return ec;
    }
    std::cout << "[build-tools] Shared objects: " << compiled_count
              << " compiled, " << (common_objs.size() - compiled_count)
              << " reused" << std::endl;

    const fs::path tools_dir = g_config.build_dir / "tools";
    const fs::path sdk_bin_dir = g_config.build_dir / "DGTL_SDK" / "bin";
    ensure_directory(tools_dir);
    ensure_directory(sdk_bin_dir);

    auto stage_tool_bin = [&](const fs::path& exe) {
        if (file_exists(exe)) {
            copy_file_if_exists(exe, tools_dir / exe.filename());
            copy_file_if_exists(exe, sdk_bin_dir / exe.filename());
            std::string fname = exe.filename().string();
            if (fname.find('-') != std::string::npos) {
                std::string alt_name = fname;
                std::replace(alt_name.begin(), alt_name.end(), '-', '_');
                copy_file_if_exists(exe, tools_dir / alt_name);
                copy_file_if_exists(exe, g_config.build_dir / alt_name);
            }
        }
    };

    auto link_tool = [&](std::string_view name, const fs::path& entry_main, const fs::path& output) -> int {
        const fs::path entry_obj = make_obj_path(obj_dir, entry_main);
        int ec = compile_object_if_needed(entry_main, entry_obj, false);
        if (ec != 0) {
            return ec;
        }

        std::error_code ec_size;
        bool needs_link = !file_exists(output) || (fs::file_size(output, ec_size) == 0);
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
        cmd += " -std=c++26 -fmodules-ts -O3 -pipe";
        for (const auto& obj : common_objs) {
            cmd += " " + quote_path(obj);
        }
        std::error_code ec_mod;
        for (const auto& entry : fs::directory_iterator(g_config.build_dir / "obj" / "modules", ec_mod)) {
            if (entry.is_regular_file() && entry.path().extension() == ".o") {
                cmd += " " + quote_path(entry.path());
            }
        }
        cmd += " " + quote_path(entry_obj) + " -o " + quote_path(output);

        std::string final_cmd = cmd;
#ifdef _WIN32
        if (cmd.size() > 7000) {
            const fs::path rsp_path = g_config.build_dir / "obj" / "build-tools" / (std::string(name) + "_link.rsp");
            ensure_directory(rsp_path.parent_path());

            const std::size_t prefix_len = g_config.gpp_path.size();
            std::string rsp_args = (cmd.size() > prefix_len) ? cmd.substr(prefix_len) : std::string{};
            if (!rsp_args.empty() && rsp_args.front() == ' ') {
                rsp_args.erase(rsp_args.begin());
            }

            std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
            if (!rsp_out.is_open()) {
                std::cerr << "[build] Failed to write response file: " << rsp_path.string() << std::endl;
                return 1;
            }
            rsp_out << rsp_args;
            rsp_out.close();

            final_cmd = g_config.gpp_path + " @" + quote_path(rsp_path);
        }
#endif

        std::cout << "[build] Linking " << name << std::endl;
        ec = run_command(final_cmd);
        if (ec == 0) {
            std::cout << "[build] Wrote " << output.string() << std::endl;
        }
        return ec;
    };

    ec = 0;
    ec |= link_tool("dgtl-opt", opt_main, opt_exe);
    if (ec != 0) return ec;
    stage_tool_bin(opt_exe);

    ec |= link_tool("dgtl-llc", llc_main, llc_exe);
    if (ec != 0) return ec;
    stage_tool_bin(llc_exe);

    ec |= link_tool("dgtl-verify", verify_main, verify_exe);
    if (ec != 0) return ec;
    stage_tool_bin(verify_exe);

    const fs::path link_exe = g_config.build_dir / "dgtl-link.exe";
    ec |= link_tool("dgtl-link", link_main, link_exe);
    if (ec != 0) return ec;
    stage_tool_bin(link_exe);

    const fs::path engine_exe = g_config.build_dir / "dgtl-engine.exe";
    ec |= link_tool("dgtl-engine", engine_main, engine_exe);
    if (ec != 0) return ec;
    stage_tool_bin(engine_exe);

    const fs::path ar_exe = g_config.build_dir / "dgtl-ar.exe";
    ec |= link_tool("dgtl-ar", ar_main, ar_exe);
    if (ec != 0) return ec;
    stage_tool_bin(ar_exe);

    const fs::path as_exe = g_config.build_dir / "dgtl-as.exe";
    ec |= link_tool("dgtl-as", as_main, as_exe);
    if (ec != 0) return ec;
    stage_tool_bin(as_exe);

    const fs::path objdump_source = g_config.src_dir / "apps" / "dgtl_objdump_main.cpp";
    const fs::path objdump_exe = g_config.build_dir / "dgtl-objdump.exe";
    ensure_directory(objdump_exe.parent_path());
    const std::string objdump_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(objdump_source) + " -o " + quote_path(objdump_exe);
    if (!file_exists(objdump_exe) || get_file_mtime(objdump_source) > get_file_mtime(objdump_exe)) {
        std::cout << "[build] Compiling: dgtl_objdump_main.cpp..." << std::endl;
        if (run_command(objdump_cmd) == 0) {
            std::cout << "[build] Wrote " << objdump_exe.string() << std::endl;
        }
    } else {
        std::cout << "[build] Reusing up-to-date dgtl-objdump" << std::endl;
    }
    stage_tool_bin(objdump_exe);

    const fs::path inspect_source = g_config.src_dir / "apps" / "dgtl_inspect_main.cpp";
    const fs::path inspect_exe = g_config.build_dir / "dgtl-inspect.exe";
    ensure_directory(inspect_exe.parent_path());
    const std::string inspect_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(inspect_source) + " -o " + quote_path(inspect_exe);
    if (!file_exists(inspect_exe) || get_file_mtime(inspect_source) > get_file_mtime(inspect_exe)) {
        if (run_command(inspect_cmd) == 0) {
            std::cout << "[build] Wrote " << inspect_exe.string() << std::endl;
        }
    } else {
        std::cout << "[build] Reusing up-to-date dgtl-inspect" << std::endl;
    }
    stage_tool_bin(inspect_exe);

    const fs::path config_src = g_config.root / "tools" / "cpp" / "dgtl_config_main.cpp";
    const fs::path config_exe = sdk_bin_dir / "dgtl-config.exe";
    if (file_exists(config_src)) {
        if (!file_exists(config_exe) || get_file_mtime(config_src) > get_file_mtime(config_exe)) {
            std::string cfg_compile = g_config.gpp_path + " -std=c++20 -O2 -pipe " + quote_path(config_src) + " -o " + quote_path(config_exe);
            if (run_command(cfg_compile) == 0) {
                std::cout << "[sdk] Built discovery tool: " << config_exe.string() << std::endl;
                copy_file_if_exists(config_exe, tools_dir / "dgtl-config.exe");
            }
        } else {
            copy_file_if_exists(config_exe, tools_dir / "dgtl-config.exe");
        }
    }

    const fs::path rodata_source = g_config.tools_dir / "dgtl_rodata_maker_main.cpp";
    const fs::path rodata_exe = g_config.build_dir / "tools" / "dgtl-rodata-maker.exe";
    ensure_directory(rodata_exe.parent_path());
    const std::string rodata_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(rodata_source) + " -o " + quote_path(rodata_exe);
    if (run_command(rodata_cmd) == 0) {
        std::cout << "[build] Wrote " << rodata_exe.string() << std::endl;
    }

    const fs::path debugger_source = g_config.tools_dir / "dgtl_debugger_main.cpp";
    const fs::path debugger_exe = g_config.build_dir / "tools" / "dgtl-debugger.exe";
    ensure_directory(debugger_exe.parent_path());
    const std::string debugger_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(debugger_source) + " -o " + quote_path(debugger_exe);
    if (run_command(debugger_cmd) == 0) {
        std::cout << "[build] Wrote " << debugger_exe.string() << std::endl;
    }

    return ec;
}

int build_sdk_static_library() {
    const fs::path sdk_obj_dir = g_config.build_dir / "sdk_obj";
    const fs::path sdk_lib_dir = g_config.build_dir / "DGTL_SDK" / "lib" / "x64";
    const fs::path sdk_lib = sdk_lib_dir / "dgtl.lib";
    const fs::path mod_obj_dir = g_config.build_dir / "obj" / "modules";

    if (sync_sdk_public_headers() != 0) {
        return 1;
    }

    if (compile_all_modules() != 0) {
        return 1;
    }

    ensure_directory(sdk_obj_dir);
    ensure_directory(sdk_lib_dir);

    const fs::path prod_main = fs::absolute(g_config.src_dir / "main.cpp").lexically_normal();
    const fs::path smoke_main = fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal();
    const fs::path opt_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal();
    const fs::path llc_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal();
    const fs::path verify_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal();
    const fs::path inspect_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal();
    const fs::path link_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal();
    const fs::path engine_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal();
    const fs::path ar_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal();
    const fs::path objdump_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal();
    const fs::path as_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal();

    const std::vector<fs::path> excluded = {
        prod_main, smoke_main, opt_main, llc_main, verify_main, inspect_main, link_main, engine_main,
        ar_main, objdump_main, as_main
    };

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
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        sdk_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(sdk_sources.begin(), sdk_sources.end());

    std::vector<fs::path> sdk_objs;
    int ec_parallel_sdk = parallel_compile_objects(sdk_sources, sdk_obj_dir, sdk_objs, nullptr, false);
    if (ec_parallel_sdk != 0) {
        return ec_parallel_sdk;
    }

    std::error_code ec_mod;
    for (const auto& entry : fs::directory_iterator(mod_obj_dir, ec_mod)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            sdk_objs.push_back(entry.path());
        }
    }

    std::sort(sdk_objs.begin(), sdk_objs.end());
    sdk_objs.erase(std::unique(sdk_objs.begin(), sdk_objs.end()), sdk_objs.end());

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

    std::string final_cmd = cmd;
#ifdef _WIN32
    if (cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / "sdk_archive.rsp";
        ensure_directory(rsp_path.parent_path());

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (!rsp_out.is_open()) {
            std::cerr << "[build] Failed to write SDK archive response file: " << rsp_path.string() << std::endl;
            return 1;
        }

        for (const auto& obj : sdk_objs) {
            rsp_out << quote_path(obj) << "\n";
        }
        rsp_out.close();

        final_cmd = "ar rcs " + quote_path(sdk_lib) + " @" + quote_path(rsp_path);
    }
#endif

    std::cout << "[build] Archiving dgtl.lib" << std::endl;
    int ec = run_command(final_cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << sdk_lib.string() << std::endl;

        // Compile & stage dgtl-config discovery tool
        const fs::path sdk_bin_dir = g_config.build_dir / "DGTL_SDK" / "bin";
        ensure_directory(sdk_bin_dir);
        const fs::path config_src = g_config.root / "tools" / "cpp" / "dgtl_config_main.cpp";
        const fs::path config_exe = sdk_bin_dir / "dgtl-config.exe";
        if (file_exists(config_src)) {
            std::string cfg_compile = g_config.gpp_path + " -std=c++20 -O2 -pipe " + quote_path(config_src) + " -o " + quote_path(config_exe);
            if (run_command(cfg_compile) == 0) {
                std::cout << "[sdk] Built discovery tool: " << config_exe.string() << std::endl;
                ensure_directory(g_config.build_dir / "tools");
                copy_file_if_exists(config_exe, g_config.build_dir / "tools" / "dgtl-config.exe");
            }
        }

        // Generate pkg-config metadata (dgtl.pc)
        const fs::path pkgconfig_dir = g_config.build_dir / "DGTL_SDK" / "lib" / "pkgconfig";
        ensure_directory(pkgconfig_dir);
        {
            std::ofstream pc(pkgconfig_dir / "dgtl.pc", std::ios::trunc);
            pc << "prefix=${pcfiledir}/../..\n"
               << "exec_prefix=${prefix}\n"
               << "libdir=${prefix}/lib/x64\n"
               << "includedir=${prefix}/include\n\n"
               << "Name: DGTL\n"
               << "Description: Deep General-purpose Transformation Layer Compiler & JIT Engine\n"
               << "Version: 2.1.0-SOVEREIGN\n"
               << "Cflags: -I${includedir}\n"
               << "Libs: -L${libdir} -ldgtl\n";
        }

        // Generate CMake package config (DGTLConfig.cmake)
        const fs::path cmake_dir = g_config.build_dir / "DGTL_SDK" / "lib" / "cmake" / "DGTL";
        ensure_directory(cmake_dir);
        {
            std::ofstream cm(cmake_dir / "DGTLConfig.cmake", std::ios::trunc);
            cm << "# DGTL CMake Package Configuration\n"
               << "set(DGTL_VERSION \"2.1.0-SOVEREIGN\")\n"
               << "set(DGTL_INCLUDE_DIRS \"${CMAKE_CURRENT_LIST_DIR}/../../../include\")\n"
               << "set(DGTL_LIBRARIES \"${CMAKE_CURRENT_LIST_DIR}/../../x64/dgtl.lib\")\n\n"
               << "if(NOT TARGET DGTL::dgtl)\n"
               << "    add_library(DGTL::dgtl UNKNOWN IMPORTED)\n"
               << "    set_target_properties(DGTL::dgtl PROPERTIES\n"
               << "        IMPORTED_LOCATION \"${DGTL_LIBRARIES}\"\n"
               << "        INTERFACE_INCLUDE_DIRECTORIES \"${DGTL_INCLUDE_DIRS}\"\n"
               << "    )\n"
               << "endif()\n";
        }

        // Generate Component Manifest
        const fs::path manifest_path = sdk_lib_dir / "COMPONENT_MANIFEST.json";
        {
            std::ofstream mf(manifest_path, std::ios::trunc);
            mf << "{\n"
               << "  \"name\": \"DGTL\",\n"
               << "  \"version\": \"2.1.0-SOVEREIGN\",\n"
               << "  \"components\": [\"core\", \"ir\", \"opt\", \"mir\", \"isa\", \"codegen\", \"object\", \"jit\", \"linker\", \"runtime\", \"sdk\"],\n"
               << "  \"libraries\": {\n"
               << "    \"unified\": \"dgtl.lib\"\n"
               << "  }\n"
               << "}\n";
        }
    }
    return ec;
}

int build_internal_static_library() {
    const fs::path internal_obj_dir = g_config.build_dir / "internal_obj";
    const fs::path internal_lib_dir = g_config.build_dir / "DGTL_INTERNAL" / "lib" / "x64";
    const fs::path internal_lib = internal_lib_dir / "dgtl_internal.lib";
    const fs::path mod_obj_dir = g_config.build_dir / "obj" / "modules";

    if (compile_all_modules() != 0) {
        return 1;
    }

    ensure_directory(internal_obj_dir);
    ensure_directory(internal_lib_dir);

    const fs::path prod_main = fs::absolute(g_config.src_dir / "main.cpp").lexically_normal();
    const fs::path smoke_main = fs::absolute(g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal();
    const fs::path opt_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal();
    const fs::path llc_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal();
    const fs::path verify_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal();
    const fs::path inspect_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_inspect_main.cpp").lexically_normal();
    const fs::path link_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_link_main.cpp").lexically_normal();
    const fs::path engine_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_engine_main.cpp").lexically_normal();
    const fs::path ar_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_ar_main.cpp").lexically_normal();
    const fs::path objdump_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_objdump_main.cpp").lexically_normal();
    const fs::path as_main = fs::absolute(g_config.src_dir / "apps" / "dgtl_as_main.cpp").lexically_normal();

    const std::vector<fs::path> excluded = {
        prod_main, smoke_main, opt_main, llc_main, verify_main, inspect_main, link_main, engine_main,
        ar_main, objdump_main, as_main
    };

    std::vector<fs::path> internal_sources;
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
            internal_sources.push_back(entry.path());
        }
    }
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        internal_sources.push_back(gpu_runtime_cpp);
    }
    std::sort(internal_sources.begin(), internal_sources.end());

    std::vector<fs::path> internal_objs;
    int ec_parallel_int = parallel_compile_objects(internal_sources, internal_obj_dir, internal_objs, nullptr, false);
    if (ec_parallel_int != 0) {
        return ec_parallel_int;
    }

    std::error_code ec_mod;
    for (const auto& entry : fs::directory_iterator(mod_obj_dir, ec_mod)) {
        if (entry.is_regular_file() && entry.path().extension() == ".o") {
            internal_objs.push_back(entry.path());
        }
    }

    std::sort(internal_objs.begin(), internal_objs.end());
    internal_objs.erase(std::unique(internal_objs.begin(), internal_objs.end()), internal_objs.end());

    bool needs_archive = !file_exists(internal_lib);
    const auto lib_time = get_file_mtime(internal_lib);
    if (!needs_archive) {
        for (const auto& obj : internal_objs) {
            if (get_file_mtime(obj) > lib_time) {
                needs_archive = true;
                break;
            }
        }
    }

    if (!needs_archive) {
        std::cout << "[build] Reusing up-to-date " << internal_lib.string() << std::endl;
        return 0;
    }

    std::string cmd = "ar rcs " + quote_path(internal_lib);
    for (const auto& obj : internal_objs) {
        cmd += " " + quote_path(obj);
    }

    std::string final_cmd = cmd;
#ifdef _WIN32
    if (cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / "internal_archive.rsp";
        ensure_directory(rsp_path.parent_path());

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (!rsp_out.is_open()) {
            std::cerr << "[build] Failed to write internal archive response file: " << rsp_path.string() << std::endl;
            return 1;
        }

        for (const auto& obj : internal_objs) {
            rsp_out << quote_path(obj) << "\n";
        }
        rsp_out.close();

        final_cmd = "ar rcs " + quote_path(internal_lib) + " @" + quote_path(rsp_path);
    }
#endif

    std::cout << "[build] Archiving dgtl_internal.lib" << std::endl;
    int ec = run_command(final_cmd);
    if (ec == 0) {
        std::cout << "[build] Wrote " << internal_lib.string() << std::endl;
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
  dgtl test               Run functional test suite (automatic discovery)
  dgtl lit [options]      Run native .dgtl FileCheck & LIT test suite
  dgtl run <file>         Compile and execute single test or payload directly
  dgtl inspect <file>     Inspect object sections, memory budget, and unwinding
  dgtl header-check       Verify header compliance gates
  dgtl gen-build          Regenerate IR definitions and rebuild
  dgtl package            Create SDK distribution archive

================================================================================
BUILD COMMANDS:
================================================================================
  build, build-release    Build production binary -> build/dgtl_release.exe
  build-smoke             Build smoke/conformance test binary
  build-strict            Build with strict validation (Wall, Wextra, Werror)
  build-tools             Compile optimizer tools (dgtl-opt, dgtl-llc, dgtl-verify, dgtl-inspect)
                          Optional: --api internal|public|both (default: both)
  build-verify            Compile verification tool (dgtl-verify)
  prepare-api             Prepare DGTL API surfaces explicitly
                          Usage: dgtl prepare-api --api internal|public|both
  api-status              Report DGTL API lane readiness
                          Usage: dgtl api-status --api internal|public|both
  clean, clean-all        Remove all build outputs and logs
  crun                    Full cycle: clean -> build -> run conformance tests

================================================================================
BINARY FORENSICS & SECTION INSPECTION:
================================================================================
  inspect <file>          Deep binary section analysis, memory budget & W^X audit
                          Flags:
                            --unwind   Decode SEH / DWARF / Compact Unwind call frames
                            --symbols  Display detailed symbol table bindings
                            --relocs   Display relocation targets and offsets
                            --verify   Run strict enterprise W^X and ABI validation gates
                            --json     Output metrics as machine-readable JSON
  readobj <file>          Inspect object file (LLVM readobj / readelf parity)
  objdump <file>          Inspect object file (Object dump and forensics)

  Examples:
    dgtl inspect build/out/module.obj
    dgtl inspect --unwind build/out/module.obj
    dgtl inspect --verify build/out/module.obj
    dgtl inspect --json build/out/module.obj

================================================================================
EXECUTION COMMANDS:
================================================================================
  run, run-all            Run full conformance suite (or single test/payload if file passed)
  run <file>              Directly compile and run any .cpp test or standalone target
  run-backend <name>      Run single backend verification
                          Options: arm64, x64, nasm, wasm, arm32, riscv64,
                                   cuda, rocm, macho, elfobj, staticlib,
                                   coffobj, cofflib
  bench-backend <name>    Benchmark backend performance
                          Usage: dgtl bench-backend x64 [profile] [pressure]
  brun                    Build and run performance benchmark suite
  test                    Execute the primary relevant test suite (automatic discovery)
  lit [options] [path]    Execute native .dgtl FileCheck & LIT test suite
  srun                    Strict conformance test (build + run)

================================================================================
CODE GENERATION & VALIDATION:
================================================================================
  gen                     Regenerate IR and ISel generated output from definitions
  isel-gen                Run standalone Multi-Target ISel generator directly
  gen-validate            Validate DGTL-Gen output integrity
  gen-build               Full cycle: gen -> validate -> build
  header-check            Verify proprietary header compliance gates
  cppcheck                Run production C++20 static analysis across include/ and src/
  debug                   Run DGTL forensic debugger for module cycles, build logs, and symbol collisions
  update-golden [strict]  Update baseline expectations for golden outputs

================================================================================
BACKEND & DISTRIBUTION:
================================================================================
  compile, compile-all    Compile frontend payloads to all 13 backends
                          Output directory: build/out/
  package                 Create SDK distribution package (DGTL_v*.zip)
                          Includes: headers, static library, binaries
                          Public SDK headers: dgtl.h

================================================================================
COMMAND ALIASES (Backward Aliasing):
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
  readobj <file>          -> inspect <file>
  objdump <file>          -> inspect <file>

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
  Compiler:               GCC g++ (auto-detected from toolchain)
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
    dgtl test               # Run automatically discovered test suites
    dgtl run tests/unit/t01_ir_stream_correctness.cpp # Run single test
    dgtl package            # Create distribution archive

  Development Workflow:
    dgtl gen-build          # IR generation + validation + build
    dgtl run-backend x64    # Quick x64 backend validation
    dgtl brun               # Performance benchmark on benchmark suites

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

int handle_build(std::vector<std::string> args = {}) {
    std::string mode_error;
    const auto parsed_mode = parse_api_mode(args, ApiMode::both, &mode_error);
    if (!parsed_mode.has_value()) {
        std::cerr << "[build] " << mode_error << std::endl;
        return 1;
    }
    if (!args.empty()) {
        std::cerr << "[build] Unknown argument: " << args.front() << std::endl;
        return 1;
    }

    if (invoke_api_policy() != 0) return 1;
    if (invoke_duplication_gate() != 0) return 1;
    
    auto prod_main = g_config.src_dir / "main.cpp";
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    auto inspect_main = g_config.src_dir / "apps" / "dgtl_inspect_main.cpp";
    auto link_main = g_config.src_dir / "apps" / "dgtl_link_main.cpp";
    auto engine_main = g_config.src_dir / "apps" / "dgtl_engine_main.cpp";
    auto ar_main = g_config.src_dir / "apps" / "dgtl_ar_main.cpp";
    auto objdump_main = g_config.src_dir / "apps" / "dgtl_objdump_main.cpp";
    auto as_main = g_config.src_dir / "apps" / "dgtl_as_main.cpp";
    
    auto prod_exe = g_config.build_dir / "dgtl_release.exe";
    std::vector<fs::path> exclude = {smoke_main, opt_main, llc_main, verify_main, inspect_main, link_main, engine_main, ar_main, objdump_main, as_main};
    
    int ec = ensure_binary("dgtl_release", prod_exe, prod_main, exclude);
    if (ec != 0) return ec;

    if (*parsed_mode == ApiMode::internal) {
        ec = build_internal_static_library();
        if (ec != 0) return ec;
        return verify_internal_api_artifacts();
    }
    if (*parsed_mode == ApiMode::public_sdk) {
        ec = build_sdk_static_library();
        if (ec != 0) return ec;
        return verify_public_api_artifacts();
    }

    ec = build_internal_static_library();
    if (ec != 0) return ec;
    ec = build_sdk_static_library();
    if (ec != 0) return ec;
    ec = verify_internal_api_artifacts();
    if (ec != 0) return ec;
    return verify_public_api_artifacts();
}

int handle_build_tools(std::vector<std::string> args = {}) {
    std::string mode_error;
    const auto parsed_mode = parse_api_mode(args, ApiMode::both, &mode_error);
    if (!parsed_mode.has_value()) {
        std::cerr << "[build-tools] " << mode_error << std::endl;
        return 1;
    }
    if (!args.empty()) {
        std::cerr << "[build-tools] Unknown argument: " << args.front() << std::endl;
        return 1;
    }

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

    if (*parsed_mode == ApiMode::internal) {
        ec = build_internal_static_library();
        if (ec != 0) return ec;
        return verify_internal_api_artifacts();
    }
    if (*parsed_mode == ApiMode::public_sdk) {
        ec = build_sdk_static_library();
        if (ec != 0) return ec;
        return verify_public_api_artifacts();
    }

    ec = build_internal_static_library();
    if (ec != 0) return ec;
    ec = build_sdk_static_library();
    if (ec != 0) return ec;
    ec = verify_internal_api_artifacts();
    if (ec != 0) return ec;
    return verify_public_api_artifacts();
}

int handle_smoke_build() {
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto prod_main = g_config.src_dir / "main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    auto inspect_main = g_config.src_dir / "apps" / "dgtl_inspect_main.cpp";
    auto link_main = g_config.src_dir / "apps" / "dgtl_link_main.cpp";
    auto engine_main = g_config.src_dir / "apps" / "dgtl_engine_main.cpp";
    auto ar_main = g_config.src_dir / "apps" / "dgtl_ar_main.cpp";
    auto objdump_main = g_config.src_dir / "apps" / "dgtl_objdump_main.cpp";
    auto as_main = g_config.src_dir / "apps" / "dgtl_as_main.cpp";
    
    auto smoke_exe = g_config.build_dir / "dgtl_main.exe";
    return ensure_binary("dgtl_main", smoke_exe, smoke_main, {prod_main, opt_main, llc_main, verify_main, inspect_main, link_main, engine_main, ar_main, objdump_main, as_main});
}

int handle_strict() {
    auto smoke_main = g_config.src_dir / "apps" / "smoke_main.cpp";
    auto prod_main = g_config.src_dir / "main.cpp";
    auto opt_main = g_config.src_dir / "apps" / "dgtl_opt_main.cpp";
    auto llc_main = g_config.src_dir / "apps" / "dgtl_llc_main.cpp";
    auto verify_main = g_config.src_dir / "apps" / "dgtl_verify_main.cpp";
    auto inspect_main = g_config.src_dir / "apps" / "dgtl_inspect_main.cpp";
    auto link_main = g_config.src_dir / "apps" / "dgtl_link_main.cpp";
    auto engine_main = g_config.src_dir / "apps" / "dgtl_engine_main.cpp";
    auto ar_main = g_config.src_dir / "apps" / "dgtl_ar_main.cpp";
    auto objdump_main = g_config.src_dir / "apps" / "dgtl_objdump_main.cpp";
    auto as_main = g_config.src_dir / "apps" / "dgtl_as_main.cpp";
    
    auto strict_exe = g_config.build_dir / "dgtl_main_strict.exe";
    return ensure_binary("dgtl_main_strict", strict_exe, smoke_main, {prod_main, opt_main, llc_main, verify_main, inspect_main, link_main, engine_main, ar_main, objdump_main, as_main}, true);
}

// -----------------------------------------------------------------------------
// Automatic Test Discovery and Dynamic Execution Engine
// -----------------------------------------------------------------------------

[[nodiscard]] std::vector<fs::path> auto_discover_test_sources(const fs::path& root_tests_dir) {
    std::vector<fs::path> discovered;
    std::error_code ec;

    const std::unordered_set<std::string> excluded_stems = {
        "benchmark_full_production_stress",
        "benchmark_hardest_e2e_gate",
        "test_suite_main"
    };

    if (file_exists(root_tests_dir)) {
        for (const auto& entry : fs::recursive_directory_iterator(root_tests_dir, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".cpp") {
                const std::string stem = entry.path().stem().string();
                if (!excluded_stems.count(stem)) {
                    discovered.push_back(entry.path());
                }
            }
        }
    }

    std::sort(discovered.begin(), discovered.end());
    return discovered;
}

int handle_single_file_run(const fs::path& target_source) {
    if (!file_exists(target_source)) {
        std::cerr << "[run] File does not exist: " << target_source.string() << std::endl;
        return 1;
    }

    if (compile_all_modules() != 0) return 1;

    const fs::path out_exe = g_config.build_dir / "tests" / (target_source.stem().string() + ".exe");
    std::cout << "[run] Preparing standalone runner for: " << target_source.filename().string() << std::endl;
    int ec = ensure_standalone_test_binary(out_exe, target_source);
    if (ec != 0) return ec;

    std::string exec_cmd = quote_path(out_exe);
    if (target_source.stem() == "DGTL_Industry_Grade_Competitive_Test_UPDATED" || target_source.stem() == "bench_competitive_suite") {
        exec_cmd += " " + quote_path(g_config.root);
    }
    return run_command(exec_cmd);
}

int handle_test_command(const std::vector<std::string>& args) {
    if (args.size() == 1 && args.front() != "full") {
        // Direct run of named test target via 'dgtl test <name>'
        fs::path candidate = args.front();
        if (candidate.extension() != ".cpp") {
            // Search dynamically in tests directory
            std::error_code ec;
            bool found = false;
            for (const auto& entry : fs::recursive_directory_iterator(g_config.root / "tests", ec)) {
                if (entry.is_regular_file(ec) && entry.path().stem() == candidate.stem()) {
                    candidate = entry.path();
                    found = true;
                    break;
                }
            }
            if (!found) {
                candidate = g_config.root / "tests" / (candidate.string() + ".cpp");
            }
        }
        return handle_single_file_run(candidate);
    }

    return 0;
}

int handle_run_backend(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::cerr << "[run] Usage: dgtl run-backend <backend>" << std::endl;
        return 1;
    }
    const fs::path smoke_exe = g_config.build_dir / "dgtl_main.exe";
    if (handle_smoke_build() != 0) return 1;
    return run_command(quote_path(smoke_exe) + " --backend " + args.front());
}

int handle_bench_backend(const std::vector<std::string>& args) {
    if (args.empty() || args.size() > 3) {
        std::cerr << "[bench] Usage: dgtl bench-backend <backend> [profile] [pressure]" << std::endl;
        return 1;
    }
    const fs::path strict_exe = g_config.build_dir / "dgtl_main_strict.exe";
    if (handle_strict() != 0) return 1;
    std::string cmd = quote_path(strict_exe) + " --backend " + args[0] + " --benchmark";
    if (args.size() >= 2) cmd += " --benchmark-profile " + args[1];
    if (args.size() == 3) cmd += " --pressure-vars " + args[2];
    return run_command(cmd);
}

int handle_strict_conformance() {
    const fs::path strict_exe = g_config.build_dir / "dgtl_main_strict.exe";
    if (handle_strict() != 0) return 1;
    const std::vector<std::string> backends = {
        "arm64", "x64", "nasm", "wasm", "arm32", "riscv64", "cuda", "rocm",
        "macho", "elfobj", "staticlib", "coffobj", "cofflib"
    };
    for (const auto& backend : backends) {
        std::cout << "[srun] Validating backend " << backend << std::endl;
        const int ec = run_command(quote_path(strict_exe) + " --backend " + backend);
        if (ec != 0) {
            std::cerr << "[srun] Backend conformance failed: " << backend << std::endl;
            return ec;
        }
    }
    std::cout << "Conformance PASSED for all backends" << std::endl;
    return 0;
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

    // 1. Automatically discover all test files across tests/ and all subdirectories
    const std::vector<fs::path> all_discovered_tests = auto_discover_test_sources(tests_root);
    std::vector<fs::path> aggregate_test_sources;
    std::vector<fs::path> standalone_tests;

    for (const auto& t : all_discovered_tests) {
        if (source_has_main(t)) {
            standalone_tests.push_back(t);
        } else {
            aggregate_test_sources.push_back(t);
        }
    }

    std::cout << "[test] Discovered " << (aggregate_test_sources.size() + standalone_tests.size())
              << " tests automatically across test hierarchy (" 
              << aggregate_test_sources.size() << " aggregate units, " 
              << standalone_tests.size() << " standalone runners)" << std::endl;

    int ec = 0;
    // 2. Build and run aggregate test suite if test_suite_main.cpp exists and has units
    if (file_exists(test_main) && !aggregate_test_sources.empty()) {
        ec = ensure_test_suite_binary(test_exe, test_main, aggregate_test_sources);
        if (ec != 0) return ec;
    }

    // 3. Compile and execute standalone tests
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

    std::vector<fs::path> test_run_deps = {g_config.src_dir, g_config.include_dir};
    if (file_exists(test_main)) test_run_deps.push_back(test_main);
    if (file_exists(test_exe))  test_run_deps.push_back(test_exe);

    for (const auto& src : aggregate_test_sources) test_run_deps.push_back(src);
    for (const auto& src : standalone_tests) test_run_deps.push_back(src);
    for (const auto& exe : standalone_exes) test_run_deps.push_back(exe);

    const auto deps_time = latest_mtime(test_run_deps);
    if (file_exists(test_success_stamp) && get_file_mtime(test_success_stamp) >= deps_time) {
        std::cout << "[test] Reusing last successful test run (no source changes detected)" << std::endl;
        return 0;
    }

    if (file_exists(test_main) && file_exists(test_exe) && !aggregate_test_sources.empty()) {
        std::cout << "[test] Running aggregate C++ suite" << std::endl;
        ec = run_command(quote_path(test_exe));
        if (ec != 0) return ec;
    }

    for (const auto& src : standalone_tests) {
        const fs::path out = g_config.build_dir / "tests" / (src.stem().string() + ".exe");
        std::cout << "[test] Running standalone " << src.filename().string() << std::endl;

        std::string standalone_cmd = quote_path(out);
        if (src.stem() == "DGTL_Industry_Grade_Competitive_Test_UPDATED" || src.stem() == "bench_competitive_suite") {
            standalone_cmd += " " + quote_path(g_config.root);
        }

        ec = run_command(standalone_cmd);
        if (ec != 0) {
            return ec;
        }
    }

    touch_file(test_success_stamp);
    return 0;
}

int handle_gen(const std::vector<std::string>& args = {}) {
    if (!args.empty()) {
        return invoke_dgtl_gen(args);
    }

    if (invoke_dgtl_gen() != 0) return 1;
    return invoke_dgtl_isel_gen();
}

int handle_gen_validate(const std::vector<std::string>& args = {}) {
    return invoke_dgtl_gen_validate(args);
}

int handle_gen_build() {
    if (invoke_dgtl_gen() != 0) return 1;
    if (invoke_dgtl_isel_gen() != 0) return 1;
    if (invoke_dgtl_gen_validate() != 0) return 1;
    if (invoke_api_policy() != 0) return 1;
    if (invoke_duplication_gate() != 0) return 1;
    return handle_build();
}

int handle_header_check() {
    return invoke_header_compliance();
}

int handle_cppcheck(const std::vector<std::string>& args = {}) {
    std::cout << "[cppcheck] Running production static analysis across include/ and src/..." << std::endl;
    ensure_directory(g_config.build_dir);
    const fs::path include_path = g_config.include_dir;
    const fs::path src_path = g_config.src_dir;
    const fs::path suppressions_file = g_config.root / "cppcheck_suppressions.txt";
    const fs::path output_file = g_config.build_dir / "cppcheckoutput.txt";

    std::string cmd = "cppcheck " + quote_path(include_path) + " " + quote_path(src_path) +
                      " --enable=warning,style,performance,portability --std=c++20 --force --platform=win64 -Dexport=\"\" -Dimport=\"\"" +
                      " --output-file=" + quote_path(output_file);

    if (file_exists(suppressions_file)) {
        cmd += " --suppressions-list=" + quote_path(suppressions_file);
    } else {
        cmd += " --suppress=missingIncludeSystem";
    }

    for (const auto& arg : args) {
        cmd += " " + arg;
    }

    int ec = run_command(cmd);
    std::cout << "[cppcheck] Analysis log saved to: " << output_file.string() << std::endl;
    return ec;
}

int handle_clean() {
    std::cout << "[clean] Removing build artifacts" << std::endl;
    std::error_code ec;
    if (fs::exists(g_config.build_dir, ec)) {
        fs::remove_all(g_config.build_dir, ec);
        std::cout << "[clean] Removed build directory: " << g_config.build_dir.string() << std::endl;
    }
    fs::path gcm_cache = g_config.root / "gcm.cache";
    if (fs::exists(gcm_cache, ec)) {
        fs::remove_all(gcm_cache, ec);
        std::cout << "[clean] Removed gcm.cache directory" << std::endl;
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
        g_config.build_dir / "dgtl-link.exe",
        g_config.build_dir / "dgtl-ar.exe",
        g_config.build_dir / "dgtl-objdump.exe",
        g_config.build_dir / "dgtl-as.exe",
        g_config.build_dir / "dgtl-inspect.exe",
        g_config.build_dir / "dgtl-engine.exe",
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

void sync_diagnostic_artifacts(int exit_code) {
    if (exit_code == 0) {
        return;
    }

    const fs::path debug_logs = g_config.root / "debug_logs";
    std::error_code ec;
    fs::create_directories(debug_logs, ec);

    const fs::path latest_md = debug_logs / "latest-error.md";
    const fs::path latest_txt = debug_logs / "latest-error.txt";
    const fs::path latest_json = debug_logs / "latest-error.json";

    const fs::path debugger_exe = g_config.build_dir / "tools" / "dgtl-debugger.exe";

    if (file_exists(debugger_exe)) {
        const std::string exe_str = quote_path(debugger_exe);
        const std::string root_str = quote_path(g_config.root);
        int ec_md = run_command(exe_str + " --root " + root_str + " --relative-paths --export " + quote_path(latest_md), false);
        int ec_txt = run_command(exe_str + " --root " + root_str + " --relative-paths --export " + quote_path(latest_txt), false);
        int ec_json = run_command(exe_str + " --root " + root_str + " --relative-paths --json --export " + quote_path(latest_json), false);
        (void)ec_md; (void)ec_txt; (void)ec_json;
    }
}

// Main entry point
int main(int argc, char** argv) {
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

    auto run_command_handler = [&]() -> int {
        if (command == "help") return handle_help();

        if (command == "build" || command == "build-release" || command == "build-tools" ||
            command == "smoke-build" || command == "build-smoke" || command == "strict" ||
            command == "build-strict" || command == "test" || command == "gen-build") {
            int ec_mod = compile_all_modules();
            if (ec_mod != 0) return ec_mod;
        }

        if (command == "build" || command == "build-release") return handle_build(args);
        if (command == "build-tools") return handle_build_tools(args);
        if (command == "smoke-build" || command == "build-smoke") return handle_smoke_build();
        if (command == "strict" || command == "build-strict") return handle_strict();
        if (command == "test") {
            return args.empty() ? handle_test() : handle_test_command(args);
        }
        if (command == "lit" || command == "dgtl-lit") return invoke_dgtl_lit(args);
        if (command == "gen") return handle_gen(args);
        if (command == "isel-gen") return invoke_dgtl_isel_gen(args);
        if (command == "gen-validate") return handle_gen_validate(args);
        if (command == "gen-build") return handle_gen_build();
        if (command == "debug") return invoke_dgtl_debugger(args);
        if (command == "header-check") return handle_header_check();
        if (command == "cppcheck" || command == "static-analysis") return handle_cppcheck(args);
        if (command == "clean" || command == "clean-all") return handle_clean();
        if (command == "prepare-api") {
            std::string mode_error;
            const auto parsed_mode = parse_api_mode(args, ApiMode::both, &mode_error);
            if (!parsed_mode.has_value()) {
                std::cerr << "[prepare-api] " << mode_error << std::endl;
                return 1;
            }
            if (!args.empty()) {
                std::cerr << "[prepare-api] Unknown argument: " << args.front() << std::endl;
                return 1;
            }
            return handle_prepare_api(*parsed_mode);
        }
        if (command == "api-status") {
            std::string mode_error;
            const auto parsed_mode = parse_api_mode(args, ApiMode::both, &mode_error);
            if (!parsed_mode.has_value()) {
                std::cerr << "[api-status] " << mode_error << std::endl;
                return 1;
            }
            if (!args.empty()) {
                std::cerr << "[api-status] Unknown argument: " << args.front() << std::endl;
                return 1;
            }
            return handle_api_status(*parsed_mode);
        }
        if (command == "inspect" || command == "readobj") {
            const fs::path inspect_exe = g_config.build_dir / "dgtl-inspect.exe";
            const fs::path inspect_tools_exe = g_config.build_dir / "tools" / "dgtl-inspect.exe";
            fs::path target_exe;
            if (file_exists(inspect_exe)) target_exe = inspect_exe;
            else if (file_exists(inspect_tools_exe)) target_exe = inspect_tools_exe;
            else {
                const fs::path inspect_source = g_config.src_dir / "apps" / "dgtl_inspect_main.cpp";
                ensure_directory(inspect_exe.parent_path());
                const std::string compile_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(inspect_source) + " -o " + quote_path(inspect_exe);
                if (run_command(compile_cmd) != 0) {
                    std::cerr << "[inspect] Failed to compile dgtl-inspect tool.\n";
                    return 1;
                }
                target_exe = inspect_exe;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "objdump" || command == "dgtl-objdump") {
            const fs::path objdump_exe = g_config.build_dir / "dgtl-objdump.exe";
            const fs::path objdump_tools_exe = g_config.build_dir / "tools" / "dgtl-objdump.exe";
            fs::path target_exe;
            if (file_exists(objdump_exe)) target_exe = objdump_exe;
            else if (file_exists(objdump_tools_exe)) target_exe = objdump_tools_exe;
            else {
                const fs::path objdump_source = g_config.src_dir / "apps" / "dgtl_objdump_main.cpp";
                ensure_directory(objdump_exe.parent_path());
                const std::string compile_cmd = g_config.gpp_path + " -std=c++26 -O3 -pipe " + quote_path(objdump_source) + " -o " + quote_path(objdump_exe);
                if (run_command(compile_cmd) != 0) {
                    std::cerr << "[objdump] Failed to compile dgtl-objdump tool.\n";
                    return 1;
                }
                target_exe = objdump_exe;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "ar" || command == "dgtl-ar") {
            const fs::path ar_exe = g_config.build_dir / "dgtl-ar.exe";
            const fs::path ar_tools_exe = g_config.build_dir / "tools" / "dgtl-ar.exe";
            fs::path target_exe;
            if (file_exists(ar_exe)) target_exe = ar_exe;
            else if (file_exists(ar_tools_exe)) target_exe = ar_tools_exe;
            else {
                std::cerr << "[ar] dgtl-ar binary not found. Run 'dgtl build-tools' first.\n";
                return 1;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "as" || command == "dgtl-as") {
            const fs::path as_exe = g_config.build_dir / "dgtl-as.exe";
            const fs::path as_tools_exe = g_config.build_dir / "tools" / "dgtl-as.exe";
            fs::path target_exe;
            if (file_exists(as_exe)) target_exe = as_exe;
            else if (file_exists(as_tools_exe)) target_exe = as_tools_exe;
            else {
                std::cerr << "[as] dgtl-as binary not found. Run 'dgtl build-tools' first.\n";
                return 1;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "link" || command == "dgtl-link") {
            const fs::path link_exe = g_config.build_dir / "dgtl-link.exe";
            const fs::path link_tools_exe = g_config.build_dir / "tools" / "dgtl-link.exe";
            fs::path target_exe;
            if (file_exists(link_exe)) target_exe = link_exe;
            else if (file_exists(link_tools_exe)) target_exe = link_tools_exe;
            else {
                const fs::path link_source = g_config.src_dir / "apps" / "dgtl_link_main.cpp";
                ensure_directory(link_exe.parent_path());
                const std::string compile_cmd = g_config.gpp_path + " -std=c++26 -fmodules-ts -O3 -pipe -I" + quote_path(g_config.include_dir) + " " + quote_path(link_source) + " -o " + quote_path(link_exe);
                if (run_command(compile_cmd) != 0) {
                    std::cerr << "[link] Failed to compile dgtl-link tool.\n";
                    return 1;
                }
                target_exe = link_exe;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "engine" || command == "dgtl-engine" || command == "jit") {
            const fs::path engine_exe = g_config.build_dir / "dgtl-engine.exe";
            const fs::path engine_tools_exe = g_config.build_dir / "tools" / "dgtl-engine.exe";
            fs::path target_exe;
            if (file_exists(engine_exe)) target_exe = engine_exe;
            else if (file_exists(engine_tools_exe)) target_exe = engine_tools_exe;
            else {
                const fs::path engine_source = g_config.src_dir / "apps" / "dgtl_engine_main.cpp";
                ensure_directory(engine_exe.parent_path());
                const std::string compile_cmd = g_config.gpp_path + " -std=c++26 -fmodules-ts -O3 -pipe -I" + quote_path(g_config.include_dir) + " " + quote_path(engine_source) + " -o " + quote_path(engine_exe);
                if (run_command(compile_cmd) != 0) {
                    std::cerr << "[engine] Failed to compile dgtl-engine tool.\n";
                    return 1;
                }
                target_exe = engine_exe;
            }
            std::string cmd = quote_path(target_exe);
            for (const auto& a : args) cmd += " " + quote_path(a);
            return run_command(cmd);
        }
        if (command == "run-backend") {
            return handle_run_backend(args);
        }
        if (command == "run" || command == "run-all") {
            if (!args.empty()) {
                // Check if user passed a source file or test target to execute directly
                const std::string& target_str = args.front();
                fs::path target_path = target_str;
                if (target_path.extension() == ".cpp" || file_exists(target_path)) {
                    return handle_single_file_run(target_path);
                }
                
                // Dynamic check in tests directory
                std::error_code ec;
                for (const auto& entry : fs::recursive_directory_iterator(g_config.root / "tests", ec)) {
                    if (entry.is_regular_file(ec) && (entry.path().stem() == target_str || entry.path().filename() == target_str)) {
                        return handle_single_file_run(entry.path());
                    }
                }
            }
            return handle_strict_conformance();
        }
        if (command == "bench" || command == "bench-backend") {
            return handle_bench_backend(args);
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
        if (command == "srun" || command == "run-strict-conformance") {
            if (!args.empty()) {
                std::cerr << "[srun] No arguments are supported" << std::endl;
                return 1;
            }
            return handle_strict_conformance();
        }

        std::cerr << "Unknown command: " << command << std::endl;
        handle_help();
        return 1;
    };

    int exit_code = run_command_handler();

    if (exit_code != 0 && command != "debug" && command != "help") {
        sync_diagnostic_artifacts(exit_code);
    }

    return exit_code;
}

std::optional<ApiMode> parse_api_mode(std::vector<std::string>& args,
                                      ApiMode default_mode,
                                      std::string* error_out) {
    ApiMode mode = default_mode;
    std::vector<std::string> passthrough;
    passthrough.reserve(args.size());

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--api" || arg == "--api-mode") {
            if (i + 1 >= args.size()) {
                if (error_out != nullptr) {
                    *error_out = "missing value for --api/--api-mode (expected internal|public|both)";
                }
                return std::nullopt;
            }
            const std::string& value = args[++i];
            if (value == "internal") {
                mode = ApiMode::internal;
            } else if (value == "public") {
                mode = ApiMode::public_sdk;
            } else if (value == "both") {
                mode = ApiMode::both;
            } else {
                if (error_out != nullptr) {
                    *error_out = "invalid --api value '" + value + "' (expected internal|public|both)";
                }
                return std::nullopt;
            }
            continue;
        }
        passthrough.push_back(arg);
    }

    args = std::move(passthrough);
    return mode;
}

int verify_internal_api_artifacts() {
    const fs::path internal_anchor = g_config.include_dir / "dgtl" / "api" / "Api_all.ixx";
    const fs::path internal_lib = g_config.build_dir / "DGTL_INTERNAL" / "lib" / "x64" / "dgtl_internal.lib";
    if (!file_exists(internal_anchor)) {
        std::cerr << "[api] Internal API not ready: missing internal API anchor "
                  << internal_anchor.string() << std::endl;
        return 1;
    }

    const fs::path gcm_cache = g_config.root / "gcm.cache";
    if (!file_exists(gcm_cache)) {
        std::cerr << "[api] Internal API not ready: missing module cache directory "
                  << gcm_cache.string() << std::endl;
        return 1;
    }

    std::error_code ec;
    bool has_module = false;
    for (const auto& entry : fs::recursive_directory_iterator(gcm_cache, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".gcm") {
            has_module = true;
            break;
        }
    }

    if (!has_module) {
        std::cerr << "[api] Internal API not ready: no compiled module artifacts found under "
                  << gcm_cache.string() << std::endl;
        return 1;
    }

    const std::vector<std::pair<std::string, std::string>> required_semantic_gcms = {
        {"dgtl.hir.semantic.gcm", "dgtl.ir_core.semantic.gcm"},
        {"dgtl.hir.type_system.gcm", "dgtl.ir_core.type_system.gcm"}
    };
    for (const auto& [canonical_gcm, legacy_gcm] : required_semantic_gcms) {
        if (!file_exists(gcm_cache / canonical_gcm) && !file_exists(gcm_cache / legacy_gcm)) {
            std::cerr << "[api] Internal API not ready: missing semantic module " << canonical_gcm << std::endl;
            return 1;
        }
    }

    if (!file_exists(internal_lib)) {
        std::cerr << "[api] Internal API not ready: missing internal linkage library "
                  << internal_lib.string() << std::endl;
        return 1;
    }

    std::cout << "[api] Internal API ready (module cache + internal library present)" << std::endl;
    return 0;
}

int verify_public_api_artifacts() {
    const fs::path sdk_include = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl.h";
    const fs::path sdk_types   = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "types.h";
    const fs::path sdk_lib     = g_config.build_dir / "DGTL_SDK" / "lib" / "x64" / "dgtl.lib";
    const fs::path sdk_config  = g_config.build_dir / "DGTL_SDK" / "bin" / "dgtl-config.exe";

    if (!file_exists(sdk_include)) {
        std::cerr << "[api] Public SDK not ready: missing namespaced header " << sdk_include.string() << std::endl;
        return 1;
    }
    if (!file_exists(sdk_types)) {
        std::cerr << "[api] Public SDK not ready: missing types header " << sdk_types.string() << std::endl;
        return 1;
    }
    if (!file_exists(sdk_lib)) {
        std::cerr << "[api] Public SDK not ready: missing library " << sdk_lib.string() << std::endl;
        return 1;
    }

    std::cout << "[api] Public SDK ready (modular headers + static library + dgtl-config present)" << std::endl;
    return 0;
}

int handle_api_status(ApiMode mode) {
    bool overall_ok = true;

    if (mode == ApiMode::internal || mode == ApiMode::both) {
        const int ec = verify_internal_api_artifacts();
        overall_ok = overall_ok && (ec == 0);
    }

    if (mode == ApiMode::public_sdk || mode == ApiMode::both) {
        const int ec = verify_public_api_artifacts();
        overall_ok = overall_ok && (ec == 0);
    }

    return overall_ok ? 0 : 1;
}

int handle_prepare_api(ApiMode mode) {
    if (mode == ApiMode::internal || mode == ApiMode::both) {
        int ec = build_internal_static_library();
        if (ec != 0) return ec;
        ec = verify_internal_api_artifacts();
        if (ec != 0) return ec;
    }

    if (mode == ApiMode::public_sdk || mode == ApiMode::both) {
        int ec = build_sdk_static_library();
        if (ec != 0) return ec;
        ec = verify_public_api_artifacts();
        if (ec != 0) return ec;
    }

    return 0;
}