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
#include <unordered_map>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <atomic>
#include <numeric>

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

enum class ApiMode {
    internal,
    public_sdk,
    both
};

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

std::optional<ApiMode> parse_api_mode(std::vector<std::string>& args,
                                      ApiMode default_mode,
                                      std::string* error_out = nullptr) {
    ApiMode mode = default_mode;
    std::vector<std::string> passthrough;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--api" || args[i] == "--api-mode") {
            if (i + 1 >= args.size()) {
                if (error_out) {
                    *error_out = "missing value for --api/--api-mode (expected internal|public|both)";
                }
                return std::nullopt;
            }

            const std::string value = args[++i];
            if (value == "internal") {
                mode = ApiMode::internal;
            } else if (value == "public") {
                mode = ApiMode::public_sdk;
            } else if (value == "both") {
                mode = ApiMode::both;
            } else {
                if (error_out) {
                    *error_out = "invalid --api value '" + value + "' (expected internal|public|both)";
                }
                return std::nullopt;
            }
            continue;
        }

        passthrough.push_back(args[i]);
    }

    args = std::move(passthrough);
    return mode;
}

[[nodiscard]] bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

[[nodiscard]] fs::file_time_type latest_mtime(const std::vector<fs::path>& paths) {
    auto newest = fs::file_time_type::min();
    std::error_code ec;
    for (const auto& path : paths) {
        if (file_exists(path)) {
            if (fs::is_directory(path, ec)) {
                for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
                    if (entry.is_regular_file(ec)) {
                        newest = std::max(newest, get_file_mtime(entry.path()));
                    }
                }
            } else {
                newest = std::max(newest, get_file_mtime(path));
            }
        }
    }
    return newest;
}

[[nodiscard]] fs::path make_obj_path(const fs::path& obj_dir, const fs::path& src_path) {
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

int verify_internal_api_artifacts() {
    const fs::path internal_anchor = g_config.include_dir / "dgtl" / "api" / "Api_all.ixx";
    const fs::path internal_lib = g_config.build_dir / "DGTL_INTERNAL" / "lib" / "x64" / "dgtl_internal.lib";
    const fs::path gcm_cache = g_config.root / "gcm.cache";

    if (!file_exists(internal_anchor)) {
        std::cerr << "[api] Internal API not ready: missing internal API anchor " << internal_anchor.string() << "\n";
        return 1;
    }
    if (!file_exists(gcm_cache)) {
        std::cerr << "[api] Internal API not ready: missing module cache directory " << gcm_cache.string() << "\n";
        return 1;
    }
    if (!file_exists(internal_lib)) {
        std::cerr << "[api] Internal API not ready: missing internal linkage library " << internal_lib.string() << "\n";
        return 1;
    }

    bool has_module = false;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(gcm_cache, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".gcm") {
            has_module = true;
            break;
        }
    }
    if (!has_module) {
        std::cerr << "[api] Internal API not ready: no compiled module artifacts found under " << gcm_cache.string() << "\n";
        return 1;
    }

    std::cout << "[api] Internal API ready (module cache + internal library present)\n";
    return 0;
}

int verify_public_api_artifacts() {
    const fs::path sdk_include = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl.h";
    const fs::path sdk_c_api = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_c_api.h";
    const fs::path sdk_core = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_core.h";
    const fs::path sdk_gpu = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_gpu.h";
    const fs::path sdk_simd = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_simd.h";
    const fs::path sdk_capabilities = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_capabilities.h";
    const fs::path sdk_session = g_config.build_dir / "DGTL_SDK" / "include" / "dgtl_sdk" / "dgtl_session.h";
    const fs::path sdk_lib = g_config.build_dir / "DGTL_SDK" / "lib" / "x64" / "dgtl.lib";

    if (!file_exists(sdk_include)) {
        std::cerr << "[api] Public SDK not ready: missing namespaced header " << sdk_include.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_c_api)) {
        std::cerr << "[api] Public SDK not ready: missing C ABI header " << sdk_c_api.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_core)) {
        std::cerr << "[api] Public SDK not ready: missing core definitions header " << sdk_core.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_gpu)) {
        std::cerr << "[api] Public SDK not ready: missing GPU header " << sdk_gpu.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_simd)) {
        std::cerr << "[api] Public SDK not ready: missing SIMD header " << sdk_simd.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_capabilities)) {
        std::cerr << "[api] Public SDK not ready: missing capability header " << sdk_capabilities.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_session)) {
        std::cerr << "[api] Public SDK not ready: missing session header " << sdk_session.string() << "\n";
        return 1;
    }
    if (!file_exists(sdk_lib)) {
        std::cerr << "[api] Public SDK not ready: missing library " << sdk_lib.string() << "\n";
        return 1;
    }

    std::cout << "[api] Public SDK ready (headers + static library present)\n";
    return 0;
}

