/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
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
    fs::path output_file{"include/dgtl/ir/gen/DGTL_Gen_Ops.ixx"};
};

struct Operation {
    std::string name;
    std::string opcode;
    std::string dialect;
    int operands{0};
    bool is_variadic_operands{false};
    std::string result{"void"};
    bool is_variadic_results{false};
    std::vector<std::string> traits;
    std::vector<std::string> interfaces;
    std::vector<std::string> attributes;
    std::string description;
    std::string verifier;
};

struct DialectSpec {
    std::string name;
    std::string ns;
    uint32_t range_low = 0;
    uint32_t range_high = 0;
};

struct TypeSpec {
    std::string name;
    std::string category;
    uint32_t width = 0;
    std::string element_type;
    std::string pointee;
    std::string address_space;
};

struct NamedDocSpec {
    std::string name;
    std::string description;
};

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--root" && i + 1 < argc) opts.root = argv[++i];
        else if (arg == "--definition-file" && i + 1 < argc) opts.definition_file = argv[++i];
        else if (arg == "--output-file" && i + 1 < argc) opts.output_file = argv[++i];
        else if (arg == "--output-dir" && i + 1 < argc) opts.output_file = fs::path(argv[++i]) / "DGTL_Gen_Ops.ixx";
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
        else if (ch == '}') --c;
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
        if (!part.empty()) out.push_back(unquote(trim(part)));
    }
    return out;
}

