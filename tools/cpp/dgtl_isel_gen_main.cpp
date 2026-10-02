/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project: DGTL (Deep General-purpose Transformation Layer)
 * Component: Multi-target ISel contract compiler / generator
 *
 * This file is generated-source infrastructure. It is intentionally the only
 * place where the textual ISel contracts are parsed and converted into C++.
 *
 * C++26 / Clang 18+ / GCC 14+ / MSVC 19.40+
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kGeneratorVersion = "3.1.0";
constexpr std::size_t kOpcodeTableSize = 65536;

struct SourceLocation {
    std::string file;
    std::size_t line = 0;
};

enum class DiagnosticSeverity : std::uint8_t { Note, Warning, Error };

struct Diagnostic {
    DiagnosticSeverity severity;
    SourceLocation loc;
    std::string message;
};

class DiagnosticEngine {
public:
    void report(DiagnosticSeverity severity, const SourceLocation& loc, std::string message) {
        diagnostics_.push_back({severity, loc, std::move(message)});
    }
    void error(const SourceLocation& loc, std::string message) {
        report(DiagnosticSeverity::Error, loc, std::move(message));
    }
    void warning(const SourceLocation& loc, std::string message) {
        report(DiagnosticSeverity::Warning, loc, std::move(message));
    }
    [[nodiscard]] bool has_errors() const {
        return std::any_of(diagnostics_.begin(), diagnostics_.end(),
            [](const Diagnostic& d) { return d.severity == DiagnosticSeverity::Error; });
    }
    void print() const {
        for (const auto& d : diagnostics_) {
            const char* level =
                d.severity == DiagnosticSeverity::Error ? "error" :
                d.severity == DiagnosticSeverity::Warning ? "warning" : "note";
            std::cerr << "[dgtl-isel-gen] " << level << ": "
                      << d.loc.file << ":" << d.loc.line << ": "
                      << d.message << "\n";
        }
    }
private:
    std::vector<Diagnostic> diagnostics_;
};

struct Options {
    fs::path root{"."};
    std::string target_filter;
    bool verbose = false;
    bool strict = true;
};

enum class ParseResult { Ok, Help, Error };

struct SemanticContract {
    std::string name;
    std::uint16_t value = 0;
};

struct SemanticContractFile {
    std::uint32_t version = 0;
    std::map<std::string, SemanticContract> semantics;
};

struct MachineFormSpec {
    std::string name;
    std::uint16_t value = 0;
    std::set<std::string> kinds;
    std::map<std::string, std::string> kind_semantics;
    std::set<std::string> allowed_semantics;
    std::set<std::string> requirements;
    std::optional<std::string> default_semantic;
};

struct MachineFormContract {
    std::uint32_t version = 0;
    std::map<std::string, MachineFormSpec> forms;
};

struct CanonicalIRSchema {
    std::set<std::string> canonical_opcodes;
    std::set<std::string> spellings;
    std::unordered_map<std::string, std::string> aliases;
    std::map<std::string, std::uint16_t> values;
};

struct TargetSpec {
    std::string arch_name;
    std::string isel_file;
    fs::path output_dir;
    std::string generated_module;
    std::string rules_module;
    std::string file_prefix;
    std::set<std::string> aliases;
    std::set<std::string> capabilities;
    std::vector<std::string> arch_enums;
    std::vector<std::string> object_formats;
};

struct DslNode {
    std::string raw_line;
    SourceLocation loc;
};

struct FormMapping {
    std::string opcode;
    std::string form;
    std::string kind;
    std::string semantic;
    SourceLocation loc;
    bool runtime_backed = false;
};

struct RuleMetadata {
    std::string name;
    std::set<std::string> targets;
    std::set<std::string> requirements;
    std::optional<int> benefit;
    SourceLocation loc;
};

