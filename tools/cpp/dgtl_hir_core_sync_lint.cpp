/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL HIR/Core Synchronization Architecture Linter
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
static bool has(const std::string& s, const std::string& q) { return s.find(q) != std::string::npos; }

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? argv[1] : ".";
    const fs::path ir = root / "include/dgtl/ir";
    const fs::path core = root / "include/dgtl/core";
    bool ok = true;

    fs::path schema_file = ir / "gen/defs/phase1_core_ops.dgtl_def";
    if (!fs::exists(schema_file)) {
        schema_file = root / "defs/ir/phase1_core_ops.dgtl_def";
    }
    const auto schema = read(schema_file);
    for (const char* q : {"canonical_source: true", "opcode_values: explicit", "stable_abi: preserve_existing_values"}) {
        if (!has(schema, q)) { std::cerr << "FAIL: missing schema directive: " << q << '\n'; ok = false; }
    }

    const auto global = read(ir / "Global_IR.ixx");
    if (has(global, "dgtl.ir_backend") || has(global, "dgtl.ir_jit")) {
        std::cerr << "FAIL: HIR aggregate exports downstream backend/JIT modules\n"; ok = false;
    }

    if (!fs::exists(ir / "Function.ixx") || !fs::exists(ir / "ControlFlow.ixx") ||
        !fs::exists(ir / "IR_Formal_Verifier.ixx")) {
        std::cerr << "FAIL: HIR ownership modules missing\n"; ok = false;
    }
    if (fs::exists(core / "Function.ixx") || fs::exists(core / "ControlFlow.ixx") || fs::exists(core / "Verified_Formal.ixx")) {
        std::cerr << "FAIL: HIR ownership remains duplicated under Core\n"; ok = false;
    }
    if (!fs::exists(root / "src/core/Target.cpp")) {
        std::cerr << "FAIL: Target implementation unit missing\n"; ok = false;
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
            if (!has(content, "Copyright (c) 2026 TataG Systems. All rights reserved.")) {
                std::cerr << "FAIL: auto-generated file missing proprietary header: "
                          << p.path().lexically_relative(root) << '\n';
                ok = false;
            }
        }
    }

    for (const auto& p : fs::recursive_directory_iterator(ir)) {
        if (!p.is_regular_file()) continue;
        const auto text = read(p.path());
        for (const char* bad : {"fallback_ir", "is_legacy_runtime_intrinsic", "CUSTOM_DIALECT_START",
                                "TargetDescription", "TargetISA", "CodegenTarget", "IR_Codegen_Bridge",
                                "IR_Execution_Bridge", "BackendProfile", "ai_cost_model"}) {
            if (has(text, bad)) {
                std::cerr << "FAIL: HIR boundary violation '" << bad << "' in "
                          << p.path().lexically_relative(root) << '\n';
                ok = false;
            }
        }
    }

    if (ok) {
        std::cout << "DGTL HIR/CORE SYNCHRONIZATION LINT PASSED: canonical HIR ownership, explicit target boundary, no legacy compatibility subsystem\n";
        return 0;
    }
    return 1;
}
