/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * DGTL-GEN: The Master DSL Parser & Code Generator.
 * Architected for MLIR-style Typed Wrappers and 64-bit Semantic Bitmasks.
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path root{"."};
    fs::path definition_file{"include/dgtl/ir/gen/defs/phase1_core_ops.dgtl_def"};
    fs::path output_file{"include/dgtl/ir/gen/DGTL_Gen_Ops.hpp"};
};

struct Operation {
    std::string name;
    std::string opcode;
    std::string dialect;
    int operands{0};
    std::string result{"void"};
    std::vector<std::string> traits;
    std::vector<std::string> interfaces;
    std::vector<std::string> attributes;
    std::string description;
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--root" && i + 1 < argc) opts.root = argv[++i];
        else if (arg == "--definition-file" && i + 1 < argc) opts.definition_file = argv[++i];
        else if (arg == "--output-file" && i + 1 < argc) opts.output_file = argv[++i];
        else if (arg == "--help") {
            std::cout << "Usage: dgtl-gen [--root <repo>] [--definition-file <rel>] [--output-file <rel>]\n";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

[[nodiscard]] std::string unquote(std::string s) {
    s = trim(std::move(s));
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
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

[[nodiscard]] std::vector<std::string> split_top_level(std::string_view text, char delim) {
    std::vector<std::string> parts;
    int p = 0, b = 0, c = 0;
    bool in_quote = false;
    std::size_t start = 0;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) continue;

        if (ch == '(') ++p;
        else if (ch == ')' && p > 0) --p;
        else if (ch == '[') ++b;
        else if (ch == ']' && b > 0) --b;
        else if (ch == '{') ++c;
        else if (ch == '}' && c > 0) --c;
        else if (ch == delim && p == 0 && b == 0 && c == 0) {
            parts.push_back(trim(std::string(text.substr(start, i - start))));
            start = i + 1;
        }
    }
    parts.push_back(trim(std::string(text.substr(start))));
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const std::string& v) { return v.empty(); }), parts.end());
    return parts;
}

[[nodiscard]] std::unordered_map<std::string, std::string> parse_key_values(std::string_view text) {
    std::unordered_map<std::string, std::string> m;
    for (const auto& seg : split_top_level(text, ',')) {
        const auto eq = seg.find('=');
        if (eq == std::string::npos) continue;
        m[trim(seg.substr(0, eq))] = trim(seg.substr(eq + 1));
    }
    return m;
}