[[nodiscard]] std::string trim(std::string s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    auto ws = [](unsigned char c) { return std::isspace(c) != 0 || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

[[nodiscard]] std::string strip_hash_comment(std::string s) {
    const auto p = s.find('#');
    if (p != std::string::npos) s.erase(p);
    return s;
}

[[nodiscard]] bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

[[nodiscard]] std::string cpp_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

[[nodiscard]] std::vector<std::string> split_list(std::string_view value, char delimiter = '|') {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const auto end = value.find(delimiter, pos);
        const auto stop = end == std::string_view::npos ? value.size() : end;
        const auto item = trim(std::string(value.substr(pos, stop - pos)));
        if (!item.empty()) out.push_back(item);
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    return out;
}

[[nodiscard]] std::map<std::string, std::string> parse_attributes(
    std::string_view attrs, const SourceLocation& loc, DiagnosticEngine& diag) {
    std::map<std::string, std::string> result;
    std::size_t pos = 0;
    while (pos < attrs.size()) {
        std::size_t comma = attrs.size();
        bool quoted = false;
        for (std::size_t i = pos; i < attrs.size(); ++i) {
            if (attrs[i] == '"') quoted = !quoted;
            else if (attrs[i] == ',' && !quoted) { comma = i; break; }
        }
        std::string item = trim(std::string(attrs.substr(pos, comma - pos)));
        pos = (comma == attrs.size()) ? attrs.size() : comma + 1;
        if (item.empty()) continue;

        const auto eq = item.find('=');
        if (eq == std::string::npos || eq == 0) {
            diag.error(loc, "expected key=value attribute, got '" + item + "'");
            continue;
        }
        std::string key = trim(item.substr(0, eq));
        std::string value = trim(item.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (!result.emplace(key, value).second)
            diag.error(loc, "duplicate attribute '" + key + "'");
    }
    return result;
}

[[nodiscard]] std::optional<std::uint64_t> parse_uint(
    std::string_view text, const SourceLocation& loc, DiagnosticEngine& diag) {
    try {
        std::size_t consumed = 0;
        const auto v = std::stoull(std::string(text), &consumed, 0);
        if (consumed != text.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (...) {
        diag.error(loc, "invalid unsigned integer '" + std::string(text) + "'");
        return std::nullopt;
    }
}

[[nodiscard]] bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--root") {
            if (++i >= argc) return false;
            opts.root = argv[i];
        } else if (arg == "--target") {
            if (++i >= argc) return false;
            opts.target_filter = argv[i];
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--strict") {
            opts.strict = true;
        } else if (arg == "--no-strict") {
            opts.strict = false;
        } else if (arg == "--help") {
            std::cout
                << "Usage: dgtl-isel-gen [--root <path>] "
                << "[--target <canonical-target-or-alias>] "
                << "[--verbose] [--strict|--no-strict]\n";
            return true;
        } else {
            std::cerr << "[dgtl-isel-gen] unknown argument: " << arg << "\n";
            return false;
        }
    }
    return true;
}

[[nodiscard]] SemanticContractFile load_semantic_contract(
    const fs::path& path, DiagnosticEngine& diag) {
    SemanticContractFile out;
    std::ifstream in(path);
    if (!in) {
        diag.error({path.string(), 0}, "cannot open semantic contract");
        return out;
    }
    std::string line;
    std::size_t line_no = 0;
    std::set<std::uint16_t> values;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string t = trim(strip_hash_comment(line));
        if (t.empty()) continue;
        const SourceLocation loc{path.generic_string(), line_no};

        if (starts_with(t, "version:")) {
            auto v = parse_uint(trim(t.substr(8)), loc, diag);
            if (v) out.version = static_cast<std::uint32_t>(*v);
            continue;
        }
        if (!starts_with(t, "semantic ")) {
            diag.error(loc, "unknown semantic-contract directive '" + t + "'");
            continue;
        }
        const auto colon = t.find(':');
        if (colon == std::string::npos) {
            diag.error(loc, "malformed semantic declaration");
            continue;
        }
        SemanticContract sem;
        sem.name = trim(t.substr(9, colon - 9));
        auto attrs = parse_attributes(t.substr(colon + 1), loc, diag);
        auto it = attrs.find("value");
        if (it == attrs.end()) {
            diag.error(loc, "semantic requires value=");
            continue;
        }
        auto v = parse_uint(it->second, loc, diag);
        if (!v || *v > 65535) continue;
        sem.value = static_cast<std::uint16_t>(*v);
        if (!out.semantics.emplace(sem.name, sem).second)
            diag.error(loc, "duplicate semantic '" + sem.name + "'");
        if (!values.insert(sem.value).second)
            diag.error(loc, "duplicate semantic value " + std::to_string(sem.value));
    }
    if (out.version == 0) diag.error({path.generic_string(), 0}, "missing semantic contract version");
    return out;
}

[[nodiscard]] MachineFormContract load_machine_form_contract(
    const fs::path& path, const SemanticContractFile& semantics, DiagnosticEngine& diag) {
    MachineFormContract out;
    std::ifstream in(path);
    if (!in) {
        diag.error({path.string(), 0}, "cannot open machine-form contract");
        return out;
    }
    std::string line;
    std::size_t line_no = 0;
    std::set<std::uint16_t> values;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string t = trim(strip_hash_comment(line));
        if (t.empty()) continue;
        const SourceLocation loc{path.generic_string(), line_no};

        if (starts_with(t, "version:")) {
            auto v = parse_uint(trim(t.substr(8)), loc, diag);
            if (v) out.version = static_cast<std::uint32_t>(*v);
            continue;
        }
        if (!starts_with(t, "form ")) {
            diag.error(loc, "unknown machine-form directive '" + t + "'");
            continue;
        }

        const auto colon = t.find(':');
        if (colon == std::string::npos) {
            diag.error(loc, "malformed machine-form declaration");
            continue;
        }

        MachineFormSpec form;
        form.name = trim(t.substr(5, colon - 5));
        auto attrs = parse_attributes(t.substr(colon + 1), loc, diag);

        auto value_it = attrs.find("value");
        if (value_it == attrs.end()) {
            diag.error(loc, "machine form requires value=");
            continue;
        }
        auto value = parse_uint(value_it->second, loc, diag);
        if (!value || *value > 255) continue;
        form.value = static_cast<std::uint16_t>(*value);

        if (auto it = attrs.find("kinds"); it != attrs.end())
            { const auto values = split_list(it->second); form.kinds.insert(values.begin(), values.end()); }
        if (auto it = attrs.find("requires"); it != attrs.end())
            { const auto values = split_list(it->second); form.requirements.insert(values.begin(), values.end()); }
        if (auto it = attrs.find("allowed_semantics"); it != attrs.end())
            { const auto values = split_list(it->second); form.allowed_semantics.insert(values.begin(), values.end()); }

        if (auto it = attrs.find("semantic"); it != attrs.end()) {
            const auto vals = split_list(it->second);
            if (vals.size() == 1) form.default_semantic = vals.front();
            else diag.error(loc, "semantic= accepts one default semantic; use allowed_semantics for multiple");
        }

        if (auto it = attrs.find("kind_semantics"); it != attrs.end()) {
            for (const auto& pair : split_list(it->second)) {
                const auto colon2 = pair.find(':');
                if (colon2 == std::string::npos) {
                    diag.error(loc, "kind_semantics entry must be Kind:Semantic");
                    continue;
                }
                const auto kind = trim(pair.substr(0, colon2));
                const auto sem = trim(pair.substr(colon2 + 1));
                if (!form.kinds.contains(kind)) {
                    diag.error(loc, "kind_semantics references undeclared kind '" + kind + "'");
                }
                if (!semantics.semantics.contains(sem)) {
                    diag.error(loc, "kind_semantics references unknown semantic '" + sem + "'");
                }
                form.kind_semantics[kind] = sem;
            }
        }

        if (!form.default_semantic && form.kinds.empty() && form.allowed_semantics.empty() && form.name != "Unhandled") {
            diag.error(loc, "machine form needs semantic= or kinds=");
        }
        if (form.default_semantic && !semantics.semantics.contains(*form.default_semantic))
            diag.error(loc, "unknown default semantic '" + *form.default_semantic + "'");

        for (const auto& sem : form.allowed_semantics) {
            if (!semantics.semantics.contains(sem))
                diag.error(loc, "unknown allowed semantic '" + sem + "'");
        }

        if (!out.forms.emplace(form.name, form).second)
            diag.error(loc, "duplicate machine form '" + form.name + "'");
        if (!values.insert(form.value).second)
            diag.error(loc, "duplicate machine-form value " + std::to_string(form.value));
    }
    if (out.version == 0) diag.error({path.generic_string(), 0}, "missing machine-form contract version");
    return out;
}

[[nodiscard]] CanonicalIRSchema load_ir_schema(
    const fs::path& path, DiagnosticEngine& diag) {
    CanonicalIRSchema out;
    std::ifstream in(path);
    if (!in) {
        diag.error({path.string(), 0}, "cannot open canonical IR definition");
        return out;
    }
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string t = trim(strip_hash_comment(line));
        if (t.empty()) continue;
        const SourceLocation loc{path.generic_string(), line_no};

        if (starts_with(t, "op ")) {
            const auto colon = t.find(':');
            if (colon == std::string::npos) {
                diag.error(loc, "malformed op declaration");
                continue;
            }
            const std::string attrs_text = t.substr(colon + 1);
            auto attrs = parse_attributes(attrs_text, loc, diag);
            const auto op_it = attrs.find("opcode");
            if (op_it == attrs.end()) {
                diag.error(loc, "op declaration requires opcode=");
                continue;
            }
            const std::string opcode = op_it->second;
            out.canonical_opcodes.insert(opcode);
            out.spellings.insert(opcode);
            if (auto value_it = attrs.find("value"); value_it != attrs.end()) {
                auto value = parse_uint(value_it->second, loc, diag);
                if (value && *value <= 65535) {
                    if (!out.values.emplace(opcode, static_cast<std::uint16_t>(*value)).second)
                        diag.error(loc, "duplicate opcode value/name '" + opcode + "'");
                }
            }
            continue;
        }

        if (starts_with(t, "alias ")) {
            const auto eq = t.find('=');
            if (eq == std::string::npos) {
                diag.error(loc, "malformed alias declaration");
                continue;
            }
            const std::string alias = trim(t.substr(6, eq - 6));
            const std::string canonical = trim(t.substr(eq + 1));
            if (!out.aliases.emplace(alias, canonical).second)
                diag.error(loc, "duplicate alias '" + alias + "'");
            out.spellings.insert(alias);
            continue;
        }
    }

    for (const auto& [alias, canonical] : out.aliases) {
        if (!out.canonical_opcodes.contains(canonical))
            diag.error({path.generic_string(), 0},
                       "alias '" + alias + "' points to unknown canonical opcode '" + canonical + "'");
    }

    for (const auto& [alias, ignored] : out.aliases) {
        std::set<std::string> seen;
        std::string cur = alias;
        while (true) {
            if (!seen.insert(cur).second) {
                diag.error({path.generic_string(), 0}, "cyclic opcode alias involving '" + alias + "'");
                break;
            }
            auto it = out.aliases.find(cur);
            if (it == out.aliases.end()) break;
            cur = it->second;
        }
    }

    return out;
}

