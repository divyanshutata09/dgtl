/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path root{"."};
    std::vector<fs::path> log_paths;
    std::optional<fs::path> export_path;
    std::optional<fs::path> baseline_path;
    std::string focus;
    bool summary_only{false};
    bool relative_paths{false};
    bool json{false};
    bool deep_symbols{true};
    bool all_logs{false};
    bool write_baseline{false};
    std::size_t top{8};
};

struct ModuleUnit {
    fs::path path;
    std::string module_name;
    std::vector<std::string> imports;
};

struct ErrorRecord {
    fs::path file;
    int line{0};
    std::string kind;
    std::string message;
    std::string raw;
    std::string command_context;
};

struct ErrorCluster {
    std::string signature;
    std::size_t count{0};
    std::vector<ErrorRecord> samples;
    std::map<std::string, std::size_t> command_hits;
    std::string likely_command;
    std::size_t likely_command_hits{0};
};

struct SymbolSite {
    fs::path file;
    int line{0};
    std::string text;
};

struct SymbolCollision {
    std::string symbol;
    std::vector<SymbolSite> sites;
};

struct OwnershipCandidate {
    fs::path file;
    int line{0};
    std::string declaration;
    int score{0};
    std::vector<std::string> reasons;
    int snippet_start_line{0};
    int snippet_end_line{0};
    std::vector<std::string> snippet_lines;
};

struct OwnershipRecommendation {
    std::string symbol;
    fs::path error_file;
    int error_line{0};
    std::string trigger_message;
    std::vector<OwnershipCandidate> candidates;
};

struct AmbiguousDiagnostic {
    std::string symbol;
    fs::path error_file;
    int error_line{0};
    std::string trigger_message;
    std::vector<SymbolSite> candidates;
};

struct Analysis {
    std::vector<ModuleUnit> modules;
    std::vector<std::vector<const ModuleUnit*>> cycles;
    std::vector<ErrorCluster> clusters;
    std::vector<std::string> cache_hints;
    std::vector<SymbolCollision> collisions;
    std::vector<AmbiguousDiagnostic> ambiguous_diagnostics;
    std::vector<OwnershipRecommendation> ownership_recommendations;
    std::vector<fs::path> logs_scanned;
    std::vector<std::string> suspect_symbols;
    std::string last_command;
    std::string last_exit_code;
    std::optional<fs::path> last_session_log;
    std::vector<std::string> compiler_commands;
    std::string likely_failing_command;
    std::string compiler_fingerprint;
    std::string command_fingerprint;
    std::size_t declaration_cache_hits{0};
    std::size_t declaration_cache_misses{0};
    std::size_t declaration_files_scanned{0};
    std::vector<std::string> regression_notes;
};

struct DeclarationIndexCacheEntry {
    std::int64_t mtime_ticks{0};
    std::vector<SymbolSite> sites;
};

struct Snapshot {
    std::string fingerprint;
    std::size_t cluster_count{0};
    std::size_t collision_count{0};
    std::size_t recommendation_count{0};
    std::vector<std::string> top_signatures;
};

enum class ParseStatus {
    Ok,
    Help,
    Error
};

[[nodiscard]] std::string trim(std::string_view input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return std::string(input.substr(start, end - start));
}

[[nodiscard]] std::string to_lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

[[nodiscard]] bool has_source_extension(const fs::path& path) {
    static const std::unordered_set<std::string> allowed{
        ".ixx", ".cpp", ".cc", ".cxx", ".h", ".hh", ".hpp", ".hxx"};
    return allowed.contains(to_lower(path.extension().string()));
}

[[nodiscard]] bool should_skip_path(const fs::path& path) {
    for (const auto& part : path) {
        const std::string token = to_lower(part.string());
        if (token == ".git" || token == ".vs" || token == "node_modules" || token == "debug_logs" || token == ".dot_dgtl") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto get_file_mtime(const fs::path& p) -> fs::file_time_type {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : ftime;
}

[[nodiscard]] std::string display_path(const fs::path& root, const fs::path& path, bool relative_paths) {
    if (!relative_paths) {
        return path.string();
    }

    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    if (ec) {
        return path.string();
    }
    return rel.generic_string();
}

void emit_text(const Analysis& analysis, const fs::path& root, const Options& opts);

[[nodiscard]] bool is_identifier_like(std::string_view text) {
    if (text.empty() || text.size() > 128) {
        return false;
    }

    if (!(std::isalpha(static_cast<unsigned char>(text.front())) != 0 || text.front() == '_')) {
        return false;
    }

    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_' && ch != ':' && ch != '@') {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool looks_like_diagnostic_symbol(std::string_view text) {
    static const std::unordered_set<std::string> stop_words{
        "error", "warning", "note", "candidate", "candidates", "struct", "class", "const",
        "void", "bool", "int", "double", "float", "return", "module", "import", "token",
        "type", "value", "before", "after", "line", "expected", "declared", "include"};

    if (!is_identifier_like(text)) {
        return false;
    }

    const std::string lower = to_lower(text);
    if (stop_words.contains(lower)) {
        return false;
    }

    const bool has_separator = text.find('_') != std::string_view::npos || text.find(':') != std::string_view::npos;
    const bool starts_upper = std::isupper(static_cast<unsigned char>(text.front())) != 0;
    const bool has_internal_upper = std::any_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isupper(c) != 0;
    });

    return has_separator || starts_upper || has_internal_upper;
}

[[nodiscard]] bool looks_like_actionable_owner_symbol(std::string_view text) {
    static const std::unordered_set<std::string> blocked{
        "dgtl", "dgtl::ir", "std", "std::unordered_map", "size_t", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "int32_t", "int64_t", "bool", "int", "double", "float", "char",
        "unordered_map", "vector", "string", "map", "set", "optional", "variant"};

    if (!is_identifier_like(text)) {
        return false;
    }

    const std::string lower = to_lower(text);
    if (blocked.contains(lower)) {
        return false;
    }

    const bool has_upper = std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isupper(c) != 0; });
    const bool has_underscore = text.find('_') != std::string_view::npos;
    const bool has_namespace = text.find("::") != std::string_view::npos;
    return has_upper || has_underscore || (has_namespace && (has_upper || has_underscore));
}

[[nodiscard]] ParseStatus parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (arg == "--root") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --root\n";
                return ParseStatus::Error;
            }
            opts.root = argv[++i];
        } else if (arg == "--log") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --log\n";
                return ParseStatus::Error;
            }
            opts.log_paths.push_back(argv[++i]);
        } else if (arg == "--export") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --export\n";
                return ParseStatus::Error;
            }
            opts.export_path = fs::path(argv[++i]);
        } else if (arg == "--baseline") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --baseline\n";
                return ParseStatus::Error;
            }
            opts.baseline_path = fs::path(argv[++i]);
        } else if (arg == "--focus") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --focus\n";
                return ParseStatus::Error;
            }
            opts.focus = argv[++i];
        } else if (arg == "--top") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --top\n";
                return ParseStatus::Error;
            }
            opts.top = static_cast<std::size_t>(std::max(1, std::stoi(argv[++i])));
        } else if (arg == "--summary-only") {
            opts.summary_only = true;
        } else if (arg == "--relative-paths") {
            opts.relative_paths = true;
        } else if (arg == "--json") {
            opts.json = true;
        } else if (arg == "--no-deep-symbols") {
            opts.deep_symbols = false;
        } else if (arg == "--all-logs") {
            opts.all_logs = true;
        } else if (arg == "--write-baseline") {
            opts.write_baseline = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "================================================================================\n"
                << "                  DGTL ENTERPRISE NATIVE & FORENSIC DEBUGGER                    \n"
                << "================================================================================\n\n"
                << "NATIVE EXECUTION & CRASH INTERCEPTION:\n"
                << "  dgtl-debugger run <executable.exe> [args...]\n"
                << "      Launch target under Win32 native debug interceptor.\n"
                << "      Captures hardware exceptions (0xC0000005, 0xC000001D, 0xC0000094, etc.),\n"
                << "      performs unmapped memory page diagnosis (VirtualQueryEx), dumps 64-bit\n"
                << "      register context, disassembles faulting RIP, and unwinds stack backtrace.\n\n"
                << "PE STRUCTURAL AUDIT & SECTION FORENSICS:\n"
                << "  dgtl-debugger inspect-exe <executable.exe>\n"
                << "      Deep structural validation of PE32+ binaries: header checks, section\n"
                << "      alignments, zero-size BSS detection, out-of-bounds RVAs, section overlaps,\n"
                << "      Import Directory Table (IAT/ILT) integrity, and entry point disassembly.\n\n"
                << "STATIC LOG, CYCLE, & COLLISION ANALYSIS:\n"
                << "  dgtl-debugger [--root <path>] [--log <path>]... [--export <path>] [--focus <text>]\n"
                << "      Scans C++20 module graphs, detects circular dependencies, tracks symbol\n"
                << "      collisions, clusters build errors, and exports JSON / Markdown diagnostics.\n\n"
                << "DIRECT SHORTCUTS:\n"
                << "  dgtl-debugger <executable.exe>  ->  Shorthand for 'dgtl-debugger run <executable.exe>'\n"
                << "================================================================================\n";
            return ParseStatus::Help;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return ParseStatus::Error;
        }
    }

    return ParseStatus::Ok;
}

[[nodiscard]] bool is_log_candidate(const fs::path& path) {
    const std::string ext = to_lower(path.extension().string());
    if (ext != ".log" && ext != ".txt") {
        return false;
    }

    const std::string stem = to_lower(path.stem().string());
    if (stem.rfind("latest-error", 0) == 0 || stem.rfind("error-", 0) == 0) {
        return false;
    }

    return stem.find("log") != std::string::npos ||
           stem.find("debug") != std::string::npos ||
           stem.find("build") != std::string::npos ||
           stem.find("replay") != std::string::npos;
}

void append_utf8_codepoint(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

[[nodiscard]] std::vector<std::uint8_t> read_file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

[[nodiscard]] std::string decode_text_bytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return {};
    }

    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        return std::string(bytes.begin() + 3, bytes.end());
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        std::string out;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const std::uint16_t unit = static_cast<std::uint16_t>(bytes[i]) |
                                       (static_cast<std::uint16_t>(bytes[i + 1]) << 8);
            append_utf8_codepoint(out, unit);
        }
        return out;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        std::string out;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const std::uint16_t unit = (static_cast<std::uint16_t>(bytes[i]) << 8) |
                                       static_cast<std::uint16_t>(bytes[i + 1]);
            append_utf8_codepoint(out, unit);
        }
        return out;
    }

    return std::string(bytes.begin(), bytes.end());
}

[[nodiscard]] std::vector<std::string> read_lines(const fs::path& path) {
    std::vector<std::string> lines;
    const std::string text = decode_text_bytes(read_file_bytes(path));
    if (text.empty()) {
        return lines;
    }

    std::string current;
    for (char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty() || (!text.empty() && text.back() == '\n')) {
        lines.push_back(current);
    }
    return lines;
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
    return trim(decode_text_bytes(read_file_bytes(path)));
}

[[nodiscard]] std::string strip_compiler_record_prefix(std::string line) {
    const std::size_t pipe = line.find('|');
    if (pipe == std::string::npos) {
        return trim(line);
    }
    return trim(line.substr(pipe + 1));
}

[[nodiscard]] std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] Snapshot load_snapshot(const fs::path& path) {
    Snapshot snapshot;
    for (const auto& line : read_lines(path)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "fingerprint") snapshot.fingerprint = value;
        else if (key == "cluster_count") snapshot.cluster_count = static_cast<std::size_t>(std::stoull(value));
        else if (key == "collision_count") snapshot.collision_count = static_cast<std::size_t>(std::stoull(value));
        else if (key == "recommendation_count") snapshot.recommendation_count = static_cast<std::size_t>(std::stoull(value));
        else if (key.rfind("top_signature_", 0) == 0) snapshot.top_signatures.push_back(value);
    }
    return snapshot;
}

void write_snapshot(const Snapshot& snapshot, const fs::path& path) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "fingerprint=" << snapshot.fingerprint << "\n";
    out << "cluster_count=" << snapshot.cluster_count << "\n";
    out << "collision_count=" << snapshot.collision_count << "\n";
    out << "recommendation_count=" << snapshot.recommendation_count << "\n";
    for (std::size_t i = 0; i < snapshot.top_signatures.size(); ++i) {
        out << "top_signature_" << i << "=" << snapshot.top_signatures[i] << "\n";
    }
}

[[nodiscard]] Snapshot make_snapshot(const Analysis& analysis) {
    Snapshot snapshot;
    snapshot.fingerprint = analysis.command_fingerprint;
    snapshot.cluster_count = analysis.clusters.size();
    snapshot.collision_count = analysis.collisions.size();
    snapshot.recommendation_count = analysis.ownership_recommendations.size();
    for (std::size_t i = 0; i < std::min<std::size_t>(5, analysis.clusters.size()); ++i) {
        snapshot.top_signatures.push_back(analysis.clusters[i].signature);
    }
    return snapshot;
}