int handle_api_status(ApiMode mode) {
    bool overall_ok = true;
    if (mode == ApiMode::internal || mode == ApiMode::both) {
        overall_ok = (verify_internal_api_artifacts() == 0) && overall_ok;
    }
    if (mode == ApiMode::public_sdk || mode == ApiMode::both) {
        overall_ok = (verify_public_api_artifacts() == 0) && overall_ok;
    }
    return overall_ok ? 0 : 1;
}

// -----------------------------------------------------------------------------
// Unified Compilation Engine & Topological Dependency Resolution
// -----------------------------------------------------------------------------

struct CompileOptions {
    std::string name;
    fs::path output;
    std::vector<fs::path> sources;
    std::vector<fs::path> includes;
};


std::vector<fs::path> get_release_sources() {
    std::vector<fs::path> sources = {g_config.src_dir / "main.cpp"};
    std::unordered_set<std::string> excluded = {
        (g_config.src_dir / "apps" / "smoke_main.cpp").lexically_normal().string(),
        (g_config.src_dir / "apps" / "dgtl_llc_main.cpp").lexically_normal().string(),
        (g_config.src_dir / "apps" / "dgtl_opt_main.cpp").lexically_normal().string(),
        (g_config.src_dir / "apps" / "dgtl_verify_main.cpp").lexically_normal().string()
    };

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(g_config.src_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
            std::string norm = fs::absolute(entry.path()).lexically_normal().string();
            if (norm != fs::absolute(g_config.src_dir / "main.cpp").lexically_normal().string() && !excluded.count(norm)) {
                sources.push_back(entry.path());
            }
        }
    }
    const fs::path gpu_runtime_cpp = g_config.include_dir / "dgtl" / "runtime" / "dgtl_gpu.cpp";
    if (file_exists(gpu_runtime_cpp)) {
        sources.push_back(gpu_runtime_cpp);
    }
    return sources;
}

struct ModuleUnit {
    fs::path path;
    std::string module_name;
    std::string partition_name;
    bool is_partition = false;
    bool is_master = false;
    std::vector<std::string> imports;
};

// Pure deterministic token scanner pulls parsing signatures safely
void parse_module_metadata(ModuleUnit& unit) {
    std::ifstream file(unit.path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Strip trailing carriage return if parsing on Windows environments
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
            
            // Clean up any edge spaces or trailing pollution bytes
            while(!token.empty() && (token.back() == ' ' || token.back() == ';')) {
                token.pop_back();
            }
            if (!token.empty()) {
                unit.imports.push_back(token);
            }
        }
    }
}

// Master DAG Post-Order traversal handles building the stack loop trace smoothly
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

        // Ensure partitions of the same module are sequenced properly before the master interface unit
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
            
            // Handle internal short-form partition imports (e.g., ":fwd")
            if (!raw_import.empty() && raw_import.front() == ':') {
                target_key = unit.module_name + raw_import;
            }
            
            // Fix: If a file imports a full module name that is actually registered as an interface 
            // partition inside our database, resolve the dependency mapping cleanly.
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