[[nodiscard]] std::string canonical_opcode(
    std::string opcode, const CanonicalIRSchema& schema) {
    std::set<std::string> seen;
    while (true) {
        const auto it = schema.aliases.find(opcode);
        if (it == schema.aliases.end()) break;
        if (!seen.insert(opcode).second) break;
        opcode = it->second;
    }
    return opcode;
}

[[nodiscard]] std::vector<TargetSpec> load_target_contract(
    const fs::path& path, DiagnosticEngine& diag) {
    std::vector<TargetSpec> targets;
    std::ifstream in(path);
    if (!in) {
        diag.error({path.string(), 0}, "cannot open target contract");
        return targets;
    }

    TargetSpec* current = nullptr;
    std::unordered_set<std::string> names;
    std::string line;
    std::size_t line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        const std::string t = trim(strip_hash_comment(line));
        if (t.empty()) continue;
        const SourceLocation loc{path.generic_string(), line_no};

        if (starts_with(t, "version:")) continue;

        if (starts_with(t, "target ")) {
            const auto colon = t.find(':');
            if (colon == std::string::npos) {
                diag.error(loc, "malformed target declaration");
                current = nullptr;
                continue;
            }
            TargetSpec target;
            target.arch_name = trim(t.substr(7, colon - 7));
            if (target.arch_name.empty()) {
                diag.error(loc, "target name cannot be empty");
                current = nullptr;
                continue;
            }
            if (!names.insert(target.arch_name).second) {
                diag.error(loc, "duplicate target '" + target.arch_name + "'");
                current = nullptr;
                continue;
            }
            targets.push_back(std::move(target));
            current = &targets.back();
            continue;
        }

        if (!current) {
            diag.error(loc, "target property appears before a target declaration");
            continue;
        }

        const auto eq = t.find('=');
        if (eq == std::string::npos) {
            diag.error(loc, "target property must be key=value");
            continue;
        }
        const std::string key = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));

        if (key == "isel_file") current->isel_file = value;
        else if (key == "output_dir") current->output_dir = value;
        else if (key == "generated_module") current->generated_module = value;
        else if (key == "rules_module") current->rules_module = value;
        else if (key == "file_prefix") current->file_prefix = value;
        else if (key == "aliases") {
            const auto values = split_list(value); current->aliases.insert(values.begin(), values.end());
        } else if (key == "capabilities") {
            const auto values = split_list(value); current->capabilities.insert(values.begin(), values.end());
        } else if (key == "arch_enums") {
            current->arch_enums = split_list(value);
        } else if (key == "object_formats") {
            current->object_formats = split_list(value);
        } else {
            diag.error(loc, "unknown target property '" + key + "'");
        }
    }

    for (const auto& t : targets) {
        if (t.isel_file.empty() || t.output_dir.empty() || t.generated_module.empty() ||
            t.rules_module.empty() || t.file_prefix.empty()) {
            diag.error({path.generic_string(), 0},
                       "target '" + t.arch_name + "' is missing required output/input properties");
        }
        if (t.aliases.empty()) diag.error({path.generic_string(), 0},
            "target '" + t.arch_name + "' has no aliases");
        if (t.arch_enums.empty()) {
            diag.warning({path.generic_string(), 0},
                "target '" + t.arch_name + "' has no declared arch_enums; it cannot be reached by TargetMachine dispatch");
        }
    }

    return targets;
}

[[nodiscard]] bool parse_form_line(
    std::string_view line, FormMapping& mapping) {
    const std::string t = trim(std::string(line));
    if (!starts_with(t, "form ")) return false;
    const auto colon = t.find(':');
    if (colon == std::string::npos || colon <= 5) return false;

    std::size_t pos = 0;
    while (pos < t.size() - colon - 1) {
        const std::size_t base = colon + 1 + pos;
        std::size_t comma = t.find(',', base);
        if (comma == std::string::npos) comma = t.size();
        std::string item = trim(t.substr(base, comma - base));
        pos = (comma == t.size()) ? t.size() - colon - 1 : comma - colon;
        if (item.empty()) {
            if (comma == t.size()) break;
            continue;
        }
        const auto eq = item.find('=');
        if (eq == std::string::npos) return false;
        std::string key = trim(item.substr(0, eq));
        std::string value = trim(item.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (key == "opcode") mapping.opcode = value;
        else if (key == "machine_form") mapping.form = value;
        else if (key == "kind") mapping.kind = value;
        else if (key == "semantic") mapping.semantic = value;
        else if (key == "description") {}
        else return false;

        if (comma == t.size()) break;
    }
    return !mapping.opcode.empty() && !mapping.form.empty();
}

[[nodiscard]] bool parse_rule_metadata(
    std::string_view line, RuleMetadata& meta) {
    const std::string t = trim(std::string(line));
    if (!starts_with(t, "rule ")) return false;
    const auto colon = t.find(':');
    const auto lb = t.rfind('[');
    const auto rb = t.rfind(']');
    if (colon == std::string::npos || lb == std::string::npos || rb == std::string::npos ||
        rb < lb || colon <= 5) return false;

    meta.name = trim(t.substr(5, colon - 5));
    const std::string attrs = t.substr(lb + 1, rb - lb - 1);
    std::size_t pos = 0;
    while (pos < attrs.size()) {
        std::size_t comma = attrs.find(',', pos);
        if (comma == std::string::npos) comma = attrs.size();
        std::string item = trim(attrs.substr(pos, comma - pos));
        pos = comma + 1;
        if (item.empty()) continue;
        const auto eq = item.find('=');
        if (eq == std::string::npos) return false;
        const auto key = trim(item.substr(0, eq));
        const auto value = trim(item.substr(eq + 1));
        if (key == "targets") {
            for (const auto& v : split_list(value)) meta.targets.insert(v);
        } else if (key == "benefit") {
            try {
                std::size_t consumed = 0;
                const int v = std::stoi(value, &consumed);
                if (consumed != value.size() || v < 0) return false;
                meta.benefit = v;
            } catch (...) {
                return false;
            }
        } else if (starts_with(key, "require_")) {
            if (value == "true" || value == "1") meta.requirements.insert(key.substr(8));
            else if (value != "false" && value != "0") return false;
        }
    }
    return !meta.name.empty() && !meta.targets.empty() && meta.benefit.has_value();
}

[[nodiscard]] bool is_common_runtime_source(const SourceLocation& loc) {
    return fs::path(loc.file).filename() == "common_runtime_forms.dgtl_isel";
}

[[nodiscard]] bool resolve_dsl_source(
    const fs::path& file,
    const fs::path& base,
    std::vector<DslNode>& nodes,
    std::set<std::string>& visited,
    std::vector<std::string>& stack,
    DiagnosticEngine& diag) {
    std::error_code ec;
    const fs::path canonical_file = fs::weakly_canonical(file, ec);
    if (ec) {
        diag.error({file.generic_string(), 0}, "cannot canonicalize DSL path");
        return false;
    }
    const fs::path canonical_base = fs::weakly_canonical(base, ec);
    if (ec) {
        diag.error({base.generic_string(), 0}, "cannot canonicalize DSL root");
        return false;
    }

    const auto relative = canonical_file.lexically_relative(canonical_base);
    const auto relstr = relative.generic_string();
    if (relative.empty() || relative.is_absolute() || relstr == ".." || starts_with(relstr, "../")) {
        diag.error({file.generic_string(), 0}, "include escapes the ISA definition root");
        return false;
    }

    const std::string key = canonical_file.generic_string();
    if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
        std::string chain;
        for (const auto& p : stack) {
            if (!chain.empty()) chain += " -> ";
            chain += p;
        }
        diag.error({file.generic_string(), 0}, "cyclic include: " + chain + " -> " + key);
        return false;
    }
    if (visited.contains(key)) return true;

    std::ifstream in(canonical_file);
    if (!in) {
        diag.error({canonical_file.generic_string(), 0}, "cannot open DSL source");
        return false;
    }

    visited.insert(key);
    stack.push_back(key);

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        std::string t = trim(strip_hash_comment(line));
        if (t.empty()) continue;

        if (starts_with(t, "include ")) {
            std::string inc = trim(t.substr(8));
            if (inc.size() >= 2 && inc.front() == '"' && inc.back() == '"')
                inc = inc.substr(1, inc.size() - 2);
            if (!resolve_dsl_source(canonical_base / inc, canonical_base,
                                    nodes, visited, stack, diag)) return false;
            continue;
        }

        nodes.push_back({std::move(t), {canonical_file.generic_string(), line_no}});
    }

    stack.pop_back();
    return true;
}

