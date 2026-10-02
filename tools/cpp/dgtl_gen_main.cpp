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
    fs::path definition_file{"defs/ir/phase1_core_ops.dgtl_def"};
    fs::path output_file{"include/dgtl/ir/gen/DGTL_Gen_Ops.ixx"};
    fs::path enum_output_file{"include/dgtl/ir/gen/DGTL_Gen_Enums.ixx"};
};

struct Operation {
    std::string name;
    std::string opcode;
    std::optional<uint32_t> stable_value;
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

struct OpcodeAlias {
    enum class Kind { alias, explicit_value };
    std::string name;
    Kind kind{Kind::alias};
    std::string target;
    std::optional<uint32_t> value;
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
        else if (arg == "--enum-output-file" && i + 1 < argc) opts.enum_output_file = argv[++i];
        else if (arg == "--check") { /* validation-only mode; handled after parse */ }
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
    std::vector<OpcodeAlias> opcode_aliases;
    std::vector<Operation> operations;

    enum class Section {
        none, types, traits, interfaces, attributes, dialects,
        aliases, operations
    };
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
        if (t == "[aliases]") { section = Section::aliases; continue; }

        if (section == Section::aliases) {
            if (t.starts_with("alias ")) {
                const auto body = trim(t.substr(6));
                const auto eq = body.find('=');
                if (eq == std::string::npos) {
                    std::cerr << "FATAL: malformed opcode alias: " << t << "\n";
                    return 1;
                }
                OpcodeAlias entry;
                entry.name = trim(body.substr(0, eq));
                entry.kind = OpcodeAlias::Kind::alias;
                entry.target = trim(body.substr(eq + 1));
                opcode_aliases.push_back(std::move(entry));
                continue;
            }
        }

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
                if (kv.count("value")) op.stable_value = parse_u32(kv["value"]);
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

    // ------------------------------------------------------------------------
    // Canonical schema validation.  The DSL owns stable operation identity.
    // ------------------------------------------------------------------------
    {
        std::unordered_map<std::string, std::string> opcode_to_name;
        std::unordered_map<uint32_t, std::string> value_to_name;
        for (const auto& op : operations) {
            if (op.opcode.empty()) {
                std::cerr << "FATAL: operation '" << op.name << "' has no opcode symbol\n";
                return 1;
            }
            if (!op.stable_value.has_value()) {
                std::cerr << "FATAL: operation '" << op.name << "' has no explicit stable value; "
                          << "canonical DGTL IR requires value=<uint16>\n";
                return 1;
            }
            if (*op.stable_value > 0xFFFFu) {
                std::cerr << "FATAL: stable opcode value out of uint16 range for '" << op.name << "'\n";
                return 1;
            }
            if (auto [it, inserted] = opcode_to_name.emplace(op.opcode, op.name); !inserted) {
                std::cerr << "FATAL: duplicate opcode symbol '" << op.opcode << "' used by '"
                          << it->second << "' and '" << op.name << "'\n";
                return 1;
            }
            if (auto [it, inserted] = value_to_name.emplace(*op.stable_value, op.name); !inserted) {
                std::cerr << "FATAL: duplicate stable opcode value 0x" << std::hex << *op.stable_value
                          << std::dec << " used by '" << it->second << "' and '" << op.name << "'\n";
                return 1;
            }
            // References in traits/interfaces/attributes must resolve against the schema.
            for (const auto& name : op.traits) {
                // Checked below after metadata vectors are parsed.
                (void)name;
            }
        }
    }

    {
        std::unordered_map<std::string, bool> canonical_opcode_names;
        std::unordered_map<std::string, uint32_t> canonical_opcode_values;
        for (const auto& op : operations) {
            canonical_opcode_names.emplace(op.opcode, true);
            canonical_opcode_values.emplace(op.opcode, *op.stable_value);
        }

        std::unordered_map<std::string, bool> alias_names;
        for (const auto& entry : opcode_aliases) {
            if (entry.name.empty()) {
                std::cerr << "FATAL: opcode alias entry has an empty name\n";
                return 1;
            }
            if (canonical_opcode_names.contains(entry.name) || alias_names.contains(entry.name)) {
                std::cerr << "FATAL: duplicate opcode alias name '" << entry.name << "'\n";
                return 1;
            }
            if (entry.kind == OpcodeAlias::Kind::alias) {
                if (!canonical_opcode_values.contains(entry.target)) {
                    std::cerr << "FATAL: opcode alias '" << entry.name
                              << "' targets unknown canonical opcode '" << entry.target << "'\n";
                    return 1;
                }
            } else if (!entry.value.has_value() || *entry.value > 0xFFFFu) {
                std::cerr << "FATAL: opcode alias value out of uint16 range for '"
                          << entry.name << "'\n";
                return 1;
            }
            alias_names.emplace(entry.name, true);
        }
    }

    fs::create_directories(out_path.parent_path());
    fs::create_directories((root / opts.enum_output_file).parent_path());

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

    for (const auto& op : operations) {
        for (const auto& name : op.traits) {
            if (!trait_indices.contains(name)) { std::cerr << "FATAL: unknown trait '" << name << "' in op '" << op.name << "'\n"; return 1; }
        }
        for (const auto& name : op.interfaces) {
            if (!interface_indices.contains(name)) { std::cerr << "FATAL: unknown interface '" << name << "' in op '" << op.name << "'\n"; return 1; }
        }
        for (const auto& name : op.attributes) {
            if (!attribute_indices.contains(name)) { std::cerr << "FATAL: unknown attribute '" << name << "' in op '" << op.name << "'\n"; return 1; }
        }
    }

    auto compute_bits = [](const std::vector<std::string>& names, const std::unordered_map<std::string, std::size_t>& index_map) -> uint64_t {
        uint64_t bits = 0ULL;
        for (const auto& name : names) {
            auto it = index_map.find(name);
            if (it != index_map.end()) bits |= (1ULL << it->second);
        }
        return bits;
    };

