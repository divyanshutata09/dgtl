/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>

namespace fs = std::filesystem;

namespace {

inline constexpr std::size_t kOpcodeTableSize = 65536;

struct Options {
    fs::path input{"include/dgtl/isa/defs/x64_core.dgtl_isel"};
    fs::path output_rules{"include/dgtl/isa/X64/X64_ISel_Rules_Generated.ixx"};
    fs::path output_generated{"include/dgtl/isa/X64/X64_ISel_Generated.ixx"};
    fs::path root{"."};
};

struct FormMapping {
    std::string opcode;
    std::string form;
    std::string kind;
};

[[nodiscard]] std::string canonical_opcode_name(std::string_view opcode) {
    static const std::unordered_map<std::string, std::string> kAliases = {
        {"MOV", "REG_MOVE"},
        {"CONST", "LOAD_IMM"},
        {"RETURN", "RET"}
    };
    const auto it = kAliases.find(std::string(opcode));
    if (it != kAliases.end()) {
        return it->second;
    }
    return std::string(opcode);
}

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--root") {
            if (i + 1 >= argc) return false;
            opts.root = argv[++i];
        } else if (arg == "--input") {
            if (i + 1 >= argc) return false;
            opts.input = argv[++i];
        } else if (arg == "--output" || arg == "--rules-output") {
            if (i + 1 >= argc) return false;
            opts.output_rules = argv[++i];
        } else if (arg == "--generated-output") {
            if (i + 1 >= argc) return false;
            opts.output_generated = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: dgtl-isel-gen [--root <repo>] [--input <file>] [--rules-output <file>] [--generated-output <file>]\n";
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

[[nodiscard]] std::string make_generic_path(const fs::path& p) {
    return p.generic_string();
}

[[nodiscard]] bool starts_with_ascii(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string strip_hash_comment(std::string s) {
    const std::size_t hash = s.find('#');
    if (hash != std::string::npos) {
        s.erase(hash);
    }
    return s;
}

[[nodiscard]] bool parse_rule_name(std::string_view line, std::string& rule_name) {
    const std::string t = trim(std::string(line));
    if (!starts_with_ascii(t, "rule ")) {
        return false;
    }
    const std::size_t colon = t.find(':');
    if (colon == std::string::npos || colon <= 5) {
        return false;
    }
    rule_name = trim(t.substr(5, colon - 5));
    return !rule_name.empty();
}

[[nodiscard]] bool parse_form_line(std::string_view line, FormMapping& mapping) {
    const std::string trimmed = trim(std::string(line));
    if (!starts_with_ascii(trimmed, "form ")) {
        return false;
    }

    std::string body = trim(trimmed.substr(5));
    if (body.empty()) {
        return false;
    }

    std::size_t colon_pos = body.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        return false;
    }

    std::string form_name = trim(body.substr(0, colon_pos));
    if (form_name.empty()) {
        return false;
    }

    std::string pairs_section = trim(body.substr(colon_pos + 1));
    
    mapping.opcode.clear();
    mapping.form.clear();
    mapping.kind.clear();

    std::size_t pos = 0;
    while (pos < pairs_section.size()) {
        std::size_t comma_pos = pairs_section.find(',', pos);
        if (comma_pos == std::string::npos) {
            comma_pos = pairs_section.size();
        }

        std::string pair = trim(pairs_section.substr(pos, comma_pos - pos));
        pos = comma_pos + 1;

        if (pair.empty()) continue;

        std::size_t eq_pos = pair.find('=');
        if (eq_pos == std::string::npos || eq_pos == 0) {
            return false;
        }

        std::string key = trim(pair.substr(0, eq_pos));
        std::string value = trim(pair.substr(eq_pos + 1));

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "opcode") {
            mapping.opcode = value;
        } else if (key == "machine_form") {
            mapping.form = value;
        } else if (key == "kind") {
            mapping.kind = value;
        } else if (key == "description") {
            continue;
        } else {
            return false;
        }
    }

    if (mapping.opcode.empty() || mapping.form.empty()) {
        return false;
    }

    return true;
}