[[nodiscard]] std::string fnv1a64(std::string_view data) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << hash;
    return out.str();
}

[[nodiscard]] std::string read_file_exact(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] std::string contract_material(
    const fs::path& root,
    const CanonicalIRSchema& schema,
    const SemanticContractFile& semantics,
    const MachineFormContract& forms,
    const std::vector<TargetSpec>& targets,
    const std::map<std::string, std::string>& source_hashes) {
    std::ostringstream m;
    m << "generator=" << kGeneratorVersion << "\n";
    m << "ir_count=" << schema.canonical_opcodes.size() << "\n";
    for (const auto& [a, c] : schema.aliases) m << "alias=" << a << "=" << c << "\n";
    for (const auto& [n, v] : schema.values) m << "opcode=" << n << ":" << v << "\n";
    for (const auto& [n, s] : semantics.semantics) m << "semantic=" << n << ":" << s.value << "\n";
    for (const auto& [n, f] : forms.forms) {
        m << "form=" << n << ":" << f.value << "\n";
        for (const auto& x : f.kinds) m << "kind=" << n << ":" << x << "\n";
        for (const auto& [k, s] : f.kind_semantics) m << "kind_semantic=" << n << ":" << k << "=" << s << "\n";
        for (const auto& s : f.allowed_semantics) m << "allowed=" << n << ":" << s << "\n";
        for (const auto& r : f.requirements) m << "requires=" << n << ":" << r << "\n";
        if (f.default_semantic) m << "default_semantic=" << n << ":" << *f.default_semantic << "\n";
    }
    for (const auto& t : targets) {
        m << "target=" << t.arch_name << "\n";
        m << "isel=" << t.isel_file << "\n";
        for (const auto& a : t.aliases) m << "target_alias=" << t.arch_name << ":" << a << "\n";
        for (const auto& c : t.capabilities) m << "cap=" << t.arch_name << ":" << c << "\n";
        for (const auto& a : t.arch_enums) m << "arch_enum=" << t.arch_name << ":" << a << "\n";
        for (const auto& o : t.object_formats) m << "objfmt=" << t.arch_name << ":" << o << "\n";
    }
    for (const auto& [path, hash] : source_hashes) m << "source=" << path << ":" << hash << "\n";
    (void)root;
    return fnv1a64(m.str());
}

[[nodiscard]] std::optional<std::string> resolve_semantic(
    const FormMapping& mapping,
    const MachineFormContract& forms,
    const SemanticContractFile& semantics) {
    (void)semantics;
    const auto form_it = forms.forms.find(mapping.form);
    if (form_it == forms.forms.end()) return std::nullopt;
    const auto& spec = form_it->second;

    if (!mapping.semantic.empty()) return mapping.semantic;
    if (!mapping.kind.empty()) {
        auto it = spec.kind_semantics.find(mapping.kind);
        if (it == spec.kind_semantics.end()) return std::nullopt;
        return it->second;
    }
    if (spec.default_semantic) return *spec.default_semantic;
    if (spec.allowed_semantics.size() == 1) return *spec.allowed_semantics.begin();
    if (mapping.form == "Unhandled") return std::string("None");
    return std::nullopt;
}

[[nodiscard]] bool validate_form(
    const FormMapping& mapping,
    const TargetSpec& target,
    const MachineFormContract& forms,
    const SemanticContractFile& semantics,
    DiagnosticEngine& diag) {
    const auto it = forms.forms.find(mapping.form);
    if (it == forms.forms.end()) {
        diag.error(mapping.loc, "unknown machine form '" + mapping.form + "'");
        return false;
    }
    const auto& spec = it->second;

    if (!mapping.kind.empty() && !spec.kinds.contains(mapping.kind)) {
        diag.error(mapping.loc, "kind '" + mapping.kind + "' is not declared by machine form '" + mapping.form + "'");
        return false;
    }

    for (const auto& req : spec.requirements) {
        if (!target.capabilities.contains(req)) {
            diag.error(mapping.loc,
                "machine form '" + mapping.form + "' requires target capability '" + req +
                "', absent from target '" + target.arch_name + "'");
            return false;
        }
    }

    const auto semantic = resolve_semantic(mapping, forms, semantics);
    if (!semantic || !semantics.semantics.contains(*semantic)) {
        diag.error(mapping.loc, "cannot resolve semantic kind for opcode '" + mapping.opcode + "'");
        return false;
    }

    if (!spec.allowed_semantics.empty() && !spec.allowed_semantics.contains(*semantic)) {
        diag.error(mapping.loc,
            "semantic '" + *semantic + "' is not allowed for machine form '" + mapping.form + "'");
        return false;
    }
    return true;
}

struct BaseMapping {
    std::string opcode;
    std::string form;
    std::string semantic;
    bool runtime_backed = false;
};

struct ParsedTarget {
    TargetSpec target;
    std::vector<std::string> rules;
    std::vector<FormMapping> mappings;
    std::vector<std::string> forms_source;
    std::vector<FormMapping> delta_mappings;
};

