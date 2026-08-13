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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kRequiredHeader =
    "Copyright (c) 2026 TataG Systems. All rights reserved.";

struct Options {
    fs::path root{"."};
    bool summary_only{false};
    bool relative_paths{false};
    int header_scan_lines{12};
};

[[nodiscard]] std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool has_allowed_extension(const fs::path& p) {
    static const std::unordered_set<std::string> kAllowed{
        ".cpp", ".ixx", ".h", ".c", ".cc", ".hh", ".hxx", ".cxx", ".inl"};
    const std::string ext = to_lower(p.extension().string());
    return kAllowed.contains(ext);
}

[[nodiscard]] bool should_exclude_path(const fs::path& p) {
    // Exclude docs and build trees from compliance scan, matching current workflow.
    for (const auto& part : p) {
        const std::string token = to_lower(part.string());
        if (token == "build" || token == "docs" || token == "my_docs") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string join_first_lines(const fs::path& file, int max_lines) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }

    std::string out;
    std::string line;
    int lines = 0;
    while (lines < max_lines && std::getline(in, line)) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out += line;
        ++lines;
    }
    return out;
}

[[nodiscard]] bool contains_required_header(const fs::path& file, int header_scan_lines) {
    const std::string head = join_first_lines(file, header_scan_lines);
    return head.find(kRequiredHeader) != std::string::npos;
}

[[nodiscard]] std::string make_display_path(const fs::path& root, const fs::path& file, bool relative_paths) {
    if (!relative_paths) {
        return file.string();
    }

    std::error_code ec;
    const fs::path rel = fs::relative(file, root, ec);
    if (ec) {
        return file.string();
    }
    return rel.generic_string();
}

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "--summary-only") {
            opts.summary_only = true;
        } else if (arg == "--relative-paths") {
            opts.relative_paths = true;
        } else if (arg == "--help") {
            std::cout
                << "DGTL proprietary header checker\n"
                << "Usage: dgtl-header-check [--root <path>] [--summary-only] [--relative-paths] [--header-scan-lines <n>]\n";
            return false;
        } else if (arg == "--root") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --root\n";
                return false;
            }
            opts.root = argv[++i];
        } else if (arg == "--header-scan-lines") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --header-scan-lines\n";
                return false;
            }
            opts.header_scan_lines = std::max(1, std::stoi(argv[++i]));
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    return true;
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
        std::cerr << "Invalid root directory: " << opts.root.string() << "\n";
        return 1;
    }

    std::vector<std::string> missing;
    std::size_t scanned = 0;

    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const fs::path p = entry.path();

        if (entry.is_directory(ec) && should_exclude_path(p)) {
            it.disable_recursion_pending();
            continue;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (!has_allowed_extension(p)) {
            continue;
        }
        if (should_exclude_path(p)) {
            continue;
        }

        ++scanned;
        if (!contains_required_header(p, opts.header_scan_lines)) {
            missing.push_back(make_display_path(root, p, opts.relative_paths));
        }
    }

    std::sort(missing.begin(), missing.end());

    if (missing.empty()) {
        std::cout << "OK: Proprietary header found in all scanned source files.\n";
        return 0;
    }

    std::cout << "Missing proprietary header in " << missing.size() << " file(s) out of " << scanned
              << " scanned:\n";
    if (!opts.summary_only) {
        for (const auto& path : missing) {
            std::cout << " - " << path << "\n";
        }
    }

    return 1;
}