[[nodiscard]] std::string build_command_fingerprint(const Analysis& analysis) {
    std::ostringstream key;
    key << analysis.last_command << '\n' << analysis.last_exit_code << '\n';
    key << analysis.compiler_fingerprint << '\n';
    for (const auto& path : analysis.logs_scanned) {
        key << path.generic_string() << '\n';
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(8, analysis.clusters.size()); ++i) {
        key << analysis.clusters[i].signature << '#' << analysis.clusters[i].count << '\n';
    }
    return hex64(fnv1a64(key.str()));
}

void attach_candidate_source_slices(Analysis& analysis) {
    std::unordered_map<std::string, std::vector<std::string>> cache;

    auto get_lines = [&](const fs::path& path) -> const std::vector<std::string>& {
        const std::string key = path.generic_string();
        auto found = cache.find(key);
        if (found == cache.end()) {
            found = cache.insert({key, read_lines(path)}).first;
        }
        return found->second;
    };

    for (auto& rec : analysis.ownership_recommendations) {
        for (auto& candidate : rec.candidates) {
            if (candidate.line <= 0) {
                continue;
            }
            const auto& lines = get_lines(candidate.file);
            if (lines.empty()) {
                continue;
            }
            const int start = std::max(1, candidate.line - 2);
            const int end = std::min(static_cast<int>(lines.size()), candidate.line + 2);
            candidate.snippet_start_line = start;
            candidate.snippet_end_line = end;
            for (int line_no = start; line_no <= end; ++line_no) {
                candidate.snippet_lines.push_back(lines[static_cast<std::size_t>(line_no - 1)]);
            }
        }
    }
}

void compare_against_snapshot(const Snapshot& previous, const Snapshot& current, Analysis& analysis, std::string_view label) {
    if (previous.fingerprint.empty()) {
        return;
    }
    if (previous.fingerprint == current.fingerprint) {
        analysis.regression_notes.push_back(std::string(label) + ": identical fingerprint to previous snapshot.");
        return;
    }
    if (current.cluster_count > previous.cluster_count) {
        analysis.regression_notes.push_back(std::string(label) + ": error cluster count increased from " + std::to_string(previous.cluster_count) + " to " + std::to_string(current.cluster_count) + ".");
    } else if (current.cluster_count < previous.cluster_count) {
        analysis.regression_notes.push_back(std::string(label) + ": error cluster count decreased from " + std::to_string(previous.cluster_count) + " to " + std::to_string(current.cluster_count) + ".");
    }
    if (!current.top_signatures.empty() && !previous.top_signatures.empty() && current.top_signatures.front() != previous.top_signatures.front()) {
        analysis.regression_notes.push_back(std::string(label) + ": top failure signature changed from '" + previous.top_signatures.front() + "' to '" + current.top_signatures.front() + "'.");
    }
}

[[nodiscard]] std::vector<fs::path> discover_log_files(const fs::path& root) {
    std::vector<fs::path> logs;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const fs::path path = entry.path();

        if (entry.is_directory(ec) && should_skip_path(path)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(ec) || !is_log_candidate(path)) {
            continue;
        }

        logs.push_back(path);
    }

    std::sort(logs.begin(), logs.end());
    logs.erase(std::unique(logs.begin(), logs.end()), logs.end());
    return logs;
}

std::unordered_map<std::string, DeclarationIndexCacheEntry> load_declaration_index_cache(const fs::path& cache_path) {
    std::unordered_map<std::string, DeclarationIndexCacheEntry> cache;
    const auto lines = read_lines(cache_path);
    if (lines.empty() || trim(lines.front()) != "version=2") {
        return {};
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::size_t first_tab = line.find('\t');
        const std::size_t second_tab = first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
        const std::size_t third_tab = second_tab == std::string::npos ? std::string::npos : line.find('\t', second_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos || third_tab == std::string::npos) {
            continue;
        }

        const std::string kind = line.substr(0, first_tab);
        const std::string path_text = line.substr(first_tab + 1, second_tab - first_tab - 1);
        const std::string first_value = line.substr(second_tab + 1, third_tab - second_tab - 1);
        const std::string second_value = line.substr(third_tab + 1);
        if (path_text.empty() || first_value.empty()) {
            continue;
        }

        DeclarationIndexCacheEntry& entry = cache[path_text];
        if (kind == "meta") {
            entry.mtime_ticks = std::stoll(first_value);
        } else if (kind == "site") {
            entry.sites.push_back(SymbolSite{fs::path(path_text), std::stoi(first_value), second_value});
        }
    }

    return cache;
}

void write_declaration_index_cache(const fs::path& cache_path, const std::unordered_map<std::string, DeclarationIndexCacheEntry>& cache) {
    std::error_code ec;
    if (!cache_path.parent_path().empty()) {
        fs::create_directories(cache_path.parent_path(), ec);
    }
    std::ofstream out(cache_path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }

    out << "version=2\n";
    std::vector<std::string> paths;
    paths.reserve(cache.size());
    for (const auto& [path_text, _] : cache) {
        paths.push_back(path_text);
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& path_text : paths) {
        const auto found = cache.find(path_text);
        if (found == cache.end()) {
            continue;
        }

        out << "meta\t" << path_text << '\t' << found->second.mtime_ticks << "\t0\n";
        for (const auto& site : found->second.sites) {
            std::string text = site.text;
            std::replace(text.begin(), text.end(), '\t', ' ');
            out << "site\t" << path_text << '\t' << site.line << '\t' << text << "\n";
        }
    }
}

[[nodiscard]] std::optional<ModuleUnit> parse_module_unit(const fs::path& path) {
    const auto lines = read_lines(path);
    ModuleUnit unit;
    unit.path = path;

    for (const std::string& raw : lines) {
        const std::string line = trim(raw);
        if (line.empty()) {
            continue;
        }

        if (line.rfind("export module ", 0) == 0) {
            std::string name = trim(line.substr(std::string("export module ").size()));
            if (!name.empty() && name.back() == ';') {
                name.pop_back();
            }
            unit.module_name = trim(name);
            continue;
        }

        if (line.rfind("module ", 0) == 0 && unit.module_name.empty()) {
            std::string name = trim(line.substr(std::string("module ").size()));
            if (!name.empty() && name.back() == ';') {
                name.pop_back();
            }
            if (name != ";") {
                unit.module_name = trim(name);
            }
            continue;
        }

        if (line.rfind("import ", 0) == 0 || line.rfind("export import ", 0) == 0) {
            const bool exported = line.rfind("export import ", 0) == 0;
            std::string name = exported
                ? trim(line.substr(std::string("export import ").size()))
                : trim(line.substr(std::string("import ").size()));
            if (!name.empty() && name.back() == ';') {
                name.pop_back();
            }
            if (!name.empty() && name.front() != '<' && name.find(' ') == std::string::npos) {
                unit.imports.push_back(name);
            }
        }
    }

    if (unit.module_name.empty()) {
        return std::nullopt;
    }
    return unit;
}

[[nodiscard]] std::vector<ModuleUnit> discover_modules(const fs::path& root) {
    std::vector<ModuleUnit> modules;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const fs::path path = entry.path();

        if (entry.is_directory(ec) && should_skip_path(path)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(ec) || !has_source_extension(path) || should_skip_path(path)) {
            continue;
        }

        if (auto unit = parse_module_unit(path)) {
            modules.push_back(std::move(*unit));
        }
    }

    std::sort(modules.begin(), modules.end(), [](const ModuleUnit& a, const ModuleUnit& b) {
        return a.module_name < b.module_name;
    });
    return modules;
}

void find_module_cycles(Analysis& analysis) {
    std::unordered_map<std::string, const ModuleUnit*> by_name;
    for (const auto& unit : analysis.modules) {
        by_name[unit.module_name] = &unit;
    }

    std::unordered_map<std::string, int> index;
    std::unordered_map<std::string, int> lowlink;
    std::unordered_set<std::string> on_stack;
    std::vector<std::string> stack;
    int next_index = 0;

    std::function<void(const ModuleUnit&)> strongconnect = [&](const ModuleUnit& unit) {
        index[unit.module_name] = next_index;
        lowlink[unit.module_name] = next_index;
        ++next_index;
        stack.push_back(unit.module_name);
        on_stack.insert(unit.module_name);

        for (const std::string& imported : unit.imports) {
            const auto found = by_name.find(imported);
            if (found == by_name.end()) {
                continue;
            }

            if (!index.contains(imported)) {
                strongconnect(*found->second);
                lowlink[unit.module_name] = std::min(lowlink[unit.module_name], lowlink[imported]);
            } else if (on_stack.contains(imported)) {
                lowlink[unit.module_name] = std::min(lowlink[unit.module_name], index[imported]);
            }
        }

        if (lowlink[unit.module_name] != index[unit.module_name]) {
            return;
        }

        std::vector<const ModuleUnit*> component;
        while (!stack.empty()) {
            const std::string node = stack.back();
            stack.pop_back();
            on_stack.erase(node);

            const auto found = by_name.find(node);
            if (found != by_name.end()) {
                component.push_back(found->second);
            }
            if (node == unit.module_name) {
                break;
            }
        }

        bool self_cycle = false;
        if (component.size() == 1) {
            const ModuleUnit* single = component.front();
            self_cycle = std::find(single->imports.begin(), single->imports.end(), single->module_name) != single->imports.end();
        }

        if (component.size() > 1 || self_cycle) {
            std::sort(component.begin(), component.end(), [](const ModuleUnit* a, const ModuleUnit* b) {
                return a->module_name < b->module_name;
            });
            analysis.cycles.push_back(std::move(component));
        }
    };

    for (const auto& unit : analysis.modules) {
        if (!index.contains(unit.module_name)) {
            strongconnect(unit);
        }
    }

    std::sort(analysis.cycles.begin(), analysis.cycles.end(), [](const auto& a, const auto& b) {
        if (a.size() != b.size()) {
            return a.size() > b.size();
        }
        return a.front()->module_name < b.front()->module_name;
    });
}

[[nodiscard]] std::string normalize_error_signature(std::string message) {
    for (char& ch : message) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            ch = '#';
        }
    }
    return trim(message);
}