[[nodiscard]] std::vector<std::string> parse_pipe_list(std::string_view s) {
    std::vector<std::string> out;
    for (const auto& part : split_top_level(s, '|')) {
        if (!part.empty()) out.push_back(trim(part));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) return 1;

    const fs::path root = fs::weakly_canonical(opts.root);
    const fs::path def_path = root / opts.definition_file;
    const fs::path out_path = root / opts.output_file;

    if (!fs::exists(def_path)) {
        std::cerr << "FATAL: Definition file not found: " << def_path.string() << "\n";
        return 1;
    }

    std::cout << "[dgtl-gen] Parsing DGTL Dialect Specification...\n";

    std::unordered_map<std::string, std::string> types, traits, interfaces, attributes, dialects;
    std::vector<std::string> trait_order, interface_order, attribute_order;
    std::vector<Operation> operations;

    enum class Section { none, types, traits, interfaces, attributes, dialects, operations };
    Section section = Section::none;

    std::ifstream in(def_path);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t.starts_with("[operations")) { section = Section::operations; continue; }
        if (t == "[dialects]") { section = Section::dialects; continue; }
        if (t == "[traits]") { section = Section::traits; continue; }
        if (t == "[attributes]") { section = Section::attributes; continue; }
        if (t == "[interfaces]") { section = Section::interfaces; continue; }
        if (t == "[types]") { section = Section::types; continue; }

        if (section == Section::types && t.starts_with("type ")) {
            auto body = t.substr(5); auto col = body.find(':');
            types[trim(body.substr(0, col))] = col == std::string::npos ? "" : trim(body.substr(col + 1));
        } else if (section == Section::traits && t.starts_with("trait ")) {
            auto body = t.substr(6); auto col = body.find(':');
            auto name = trim(body.substr(0, col));
            if (!traits.count(name)) trait_order.push_back(name);
            traits[name] = col == std::string::npos ? "" : unquote(body.substr(col + 1));
        } else if (section == Section::interfaces && t.starts_with("interface ")) {
            auto body = t.substr(10); auto col = body.find(':');
            auto name = trim(body.substr(0, col));
            if (!interfaces.count(name)) interface_order.push_back(name);
            interfaces[name] = col == std::string::npos ? "" : unquote(body.substr(col + 1));
        } else if (section == Section::attributes && t.starts_with("attribute ")) {
            auto body = t.substr(10); auto col = body.find(':');
            auto name = trim(body.substr(0, col));
            if (!attributes.count(name)) attribute_order.push_back(name);
            attributes[name] = col == std::string::npos ? "" : unquote(body.substr(col + 1));
        } else if (section == Section::dialects && t.starts_with("dialect ")) {
            auto body = t.substr(8); auto col = body.find(':');
            dialects[trim(body.substr(0, col))] = col == std::string::npos ? "" : trim(body.substr(col + 1));
        } else if (section == Section::operations && t.starts_with("op ")) {
            auto body = t.substr(3); auto col = body.find(':');
            Operation op;
            op.name = trim(body.substr(0, col));
            if (col != std::string::npos) {
                auto kv = parse_key_values(body.substr(col + 1));
                if (kv.count("opcode")) op.opcode = kv["opcode"];
                if (kv.count("dialect")) op.dialect = kv["dialect"];
                if (kv.count("result")) op.result = kv["result"];
                if (kv.count("description")) op.description = unquote(kv["description"]);
                if (kv.count("traits")) op.traits = parse_pipe_list(kv["traits"]);
                if (kv.count("interfaces")) op.interfaces = parse_pipe_list(kv["interfaces"]);
                if (kv.count("attributes")) op.attributes = parse_pipe_list(kv["attributes"]);
                if (kv.count("operands")) {
                    auto spec = trim(kv["operands"]);
                    bool numeric = !spec.empty() && std::all_of(spec.begin(), spec.end(), ::isdigit);
                    op.operands = numeric ? std::stoi(spec) : static_cast<int>(parse_pipe_list(spec).size());
                }
            }
            if (op.result.empty()) op.result = "void";
            operations.push_back(std::move(op));
        }
    }

    // Architect Hard-Stop: 64-bit semantic mask limit
    if (trait_order.size() > 64 || interface_order.size() > 64 || attribute_order.size() > 64) {
        std::cerr << "FATAL: Exceeded 64-bit capacity for traits/interfaces/attributes.\n";
        return 1;
    }

    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "FATAL: Failed to open output: " << out_path.string() << "\n";
        return 1;
    }

    // ========================================================================
    // HEADER EMISSION
    // ========================================================================
    out << R"(/* Auto-generated by tools/cpp/dgtl_gen_main.cpp. Do not edit manually. */
/* DGTL-Gen DSL Code Generator - Production Quality IR Infrastructure */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <optional>

#include "dgtl/ir/base/IR_Enums.hpp"
#include "dgtl/ir/base/IR_Node.hpp"

namespace dgtl::ir::gen {

)";

    auto emit_bitmask_enum = [&](std::string_view name, const std::vector<std::string>& order) {
        out << "enum class " << name << " : uint64_t {\n";
        out << "    None = 0ULL,\n";
        uint64_t bit = 0;
        for (const auto& item : order) {
            out << "    " << item << " = 1ULL << " << bit++ << ",\n";
        }
        out << "};\n\n";
    };

    emit_bitmask_enum("GenTrait", trait_order);
    emit_bitmask_enum("GenInterface", interface_order);
    emit_bitmask_enum("GenAttribute", attribute_order);

    auto compute_bits = [](const std::vector<std::string>& names, const std::vector<std::string>& order) -> uint64_t {
        uint64_t bits = 0ULL;
        for (const auto& name : names) {
            auto it = std::find(order.begin(), order.end(), name);
            if (it != order.end()) bits |= (1ULL << std::distance(order.begin(), it));
        }
        return bits;
    };

    out << R"(struct OpDefMetadata {
    std::string_view name;
    dgtl::ir::OpCode opcode;
    std::string_view dialect;
    uint64_t trait_bits;
    uint64_t interface_bits;
    uint64_t attribute_bits;
    uint8_t operand_count;
    std::string_view result_type;
    std::string_view description;
    bool has_result;
};

)";

    out << "inline constexpr std::array<OpDefMetadata, " << operations.size() << "> kGeneratedOpDefs = {{\n";
    for (const auto& op : operations) {
        out << "    {\"" << cpp_escape(op.name) << "\", dgtl::ir::OpCode::" << op.opcode
            << ", \"" << cpp_escape(op.dialect) << "\", " 
            << compute_bits(op.traits, trait_order) << "ULL, "
            << compute_bits(op.interfaces, interface_order) << "ULL, " 
            << compute_bits(op.attributes, attribute_order) << "ULL, "
            << op.operands << ", \"" << cpp_escape(op.result) << "\", \""
            << cpp_escape(op.description) << "\", " << (op.result != "void" ? "true" : "false") << "},\n";
    }
    out << "}};\n\n";

    out << "// ========================================================================\n";
    out << "// TYPED OPERATION WRAPPERS (MLIR-Style)\n";
    out << "// ========================================================================\n\n";

    for (const auto& op : operations) {
        out << "struct " << op.name << "Op {\n";
        out << "    // --- Metadata ---\n";
        out << "    static constexpr std::string_view kName = \"" << cpp_escape(op.name) << "\";\n";
        out << "    static constexpr dgtl::ir::OpCode kOpcode = dgtl::ir::OpCode::" << op.opcode << ";\n";
        
        // MLIR-Style Wrapper implementation
        out << R"(
    // --- The Wrapper Target ---
    dgtl::ir::IRNode* node_ptr;

    // Construct a typed view over a raw memory node
    explicit )" << op.name << R"(Op(dgtl::ir::IRNode& n) : node_ptr(&n) {}
    explicit )" << op.name << R"(Op(dgtl::ir::IRNode* n) : node_ptr(n) {}

    // --- The Generic IRBuilder Contract ---
    [[nodiscard]] static dgtl::ir::IRNode build(
        uint32_t result_id = dgtl::ir::INVALID_SLOT,
        uint32_t op_a = dgtl::ir::INVALID_SLOT,
        uint32_t op_b = dgtl::ir::INVALID_SLOT,
        uint32_t op_c = dgtl::ir::INVALID_SLOT,
        uint16_t flags = dgtl::ir::Flags::NONE) noexcept {
        
        dgtl::ir::IRNode n{};
        n.opcode = kOpcode;
        n.flags = flags;
        n.result_id = result_id;
        n.op_a = op_a;
        n.op_b = op_b;
        n.op_c = op_c;
        n.intent_hash = dgtl::ir::IRNode::calculate_hash(n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
        return n;
    }
};

)";
    }

    out << R"(