int compile_all_modules() {
    const fs::path mod_obj_dir = g_config.build_dir / "obj" / "modules";
    ensure_directory(mod_obj_dir);

    std::unordered_map<std::string, ModuleUnit> module_database;
    std::vector<std::string> scan_roots;
    std::error_code ec_scan;

    // Discover files dynamically from drive topology
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

    // Stabilize array via lexical sorting order baseline
    std::sort(scan_roots.begin(), scan_roots.end());

    std::vector<ModuleUnit> deployment_pipeline;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> active_stack;

    for (const auto& root_key : scan_roots) {
        resolve_topological_dfs(root_key, module_database, visited, active_stack, deployment_pipeline);
    }

    // Process our mathematically ordered pipeline stream cleanly
    for (const auto& module_unit : deployment_pipeline) {
        const fs::path& src = module_unit.path;
        std::string rel_path = fs::relative(src, g_config.root).string();
        
        // Fix: Use the detailed relative path token footprint to avoid cross-directory naming collision caking
        fs::path rel = fs::relative(src, g_config.include_dir);
        std::string stem = rel.parent_path().string() + "/" + rel.stem().string();
        std::replace(stem.begin(), stem.end(), '\\', '_');
        std::replace(stem.begin(), stem.end(), '/', '_');
        std::replace(stem.begin(), stem.end(), '.', '_');
        const fs::path obj = mod_obj_dir / (stem + ".o");

        const auto src_time = get_file_mtime(src);
        const auto obj_time = get_file_mtime(obj);

        std::error_code ec_size;
        const bool needs_compile = !file_exists(obj) || (fs::file_size(obj, ec_size) == 0) || src_time > obj_time;
        if (!needs_compile) {
            continue;
        }

        std::cout << "[build] Compiling module: " << rel_path << "..." << std::endl;
        std::string cmd = g_config.compiler_path;
        cmd += " -std=c++26 -fmodules-ts -O2 -pipe -I" + quote_path(g_config.include_dir) + " -c " + quote_path(src) + " -o " + quote_path(obj);
        
        int ec = run_cmd(cmd, false);
        if (ec != 0) {
            std::cerr << "[build] Failed to compile module: " << rel_path << std::endl;
            return ec;
        }
    }
    return 0;
}