[[nodiscard]] std::optional<SymbolSite> parse_site_from_diagnostic_line(const std::string& line) {
    const std::size_t colon1 = line.find(':');
    const std::size_t colon2 = colon1 == std::string::npos ? std::string::npos : line.find(':', colon1 + 1);
    if (colon1 == std::string::npos || colon2 == std::string::npos) {
        return std::nullopt;
    }

    const std::string file = trim(line.substr(0, colon1));
    const std::string line_token = trim(line.substr(colon1 + 1, colon2 - colon1 - 1));
    if (file.empty() || line_token.empty() ||
        !std::all_of(line_token.begin(), line_token.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }

    const std::size_t message_pos = line.find("note:");
    const std::string text = message_pos == std::string::npos ? trim(line.substr(colon2 + 1)) : trim(line.substr(message_pos + 5));
    return SymbolSite{fs::path(file), std::stoi(line_token), text};
}

[[nodiscard]] std::string extract_ambiguous_symbol(const std::string& line) {
    const std::size_t first_quote = line.find('\'');
    if (first_quote == std::string::npos) {
        return {};
    }

    const std::size_t second_quote = line.find('\'', first_quote + 1);
    const std::size_t paren = line.find('(', first_quote + 1);
    std::size_t end = second_quote;
    if (paren != std::string::npos && (second_quote == std::string::npos || paren < second_quote)) {
        end = paren;
    }
    if (end == std::string::npos || end <= first_quote + 1) {
        return {};
    }
    return trim(line.substr(first_quote + 1, end - first_quote - 1));
}

void collect_symbol_candidate(std::unordered_set<std::string>& symbols, std::string candidate) {
    candidate = trim(candidate);
    while (!candidate.empty() && (candidate.back() == ')' || candidate.back() == ';' || candidate.back() == ',' || candidate.back() == '.')) {
        candidate.pop_back();
    }
    while (!candidate.empty() && (candidate.front() == '&' || candidate.front() == '*' || candidate.front() == '(')) {
        candidate.erase(candidate.begin());
    }

    if (looks_like_diagnostic_symbol(candidate)) {
        symbols.insert(std::move(candidate));
    }
}

void extract_symbols_from_text(std::string_view text, std::unordered_set<std::string>& symbols) {
    const std::string s(text);

    for (char quote : {'\'', '"'}) {
        std::size_t pos = 0;
        while ((pos = s.find(quote, pos)) != std::string::npos) {
            const std::size_t end = s.find(quote, pos + 1);
            if (end == std::string::npos) {
                break;
            }
            collect_symbol_candidate(symbols, s.substr(pos + 1, end - pos - 1));
            pos = end + 1;
        }
    }

    std::size_t pos = 0;
    while ((pos = s.find('(', pos)) != std::string::npos) {
        std::size_t start = pos;
        while (start > 0) {
            const char prev = s[start - 1];
            if (std::isalnum(static_cast<unsigned char>(prev)) != 0 || prev == '_' || prev == ':' ) {
                --start;
                continue;
            }
            break;
        }
        if (start < pos) {
            collect_symbol_candidate(symbols, s.substr(start, pos - start));
        }
        ++pos;
    }
}

void analyze_log_file(const fs::path& log_path, Analysis& analysis) {
    const auto lines = read_lines(log_path);
    analysis.logs_scanned.push_back(log_path);
    std::unordered_map<std::string, std::size_t> cluster_map;
    for (std::size_t idx = 0; idx < analysis.clusters.size(); ++idx) {
        cluster_map[analysis.clusters[idx].signature] = idx;
    }
    std::unordered_set<std::string> log_symbols(analysis.suspect_symbols.begin(), analysis.suspect_symbols.end());
    std::string last_compiler_command;

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[static_cast<std::size_t>(i)];
        const std::string lower = to_lower(line);
        if (line.rfind("[orchestrator] $ ", 0) == 0 && (lower.find("g++") != std::string::npos || lower.find("clang++") != std::string::npos)) {
            last_compiler_command = trim(line.substr(std::string("[orchestrator] $ ").size()));
            analysis.compiler_commands.push_back(last_compiler_command);
        }
        const bool is_error = lower.find(" error:") != std::string::npos ||
                              lower.find("fatal error:") != std::string::npos ||
                              lower.find("unknown command:") != std::string::npos ||
                              lower.find("failed to compile") != std::string::npos;
        const bool is_warning = lower.find("warning:") != std::string::npos;
        if (!is_error && !is_warning) {
            continue;
        }

        ErrorRecord record;
        record.raw = line;
        record.kind = is_error ? "error" : "warning";
        record.command_context = last_compiler_command;

        const std::size_t colon1 = line.find(':');
        const std::size_t colon2 = colon1 == std::string::npos ? std::string::npos : line.find(':', colon1 + 1);
        const std::size_t colon3 = colon2 == std::string::npos ? std::string::npos : line.find(':', colon2 + 1);

        if (colon1 != std::string::npos && colon2 != std::string::npos) {
            record.file = trim(line.substr(0, colon1));
            const std::string line_token = trim(line.substr(colon1 + 1, colon2 - colon1 - 1));
            if (!line_token.empty() && std::all_of(line_token.begin(), line_token.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                record.line = std::stoi(line_token);
            }
            if (colon3 != std::string::npos) {
                record.message = trim(line.substr(colon3 + 1));
            } else {
                record.message = trim(line.substr(colon2 + 1));
            }
        }

        if (record.message.empty()) {
            record.message = trim(line);
        }

        extract_symbols_from_text(record.message, log_symbols);
        extract_symbols_from_text(record.raw, log_symbols);

        const std::string signature = normalize_error_signature(record.message);
        auto found = cluster_map.find(signature);
        std::size_t cluster_index = 0;
        if (found == cluster_map.end()) {
            analysis.clusters.push_back(ErrorCluster{signature, 0, {}});
            cluster_index = analysis.clusters.size() - 1;
            cluster_map[signature] = cluster_index;
        } else {
            cluster_index = found->second;
        }

        ErrorCluster& cluster = analysis.clusters[cluster_index];

        cluster.count += 1;
        if (cluster.samples.size() < 4) {
            cluster.samples.push_back(std::move(record));
        }
        if (!last_compiler_command.empty()) {
            cluster.command_hits[last_compiler_command] += 1;
        }

        if (lower.find("crc mismatch") != std::string::npos) {
            analysis.cache_hints.push_back("Module cache CRC mismatch detected in " + log_path.string() + "; clear stale gcm.cache or rebuild affected module closure.");
        }
        if (lower.find("cannot import module in its own purview") != std::string::npos) {
            analysis.cache_hints.push_back("A module imported itself through an indirect cycle; inspect SCCs in the module graph and remove type-level back edges from interface units.");
        }
        if (lower.find("ambiguous") != std::string::npos) {
            analysis.cache_hints.push_back("Ambiguous symbol resolution detected in " + log_path.string() + "; prefer a single owning declaration and eliminate duplicate forward declarations across module interfaces.");

            AmbiguousDiagnostic diag;
            diag.symbol = extract_ambiguous_symbol(line);
            diag.trigger_message = trim(line);
            if (const auto trigger_site = parse_site_from_diagnostic_line(line)) {
                diag.error_file = trigger_site->file;
                diag.error_line = trigger_site->line;
            }

            for (int j = i + 1; j < static_cast<int>(lines.size()) && j <= i + 18; ++j) {
                const std::string& candidate_line = lines[static_cast<std::size_t>(j)];
                const std::string candidate_lower = to_lower(candidate_line);
                if (candidate_lower.find(" error:") != std::string::npos && j > i + 1) {
                    break;
                }
                if (candidate_lower.find("candidate ") == std::string::npos && candidate_lower.find("candidates are:") == std::string::npos) {
                    continue;
                }
                if (auto site = parse_site_from_diagnostic_line(candidate_line)) {
                    diag.candidates.push_back(*site);
                }
            }

            if (!diag.symbol.empty()) {
                analysis.ambiguous_diagnostics.push_back(std::move(diag));
            }
        }
    }

    analysis.suspect_symbols.assign(log_symbols.begin(), log_symbols.end());
    std::sort(analysis.suspect_symbols.begin(), analysis.suspect_symbols.end());
}

void finalize_cluster_command_contexts(Analysis& analysis) {
    for (auto& cluster : analysis.clusters) {
        std::pair<std::string, std::size_t> best{"", 0};
        for (const auto& [command, hits] : cluster.command_hits) {
            if (hits > best.second || (hits == best.second && command < best.first)) {
                best = {command, hits};
            }
        }
        cluster.likely_command = best.first;
        cluster.likely_command_hits = best.second;
    }

    if (!analysis.clusters.empty()) {
        if (!analysis.clusters.front().likely_command.empty()) {
            analysis.likely_failing_command = analysis.clusters.front().likely_command;
        } else if (!analysis.compiler_commands.empty()) {
            analysis.likely_failing_command = analysis.compiler_commands.back();
        }
    } else if (!analysis.last_exit_code.empty() && analysis.last_exit_code != "0" && !analysis.compiler_commands.empty()) {
        analysis.likely_failing_command = analysis.compiler_commands.back();
    }
}

void backfill_cluster_commands_from_compiler_history(Analysis& analysis) {
    if (analysis.compiler_commands.empty()) {
        return;
    }

    for (auto& cluster : analysis.clusters) {
        if (!cluster.likely_command.empty()) {
            continue;
        }

        std::string matched_command;
        for (const auto& sample : cluster.samples) {
            if (sample.file.empty()) {
                continue;
            }

            const std::string file_generic = to_lower(sample.file.generic_string());
            const std::string file_native = to_lower(sample.file.string());
            for (auto it = analysis.compiler_commands.rbegin(); it != analysis.compiler_commands.rend(); ++it) {
                const std::string lowered = to_lower(*it);
                if (lowered.find(file_generic) != std::string::npos || lowered.find(file_native) != std::string::npos) {
                    matched_command = *it;
                    break;
                }
            }
            if (!matched_command.empty()) {
                break;
            }
        }

        if (!matched_command.empty()) {
            cluster.likely_command = matched_command;
            cluster.likely_command_hits = cluster.count;
        }
    }

    if (analysis.likely_failing_command.empty() && !analysis.clusters.empty()) {
        for (const auto& cluster : analysis.clusters) {
            if (!cluster.likely_command.empty()) {
                analysis.likely_failing_command = cluster.likely_command;
                break;
            }
        }
    }
}

[[nodiscard]] bool path_is_within_root(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec) {
        return false;
    }
    const fs::path canonical_path = fs::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }

    const std::string root_text = canonical_root.generic_string();
    const std::string path_text = canonical_path.generic_string();
    return path_text.rfind(root_text, 0) == 0;
}

[[nodiscard]] int shared_path_prefix_score(const fs::path& a, const fs::path& b) {
    auto ita = a.begin();
    auto itb = b.begin();
    int score = 0;
    for (; ita != a.end() && itb != b.end(); ++ita, ++itb) {
        if (to_lower(ita->string()) != to_lower(itb->string())) {
            break;
        }
        score += 5;
    }
    return score;
}

void score_candidate(const fs::path& root, const fs::path& error_file, OwnershipCandidate& candidate) {
    int score = 0;
    const std::string file_text = to_lower(candidate.file.generic_string());
    const std::string decl_text = to_lower(candidate.declaration);

    if (path_is_within_root(root, candidate.file)) {
        score += 20;
        candidate.reasons.push_back("inside DGTL workspace");
    } else {
        score -= 15;
        candidate.reasons.push_back("outside DGTL workspace");
    }
    if (file_text.find("/gen/") != std::string::npos || file_text.find("_gen_") != std::string::npos) {
        score += 35;
        candidate.reasons.push_back("generated-definition ownership path");
    }
    if (file_text.find("/include/dgtl/ir/gen/") != std::string::npos) {
        score += 35;
        candidate.reasons.push_back("importable generated IR interface");
    }
    if (file_text.find("/tools/cpp/") != std::string::npos) {
        score -= 25;
        candidate.reasons.push_back("generator tool source, not runtime owner");
    }
    if (file_text.find("/src/") != std::string::npos || candidate.file.extension() == ".cpp") {
        score += 20;
        candidate.reasons.push_back("implementation file");
    }
    if (decl_text.find("inline") != std::string::npos || decl_text.find("constexpr") != std::string::npos) {
        score += 20;
        candidate.reasons.push_back("inline or constexpr definition");
    }
    if (!candidate.declaration.empty() && candidate.declaration.back() == ';' && decl_text.find("inline") == std::string::npos) {
        score -= 20;
        candidate.reasons.push_back("declaration-only surface");
    }
    if (file_text.find("/rewrite/") != std::string::npos || file_text.find("/base/") != std::string::npos) {
        score -= 10;
        candidate.reasons.push_back("non-owning interface layer");
    }
    if (!error_file.empty()) {
        const int prefix = shared_path_prefix_score(error_file, candidate.file);
        if (prefix > 0) {
            score += prefix;
            candidate.reasons.push_back("path proximity to error site");
        }
    }

    candidate.score = score;
}

void build_ownership_recommendations(const fs::path& root, Analysis& analysis) {
    std::unordered_map<std::string, const SymbolCollision*> collisions_by_symbol;
    for (const auto& collision : analysis.collisions) {
        collisions_by_symbol[collision.symbol] = &collision;
    }
    std::unordered_set<std::string> recommended_symbols;

    for (const auto& diag : analysis.ambiguous_diagnostics) {
        if (diag.symbol.empty()) {
            continue;
        }

        OwnershipRecommendation rec;
        rec.symbol = diag.symbol;
        rec.error_file = diag.error_file;
        rec.error_line = diag.error_line;
        rec.trigger_message = diag.trigger_message;

        std::set<std::tuple<std::string, int, std::string>> seen;
        auto push_candidate = [&](const SymbolSite& site) {
            const auto key = std::make_tuple(to_lower(site.file.generic_string()), site.line, site.text);
            if (!seen.insert(key).second) {
                return;
            }
            rec.candidates.push_back(OwnershipCandidate{site.file, site.line, site.text, 0, {}});
        };

        for (const auto& site : diag.candidates) {
            push_candidate(site);
        }
        if (const auto found = collisions_by_symbol.find(diag.symbol); found != collisions_by_symbol.end()) {
            for (const auto& site : found->second->sites) {
                push_candidate(site);
            }
        }

        for (auto& candidate : rec.candidates) {
            score_candidate(root, rec.error_file, candidate);
        }

        std::sort(rec.candidates.begin(), rec.candidates.end(), [](const OwnershipCandidate& a, const OwnershipCandidate& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            if (a.file != b.file) {
                return a.file.generic_string() < b.file.generic_string();
            }
            return a.line < b.line;
        });

        if (!rec.candidates.empty()) {
            recommended_symbols.insert(rec.symbol);
            analysis.ownership_recommendations.push_back(std::move(rec));
        }
    }

    for (const auto& collision : analysis.collisions) {
        if (recommended_symbols.contains(collision.symbol) || collision.sites.size() < 2 ||
            !looks_like_actionable_owner_symbol(collision.symbol)) {
            continue;
        }

        OwnershipRecommendation rec;
        rec.symbol = collision.symbol;
        rec.trigger_message = "Ownership fallback generated from multi-site declaration surface.";
        for (const auto& site : collision.sites) {
            rec.candidates.push_back(OwnershipCandidate{site.file, site.line, site.text, 0, {}});
        }

        for (auto& candidate : rec.candidates) {
            score_candidate(root, {}, candidate);
        }

        std::sort(rec.candidates.begin(), rec.candidates.end(), [](const OwnershipCandidate& a, const OwnershipCandidate& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            if (a.file != b.file) {
                return a.file.generic_string() < b.file.generic_string();
            }
            return a.line < b.line;
        });

        analysis.ownership_recommendations.push_back(std::move(rec));
    }

    std::sort(analysis.ownership_recommendations.begin(), analysis.ownership_recommendations.end(), [](const OwnershipRecommendation& a, const OwnershipRecommendation& b) {
        if (a.candidates.empty() || b.candidates.empty()) {
            return a.symbol < b.symbol;
        }
        return a.candidates.front().score > b.candidates.front().score;
    });
}