// --- Static Lookup Utilities ---
[[nodiscard]] inline const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept {
    for (const auto& def : kGeneratedOpDefs) if (def.opcode == opcode) return &def;
    return nullptr;
}

[[nodiscard]] inline bool op_has_attribute(dgtl::ir::OpCode opcode, GenAttribute attr) noexcept {
    const auto* def = find_generated_op_def(opcode);
    if (!def) return false;
    return (def->attribute_bits & static_cast<uint64_t>(attr)) != 0ULL;
}

[[nodiscard]] inline bool op_has_trait(dgtl::ir::OpCode opcode, GenTrait trait) noexcept {
    const auto* def = find_generated_op_def(opcode);
    if (!def) return false;
    return (def->trait_bits & static_cast<uint64_t>(trait)) != 0ULL;
}

[[nodiscard]] inline bool op_implements_interface(dgtl::ir::OpCode opcode, GenInterface iface) noexcept {
    const auto* def = find_generated_op_def(opcode);
    if (!def) return false;
    return (def->interface_bits & static_cast<uint64_t>(iface)) != 0ULL;
}

[[nodiscard]] inline bool verify_generated_node(const dgtl::ir::IRNode& node) noexcept {
    return find_generated_op_def(node.opcode) != nullptr;
}

[[nodiscard]] inline dgtl::ir::IRNode make_generated_node(
    dgtl::ir::OpCode opcode,
    uint32_t result_id = dgtl::ir::INVALID_SLOT,
    uint32_t op_a = dgtl::ir::INVALID_SLOT,
    uint32_t op_b = dgtl::ir::INVALID_SLOT,
    uint32_t op_c = dgtl::ir::INVALID_SLOT,
    uint16_t flags = dgtl::ir::Flags::NONE) noexcept {
    dgtl::ir::IRNode n{};
    n.opcode = opcode;
    n.flags = flags;
    n.result_id = result_id;
    n.op_a = op_a;
    n.op_b = op_b;
    n.op_c = op_c;
    n.intent_hash = dgtl::ir::IRNode::calculate_hash(n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
    return n;
}

inline constexpr size_t kNumGeneratedOps = )" << operations.size() << R"(;
inline constexpr size_t kNumTraits = )" << traits.size() << R"(;
inline constexpr size_t kNumInterfaces = )" << interfaces.size() << R"(;
inline constexpr size_t kNumAttributes = )" << attributes.size() << R"(;
inline constexpr size_t kNumDialects = )" << dialects.size() << R"(;
inline constexpr size_t kNumTypes = )" << types.size() << R"(;

}  // namespace dgtl::ir::gen
)";

    std::cout << "[dgtl-gen] Success! Emitted " << operations.size() << " typed wrappers to " << opts.output_file.string() << "\n";
    return 0;
}