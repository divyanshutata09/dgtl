/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

[[nodiscard]] bool contains(std::string_view text, std::string_view token) {
    return text.find(token) != std::string_view::npos;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path root = fs::current_path();
    if (argc > 1) {
        root = fs::path(argv[1]);
    }
    root = fs::weakly_canonical(root);

    const fs::path gen_header = root / "include/dgtl/ir/gen/DGTL_Gen_Ops.hpp";
    if (!fs::exists(gen_header)) {
        std::cerr << "Missing generated header: " << gen_header.string() << "\n";
        return 1;
    }

    std::cout << "\n[DGTL-Gen Validation]\n\n";
    const std::uintmax_t bytes = fs::file_size(gen_header);
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::cout << "[1] Validating generated header...\n";
    std::cout << "    => Size: " << mb << " MB\n";

    const std::string content = read_all(gen_header);
    std::cout << "\n[2] Checking required generated API...\n";
    const std::vector<std::string> required_checks = {
        "GenTrait",
        "GenInterface",
        "GenAttribute",
        "kGeneratedOpDefs",
        "find_generated_op_def",
        "op_has_attribute",
        "op_has_trait",
        "op_implements_interface",
        "verify_generated_node",
        "make_generated_node",
        "kNumGeneratedOps",
        "kNumTraits",
        "kNumInterfaces",
        "kNumAttributes",
        "kNumDialects",
        "kNumTypes"
    };

    bool ok = true;
    for (const auto& check : required_checks) {
        if (contains(content, check)) {
            std::cout << "    [OK] " << check << "\n";
        } else {
            std::cout << "    [FAIL] Missing: " << check << "\n";
            ok = false;
        }
    }

    std::cout << "\n[3] Artifact Counts\n";
    const std::regex builder_re(R"(struct\s+\w+Op\s*\{)");
    const auto begin = std::sregex_iterator(content.begin(), content.end(), builder_re);
    const auto end = std::sregex_iterator();
    const auto count = static_cast<std::size_t>(std::distance(begin, end));
    std::cout << "    => Builders: " << count << "\n";

    if (count == 0) {
        std::cout << "    [FAIL] No generated typed wrappers were found\n";
        ok = false;
    }

    std::cout << "\n[4] Semantic bit sanity checks\n";
    const std::regex op_re(
        R"DGTL(\{"([^"]+)",\s*dgtl::ir::OpCode::[A-Z0-9_]+,\s*"[^"]*",\s*([0-9]+)ULL,\s*([0-9]+)ULL,\s*([0-9]+)ULL,)DGTL"
    );

    bool saw_add_i32 = false;
    bool saw_load = false;
    bool saw_branch = false;

    for (auto it = std::sregex_iterator(content.begin(), content.end(), op_re); it != std::sregex_iterator(); ++it) {
        const std::string op_name = (*it)[1].str();
        const auto trait_bits = static_cast<unsigned long long>(std::stoull((*it)[2].str()));
        const auto interface_bits = static_cast<unsigned long long>(std::stoull((*it)[3].str()));

        if (op_name == "AddI32") {
            saw_add_i32 = true;
            if (trait_bits == 0ULL || interface_bits == 0ULL) {
                std::cout << "    [FAIL] AddI32 has zero trait/interface bits\n";
                ok = false;
            }
        }
        if (op_name == "Load") {
            saw_load = true;
            if (trait_bits == 0ULL || interface_bits == 0ULL) {
                std::cout << "    [FAIL] Load has zero trait/interface bits\n";
                ok = false;
            }
        }
        if (op_name == "Branch") {
            saw_branch = true;
            if (trait_bits == 0ULL || interface_bits == 0ULL) {
                std::cout << "    [FAIL] Branch has zero trait/interface bits\n";
                ok = false;
            }
        }
    }

    if (!saw_add_i32) {
        std::cout << "    [FAIL] Could not find AddI32 metadata entry\n";
        ok = false;
    }
    if (!saw_load) {
        std::cout << "    [FAIL] Could not find Load metadata entry\n";
        ok = false;
    }
    if (!saw_branch) {
        std::cout << "    [FAIL] Could not find Branch metadata entry\n";
        ok = false;
    }

    if (!ok) {
        std::cout << "\n[FAIL] DGTL-Gen Validation Failed\n";
        return 1;
    }

    std::cout << "\n[SUCCESS] DGTL-Gen Validation Complete\n";
    return 0;
}