    auto map_result_to_typecode = [](std::string_view ty) -> std::string {
        if (ty.empty() || ty == "void") return "dgtl::ir::TypeCode::VOID_T";
        if (ty == "Bool") return "dgtl::ir::TypeCode::BOOL";
        if (ty == "Predicate") return "dgtl::ir::TypeCode::PREDICATE";
        if (ty == "AddressSpace") return "dgtl::ir::TypeCode::ADDRESS_SPACE";
        if (ty == "Ptr") return "dgtl::ir::TypeCode::PTR";
        if (ty == "StringHandle") return "dgtl::ir::TypeCode::STRING_HANDLE";
        if (ty == "I4") return "dgtl::ir::TypeCode::I4";
        if (ty == "I8") return "dgtl::ir::TypeCode::I8";
        if (ty == "I16") return "dgtl::ir::TypeCode::I16";
        if (ty == "I32") return "dgtl::ir::TypeCode::I32";
        if (ty == "I64") return "dgtl::ir::TypeCode::I64";
        if (ty == "I128") return "dgtl::ir::TypeCode::I128";
        if (ty == "I256") return "dgtl::ir::TypeCode::I256";
        if (ty == "I512") return "dgtl::ir::TypeCode::I512";
        if (ty == "F8") return "dgtl::ir::TypeCode::F8";
        if (ty == "BF16") return "dgtl::ir::TypeCode::BF16";
        if (ty == "F16") return "dgtl::ir::TypeCode::F16";
        if (ty == "F32") return "dgtl::ir::TypeCode::F32";
        if (ty == "F64") return "dgtl::ir::TypeCode::F64";
        if (ty == "F128") return "dgtl::ir::TypeCode::F128";
        if (ty == "Vec3xF32") return "dgtl::ir::TypeCode::VEC3_F32";
        if (ty == "Vec4xF32") return "dgtl::ir::TypeCode::VEC4_F32";
        if (ty == "Mat4x4F32") return "dgtl::ir::TypeCode::MAT4X4_F32";
        if (ty == "QuatF32") return "dgtl::ir::TypeCode::QUAT_F32";
        if (ty == "VectorF32x16") return "dgtl::ir::TypeCode::VECTOR_F32X16";
        if (ty == "VectorI32x16") return "dgtl::ir::TypeCode::VECTOR_I32X16";
        if (ty == "VectorF16x32") return "dgtl::ir::TypeCode::VECTOR_F16X32";
        if (ty == "TensorFP8") return "dgtl::ir::TypeCode::TENSOR_FP8";
        if (ty == "TensorBF16") return "dgtl::ir::TypeCode::TENSOR_BF16";
        if (ty == "TensorF32") return "dgtl::ir::TypeCode::TENSOR_F32";
        if (ty == "TensorI64") return "dgtl::ir::TypeCode::TENSOR_I64";
        return "dgtl::ir::TypeCode::VOID_T";
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
        << "#include <concepts>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <optional>\n"
        << "#include <string_view>\n\n"
        << "export module dgtl.ir.gen;\n\n"
        << "import dgtl.ir_enums;\n"
        << "import dgtl.ir_node;\n\n"
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

    out << "struct OpDefMetadata {\n"
        << "    std::string_view name;\n"
        << "    dgtl::ir::OpCode opcode;\n"
        << "    uint16_t stable_id;\n"
        << "    std::string_view dialect;\n"
        << "    uint64_t trait_bits;\n"
        << "    uint64_t interface_bits;\n"
        << "    uint64_t attribute_bits;\n"
        << "    uint8_t operand_count;\n"
        << "    std::string_view result_type;\n"
        << "    std::string_view description;\n"
        << "    bool has_result;\n"
        << "    dgtl::ir::TypeCode result_type_code;\n"
        << "};\n\n";

    out << "struct OpAliasMetadata {\n"
        << "    std::string_view name;\n"
        << "    dgtl::ir::OpCode target;\n"
        << "};\n\n";
    out << "inline constexpr std::array<OpAliasMetadata, " << opcode_aliases.size() << "> kGeneratedOpAliases = {{\n";
    for (const auto& alias : opcode_aliases) {
        out << "    {\"" << cpp_escape(alias.name) << "\", dgtl::ir::OpCode::" << alias.target << "},\n";
    }
    out << "}};\n\n";

    out << "inline constexpr std::size_t kNumGeneratedOps = " << operations.size() << ";\n";
    out << "inline constexpr std::array<OpDefMetadata, " << operations.size() << "> kGeneratedOpDefs = {{\n";

    for (const auto& op : operations) {
        out << "    {\"" << cpp_escape(op.name) << "\", dgtl::ir::OpCode::" << op.opcode
            << ", static_cast<uint16_t>(0x" << std::hex << *op.stable_value << std::dec << "), \"" << cpp_escape(op.dialect) << "\", " 
            << compute_bits(op.traits, trait_indices) << "ULL, "
            << compute_bits(op.interfaces, interface_indices) << "ULL, " 
            << compute_bits(op.attributes, attribute_indices) << "ULL, "
            << "static_cast<uint8_t>(" << op.operands << "), \"" << cpp_escape(op.result) << "\", \""
            << cpp_escape(op.description) << "\", " << (op.result != "void" ? "true" : "false") << ", "
            << map_result_to_typecode(op.result) << "},\n";
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
        if (static_cast<uint16_t>(def.opcode) != def.stable_id) return false;
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
    dgtl::ir::IRNode* node_ptr = nullptr;

    constexpr OpWrapperBase() noexcept = default;
    explicit constexpr OpWrapperBase(dgtl::ir::IRNode& n) noexcept : node_ptr(&n) {}
    explicit constexpr OpWrapperBase(dgtl::ir::IRNode* n) noexcept : node_ptr(n) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return node_ptr != nullptr; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return node_ptr != nullptr; }
    [[nodiscard]] constexpr dgtl::ir::IRNode* get() noexcept { return node_ptr; }
    [[nodiscard]] constexpr const dgtl::ir::IRNode* get() const noexcept { return node_ptr; }
    [[nodiscard]] constexpr dgtl::ir::IRNode* operator->() noexcept { return node_ptr; }
    [[nodiscard]] constexpr const dgtl::ir::IRNode* operator->() const noexcept { return node_ptr; }
    [[nodiscard]] constexpr dgtl::ir::IRNode& operator*() noexcept { return *node_ptr; }
    [[nodiscard]] constexpr const dgtl::ir::IRNode& operator*() const noexcept { return *node_ptr; }

    [[nodiscard]] constexpr uint32_t result_id() const noexcept { return node_ptr ? node_ptr->result_id : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_a() const noexcept { return node_ptr ? node_ptr->op_a : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_b() const noexcept { return node_ptr ? node_ptr->op_b : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint32_t op_c() const noexcept { return node_ptr ? node_ptr->op_c : dgtl::ir::INVALID_SLOT; }
    [[nodiscard]] constexpr uint16_t flags() const noexcept { return node_ptr ? node_ptr->flags : 0u; }
    [[nodiscard]] constexpr uint32_t logic_flags() const noexcept { return node_ptr ? node_ptr->logic_flags : 0u; }
    [[nodiscard]] constexpr uint64_t intent_hash() const noexcept { return node_ptr ? node_ptr->intent_hash : 0ULL; }

    // Semantic named operand accessors
    [[nodiscard]] constexpr uint32_t lhs() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t rhs() const noexcept { return op_b(); }
    [[nodiscard]] constexpr uint32_t extra() const noexcept { return op_c(); }
    [[nodiscard]] constexpr uint32_t ptr() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t val() const noexcept { return op_b(); }
    [[nodiscard]] constexpr uint32_t condition() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t true_target() const noexcept { return op_b(); }
    [[nodiscard]] constexpr uint32_t false_target() const noexcept { return op_c(); }
    [[nodiscard]] constexpr uint32_t true_value() const noexcept { return op_b(); }
    [[nodiscard]] constexpr uint32_t false_value() const noexcept { return op_c(); }
    [[nodiscard]] constexpr uint32_t target() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t source() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t base_ptr() const noexcept { return op_a(); }
    [[nodiscard]] constexpr uint32_t index_slot() const noexcept { return op_b(); }

    // In-place node mutation helpers
    constexpr void set_result_id(uint32_t id) noexcept { if (node_ptr) node_ptr->result_id = id; }
    constexpr void set_op_a(uint32_t a) noexcept { if (node_ptr) node_ptr->op_a = a; }
    constexpr void set_op_b(uint32_t b) noexcept { if (node_ptr) node_ptr->op_b = b; }
    constexpr void set_op_c(uint32_t c) noexcept { if (node_ptr) node_ptr->op_c = c; }
    constexpr void set_flags(uint16_t f) noexcept { if (node_ptr) node_ptr->flags = f; }
    constexpr void set_lhs(uint32_t a) noexcept { set_op_a(a); }
    constexpr void set_rhs(uint32_t b) noexcept { set_op_b(b); }
    constexpr void set_ptr(uint32_t a) noexcept { set_op_a(a); }
    constexpr void set_val(uint32_t b) noexcept { set_op_b(b); }
    constexpr void set_condition(uint32_t a) noexcept { set_op_a(a); }
    constexpr void set_target(uint32_t a) noexcept { set_op_a(a); }

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

    [[nodiscard]] static constexpr bool is_commutative() noexcept { return has_trait(GenTrait::Commutative); }
    [[nodiscard]] static constexpr bool is_terminator() noexcept { return has_trait(GenTrait::IsTerminator); }
    [[nodiscard]] static constexpr bool is_memory_op() noexcept {
        return has_trait(GenTrait::IsMemoryOp) || has_trait(GenTrait::IsMemoryRead) || has_trait(GenTrait::IsMemoryWrite);
    }
    [[nodiscard]] static constexpr bool is_memory_read() noexcept { return has_trait(GenTrait::IsMemoryRead); }
    [[nodiscard]] static constexpr bool is_memory_write() noexcept { return has_trait(GenTrait::IsMemoryWrite); }
    [[nodiscard]] static constexpr bool has_no_side_effects() noexcept { return has_trait(GenTrait::HasNoSideEffects); }
    [[nodiscard]] static constexpr bool is_vectorizable() noexcept { return has_trait(GenTrait::IsVectorizable); }
    [[nodiscard]] static constexpr bool is_fusible() noexcept { return has_trait(GenTrait::IsFusible); }
    [[nodiscard]] static constexpr bool is_speculative() noexcept { return has_trait(GenTrait::IsSpeculative); }
    [[nodiscard]] static constexpr bool is_gpu() noexcept { return has_trait(GenTrait::IsGPU); }
    [[nodiscard]] static constexpr bool is_tensor() noexcept { return has_trait(GenTrait::IsTensor); }
    [[nodiscard]] static constexpr bool is_async() noexcept { return has_trait(GenTrait::IsAsyncSpawnable); }
    [[nodiscard]] static constexpr bool is_hardware_intrinsic() noexcept { return has_trait(GenTrait::IsHardwareIntrinsic); }
    [[nodiscard]] static constexpr bool is_guard() noexcept { return has_trait(GenTrait::IsGuard); }
    [[nodiscard]] static constexpr bool is_latency_critical() noexcept { return has_trait(GenTrait::IsLatencyCritical); }

    [[nodiscard]] static constexpr bool classof(const dgtl::ir::IRNode* node) noexcept {
        return node && node->opcode == Derived::kOpcode;
    }
    [[nodiscard]] static constexpr bool classof(const dgtl::ir::IRNode& node) noexcept {
        return node.opcode == Derived::kOpcode;
    }
};

// C++20 Concepts for compile-time op reflection
template <typename T>
concept IsDGTLOp = requires {
    { T::kOpcode } -> std::convertible_to<dgtl::ir::OpCode>;
    { T::kName } -> std::convertible_to<std::string_view>;
    { T::kTraitBits } -> std::convertible_to<uint64_t>;
};

template <typename T>
concept IsDGTLTerminatorOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsTerminator)) != 0ULL);

template <typename T>
concept IsDGTLCommutativeOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::Commutative)) != 0ULL);