[[nodiscard]] std::optional<ParsedTarget> parse_target(
    const TargetSpec& target,
    const fs::path& root,
    const CanonicalIRSchema& schema,
    const MachineFormContract& forms,
    const SemanticContractFile& semantics,
    DiagnosticEngine& diag) {
    const fs::path input = root / "defs/isel" / target.isel_file;
    if (!fs::exists(input)) {
        diag.error({input.generic_string(), 0}, "target DSL file does not exist");
        return std::nullopt;
    }

    ParsedTarget out{target, {}, {}, {}};
    std::vector<DslNode> nodes;
    std::set<std::string> visited;
    std::vector<std::string> stack;
    if (!resolve_dsl_source(input, root / "defs/isel", nodes, visited, stack, diag))
        return std::nullopt;

    std::set<std::string> rule_names;
    std::map<std::string, FormMapping> canonical_forms;

    for (const auto& node : nodes) {
        if (starts_with(node.raw_line, "rule ")) {
            RuleMetadata meta;
            meta.loc = node.loc;
            if (!parse_rule_metadata(node.raw_line, meta)) {
                diag.error(node.loc, "malformed rule; expected [targets=..., benefit=N, require_feature=true]");
                continue;
            }
            if (!rule_names.insert(meta.name).second) {
                diag.error(node.loc, "duplicate rule name '" + meta.name + "'");
                continue;
            }
            bool target_ok = false;
            for (const auto& alias : meta.targets) {
                if (target.aliases.contains(alias) || alias == target.arch_name) {
                    target_ok = true;
                } else {
                    diag.error(node.loc, "rule '" + meta.name + "' references unknown target selector '" +
                        alias + "' for target contract '" + target.arch_name + "'");
                }
            }
            if (!target_ok) {
                diag.error(node.loc, "rule '" + meta.name + "' is not applicable to target '" +
                    target.arch_name + "'");
                continue;
            }
            for (const auto& req : meta.requirements) {
                if (!target.capabilities.contains(req)) {
                    diag.error(node.loc, "rule '" + meta.name + "' requires capability '" + req +
                        "' not present on target '" + target.arch_name + "'");
                }
            }
            if (node.raw_line.find("->") == std::string::npos) {
                diag.error(node.loc, "rule must contain '->'");
                continue;
            }
            out.rules.push_back(node.raw_line);
            continue;
        }

        if (starts_with(node.raw_line, "form ")) {
            FormMapping mapping;
            mapping.loc = node.loc;
            if (!parse_form_line(node.raw_line, mapping)) {
                diag.error(node.loc, "malformed form mapping");
                continue;
            }

            mapping.runtime_backed = is_common_runtime_source(node.loc);
            const std::string canonical = canonical_opcode(mapping.opcode, schema);
            if (!schema.spellings.contains(mapping.opcode)) {
                diag.error(node.loc, "opcode '" + mapping.opcode + "' is not in the canonical IR schema");
                continue;
            }
            if (!schema.canonical_opcodes.contains(canonical)) {
                diag.error(node.loc, "opcode '" + mapping.opcode + "' resolves to unknown canonical opcode '" + canonical + "'");
                continue;
            }

            const auto semantic = resolve_semantic(mapping, forms, semantics);
            if (semantic) mapping.semantic = *semantic;
            if (!validate_form(mapping, target, forms, semantics, diag)) continue;

            auto existing = canonical_forms.find(canonical);
            if (existing != canonical_forms.end()) {
                if (existing->second.form != mapping.form ||
                    existing->second.kind != mapping.kind ||
                    existing->second.semantic != mapping.semantic) {
                    diag.error(node.loc, "conflicting mappings for canonical opcode '" + canonical + "'");
                    continue;
                }
                continue;
            }

            FormMapping canonical_mapping = mapping;
            canonical_mapping.opcode = canonical;
            canonical_forms.emplace(canonical, canonical_mapping);
            out.mappings.push_back(std::move(mapping));
            out.forms_source.push_back(node.raw_line);
            continue;
        }

        diag.error(node.loc, "unknown DSL directive '" + node.raw_line + "'");
    }

    if (canonical_forms.size() != schema.canonical_opcodes.size()) {
        std::set<std::string> missing;
        for (const auto& opcode : schema.canonical_opcodes)
            if (!canonical_forms.contains(opcode)) missing.insert(opcode);
        for (const auto& opcode : missing)
            diag.error({input.generic_string(), 0},
                       "missing explicit ISel coverage for canonical opcode '" + opcode + "'");
    }

    std::sort(out.rules.begin(), out.rules.end());
    std::sort(out.forms_source.begin(), out.forms_source.end());
    std::sort(out.mappings.begin(), out.mappings.end(),
        [](const FormMapping& a, const FormMapping& b) {
            return a.opcode < b.opcode;
        });

    return out;
}

[[nodiscard]] std::string emit_target_module(
    const ParsedTarget& parsed,
    const std::string& global_fingerprint,
    const std::string& target_fingerprint) {
    const auto& target = parsed.target;
    const auto& deltas = parsed.delta_mappings;
    std::ostringstream out;

    out << "/* Auto-generated by dgtl-isel-gen " << kGeneratorVersion << ". Do not edit. */\n"
        << "/* Target: " << target.arch_name << " — "
        << deltas.size() << " delta(s) from base */\n"
        << "module;\n\n"
        << "#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n\n"
        << "export module " << target.generated_module << ";\n\n"
        << "import dgtl.ir_enums;\n"
        << "import dgtl.mir.isel_gen;\n\n"
        << "export namespace dgtl::isa::" << target.arch_name << " {\n"
        << "using MachineForm = dgtl::mir::MachineForm;\n"
        << "using MachineSemanticKind = dgtl::mir::MachineSemanticKind;\n"
        << "namespace generated {\n\n"
        << "inline constexpr std::string_view kGeneratedTarget = \"" << target.arch_name << "\";\n"
        << "inline constexpr std::string_view kGeneratorVersion = \"" << kGeneratorVersion << "\";\n"
        << "inline constexpr std::string_view kContractFingerprint = \"" << global_fingerprint << "\";\n"
        << "inline constexpr std::string_view kTargetContractFingerprint = \"" << target_fingerprint << "\";\n\n";

    out << "// Target capabilities declared in targets.dgtl_contract\n"
        << "inline constexpr std::size_t kCapabilityCount = " << target.capabilities.size() << ";\n"
        << "inline constexpr std::array<std::string_view, " << target.capabilities.size() << "> kCapabilities = {{\n";
    for (const auto& cap : target.capabilities)
        out << "    \"" << cpp_escape(cap) << "\",\n";
    out << "}};\n\n";

    out << "inline constexpr std::size_t kArchEnumCount = " << target.arch_enums.size() << ";\n"
        << "inline constexpr std::array<std::string_view, " << target.arch_enums.size() << "> kArchEnums = {{\n";
    for (const auto& ae : target.arch_enums)
        out << "    \"" << cpp_escape(ae) << "\",\n";
    out << "}};\n\n";

    out << "inline constexpr std::size_t kObjectFormatCount = " << target.object_formats.size() << ";\n"
        << "inline constexpr std::array<std::string_view, " << target.object_formats.size() << "> kObjectFormats = {{\n";
    for (const auto& fmt : target.object_formats)
        out << "    \"" << cpp_escape(fmt) << "\",\n";
    out << "}};\n\n";

    out << "// Form deltas from base (base is in dgtl.mir.isel_gen)\n"
        << "inline constexpr std::size_t kDeltaCount = " << deltas.size() << ";\n";

    if (!deltas.empty()) {
        out << "struct DeltaEntry { ir::OpCode opcode; MachineForm form; MachineSemanticKind semantic; bool runtime_backed; };\n"
            << "inline constexpr std::array<DeltaEntry, " << deltas.size() << "> kDeltaTable = {{\n";
        for (const auto& m : deltas)
            out << "    { ir::OpCode::" << m.opcode
                << ", MachineForm::" << m.form
                << ", MachineSemanticKind::" << (m.semantic.empty() ? "None" : m.semantic)
                << ", " << (m.runtime_backed ? "true" : "false") << " },\n";
        out << "}};\n\n";
    }

    out << "[[nodiscard]] constexpr MachineForm classify_machine_form(ir::OpCode opcode) noexcept {\n";
    if (!deltas.empty()) {
        out << "    for (const auto& d : kDeltaTable)\n"
            << "        if (d.opcode == opcode) return d.form;\n";
    }
    out << "    return dgtl::mir::generated::base_classify_machine_form(opcode);\n"
        << "}\n\n";

    out << "[[nodiscard]] constexpr MachineSemanticKind semantic_kind(ir::OpCode opcode) noexcept {\n";
    if (!deltas.empty()) {
        out << "    for (const auto& d : kDeltaTable)\n"
            << "        if (d.opcode == opcode) return d.semantic;\n";
    }
    out << "    return dgtl::mir::generated::base_semantic_kind(opcode);\n"
        << "}\n\n";

    out << "[[nodiscard]] constexpr bool is_runtime_backed(ir::OpCode opcode) noexcept {\n";
    if (!deltas.empty()) {
        out << "    for (const auto& d : kDeltaTable)\n"
            << "        if (d.opcode == opcode) return d.runtime_backed;\n";
    }
    out << "    return dgtl::mir::generated::base_is_runtime_backed(opcode);\n"
        << "}\n\n";

    out << "} // namespace generated\n"
        << "} // namespace dgtl::isa::" << target.arch_name << "\n\n";

    out << "export namespace dgtl::isa::lowering::" << target.arch_name << " {\n"
        << "using namespace dgtl::isa::" << target.arch_name << ";\n"
        << "namespace generated = dgtl::isa::" << target.arch_name << "::generated;\n"
        << "}\n";
    return out.str();
}

