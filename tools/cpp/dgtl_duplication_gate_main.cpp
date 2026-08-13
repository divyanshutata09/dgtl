/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
            if (i + 1 >= argc) return false;
            opts.root = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: dgtl-dup-gate [--root <repo>] [--summary-only]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool is_source_ext(const fs::path& p) {
    static const std::unordered_set<std::string> exts = {
        ".cpp", ".hpp", ".ixx", ".h", ".hh", ".hxx", ".c", ".cc", ".cxx", ".inl", ".ps1"};
    return exts.contains(to_lower(p.extension().string()));
}

[[nodiscard]] bool skip_path(const fs::path& p) {
    for (const auto& part : p) {
        const std::string token = to_lower(part.string());
        if (token == "build" || token == ".git" || token == ".vs" || token == "out") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string content_hash_hex(std::string_view text) {
    // FNV-1a 64-bit (portable and fast enough for duplicate bucketing here).
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : text) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

[[nodiscard]] bool regex_contains(std::string_view text, const std::string& pattern) {
    try {
        const std::regex re(pattern);
        return std::regex_search(text.begin(), text.end(), re);
    } catch (...) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        std::cerr << "Invalid arguments. Use --help.\n";
        return 1;
    }

    const fs::path root = fs::weakly_canonical(opts.root);
    std::vector<std::string> failures;

    // 1) Exact duplicate content check.
    std::vector<fs::path> sources;
    const std::vector<fs::path> scan_roots = {root / "include", root / "src", root / "tests", root / "tools"};
    for (const auto& base : scan_roots) {
        if (!fs::exists(base)) continue;
        for (auto it = fs::recursive_directory_iterator(base), end = fs::recursive_directory_iterator(); it != end; ++it) {
            if (!it->is_regular_file()) continue;
            const fs::path p = it->path();
            if (!is_source_ext(p)) continue;
            if (skip_path(p)) continue;
            sources.push_back(p);
        }
    }

    std::unordered_map<std::string, std::vector<fs::path>> hash_buckets;
    for (const auto& file : sources) {
        const auto hash = content_hash_hex(read_all(file));
        hash_buckets[hash].push_back(file);
    }

    for (const auto& [_, files] : hash_buckets) {
        if (files.size() <= 1) continue;
        std::string msg = "exact duplicate files detected in source scope: ";
        for (std::size_t i = 0; i < files.size(); ++i) {
            std::error_code ec;
            const auto rel = fs::relative(files[i], root, ec);
            msg += ec ? files[i].string() : rel.generic_string();
            if (i + 1 < files.size()) msg += ", ";
        }
        failures.push_back(std::move(msg));
    }

    const std::vector<Rule> rules = {
        {"src/main.cpp", "parse_backend_driver_target\\(", "main backend parsing must route through driver API parser", true},
        {"src/apps/smoke_cli_and_options.inl", "parse_backend_driver_target\\(", "smoke backend parsing must route through driver API parser", true},
        {"src/main.cpp", "backend_str\\s*==\\s*\"", "main must not reintroduce local backend alias table", false},
        {"src/apps/smoke_cli_and_options.inl", "backend_str\\s*==\\s*\"", "smoke must not reintroduce local backend alias table", false},
        {"src/main.cpp", "Myzon_Intrinsics\\.(hpp|ixx)|dgtl::integration::myzon::", "main must not directly couple to legacy frontend intrinsics APIs", false},
        {"src/sdk/dgtl_sdk_impl.cpp", "Myzon_Intrinsics\\.(hpp|ixx)|dgtl::integration::myzon::", "sdk payload path must not directly couple to legacy frontend intrinsics APIs", false},

        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "parse_polyhedral_mode\\(", "shared utils must own polyhedral mode parser implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "parse_polyhedral_parallel_policy\\(", "shared utils must own polyhedral parallel parser implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "bytes_to_hex\\(", "shared utils must own bytes-to-hex implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "write_text_file\\(", "shared utils must own text-file writer implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "write_binary_file\\(", "shared utils must own binary-file writer implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "emit_arm32_text\\(", "shared utils must own ARM32 helper implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "emit_riscv64_text\\(", "shared utils must own RISC-V helper implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "emit_cuda_text\\(", "shared utils must own CUDA helper implementation", true},
        {"include/dgtl/backend/driver/Backend_Driver_Shared_Utils.ixx", "emit_rocm_text\\(", "shared utils must own ROCm helper implementation", true},

        {"src/main.cpp", "driver::shared::parse_polyhedral_mode\\(", "main must consume shared polyhedral mode parser", true},
        {"src/main.cpp", "driver::shared::parse_polyhedral_parallel_policy\\(", "main must consume shared polyhedral parallel parser", true},
        {"src/apps/smoke_cli_and_options.inl", "driver::shared::parse_polyhedral_mode\\(", "smoke must consume shared polyhedral mode parser", true},
        {"src/apps/smoke_cli_and_options.inl", "driver::shared::parse_polyhedral_parallel_policy\\(", "smoke must consume shared polyhedral parallel parser", true},
        {"src/main.cpp", "driver::shared::write_text_file\\(", "main must consume shared text writer", true},
        {"src/main.cpp", "driver::shared::write_binary_file\\(", "main must consume shared binary writer", true},
        {"src/apps/smoke_emitters_and_io.inl", "driver::shared::write_text_file\\(", "smoke must consume shared text writer", true},
        {"src/apps/smoke_emitters_and_io.inl", "driver::shared::write_binary_file\\(", "smoke must consume shared binary writer", true},

        {"src/main.cpp", "mode_str\\s*==\\s*\"off\"|mode_str\\s*==\\s*\"safe\"|mode_str\\s*==\\s*\"aggressive\"", "main must not duplicate polyhedral mode literal parser", false},
        {"src/apps/smoke_cli_and_options.inl", "mode_str\\s*==\\s*\"off\"|mode_str\\s*==\\s*\"safe\"|mode_str\\s*==\\s*\"aggressive\"", "smoke must not duplicate polyhedral mode literal parser", false},
        {"src/main.cpp", "policy_str\\s*==\\s*\"auto\"|policy_str\\s*==\\s*\"off\"|policy_str\\s*==\\s*\"on\"", "main must not duplicate polyhedral parallel literal parser", false},
        {"src/apps/smoke_cli_and_options.inl", "policy_str\\s*==\\s*\"auto\"|policy_str\\s*==\\s*\"off\"|policy_str\\s*==\\s*\"on\"", "smoke must not duplicate polyhedral parallel literal parser", false},
        {"src/main.cpp", "0123456789ABCDEF", "main must not duplicate bytes-to-hex table", false},
        {"src/apps/smoke_emitters_and_io.inl", "0123456789ABCDEF", "smoke emitters must not duplicate bytes-to-hex table", false},
        {"src/main.cpp", "\\.arch armv7-a|\\.arch rv64gcv|DeviceType::NVIDIA_PTX|DeviceType::AMD_GCN_CDNA", "main must not duplicate shared ARM32/RISCV/CUDA/ROCM helper bodies", false},
        {"src/apps/smoke_emitters_and_io.inl", "\\.arch armv7-a|\\.arch rv64gcv|DeviceType::NVIDIA_PTX|DeviceType::AMD_GCN_CDNA", "smoke must not duplicate shared ARM32/RISCV/CUDA/ROCM helper bodies", false}
    };

    for (const auto& rule : rules) {
        const fs::path file = root / rule.relative_path;
        if (!fs::exists(file)) {
            failures.push_back("cannot validate '" + rule.description + "' because file is missing: " + rule.relative_path);
            continue;
        }
        const std::string content = read_all(file);
        const bool matched = regex_contains(content, rule.pattern);
        if (rule.must_match && !matched) {
            failures.push_back("missing expected duplication-policy pattern '" + rule.description + "' in " + rule.relative_path);
        } else if (!rule.must_match && matched) {
            failures.push_back("forbidden duplication-policy pattern '" + rule.description + "' found in " + rule.relative_path);
        }
    }

    if (!failures.empty()) {
        std::cout << "[dup-gate] FAIL\n";
        for (const auto& f : failures) {
            std::cout << "  - " << f << "\n";
        }
        return 1;
    }

    if (!opts.summary_only) {
        std::cout << "[dup-gate] PASS\n";
    }
    return 0;
}