[[nodiscard]] bool looks_like_decl_line(const std::string& line) {
    static const std::vector<std::string> tokens{
        "find_generated_op_def(", "struct ", "class ", "enum class ", "using ", "typedef ", "void ", "int ", "bool ", "const "};
    for (const auto& token : tokens) {
        if (line.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<SymbolSite> collect_declaration_sites_cached(const fs::path& root, Analysis& analysis) {
    const fs::path cache_path = root / "build" / "logs" / "dgtl-symbol-index.cache";
    auto cache = load_declaration_index_cache(cache_path);

    std::vector<SymbolSite> sites;
    std::unordered_set<std::string> current_paths;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        const fs::path path = entry.path();

        if (entry.is_directory(ec) && should_skip_path(path)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(ec) || !has_source_extension(path) || should_skip_path(path)) {
            continue;
        }

        const std::string path_text = path.generic_string();
        current_paths.insert(path_text);
        const std::int64_t mtime_ticks = static_cast<std::int64_t>(get_file_mtime(path).time_since_epoch().count());

        auto cache_it = cache.find(path_text);
        if (cache_it != cache.end() && cache_it->second.mtime_ticks == mtime_ticks) {
            analysis.declaration_cache_hits += 1;
            sites.insert(sites.end(), cache_it->second.sites.begin(), cache_it->second.sites.end());
            continue;
        }

        analysis.declaration_cache_misses += 1;
        analysis.declaration_files_scanned += 1;

        DeclarationIndexCacheEntry refreshed;
        refreshed.mtime_ticks = mtime_ticks;
        const auto lines = read_lines(path);
        for (int line_no = 0; line_no < static_cast<int>(lines.size()); ++line_no) {
            const std::string& line = lines[static_cast<std::size_t>(line_no)];
            if (!looks_like_decl_line(line)) {
                continue;
            }
            SymbolSite site{path, line_no + 1, trim(line)};
            refreshed.sites.push_back(site);
            sites.push_back(std::move(site));
        }

        cache[path_text] = std::move(refreshed);
    }

    for (auto it = cache.begin(); it != cache.end();) {
        if (!current_paths.contains(it->first)) {
            it = cache.erase(it);
            continue;
        }
        ++it;
    }

    write_declaration_index_cache(cache_path, cache);
    return sites;
}

void analyze_symbol_collisions(const fs::path& root, Analysis& analysis, const Options& opts) {
    if (!opts.deep_symbols) {
        return;
    }

    static const std::vector<std::string> watch_symbols{
        "find_generated_op_def", "RuntimeFeedback", "_mm_pause", "IRStream", "TargetMachine"};

    std::vector<std::string> symbols = watch_symbols;
    symbols.insert(symbols.end(), analysis.suspect_symbols.begin(), analysis.suspect_symbols.end());
    if (!opts.focus.empty()) {
        symbols.push_back(opts.focus);
    }
    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());

    std::unordered_map<std::string, std::vector<SymbolSite>> sites;
    const auto declaration_sites = collect_declaration_sites_cached(root, analysis);
    for (const auto& site : declaration_sites) {
        for (const auto& symbol : symbols) {
            if (site.text.find(symbol) != std::string::npos) {
                sites[symbol].push_back(site);
            }
        }
    }

    for (auto& [symbol, entries] : sites) {
        if (entries.size() <= 1) {
            continue;
        }
        std::sort(entries.begin(), entries.end(), [](const SymbolSite& a, const SymbolSite& b) {
            if (a.file != b.file) {
                return a.file.string() < b.file.string();
            }
            return a.line < b.line;
        });
        analysis.collisions.push_back(SymbolCollision{symbol, std::move(entries)});
    }

    std::sort(analysis.collisions.begin(), analysis.collisions.end(), [](const SymbolCollision& a, const SymbolCollision& b) {
        return a.sites.size() > b.sites.size();
    });
}

[[nodiscard]] std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

void emit_json(const Analysis& analysis, const fs::path& root) {
    std::cout << "{\n";
    std::cout << "  \"root\": \"" << json_escape(root.string()) << "\",\n";
    std::cout << "  \"module_count\": " << analysis.modules.size() << ",\n";
    std::cout << "  \"cycle_count\": " << analysis.cycles.size() << ",\n";
    std::cout << "  \"declaration_cache\": {\"hits\": " << analysis.declaration_cache_hits
              << ", \"misses\": " << analysis.declaration_cache_misses
              << ", \"files_scanned\": " << analysis.declaration_files_scanned << "},\n";
    std::cout << "  \"logs_scanned\": [";
    for (std::size_t i = 0; i < analysis.logs_scanned.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << "\"" << json_escape(analysis.logs_scanned[i].string()) << "\"";
    }
    std::cout << "],\n";
    std::cout << "  \"cycles\": [\n";
    for (std::size_t i = 0; i < analysis.cycles.size(); ++i) {
        const auto& cycle = analysis.cycles[i];
        std::cout << "    {\"modules\": [";
        for (std::size_t j = 0; j < cycle.size(); ++j) {
            if (j != 0) std::cout << ", ";
            std::cout << "\"" << json_escape(cycle[j]->module_name) << "\"";
        }
        std::cout << "]}";
        if (i + 1 != analysis.cycles.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"error_clusters\": [\n";
    for (std::size_t i = 0; i < analysis.clusters.size(); ++i) {
        const auto& cluster = analysis.clusters[i];
        std::cout << "    {\"signature\": \"" << json_escape(cluster.signature) << "\", \"count\": " << cluster.count;
        if (!cluster.likely_command.empty()) {
            std::cout << ", \"likely_command\": \"" << json_escape(cluster.likely_command) << "\", \"likely_command_hits\": " << cluster.likely_command_hits;
        }
        std::cout << "}";
        if (i + 1 != analysis.clusters.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"cache_hints\": [";
    for (std::size_t i = 0; i < analysis.cache_hints.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << "\"" << json_escape(analysis.cache_hints[i]) << "\"";
    }
    std::cout << "],\n";
    std::cout << "  \"last_command\": \"" << json_escape(analysis.last_command) << "\",\n";
    std::cout << "  \"last_exit_code\": \"" << json_escape(analysis.last_exit_code) << "\",\n";
    std::cout << "  \"likely_failing_command\": \"" << json_escape(analysis.likely_failing_command) << "\",\n";
    std::cout << "  \"compiler_fingerprint\": \"" << json_escape(analysis.compiler_fingerprint) << "\",\n";
    std::cout << "  \"command_fingerprint\": \"" << json_escape(analysis.command_fingerprint) << "\",\n";
    std::cout << "  \"symbol_collisions\": [";
    for (std::size_t i = 0; i < analysis.collisions.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << "{\"symbol\": \"" << json_escape(analysis.collisions[i].symbol)
                  << "\", \"site_count\": " << analysis.collisions[i].sites.size() << "}";
    }
    std::cout << "],\n";
    std::cout << "  \"regression_notes\": [";
    for (std::size_t i = 0; i < analysis.regression_notes.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << "\"" << json_escape(analysis.regression_notes[i]) << "\"";
    }
    std::cout << "],\n";
    std::cout << "  \"ownership_recommendations\": [";
    for (std::size_t i = 0; i < analysis.ownership_recommendations.size(); ++i) {
        if (i != 0) std::cout << ", ";
        const auto& rec = analysis.ownership_recommendations[i];
        std::cout << "{\"symbol\": \"" << json_escape(rec.symbol) << "\", \"candidate_count\": " << rec.candidates.size();
        if (!rec.candidates.empty()) {
            std::cout << ", \"winner\": \"" << json_escape(rec.candidates.front().file.generic_string()) << ":" << rec.candidates.front().line << "\"";
        }
        std::cout << "}";
    }
    std::cout << "]\n";
    std::cout << "}\n";
}

std::string build_markdown_report(const Analysis& analysis, const fs::path& root, const Options& opts) {
    std::ostringstream out;
    if (analysis.clusters.empty() && analysis.last_exit_code == "0") {
        out << "# DGTL Build Diagnostic Report\n\n";
        out << "- Status: **SUCCESS** (0 errors detected)\n";
    } else {
        out << "# DGTL Incident Report\n\n";
    }
    out << "- Root: " << root.string() << "\n";
    if (!analysis.last_command.empty()) {
        out << "- Last command: `" << analysis.last_command << "`\n";
    }
    if (!analysis.last_exit_code.empty()) {
        out << "- Last exit code: `" << analysis.last_exit_code << "`\n";
    }
    if (!analysis.compiler_fingerprint.empty()) {
        out << "- Compiler fingerprint: `" << analysis.compiler_fingerprint << "`\n";
    }
    if (!analysis.command_fingerprint.empty()) {
        out << "- Command fingerprint: `" << analysis.command_fingerprint << "`\n";
    }
    out << "- Logs scanned: " << analysis.logs_scanned.size() << "\n";
    out << "- Declaration cache: " << analysis.declaration_cache_hits << " hits, "
        << analysis.declaration_cache_misses << " misses, "
        << analysis.declaration_files_scanned << " rescanned files\n";
    if (!analysis.likely_failing_command.empty()) {
        out << "- Likely failing command: `" << analysis.likely_failing_command << "`\n";
    }
    out << "\n## Error Clusters\n";
    if (analysis.clusters.empty()) {
        out << "- No errors detected.\n";
    } else {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            out << "- (x" << analysis.clusters[i].count << ") " << analysis.clusters[i].signature << "\n";
            if (!analysis.clusters[i].likely_command.empty()) {
                out << "  Command: `" << analysis.clusters[i].likely_command << "` (" << analysis.clusters[i].likely_command_hits << " hits)\n";
            }
        }
    }
    if (!analysis.clusters.empty()) {
        out << "\n## Failing Command Context\n";
        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            const auto& cluster = analysis.clusters[i];
            if (cluster.likely_command.empty()) {
                continue;
            }
            out << "- `" << cluster.signature << "`\n";
            out << "  Compile step: `" << cluster.likely_command << "`\n";
            out << "  Evidence hits: " << cluster.likely_command_hits << "\n";
        }
    }
    out << "\n## Ownership Recommendations\n";
    if (analysis.ownership_recommendations.empty()) {
        out << "- No ambiguous ownership recommendations generated.\n";
    } else {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.ownership_recommendations.size()); ++i) {
            const auto& rec = analysis.ownership_recommendations[i];
            out << "- Symbol `" << rec.symbol << "`\n";
            if (!rec.candidates.empty()) {
                const auto& winner = rec.candidates.front();
                out << "  Winner: " << display_path(root, winner.file, opts.relative_paths) << ":" << winner.line
                    << " (score " << winner.score << ")\n";
                if (!winner.snippet_lines.empty()) {
                    out << "  Evidence:\n\n```cpp\n";
                    for (std::size_t idx = 0; idx < winner.snippet_lines.size(); ++idx) {
                        out << (winner.snippet_start_line + static_cast<int>(idx)) << ": " << winner.snippet_lines[idx] << "\n";
                    }
                    out << "```\n";
                }
            }
        }
    }
    if (!analysis.regression_notes.empty()) {
        out << "\n## Regression Notes\n";
        for (const auto& note : analysis.regression_notes) {
            out << "- " << note << "\n";
        }
    }
    return out.str();
}

[[nodiscard]] std::string build_text_report(const Analysis& analysis, const fs::path& root, const Options& opts) {
    std::ostringstream buffer;
    auto* old = std::cout.rdbuf(buffer.rdbuf());
    emit_text(analysis, root, opts);
    std::cout.rdbuf(old);
    return buffer.str();
}

void write_export_report(const Analysis& analysis, const fs::path& root, const Options& opts) {
    if (!opts.export_path) {
        return;
    }

    std::error_code ec;
    const fs::path export_path = *opts.export_path;
    if (!export_path.parent_path().empty()) {
        fs::create_directories(export_path.parent_path(), ec);
    }

    std::ofstream out(export_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Failed to open export path: " << export_path.string() << "\n";
        return;
    }

    const std::string ext = to_lower(export_path.extension().string());
    if (ext == ".json") {
        std::ostringstream buffer;
        auto* old = std::cout.rdbuf(buffer.rdbuf());
        emit_json(analysis, root);
        std::cout.rdbuf(old);
        out << buffer.str();
    } else if (ext == ".txt") {
        out << build_text_report(analysis, root, opts);
    } else {
        out << build_markdown_report(analysis, root, opts);
    }
}

void emit_text(const Analysis& analysis, const fs::path& root, const Options& opts) {
    std::cout << "DGTL Forensic Debugger\n";
    std::cout << "Root: " << root.string() << "\n\n";
    if (!analysis.last_command.empty()) {
        std::cout << "Last command: " << analysis.last_command << "\n";
    }
    if (!analysis.last_exit_code.empty()) {
        std::cout << "Last exit code: " << analysis.last_exit_code << "\n";
    }
    if (!analysis.compiler_fingerprint.empty()) {
        std::cout << "Compiler fingerprint: " << analysis.compiler_fingerprint << "\n";
    }
    if (!analysis.command_fingerprint.empty()) {
        std::cout << "Command fingerprint: " << analysis.command_fingerprint << "\n";
    }
    std::cout << "Declaration cache: " << analysis.declaration_cache_hits << " hits, "
              << analysis.declaration_cache_misses << " misses, "
              << analysis.declaration_files_scanned << " rescanned files\n";
    if (!analysis.likely_failing_command.empty()) {
        std::cout << "Likely failing command: " << analysis.likely_failing_command << "\n";
    }
    if (analysis.last_session_log) {
        std::cout << "Last session log: " << display_path(root, *analysis.last_session_log, opts.relative_paths) << "\n";
    }
    if (!analysis.last_command.empty() || !analysis.last_exit_code.empty() || analysis.last_session_log) {
        std::cout << "\n";
    }

    std::cout << "[1] Module Graph\n";
    std::cout << "  Modules discovered: " << analysis.modules.size() << "\n";
    std::cout << "  Cycles detected:    " << analysis.cycles.size() << "\n";
    if (!opts.summary_only) {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.cycles.size()); ++i) {
            const auto& cycle = analysis.cycles[i];
            std::cout << "  Cycle " << (i + 1) << ":\n";
            for (const ModuleUnit* unit : cycle) {
                std::cout << "    - " << unit->module_name << " @ "
                          << display_path(root, unit->path, opts.relative_paths) << "\n";
            }
        }
    }
    std::cout << "\n";

    std::cout << "[2] Error Clusters\n";
    std::cout << "  Logs scanned: " << analysis.logs_scanned.size() << "\n";
    std::cout << "  Clusters: " << analysis.clusters.size() << "\n";
    for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
        const auto& cluster = analysis.clusters[i];
        std::cout << "  - (x" << cluster.count << ") " << cluster.signature << "\n";
        if (!cluster.likely_command.empty()) {
            std::cout << "      command: " << cluster.likely_command << " (" << cluster.likely_command_hits << " hits)\n";
        }
        if (!opts.summary_only) {
            for (const auto& sample : cluster.samples) {
                std::cout << "      " << display_path(root, sample.file, opts.relative_paths);
                if (sample.line > 0) {
                    std::cout << ":" << sample.line;
                }
                std::cout << " :: " << sample.message << "\n";
            }
        }
    }
    std::cout << "\n";

    std::cout << "[2a] Failing Command Context\n";
    if (analysis.clusters.empty()) {
        std::cout << "  No error clusters available for command attribution.\n";
    } else {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            const auto& cluster = analysis.clusters[i];
            if (cluster.likely_command.empty()) {
                continue;
            }
            std::cout << "  - " << cluster.signature << "\n";
            std::cout << "      compile step: " << cluster.likely_command << "\n";
            std::cout << "      evidence hits: " << cluster.likely_command_hits << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "[3] Cache & Cycle Hints\n";
    if (analysis.cache_hints.empty()) {
        std::cout << "  No cache-specific hints emitted.\n";
    } else {
        std::set<std::string> deduped(analysis.cache_hints.begin(), analysis.cache_hints.end());
        for (const auto& hint : deduped) {
            std::cout << "  - " << hint << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "[4] Symbol Collision Radar\n";
    std::cout << "  Dynamic suspect symbols: " << analysis.suspect_symbols.size() << "\n";
    std::cout << "  Suspect symbols: " << analysis.collisions.size() << "\n";
    if (!opts.summary_only) {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.collisions.size()); ++i) {
            const auto& collision = analysis.collisions[i];
            std::cout << "  - " << collision.symbol << " appears at " << collision.sites.size() << " sites\n";
            for (std::size_t j = 0; j < std::min<std::size_t>(3, collision.sites.size()); ++j) {
                const auto& site = collision.sites[j];
                std::cout << "      " << display_path(root, site.file, opts.relative_paths)
                          << ":" << site.line << " :: " << site.text << "\n";
            }
        }
    }
    std::cout << "\n";

    std::cout << "[5] Ownership Arbitration\n";
    std::cout << "  Recommendations: " << analysis.ownership_recommendations.size() << "\n";
    for (std::size_t i = 0; i < std::min(opts.top, analysis.ownership_recommendations.size()); ++i) {
        const auto& rec = analysis.ownership_recommendations[i];
        if (rec.candidates.empty()) {
            continue;
        }
        const auto& winner = rec.candidates.front();
        std::cout << "  - " << rec.symbol << " -> " << display_path(root, winner.file, opts.relative_paths)
                  << ":" << winner.line << " (score " << winner.score << ")\n";
        if (!opts.summary_only) {
            std::cout << "      why: ";
            for (std::size_t j = 0; j < winner.reasons.size(); ++j) {
                if (j != 0) {
                    std::cout << ", ";
                }
                std::cout << winner.reasons[j];
            }
            std::cout << "\n";
            if (!winner.snippet_lines.empty()) {
                for (std::size_t idx = 0; idx < winner.snippet_lines.size(); ++idx) {
                    std::cout << "      " << (winner.snippet_start_line + static_cast<int>(idx)) << ": " << winner.snippet_lines[idx] << "\n";
                }
            }
        }
    }
    std::cout << "\n";

    std::cout << "[6] Staff-Level Triage\n";
    if (!analysis.cycles.empty()) {
        std::cout << "  Primary structural risk: module SCCs are present. Remove type-sharing dependencies from interface units and keep feedback data crossing boundaries as scalars or opaque handles.\n";
    } else {
        std::cout << "  Primary structural risk: no import SCCs detected in scanned units.\n";
    }
    if (!analysis.clusters.empty()) {
        std::cout << "  Primary execution risk: fix the highest-frequency error cluster first because downstream diagnostics are likely derivative noise.\n";
    }
    for (const auto& note : analysis.regression_notes) {
        std::cout << "  Regression: " << note << "\n";
    }
    if (!opts.focus.empty()) {
        std::cout << "  Focus hint: searched with focus token '" << opts.focus << "'.\n";
    }
}