[[nodiscard]] bool is_known_machine_form(std::string_view form) {
    static const std::unordered_set<std::string> known = {
        "Unhandled", "NoOp", "Move", "LoadImmediate", "LoadStack", "StoreStack",
        "IntegerBinary32", "IntegerBinary64", "FloatBinary32", "FloatBinary64", "FloatTernaryFma32",
        "Compare32", "VectorBinary512", "VectorGather512", "VectorStore512",
        "Call", "Return", "Goto", "BranchCond", "BranchCompare", "Syscall",
        "TensorOp", "HardwareFence", "AtomicOp", "ConcurrentSync"
    };
    return known.find(std::string(form)) != known.end();
}

[[nodiscard]] bool kind_matches_form(std::string_view form, std::string_view kind) {
    if (kind.empty()) {
        return true;
    }
    if (form == "IntegerBinary32" || form == "IntegerBinary64") {
        static const std::unordered_set<std::string> allowed = {"Add", "Sub", "Mul", "Div", "Mod", "And", "Or", "Xor", "Shl", "Shr"};
        return allowed.find(std::string(kind)) != allowed.end();
    }
    if (form == "FloatBinary32" || form == "FloatBinary64") {
        static const std::unordered_set<std::string> allowed = {"Add", "Sub", "Mul", "Div"};
        return allowed.find(std::string(kind)) != allowed.end();
    }
    if (form == "Compare32") {
        return kind == "Any";
    }
    if (form == "VectorBinary512") {
        static const std::unordered_set<std::string> allowed = {"Add", "Mul"};
        return allowed.find(std::string(kind)) != allowed.end();
    }
    return kind.empty();
}

void write_rules_header(std::ofstream& out,
                        const fs::path& relative_input,
                        const std::vector<std::string>& rules,
                        const std::vector<std::string>& forms) {
    out << "/*\n"
        << " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
        << " *\n"
        << " * Project:    DGTL (Deep General-purpose Transformation Layer)\n"
        << " * Owner:      Divyanshu Tata\n"
        << " * Repository: github.com/TataG-Systems/DGTL\n"
        << " *\n"
        << " * This software is the proprietary property of TataG Systems.\n"
        << " * Unauthorized copying, modification, or distribution is strictly prohibited.\n"
        << " */\n\n";
    out << "/* Auto-generated from " << cpp_escape(make_generic_path(relative_input))
        << ". Do not edit manually. */\n";
    out << "module;\n\n";
    out << "#include <array>\n#include <cstddef>\n#include <string_view>\n\n";
    out << "export module dgtl.ir_core.isel_rules;\n\n";
    out << "export namespace dgtl::isa::lowering::generated {\n\n";
    out << "inline constexpr std::string_view kX64CoreRuleDslSource = \""
        << cpp_escape(make_generic_path(relative_input)) << "\";\n";
    out << "inline constexpr std::size_t kX64CoreRuleDslCount = " << rules.size() << ";\n\n";
    out << "inline constexpr std::array<std::string_view, " << rules.size() << "> kX64CoreRuleDslLines = {\n";
    for (std::size_t i = 0; i < rules.size(); ++i) {
        out << "    \"" << cpp_escape(rules[i]) << "\"";
        out << (i + 1 < rules.size() ? ",\n" : "\n");
    }
    out << "};\n\n";

    out << "inline constexpr std::size_t kX64CoreFormDslCount = " << forms.size() << ";\n\n";
    out << "inline constexpr std::array<std::string_view, " << forms.size() << "> kX64CoreFormDslLines = {\n";
    for (std::size_t i = 0; i < forms.size(); ++i) {
        out << "    \"" << cpp_escape(forms[i]) << "\"";
        out << (i + 1 < forms.size() ? ",\n" : "\n");
    }
    out << "};\n\n} // namespace dgtl::isa::lowering::generated\n";
}

