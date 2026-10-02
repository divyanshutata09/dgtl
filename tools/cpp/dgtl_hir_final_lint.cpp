/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL HIR Final Architecture Linter
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static bool contains(const std::string& s, const std::string& q) {
    return s.find(q) != std::string::npos;
}

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? argv[1] : ".";
    const fs::path ir = root / "include/dgtl/ir";
    bool ok = true;

    fs::path schema_file = ir / "gen/defs/phase1_core_ops.dgtl_def";
    if (!fs::exists(schema_file)) {
        schema_file = root / "defs/ir/phase1_core_ops.dgtl_def";
    }
    const auto schema = read(schema_file);
    for (const char* required : {"canonical_source: true", "opcode_values: explicit", "stable_abi: preserve_existing_values"}) {
        if (!contains(schema, required)) {
            std::cerr << "FAIL: canonical schema directive missing: " << required << '\n';
            ok = false;
        }
    }

    // Verify that auto-generated files have the proprietary copyright header
    for (const auto& rel : {"include/dgtl/ir/gen", "include/dgtl/mir"}) {
        const fs::path target_dir = root / rel;
        if (!fs::exists(target_dir)) continue;
        for (const auto& p : fs::recursive_directory_iterator(target_dir)) {
            if (!p.is_regular_file()) continue;
            const auto ext = p.path().extension().string();
            if (ext != ".ixx" && ext != ".cpp" && ext != ".h") continue;
            const auto content = read(p.path());
            if (!contains(content, "Copyright (c) 2026 TataG Systems. All rights reserved.")) {
                std::cerr << "FAIL: auto-generated file missing proprietary header: "
                          << p.path().lexically_relative(root) << '\n';
                ok = false;
            }
        }
    }

    const std::vector<std::string> forbidden = {
        "import dgtl.target", "TargetDescription", "BackendProfile", "TargetISA",
        "CodegenTarget", "IR_Codegen_Bridge", "IR_Execution_Bridge",
        "export import dgtl.ir_backend", "export import dgtl.ir_jit",
        "CUSTOM_DIALECT_START", "is_legacy_runtime_intrinsic", "fallback_ir",
        "find_minimal_equivalent", "fold_if_possible", "DGTLAICostModel",
        "ai_cost_model"
    };

    for (const auto& p : fs::recursive_directory_iterator(ir)) {
        if (!p.is_regular_file() || (p.path().extension() != ".ixx" && p.path().extension() != ".dgtl_def")) continue;
        const auto text = read(p.path());
        for (const auto& bad : forbidden) {
            if (contains(text, bad)) {
                std::cerr << "FAIL: HIR architectural dependency/legacy term '" << bad
                          << "' in " << p.path().lexically_relative(root) << '\n';
                ok = false;
            }
        }
    }

    if (contains(read(ir / "Global_IR.ixx"), "export import dgtl.core")) {
        std::cerr << "FAIL: HIR aggregate exports Core ownership modules\n";
        ok = false;
    }

    if (ok) {
        std::cout << "DGTL HIR FINAL ARCHITECTURE OK: canonical schema, passive HIR nodes, target-free HIR, no legacy subsystem\n";
        return 0;
    }
    return 1;
}