[[nodiscard]] Analysis run_analysis(const fs::path& root, const Options& opts) {
    Analysis analysis;
    analysis.suspect_symbols = {
        "find_generated_op_def", "RuntimeFeedback", "_mm_pause", "IRStream", "TargetMachine"
    };
    analysis.modules = discover_modules(root);
    find_module_cycles(analysis);

    const std::vector<fs::path> candidate_session_logs = {
        root / "debug_logs" / "sessions" / "last-nondebug-session.log",
        root / "debug_logs" / "sessions" / "last-session.log",
        root / "build" / "logs" / "last-nondebug-session.log",
        root / "build" / "logs" / "last-session.log"
    };
    for (const auto& cand : candidate_session_logs) {
        if (fs::exists(cand)) {
            analysis.last_session_log = cand;
            break;
        }
    }

    const std::vector<fs::path> candidate_commands = {
        root / "debug_logs" / "sessions" / "last-nondebug-command.txt",
        root / "debug_logs" / "sessions" / "last-command.txt",
        root / "build" / "logs" / "last-nondebug-command.txt",
        root / "build" / "logs" / "last-command.txt"
    };
    for (const auto& cand : candidate_commands) {
        if (fs::exists(cand)) {
            analysis.last_command = read_text_file(cand);
            if (!analysis.last_command.empty()) break;
        }
    }

    const std::vector<fs::path> candidate_exit_codes = {
        root / "debug_logs" / "sessions" / "last-nondebug-exit-code.txt",
        root / "debug_logs" / "sessions" / "last-exit-code.txt",
        root / "build" / "logs" / "last-nondebug-exit-code.txt",
        root / "build" / "logs" / "last-exit-code.txt"
    };
    for (const auto& cand : candidate_exit_codes) {
        if (fs::exists(cand)) {
            analysis.last_exit_code = read_text_file(cand);
            if (!analysis.last_exit_code.empty()) break;
        }
    }

    const std::vector<fs::path> candidate_compiler_logs = {
        root / "debug_logs" / "sessions" / "last-compiler-invocations.log",
        root / "build" / "logs" / "last-compiler-invocations.log"
    };
    for (const auto& cand : candidate_compiler_logs) {
        if (fs::exists(cand)) {
            const auto compiler_lines = read_lines(cand);
            for (const auto& line : compiler_lines) {
                const std::string command = strip_compiler_record_prefix(line);
                if (!command.empty()) {
                    analysis.compiler_commands.push_back(command);
                }
            }
            if (!analysis.compiler_commands.empty()) break;
        }
    }

    std::vector<fs::path> logs = opts.log_paths;
    if (logs.empty()) {
        if (!opts.all_logs && analysis.last_session_log) {
            logs.push_back(*analysis.last_session_log);
        } else if (opts.all_logs) {
            logs = discover_log_files(root);
        } else if (analysis.last_session_log) {
            logs.push_back(*analysis.last_session_log);
        }
    }

    std::sort(logs.begin(), logs.end());
    logs.erase(std::unique(logs.begin(), logs.end()), logs.end());

    for (const auto& log_path : logs) {
        if (fs::exists(log_path)) {
            analyze_log_file(log_path, analysis);
        }
    }

    std::sort(analysis.clusters.begin(), analysis.clusters.end(), [](const ErrorCluster& a, const ErrorCluster& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.signature < b.signature;
    });
    finalize_cluster_command_contexts(analysis);

    analyze_symbol_collisions(root, analysis, opts);
    build_ownership_recommendations(root, analysis);
    attach_candidate_source_slices(analysis);
    backfill_cluster_commands_from_compiler_history(analysis);
    if (!analysis.compiler_commands.empty()) {
        std::ostringstream compiler_key;
        for (const auto& cmd : analysis.compiler_commands) {
            compiler_key << cmd << '\n';
        }
        analysis.compiler_fingerprint = hex64(fnv1a64(compiler_key.str()));
    }
    analysis.command_fingerprint = build_command_fingerprint(analysis);

    const fs::path log_root = root / "build" / "logs";
    const fs::path latest_snapshot_path = log_root / "dgtl-debug-latest.snapshot";
    const fs::path baseline_snapshot_path = opts.baseline_path.value_or(log_root / "dgtl-debug-baseline.snapshot");
    const Snapshot current_snapshot = make_snapshot(analysis);
    compare_against_snapshot(load_snapshot(latest_snapshot_path), current_snapshot, analysis, "Previous run");
    compare_against_snapshot(load_snapshot(baseline_snapshot_path), current_snapshot, analysis, "Baseline");
    write_snapshot(current_snapshot, latest_snapshot_path);
    if (opts.write_baseline) {
        write_snapshot(current_snapshot, baseline_snapshot_path);
    }
    return analysis;
}

} // namespace

