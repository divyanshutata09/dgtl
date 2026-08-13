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

[[nodiscard]] bool file_omits(std::string_view text, std::string_view token) {
    return text.find(token) == std::string_view::npos;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path root = fs::current_path();
    if (argc > 1) {
        root = fs::path(argv[1]);
    }
    root = fs::weakly_canonical(root);

    const fs::path gen_header = root / "include/dgtl/ir/gen/DGTL_Gen_Ops.ixx";
    const fs::path ir_ssa = root / "include/dgtl/ir/base/IR_SSA.ixx";
    const fs::path pattern_rewriter = root / "include/dgtl/ir/rewrite/Pattern_Rewriter.ixx";
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
        "kNumTypes",
        "validate_generated_op_defs"
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
    const std::regex builder_re(R"(struct\s+\w+Op(?:\s*:\s*[^{]+)?\s*\{)");
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
    bool saw_string_const = false;
    bool saw_string_concat = false;

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
        if (op_name == "StringConst") {
            saw_string_const = true;
        }
        if (op_name == "StringConcat") {
            saw_string_concat = true;
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

    if (!saw_string_const) {
        std::cout << "    [FAIL] Could not find StringConst metadata entry\n";
        ok = false;
    }
    if (!saw_string_concat) {
        std::cout << "    [FAIL] Could not find StringConcat metadata entry\n";
        ok = false;
    }

    std::cout << "\n[5] Dialect propagation checks\n";
    const std::vector<std::string> required_dialect_checks = {
        "{\"Load\", dgtl::ir::OpCode::LOAD, \"core\"",
        "{\"StringConst\", dgtl::ir::OpCode::STRING_CONST, \"core\"",
        "{\"GpuLaunch\", dgtl::ir::OpCode::GPU_LAUNCH, \"gpu\"",
        "{\"GatherF32x512\", dgtl::ir::OpCode::GATHER_F32_512, \"simd\""
    };
    for (const auto& check : required_dialect_checks) {
        if (contains(content, check)) {
            std::cout << "    [OK] " << check << "\n";
        } else {
            std::cout << "    [FAIL] Missing dialect marker: " << check << "\n";
            ok = false;
        }
    }

    std::cout << "\n[6] Ownership surface checks\n";
    const std::string ssa_content = read_all(ir_ssa);
    const std::string rewriter_content = read_all(pattern_rewriter);
    const std::string shadow_lookup_decl = "[[nodiscard]] const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept;";
    const std::string shadow_verify_decl = "[[nodiscard]] bool verify_generated_node(const dgtl::ir::IRNode& node) noexcept;";
    const std::string ownership_marker = "Sole owning export surface for generated op metadata lookup helpers.";

    if (contains(content, ownership_marker)) {
        std::cout << "    [OK] Generated ownership marker present\n";
    } else {
        std::cout << "    [FAIL] Generated ownership marker missing\n";
        ok = false;
    }
    if (file_omits(content, "import dgtl.op_traits;")) {
        std::cout << "    [OK] Generated module avoids unused dgtl.op_traits dependency\n";
    } else {
        std::cout << "    [FAIL] Generated module still imports unused dgtl.op_traits\n";
        ok = false;
    }
    if (contains(ssa_content, "import dgtl.ir_core.gen;") && file_omits(ssa_content, shadow_lookup_decl)) {
        std::cout << "    [OK] IR_SSA imports generated lookup ownership cleanly\n";
    } else {
        std::cout << "    [FAIL] IR_SSA still shadows generated lookup ownership\n";
        ok = false;
    }
    if (contains(rewriter_content, "import dgtl.ir_core.gen;") &&
        file_omits(rewriter_content, shadow_lookup_decl) &&
        file_omits(rewriter_content, shadow_verify_decl)) {
        std::cout << "    [OK] Pattern_Rewriter imports generated lookup ownership cleanly\n";
    } else {
        std::cout << "    [FAIL] Pattern_Rewriter still shadows generated lookup APIs\n";
        ok = false;
    }

    if (!ok) {
        std::cout << "\n[FAIL] DGTL-Gen Validation Failed\n";
        return 1;
    }

    std::cout << "\n[SUCCESS] DGTL-Gen Validation Complete\n";
    return 0;
}