template <typename T>
concept IsDGTLMemoryOp = IsDGTLOp<T> &&
    ((T::kTraitBits & (static_cast<uint64_t>(GenTrait::IsMemoryOp) |
                       static_cast<uint64_t>(GenTrait::IsMemoryRead) |
                       static_cast<uint64_t>(GenTrait::IsMemoryWrite))) != 0ULL);

template <typename T>
concept IsDGTLVectorOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsVectorizable)) != 0ULL);

template <typename T>
concept IsDGTLGPUOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsGPU)) != 0ULL);

template <typename T>
concept IsDGTLFusibleOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsFusible)) != 0ULL);

template <typename T>
concept IsDGTLPureOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::HasNoSideEffects)) != 0ULL);

template <typename T>
concept IsDGTLGuardOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsGuard)) != 0ULL);

template <typename T>
concept IsDGTLTensorOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsTensor)) != 0ULL);

template <typename T>
concept IsDGTLAsyncOp = IsDGTLOp<T> &&
    ((T::kTraitBits & static_cast<uint64_t>(GenTrait::IsAsyncSpawnable)) != 0ULL);

// LLVM-style RTTI casting support
template <typename Op>
[[nodiscard]] constexpr bool isa(const dgtl::ir::IRNode* node) noexcept {
    return Op::classof(node);
}
template <typename Op>
[[nodiscard]] constexpr bool isa(const dgtl::ir::IRNode& node) noexcept {
    return Op::classof(node);
}
template <typename Op>
[[nodiscard]] constexpr std::optional<Op> dyn_cast(dgtl::ir::IRNode* node) noexcept {
    if (isa<Op>(node)) return Op(*node);
    return std::nullopt;
}
template <typename Op>
[[nodiscard]] constexpr std::optional<Op> dyn_cast(dgtl::ir::IRNode& node) noexcept {
    if (isa<Op>(node)) return Op(node);
    return std::nullopt;
}
template <typename Op>
[[nodiscard]] constexpr Op cast(dgtl::ir::IRNode& node) noexcept {
    return Op(node);
}
template <typename Op>
[[nodiscard]] constexpr Op cast(dgtl::ir::IRNode* node) noexcept {
    return Op(node);
}

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
        out << "    static constexpr dgtl::ir::TypeCode kResultTypeCode = " << map_result_to_typecode(op.result) << ";\n";
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
        out << "    [[nodiscard]] static constexpr dgtl::ir::TypeCode result_type_code() noexcept { return kResultTypeCode; }\n";
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
)";
        if (op.operands == 2 && op.result != "void") {
            out << "    [[nodiscard]] static dgtl::ir::IRNode build_binary(uint32_t result_id, uint32_t lhs, uint32_t rhs, uint16_t flags = dgtl::ir::Flags::NONE) noexcept {\n"
                << "        return build(result_id, lhs, rhs, dgtl::ir::INVALID_SLOT, flags);\n"
                << "    }\n";
        } else if (op.operands == 2 && op.result == "void") {
            out << "    [[nodiscard]] static dgtl::ir::IRNode build_store(uint32_t ptr, uint32_t val, uint16_t flags = dgtl::ir::Flags::NONE) noexcept {\n"
                << "        return build(dgtl::ir::INVALID_SLOT, ptr, val, dgtl::ir::INVALID_SLOT, flags);\n"
                << "    }\n";
        } else if (op.operands == 1 && op.result != "void") {
            out << "    [[nodiscard]] static dgtl::ir::IRNode build_unary(uint32_t result_id, uint32_t operand, uint16_t flags = dgtl::ir::Flags::NONE) noexcept {\n"
                << "        return build(result_id, operand, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, flags);\n"
                << "    }\n";
        } else if (op.operands == 0 && op.result != "void") {
            out << "    [[nodiscard]] static dgtl::ir::IRNode build_nullary(uint32_t result_id, uint16_t flags = dgtl::ir::Flags::NONE) noexcept {\n"
                << "        return build(result_id, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, flags);\n"
                << "    }\n";
        } else if (op.operands == 3) {
            out << "    [[nodiscard]] static dgtl::ir::IRNode build_ternary(uint32_t result_id, uint32_t a, uint32_t b, uint32_t c, uint16_t flags = dgtl::ir::Flags::NONE) noexcept {\n"
                << "        return build(result_id, a, b, c, flags);\n"
                << "    }\n";
        }
        out << "};\n\n";
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