[[nodiscard]] std::string normalize_rule_for_target(
    std::string rule, const TargetSpec& target) {
    std::string normalized_targets;
    if (!target.arch_enums.empty()) {
        for (std::size_t i = 0; i < target.arch_enums.size(); ++i) {
            if (i) normalized_targets += "|";
            normalized_targets += target.arch_enums[i];
        }
    } else {
        normalized_targets = target.arch_name;
    }

    const auto lb = rule.find("[targets=");
    if (lb == std::string::npos) return rule;
    const auto start = lb + 9;
    const auto comma = rule.find(',', start);
    const auto rb = rule.find(']', start);
    const auto end = (comma != std::string::npos && comma < rb) ? comma : rb;
    if (end == std::string::npos) return rule;
    rule.replace(start, end - start, normalized_targets);
    return rule;
}

[[nodiscard]] std::string emit_rules_module(
    const ParsedTarget& parsed,
    const std::string& global_fingerprint,
    const std::string& target_fingerprint,
    const fs::path& root) {
    const auto& t = parsed.target;
    std::ostringstream out;
    out << "/* Auto-generated by dgtl-isel-gen " << kGeneratorVersion << ". Do not edit. */\n"
        << "module;\n\n#include <array>\n#include <cstddef>\n#include <string_view>\n\n"
        << "export module " << t.rules_module << ";\n\n"
        << "export namespace dgtl::isa::" << t.arch_name << "::generated {\n"
        << "inline constexpr std::string_view kGeneratedTarget = \"" << t.arch_name << "\";\n"
        << "inline constexpr std::string_view kGeneratorVersion = \"" << kGeneratorVersion << "\";\n"
        << "inline constexpr std::string_view kContractFingerprint = \"" << global_fingerprint << "\";\n"
        << "inline constexpr std::string_view kTargetContractFingerprint = \"" << target_fingerprint << "\";\n"
        << "inline constexpr std::string_view kRuleDslSource = \""
        << cpp_escape((root / "defs/isel" / t.isel_file).lexically_relative(root).generic_string()) << "\";\n"
        << "inline constexpr std::size_t kRuleDslCount = " << parsed.rules.size() << ";\n"
        << "inline constexpr std::array<std::string_view, " << parsed.rules.size() << "> kRuleDslLines = {\n";
    for (std::size_t i = 0; i < parsed.rules.size(); ++i)
        out << "    \"" << cpp_escape(normalize_rule_for_target(parsed.rules[i], t)) << "\"" << (i + 1 < parsed.rules.size() ? "," : "") << "\n";
    out << "};\n\n"
        << "inline constexpr std::size_t kFormDslCount = " << parsed.forms_source.size() << ";\n"
        << "inline constexpr std::array<std::string_view, " << parsed.forms_source.size() << "> kFormDslLines = {\n";
    for (std::size_t i = 0; i < parsed.forms_source.size(); ++i)
        out << "    \"" << cpp_escape(parsed.forms_source[i]) << "\"" << (i + 1 < parsed.forms_source.size() ? "," : "") << "\n";
    out << "};\n"
        << "} // namespace dgtl::isa::" << t.arch_name << "::generated\n";
    return out.str();
}

[[nodiscard]] std::string emit_dispatch(
    const std::vector<ParsedTarget>& parsed_targets) {
    std::ostringstream out;
    out << "/* Auto-generated by dgtl-isel-gen " << kGeneratorVersion << ". Do not edit. */\n"
        << "module;\n\n#include <span>\n#include <string_view>\n\n"
        << "export module dgtl.isa.isel_dispatch;\n\n";

    for (const auto& p : parsed_targets) {
        out << "export import " << p.target.generated_module << ";\n";
        out << "import " << p.target.rules_module << ";\n";
    }
    out << "\nimport dgtl.mir.isel_gen;\n"
        << "import dgtl.target;\n"
        << "import dgtl.ir_enums;\n\n"
        << "export namespace dgtl::isa {\n"
        << "using MachineForm = dgtl::mir::MachineForm;\n"
        << "using MachineSemanticKind = dgtl::mir::MachineSemanticKind;\n\n";

    out << "[[nodiscard]] constexpr MachineForm classify_machine_form(\n"
        << "    const core::TargetMachine& target, ir::OpCode opcode) noexcept {\n"
        << "    switch (target.get_arch()) {\n";
    for (const auto& p : parsed_targets) {
        if (p.target.arch_enums.empty()) continue;
        for (const auto& arch : p.target.arch_enums)
            out << "        case core::ArchType::" << arch << ":\n";
        out << "            return dgtl::mir::generated::canonicalize_machine_form(\n"
            << "                dgtl::isa::" << p.target.arch_name
            << "::generated::classify_machine_form(opcode));\n";
    }
    out << "        default: return MachineForm::Unhandled;\n"
        << "    }\n}\n\n";

    out << "[[nodiscard]] constexpr MachineSemanticKind semantic_kind(\n"
        << "    const core::TargetMachine& target, ir::OpCode opcode) noexcept {\n"
        << "    switch (target.get_arch()) {\n";
    for (const auto& p : parsed_targets) {
        if (p.target.arch_enums.empty()) continue;
        for (const auto& arch : p.target.arch_enums)
            out << "        case core::ArchType::" << arch << ":\n";
        out << "            return dgtl::isa::" << p.target.arch_name
            << "::generated::semantic_kind(opcode);\n";
    }
    out << "        default: return MachineSemanticKind::None;\n"
        << "    }\n}\n\n";

    out << "[[nodiscard]] constexpr bool is_runtime_backed(\n"
        << "    const core::TargetMachine& target, ir::OpCode opcode) noexcept {\n"
        << "    switch (target.get_arch()) {\n";
    for (const auto& p : parsed_targets) {
        if (p.target.arch_enums.empty()) continue;
        for (const auto& arch : p.target.arch_enums)
            out << "        case core::ArchType::" << arch << ":\n";
        out << "            return dgtl::isa::" << p.target.arch_name
            << "::generated::is_runtime_backed(opcode);\n";
    }
    out << "        default: return false;\n"
        << "    }\n}\n\n";

    out << "[[nodiscard]] constexpr std::span<const std::string_view> rule_lines(\n"
        << "    const core::TargetMachine& target) noexcept {\n"
        << "    switch (target.get_arch()) {\n";
    for (const auto& p : parsed_targets) {
        if (p.target.arch_enums.empty()) continue;
        for (const auto& arch : p.target.arch_enums)
            out << "        case core::ArchType::" << arch << ":\n";
        out << "            return {dgtl::isa::" << p.target.arch_name
            << "::generated::kRuleDslLines};\n";
    }
    out << "        default: return {};\n"
        << "    }\n}\n\n";

    out << "} // namespace dgtl::isa\n";
    return out.str();
}

[[nodiscard]] bool write_file(const fs::path& path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const fs::path tmp = path.string() + ".dgtl_tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) return false;
    }
#ifdef _WIN32
    if (std::filesystem::exists(path, ec)) std::filesystem::remove(path, ec);