void write_generated_header(std::ofstream& out,
                            const fs::path& relative_input,
                            const std::vector<FormMapping>& mappings) {
    out << "/*\n"
        << " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
        << " *\n"
        << " * Project:    DGTL (Deep General-purpose Transformation Layer)\n"
        << " * Owner:      Divyanshu Tata\n"
        << " * Repository: github.com/TataG-Systems/DGTL\n"
        << " *\n"
        << " * This software is the proprietary property of TataG Systems.\n"
        << " * Unauthorized copying, modification, or distribution is strictly prohibited.\n"
        << " */\n\n";
    out << "/* Auto-generated from " << cpp_escape(make_generic_path(relative_input))
        << ". Do not edit manually. */\n";
    out << "module;\n\n";
    out << "#include <algorithm>\n#include <array>\n#include <cstddef>\n#include <cstdint>\n\n";
    out << "export module dgtl.ir_core.isel_gen;\n\n";
    out << "import dgtl.ir_enums;\n";
    out << "import dgtl.ir_node;\n";
    out << "import dgtl.ir_core.bridge;\n\n";
    out << "export namespace dgtl::isa::lowering {\n\n";
    out << "enum class MachineForm : uint8_t {\n";
    out << "    Unhandled = 0,\n";
    out << "    NoOp,\n";
    out << "    Move,\n";
    out << "    LoadImmediate,\n";
    out << "    LoadStack,\n";
    out << "    StoreStack,\n";
    out << "    IntegerBinary32,\n";
    out << "    IntegerBinary64,\n";
    out << "    FloatBinary32,\n";
    out << "    FloatBinary64,\n";
    out << "    FloatTernaryFma32,\n";
    out << "    Compare32,\n";
    out << "    VectorBinary512,\n";
    out << "    VectorGather512,\n";
    out << "    VectorStore512,\n";
    out << "    Call,\n";
    out << "    Return,\n";
    out << "    Goto,\n";
    out << "    BranchCond,\n";
    out << "    BranchCompare,\n";
    out << "    Syscall,\n";
    out << "    TensorOp,\n";
    out << "    HardwareFence,\n";
    out << "    AtomicOp,\n";
    out << "    ConcurrentSync,\n";
    out << "};\n\n";
    out << "namespace generated {\n\n";
    out << "enum class X64IntegerBinary32Kind : uint8_t { None = 0, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr };\n";
    out << "enum class X64IntegerBinary64Kind : uint8_t { None = 0, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr };\n";
    out << "enum class X64FloatBinary32Kind : uint8_t { None = 0, Add, Sub, Mul, Div };\n";
    out << "enum class X64FloatBinary64Kind : uint8_t { None = 0, Add, Sub, Mul, Div };\n";
    out << "enum class X64Compare32Kind : uint8_t { None = 0, Any };\n";
    out << "enum class X64VectorBinary512Kind : uint8_t { None = 0, Add, Mul };\n\n";

    out << "inline constexpr size_t kMaxOpCodeIndex = []() constexpr {\n"
        << "    size_t highest = 0;\n";
    for (const auto& mapping : mappings) {
        out << "    if (static_cast<size_t>(ir::OpCode::" << mapping.opcode << ") > highest) highest = static_cast<size_t>(ir::OpCode::" << mapping.opcode << ");\n";
    }
    out << "    return highest < " << kOpcodeTableSize << " ? highest : (" << kOpcodeTableSize << " - 1);\n"
        << "}();\n\n";

    out << "alignas(64) inline constexpr auto kMachineFormTable = []() constexpr {\n";
    out << "    std::array<MachineForm, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(MachineForm::Unhandled);\n";
    out << "    auto set = [&](ir::OpCode opcode, MachineForm form) constexpr {\n";
    out << "        const size_t index = static_cast<size_t>(opcode);\n";
    out << "        if (index < table.size()) table[index] = form;\n";
    out << "    };\n\n";
    
    for (const FormMapping& mapping : mappings) {
        std::string target_form = mapping.form;
        if (target_form == "Call") {
            if (mapping.opcode.rfind("STRING_", 0) == 0) {
                target_form = "Call";
            } else if (mapping.opcode.rfind("TENSOR_", 0) == 0 || mapping.opcode == "MATMUL_BF16" || 
                       mapping.opcode == "MATMUL_FP8" || mapping.opcode == "SDPA_F16" || 
                       mapping.opcode == "SOFTMAX_FAST" || mapping.opcode == "LAYER_NORM" || mapping.opcode == "ROPE_EMBED") {
                target_form = "TensorOp";
            } else if (mapping.opcode.rfind("ATOMIC_", 0) == 0) {
                target_form = "AtomicOp";
            } else if (mapping.opcode == "ACQUIRE_FENCE" || mapping.opcode == "RELEASE_FENCE" || 
                       mapping.opcode == "MEMORY_FENCE" || mapping.opcode == "ATOMIC_FENCE") {
                target_form = "HardwareFence";
            } else if (mapping.opcode.rfind("ASYNC_", 0) == 0 || mapping.opcode.rfind("FIBER_", 0) == 0 || 
                       mapping.opcode.rfind("LOCK_", 0) == 0 || mapping.opcode.rfind("CHAN_", 0) == 0 || 
                       mapping.opcode == "BARRIER_SYNC" || mapping.opcode == "THREAD_SPAWN" || mapping.opcode == "SYNC_BARRIER") {
                target_form = "ConcurrentSync";
            }
        }
        out << "    set(ir::OpCode::" << mapping.opcode << ", MachineForm::" << target_form << ");\n";
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kIntegerBinary32KindTable = []() constexpr {\n";
    out << "    std::array<X64IntegerBinary32Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64IntegerBinary32Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "IntegerBinary32" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64IntegerBinary32Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kIntegerBinary64KindTable = []() constexpr {\n";
    out << "    std::array<X64IntegerBinary64Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64IntegerBinary64Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "IntegerBinary64" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64IntegerBinary64Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kFloatBinary32KindTable = []() constexpr {\n";
    out << "    std::array<X64FloatBinary32Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64FloatBinary32Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "FloatBinary32" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64FloatBinary32Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kCompare32KindTable = []() constexpr {\n";
    out << "    std::array<X64Compare32Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64Compare32Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "Compare32" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64Compare32Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kFloatBinary64KindTable = []() constexpr {\n";
    out << "    std::array<X64FloatBinary64Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64FloatBinary64Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "FloatBinary64" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64FloatBinary64Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "alignas(64) inline constexpr auto kVectorBinary512KindTable = []() constexpr {\n";
    out << "    std::array<X64VectorBinary512Kind, kMaxOpCodeIndex + 1> table{};\n";
    out << "    table.fill(X64VectorBinary512Kind::None);\n";
    for (const FormMapping& mapping : mappings) {
        if (mapping.form == "VectorBinary512" && !mapping.kind.empty()) {
            out << "    table[static_cast<size_t>(ir::OpCode::" << mapping.opcode << ")] = X64VectorBinary512Kind::" << mapping.kind << ";\n";
        }
    }
    out << "    return table;\n";
    out << "}();\n\n";

    out << "[[nodiscard]] constexpr MachineForm classify_machine_form(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kMachineFormTable.size() ? kMachineFormTable[index] : kMachineFormTable[0];\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64IntegerBinary32Kind integer_binary32_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kIntegerBinary32KindTable.size() ? kIntegerBinary32KindTable[index] : X64IntegerBinary32Kind::None;\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64IntegerBinary64Kind integer_binary64_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kIntegerBinary64KindTable.size() ? kIntegerBinary64KindTable[index] : X64IntegerBinary64Kind::None;\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64FloatBinary32Kind float_binary32_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kFloatBinary32KindTable.size() ? kFloatBinary32KindTable[index] : X64FloatBinary32Kind::None;\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64Compare32Kind compare32_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kCompare32KindTable.size() ? kCompare32KindTable[index] : X64Compare32Kind::None;\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64FloatBinary64Kind float_binary64_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kFloatBinary64KindTable.size() ? kFloatBinary64KindTable[index] : X64FloatBinary64Kind::None;\n";
    out << "}\n\n";
    out << "[[nodiscard]] constexpr X64VectorBinary512Kind vector_binary512_kind(ir::OpCode opcode) noexcept {\n";
    out << "    const size_t index = static_cast<size_t>(opcode);\n";
    out << "    return index < kVectorBinary512KindTable.size() ? kVectorBinary512KindTable[index] : X64VectorBinary512Kind::None;\n";
    out << "}\n\n";
    out << "} // namespace generated\n";
    out << "} // namespace dgtl::isa::lowering\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options opts;
        if (!parse_args(argc, argv, opts)) {
            std::cerr << "Invalid arguments. Use --help.\n";
            return 1;
        }

        static const std::set<std::string> kCanonicalOpcodes = {
            "NOP","LOAD","STORE","REG_MOVE","MOV","LOAD_IMM","CONST","LEA","PREFETCH","ATOMIC_LOAD","ATOMIC_STORE","ATOMIC_FENCE","IMMEDIATE","PTR_CAST","PTR_DIFF","BOOL_SELECT","STRING_CONST","STRING_LENGTH","STRING_COMPARE","STRING_CONCAT","STRING_SUBSTRING","GEP","PTR_TO_INT","INT_TO_PTR",
            "ADD_I32","SUB_I32","MUL_I32","DIV_I32","SHL_I32","AND_I32","OR_I32","XOR_I32","MOD_I32","SHR_I32",
            "ADD_I64","SUB_I64","MUL_I64","DIV_I64","MOD_I64","AND_I64","OR_I64","XOR_I64","SHL_I64","SHR_I64",
            "ADD_F32","SUB_F32","MUL_F32","DIV_F32","FMA_F32","FMA_WIDEN_F16_F32",
            "ADD_F64","SUB_F64","MUL_F64","DIV_F64",
            "CMP_EQ_I32","CMP_NE_I32","CMP_LT_I32","CMP_LE_I32","CMP_GT_I32","CMP_GE_I32",
            "GATHER_F32_512","GATHER_16","STORE_V512","VEC_ADD_F32_512","VEC_MUL_F32_512","SIMD_BSL_PREDICATE","FCVTL_F16_TO_F32","FCVTN_F32_TO_F16",
            "VEC_LOAD","VEC_STORE","VEC_SHUFFLE","VEC_REDUCE_ADD",
            "LABEL","GOTO","BRANCH","RET","RETURN","BRANCH_COND","BRANCH_COMPARE_FUSED","CALL","SYSCALL","GUARD_ALIAS",
            "PHI_NODE","GUARD_VALUE","DEOPT_TRIGGER","ASSUME","ASYNC_LAUNCH","SYNC_BARRIER","GPU_LAUNCH","OWNERSHIP_ACQUIRE","OWNERSHIP_RELEASE","OWNERSHIP_TRANSFER","ATOMIC_SNAPSHOT","ASYNC_SPAWN","ASYNC_AWAIT","FIBER_YIELD","BARRIER_SYNC","CHAN_SEND","CHAN_RECV","LOCK_ACQUIRE","LOCK_RELEASE","MEMORY_FENCE","THREAD_SPAWN",
            "GRAPH_BUILD","GRAPH_TRAVERSE","TREE_BUILD","TREE_TRAVERSE","TRIE_BUILD","TRIE_LOOKUP","TRIE_INSERT","DSA_ALLOCATE","DSA_DEALLOCATE","SCOP_ANALYZE",
            "TENSOR_RESHAPE","TENSOR_REDUCE","TENSOR_TRANSPOSE","TENSOR_SLICE","TENSOR_CONCAT","TENSOR_BROADCAST","TENSOR_GEMM","TENSOR_CONV","TENSOR_POOL","TENSOR_SOFTMAX",
            "REGION_BEGIN","REGION_END","FUSION_GROUP","PHI_MULTI","TUPLE_PACK","TUPLE_UNPACK","SELECT_MULTI","CALL_VARIADIC","SWITCH_DISPATCH","VOLATILE_LOAD","VOLATILE_STORE","ACQUIRE_FENCE","RELEASE_FENCE",
            "READ_TSC","BRANCH_LIKELY","BRANCH_UNLIKELY","CLFLUSH","PREFETCH_T0","SPIN_ACQUIRE","RING_PUSH_FREE",
            "FAST_INV_SQRT","VEC3_CROSS","VEC4_DOT","MAT4_MUL","QUAT_SLERP","RAY_AABB_ISECT",
            "MATMUL_BF16","MATMUL_FP8","SDPA_F16","SOFTMAX_FAST","LAYER_NORM","ROPE_EMBED",
            "WASM_INVOKE","FIBER_YIELD_LOOP","SUSPEND_CTX","RESUME_CTX","NEON_VADD_Q",
            "OP_ADD","ADD_I8","ADD_I128","ADD_I256","ADD_F8","ADD_BF16","BIT_FIELD_INSERT","BIT_FIELD_EXTRACT","UNDEF","SELECT"
        };

        const fs::path root = fs::weakly_canonical(opts.root);
        const fs::path input_file = root / opts.input;
        const fs::path output_rules = root / opts.output_rules;
        const fs::path output_generated = root / opts.output_generated;

        std::ifstream in(input_file);
        if (!in.is_open()) {
            std::cerr << "Missing ISel DSL file: " << input_file.string() << "\n";
            return 1;
        }

        std::vector<std::string> rules;
        std::vector<std::string> forms;
        std::vector<FormMapping> mappings;
        std::vector<FormMapping> canonical_mappings;
        std::unordered_set<std::string> rule_names;
        std::unordered_set<std::string> mapped_opcodes;
        std::unordered_map<std::string, FormMapping> canonical_map_by_opcode;

        std::string line;
        std::size_t line_no = 0;
        while (std::getline(in, line)) {
            ++line_no;
            std::string t = trim(strip_hash_comment(line));
            if (t.empty()) continue;

            if (t.rfind("rule ", 0) == 0) {
                std::string rule_name;
                if (!parse_rule_name(t, rule_name)) {
                    std::cerr << "Malformed rule at line " << line_no << ": " << t << "\n";
                    return 1;
                }
                if (!rule_names.insert(rule_name).second) {
                    std::cerr << "Duplicate rule name '" << rule_name << "' at line " << line_no << "\n";
                    return 1;
                }
                rules.push_back(std::move(t));
                continue;
            }

            if (t.rfind("form ", 0) == 0) {
                forms.push_back(t);
                FormMapping mapping{};
                if (!parse_form_line(t, mapping)) {
                    std::cerr << "Malformed form mapping at line " << line_no << ": " << t << "\n";
                    return 1;
                }
                if (!is_known_machine_form(mapping.form)) {
                    std::cerr << "Unknown machine form '" << mapping.form << "' at line " << line_no << "\n";
                    return 1;
                }
                if (!kind_matches_form(mapping.form, mapping.kind)) {
                    std::cerr << "Invalid kind '" << mapping.kind << "' for form '" << mapping.form
                              << "' at line " << line_no << "\n";
                    return 1;
                }
                if (!mapped_opcodes.insert(mapping.opcode).second) {
                    std::cerr << "Duplicate form mapping for opcode '" << mapping.opcode << "' at line " << line_no << "\n";
                    return 1;
                }

                const std::string canonical_opcode = canonical_opcode_name(mapping.opcode);
                const auto it_existing = canonical_map_by_opcode.find(canonical_opcode);
                if (it_existing != canonical_map_by_opcode.end()) {
                    const FormMapping& existing = it_existing->second;
                    if (existing.form != mapping.form || existing.kind != mapping.kind) {
                        std::cerr << "Conflicting alias mappings for canonical opcode '" << canonical_opcode
                                  << "' at line " << line_no << "\n";
                        return 1;
                    }
                } else {
                    FormMapping canonical = mapping;
                    canonical.opcode = canonical_opcode;
                    canonical_map_by_opcode.emplace(canonical_opcode, canonical);
                    canonical_mappings.push_back(std::move(canonical));
                }

                mappings.push_back(std::move(mapping));
                continue;
            }

            std::cerr << "Unknown DSL directive at line " << line_no << ": " << t << "\n";
            return 1;
        }

        std::set<std::string> missing;
        for (const auto& op : kCanonicalOpcodes) {
            if (canonical_map_by_opcode.find(canonical_opcode_name(op)) == canonical_map_by_opcode.end()) {
                missing.insert(op);
            }
        }
        if (!missing.empty()) {
            std::cerr << "[dgtl-isel-gen] WARNING: The following canonical IR opcodes are not mapped in the DSL and will default to Unhandled:\n";
            for (const auto& op : missing) {
                std::cerr << "  - " << op << "\n";
            }
        }

        fs::create_directories(output_rules.parent_path());
        fs::create_directories(output_generated.parent_path());

        fs::path relative_input = input_file;
        const fs::path rel_try = input_file.lexically_relative(root);
        if (!rel_try.empty()) {
            relative_input = rel_try;
        }

        {
            std::ofstream out(output_rules, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "Failed to open output file: " << output_rules.string() << "\n";
                return 1;
            }
            write_rules_header(out, relative_input, rules, forms);
            if (!out.good()) {
                std::cerr << "Failed while writing: " << output_rules.string() << "\n";
                return 1;
            }
        }
        {
            std::ofstream out(output_generated, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "Failed to open output file: " << output_generated.string() << "\n";
                return 1;
            }
            write_generated_header(out, relative_input, mappings);
            if (!out.good()) {
                std::cerr << "Failed while writing: " << output_generated.string() << "\n";
                return 1;
            }
        }

        std::cout << "[dgtl-isel-gen] Generated " << opts.output_rules.string()
                  << " and " << opts.output_generated.string()
                  << " from " << opts.input.string()
                  << " with " << rules.size() << " rules, " << mappings.size() << " DSL form mappings"
                  << " (" << canonical_mappings.size() << " canonical)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[dgtl-isel-gen] Fatal error: " << ex.what() << "\n";
        return 1;
    }
}