namespace dgtl::debug {

// ============================================================================
// 1. PURE C++ x86_64 DISASSEMBLER
// ============================================================================
struct DecodedInstruction {
    std::string text;
    size_t length{1};
    uint64_t target_mem_address{0};
    bool has_mem_ref{false};
};

class X64Disassembler {
public:
    static DecodedInstruction decode(const uint8_t* code, size_t max_len, uint64_t rip) {
        DecodedInstruction res;
        if (!code || max_len == 0) {
            res.text = "db 0x00";
            res.length = 1;
            return res;
        }

        size_t offset = 0;
        bool rex_w = false, rex_r = false, rex_x = false, rex_b = false;
        bool has_66 = false, has_67 = false, has_f2 = false, has_f3 = false;

        // Decode prefixes
        while (offset < max_len) {
            uint8_t pfx = code[offset];
            if (pfx == 0x66) { has_66 = true; ++offset; }
            else if (pfx == 0x67) { has_67 = true; ++offset; }
            else if (pfx == 0xF2) { has_f2 = true; ++offset; }
            else if (pfx == 0xF3) { has_f3 = true; ++offset; }
            else if (pfx >= 0x40 && pfx <= 0x4F) {
                rex_w = (pfx & 0x08) != 0;
                rex_r = (pfx & 0x04) != 0;
                rex_x = (pfx & 0x02) != 0;
                rex_b = (pfx & 0x01) != 0;
                ++offset;
                break;
            } else if (pfx == 0x2E || pfx == 0x36 || pfx == 0x3E || pfx == 0x26 || pfx == 0x64 || pfx == 0x65) {
                ++offset;
            } else {
                break;
            }
        }

        if (offset >= max_len) {
            res.text = "db 0x" + to_hex(code[0]);
            res.length = 1;
            return res;
        }

        static const char* r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                     "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15" };
        static const char* r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
                                     "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
        static const char* r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
                                     "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" };
        static const char* r8[]  = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
                                     "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" };
        static const char* xmm[] = { "xmm0", "xmm1", "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",  "xmm7",
                                     "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" };

        auto reg_name = [&](uint8_t id, bool is_64, bool is_16, bool is_8) -> std::string {
            id &= 0x0F;
            if (is_64) return r64[id];
            if (is_16) return r16[id];
            if (is_8)  return r8[id];
            return r32[id];
        };

        uint8_t op = code[offset++];

        auto decode_modrm = [&](bool is_64, bool is_16, bool is_8,
                                std::string& out_rm, std::string& out_reg, uint8_t& out_reg_id, uint8_t& out_rm_id) -> bool {
            if (offset >= max_len) return false;
            uint8_t modrm = code[offset++];
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t reg = ((modrm >> 3) & 7) | (rex_r ? 8 : 0);
            uint8_t rm  = (modrm & 7) | (rex_b ? 8 : 0);
            out_reg_id = reg;
            out_rm_id = rm;
            out_reg = reg_name(reg, is_64, is_16, is_8);

            if (mod == 3) {
                out_rm = reg_name(rm, is_64, is_16, is_8);
                return true;
            }

            res.has_mem_ref = true;
            std::string ptr_size = is_64 ? "qword ptr " : (is_16 ? "word ptr " : (is_8 ? "byte ptr " : "dword ptr "));

            bool has_sib = ((rm & 7) == 4);
            uint8_t scale = 1, idx = 0, base = 0;
            bool has_index = false;

            if (has_sib) {
                if (offset >= max_len) return false;
                uint8_t sib = code[offset++];
                scale = 1 << ((sib >> 6) & 3);
                idx = ((sib >> 3) & 7) | (rex_x ? 8 : 0);
                base = (sib & 7) | (rex_b ? 8 : 0);
                has_index = (idx != 4);
            }

            int64_t disp = 0;
            if (mod == 0) {
                if (!has_sib && (rm & 7) == 5) {
                    if (offset + 4 > max_len) return false;
                    int32_t d32 = 0;
                    std::memcpy(&d32, &code[offset], 4);
                    offset += 4;
                    res.target_mem_address = rip + offset + d32;
                    std::ostringstream ss;
                    ss << ptr_size << "[rip " << (d32 >= 0 ? "+ " : "- ") << "0x" << std::hex << std::abs(d32) << "]";
                    out_rm = ss.str();
                    return true;
                }
                if (has_sib && (base & 7) == 5) {
                    if (offset + 4 > max_len) return false;
                    int32_t d32 = 0;
                    std::memcpy(&d32, &code[offset], 4);
                    offset += 4;
                    disp = d32;
                }
            } else if (mod == 1) {
                if (offset >= max_len) return false;
                int8_t d8 = static_cast<int8_t>(code[offset++]);
                disp = d8;
            } else if (mod == 2) {
                if (offset + 4 > max_len) return false;
                int32_t d32 = 0;
                std::memcpy(&d32, &code[offset], 4);
                offset += 4;
                disp = d32;
            }

            std::ostringstream mem_ss;
            mem_ss << ptr_size << "[";
            bool has_base = !(mod == 0 && has_sib && (base & 7) == 5);
            if (has_base) {
                mem_ss << (has_sib ? r64[base] : r64[rm]);
            }
            if (has_sib && has_index) {
                if (has_base) mem_ss << " + ";
                mem_ss << r64[idx];
                if (scale > 1) mem_ss << "*" << (int)scale;
            }
            if (disp != 0 || !has_base) {
                if (disp >= 0) {
                    if (has_base || (has_sib && has_index)) mem_ss << " + ";
                    mem_ss << "0x" << std::hex << disp;
                } else {
                    mem_ss << " - 0x" << std::hex << -disp;
                }
            }
            mem_ss << "]";
            out_rm = mem_ss.str();
            return true;
        };

        if (op >= 0x50 && op <= 0x57) {
            uint8_t r = (op - 0x50) | (rex_b ? 8 : 0);
            res.text = "push " + std::string(r64[r]);
        } else if (op >= 0x58 && op <= 0x5F) {
            uint8_t r = (op - 0x58) | (rex_b ? 8 : 0);
            res.text = "pop " + std::string(r64[r]);
        } else if (op == 0xC3) {
            res.text = "ret";
        } else if (op == 0x90) {
            res.text = "nop";
        } else if (op == 0xE8) {
            if (offset + 4 <= max_len) {
                int32_t rel = 0;
                std::memcpy(&rel, &code[offset], 4);
                offset += 4;
                uint64_t target = rip + offset + rel;
                std::ostringstream ss;
                ss << "call 0x" << std::hex << target;
                res.text = ss.str();
            }
        } else if (op == 0xE9) {
            if (offset + 4 <= max_len) {
                int32_t rel = 0;
                std::memcpy(&rel, &code[offset], 4);
                offset += 4;
                uint64_t target = rip + offset + rel;
                std::ostringstream ss;
                ss << "jmp 0x" << std::hex << target;
                res.text = ss.str();
            }
        } else if (op == 0xEB) {
            if (offset < max_len) {
                int8_t rel = static_cast<int8_t>(code[offset++]);
                uint64_t target = rip + offset + rel;
                std::ostringstream ss;
                ss << "jmp 0x" << std::hex << target;
                res.text = ss.str();
            }
        } else if (op >= 0x70 && op <= 0x7F) {
            static const char* jcc[] = { "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja",
                                         "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg" };
            if (offset < max_len) {
                int8_t rel = static_cast<int8_t>(code[offset++]);
                uint64_t target = rip + offset + rel;
                std::ostringstream ss;
                ss << jcc[op - 0x70] << " 0x" << std::hex << target;
                res.text = ss.str();
            }
        } else if (op == 0x89) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "mov " + rm_str + ", " + reg_str;
            }
        } else if (op == 0x8B) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "mov " + reg_str + ", " + rm_str;
            }
        } else if (op == 0x8D) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "lea " + reg_str + ", " + rm_str;
            }
        } else if (op == 0x31) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "xor " + rm_str + ", " + reg_str;
            }
        } else if (op == 0x33) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "xor " + reg_str + ", " + rm_str;
            }
        } else if (op == 0x85) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                res.text = "test " + rm_str + ", " + reg_str;
            }
        } else if (op == 0x83) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                if (offset < max_len) {
                    int8_t imm = static_cast<int8_t>(code[offset++]);
                    static const char* mnem[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
                    std::ostringstream ss;
                    ss << mnem[r_id & 7] << " " << rm_str << ", 0x" << std::hex << (int)(uint8_t)imm;
                    res.text = ss.str();
                }
            }
        } else if (op == 0x81) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                if (offset + 4 <= max_len) {
                    uint32_t imm = 0;
                    std::memcpy(&imm, &code[offset], 4);
                    offset += 4;
                    static const char* mnem[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
                    std::ostringstream ss;
                    ss << mnem[r_id & 7] << " " << rm_str << ", 0x" << std::hex << imm;
                    res.text = ss.str();
                }
            }
        } else if (op == 0x80) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(false, false, true, rm_str, reg_str, r_id, rm_id)) {
                if (offset < max_len) {
                    uint8_t imm = code[offset++];
                    static const char* mnem[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
                    std::ostringstream ss;
                    ss << mnem[r_id & 7] << " " << rm_str << ", 0x" << std::hex << (int)imm;
                    res.text = ss.str();
                }
            }
        } else if (op == 0xC7) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                if (offset + 4 <= max_len) {
                    uint32_t imm = 0;
                    std::memcpy(&imm, &code[offset], 4);
                    offset += 4;
                    std::ostringstream ss;
                    ss << "mov " << rm_str << ", 0x" << std::hex << imm;
                    res.text = ss.str();
                }
            }
        } else if (op >= 0xB8 && op <= 0xBF) {
            uint8_t r = (op - 0xB8) | (rex_b ? 8 : 0);
            if (rex_w) {
                if (offset + 8 <= max_len) {
                    uint64_t imm = 0;
                    std::memcpy(&imm, &code[offset], 8);
                    offset += 8;
                    std::ostringstream ss;
                    ss << "mov " << r64[r] << ", 0x" << std::hex << imm;
                    res.text = ss.str();
                }
            } else {
                if (offset + 4 <= max_len) {
                    uint32_t imm = 0;
                    std::memcpy(&imm, &code[offset], 4);
                    offset += 4;
                    std::ostringstream ss;
                    ss << "mov " << r32[r] << ", 0x" << std::hex << imm;
                    res.text = ss.str();
                }
            }
        } else if (op == 0xFF) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
                uint8_t subop = r_id & 7;
                if (subop == 0) res.text = "inc " + rm_str;
                else if (subop == 1) res.text = "dec " + rm_str;
                else if (subop == 2) res.text = "call " + rm_str;
                else if (subop == 4) res.text = "jmp " + rm_str;
                else if (subop == 6) res.text = "push " + rm_str;
                else res.text = "grp5_" + std::to_string(subop) + " " + rm_str;
            }
        } else if (op == 0x0F) {
            if (offset < max_len) {
                uint8_t op2 = code[offset++];
                if (op2 >= 0x80 && op2 <= 0x8F) {
                    static const char* jcc[] = { "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja",
                                                 "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg" };
                    if (offset + 4 <= max_len) {
                        int32_t rel = 0;
                        std::memcpy(&rel, &code[offset], 4);
                        offset += 4;
                        uint64_t target = rip + offset + rel;
                        std::ostringstream ss;
                        ss << jcc[op2 - 0x80] << " 0x" << std::hex << target;
                        res.text = ss.str();
                    }
                } else if (op2 == 0x10 || op2 == 0x11) {
                    std::string rm_str, reg_str;
                    uint8_t r_id = 0, rm_id = 0;
                    if (decode_modrm(rex_w, false, false, rm_str, reg_str, r_id, rm_id)) {
                        std::string mnem = has_f2 ? "movsd" : (has_f3 ? "movss" : "movups");
                        if (op2 == 0x10) res.text = mnem + " " + std::string(xmm[r_id & 0xF]) + ", " + rm_str;
                        else res.text = mnem + " " + rm_str + ", " + std::string(xmm[r_id & 0xF]);
                    }
                } else if (op2 == 0xB6 || op2 == 0xB7) {
                    std::string rm_str, reg_str;
                    uint8_t r_id = 0, rm_id = 0;
                    if (decode_modrm(false, op2 == 0xB7, op2 == 0xB6, rm_str, reg_str, r_id, rm_id)) {
                        res.text = "movzx " + std::string(r32[r_id & 0xF]) + ", " + rm_str;
                    }
                }
            }
        }

        if (res.text.empty()) {
            std::ostringstream ss;
            ss << "db 0x" << std::hex << (int)op;
            res.text = ss.str();
        }

        res.length = std::max<size_t>(1, offset);
        return res;
    }

private:
    static std::string to_hex(uint8_t val) {
        std::ostringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)val;
        return ss.str();
    }
};

