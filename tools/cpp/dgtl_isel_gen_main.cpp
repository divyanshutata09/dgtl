/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input{"include/dgtl/isa/defs/x64_core.dgtl_isel"};
    fs::path output{"include/dgtl/isa/lowering/gen/X64_ISel_Rules_Generated.hpp"};
    fs::path root{"."};
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--root") {
            if (i + 1 >= argc) return false;
            opts.root = argv[++i];
        } else if (arg == "--input") {
            if (i + 1 >= argc) return false;
            opts.input = argv[++i];
        } else if (arg == "--output") {
            if (i + 1 >= argc) return false;
            opts.output = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: dgtl-isel-gen [--root <repo>] [--input <file>] [--output <file>]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string trim(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && is_ws(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

[[nodiscard]] std::string cpp_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out.push_back(c);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        std::cerr << "Invalid arguments. Use --help.\n";
        return 1;
    }

    const fs::path root = fs::weakly_canonical(opts.root);
    const fs::path input_file = root / opts.input;
    const fs::path output_file = root / opts.output;

    std::ifstream in(input_file);
    if (!in.is_open()) {
        std::cerr << "Missing ISel DSL file: " << input_file.string() << "\n";
        return 1;
    }

    std::vector<std::string> rules;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.rfind("rule ", 0) == 0) {
            rules.push_back(std::move(t));
        }
    }

    fs::create_directories(output_file.parent_path());
    std::ofstream out(output_file, std::ios::binary);
    out << "/* Auto-generated from include/dgtl/isa/defs/x64_core.dgtl_isel. Do not edit manually. */\n";
    out << "#pragma once\n\n";
    out << "#include <array>\n#include <string_view>\n\n";
    out << "namespace dgtl::isa::lowering::generated {\n\n";
    out << "inline constexpr std::array<std::string_view, " << rules.size() << "> kX64CoreRuleDslLines = {\n";
    for (std::size_t i = 0; i < rules.size(); ++i) {
        out << "    \"" << cpp_escape(rules[i]) << "\"";
        out << (i + 1 < rules.size() ? ",\n" : "\n");
    }
    out << "};\n\n} // namespace dgtl::isa::lowering::generated\n";

    std::cout << "[dgtl-isel-gen] Generated " << opts.output.string()
              << " from " << opts.input.string()
              << " with " << rules.size() << " rules\n";
    return 0;
}