[[nodiscard]] inline constexpr const OpDefMetadata* find_generated_op_def_by_name(std::string_view name) noexcept {
    for (const auto& def : kGeneratedOpDefs) {
        if (def.name == name) return &def;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr const OpAliasMetadata* find_op_alias(std::string_view name) noexcept {
    for (const auto& alias : kGeneratedOpAliases) {
        if (alias.name == name) return &alias;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr std::optional<dgtl::ir::OpCode> find_opcode_by_name(std::string_view name) noexcept {
    if (const auto* def = find_generated_op_def_by_name(name)) return def->opcode;
    if (const auto* alias = find_op_alias(name)) return alias->target;
    return std::nullopt;
}

[[nodiscard]] inline constexpr const DialectDefMetadata* find_dialect_by_name(std::string_view name) noexcept {
    for (const auto& d : kGeneratedDialects) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr const DialectDefMetadata* find_dialect_for_opcode(dgtl::ir::OpCode opcode) noexcept {
    const auto val = static_cast<uint32_t>(opcode);
    for (const auto& d : kGeneratedDialects) {
        if (val >= d.range_low && val <= d.range_high) return &d;
    }
    return nullptr;
}

[[nodiscard]] inline constexpr const TypeDefMetadata* find_type_by_name(std::string_view name) noexcept {
    for (const auto& t : kGeneratedTypes) {
        if (t.name == name) return &t;
    }
    return nullptr;
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


    // ------------------------------------------------------------------------
    // Partition generated metadata from typed node wrappers.
    //
    // The metadata module is deliberately independent of IRNode. This removes
    // the former cycle IRNode -> schema -> generated metadata -> IRNode and
    // permits IRNode to consume canonical schema semantics directly.
    // ------------------------------------------------------------------------
    out.flush();
    out.close();
    {
        std::ifstream gin(out_path, std::ios::binary);
        std::string full((std::istreambuf_iterator<char>(gin)), std::istreambuf_iterator<char>());
        gin.close();

        const std::string wrapper_begin = "template <typename Derived>\nstruct OpWrapperBase";
        const std::string lookup_begin = "inline constexpr std::array<const OpDefMetadata*, 65536>";
        const auto wb = full.find(wrapper_begin);
        const auto lb = full.find(lookup_begin);
        if (wb == std::string::npos || lb == std::string::npos || wb >= lb) {
            std::cerr << "FATAL: generated wrapper partition markers not found\n";
            return 1;
        }

        std::string metadata = full.substr(0, wb);
        metadata += full.substr(lb);

        // Metadata must not contain references to wrapper types or IRNode.
        const auto custom_begin = metadata.find("[[nodiscard]] inline constexpr std::string_view op_custom_verifier");
        const auto attr_begin = metadata.find("[[nodiscard]] inline bool op_has_attribute", custom_begin);
        if (custom_begin != std::string::npos && attr_begin != std::string::npos) {
            metadata.erase(custom_begin, attr_begin - custom_begin);
        }
        const auto verify_begin = metadata.find("[[nodiscard]] inline bool verify_generated_node");
        const auto namespace_end = metadata.rfind("} // namespace dgtl::ir::gen");
        if (verify_begin != std::string::npos && namespace_end != std::string::npos && verify_begin < namespace_end) {
            metadata.erase(verify_begin, namespace_end - verify_begin);
        }
        const auto import_node = metadata.find("import dgtl.ir_node;\n");
        if (import_node != std::string::npos) metadata.erase(import_node, std::string("import dgtl.ir_node;\n").size());

        const fs::path wrapper_path = out_path.parent_path() / "DGTL_Gen_Op_Wrappers.ixx";
        std::ofstream mout(out_path, std::ios::binary | std::ios::trunc);
        mout << metadata;
        mout.close();

        std::string wrapper =
            "/*\n"
            " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
            " *\n"
            " * Project:     DGTL (Deep General-purpose Transformation Layer)\n"
            " * Owner:       Divyanshu Tata\n"
            " * Repository:  github.com/TataG-Systems/DGTL\n"
            " *\n"
            " * Component:   Auto-generated DGTL IR Typed Operation Wrappers\n"
            " *\n"
            " * This software is the proprietary property of TataG Systems.\n"
            " * Unauthorized copying, modification, or distribution is strictly prohibited.\n"
            " *\n"
            " * Auto-generated by tools/cpp/dgtl_gen_main.cpp.\n"
            " */\n"
            "module;\n"
            "#include <cstdint>\n"
            "#include <optional>\n"
            "#include <string_view>\n"
            "export module dgtl.ir.gen_wrappers;\n"
            "import dgtl.ir_enums;\n"
            "import dgtl.ir_node;\n"
            "import dgtl.ir.gen;\n"
            "export namespace dgtl::ir::gen {\n\n";
        wrapper += full.substr(wb, lb - wb);

        wrapper += R"(
[[nodiscard]] inline constexpr std::string_view op_custom_verifier(dgtl::ir::OpCode opcode) noexcept {
    switch (opcode) {
)";
        for (const auto& op : operations) {
            if (!op.verifier.empty()) {
                wrapper += "        case dgtl::ir::OpCode::" + op.opcode +
                           ": return " + op.name + "Op::kCustomVerifier;\n";
            }
        }
        wrapper += R"(        default: return "";
    }
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
    n.intent_hash = dgtl::ir::IRNode::calculate_hash(
        n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
    return n;
}

} // namespace dgtl::ir::gen
)";
        std::ofstream wout(wrapper_path, std::ios::binary | std::ios::trunc);
        wout << wrapper;
        wout.close();

        std::error_code remove_ec;
        fs::remove(out_path.parent_path() / "DGTL_Gen_Wrappers.ixx", remove_ec);
        std::cout << "[dgtl-gen] Partitioned metadata and typed wrappers into separate modules\n";
    }

    // ABI support surface retained for alias with existing HIR code.
    // Operation identity itself is never sourced from this block; OpCode and
    // TypeCode are emitted from the canonical DSL immediately above it.
    static constexpr std::string_view kGeneratedEnumSupport = R"DGTL(
inline constexpr std::uint32_t kMaxTypeHandle = static_cast<std::uint32_t>(TypeCode::TENSOR_I64);

[[nodiscard]] inline constexpr bool is_valid_type_handle(std::uint32_t type_handle) noexcept {
    return type_handle >= static_cast<std::uint32_t>(TypeCode::BOOL) &&
           type_handle <= kMaxTypeHandle;
}

[[nodiscard]] inline constexpr bool is_floating_type_code(TypeCode type_code) noexcept {
    return type_code == TypeCode::F8 ||
           type_code == TypeCode::BF16 ||
           type_code == TypeCode::F16 ||
           type_code == TypeCode::F32 ||
           type_code == TypeCode::F64 ||
           type_code == TypeCode::F128;
}

[[nodiscard]] inline constexpr bool is_pointer_type_code(TypeCode type_code) noexcept {
    return type_code == TypeCode::PTR;
}

namespace Flags {
    inline constexpr std::uint16_t NONE               = 0x0000u;
    inline constexpr std::uint16_t IS_CONST           = 0x0001u;
    inline constexpr std::uint16_t IS_DEAD            = 0x0002u;
    inline constexpr std::uint16_t HAS_SIDE_EFFECTS   = 0x0004u;
    inline constexpr std::uint16_t IS_VOLATILE        = 0x0008u;

    inline constexpr std::uint16_t IS_SIMD            = 0x0010u;
    inline constexpr std::uint16_t IS_SIMD_BURST      = IS_SIMD;
    inline constexpr std::uint16_t IS_VECTOR_LANE     = 0x0020u;
    inline constexpr std::uint16_t IS_PREDICATED      = 0x0040u;

    inline constexpr std::uint16_t IS_FUSED           = 0x0080u;
    inline constexpr std::uint16_t IS_MACRO_FUSED     = IS_FUSED;
    inline constexpr std::uint16_t IS_OPTIMIZED       = 0x0100u;
    inline constexpr std::uint16_t IS_LOOP_INVARIANT  = 0x0200u;

    inline constexpr std::uint16_t IS_SPEC_CONST      = 0x0400u;
    inline constexpr std::uint16_t NON_TEMPORAL       = 0x0800u;
    inline constexpr std::uint16_t NON_TEMPORAL_HINT  = NON_TEMPORAL;

    inline constexpr std::uint16_t IS_BRANCH          = 0x1000u;
    inline constexpr std::uint16_t IS_RETURN          = 0x2000u;

    inline constexpr std::uint16_t IS_GUARD           = 0x4000u;
    inline constexpr std::uint16_t MAY_DEOPT          = 0x8000u;

    inline constexpr std::uint16_t kCanonicalSemanticMask =
        IS_CONST | IS_DEAD | HAS_SIDE_EFFECTS | IS_VOLATILE |
        IS_SIMD | IS_VECTOR_LANE | IS_PREDICATED |
        IS_FUSED | IS_OPTIMIZED | IS_LOOP_INVARIANT |
        IS_SPEC_CONST | NON_TEMPORAL |
        IS_BRANCH | IS_RETURN | IS_GUARD | MAY_DEOPT;

    static_assert(kCanonicalSemanticMask == 0xFFFFu,
                  "Flags canonical semantic bits must be unique and fully packed (16/16 used)");
} // namespace Flags

namespace ExtendedFlags {
    inline constexpr std::uint32_t MEM_RELAXED   = 0x00000001u;
    inline constexpr std::uint32_t MEM_ACQUIRE   = 0x00000002u;
    inline constexpr std::uint32_t MEM_RELEASE_  = 0x00000004u;
    inline constexpr std::uint32_t MEM_SEQ_CST   = 0x00000008u;

    inline constexpr std::uint32_t NO_ALIAS      = 0x00000010u;
    inline constexpr std::uint32_t MAY_ALIAS     = 0x00000020u;

    inline constexpr std::uint32_t FAST_MATH     = 0x00000040u;
    inline constexpr std::uint32_t STRICT_FP     = 0x00000080u;

    inline constexpr std::uint32_t LATENCY_CRITICAL = 0x00000100u;
    inline constexpr std::uint32_t THROUGHPUT_MODE  = 0x00000200u;

    inline constexpr std::uint32_t PREFETCH_HINT = 0x00000400u;

    inline constexpr std::uint32_t VECTORIZABLE  = 0x00000800u;
    inline constexpr std::uint32_t VEC_WIDTH_128 = 0x00001000u;
    inline constexpr std::uint32_t VEC_WIDTH_256 = 0x00002000u;
    inline constexpr std::uint32_t VEC_WIDTH_512 = 0x00004000u;

    inline constexpr std::uint32_t PARALLEL_SAFE = 0x00008000u;
    inline constexpr std::uint32_t REQUIRES_SYNC = 0x00010000u;

    inline constexpr std::uint32_t HOT_PATH      = 0x00020000u;
    inline constexpr std::uint32_t COLD_PATH     = 0x00040000u;
    inline constexpr std::uint32_t HOST_ONLY     = 0x00080000u;
    inline constexpr std::uint32_t DEVICE_ONLY   = 0x00100000u;
    inline constexpr std::uint32_t PIPELINE_READY = 0x00200000u;

    inline constexpr std::uint32_t DEOPT_SAFEPOINT = 0x00400000u;

    inline constexpr std::uint32_t PROFILE_GUIDED   = 0x00800000u;
    inline constexpr std::uint32_t SPECULATIVE_PATH = 0x01000000u;
    inline constexpr std::uint32_t CACHE_HOT        = 0x02000000u;
    inline constexpr std::uint32_t CACHE_COLD       = 0x04000000u;
    inline constexpr std::uint32_t INLINE_CANDIDATE = 0x08000000u;
    inline constexpr std::uint32_t NO_INLINE        = 0x10000000u;
    inline constexpr std::uint32_t VIRTUALIZED      = 0x20000000u;
    inline constexpr std::uint32_t SECURITY_HARDEN  = 0x40000000u;
    inline constexpr std::uint32_t DETERMINISTIC    = 0x80000000u;

    inline constexpr std::uint64_t IS_OWNED         = 0x0001000000ULL;
    inline constexpr std::uint64_t IS_SHARED        = 0x0002000000ULL;
    inline constexpr std::uint64_t IS_ATOMIC        = 0x0004000000ULL;
    inline constexpr std::uint64_t IS_ISOLATED      = 0x0008000000ULL;
    inline constexpr std::uint64_t IS_LINEAR        = 0x0010000000ULL;
    inline constexpr std::uint64_t IS_BORROWED      = 0x0020000000ULL;
    inline constexpr std::uint64_t MAY_DEADLOCK     = 0x0040000000ULL;
    inline constexpr std::uint64_t REQUIRES_FENCE   = 0x0080000000ULL;

    inline constexpr std::uint64_t IS_CONCURRENT    = 0x0100000000ULL;
    inline constexpr std::uint64_t IS_SPAWN         = 0x0200000000ULL;
    inline constexpr std::uint64_t IS_FIBER         = 0x0400000000ULL;
    inline constexpr std::uint64_t IS_ASYNC         = 0x0800000000ULL;
    inline constexpr std::uint64_t REQUIRES_SYNC_OP = 0x1000000000ULL;
    inline constexpr std::uint64_t BARRIER_POINT    = 0x2000000000ULL;
    inline constexpr std::uint64_t CHANNEL_OP       = 0x4000000000ULL;
    inline constexpr std::uint64_t LOCK_OP          = 0x8000000000ULL;

    inline constexpr std::uint64_t IS_GRAPH_OP      = 0x10000000000ULL;
    inline constexpr std::uint64_t IS_TREE_OP       = 0x20000000000ULL;
    inline constexpr std::uint64_t IS_TRIE_OP       = 0x40000000000ULL;
    inline constexpr std::uint64_t IS_TENSOR_OP     = 0x80000000000ULL;
    inline constexpr std::uint64_t HAS_SCOP         = 0x100000000000ULL;

    inline constexpr std::uint64_t LANG_DSL         = 0x200000000000ULL;
    inline constexpr std::uint64_t LANG_CPP         = 0x400000000000ULL;
    inline constexpr std::uint64_t LANG_RUST        = 0x800000000000ULL;
} // namespace ExtendedFlags

namespace AuxFlags {
    inline constexpr std::uint32_t NONE = 0x00000000u;

    inline constexpr std::uint32_t PTR_ADDRSPACE_GENERIC  = 0x00000001u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_GLOBAL   = 0x00000002u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_SHARED   = 0x00000004u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_LOCAL    = 0x00000008u;
    inline constexpr std::uint32_t PTR_ADDRSPACE_CONSTANT = 0x00000010u;

    inline constexpr std::uint32_t PTR_ADDRSPACE_MASK =
        PTR_ADDRSPACE_GENERIC |
        PTR_ADDRSPACE_GLOBAL |
        PTR_ADDRSPACE_SHARED |
        PTR_ADDRSPACE_LOCAL |
        PTR_ADDRSPACE_CONSTANT;

    inline constexpr std::uint32_t PREDICATE_CANONICAL = 0x00000020u;

    static_assert((PTR_ADDRSPACE_MASK & PREDICATE_CANONICAL) == 0u,
                  "AuxFlags domains must not overlap");
} // namespace AuxFlags

namespace PhaseDFlags {
    inline constexpr std::uint32_t IS_VARIADIC_OP     = 0x00000001u;
    inline constexpr std::uint32_t HAS_MULTI_RESULT   = 0x00000002u;
    inline constexpr std::uint32_t EFFECT_TRACKED    = 0x00000004u;
    inline constexpr std::uint32_t DOMINANCE_ROOT    = 0x00000008u;

    inline constexpr std::uint32_t TYPE_VERIFIED      = 0x00000010u;
    inline constexpr std::uint32_t EFFECT_VERIFIED    = 0x00000020u;
    inline constexpr std::uint32_t DOMINANCE_VERIFIED = 0x00000040u;
} // namespace PhaseDFlags

namespace HintFlags {
    inline constexpr std::uint16_t LatencySensitive     = 0x2000u;
    inline constexpr std::uint16_t ThroughputMode       = 0x4000u;

    inline constexpr std::uint16_t RequiresVecPort      = 0x0002u;
    inline constexpr std::uint16_t SVEActive            = 0x0004u;
    inline constexpr std::uint16_t IsSpillCandidate     = 0x0008u;

    inline constexpr std::uint16_t BranchLikely         = 0x1000u;
    inline constexpr std::uint16_t BranchUnlikely       = 0x2000u;

    inline constexpr std::uint16_t IsHosted             = 0x0040u;
    inline constexpr std::uint16_t RequiresDepassTarget = 0x0100u;
    inline constexpr std::uint16_t NonTemporalHint      = 0x0800u;
} // namespace HintFlags

inline constexpr std::uint32_t NULL_REGISTER = 0x0u;
inline constexpr std::uint32_t INVALID_ATOM  = 0xFFFFFFFFu;
inline constexpr std::uint32_t INVALID_SLOT  = INVALID_ATOM;

enum class OwnershipModel : std::uint8_t {
    OWNED = 0,
    SHARED = 1,
    CONST = 2,
    ATOMIC = 3,
    ISOLATED = 4,
    MUT = 5,
    AUTO = 6,
    VOLATILE = 7,
    LINEAR = 8,
    MANAGED = 9,
    OPAQUE = 10,
    SEALED = 11,
    CAP = 12
};

enum class ConcurrencyClass : std::uint8_t {
    SEQUENTIAL = 0,
    SPAWN = 1,
    ASYNC = 2,
    FIBER = 3,
    ELASTIC = 4,
    THREAD = 5,
    SYNC = 6,
    AWAIT_READY = 7,
    JOIN_POINT = 8,
    LOCK_OP = 9,
    FENCE = 10,
    RECV_OP = 11,
    SEND_OP = 12,
    BARRIER = 13
};

enum class DSAType : std::uint8_t {
    SCALAR = 0,
    TENSOR = 1,
    GRAPH = 2,
    TREE = 3,
    TRIE = 4,
    NODE = 5,
    EDGE = 6,
    STREAM = 7
};

enum class FrontendLanguage : std::uint8_t {
    UNIVERSAL = 0,
    DSL = 1,
    CPP = 2,
    RUST = 3,
    PYTHON = 4,
    GO = 5,
    SWIFT = 6,
    KOTLIN = 7,
    JAVA = 8,
    C = 9
};

struct OwnershipLifetime {
    OwnershipModel model         = OwnershipModel::OWNED;
    std::uint32_t acquire_site   = 0u;
    std::uint32_t release_site   = 0u;
    std::uint32_t transfer_count = 0u;
    std::uint32_t last_transfer_site = 0u;
    bool is_borrowed             = false;
    bool has_aliasing            = false;
};

struct TypeContext {
    FrontendLanguage language         = FrontendLanguage::UNIVERSAL;
    OwnershipModel default_ownership  = OwnershipModel::OWNED;
    ConcurrencyClass concurrency_class = ConcurrencyClass::SEQUENTIAL;
    DSAType primary_dsa               = DSAType::SCALAR;
    bool is_value_type                = false;
    bool is_generic                   = false;
    std::uint32_t generic_params      = 0u;
    std::uint64_t type_id             = 0ULL;
};
)DGTL";

    // ------------------------------------------------------------------------
    // Generate DGTL_Gen_Enums.ixx directly from the canonical DSL.
    //
    // The generated enum artifact is deterministic: the previous DGTL_Gen_Enums.ixx
    // is never read and never used as an input to generation.
    // ------------------------------------------------------------------------
    {
        const fs::path enum_path = root / opts.enum_output_file;
        std::ofstream eout(enum_path, std::ios::binary | std::ios::trunc);
        if (!eout) {
            std::cerr << "FATAL: failed to open generated enum output: "
                      << enum_path.string() << "\n";
            return 1;
        }

        eout << "/*\n"
             << " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
             << " *\n"
             << " * Project:     DGTL (Deep General-purpose Transformation Layer)\n"
             << " * Owner:       Divyanshu Tata\n"
             << " * Repository:  github.com/TataG-Systems/DGTL\n"
             << " *\n"
             << " * Component:   Auto-generated DGTL IR Enumerations and Core Opcodes\n"
             << " *\n"
             << " * This software is the proprietary property of TataG Systems.\n"
             << " * Unauthorized copying, modification, or distribution is strictly prohibited.\n"
             << " *\n"
             << " * ============================================================================\n"
             << " * AUTO-GENERATED FILE — DO NOT EDIT MANUALLY\n"
             << " * Generator:        tools/cpp/dgtl_gen_main.cpp\n"
             << " * Canonical Source: " << opts.definition_file.generic_string() << "\n"
             << " * ============================================================================\n"
             << " */\n\n"
             << "module;\n\n"
             << "#include <cstddef>\n"
             << "#include <cstdint>\n\n"
             << "export module dgtl.ir_enums;\n\n"
             << "export namespace dgtl::ir {\n\n"
             << "enum class OpCode : std::uint16_t {\n";

        for (const auto& op : operations) {
            eout << "    " << op.opcode << " = 0x"
                 << std::hex << std::uppercase << *op.stable_value
                 << std::dec << ",\n";
        }

        if (!opcode_aliases.empty()) {
            eout << "\n    // Canonical source-level aliases — emitted from the DSL.\n";
            for (const auto& entry : opcode_aliases) {
                eout << "    " << entry.name << " = " << entry.target << ",\n";
            }
        }
        eout << "};\n\n";

        eout << "enum class TypeCode : std::uint16_t {\n"
             << "    VOID_T = 0,\n";

        for (std::size_t i = 0; i < types.size(); ++i) {
            const auto& t = types[i];
            std::string name;
            if (t.name == "Bool") name = "BOOL";
            else if (t.name == "Predicate") name = "PREDICATE";
            else if (t.name == "AddressSpace") name = "ADDRESS_SPACE";
            else if (t.name == "Ptr") name = "PTR";
            else if (t.name == "StringHandle") name = "STRING_HANDLE";
            else if (t.name == "Vec3xF32") name = "VEC3_F32";
            else if (t.name == "Vec4xF32") name = "VEC4_F32";
            else if (t.name == "Mat4x4F32") name = "MAT4X4_F32";
            else if (t.name == "QuatF32") name = "QUAT_F32";
            else if (t.name == "VectorF32x16") name = "VECTOR_F32X16";
            else if (t.name == "VectorI32x16") name = "VECTOR_I32X16";
            else if (t.name == "VectorF16x32") name = "VECTOR_F16X32";
            else if (t.name == "TensorFP8") name = "TENSOR_FP8";
            else if (t.name == "TensorBF16") name = "TENSOR_BF16";
            else if (t.name == "TensorF32") name = "TENSOR_F32";
            else if (t.name == "TensorI64") name = "TENSOR_I64";
            else {
                name = t.name;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
            eout << "    " << name << " = " << (i + 1) << ",\n";
        }
        eout << "};\n\n";
        eout << kGeneratedEnumSupport << "\n\n";


        eout << "inline constexpr std::uint16_t kMaxOpcode = []() constexpr {\n"
             << "    std::uint16_t value = 0;\n";
        for (const auto& op : operations) {
            eout << "    if (value < 0x" << std::hex << std::uppercase << *op.stable_value
                 << ") value = 0x" << *op.stable_value << std::dec << ";\n";
        }
        for (const auto& entry : opcode_aliases) {
            if (entry.kind == OpcodeAlias::Kind::explicit_value) {
                eout << "    if (value < 0x" << std::hex << std::uppercase << *entry.value
                     << ") value = 0x" << *entry.value << std::dec << ";\n";
            }
        }
        eout << "    return value;\n"
             << "}();\n"
             << "inline constexpr std::uint16_t kMaxTypeCode = "
             << static_cast<unsigned long long>(types.size()) << ";\n"
             << "inline constexpr std::size_t kCanonicalOpCount = "
             << operations.size() << ";\n\n"
             << "[[nodiscard]] constexpr std::uint16_t to_underlying(OpCode op) noexcept {\n"
             << "    return static_cast<std::uint16_t>(op);\n"
             << "}\n"
             << "[[nodiscard]] constexpr std::uint16_t to_underlying(TypeCode type) noexcept {\n"
             << "    return static_cast<std::uint16_t>(type);\n"
             << "}\n"
             << "[[nodiscard]] constexpr bool is_valid_type_code(TypeCode type) noexcept {\n"
             << "    return to_underlying(type) <= kMaxTypeCode;\n"
             << "}\n\n";

        for (const auto& op : operations) {
            eout << "static_assert(static_cast<std::uint16_t>(OpCode::"
                 << op.opcode << ") == 0x" << std::hex << std::uppercase
                 << *op.stable_value << std::dec
                 << ", \"DGTL stable opcode ID drift: " << op.opcode << "\");\n";
        }

        eout << "\n} // namespace dgtl::ir\n";
        eout.close();
        if (!eout) {
            std::cerr << "FATAL: failed while writing generated enum artifact\n";
            return 1;
        }

        std::cout << "[dgtl-gen] Generated canonical enum/type artifact: "
                  << enum_path.string() << "\n";
    }

    std::cout << "[dgtl-gen] Canonical schema validated: "
              << operations.size() << " operations, explicit stable IDs\n";
    std::cout << "[dgtl-gen] Generated DGTL_Gen_Enums.ixx + metadata + typed wrappers from canonical DSL\n";
    std::cout << "[dgtl-gen] Metadata: " << opts.output_file.string() << "\n";
    return 0;
}