// ============================================================================
// 2. PE/COFF EXECUTABLE AND OBJECT AUDITOR
// ============================================================================
class PeInspector {
public:
    static bool audit(const fs::path& path, std::ostream& out = std::cout) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            out << "[dgtl-pe] Error: Unable to open file: " << path.string() << "\n";
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(file_size);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);

        if (file_size < 0x40) {
            out << "[dgtl-pe] Error: File too small to contain DOS header (" << file_size << " bytes)\n";
            return false;
        }

        out << "================================================================================\n";
        out << "            DGTL SOVEREIGN PE/COFF EXECUTABLE STRUCTURAL AUDITOR                \n";
        out << "================================================================================\n";
        out << "Target Binary: " << path.string() << " (" << file_size << " bytes / " << (file_size / 1024.0) << " KB)\n\n";

        // DOS Header
        uint16_t dos_magic = 0;
        std::memcpy(&dos_magic, buffer.data(), 2);
        if (dos_magic != 0x5A4D) {
            out << "[dgtl-pe] [FAIL] Invalid DOS Header magic (expected 0x5A4D 'MZ', got 0x" << std::hex << dos_magic << std::dec << ")\n";
            return false;
        }
        out << "[dgtl-pe] [PASS] DOS Signature: 'MZ' (0x5A4D)\n";

        uint32_t pe_offset = 0;
        std::memcpy(&pe_offset, buffer.data() + 0x3C, 4);
        if (pe_offset + 24 > file_size) {
            out << "[dgtl-pe] [FAIL] Invalid e_lfanew offset (0x" << std::hex << pe_offset << std::dec << ")\n";
            return false;
        }

        uint32_t pe_sig = 0;
        std::memcpy(&pe_sig, buffer.data() + pe_offset, 4);
        if (pe_sig != 0x00004550) {
            out << "[dgtl-pe] [FAIL] Invalid PE Signature (expected 'PE\\0\\0', got 0x" << std::hex << pe_sig << std::dec << ")\n";
            return false;
        }
        out << "[dgtl-pe] [PASS] PE Signature: 'PE\\0\\0' at offset 0x" << std::hex << pe_offset << std::dec << "\n";

        // COFF File Header
        const uint8_t* coff_hdr = buffer.data() + pe_offset + 4;
        uint16_t machine = 0, num_sections = 0, opt_hdr_size = 0, characteristics = 0;
        uint32_t timestamp = 0;
        std::memcpy(&machine, coff_hdr, 2);
        std::memcpy(&num_sections, coff_hdr + 2, 2);
        std::memcpy(&timestamp, coff_hdr + 4, 4);
        std::memcpy(&opt_hdr_size, coff_hdr + 16, 2);
        std::memcpy(&characteristics, coff_hdr + 18, 2);

        out << "[dgtl-pe] Architecture: " << (machine == 0x8664 ? "x86_64 (AMD64)" : (machine == 0x14C ? "x86 (i386)" : "Unknown 0x" + std::to_string(machine)))
            << " | Sections: " << num_sections << " | OptHdrSize: " << opt_hdr_size << " bytes\n";

        if (machine != 0x8664) {
            out << "[dgtl-pe] [WARN] Expected x86_64 machine type\n";
        }

        if (opt_hdr_size < 112) {
            out << "[dgtl-pe] [FAIL] Optional Header size too small for PE32+ (" << opt_hdr_size << " bytes)\n";
            return false;
        }

        // Optional Header
        const uint8_t* opt_hdr = coff_hdr + 20;
        uint16_t opt_magic = 0;
        std::memcpy(&opt_magic, opt_hdr, 2);
        if (opt_magic != 0x20B) {
            out << "[dgtl-pe] [FAIL] Not PE32+ 64-bit binary (magic=0x" << std::hex << opt_magic << std::dec << ")\n";
            return false;
        }

        uint32_t entry_rva = 0, section_align = 0, file_align = 0, image_size = 0, headers_size = 0;
        uint64_t image_base = 0;
        uint16_t subsystem = 0;

        std::memcpy(&entry_rva, opt_hdr + 16, 4);
        std::memcpy(&image_base, opt_hdr + 24, 8);
        std::memcpy(&section_align, opt_hdr + 32, 4);
        std::memcpy(&file_align, opt_hdr + 36, 4);
        std::memcpy(&image_size, opt_hdr + 56, 4);
        std::memcpy(&headers_size, opt_hdr + 60, 4);
        std::memcpy(&subsystem, opt_hdr + 68, 2);

        out << "--------------------------------------------------------------------------------\n";
        out << "OPTIONAL HEADER (PE32+):\n";
        out << "  ImageBase          : 0x" << std::hex << image_base << "\n";
        out << "  AddressOfEntryPoint: 0x" << entry_rva << " (VA: 0x" << (image_base + entry_rva) << ")\n";
        out << "  SizeOfImage        : 0x" << image_size << " (" << std::dec << image_size << " bytes)\n";
        out << "  SizeOfHeaders      : 0x" << std::hex << headers_size << "\n";
        out << "  SectionAlignment   : 0x" << section_align << "\n";
        out << "  FileAlignment      : 0x" << file_align << "\n";
        out << "  Subsystem          : " << (subsystem == 3 ? "IMAGE_SUBSYSTEM_WINDOWS_CUI (Console)" : (subsystem == 2 ? "IMAGE_SUBSYSTEM_WINDOWS_GUI" : std::to_string(subsystem))) << std::dec << "\n";

        // Section Headers
        const uint8_t* sec_hdr_start = opt_hdr + opt_hdr_size;
        out << "--------------------------------------------------------------------------------\n";
        out << "SECTION TABLE AUDIT (" << num_sections << " sections):\n";
        out << "Idx Name          VirtSize  VirtAddr  RawSize   RawOff    Flags       Status\n";

        struct SectionView {
            std::string name;
            uint32_t va{0};
            uint32_t vsz{0};
            uint32_t rsz{0};
            uint32_t roff{0};
            uint32_t flags{0};
            bool is_code{false};
            bool is_writable{false};
            bool is_bss{false};
        };
        std::vector<SectionView> sections;
        bool has_critical_error = false;
        bool entry_point_found = false;

        for (uint16_t i = 0; i < num_sections; ++i) {
            const uint8_t* sh = sec_hdr_start + (i * 40);
            if (sh + 40 > buffer.data() + file_size) break;

            char name_buf[9] = {0};
            std::memcpy(name_buf, sh, 8);
            std::string sec_name(name_buf);

            uint32_t vsz = 0, va = 0, rsz = 0, roff = 0, flags = 0;
            std::memcpy(&vsz, sh + 8, 4);
            std::memcpy(&va, sh + 12, 4);
            std::memcpy(&rsz, sh + 16, 4);
            std::memcpy(&roff, sh + 20, 4);
            std::memcpy(&flags, sh + 36, 4);

            bool is_code = (flags & 0x00000020) != 0;
            bool is_writable = (flags & 0x80000000) != 0;
            bool is_bss = sec_name == ".bss" || (flags & 0x00000080) != 0;

            sections.push_back({sec_name, va, vsz, rsz, roff, flags, is_code, is_writable, is_bss});

            std::string status = "OK";
            if (vsz == 0) {
                status = "[CRITICAL] Zero VirtualSize!";
                has_critical_error = true;
            } else if (va + vsz > image_size) {
                status = "[CRITICAL] Out-of-bounds (exceeds SizeOfImage)!";
                has_critical_error = true;
            }

            if (entry_rva >= va && entry_rva < va + vsz) {
                entry_point_found = true;
                if (!is_code) {
                    status += " [CRITICAL] Entry point in non-code section!";
                    has_critical_error = true;
                }
            }

            out << std::right << std::setw(3) << i << " "
                << std::left << std::setw(13) << sec_name << " "
                << std::right << std::hex << std::setw(8) << std::setfill('0') << vsz << "  "
                << std::setw(8) << std::setfill('0') << va << "  "
                << std::setw(8) << std::setfill('0') << rsz << "  "
                << std::setw(8) << std::setfill('0') << roff << "  "
                << "0x" << std::setw(8) << std::setfill('0') << flags << "  "
                << status << "\n" << std::dec;
        }

        if (!entry_point_found) {
            out << "[dgtl-pe] [CRITICAL ERROR] Entry point RVA 0x" << std::hex << entry_rva << std::dec << " does NOT fall into any section!\n";
            has_critical_error = true;
        }

        // Import Directory Table (DataDirectory[1])
        uint32_t import_rva = 0, import_size = 0;
        if (opt_hdr_size >= 128) {
            std::memcpy(&import_rva, opt_hdr + 120, 4);
            std::memcpy(&import_size, opt_hdr + 124, 4);
        }

        out << "--------------------------------------------------------------------------------\n";
        out << "IMPORT DIRECTORY AUDIT (RVA 0x" << std::hex << import_rva << ", 0x" << import_size << " bytes):\n" << std::dec;

        auto rva_to_offset = [&](uint32_t rva) -> size_t {
            for (const auto& s : sections) {
                uint32_t limit = (s.vsz > 0) ? s.vsz : s.rsz;
                if (rva >= s.va && rva < s.va + limit) {
                    return s.roff + (rva - s.va);
                }
            }
            return 0;
        };

        if (import_rva != 0 && import_rva < image_size) {
            size_t idt_off = rva_to_offset(import_rva);
            if (idt_off != 0 && idt_off < file_size) {
                size_t curr = idt_off;
                size_t dll_count = 0;
                while (curr + 20 <= file_size) {
                    uint32_t ilt_rva = 0, name_rva = 0, iat_rva = 0;
                    std::memcpy(&ilt_rva, buffer.data() + curr, 4);
                    std::memcpy(&name_rva, buffer.data() + curr + 12, 4);
                    std::memcpy(&iat_rva, buffer.data() + curr + 16, 4);
                    if (ilt_rva == 0 && name_rva == 0 && iat_rva == 0) break;

                    std::string dll_name = "unknown";
                    size_t name_off = rva_to_offset(name_rva);
                    if (name_off != 0 && name_off < file_size) {
                        dll_name = reinterpret_cast<const char*>(buffer.data() + name_off);
                    }

                    out << "  [DLL " << ++dll_count << "] " << dll_name << " (IAT RVA: 0x" << std::hex << iat_rva << ", ILT RVA: 0x" << ilt_rva << ")\n" << std::dec;

                    uint32_t thunk_rva = (ilt_rva != 0) ? ilt_rva : iat_rva;
                    size_t thunk_off = rva_to_offset(thunk_rva);
                    size_t fn_count = 0;
                    if (thunk_off != 0) {
                        while (thunk_off + 8 <= file_size) {
                            uint64_t val = 0;
                            std::memcpy(&val, buffer.data() + thunk_off, 8);
                            if (val == 0) break;
                            std::string fn_name;
                            if ((val & 0x8000000000000000ULL) != 0) {
                                fn_name = "Ordinal #" + std::to_string(val & 0xFFFF);
                            } else {
                                size_t ib_off = rva_to_offset(static_cast<uint32_t>(val));
                                if (ib_off != 0 && ib_off + 2 < file_size) {
                                    fn_name = reinterpret_cast<const char*>(buffer.data() + ib_off + 2);
                                }
                            }
                            if (++fn_count <= 8) {
                                out << "    - " << fn_name << "\n";
                            }
                            thunk_off += 8;
                        }
                        if (fn_count > 8) {
                            out << "    ... (" << (fn_count - 8) << " more imports)\n";
                        }
                    }
                    curr += 20;
                }
            }
        } else {
            out << "  [WARN] No Import Directory Table found.\n";
        }

        // Disassemble entry point
        out << "--------------------------------------------------------------------------------\n";
        out << "ENTRY POINT DISASSEMBLY (Starting at 0x" << std::hex << (image_base + entry_rva) << "):\n" << std::dec;
        size_t ep_off = rva_to_offset(entry_rva);
        if (ep_off != 0 && ep_off < file_size) {
            uint64_t cur_rip = image_base + entry_rva;
            size_t cur_off = ep_off;
            for (int i = 0; i < 20 && cur_off < file_size; ++i) {
                size_t rem = file_size - cur_off;
                DecodedInstruction instr = X64Disassembler::decode(buffer.data() + cur_off, rem, cur_rip);
                out << "  0x" << std::hex << std::setw(16) << std::setfill('0') << cur_rip << ": ";
                for (size_t b = 0; b < 6; ++b) {
                    if (b < instr.length) {
                        out << std::setw(2) << std::setfill('0') << (int)buffer[cur_off + b] << " ";
                    } else {
                        out << "   ";
                    }
                }
                out << "  " << instr.text << "\n" << std::dec;
                cur_off += instr.length;
                cur_rip += instr.length;
                if (instr.text == "ret") break;
            }
        } else {
            out << "  [FAIL] Cannot map entry point RVA to file offset!\n";
        }

        out << "================================================================================\n";
        out << "AUDIT RESULT: " << (has_critical_error ? "[FAIL] Binary has critical layout defects!" : "[PASS] Binary structure is valid.") << "\n";
        out << "================================================================================\n\n";

        return !has_critical_error;
    }
};