[[nodiscard]] BuildResult invoke_compiler(const CompileOptions& opts) {
    ensure_directory(opts.output.parent_path());
    const fs::path obj_dir = g_config.build_dir / "obj" / opts.name;
    ensure_directory(obj_dir);

    const unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<bool> compile_failed{false};
    std::atomic<int> compiled_count{0};
    std::vector<fs::path> objs(opts.sources.size());
    std::atomic<std::size_t> current_work_idx{0};

    auto compile_single_obj = [&](const fs::path& src, const fs::path& obj) -> int {
        const auto src_time = get_file_mtime(src);
        const auto include_time = latest_mtime({g_config.include_dir});
        const auto obj_time = get_file_mtime(obj);

        std::error_code ec_size;
        const bool needs_compile = !file_exists(obj) || (fs::file_size(obj, ec_size) == 0) || src_time > obj_time || include_time > obj_time;
        if (!needs_compile) return 0;

        ensure_directory(obj.parent_path());
        CommandBuilder cmd(g_config.compiler_path);
        cmd.add("-std=c++26 -fmodules-ts -O2 -pipe");
        for (const auto& inc : opts.includes) cmd.add("-I").add_path(inc);
        cmd.add("-c").add_path(src).add("-o").add_path(obj);

        return run_cmd(cmd.build(), false);
    };

    auto worker = [&]() {
        while (!compile_failed.load(std::memory_order_relaxed)) {
            std::size_t idx = current_work_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= opts.sources.size()) break;

            const auto& src = opts.sources[idx];
            const fs::path obj = make_obj_path(obj_dir, src);
            const auto before = get_file_mtime(obj);
            int ec = compile_single_obj(src, obj);
            if (ec != 0) {
                compile_failed.store(true, std::memory_order_relaxed);
                return;
            }
            if (get_file_mtime(obj) > before) {
                compiled_count.fetch_add(1, std::memory_order_relaxed);
            }
            objs[idx] = obj;
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
    if (compile_failed.load()) return BuildResult::Error;

    std::error_code ec_out;
    const bool output_missing = !file_exists(opts.output) || (fs::file_size(opts.output, ec_out) == 0);
    const auto out_time = get_file_mtime(opts.output);
    bool needs_link = output_missing || compiled_count.load() > 0;
    if (!needs_link) {
        for (const auto& obj : objs) {
            if (get_file_mtime(obj) > out_time) {
                needs_link = true;
                break;
            }
        }
    }

    if (!needs_link) {
        return BuildResult::UpToDate;
    }

    std::cout << "[build] Linking " << opts.name << "\n";
    CommandBuilder link_cmd(g_config.compiler_path);
    link_cmd.add("-std=c++26 -fmodules-ts -O2 -pipe");
    for (const auto& obj : objs) link_cmd.add_path(obj);

    if (opts.output.extension() == ".exe") {
        std::error_code ec_mod;
        fs::path mod_obj_dir = g_config.build_dir / "obj" / "modules";
        if (fs::exists(mod_obj_dir, ec_mod)) {
            for (const auto& entry : fs::directory_iterator(mod_obj_dir, ec_mod)) {
                if (entry.is_regular_file() && entry.path().extension() == ".o") {
                    link_cmd.add_path(entry.path());
                }
            }
        }
    }

    link_cmd.add("-o").add_path(opts.output);
    std::string final_cmd = link_cmd.build();

#ifdef _WIN32
    if (final_cmd.size() > 7000) {
        const fs::path rsp_path = g_config.build_dir / "obj" / (opts.name + "_link.rsp");
        ensure_directory(rsp_path.parent_path());

        const std::size_t prefix_len = g_config.compiler_path.size();
        std::string rsp_args = (final_cmd.size() > prefix_len) ? final_cmd.substr(prefix_len) : std::string{};
        if (!rsp_args.empty() && rsp_args.front() == ' ') {
            rsp_args.erase(rsp_args.begin());
        }

        std::ofstream rsp_out(rsp_path, std::ios::binary | std::ios::trunc);
        if (rsp_out.is_open()) {
            rsp_out << rsp_args;
            rsp_out.close();
            final_cmd = g_config.compiler_path + " @" + quote_path(rsp_path);
        }
    }
#endif

    int ec = run_cmd(final_cmd);
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
              << "Commands: build, build-tools, run, test, gen, gen-validate, gen-build, header-check, api-policy, dup-gate, api-status\n"
              << "  api-status --api internal|public|both  Verify DGTL API lane readiness\n"
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
        {"debug",        {g_config.tools_dir / "dgtl_debugger_main.cpp", {"--root", quote_path(g_config.root), "--relative-paths"}}},
        {"gen",          {g_config.tools_dir / "dgtl_gen_main.cpp", {}}},
        {"gen-validate", {g_config.tools_dir / "validate_dgtl_gen_main.cpp", {}}},
        {"isel-gen",     {g_config.tools_dir / "dgtl_isel_gen_main.cpp", {}}},
        {"rodata-maker", {g_config.tools_dir / "dgtl_rodata_maker_main.cpp", {}}}
    };

    if (command == "help") {
        show_usage();
        return 0;
    }

    if (command == "api-status") {
        std::string mode_error;
        const auto parsed_mode = parse_api_mode(args, ApiMode::both, &mode_error);
        if (!parsed_mode.has_value()) {
            std::cerr << "[api-status] " << mode_error << "\n";
            return 1;
        }
        if (!args.empty()) {
            std::cerr << "[api-status] Unknown argument: " << args.front() << "\n";
            return 1;
        }
        return handle_api_status(*parsed_mode);
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
        if (compile_all_modules() != 0) return 1;
        if (invoke_tool("api-policy", tool_map["api-policy"].src, tool_map["api-policy"].default_args) != 0) return 1;
        if (invoke_tool("dup-gate", tool_map["dup-gate"].src, tool_map["dup-gate"].default_args) != 0) return 1;
        
        CompileOptions main_build{
            .name = "dgtl_release",
            .output = g_config.build_dir / "dgtl_release.exe",
            .sources = get_release_sources(),
            .includes = {g_config.include_dir}
        };
        return invoke_compiler(main_build) == BuildResult::Error ? 1 : 0;
    }

    if (command == "gen-build") {
        if (compile_all_modules() != 0) return 1;
        if (invoke_tool("gen", tool_map["gen"].src) != 0) return 1;
        if (invoke_tool("gen-validate", tool_map["gen-validate"].src) != 0) return 1;
        if (invoke_tool("api-policy", tool_map["api-policy"].src, tool_map["api-policy"].default_args) != 0) return 1;
        if (invoke_tool("dup-gate", tool_map["dup-gate"].src, tool_map["dup-gate"].default_args) != 0) return 1;

        CompileOptions main_build{
            .name = "dgtl_release",
            .output = g_config.build_dir / "dgtl_release.exe",
            .sources = get_release_sources(),
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