#endif
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<BaseMapping> compute_base_mappings(
    std::vector<ParsedTarget>& parsed_targets) {

    if (parsed_targets.empty()) return {};

    const auto& ref = parsed_targets.front().mappings;
    std::vector<BaseMapping> base;
    base.reserve(ref.size());

    for (const auto& ref_m : ref) {
        std::map<std::string, std::size_t> form_votes;
        std::map<std::string, std::size_t> sem_votes;
        std::size_t rt_true = 0;

        for (const auto& pt : parsed_targets) {
            for (const auto& m : pt.mappings) {
                if (m.opcode == ref_m.opcode) {
                    form_votes[m.form]++;
                    sem_votes[m.semantic]++;
                    if (m.runtime_backed) rt_true++;
                    break;
                }
            }
        }

        std::string best_form;
        std::size_t best_form_count = 0;
        for (const auto& [f, c] : form_votes) {
            if (c > best_form_count) { best_form = f; best_form_count = c; }
        }

        std::string best_sem;
        std::size_t best_sem_count = 0;
        for (const auto& [s, c] : sem_votes) {
            if (c > best_sem_count) { best_sem = s; best_sem_count = c; }
        }

        const bool base_rt = (rt_true * 2 > parsed_targets.size());
        base.push_back({ref_m.opcode, best_form, best_sem, base_rt});
    }

    for (auto& pt : parsed_targets) {
        pt.delta_mappings.clear();
        for (const auto& m : pt.mappings) {
            for (const auto& bm : base) {
                if (bm.opcode == m.opcode) {
                    if (m.form != bm.form ||
                        m.semantic != bm.semantic ||
                        m.runtime_backed != bm.runtime_backed) {
                        pt.delta_mappings.push_back(m);
                    }
                    break;
                }
            }
        }
    }

    return base;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) return 2;
    if (argc > 1 && std::string_view(argv[1]) == "--help") return 0;

    try {
        const fs::path root = fs::weakly_canonical(opts.root);

        DiagnosticEngine diag;
        const fs::path ir_path = root / "defs/ir/phase1_core_ops.dgtl_def";
        const fs::path sem_path = root / "defs/isel/machine_semantics.dgtl_contract";
        const fs::path form_path = root / "defs/isel/machine_forms.dgtl_contract";
        const fs::path target_path = root / "defs/isel/targets.dgtl_contract";

        const auto schema = load_ir_schema(ir_path, diag);
        const auto semantics = load_semantic_contract(sem_path, diag);
        const auto forms = load_machine_form_contract(form_path, semantics, diag);
        const auto targets = load_target_contract(target_path, diag);

        if (schema.canonical_opcodes.empty())
            diag.error({ir_path.generic_string(), 0}, "canonical IR schema contains no opcodes");
        if (semantics.semantics.empty())
            diag.error({sem_path.generic_string(), 0}, "semantic contract contains no semantics");
        if (forms.forms.empty())
            diag.error({form_path.generic_string(), 0}, "machine-form contract contains no forms");
        if (targets.empty())
            diag.error({target_path.generic_string(), 0}, "target contract contains no targets");

        if (diag.has_errors()) {
            diag.print();
            return 1;
        }

        std::vector<TargetSpec> selected;
        bool found = opts.target_filter.empty();
        for (const auto& t : targets) {
            if (opts.target_filter.empty()) {
                selected.push_back(t);
            } else if (t.arch_name == opts.target_filter || t.aliases.contains(opts.target_filter)) {
                selected.push_back(t);
                found = true;
            }
        }
        if (!found) {
            std::cerr << "[dgtl-isel-gen] unknown target '" << opts.target_filter << "'\n";
            return 2;
        }

        std::vector<ParsedTarget> parsed;
        std::map<std::string, std::string> source_hashes;

        const std::vector<fs::path> contract_files = {
            ir_path, sem_path, form_path, target_path
        };
        for (const auto& p : contract_files)
            source_hashes[p.lexically_relative(root).generic_string()] = fnv1a64(read_file_exact(p));

        for (const auto& t : targets) {
            const fs::path p = root / "defs/isel" / t.isel_file;
            source_hashes[p.lexically_relative(root).generic_string()] = fnv1a64(read_file_exact(p));
            std::vector<DslNode> nodes;
            std::set<std::string> visited;
            std::vector<std::string> stack;
            if (!resolve_dsl_source(p, root / "defs/isel", nodes, visited, stack, diag)) continue;
            for (const auto& v : visited) {
                const fs::path vp(v);
                source_hashes[vp.lexically_relative(root).generic_string()] = fnv1a64(read_file_exact(vp));
            }
        }

        const std::string global_fingerprint =
            contract_material(root, schema, semantics, forms, targets, source_hashes);

        for (const auto& t : targets) {
            if (auto parsed_target = parse_target(t, root, schema, forms, semantics, diag))
                parsed.push_back(std::move(*parsed_target));
        }

        if (diag.has_errors()) {
            diag.print();
            return 1;
        }

        const std::vector<BaseMapping> base_mappings = compute_base_mappings(parsed);

        if (opts.verbose) {
            std::cout << "[dgtl-isel-gen] generator=" << kGeneratorVersion
                      << " fingerprint=" << global_fingerprint
                      << " canonical_opcodes=" << schema.canonical_opcodes.size()
                      << " targets=" << parsed.size()
                      << " base_opcodes=" << base_mappings.size() << "\n";
            for (const auto& p : parsed)
                std::cout << "[dgtl-isel-gen]   " << p.target.arch_name
                          << ": " << p.mappings.size() << " mappings, "
                          << p.rules.size() << " rules, "
                          << p.delta_mappings.size() << " delta(s)\n";
        }

        std::vector<std::pair<fs::path, std::string>> outputs;

        std::ostringstream common;
        common << "/* Auto-generated by dgtl-isel-gen " << kGeneratorVersion << ". Do not edit. */\n"
               << "/* Base tables cover all canonical opcodes with majority-vote mappings. */\n"
               << "module;\n\n"
               << "#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n#include <type_traits>\n\n"
               << "export module dgtl.mir.isel_gen;\n\n"
               << "import dgtl.ir_enums;\n\n"
               << "export namespace dgtl::mir {\n\n"
               << "enum class MachineForm : std::uint8_t {\n";
        for (const auto& [name, spec] : forms.forms)
            common << "    " << name << " = " << spec.value << ",\n";
        common << "};\n\n"
               << "enum class MachineSemanticKind : std::uint16_t {\n";
        for (const auto& [name, spec] : semantics.semantics)
            common << "    " << name << " = " << spec.value << ",\n";
        common << "};\n\n"
               << "namespace generated {\n"
               << "inline constexpr std::string_view kGeneratorVersion = \"" << kGeneratorVersion << "\";\n"
               << "inline constexpr std::string_view kContractFingerprint = \"" << global_fingerprint << "\";\n"
               << "template <typename TargetForm>\n"
               << "[[nodiscard]] constexpr MachineForm canonicalize_machine_form(TargetForm form) noexcept {\n"
               << "    static_assert(std::is_enum_v<TargetForm>);\n"
               << "    return static_cast<MachineForm>(static_cast<std::uint8_t>(form));\n"
               << "}\n"
               << "template <typename TargetKind>\n"
               << "[[nodiscard]] constexpr std::uint8_t canonicalize_kind(TargetKind kind) noexcept {\n"
               << "    static_assert(std::is_enum_v<TargetKind>);\n"
               << "    return static_cast<std::uint8_t>(kind);\n"
               << "}\n"
               << "[[nodiscard]] constexpr bool semantic_kind_matches_form(\n"
               << "    MachineForm form, MachineSemanticKind semantic) noexcept {\n"
               << "    switch (form) {\n";
        for (const auto& [name, spec] : forms.forms) {
            common << "        case MachineForm::" << name << ":\n";
            if (spec.allowed_semantics.empty()) {
                common << "            return false;\n";
            } else {
                common << "            switch (semantic) {\n";
                for (const auto& sem : spec.allowed_semantics)
                    common << "                case MachineSemanticKind::" << sem << ":\n";
                common << "                    return true;\n"
                       << "                default: return false;\n"
                       << "            }\n";
            }
        }
        common << "        default: return false;\n"
               << "    }\n"
               << "}\n\n";

        common << "// Base ISel tables\n";
        common << "inline constexpr std::size_t kBaseMaxOpCodeIndex = []() constexpr {\n"
               << "    std::size_t highest = 0;\n";
        for (const auto& bm : base_mappings)
            common << "    highest = highest < static_cast<std::size_t>(ir::OpCode::" << bm.opcode
                   << ") ? static_cast<std::size_t>(ir::OpCode::" << bm.opcode << ") : highest;\n";
        common << "    return highest;\n}();\n\n";

        common << "inline constexpr auto kBaseFormTable = []() constexpr {\n"
               << "    std::array<MachineForm, kBaseMaxOpCodeIndex + 1> table{};\n"
               << "    table.fill(MachineForm::Unhandled);\n";
        for (const auto& bm : base_mappings)
            common << "    table[static_cast<std::size_t>(ir::OpCode::" << bm.opcode
                   << ")] = MachineForm::" << bm.form << ";\n";
        common << "    return table;\n}();\n\n";

        common << "inline constexpr auto kBaseSemanticTable = []() constexpr {\n"
               << "    std::array<MachineSemanticKind, kBaseMaxOpCodeIndex + 1> table{};\n"
               << "    table.fill(MachineSemanticKind::None);\n";
        for (const auto& bm : base_mappings)
            common << "    table[static_cast<std::size_t>(ir::OpCode::" << bm.opcode
                   << ")] = MachineSemanticKind::" << (bm.semantic.empty() ? "None" : bm.semantic) << ";\n";
        common << "    return table;\n}();\n\n";

        common << "inline constexpr auto kBaseRuntimeTable = []() constexpr {\n"
               << "    std::array<bool, kBaseMaxOpCodeIndex + 1> table{};\n"
               << "    table.fill(false);\n";
        for (const auto& bm : base_mappings)
            if (bm.runtime_backed)
                common << "    table[static_cast<std::size_t>(ir::OpCode::" << bm.opcode << ")] = true;\n";
        common << "    return table;\n}();\n\n";

        common << "[[nodiscard]] constexpr MachineForm base_classify_machine_form(ir::OpCode opcode) noexcept {\n"
               << "    const auto i = static_cast<std::size_t>(opcode);\n"
               << "    return i < kBaseFormTable.size() ? kBaseFormTable[i] : MachineForm::Unhandled;\n"
               << "}\n"
               << "[[nodiscard]] constexpr MachineSemanticKind base_semantic_kind(ir::OpCode opcode) noexcept {\n"
               << "    const auto i = static_cast<std::size_t>(opcode);\n"
               << "    return i < kBaseSemanticTable.size() ? kBaseSemanticTable[i] : MachineSemanticKind::None;\n"
               << "}\n"
               << "[[nodiscard]] constexpr bool base_is_runtime_backed(ir::OpCode opcode) noexcept {\n"
               << "    const auto i = static_cast<std::size_t>(opcode);\n"
               << "    return i < kBaseRuntimeTable.size() && kBaseRuntimeTable[i];\n"
               << "}\n"
               << "} // namespace generated\n"
               << "} // namespace dgtl::mir\n";

        // Canonically emitted into selection/
        outputs.push_back({root / "include/dgtl/mir/selection/ISel_Generated.ixx", common.str()});

        std::ostringstream rules_bridge;
        rules_bridge << "/* Auto-generated by dgtl-isel-gen " << kGeneratorVersion << ". Do not edit. */\n"
                     << "module;\n#include <string_view>\n\n"
                     << "export module dgtl.mir.isel_rules;\n\n"
                     << "export namespace dgtl::mir::generated {\n"
                     << "inline constexpr std::string_view kGeneratorVersion = \"" << kGeneratorVersion << "\";\n"
                     << "inline constexpr std::string_view kContractFingerprint = \"" << global_fingerprint << "\";\n"
                     << "}\n";

        // Canonically emitted into selection/
        outputs.push_back({root / "include/dgtl/mir/selection/ISel_Rules_Generated.ixx", rules_bridge.str()});

        for (const auto& p : parsed) {
            const std::string target_material =
                p.target.arch_name + "\n" + global_fingerprint + "\n" +
                p.target.isel_file + "\n" +
                fnv1a64([&] {
                    std::ostringstream s;
                    for (const auto& m : p.mappings)
                        s << m.opcode << "|" << m.form << "|" << m.kind << "|" << m.semantic << "\n";
                    for (const auto& r : p.rules) s << "rule|" << r << "\n";
                    return s.str();
                }());
            const std::string target_fp = fnv1a64(target_material);

            outputs.push_back({
                root / p.target.output_dir / (p.target.file_prefix + "ISel_Generated.ixx"),
                emit_target_module(p, global_fingerprint, target_fp)
            });
            outputs.push_back({
                root / p.target.output_dir / (p.target.file_prefix + "ISel_Rules_Generated.ixx"),
                emit_rules_module(p, global_fingerprint, target_fp, root)
            });
        }

        outputs.push_back({
            root / "include/dgtl/isa/ISel_Dispatch.ixx",
            emit_dispatch(parsed)
        });

        // Exact invariant validation for selection/ paths
        for (const auto& [path, content] : outputs) {
            const bool is_neutral_isel = (path.filename() == "ISel_Generated.ixx" &&
                                          path.parent_path().filename() == "selection");

            if (is_neutral_isel) {
                if (content.find("dgtl.isa.") != std::string::npos) {
                    diag.error({path.generic_string(), 0},
                        "neutral MIR bridge illegally imports target ISA");
                }
                if (content.find("enum class MachineForm") == std::string::npos) {
                    diag.error({path.generic_string(), 0},
                        "neutral MIR bridge must own canonical MachineForm");
                }
                if (content.find("base_classify_machine_form") == std::string::npos) {
                    diag.error({path.generic_string(), 0},
                        "neutral MIR bridge must export base_classify_machine_form");
                }
            } else if (path.filename().string().find("ISel_Generated.ixx") != std::string::npos) {
                if (content.find("enum class MachineForm") != std::string::npos) {
                    diag.error({path.generic_string(), 0},
                        "target generated module must not redefine MachineForm");
                }
                if (content.find("kDeltaCount") == std::string::npos) {
                    diag.error({path.generic_string(), 0},
                        "target generated module must use delta architecture (missing kDeltaCount)");
                }
            }
        }

        if (diag.has_errors()) {
            diag.print();
            return 1;
        }

        for (const auto& [path, content] : outputs) {
            if (!write_file(path, content)) {
                std::cerr << "[dgtl-isel-gen] failed to write " << path << "\n";
                return 1;
            }
        }

        if (opts.verbose) {
            for (const auto& p : parsed)
                std::cout << "[dgtl-isel-gen] " << p.target.arch_name
                          << ": " << p.mappings.size() << " mappings, "
                          << p.rules.size() << " rules, "
                          << p.delta_mappings.size() << " delta(s)\n";
        }
        std::cout << "[dgtl-isel-gen] generation complete: " << global_fingerprint << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[dgtl-isel-gen] fatal: " << ex.what() << "\n";
        return 1;
    }
}