// ============================================================================
// 3. SOVEREIGN NATIVE PROCESS DEBUGGER
// ============================================================================
class NativeDebugger {
public:
    static int run(int argc, char** argv) {
        if (argc < 2) {
            std::cerr << "Usage: dgtl-debugger run <executable_path> [args...]\n";
            return 1;
        }

        fs::path exe_path = argv[1];
        if (!fs::exists(exe_path)) {
            std::cerr << "[dgtl-debugger] Error: Executable does not exist: " << exe_path.string() << "\n";
            return 1;
        }

        std::cout << "[dgtl-debugger] Pre-flight PE structural inspection of " << exe_path.filename().string() << "...\n";
        PeInspector::audit(exe_path);

#if defined(_WIN32) || defined(_WIN64)
        std::string cmdline = "\"" + exe_path.string() + "\"";
        for (int i = 2; i < argc; ++i) {
            cmdline += " \"";
            cmdline += argv[i];
            cmdline += "\"";
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::cout << "[dgtl-debugger] Launching under Win32 native debug interceptor: " << cmdline << "\n";
        std::cout << "================================================================================\n";

        BOOL created = CreateProcessA(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            FALSE,
            DEBUG_ONLY_THIS_PROCESS,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!created) {
            std::cerr << "[dgtl-debugger] Failed to launch child process. Win32 Error: " << GetLastError() << "\n";
            return 1;
        }

        DEBUG_EVENT dbg{};
        bool running = true;
        DWORD exit_code = 0;
        uint64_t image_base = 0;
        uint64_t entry_point = 0;

        struct DllInfo {
            std::string name;
            uint64_t base{0};
        };
        std::vector<DllInfo> loaded_dlls;

        while (running) {
            if (!WaitForDebugEvent(&dbg, INFINITE)) break;
            DWORD continue_status = DBG_CONTINUE;

            switch (dbg.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT: {
                image_base = reinterpret_cast<uint64_t>(dbg.u.CreateProcessInfo.lpBaseOfImage);
                entry_point = reinterpret_cast<uint64_t>(dbg.u.CreateProcessInfo.lpStartAddress);
                std::cout << "[dgtl-debugger] [EVENT] Process Created. ImageBase=0x" << std::hex << image_base
                          << ", EntryPoint=0x" << entry_point << std::dec << "\n";
                if (dbg.u.CreateProcessInfo.hFile) CloseHandle(dbg.u.CreateProcessInfo.hFile);
                break;
            }
            case LOAD_DLL_DEBUG_EVENT: {
                uint64_t dll_base = reinterpret_cast<uint64_t>(dbg.u.LoadDll.lpBaseOfDll);
                char dll_name_buf[MAX_PATH] = {0};
                if (dbg.u.LoadDll.hFile) {
                    GetFinalPathNameByHandleA(dbg.u.LoadDll.hFile, dll_name_buf, MAX_PATH, FILE_NAME_NORMALIZED);
                    CloseHandle(dbg.u.LoadDll.hFile);
                }
                std::string dname = dll_name_buf[0] ? fs::path(dll_name_buf).filename().string() : ("dll_at_0x" + std::to_string(dll_base));
                loaded_dlls.push_back({dname, dll_base});
                std::cout << "[dgtl-debugger] [EVENT] Loaded DLL: " << dname << " at 0x" << std::hex << dll_base << std::dec << "\n";
                break;
            }
            case EXCEPTION_DEBUG_EVENT: {
                const auto& rec = dbg.u.Exception.ExceptionRecord;
                DWORD code = rec.ExceptionCode;

                if (code == STATUS_BREAKPOINT) {
                    continue_status = DBG_CONTINUE;
                    break;
                }

                std::cout << "\n================================================================================\n";
                std::cout << "          DGTL NATIVE CRASH INTERCEPTOR: HARDWARE EXCEPTION TRAPPED             \n";
                std::cout << "================================================================================\n";

                std::string exc_name = "UNKNOWN_EXCEPTION";
                if (code == 0xC0000005) exc_name = "STATUS_ACCESS_VIOLATION (0xC0000005)";
                else if (code == 0xC0000139) exc_name = "STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139 - missing imported DLL symbol)";
                else if (code == 0xC000001D) exc_name = "STATUS_ILLEGAL_INSTRUCTION (0xC000001D)";
                else if (code == 0xC0000094) exc_name = "STATUS_INTEGER_DIVIDE_BY_ZERO (0xC0000094)";
                else if (code == 0xC00000FD) exc_name = "STATUS_STACK_OVERFLOW (0xC00000FD)";

                uint64_t fault_rip = reinterpret_cast<uint64_t>(rec.ExceptionAddress);
                std::cout << "[!] EXCEPTION CATEGORY : " << exc_name << " (0x" << std::hex << code << ")\n";
                std::cout << "[!] FAULTING RIP       : 0x" << fault_rip << "\n";

                if (code == 0xC0000005 && rec.NumberParameters >= 2) {
                    uint64_t access_type = rec.ExceptionInformation[0];
                    uint64_t fault_addr = rec.ExceptionInformation[1];
                    std::cout << "[!] ACCESS VIOLATION   : " << (access_type == 0 ? "READ" : (access_type == 1 ? "WRITE" : "EXECUTE (DEP)"))
                              << " of memory address 0x" << fault_addr << "\n";

                    MEMORY_BASIC_INFORMATION mbi{};
                    if (VirtualQueryEx(pi.hProcess, reinterpret_cast<LPCVOID>(fault_addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
                        const char* state_str = (mbi.State == MEM_COMMIT) ? "MEM_COMMIT" : ((mbi.State == MEM_RESERVE) ? "MEM_RESERVE" : "MEM_FREE (UNMAPPED!)");
                        std::cout << "[!] MEMORY PAGE STATUS : " << state_str << " | RegionSize=0x" << mbi.RegionSize << " | Protect=0x" << mbi.Protect << "\n";
                        if (fault_addr == 0 || fault_addr < 0x1000) {
                            std::cout << "\n[!] ROOT CAUSE DIAGNOSIS:\n";
                            std::cout << "    NULL Pointer Dereference / Unresolved Call (faulting address = 0x" << std::hex << fault_addr << std::dec << ")!\n";
                            std::cout << "    An unlinked function pointer or null relocation thunk was executed or dereferenced.\n";
                        } else if (mbi.State != MEM_COMMIT) {
                            std::cout << "\n[!] ROOT CAUSE DIAGNOSIS:\n";
                            std::cout << "    The process crashed because it attempted to access UNCOMMITTED/UNMAPPED virtual memory.\n";
                            std::cout << "    Target address 0x" << fault_addr << " is outside the valid allocated memory space of the executable!\n";
                        }
                    }
                }

                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, dbg.dwThreadId);
                if (hThread) {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_ALL;
                    if (GetThreadContext(hThread, &ctx)) {
                        std::cout << "--------------------------------------------------------------------------------\n";
                        std::cout << "CPU REGISTERS (x86_64):\n";
                        std::cout << "  RAX = 0x" << std::setw(16) << std::setfill('0') << ctx.Rax
                                  << "   RBX = 0x" << std::setw(16) << std::setfill('0') << ctx.Rbx
                                  << "   RCX = 0x" << std::setw(16) << std::setfill('0') << ctx.Rcx << "\n";
                        std::cout << "  RDX = 0x" << std::setw(16) << std::setfill('0') << ctx.Rdx
                                  << "   RSI = 0x" << std::setw(16) << std::setfill('0') << ctx.Rsi
                                  << "   RDI = 0x" << std::setw(16) << std::setfill('0') << ctx.Rdi << "\n";
                        std::cout << "  RBP = 0x" << std::setw(16) << std::setfill('0') << ctx.Rbp
                                  << "   RSP = 0x" << std::setw(16) << std::setfill('0') << ctx.Rsp
                                  << "   RIP = 0x" << std::setw(16) << std::setfill('0') << ctx.Rip << "\n";
                        std::cout << "  R8  = 0x" << std::setw(16) << std::setfill('0') << ctx.R8
                                  << "   R9  = 0x" << std::setw(16) << std::setfill('0') << ctx.R9
                                  << "   R10 = 0x" << std::setw(16) << std::setfill('0') << ctx.R10 << "\n";
                        std::cout << "  R11 = 0x" << std::setw(16) << std::setfill('0') << ctx.R11
                                  << "   R12 = 0x" << std::setw(16) << std::setfill('0') << ctx.R12
                                  << "   R13 = 0x" << std::setw(16) << std::setfill('0') << ctx.R13 << "\n";
                        std::cout << "  R14 = 0x" << std::setw(16) << std::setfill('0') << ctx.R14
                                  << "   R15 = 0x" << std::setw(16) << std::setfill('0') << ctx.R15
                                  << "   EFL = 0x" << std::setw(8) << std::setfill('0') << ctx.EFlags << "\n" << std::dec;

                        std::cout << "--------------------------------------------------------------------------------\n";
                        std::cout << "DISASSEMBLY AT FAULTING INSTRUCTION:\n";
                        uint8_t code_buf[64] = {0};
                        SIZE_T read_bytes = 0;
                        if (ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(ctx.Rip), code_buf, sizeof(code_buf), &read_bytes) && read_bytes > 0) {
                            uint64_t cur_rip = ctx.Rip;
                            size_t cur_off = 0;
                            for (int i = 0; i < 5 && cur_off < read_bytes; ++i) {
                                auto instr = X64Disassembler::decode(code_buf + cur_off, read_bytes - cur_off, cur_rip);
                                std::cout << (i == 0 ? "  --> 0x" : "      0x") << std::hex << std::setw(16) << std::setfill('0') << cur_rip << ": ";
                                for (size_t b = 0; b < 6; ++b) {
                                    if (b < instr.length) {
                                        std::cout << std::setw(2) << std::setfill('0') << (int)code_buf[cur_off + b] << " ";
                                    } else {
                                        std::cout << "   ";
                                    }
                                }
                                std::cout << "  " << instr.text << "\n" << std::dec;
                                cur_off += instr.length;
                                cur_rip += instr.length;
                            }
                        }

                        std::cout << "--------------------------------------------------------------------------------\n";
                        std::cout << "CALL STACK DUMP (RSP = 0x" << std::hex << ctx.Rsp << "):\n" << std::dec;
                        uint64_t stack_slots[16] = {0};
                        if (ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(ctx.Rsp), stack_slots, sizeof(stack_slots), &read_bytes)) {
                            size_t count = read_bytes / 8;
                            for (size_t i = 0; i < count; ++i) {
                                uint64_t val = stack_slots[i];
                                std::string owner = "unknown";
                                for (const auto& dll : loaded_dlls) {
                                    if (val >= dll.base && val < dll.base + 0x10000000) {
                                        owner = dll.name + "+0x" + std::to_string(val - dll.base);
                                        break;
                                    }
                                }
                                if (val >= image_base && val < image_base + 0x1000000) {
                                    owner = exe_path.filename().string() + "+0x" + std::to_string(val - image_base);
                                }
                                std::cout << "  [RSP+" << std::setw(2) << (i * 8) << "] 0x" << std::hex << std::setw(16) << std::setfill('0') << val
                                          << "  (" << owner << ")\n" << std::dec;
                            }
                        }
                    }
                    CloseHandle(hThread);
                }

                std::cout << "================================================================================\n\n";
                TerminateProcess(pi.hProcess, code);
                running = false;
                exit_code = code;
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT: {
                exit_code = dbg.u.ExitProcess.dwExitCode;
                running = false;
                std::cout << "\n[dgtl-debugger] Process Exited with Code: " << exit_code
                          << " (0x" << std::hex << exit_code << std::dec << ")\n";
                if (exit_code == 0) {
                    std::cout << "[dgtl-debugger] SUCCESS: Executable completed with exit code 0.\n";
                }
                break;
            }
            default:
                break;
            }

            ContinueDebugEvent(dbg.dwProcessId, dbg.dwThreadId, continue_status);
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(exit_code);
#else
        std::cerr << "[dgtl-debugger] Native execution debugger currently supports Windows host.\n";
        return 1;
#endif
    }
};

} // namespace dgtl::debug

int main(int argc, char** argv) {
    if (argc >= 2) {
        std::string_view subcmd = argv[1];
        if (subcmd == "run" || subcmd == "exec") {
            return dgtl::debug::NativeDebugger::run(argc - 1, argv + 1);
        }
        if (subcmd == "inspect-exe" || subcmd == "check-exe" || subcmd == "inspect") {
            if (argc < 3) {
                std::cerr << "Usage: dgtl-debugger inspect-exe <path_to_exe>\n";
                return 1;
            }
            return dgtl::debug::PeInspector::audit(argv[2]) ? 0 : 1;
        }
        if (subcmd.ends_with(".exe") && fs::exists(subcmd)) {
            return dgtl::debug::NativeDebugger::run(argc, argv);
        }
    }

    Options opts;
    const ParseStatus parse_status = parse_args(argc, argv, opts);
    if (parse_status == ParseStatus::Help) {
        return 0;
    }
    if (parse_status == ParseStatus::Error) {
        return 1;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(opts.root, ec);
    if (ec || !fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "Invalid root directory: " << opts.root.string() << "\n";
        return 1;
    }

    Analysis analysis = run_analysis(root, opts);

    if (!opts.focus.empty()) {
        const std::string focus = to_lower(opts.focus);
        analysis.cycles.erase(
            std::remove_if(analysis.cycles.begin(), analysis.cycles.end(), [&](const std::vector<const ModuleUnit*>& cycle) {
                return std::none_of(cycle.begin(), cycle.end(), [&](const ModuleUnit* unit) {
                    return to_lower(unit->module_name).find(focus) != std::string::npos ||
                           to_lower(unit->path.string()).find(focus) != std::string::npos;
                });
            }),
            analysis.cycles.end());
        analysis.clusters.erase(
            std::remove_if(analysis.clusters.begin(), analysis.clusters.end(), [&](const ErrorCluster& cluster) {
                return to_lower(cluster.signature).find(focus) == std::string::npos;
            }),
            analysis.clusters.end());
        analysis.collisions.erase(
            std::remove_if(analysis.collisions.begin(), analysis.collisions.end(), [&](const SymbolCollision& collision) {
                return to_lower(collision.symbol).find(focus) == std::string::npos;
            }),
            analysis.collisions.end());
        analysis.ownership_recommendations.erase(
            std::remove_if(analysis.ownership_recommendations.begin(), analysis.ownership_recommendations.end(), [&](const OwnershipRecommendation& rec) {
                return to_lower(rec.symbol).find(focus) == std::string::npos &&
                       to_lower(rec.trigger_message).find(focus) == std::string::npos;
            }),
            analysis.ownership_recommendations.end());
        analysis.cache_hints.erase(
            std::remove_if(analysis.cache_hints.begin(), analysis.cache_hints.end(), [&](const std::string& hint) {
                return to_lower(hint).find(focus) == std::string::npos;
            }),
            analysis.cache_hints.end());
        analysis.suspect_symbols.erase(
            std::remove_if(analysis.suspect_symbols.begin(), analysis.suspect_symbols.end(), [&](const std::string& symbol) {
                return to_lower(symbol).find(focus) == std::string::npos;
            }),
            analysis.suspect_symbols.end());
    }

    if (!opts.export_path.has_value()) {
        if (opts.json) {
            emit_json(analysis, root);
        } else {
            emit_text(analysis, root, opts);
        }
    }

    write_export_report(analysis, root, opts);
    return 0;
}