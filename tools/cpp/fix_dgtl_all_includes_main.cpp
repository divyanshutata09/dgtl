/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Rule {
    std::string path;
    std::string replacement;
};

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_all(const fs::path& p, std::string_view content) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

int fix_file(const fs::path& repo, const Rule& r) {
    const fs::path file = repo / fs::path(r.path);
    if (!fs::exists(file)) return 0;

    std::string content = read_all(file);
    const std::string pattern = "#include \"dgtl/dgtl_all.hpp\"";
    const std::size_t pos = content.find(pattern);
    if (pos == std::string::npos) return 0;

    content.replace(pos, pattern.size(), r.replacement);
    if (!write_all(file, content)) {
        std::cerr << "Failed to write: " << file.string() << "\n";
        return 0;
    }
    std::cout << "Fixed: " << r.path << "\n";
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path repo = fs::current_path();
    if (argc > 1) repo = argv[1];

    const std::string r2 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r3 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r4 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r5 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r6 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r7 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r8 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r9 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r10 = "import dgtl.arena;\nimport dgtl.ir_stream;";
    const std::string r11 = "import dgtl.ir_stream;";

    const std::vector<Rule> rules = {
        {"tests/functional/pipeline/test_pipeline_0.cpp", r3},
        {"tests/latency/fractal_torture_test.cpp", r3},
        {"tests/functional/pipeline/test_pipeline_20_the_gauntlet.cpp", r7},
        {"tests/latency/reg_exhaustion_test.cpp", r10},
        {"tests/performance/stress/X64_Emitter_Test.cpp", r11}
    };

    int total = 0;
    for (const auto& r : rules) total += fix_file(repo, r);
    std::cout << "Total files fixed: " << total << "\n";
    return 0;
}