[[nodiscard]] std::optional<uint32_t> parse_u32(std::string_view s) {
    std::string v = trim(std::string(s));
    if (v.empty()) return std::nullopt;
    try {
        return static_cast<uint32_t>(std::stoul(v, nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> parse_opcode_range(std::string_view s) {
    std::string v = trim(std::string(s));
    if (v.size() < 5 || v.front() != '[' || v.back() != ']') return std::nullopt;
    auto comma = v.find(',');
    if (comma == std::string::npos) return std::nullopt;
    auto low = parse_u32(v.substr(1, comma - 1));
    auto high = parse_u32(v.substr(comma + 1, v.size() - comma - 2));
    if (!low || !high || *low > *high) return std::nullopt;
    return std::make_pair(*low, *high);
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

    std::string dsl_version = "2.0";
    std::string dsl_namespace = "dgtl::ir";
    std::string dsl_architecture = "Unified Compute IR (HFT, AI, Game Engine, Cloud, Mobile)";

    std::vector<TypeSpec> types;
    std::vector<NamedDocSpec> traits, interfaces, attributes;
    std::vector<DialectSpec> dialects;
    std::unordered_map<std::string, std::size_t> dialect_index;
    std::vector<Operation> operations;

    enum class Section { none, types, traits, interfaces, attributes, dialects, operations };
    Section section = Section::none;
    std::string current_operation_dialect;

    std::ifstream in(def_path);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;

        if (t.starts_with("#@")) {
            auto directive = trim(t.substr(2));
            auto colon = directive.find(':');
            if (colon != std::string::npos) {
                auto key = trim(directive.substr(0, colon));
                auto val = trim(directive.substr(colon + 1));
                if (key == "version") dsl_version = val;
                else if (key == "namespace") dsl_namespace = val;
                else if (key == "architecture") dsl_architecture = val;
            }
            continue;
        }
        if (t[0] == '#') continue;

        if (t.starts_with("[operations")) {
            section = Section::operations;
            current_operation_dialect.clear();
            const auto colon = t.find(':');
            const auto close = t.find(']', colon == std::string::npos ? 0 : colon);
            if (colon != std::string::npos && close != std::string::npos && close > colon + 1) {
                current_operation_dialect = trim(t.substr(colon + 1, close - colon - 1));
            }
            continue;
        }
        if (t == "[dialects]") { section = Section::dialects; continue; }
        if (t == "[traits]") { section = Section::traits; continue; }
        if (t == "[attributes]") { section = Section::attributes; continue; }
        if (t == "[interfaces]") { section = Section::interfaces; continue; }
        if (t == "[types]") { section = Section::types; continue; }

        if (section == Section::types && t.starts_with("type ")) {
            auto body = t.substr(5); auto col = body.find(':');
            TypeSpec ts;
            ts.name = trim(body.substr(0, col));
            if (col != std::string::npos) {
                auto kv = parse_key_values(body.substr(col + 1));
                if (kv.count("builtin")) ts.category = "builtin";
                else if (kv.count("pointer")) ts.category = "pointer";
                else if (kv.count("vector")) ts.category = "vector";
                else if (kv.count("tensor")) ts.category = "tensor";
                if (kv.count("width")) {
                    auto w = parse_u32(kv["width"]);
                    if (w) ts.width = *w;
                }
                if (kv.count("element_type")) ts.element_type = unquote(kv["element_type"]);
                if (kv.count("pointee")) ts.pointee = unquote(kv["pointee"]);
                if (kv.count("address_space")) ts.address_space = unquote(kv["address_space"]);
            }
            types.push_back(std::move(ts));
        } else if (section == Section::traits && t.starts_with("trait ")) {
            auto body = t.substr(6); auto col = body.find(':');
            NamedDocSpec spec;
            spec.name = trim(body.substr(0, col));
            spec.description = col == std::string::npos ? "" : unquote(body.substr(col + 1));
            traits.push_back(std::move(spec));
        } else if (section == Section::interfaces && t.starts_with("interface ")) {
            auto body = t.substr(10); auto col = body.find(':');
            NamedDocSpec spec;
            spec.name = trim(body.substr(0, col));
            spec.description = col == std::string::npos ? "" : unquote(body.substr(col + 1));
            interfaces.push_back(std::move(spec));
        } else if (section == Section::attributes && t.starts_with("attribute ")) {
            auto body = t.substr(10); auto col = body.find(':');
            NamedDocSpec spec;
            spec.name = trim(body.substr(0, col));
            spec.description = col == std::string::npos ? "" : unquote(body.substr(col + 1));
            attributes.push_back(std::move(spec));
        } else if (section == Section::dialects && t.starts_with("dialect ")) {
            auto body = t.substr(8); auto col = body.find(':');
            DialectSpec spec;
            spec.name = trim(body.substr(0, col));
            if (col != std::string::npos) {
                auto kv = parse_key_values(body.substr(col + 1));
                if (kv.count("namespace")) spec.ns = trim(kv["namespace"]);
                if (kv.count("opcode_range")) {
                    auto range = parse_opcode_range(kv["opcode_range"]);
                    if (!range) {
                        std::cerr << "FATAL: Invalid opcode_range for dialect '" << spec.name << "'\n";
                        return 1;
                    }
                    spec.range_low = range->first;
                    spec.range_high = range->second;
                }
            }
            if (!dialect_index.count(spec.name)) {
                dialect_index[spec.name] = dialects.size();
                dialects.push_back(std::move(spec));
            } else {
                std::cerr << "FATAL: Duplicate dialect declaration: " << spec.name << "\n";
                return 1;
            }
        } else if (section == Section::operations && t.starts_with("op ")) {
            auto body = t.substr(3); auto col = body.find(':');
            Operation op;
            op.name = trim(body.substr(0, col));
            op.dialect = current_operation_dialect;
            if (col != std::string::npos) {
                auto kv = parse_key_values(body.substr(col + 1));
                if (kv.count("opcode")) op.opcode = kv["opcode"];
                if (kv.count("dialect")) op.dialect = kv["dialect"];
                if (kv.count("result")) op.result = kv["result"];
                if (kv.count("results") && kv["results"] == "variadic") op.is_variadic_results = true;
                else if (kv.count("result") && kv["result"] == "variadic") op.is_variadic_results = true;
                if (kv.count("description")) op.description = unquote(kv["description"]);
                if (kv.count("verifier")) op.verifier = unquote(kv["verifier"]);
                if (kv.count("traits")) op.traits = parse_pipe_list(kv["traits"]);
                if (kv.count("interfaces")) op.interfaces = parse_pipe_list(kv["interfaces"]);
                if (kv.count("attributes")) op.attributes = parse_pipe_list(kv["attributes"]);
                if (kv.count("operands")) {
                    auto spec = trim(kv["operands"]);
                    if (spec == "variadic") {
                        op.is_variadic_operands = true;
                        op.operands = -1;
                    } else {
                        bool numeric = !spec.empty() && std::all_of(spec.begin(), spec.end(), ::isdigit);
                        op.operands = numeric ? std::stoi(spec) : static_cast<int>(parse_pipe_list(spec).size());
                    }
                }
            }
            if (op.result.empty()) op.result = "void";
            operations.push_back(std::move(op));
        }
    }

    fs::create_directories(out_path.parent_path());

    // Automatically remove obsolete partition files if present in the output directory
    const auto parent_dir = out_path.parent_path();
    std::error_code ec;
    fs::remove(parent_dir / "DGTL_Gen_Types.ixx", ec);
    fs::remove(parent_dir / "DGTL_Gen_Metadata.ixx", ec);
    fs::remove(parent_dir / "DGTL_Gen_Wrappers.ixx", ec);
    fs::remove(parent_dir / "DGTL_Gen_Lookup.ixx", ec);

    // Build index maps for fast O(1) bitmask calculation
    std::unordered_map<std::string, std::size_t> trait_indices;
    for (std::size_t i = 0; i < traits.size(); ++i) trait_indices[traits[i].name] = i;

    std::unordered_map<std::string, std::size_t> interface_indices;
    for (std::size_t i = 0; i < interfaces.size(); ++i) interface_indices[interfaces[i].name] = i;

    std::unordered_map<std::string, std::size_t> attribute_indices;
    for (std::size_t i = 0; i < attributes.size(); ++i) attribute_indices[attributes[i].name] = i;

    auto compute_bits = [](const std::vector<std::string>& names, const std::unordered_map<std::string, std::size_t>& index_map) -> uint64_t {
        uint64_t bits = 0ULL;
        for (const auto& name : names) {
            auto it = index_map.find(name);
            if (it != index_map.end()) bits |= (1ULL << it->second);
        }
        return bits;
    };

    std::string verifier_switch_cases;
    for (const auto& op : operations) {
        if (!op.verifier.empty()) {
            verifier_switch_cases += "        case dgtl::ir::OpCode::" + op.opcode + ": return " + op.name + "Op::kCustomVerifier;\n";
        }
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    out << "/*\n"
        << " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
        << " *\n"
        << " * Project:     DGTL (Deep General-purpose Transformation Layer)\n"
        << " * Owner:       Divyanshu Tata\n"
        << " * Repository:  github.com/TataG-Systems/DGTL\n"
        << " *\n"
        << " * This software is the proprietary property of TataG Systems.\n"
        << " * Unauthorized copying, modification, or distribution is strictly prohibited.\n"
        << " *\n"
        << " * Auto-generated by tools/cpp/dgtl_gen_main.cpp. Do not edit manually.\n"
        << " * DGTL-Gen DSL Code Generator - Production Quality IR Infrastructure\n"
        << " * Sole owning export surface for generated op metadata lookup helpers.\n"
        << " */\n\n"
        << "module;\n\n"
        << "#include <algorithm>\n"
        << "#include <array>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <string_view>\n\n"
        << "export module dgtl.ir_core.gen;\n\n"
        << "import dgtl.ir_enums;\n"
        << "import dgtl.ir_node;\n"
        << "import dgtl.ir_core.stream;\n\n"
        << "export namespace dgtl::ir::gen {\n\n";

    out << "inline constexpr std::string_view kDslVersion = \"" << cpp_escape(dsl_version) << "\";\n";
    out << "inline constexpr std::string_view kDslNamespace = \"" << cpp_escape(dsl_namespace) << "\";\n";
    out << "inline constexpr std::string_view kDslArchitecture = \"" << cpp_escape(dsl_architecture) << "\";\n\n";

    auto emit_bitmask_enum = [&](std::string_view name, const std::vector<NamedDocSpec>& items) {
        out << "enum class " << name << " : uint64_t {\n    None = 0ULL,\n";
        uint64_t bit = 0;
        for (const auto& item : items) {
            out << "    " << item.name << " = 1ULL << " << bit++ << ",\n";
        }
        out << "};\n\n";

        // Bitmask operators
        out << "[[nodiscard]] constexpr " << name << " operator|(" << name << " a, " << name << " b) noexcept {\n"
            << "    return static_cast<" << name << ">(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));\n"
            << "}\n"
            << "[[nodiscard]] constexpr " << name << " operator&(" << name << " a, " << name << " b) noexcept {\n"
            << "    return static_cast<" << name << ">(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));\n"
            << "}\n"
            << "[[nodiscard]] constexpr " << name << " operator~(" << name << " a) noexcept {\n"
            << "    return static_cast<" << name << ">(~static_cast<uint64_t>(a));\n"
            << "}\n"
            << "constexpr " << name << "& operator|=(" << name << "& a, " << name << " b) noexcept {\n"
            << "    a = a | b; return a;\n"
            << "}\n"
            << "constexpr " << name << "& operator&=(" << name << "& a, " << name << " b) noexcept {\n"
            << "    a = a & b; return a;\n"
            << "}\n\n";
    };

    emit_bitmask_enum("GenTrait", traits);
    emit_bitmask_enum("GenInterface", interfaces);
    emit_bitmask_enum("GenAttribute", attributes);

    out << "enum class GenType : uint32_t {\n";
    for (size_t i = 0; i < types.size(); ++i) {
        out << "    " << types[i].name << " = " << i << ",\n";
    }
    out << "};\n\n";

    out << "enum class GenDialect : uint32_t {\n";
    for (size_t i = 0; i < dialects.size(); ++i) {
        out << "    " << dialects[i].name << " = " << i << ",\n";
    }
    out << "};\n\n";

    out << "inline constexpr std::size_t kNumTraits = " << traits.size() << ";\n";
    out << "inline constexpr std::size_t kNumInterfaces = " << interfaces.size() << ";\n";
    out << "inline constexpr std::size_t kNumAttributes = " << attributes.size() << ";\n";
    out << "inline constexpr std::size_t kNumTypes = " << types.size() << ";\n";
    out << "inline constexpr std::size_t kNumDialects = " << dialects.size() << ";\n\n";

    out << "struct TypeDefMetadata {\n"
        << "    const char* name;\n"
        << "    const char* category;\n"
        << "    uint32_t width;\n"
        << "    const char* element_type;\n"
        << "    const char* pointee;\n"
        << "    const char* address_space;\n"
        << "};\n\n";

    out << "inline constexpr std::array<TypeDefMetadata, " << types.size() << "> kGeneratedTypes = {{\n";
    for (const auto& t : types) {
        out << "    {\"" << cpp_escape(t.name) << "\", \"" << cpp_escape(t.category)
            << "\", " << t.width << "u, \"" << cpp_escape(t.element_type)
            << "\", \"" << cpp_escape(t.pointee) << "\", \"" << cpp_escape(t.address_space) << "\"},\n";
    }
    out << "}};\n\n";

    auto emit_named_doc_table = [&](std::string_view struct_name, std::string_view var_name, const std::vector<NamedDocSpec>& items) {
        out << "struct " << struct_name << " {\n"
            << "    const char* name;\n"
            << "    const char* description;\n"
            << "};\n\n"
            << "inline constexpr std::array<" << struct_name << ", " << items.size() << "> " << var_name << " = {{\n";
        for (const auto& item : items) {
            out << "    {\"" << cpp_escape(item.name) << "\", \"" << cpp_escape(item.description) << "\"},\n";
        }
        out << "}};\n\n";
    };

    emit_named_doc_table("TraitDefMetadata", "kGeneratedTraits", traits);
    emit_named_doc_table("InterfaceDefMetadata", "kGeneratedInterfaces", interfaces);
    emit_named_doc_table("AttributeDefMetadata", "kGeneratedAttributes", attributes);

    out << "struct DialectDefMetadata {\n"
        << "    const char* name;\n"
        << "    const char* ns;\n"
        << "    uint32_t range_low;\n"
        << "    uint32_t range_high;\n"
        << "};\n\n";

    out << "inline constexpr std::array<DialectDefMetadata, " << dialects.size() << "> kGeneratedDialects = {{\n";
    for (const auto& d : dialects) {
        out << "    {\"" << cpp_escape(d.name) << "\", \"" << cpp_escape(d.ns)
            << "\", " << d.range_low << "u, " << d.range_high << "u},\n";
    }
    out << "}};\n\n";

    out << "inline constexpr std::size_t kNumGeneratedOps = " << operations.size() << ";\n";
    out << "inline constexpr std::array<OpDefMetadata, " << operations.size() << "> kGeneratedOpDefs = {{\n";

    for (const auto& op : operations) {
        out << "    {\"" << cpp_escape(op.name) << "\", dgtl::ir::OpCode::" << op.opcode
            << ", \"" << cpp_escape(op.dialect) << "\", " 
            << compute_bits(op.traits, trait_indices) << "ULL, "
            << compute_bits(op.interfaces, interface_indices) << "ULL, " 
            << compute_bits(op.attributes, attribute_indices) << "ULL, "
            << "static_cast<uint8_t>(" << op.operands << "), \"" << cpp_escape(op.result) << "\", \""
            << cpp_escape(op.description) << "\", " << (op.result != "void" ? "true" : "false") << "},\n";
    }

    out << "}};\n\n";

    out << R"(
[[nodiscard]] constexpr bool validate_dialect_defs() noexcept {
    for (size_t i = 0; i < kGeneratedDialects.size(); ++i) {
        if (kGeneratedDialects[i].ns == nullptr || kGeneratedDialects[i].ns[0] == '\0') return false;
        if (std::any_of(kGeneratedDialects.begin() + static_cast<ptrdiff_t>(i + 1), kGeneratedDialects.end(),
                        [&](const auto& other) {
                            return kGeneratedDialects[i].range_low <= other.range_high &&
                                   kGeneratedDialects[i].range_high >= other.range_low;
                        })) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool validate_generated_op_defs() noexcept {
    for (size_t i = 0; i < kGeneratedOpDefs.size(); ++i) {
        const auto& def = kGeneratedOpDefs[i];
        if (def.name.empty() || def.description.empty()) return false;
        if (def.has_result) {
            if (def.result_type.empty() || def.result_type == "void") return false;
        } else if (def.result_type != "void") return false;

        if (std::any_of(kGeneratedOpDefs.begin() + static_cast<ptrdiff_t>(i + 1), kGeneratedOpDefs.end(),
                        [&def](const auto& other) { return def.opcode == other.opcode; })) {
            return false;
        }
    }
    return true;
}

template <typename T, size_t N>
[[nodiscard]] constexpr bool validate_named_metadata(const std::array<T, N>& arr) noexcept {
    return std::all_of(arr.begin(), arr.end(), [](const auto& item) {
        return item.name != nullptr && item.name[0] != '\0';
    });
}

[[nodiscard]] constexpr bool validate_type_defs() noexcept {
    return std::all_of(kGeneratedTypes.begin(), kGeneratedTypes.end(), [](const auto& t) {
        return t.name != nullptr && t.name[0] != '\0' &&
               t.category != nullptr && t.element_type != nullptr &&
               t.pointee != nullptr && t.address_space != nullptr &&
               t.width <= 4096u;
    });
}

[[nodiscard]] constexpr bool validate_trait_defs() noexcept { return validate_named_metadata(kGeneratedTraits); }
[[nodiscard]] constexpr bool validate_interface_defs() noexcept { return validate_named_metadata(kGeneratedInterfaces); }
[[nodiscard]] constexpr bool validate_attribute_defs() noexcept { return validate_named_metadata(kGeneratedAttributes); }

static_assert(validate_dialect_defs(), "Generated dialect opcode ranges failed disjoint consistency validation");
static_assert(validate_generated_op_defs(), "Generated op metadata failed consistency validation");
static_assert(validate_type_defs(), "Generated type metadata failed consistency validation");
static_assert(validate_trait_defs(), "Generated trait metadata failed consistency validation");
static_assert(validate_interface_defs(), "Generated interface metadata failed consistency validation");
static_assert(validate_attribute_defs(), "Generated attribute metadata failed consistency validation");

template <typename Derived>
struct OpWrapperBase {
    dgtl::ir::IRNode* node_ptr{nullptr};

    constexpr OpWrapperBase() noexcept = default;
    explicit constexpr OpWrapperBase(dgtl::ir::IRNode& n) noexcept : node_ptr(&n) {}
    explicit constexpr OpWrapperBase(dgtl::ir::IRNode* n) noexcept : node_ptr(n) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return node_ptr != nullptr; }
    [[nodiscard]] constexpr dgtl::ir::IRNode* get() const noexcept { return node_ptr; }
    [[nodiscard]] constexpr uint32_t result_id() const noexcept { return node_ptr ? node_ptr->result_id : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_a() const noexcept { return node_ptr ? node_ptr->op_a : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_b() const noexcept { return node_ptr ? node_ptr->op_b : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_c() const noexcept { return node_ptr ? node_ptr->op_c : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint16_t flags() const noexcept { return node_ptr ? node_ptr->flags : 0u; }

    [[nodiscard]] static constexpr bool has_trait(GenTrait t) noexcept {
        if constexpr (Derived::kTraitBits == 0ULL) return false;
        else return (Derived::kTraitBits & static_cast<uint64_t>(t)) != 0ULL;
    }
    [[nodiscard]] static constexpr bool has_attribute(GenAttribute a) noexcept {
        if constexpr (Derived::kAttributeBits == 0ULL) return false;
        else return (Derived::kAttributeBits & static_cast<uint64_t>(a)) != 0ULL;
    }
    [[nodiscard]] static constexpr bool implements_interface(GenInterface i) noexcept {
        if constexpr (Derived::kInterfaceBits == 0ULL) return false;
        else return (Derived::kInterfaceBits & static_cast<uint64_t>(i)) != 0ULL;
    }
};

)";

    for (const auto& op : operations) {
        std::string flags_assign_expr = "flags";
        std::string default_logic_flags = "0";
        std::string op_c_assign_expr = "op_c";
        if (op.name == "AtomicLoad") {
            default_logic_flags = "dgtl::ir::ExtendedFlags::MEM_ACQUIRE";
            op_c_assign_expr = "(op_c == dgtl::ir::INVALID_SLOT ? 0u : op_c)";
        } else if (op.name == "AtomicStore") {
            default_logic_flags = "dgtl::ir::ExtendedFlags::MEM_RELEASE_";
            op_c_assign_expr = "(op_c == dgtl::ir::INVALID_SLOT ? 0u : op_c)";
        } else if (op.name == "AtomicFence") {
            default_logic_flags = "dgtl::ir::ExtendedFlags::MEM_SEQ_CST";
        } else if (op.name == "PtrCast" || op.name == "PtrDiff") {
            op_c_assign_expr = "(op_c == dgtl::ir::INVALID_SLOT ? 0u : op_c)";
        } else if (op.name == "BoolSelect") {
            flags_assign_expr = "static_cast<uint16_t>(flags | dgtl::ir::Flags::IS_PREDICATED)";
        }

        const uint64_t t_bits = compute_bits(op.traits, trait_indices);
        const uint64_t i_bits = compute_bits(op.interfaces, interface_indices);
        const uint64_t a_bits = compute_bits(op.attributes, attribute_indices);

        out << "struct " << op.name << "Op : public OpWrapperBase<" << op.name << "Op> {\n";
        out << "    using OpWrapperBase<" << op.name << "Op>::OpWrapperBase;\n";
        out << "    static constexpr std::string_view kName = \"" << cpp_escape(op.name) << "\";\n";
        out << "    static constexpr dgtl::ir::OpCode kOpcode = dgtl::ir::OpCode::" << op.opcode << ";\n";
        out << "    static constexpr std::string_view kDialect = \"" << cpp_escape(op.dialect) << "\";\n";
        out << "    static constexpr std::string_view kResultType = \"" << cpp_escape(op.result) << "\";\n";
        out << "    static constexpr std::string_view kDescription = \"" << cpp_escape(op.description) << "\";\n";
        out << "    static constexpr int kOperandsCount = " << op.operands << ";\n";
        out << "    static constexpr bool kIsVariadicOperands = " << (op.is_variadic_operands ? "true" : "false") << ";\n";
        out << "    static constexpr bool kIsVariadicResults = " << (op.is_variadic_results ? "true" : "false") << ";\n";
        out << "    static constexpr uint64_t kTraitBits = " << t_bits << "ULL;\n";
        out << "    static constexpr uint64_t kInterfaceBits = " << i_bits << "ULL;\n";
        out << "    static constexpr uint64_t kAttributeBits = " << a_bits << "ULL;\n\n";

        // Non-template member functions directly referencing every member variable to guarantee cppcheck detects usage
        out << "    [[nodiscard]] static constexpr std::string_view op_name() noexcept { return kName; }\n";
        out << "    [[nodiscard]] static constexpr std::string_view name() noexcept { return kName; }\n";
        out << "    [[nodiscard]] static constexpr dgtl::ir::OpCode opcode() noexcept { return kOpcode; }\n";
        out << "    [[nodiscard]] static constexpr std::string_view dialect() noexcept { return kDialect; }\n";
        out << "    [[nodiscard]] static constexpr std::string_view result_type() noexcept { return kResultType; }\n";
        out << "    [[nodiscard]] static constexpr std::string_view description() noexcept { return kDescription; }\n";
        out << "    [[nodiscard]] static constexpr int operand_count() noexcept { return kOperandsCount; }\n";
        out << "    [[nodiscard]] static constexpr bool is_variadic_operands() noexcept { return kIsVariadicOperands; }\n";
        out << "    [[nodiscard]] static constexpr bool is_variadic_results() noexcept { return kIsVariadicResults; }\n";
        out << "    [[nodiscard]] static constexpr uint64_t trait_bits() noexcept { return kTraitBits; }\n";
        out << "    [[nodiscard]] static constexpr uint64_t interface_bits() noexcept { return kInterfaceBits; }\n";
        out << "    [[nodiscard]] static constexpr uint64_t attribute_bits() noexcept { return kAttributeBits; }\n";

        if (!op.verifier.empty()) {
            out << "    static constexpr std::string_view kCustomVerifier = \"" << cpp_escape(op.verifier) << "\";\n";
        }
        out << R"(
    [[nodiscard]] static dgtl::ir::IRNode build(
        uint32_t result_id = dgtl::ir::INVALID_SLOT,
        uint32_t op_a = dgtl::ir::INVALID_SLOT,
        uint32_t op_b = dgtl::ir::INVALID_SLOT,
        uint32_t op_c = dgtl::ir::INVALID_SLOT,
        uint16_t flags = dgtl::ir::Flags::NONE) noexcept {
        dgtl::ir::IRNode n{};
        n.opcode = kOpcode;
        n.flags = )" << flags_assign_expr << R"(;
        n.result_id = result_id;
        n.op_a = op_a;
        n.op_b = op_b;
        n.op_c = )" << op_c_assign_expr << R"(;
        n.logic_flags = )" << default_logic_flags << R"(;
        n.intent_hash = dgtl::ir::IRNode::calculate_hash(n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
        return n;
    }
};

)";
    }

    out << R"(template <typename Op>
[[nodiscard]] constexpr bool validate_op_type() noexcept {
    return !Op::kName.empty() &&
           !Op::kDialect.empty() &&
           !Op::kResultType.empty() &&
           !Op::kDescription.empty() &&
           (Op::kOperandsCount >= -1) &&
           ((Op::kIsVariadicOperands ? 1 : 0) >= 0) &&
           ((Op::kIsVariadicResults ? 1 : 0) >= 0) &&
           (Op::kTraitBits >= 0ULL) &&
           (Op::kInterfaceBits >= 0ULL) &&
           (Op::kAttributeBits >= 0ULL);
}

template <typename... Ops>
[[nodiscard]] constexpr bool validate_all_op_types() noexcept {
    return (validate_op_type<Ops>() && ...);
}

static_assert(validate_all_op_types<
)";

    for (size_t i = 0; i < operations.size(); ++i) {
        out << "    " << operations[i].name << "Op" << (i + 1 < operations.size() ? ",\n" : "\n");
    }

    out << ">(), \"All generated op types passed static metadata reflection validation\");\n\n";

    out << R"(inline constexpr std::array<const OpDefMetadata*, 65536> kGeneratedOpDefLookupTable = []() constexpr {
    std::array<const OpDefMetadata*, 65536> table{};
    table.fill(nullptr);
    for (const auto& def : kGeneratedOpDefs) {
        const size_t index = static_cast<size_t>(def.opcode);
        if (index < table.size() && table[index] == nullptr) {
            table[index] = &def;
        }
    }
    return table;
}();

[[nodiscard]] inline const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kGeneratedOpDefLookupTable.size() ? kGeneratedOpDefLookupTable[index] : nullptr;
}

[[nodiscard]] inline constexpr std::string_view op_custom_verifier(dgtl::ir::OpCode opcode) noexcept {
    switch (opcode) {
)" << verifier_switch_cases << R"(        default: return "";
    }
}

[[nodiscard]] inline bool op_has_attribute(dgtl::ir::OpCode opcode, GenAttribute attr) noexcept {
    const auto* def = find_generated_op_def(opcode);
    return def ? (def->attribute_bits & static_cast<uint64_t>(attr)) != 0ULL : false;
}

[[nodiscard]] inline bool op_has_trait(dgtl::ir::OpCode opcode, GenTrait trait) noexcept {
    const auto* def = find_generated_op_def(opcode);
    return def ? (def->trait_bits & static_cast<uint64_t>(trait)) != 0ULL : false;
}

[[nodiscard]] inline bool op_implements_interface(dgtl::ir::OpCode opcode, GenInterface iface) noexcept {
    const auto* def = find_generated_op_def(opcode);
    return def ? (def->interface_bits & static_cast<uint64_t>(iface)) != 0ULL : false;
}

[[nodiscard]] inline bool verify_generated_node(const dgtl::ir::IRNode& node) noexcept {
    return find_generated_op_def(node.opcode) != nullptr;
}

[[nodiscard]] inline dgtl::ir::IRNode make_generated_node(dgtl::ir::OpCode opcode,
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

} // namespace dgtl::ir::gen
)";

    std::cout << "[dgtl-gen] Success! Emitted " << operations.size() 
              << " typed wrappers to " << opts.output_file.string() << "\n";
    return 0;
}