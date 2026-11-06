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

#include <cstdlib>
#include <chrono>
#include <span>

import dgtl.tools.common;

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
#include <io.h>
#define DGTL_ISATTY() (_isatty(_fileno(stdout)) != 0)
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#define DGTL_ISATTY() (isatty(fileno(stdout)) != 0)
#endif

// ============================================================================
// ANSI TERMINAL FORMATTER & VT PROCESSING
// ============================================================================
struct Terminal {
    static inline bool enabled = true;

    static void init(bool enable_color) {
        if (!enable_color || std::getenv("NO_COLOR") != nullptr) {
            enabled = false;
            return;
        }
        enabled = DGTL_ISATTY();
#if defined(_WIN32) || defined(_WIN64)
        if (enabled) {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut != INVALID_HANDLE_VALUE) {
                DWORD mode = 0;
                if (GetConsoleMode(hOut, &mode)) {
                    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                    SetConsoleMode(hOut, mode);
                }
            }
        }
#endif
    }

    static const char* reset()      { return enabled ? "\033[0m" : ""; }
    static const char* bold()       { return enabled ? "\033[1m" : ""; }
    static const char* dim()        { return enabled ? "\033[2m" : ""; }
    static const char* red()        { return enabled ? "\033[1;31m" : ""; }
    static const char* green()      { return enabled ? "\033[1;32m" : ""; }
    static const char* yellow()     { return enabled ? "\033[1;33m" : ""; }
    static const char* blue()       { return enabled ? "\033[1;34m" : ""; }
    static const char* magenta()    { return enabled ? "\033[1;35m" : ""; }
    static const char* cyan()       { return enabled ? "\033[1;36m" : ""; }
    static const char* white()      { return enabled ? "\033[1;37m" : ""; }
    static const char* bg_red()     { return enabled ? "\033[41;1;37m" : ""; }
    static const char* bg_green()   { return enabled ? "\033[42;1;30m" : ""; }
    static const char* bg_blue()    { return enabled ? "\033[44;1;37m" : ""; }
    static const char* bg_dark()    { return enabled ? "\033[48;5;236m" : ""; }
};

struct DiagnosticTaxonomy {
    std::string code;
    std::string category;
    std::string title;
    std::string explanation;
    std::vector<std::string> suggested_fixes;
};

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
    bool color{true};
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
    int column{0};
    std::string kind{"error"};
    std::string message;
    std::string raw;
    std::string command_context;
    std::string error_code;
    std::string category;
    std::string title;
    std::vector<std::string> remediation_steps;
};

struct ErrorCluster {
    std::string signature;
    std::size_t count{0};
    std::vector<ErrorRecord> samples;
    std::map<std::string, std::size_t> command_hits;
    std::string likely_command;
    std::size_t likely_command_hits{0};
    std::string error_code;
    std::string category;
    std::string title;
    std::string explanation;
    std::vector<std::string> remediation_steps;
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
        } else if (arg == "--no-color") {
            opts.color = false;
        } else if (arg == "--color") {
            opts.color = true;
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
                << "NATIVE EXECUTION & FULL CRASH TELEMETRY:\n"
                << "  dgtl-debugger run <executable.exe> [args...]\n"
                << "      Launch target under Win32 native debug interceptor with comprehensive telemetry.\n"
                << "      Captures hardware exceptions (0xC0000005, 0xC000001D, 0xC0000094, etc.),\n"
                << "      performs unmapped memory page diagnosis (VirtualQueryEx), dumps 64-bit GPRs,\n"
                << "      segment registers (CS/DS/ES/FS/GS/SS), full EFLAGS & MXCSR flag decomposition,\n"
                << "      128-bit SIMD registers (XMM0-XMM15 with float/double interpretation),\n"
                << "      faulting RIP disassembly, 32-frame stack backtrace, and auto-exports\n"
                << "      structured telemetry JSON (crash_telemetry_<pid>_<timestamp>.json).\n\n"
                << "UNIVERSAL FILE & COMPILER ARTIFACT INSPECTOR:\n"
                << "  dgtl-debugger inspect <file>   (aliases: open, view)\n"
                << "      Deep forensic decoding and structured visualization of any compiler artifact:\n"
                << "      - Static Archives: .a, .lib (archive member catalog, sizes, offsets)\n"
                << "      - Relocatable Objects: .obj, .o (COFF/ELF/Mach-O sections, symbols, relocs)\n"
                << "      - Dynamic Libraries: .so, .dll (exports, imports, section headers)\n"
                << "      - Raw Section Blobs: .text (disassembly), .data/.rdata (hex+ASCII dump),\n"
                << "                           .pdata (RUNTIME_FUNCTION unwind array),\n"
                << "                           .xdata (UNWIND_INFO opcode stream), .bss\n"
                << "      - Assembly Files: .s, .asm (syntax analysis, instruction listings)\n"
                << "      - DGTL IR & Polyhedral: .dgtl (polyhedral iteration domains, schedules, tiles)\n"
                << "      - Myzon Package Slabs: .mzp (precompiled binary slab, hash, string table, symbols)\n"
                << "      - Myzon Modules & Sources: .myz, .mzi, .mzm, .my, .myzt, .myzb, .myzs, .mzc, .myd\n"
                << "      - Project Manifests: myzon.toml, myzon.lock, dgtl.toml, dgtl.lock\n\n"
                << "PE STRUCTURAL AUDIT & SECTION FORENSICS:\n"
                << "  dgtl-debugger inspect-exe <executable.exe>\n"
                << "      Deep structural validation of PE32+ binaries: header checks, section\n"
                << "      alignments, zero-size BSS detection, out-of-bounds RVAs, section overlaps,\n"
                << "      Import Directory Table (IAT/ILT) integrity, and entry point disassembly.\n\n"
                << "STATIC LOG, CYCLE, & COLLISION ANALYSIS:\n"
                << "  dgtl-debugger [--root <path>] [--log <path>]... [--export <path>] [--focus <text>]\n"
                << "      Scans C++20 module graphs, detects circular dependencies, tracks symbol\n"
                << "      collisions, clusters build errors, and exports JSON / Markdown diagnostics.\n\n"
                << "DIAGNOSTIC DISPLAY OPTIONS:\n"
                << "  --color / --no-color       Enable or disable ANSI color styling in console diagnostics.\n"
                << "  --json                     Output structured JSON diagnostics for IDE integration.\n"
                << "  --summary-only             Emit condensed diagnostic summary without full snippets.\n"
                << "  --top <N>                  Limit error clusters and collision reports to top N entries.\n\n"
                << "DIRECT SHORTCUTS:\n"
                << "  dgtl-debugger <file>       -> Auto-detects file type: executes .exe/.elf with native\n"
                << "                                debugger; opens any other file format (.a, .obj, .dgtl,\n"
                << "                                .s, .pdata, .myz, .mzp, etc.) in FileInspector.\n"
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

// ============================================================================
// DIAGNOSTIC TAXONOMY & REMEDIATION ENGINE
// ============================================================================
[[nodiscard]] DiagnosticTaxonomy classify_diagnostic(std::string_view message_text, std::string_view raw_text) {
    std::string combined = to_lower(std::string(message_text) + " " + std::string(raw_text));

    if (combined.find("cycle") != std::string::npos || combined.find("scc") != std::string::npos || combined.find("circular") != std::string::npos) {
        return {
            "MOD-001",
            "Module Graph Topology",
            "Cyclic Module Import Dependency",
            "Translation units form a strongly connected component (SCC) cycle. C++20 modules strictly prohibit circular interface imports.",
            {
                "Extract common interface types and declarations into a shared base partition or header.",
                "Replace direct type imports with forward-declared pointers, references, or opaque scalar handles.",
                "Ensure interface units (.ixx) only import necessary dependencies, moving implementation imports to .cpp units."
            }
        };
    }

    if (combined.find("cannot import module in its own purview") != std::string::npos || combined.find("purview") != std::string::npos) {
        return {
            "MOD-002",
            "C++20 Module Purview",
            "Self-Import in Module Purview",
            "A module implementation unit attempted to import its owning module interface, which is redundant and disallowed by the C++20 standard.",
            {
                "Remove 'import <module_name>;' from the module implementation file.",
                "Purview units automatically inherit all declarations exported by their owning module's interface."
            }
        };
    }

    if (combined.find("crc mismatch") != std::string::npos || combined.find("out of date") != std::string::npos || combined.find("gcm.cache") != std::string::npos) {
        return {
            "CACHE-001",
            "Module Cache Synchronization",
            "Stale Binary Module Interface (BMI) Cache",
            "Precompiled module interface file CRC does not match compiler signature or dependent header timestamp.",
            {
                "Delete stale module cache directory (e.g. 'build/gcm.cache' or '.dot_dgtl/cache').",
                "Perform a clean build of all module interfaces in topological order using 'dgtl-engine --rebuild-cache'."
            }
        };
    }

    if (combined.find("failed to find module") != std::string::npos || combined.find("module file not found") != std::string::npos || combined.find("could not open module") != std::string::npos) {
        return {
            "MOD-003",
            "Module Resolution",
            "Unresolved Module Interface",
            "The compiler was unable to locate the compiled BMI (.gcm / .ifc) for the imported module.",
            {
                "Verify the module name spelling matches the 'export module <name>;' declaration.",
                "Ensure the module interface source file is listed in the compilation build sequence prior to consumer units.",
                "Check module search path flags (-fmodule-file or -fprebuilt-module-path)."
            }
        };
    }

    if (combined.find("ambiguous") != std::string::npos || combined.find("candidate") != std::string::npos) {
        return {
            "SYM-001",
            "Symbol Resolution",
            "Ambiguous Symbol or Overload Resolution",
            "Multiple candidate declarations or definitions match the referenced identifier across imported modules.",
            {
                "Fully qualify the symbol name with its explicit enclosing namespace (e.g. 'dgtl::ir::...').",
                "Eliminate duplicate declarations across headers and module interface partitions.",
                "Consult the Ownership Arbitration section of this report to identify the authoritative candidate."
            }
        };
    }

    if (combined.find("was not declared") != std::string::npos || combined.find("undeclared") != std::string::npos || combined.find("undefined symbol") != std::string::npos || combined.find("not found") != std::string::npos) {
        return {
            "SYM-002",
            "Semantic Analysis",
            "Undeclared Identifier or Symbol",
            "The compiler encountered an identifier that has not been declared in current lexical or module scope.",
            {
                "Ensure the required header or module exporting this symbol is imported at the top of the file.",
                "Verify that the symbol is exported with 'export' keyword in the providing module interface unit.",
                "If the symbol is generated by a tooling pass (e.g. 'dgtl-gen'), verify that generation ran successfully."
            }
        };
    }

    if (combined.find("redefinition") != std::string::npos || combined.find("conflicting declaration") != std::string::npos || combined.find("multiple definition") != std::string::npos) {
        return {
            "SYM-003",
            "One Definition Rule (ODR)",
            "Symbol Redefinition Conflict",
            "The identifier has multiple non-inline definitions across different compilation units or headers.",
            {
                "Mark free functions or variable definitions in headers as 'inline' or 'constexpr'.",
                "Move non-inline function definitions to a single .cpp implementation unit."
            }
        };
    }

    if (combined.find("w^x") != std::string::npos || combined.find("writable & executable") != std::string::npos) {
        return {
            "SEC-001",
            "Binary Hardening & Security",
            "W^X Memory Layout Violation",
            "Section is marked both Writable and Executable, violating memory protection security policies.",
            {
                "Separate mutable static state into '.data' or '.bss' and executable machine instructions into '.text'.",
                "Ensure linker section flags do not combine IMAGE_SCN_MEM_WRITE (0x80000000) and IMAGE_SCN_MEM_EXECUTE (0x00000020)."
            }
        };
    }

    if (combined.find("executable stack") != std::string::npos || combined.find("pt_gnu_stack") != std::string::npos) {
        return {
            "SEC-002",
            "ELF Security Hardening",
            "Executable Stack Flag Violation",
            "The ELF PT_GNU_STACK segment is marked executable, which exposes the runtime to buffer execution exploits.",
            {
                "Ensure all object files specify non-executable stack flags (flags = 0x6 RW-).",
                "Pass '-z noexecstack' to the linker or configure SovereignELFLinker stack header."
            }
        };
    }

    if (combined.find("entry point") != std::string::npos || combined.find("zero virtualsize") != std::string::npos || combined.find("out-of-bounds") != std::string::npos) {
        return {
            "LNK-001",
            "Executable Container Geometry",
            "Invalid Section Table or Entry Point RVA",
            "The executable container has malformed header offsets, section overlaps, or an entry point outside image bounds.",
            {
                "Verify AddressOfEntryPoint points within the VirtualAddress range of a valid executable section.",
                "Ensure section VirtualSize is positive and aligned to SectionAlignment."
            }
        };
    }

    if (combined.find("code signature") != std::string::npos || combined.find("lc_code_signature") != std::string::npos) {
        return {
            "MAC-001",
            "Mach-O Platform Hardening",
            "Missing Apple Silicon Ad-hoc Code Signature",
            "macOS on ARM64 requires an ad-hoc code signature in Mach-O executables via LC_CODE_SIGNATURE.",
            {
                "Link with 'dgtl-link --format=macho --arch=arm64', which synthesizes SHA-256 Code Directory structures."
            }
        };
    }

    if (combined.find("0xc0000005") != std::string::npos || combined.find("sigsegv") != std::string::npos || combined.find("access violation") != std::string::npos) {
        return {
            "RUN-001",
            "Virtual Memory Execution",
            "Hardware Memory Access Violation (Segmentation Fault)",
            "The process attempted an unauthorized read/write/execution access to virtual memory (frequently NULL address < 0x1000).",
            {
                "Inspect faulting RIP disassembly and CPU register state at crash point.",
                "Verify pointers and vtables are non-null before invoking member functions.",
                "Check that dynamic library imports are resolved in the Import Address Table (IAT)."
            }
        };
    }

    if (combined.find("0xc000001d") != std::string::npos || combined.find("sigill") != std::string::npos || combined.find("illegal instruction") != std::string::npos) {
        return {
            "CPU-001",
            "Instruction Architecture",
            "Illegal Host CPU Opcode",
            "The processor attempted to execute an opcode byte sequence not supported by host hardware.",
            {
                "Verify host architecture matches binary architecture (e.g. attempting to run ARM64 binary on x86_64 CPU).",
                "Check compiler SIMD code emission flags (e.g. AVX-512 vs AVX2)."
            }
        };
    }

    if (combined.find("0xc0000139") != std::string::npos) {
        return {
            "DLL-001",
            "Dynamic Loader",
            "Missing Dynamic Link Library Entrypoint",
            "The operating system loader located the required DLL, but the specified exported function symbol was missing.",
            {
                "Run 'dgtl-debugger inspect-exe' to audit the Import Directory Table and verify required function names.",
                "Ensure runtime DLL versions in PATH or application directory export the expected API."
            }
        };
    }

    if (combined.find("__dgtl_rt_") != std::string::npos || combined.find("special_functions") != std::string::npos ||
        combined.find("dgtl.math") != std::string::npos || combined.find("runtime_maths") != std::string::npos ||
        combined.find("math intrinsic") != std::string::npos || combined.find("ieee-754") != std::string::npos) {
        return {
            "MATH-001",
            "Sovereign Mathematics Runtime",
            "Unresolved Math Intrinsic or Runtime Symbol",
            "The compiler or linker encountered an unresolved sovereign math runtime symbol (__dgtl_rt_*), GenAI activation, or special function intrinsic.",
            {
                "Ensure 'dgtl.runtime.maths' and 'dgtl.utils.math.special_functions' are linked in the artifact or JIT resolver fallback table.",
                "Verify that BackendRuntimeContract maps the intrinsic to an inline expansion or canonical __dgtl_rt_* symbol.",
                "If using Public SDK, verify that <dgtl_sdk/math.h> is included and linked against dgtl.lib."
            }
        };
    }

    return {
        "DIAG-000",
        "Compiler Diagnostic",
        "Build Diagnostic",
        "A compiler or linker diagnostic was encountered during compilation.",
        {
            "Review the source line and diagnostic message for syntax or semantic issues."
        }
    };
}

// ============================================================================
// INDUSTRY-GRADE DIAGNOSTIC LINE PARSER
// ============================================================================
struct ParsedDiagnostic {
    fs::path file;
    int line{0};
    int column{0};
    std::string severity{"error"};
    std::string code;
    std::string message;
};

[[nodiscard]] std::optional<ParsedDiagnostic> parse_diagnostic_line(std::string_view raw) {
    if (raw.empty()) return std::nullopt;

    std::string line(raw);
    std::string lower = to_lower(line);

    static const std::vector<std::pair<std::string, std::string>> markers = {
        {"fatal error:", "fatal error"},
        {"error:", "error"},
        {"warning:", "warning"},
        {"note:", "note"},
        {"failed to compile", "error"}
    };

    size_t marker_pos = std::string::npos;
    std::string severity = "error";
    size_t marker_len = 0;

    for (const auto& [mk, sev] : markers) {
        size_t p = lower.find(mk);
        if (p != std::string::npos) {
            if (marker_pos == std::string::npos || p < marker_pos) {
                marker_pos = p;
                severity = sev;
                marker_len = mk.length();
            }
        }
    }

    if (marker_pos == std::string::npos) {
        return std::nullopt;
    }

    ParsedDiagnostic diag;
    diag.severity = severity;

    std::string after_marker = trim(line.substr(marker_pos + marker_len));

    if (after_marker.size() > 2 && after_marker.front() == '[') {
        size_t rb = after_marker.find(']');
        if (rb != std::string::npos) {
            diag.code = after_marker.substr(1, rb - 1);
            after_marker = trim(after_marker.substr(rb + 1));
        }
    } else if (after_marker.size() > 5 && after_marker[0] == 'C' && std::isdigit((unsigned char)after_marker[1])) {
        size_t col = after_marker.find(':');
        if (col != std::string::npos && col <= 6) {
            diag.code = after_marker.substr(0, col);
            after_marker = trim(after_marker.substr(col + 1));
        }
    }
    diag.message = after_marker;

    std::string prefix = trim(line.substr(0, marker_pos));
    while (!prefix.empty() && (prefix.back() == ':' || std::isspace((unsigned char)prefix.back()))) {
        prefix.pop_back();
    }

    if (prefix.empty()) {
        return diag;
    }

    // MSVC style: file(line,col) or file(line)
    if (prefix.back() == ')') {
        size_t lp = prefix.rfind('(');
        if (lp != std::string::npos) {
            diag.file = trim(prefix.substr(0, lp));
            std::string nums = prefix.substr(lp + 1, prefix.length() - lp - 2);
            size_t comma = nums.find(',');
            if (comma != std::string::npos) {
                std::string lstr = trim(nums.substr(0, comma));
                std::string cstr = trim(nums.substr(comma + 1));
                if (!lstr.empty() && std::all_of(lstr.begin(), lstr.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    diag.line = std::stoi(lstr);
                }
                if (!cstr.empty() && std::all_of(cstr.begin(), cstr.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    diag.column = std::stoi(cstr);
                }
            } else {
                if (!nums.empty() && std::all_of(nums.begin(), nums.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    diag.line = std::stoi(nums);
                }
            }
            return diag;
        }
    }

    // GCC / Clang style: file:line:col or file:line (mindful of Windows drive letter C:\)
    size_t search_start = 0;
    if (prefix.size() >= 3 && std::isalpha((unsigned char)prefix[0]) && prefix[1] == ':' && (prefix[2] == '\\' || prefix[2] == '/')) {
        search_start = 2;
    }

    size_t last_colon = prefix.rfind(':');
    if (last_colon != std::string::npos && last_colon > search_start) {
        std::string token1 = prefix.substr(last_colon + 1);
        if (!token1.empty() && std::all_of(token1.begin(), token1.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            size_t prev_colon = prefix.rfind(':', last_colon - 1);
            if (prev_colon != std::string::npos && prev_colon > search_start) {
                std::string token2 = prefix.substr(prev_colon + 1, last_colon - prev_colon - 1);
                if (!token2.empty() && std::all_of(token2.begin(), token2.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                    diag.file = prefix.substr(0, prev_colon);
                    diag.line = std::stoi(token2);
                    diag.column = std::stoi(token1);
                    return diag;
                }
            }
            diag.file = prefix.substr(0, last_colon);
            diag.line = std::stoi(token1);
            return diag;
        }
    }

    diag.file = prefix;
    return diag;
}

// ============================================================================
// RUST/CLANG-GRADE SOURCE CONTEXT SNIPPET RENDERER
// ============================================================================
void render_source_snippet(
    std::ostream& out,
    const fs::path& file,
    int error_line,
    int error_col = 0,
    std::string_view highlight_token = ""
) {
    if (file.empty() || error_line <= 0) return;
    std::error_code ec;
    if (!fs::exists(file, ec) || !fs::is_regular_file(file, ec)) return;

    const auto lines = read_lines(file);
    if (lines.empty() || error_line > static_cast<int>(lines.size())) return;

    int start_line = std::max(1, error_line - 2);
    int end_line = std::min(static_cast<int>(lines.size()), error_line + 2);
    int line_num_width = std::max<int>(3, static_cast<int>(std::to_string(end_line).length()));

    out << "  │     " << Terminal::cyan() << "--> " << Terminal::reset()
        << file.generic_string() << ":" << error_line;
    if (error_col > 0) out << ":" << error_col;
    out << "\n";

    out << "  │     " << std::string(line_num_width, ' ') << " " << Terminal::cyan() << "│" << Terminal::reset() << "\n";

    for (int l = start_line; l <= end_line; ++l) {
        const std::string& line_text = lines[l - 1];
        bool is_target = (l == error_line);

        out << "  │     ";
        if (is_target) {
            out << Terminal::red() << Terminal::bold()
                << std::right << std::setw(line_num_width) << l << " │ "
                << Terminal::reset() << line_text << "\n";

            out << "  │     " << std::string(line_num_width, ' ') << " "
                << Terminal::cyan() << "│ " << Terminal::reset();

            size_t caret_pos = 0;
            size_t caret_len = 1;

            if (error_col > 0 && error_col <= static_cast<int>(line_text.length()) + 1) {
                caret_pos = error_col - 1;
                size_t p = caret_pos;
                while (p < line_text.length() && (std::isalnum(static_cast<unsigned char>(line_text[p])) || line_text[p] == '_' || line_text[p] == ':')) {
                    ++p;
                }
                if (p > caret_pos) caret_len = p - caret_pos;
            } else if (!highlight_token.empty()) {
                size_t f = line_text.find(highlight_token);
                if (f != std::string::npos) {
                    caret_pos = f;
                    caret_len = highlight_token.length();
                } else {
                    size_t f2 = line_text.find_first_not_of(" \t");
                    caret_pos = (f2 != std::string::npos) ? f2 : 0;
                    caret_len = std::max<size_t>(1, line_text.length() > caret_pos ? line_text.length() - caret_pos : 1);
                }
            } else {
                size_t f2 = line_text.find_first_not_of(" \t");
                caret_pos = (f2 != std::string::npos) ? f2 : 0;
                caret_len = std::max<size_t>(1, line_text.length() > caret_pos ? line_text.length() - caret_pos : 1);
            }

            for (size_t c = 0; c < caret_pos && c < line_text.length(); ++c) {
                if (line_text[c] == '\t') out << "    ";
                else out << " ";
            }

            out << Terminal::red() << Terminal::bold();
            for (size_t k = 0; k < caret_len; ++k) out << "^";
            out << Terminal::reset() << "\n";
        } else {
            out << Terminal::dim()
                << std::right << std::setw(line_num_width) << l << " │ "
                << Terminal::reset() << line_text << "\n";
        }
    }

    out << "  │     " << std::string(line_num_width, ' ') << " " << Terminal::cyan() << "│" << Terminal::reset() << "\n";
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

        auto parsed = parse_diagnostic_line(line);
        ErrorRecord record;
        record.raw = line;
        record.command_context = last_compiler_command;

        if (parsed) {
            record.file = parsed->file;
            record.line = parsed->line;
            record.column = parsed->column;
            record.kind = parsed->severity;
            record.message = parsed->message;
            record.error_code = parsed->code;
        } else {
            record.kind = is_error ? "error" : "warning";
            record.message = trim(line);
        }

        auto tax = classify_diagnostic(record.message, record.raw);
        if (record.error_code.empty()) {
            record.error_code = tax.code;
        }
        record.category = tax.category;
        record.title = tax.title;
        record.remediation_steps = tax.suggested_fixes;

        extract_symbols_from_text(record.message, log_symbols);
        extract_symbols_from_text(record.raw, log_symbols);

        const std::string signature = normalize_error_signature(record.message);
        auto found = cluster_map.find(signature);
        std::size_t cluster_index = 0;
        if (found == cluster_map.end()) {
            ErrorCluster cl;
            cl.signature = signature;
            cl.count = 0;
            cl.error_code = record.error_code;
            cl.category = record.category;
            cl.title = record.title;
            cl.explanation = tax.explanation;
            cl.remediation_steps = tax.suggested_fixes;
            analysis.clusters.push_back(std::move(cl));
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
        if (!cluster.error_code.empty()) {
            std::cout << ", \"error_code\": \"" << json_escape(cluster.error_code) << "\"";
        }
        if (!cluster.category.empty()) {
            std::cout << ", \"category\": \"" << json_escape(cluster.category) << "\"";
        }
        if (!cluster.title.empty()) {
            std::cout << ", \"title\": \"" << json_escape(cluster.title) << "\"";
        }
        if (!cluster.explanation.empty()) {
            std::cout << ", \"explanation\": \"" << json_escape(cluster.explanation) << "\"";
        }
        if (!cluster.remediation_steps.empty()) {
            std::cout << ", \"remediation\": [";
            for (std::size_t r = 0; r < cluster.remediation_steps.size(); ++r) {
                if (r != 0) std::cout << ", ";
                std::cout << "\"" << json_escape(cluster.remediation_steps[r]) << "\"";
            }
            std::cout << "]";
        }
        if (!cluster.likely_command.empty()) {
            std::cout << ", \"likely_command\": \"" << json_escape(cluster.likely_command) << "\", \"likely_command_hits\": " << cluster.likely_command_hits;
        }
        if (!cluster.samples.empty()) {
            std::cout << ", \"samples\": [";
            for (std::size_t s = 0; s < cluster.samples.size(); ++s) {
                if (s != 0) std::cout << ", ";
                const auto& sample = cluster.samples[s];
                std::cout << "{\"file\": \"" << json_escape(sample.file.generic_string()) << "\""
                          << ", \"line\": " << sample.line
                          << ", \"column\": " << sample.column
                          << ", \"kind\": \"" << json_escape(sample.kind) << "\""
                          << ", \"message\": \"" << json_escape(sample.message) << "\"}";
            }
            std::cout << "]";
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
    if (analysis.clusters.empty() && (analysis.last_exit_code.empty() || analysis.last_exit_code == "0")) {
        out << "# DGTL Forensic Diagnostic Report\n\n";
        out << "> [!NOTE]\n";
        out << "> **Status: PASS** — Zero errors or structural defects detected across " << analysis.modules.size() << " module units.\n\n";
    } else {
        out << "# DGTL Incident Report\n\n";
        out << "> [!CAUTION]\n";
        out << "> **Build/Execution Incident Detected**: Target completed with exit code `"
            << (analysis.last_exit_code.empty() ? "non-zero" : analysis.last_exit_code) << "`.\n";
        if (!analysis.clusters.empty()) {
            out << "> Primary root cause signature: `" << analysis.clusters.front().signature << "`\n";
        }
        out << "\n";
    }

    out << "## Executive Summary\n\n";
    out << "| Metric | Value |\n";
    out << "|:---|:---|\n";
    out << "| **Workspace Root** | `" << root.generic_string() << "` |\n";
    if (!analysis.last_command.empty()) {
        out << "| **Last Command** | `" << analysis.last_command << "` |\n";
    }
    if (!analysis.last_exit_code.empty()) {
        out << "| **Exit Code** | `" << analysis.last_exit_code << "` |\n";
    }
    if (!analysis.compiler_fingerprint.empty()) {
        out << "| **Compiler Hash** | `" << analysis.compiler_fingerprint << "` |\n";
    }
    out << "| **Modules Discovered** | " << analysis.modules.size() << " |\n";
    out << "| **Circular SCC Cycles** | " << analysis.cycles.size() << " |\n";
    out << "| **Error Clusters** | " << analysis.clusters.size() << " |\n";
    out << "| **Symbol Collisions** | " << analysis.collisions.size() << " |\n";
    out << "| **Declaration Cache** | " << analysis.declaration_cache_hits << " hits, " << analysis.declaration_cache_misses << " misses |\n\n";

    if (!analysis.cycles.empty()) {
        out << "## Module Topology Cycles\n\n";
        out << "> [!WARNING]\n";
        out << "> C++20 module interface cycles violate the standard and halt compilation ordering.\n\n";
        out << "```mermaid\ngraph LR\n";
        for (std::size_t i = 0; i < std::min(opts.top, analysis.cycles.size()); ++i) {
            const auto& cycle = analysis.cycles[i];
            for (std::size_t j = 0; j < cycle.size(); ++j) {
                const auto* curr = cycle[j];
                const auto* next = cycle[(j + 1) % cycle.size()];
                out << "  " << curr->module_name << " --> " << next->module_name << "\n";
            }
        }
        out << "```\n\n";
    }

    out << "## Error Clusters & Diagnostics\n\n";
    if (analysis.clusters.empty()) {
        out << "- No errors detected.\n\n";
    } else {
        out << "| Code | Category | Count | Primary Signature | Remediation Focus |\n";
        out << "|:---|:---|---:|:---|:---|\n";
        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            const auto& c = analysis.clusters[i];
            std::string code = !c.error_code.empty() ? c.error_code : ("ERR-" + std::to_string(i + 1));
            std::string rem = !c.remediation_steps.empty() ? c.remediation_steps.front() : "Review compiler output";
            out << "| `" << code << "` | " << c.category << " | " << c.count << " | `" << c.signature << "` | " << rem << " |\n";
        }
        out << "\n";

        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            const auto& c = analysis.clusters[i];
            out << "### `" << (!c.error_code.empty() ? c.error_code : "ERR") << "`: " << c.signature << "\n\n";
            if (!c.explanation.empty()) {
                out << "**Diagnosis**: " << c.explanation << "\n\n";
            }
            if (!c.likely_command.empty()) {
                out << "- **Failing Command**: `" << c.likely_command << "` (" << c.likely_command_hits << " hits)\n";
            }
            if (!c.samples.empty()) {
                out << "- **Sample Sites**:\n";
                for (const auto& sample : c.samples) {
                    out << "  - `" << display_path(root, sample.file, opts.relative_paths);
                    if (sample.line > 0) out << ":" << sample.line;
                    if (sample.column > 0) out << ":" << sample.column;
                    out << "` — *" << sample.message << "*\n";
                }
            }
            if (!c.remediation_steps.empty()) {
                out << "\n> [!TIP]\n";
                out << "> **Actionable Remediation**:\n";
                for (const auto& step : c.remediation_steps) {
                    out << "> - " << step << "\n";
                }
            }
            out << "\n";
        }
    }

    out << "## Ownership Arbitration\n\n";
    if (analysis.ownership_recommendations.empty()) {
        out << "- No ambiguous ownership conflicts identified.\n\n";
    } else {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.ownership_recommendations.size()); ++i) {
            const auto& rec = analysis.ownership_recommendations[i];
            out << "#### Symbol `" << rec.symbol << "`\n\n";
            if (!rec.candidates.empty()) {
                const auto& winner = rec.candidates.front();
                out << "- **Authoritative Owner**: `" << display_path(root, winner.file, opts.relative_paths) << ":" << winner.line
                    << "` (Confidence Score: " << winner.score << ")\n";
                if (!winner.snippet_lines.empty()) {
                    out << "\n```cpp\n";
                    for (std::size_t idx = 0; idx < winner.snippet_lines.size(); ++idx) {
                        out << (winner.snippet_start_line + static_cast<int>(idx)) << ": " << winner.snippet_lines[idx] << "\n";
                    }
                    out << "```\n\n";
                }
            }
        }
    }

    if (!analysis.regression_notes.empty()) {
        out << "## Regression Tracking\n\n";
        for (const auto& note : analysis.regression_notes) {
            out << "- " << note << "\n";
        }
        out << "\n";
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
    Terminal::init(opts.color);

    std::cout << Terminal::bold() << Terminal::cyan()
              << "╔══════════════════════════════════════════════════════════════════════════════════╗\n"
              << "║              DGTL SOVEREIGN FORENSIC DEBUGGER & DIAGNOSTIC SUITE                 ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════════╝\n"
              << Terminal::reset() << "\n";

    std::cout << Terminal::bold() << "  Workspace Root   : " << Terminal::reset() << root.string() << "\n";
    if (!analysis.last_command.empty()) {
        std::cout << Terminal::bold() << "  Last Invocation  : " << Terminal::reset() << analysis.last_command << "\n";
    }
    if (!analysis.last_exit_code.empty()) {
        bool is_success = (analysis.last_exit_code == "0");
        std::cout << Terminal::bold() << "  Execution Status : " << Terminal::reset()
                  << (is_success ? (std::string(Terminal::green()) + Terminal::bold() + "[PASS: Exit Code 0]")
                                 : (std::string(Terminal::red()) + Terminal::bold() + "[FAIL: Exit Code " + analysis.last_exit_code + "]"))
                  << Terminal::reset() << "\n";
    }
    if (!analysis.compiler_fingerprint.empty()) {
        std::cout << Terminal::bold() << "  Toolchain Hash   : " << Terminal::reset() << analysis.compiler_fingerprint << "\n";
    }
    if (analysis.last_session_log) {
        std::cout << Terminal::bold() << "  Active Log       : " << Terminal::reset() << display_path(root, *analysis.last_session_log, opts.relative_paths) << "\n";
    }

    // High-impact status metrics dashboard pill
    std::cout << "\n  " << Terminal::dim() << "┌─ METRICS DASHBOARD ──────────────────────────────────────────────────────────────┐\n"
              << "  │ " << Terminal::reset()
              << "Modules: " << Terminal::bold() << analysis.modules.size() << Terminal::reset() << "  │  "
              << "Cycles: " << (analysis.cycles.empty() ? (std::string(Terminal::green()) + "0") : (std::string(Terminal::red()) + Terminal::bold() + std::to_string(analysis.cycles.size()))) << Terminal::reset() << "  │  "
              << "Error Clusters: " << (analysis.clusters.empty() ? (std::string(Terminal::green()) + "0") : (std::string(Terminal::red()) + Terminal::bold() + std::to_string(analysis.clusters.size()))) << Terminal::reset() << "  │  "
              << "Collisions: " << (analysis.collisions.empty() ? (std::string(Terminal::green()) + "0") : (std::string(Terminal::yellow()) + Terminal::bold() + std::to_string(analysis.collisions.size()))) << Terminal::reset() << "  │  "
              << "Cache: " << Terminal::bold() << analysis.declaration_cache_hits << " hits" << Terminal::reset()
              << Terminal::dim() << "\n  └──────────────────────────────────────────────────────────────────────────────────┘\n"
              << Terminal::reset() << "\n";

    // [1] Module Graph & Structural Topology
    std::cout << Terminal::bold() << "[1] Module Graph & Topology Audit\n" << Terminal::reset();
    std::cout << "    Discovered modules : " << analysis.modules.size() << "\n";
    if (analysis.cycles.empty()) {
        std::cout << "    Topology status    : " << Terminal::green() << Terminal::bold() << "[PASS] Module dependency graph is strictly acyclic (DAG validated)" << Terminal::reset() << "\n";
    } else {
        std::cout << "    Topology status    : " << Terminal::red() << Terminal::bold() << "[FAIL] " << analysis.cycles.size() << " circular dependency cycle(s) detected!" << Terminal::reset() << "\n";
        for (std::size_t i = 0; i < std::min(opts.top, analysis.cycles.size()); ++i) {
            const auto& cycle = analysis.cycles[i];
            std::cout << "\n    " << Terminal::red() << Terminal::bold() << "Cycle #" << (i + 1) << " (" << cycle.size() << " units in SCC):" << Terminal::reset() << "\n";
            for (std::size_t j = 0; j < cycle.size(); ++j) {
                const ModuleUnit* unit = cycle[j];
                const ModuleUnit* next = cycle[(j + 1) % cycle.size()];
                std::cout << "      " << (j == 0 ? "┌─ " : "├─ ") << Terminal::bold() << unit->module_name << Terminal::reset()
                          << "  " << Terminal::dim() << "(" << display_path(root, unit->path, opts.relative_paths) << ")" << Terminal::reset() << "\n";
                std::cout << "      │   imports " << Terminal::yellow() << next->module_name << Terminal::reset() << "\n";
            }
            std::cout << "      └─ " << Terminal::red() << "[CYCLE CLOSES HERE]" << Terminal::reset() << "\n";
        }
    }
    std::cout << "\n";

    // [2] Error Clusters with Rust/Clang style cards
    std::cout << Terminal::bold() << "[2] Diagnostic Incidents & Error Clusters\n" << Terminal::reset();
    std::cout << "    Logs scanned     : " << analysis.logs_scanned.size() << "\n";
    std::cout << "    Clusters total   : " << analysis.clusters.size() << "\n";

    if (analysis.clusters.empty()) {
        std::cout << "    " << Terminal::green() << Terminal::bold() << "[PASS] No compiler or linker errors detected." << Terminal::reset() << "\n\n";
    } else {
        std::cout << "\n";
        for (std::size_t i = 0; i < std::min(opts.top, analysis.clusters.size()); ++i) {
            const auto& cluster = analysis.clusters[i];
            std::string code = !cluster.error_code.empty() ? cluster.error_code : ("ERR-00" + std::to_string(i + 1));
            std::string kind_badge = !cluster.samples.empty() ? cluster.samples.front().kind : "error";

            std::cout << "  " << Terminal::red() << Terminal::bold()
                      << "╭── " << kind_badge << "[" << code << "]: "
                      << cluster.signature
                      << " ── (" << cluster.count << " occurrence" << (cluster.count > 1 ? "s" : "") << ") ─╮\n"
                      << Terminal::reset();

            if (!cluster.likely_command.empty()) {
                std::cout << "  │  " << Terminal::dim() << "compiler step: " << Terminal::reset()
                          << cluster.likely_command << " "
                          << Terminal::dim() << "(" << cluster.likely_command_hits << " hits)" << Terminal::reset() << "\n";
            }

            if (!opts.summary_only) {
                for (std::size_t s = 0; s < std::min<std::size_t>(2, cluster.samples.size()); ++s) {
                    const auto& sample = cluster.samples[s];
                    std::cout << "  │\n";
                    render_source_snippet(std::cout, sample.file, sample.line, sample.column, sample.message);
                    std::cout << "  │     " << Terminal::bold() << sample.message << Terminal::reset() << "\n";
                }
            }

            std::cout << "  │\n";
            if (!cluster.category.empty()) {
                std::cout << "  │  " << Terminal::bold() << Terminal::cyan() << "= category: " << Terminal::reset() << cluster.category << "\n";
            }
            if (!cluster.explanation.empty()) {
                std::cout << "  │  " << Terminal::bold() << Terminal::yellow() << "= diagnosis: " << Terminal::reset() << cluster.explanation << "\n";
            }
            if (!cluster.remediation_steps.empty()) {
                std::cout << "  │  " << Terminal::bold() << Terminal::green() << "= actionable remediation:" << Terminal::reset() << "\n";
                for (std::size_t r = 0; r < cluster.remediation_steps.size(); ++r) {
                    std::cout << "  │     " << (r + 1) << ". " << cluster.remediation_steps[r] << "\n";
                }
            }

            std::cout << "  " << Terminal::red() << Terminal::bold()
                      << "╰──────────────────────────────────────────────────────────────────────────────────╯\n"
                      << Terminal::reset() << "\n";
        }
    }

    // [3] Cache & Infrastructure Health
    std::cout << Terminal::bold() << "[3] Cache & Infrastructure Health\n" << Terminal::reset();
    if (analysis.cache_hints.empty()) {
        std::cout << "    " << Terminal::green() << "[PASS] No BMI desynchronization or cache corruption detected." << Terminal::reset() << "\n";
    } else {
        std::set<std::string> deduped(analysis.cache_hints.begin(), analysis.cache_hints.end());
        for (const auto& hint : deduped) {
            std::cout << "    " << Terminal::yellow() << Terminal::bold() << "[WARN] " << Terminal::reset() << hint << "\n";
        }
    }
    std::cout << "\n";

    // [4] Symbol Collision Radar
    std::cout << Terminal::bold() << "[4] Symbol Collision & Multi-Site Radar\n" << Terminal::reset();
    std::cout << "    Suspect symbols monitored : " << analysis.suspect_symbols.size() << "\n";
    std::cout << "    Active collision sites    : " << analysis.collisions.size() << "\n";
    if (!opts.summary_only && !analysis.collisions.empty()) {
        for (std::size_t i = 0; i < std::min(opts.top, analysis.collisions.size()); ++i) {
            const auto& collision = analysis.collisions[i];
            std::cout << "    " << Terminal::yellow() << "• " << Terminal::bold() << collision.symbol << Terminal::reset()
                      << " (" << collision.sites.size() << " declaration sites):\n";
            for (std::size_t j = 0; j < std::min<std::size_t>(3, collision.sites.size()); ++j) {
                const auto& site = collision.sites[j];
                std::cout << "        - " << display_path(root, site.file, opts.relative_paths)
                          << ":" << site.line << "  " << Terminal::dim() << site.text << Terminal::reset() << "\n";
            }
        }
    }
    std::cout << "\n";

    // [5] Ownership Arbitration
    std::cout << Terminal::bold() << "[5] Ownership Arbitration (ODR Resolver)\n" << Terminal::reset();
    std::cout << "    Recommendations : " << analysis.ownership_recommendations.size() << "\n";
    for (std::size_t i = 0; i < std::min(opts.top, analysis.ownership_recommendations.size()); ++i) {
        const auto& rec = analysis.ownership_recommendations[i];
        if (rec.candidates.empty()) continue;
        const auto& winner = rec.candidates.front();
        std::cout << "    " << Terminal::green() << "✓ " << Terminal::bold() << rec.symbol << Terminal::reset()
                  << "  ──>  " << Terminal::cyan() << display_path(root, winner.file, opts.relative_paths) << ":" << winner.line << Terminal::reset()
                  << " " << Terminal::dim() << "(score: " << winner.score << ")" << Terminal::reset() << "\n";
        if (!opts.summary_only) {
            std::cout << "        scoring evidence: ";
            for (std::size_t j = 0; j < winner.reasons.size(); ++j) {
                if (j != 0) std::cout << ", ";
                std::cout << winner.reasons[j];
            }
            std::cout << "\n";
            if (!winner.snippet_lines.empty()) {
                for (std::size_t idx = 0; idx < winner.snippet_lines.size(); ++idx) {
                    std::cout << "        " << Terminal::dim() << (winner.snippet_start_line + static_cast<int>(idx)) << " | " << Terminal::reset()
                              << winner.snippet_lines[idx] << "\n";
                }
            }
        }
    }
    std::cout << "\n";

    // [6] Staff-Level Executive Triage & Remediation Plan
    std::cout << Terminal::bold() << "[6] Executive Triage & Remediation Action Plan\n" << Terminal::reset();
    if (!analysis.cycles.empty()) {
        std::cout << "    1. " << Terminal::red() << Terminal::bold() << "[HIGH PRIORITY] Break Module Cycles:" << Terminal::reset()
                  << " SCC cycles halt topological build scheduling. Move interface types to leaf partitions.\n";
    }
    if (!analysis.clusters.empty()) {
        std::cout << "    2. " << Terminal::yellow() << Terminal::bold() << "[PRIMARY ROOT CAUSE] Address Top Error Cluster First:" << Terminal::reset()
                  << " Resolving '" << analysis.clusters.front().signature << "' will eliminate cascading derivative errors.\n";
    }
    if (analysis.clusters.empty() && analysis.cycles.empty()) {
        std::cout << "    " << Terminal::green() << Terminal::bold() << "[ALL CLEAR] System is in healthy state with zero structural or compilation defects." << Terminal::reset() << "\n";
    }
    for (const auto& note : analysis.regression_notes) {
        std::cout << "    • " << Terminal::cyan() << "Regression Note: " << Terminal::reset() << note << "\n";
    }
    if (!opts.focus.empty()) {
        std::cout << "    • " << Terminal::cyan() << "Focus token: " << Terminal::reset() << opts.focus << "\n";
    }
    std::cout << "\n";
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
    static bool audit_elf(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        size_t file_size = buffer.size();
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "              DGTL SOVEREIGN ELF64 BINARY STRUCTURAL AUDITOR                    \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Target Binary: " << path.string() << " (" << file_size << " bytes / " << (file_size / 1024.0) << " KB)\n\n";

        if (file_size < 64) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " File too small to contain valid ELF64 header (" << file_size << " bytes)\n";
            return false;
        }

        const uint8_t* raw = buffer.data();
        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " ELF Magic: 0x7F 'E' 'L' 'F'\n";
        uint8_t elf_class = raw[4];
        if (elf_class != 2 /* 64-bit */) {
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " Non-64-bit ELF class: " << (int)elf_class << "\n";
        } else {
            out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " ELF Class: 64-bit\n";
        }

        uint16_t e_machine = *reinterpret_cast<const uint16_t*>(raw + 18);
        std::string arch_str = (e_machine == 0x3E) ? "x86_64 (AMD64)" : ((e_machine == 0xB7) ? "AArch64 (ARM64)" : ((e_machine == 0xF3) ? "RISC-V 64" : ("Unknown machine 0x" + std::to_string(e_machine))));
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Architecture : " << Terminal::bold() << arch_str << Terminal::reset() << "\n";

        uint64_t entry_point = *reinterpret_cast<const uint64_t*>(raw + 24);
        uint64_t phoff = *reinterpret_cast<const uint64_t*>(raw + 32);
        uint64_t shoff = *reinterpret_cast<const uint64_t*>(raw + 40);
        uint16_t phentsize = *reinterpret_cast<const uint16_t*>(raw + 54);
        uint16_t phnum = *reinterpret_cast<const uint16_t*>(raw + 56);
        uint16_t shentsize = *reinterpret_cast<const uint16_t*>(raw + 58);
        uint16_t shnum = *reinterpret_cast<const uint16_t*>(raw + 60);

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Entry Point  : 0x" << std::hex << entry_point << std::dec << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Segments     : " << phnum << " program headers (at offset 0x" << std::hex << phoff << std::dec << ")\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Sections     : " << shnum << " section headers (at offset 0x" << std::hex << shoff << std::dec << ")\n";

        bool has_critical_error = false;

        // Audit program headers (PT_LOAD segments and BSS memory expansion)
        if (phoff + static_cast<size_t>(phnum) * phentsize <= file_size && phnum > 0) {
            out << "--------------------------------------------------------------------------------\n";
            out << "PROGRAM HEADER / LOAD SEGMENT AUDIT (" << phnum << " segments):\n";
            out << "Idx Type         Offset    VirtAddr          FileSz    MemSz     Flags  Status\n";
            for (uint16_t i = 0; i < phnum; ++i) {
                const uint8_t* ph = raw + phoff + (i * phentsize);
                uint32_t p_type = *reinterpret_cast<const uint32_t*>(ph);
                uint32_t p_flags = *reinterpret_cast<const uint32_t*>(ph + 4);
                uint64_t p_offset = *reinterpret_cast<const uint64_t*>(ph + 8);
                uint64_t p_vaddr = *reinterpret_cast<const uint64_t*>(ph + 16);
                uint64_t p_filesz = *reinterpret_cast<const uint64_t*>(ph + 32);
                uint64_t p_memsz = *reinterpret_cast<const uint64_t*>(ph + 40);

                std::string type_str = (p_type == 1) ? "PT_LOAD" : ((p_type == 3) ? "PT_INTERP" : ((p_type == 6) ? "PT_PHDR" : ((p_type == 7) ? "PT_TLS" : ((p_type == 0x6474E551) ? "PT_GNU_STACK" : ("0x" + std::to_string(p_type))))));
                std::string status = "OK";
                if (p_type == 1 /* PT_LOAD */) {
                    if (p_memsz > p_filesz) {
                        status = "OK (Zero-Fill BSS: +" + std::to_string(p_memsz - p_filesz) + " bytes)";
                    }
                    if ((p_flags & 1) && (p_flags & 2)) {
                        status = "[CRITICAL] W^X violation (Writable & Executable)!";
                        has_critical_error = true;
                    }
                } else if (p_type == 3 /* PT_INTERP */) {
                    status = "[WARN] Dynamic interpreter present (ld-linux)";
                } else if (p_type == 0x6474E551 /* PT_GNU_STACK */) {
                    status = ((p_flags & 1) == 0) ? "OK (Non-executable stack)" : "[WARN] Executable stack";
                }

                out << std::right << std::setw(3) << i << " "
                    << std::left << std::setw(10) << type_str << " "
                    << std::right << std::hex << std::setw(8) << std::setfill('0') << p_offset << "  "
                    << std::setw(16) << std::setfill('0') << p_vaddr << "  "
                    << std::setw(8) << std::setfill('0') << p_filesz << "  "
                    << std::setw(8) << std::setfill('0') << p_memsz << "  "
                    << "0x" << std::setw(4) << std::setfill('0') << p_flags << "  ";

                if (status.find("[CRITICAL]") != std::string::npos) {
                    out << Terminal::bg_red() << Terminal::bold() << status << Terminal::reset();
                } else if (status.find("[WARN]") != std::string::npos) {
                    out << Terminal::yellow() << Terminal::bold() << status << Terminal::reset();
                } else {
                    out << Terminal::green() << status << Terminal::reset();
                }
                out << "\n" << std::dec;
            }
        }

        if (has_critical_error) {
            out << "\n" << Terminal::yellow() << Terminal::bold() << "  [!] ARCHITECTURAL DEFECT & REMEDIATION ADVICE:\n" << Terminal::reset()
                << "      The binary violates W^X memory protection. Segments must not be simultaneously writable and executable.\n"
                << "      Recommendation: Rebuild with SovereignELFLinker ensuring code (.text) and data (.data/.bss) reside in disjoint PT_LOAD segments.\n";
        }

        out << "================================================================================\n";
        out << "AUDIT RESULT: " << (has_critical_error ? (std::string(Terminal::red()) + Terminal::bold() + "[FAIL] ELF binary has critical layout defects!")
                                                     : (std::string(Terminal::green()) + Terminal::bold() + "[PASS] ELF binary structure is valid."))
            << Terminal::reset() << "\n";
        out << "================================================================================\n\n";
        return !has_critical_error;
    }

    static bool audit_macho(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        size_t file_size = buffer.size();
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "              DGTL SOVEREIGN MACH-O 64 BINARY STRUCTURAL AUDITOR                \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Target Binary: " << path.string() << " (" << file_size << " bytes / " << (file_size / 1024.0) << " KB)\n\n";

        if (file_size < 32) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset() << " File too small to contain valid Mach-O 64 header\n";
            return false;
        }

        const uint8_t* raw = buffer.data();
        int32_t cputype = *reinterpret_cast<const int32_t*>(raw + 4);
        std::string arch_str = (cputype == 0x0100000C) ? "ARM64 (Apple Silicon)" : ((cputype == 0x01000007) ? "x86_64 (Intel Mac)" : ("Unknown 0x" + std::to_string(cputype)));
        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " Mach-O 64 Magic (0xFEEDFACF)\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Architecture : " << Terminal::bold() << arch_str << Terminal::reset() << "\n";
        uint32_t ncmds = *reinterpret_cast<const uint32_t*>(raw + 16);
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Load Commands: " << ncmds << "\n";

        bool has_code_signature = false;
        size_t cursor = 32;
        for (uint32_t c = 0; c < ncmds && cursor + 8 <= file_size; ++c) {
            uint32_t cmd = *reinterpret_cast<const uint32_t*>(raw + cursor);
            uint32_t cmdsize = *reinterpret_cast<const uint32_t*>(raw + cursor + 4);
            if (cmd == 0x1D /* LC_CODE_SIGNATURE */) {
                has_code_signature = true;
            }
            cursor += cmdsize;
        }
        if (has_code_signature) {
            out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " Code Signature : LC_CODE_SIGNATURE present (Apple Silicon valid)\n";
        } else if (cputype == 0x0100000C) {
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " Missing LC_CODE_SIGNATURE on ARM64 Mach-O. Apple Silicon kernel requires signature.\n";
        }

        out << "================================================================================\n";
        out << "AUDIT RESULT: " << Terminal::green() << Terminal::bold() << "[PASS] Mach-O 64 binary structure is valid." << Terminal::reset() << "\n";
        out << "================================================================================\n\n";
        return true;
    }

    static bool audit_wasm(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        size_t file_size = buffer.size();
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "             DGTL SOVEREIGN WEBASSEMBLY MODULE STRUCTURAL AUDITOR               \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Target Binary: " << path.string() << " (" << file_size << " bytes / " << (file_size / 1024.0) << " KB)\n\n";

        if (file_size < 8) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset() << " File too small to contain valid WASM header\n";
            return false;
        }

        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " WebAssembly Magic: '\\0asm' (0x6D736100)\n";
        uint32_t version = *reinterpret_cast<const uint32_t*>(buffer.data() + 4);
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " WASM Version   : " << version << "\n";

        // Section scanner
        size_t offset = 8;
        uint32_t import_count = 0;
        uint32_t export_count = 0;
        uint32_t func_count = 0;
        bool has_memory = false;

        auto read_leb_u32 = [&](size_t& off) -> uint32_t {
            uint32_t result = 0;
            uint32_t shift = 0;
            while (off < file_size) {
                uint8_t byte = buffer[off++];
                result |= (byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) break;
                shift += 7;
            }
            return result;
        };

        while (offset < file_size) {
            uint8_t sec_id = buffer[offset++];
            size_t sec_len = read_leb_u32(offset);
            size_t sec_end = offset + sec_len;

            if (sec_id == 2 /* Import */) {
                size_t p = offset;
                import_count = read_leb_u32(p);
            } else if (sec_id == 3 /* Function */) {
                size_t p = offset;
                func_count = read_leb_u32(p);
            } else if (sec_id == 5 /* Memory */) {
                has_memory = true;
            } else if (sec_id == 7 /* Export */) {
                size_t p = offset;
                export_count = read_leb_u32(p);
            }
            offset = sec_end;
        }

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Defined Functions: " << func_count << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Exported Entities: " << export_count << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Linear Memory    : " << (has_memory ? "Yes (1 memory segment)" : "No") << "\n";

        if (import_count == 0) {
            out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " Pure Sovereign WASM Module: 0 external imports (100% self-contained standalone)\n";
        } else {
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " Module has " << import_count << " external imports from host environment\n";
        }

        out << "================================================================================\n";
        out << "AUDIT RESULT: " << Terminal::green() << Terminal::bold() << "[PASS] WebAssembly binary module structure is valid." << Terminal::reset() << "\n";
        out << "================================================================================\n\n";
        return true;
    }

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

        // ─── Multi-Format Dispatch ──────────────────────────────────────────
        if (file_size >= 4 && buffer[0] == 0x7F && buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F') {
            return audit_elf(path, buffer, out);
        }
        if (file_size >= 4 && (*reinterpret_cast<const uint32_t*>(buffer.data()) == 0xFEEDFACF || *reinterpret_cast<const uint32_t*>(buffer.data()) == 0xCFFAEDFE)) {
            return audit_macho(path, buffer, out);
        }
        if (file_size >= 8 && buffer[0] == 0x00 && buffer[1] == 'a' && buffer[2] == 's' && buffer[3] == 'm') {
            return audit_wasm(path, buffer, out);
        }

        if (file_size < 0x40) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " File too small to contain DOS header (" << file_size << " bytes)\n";
            return false;
        }

        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "            DGTL SOVEREIGN PE/COFF EXECUTABLE STRUCTURAL AUDITOR                \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Target Binary: " << path.string() << " (" << file_size << " bytes / " << (file_size / 1024.0) << " KB)\n\n";

        // DOS Header
        uint16_t dos_magic = 0;
        std::memcpy(&dos_magic, buffer.data(), 2);
        if (dos_magic != 0x5A4D) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Invalid DOS Header magic (expected 0x5A4D 'MZ', got 0x" << std::hex << dos_magic << std::dec << ")\n";
            return false;
        }
        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " DOS Signature: 'MZ' (0x5A4D)\n";

        uint32_t pe_offset = 0;
        std::memcpy(&pe_offset, buffer.data() + 0x3C, 4);
        if (pe_offset + 24 > file_size) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Invalid e_lfanew offset (0x" << std::hex << pe_offset << std::dec << ")\n";
            return false;
        }

        uint32_t pe_sig = 0;
        std::memcpy(&pe_sig, buffer.data() + pe_offset, 4);
        if (pe_sig != 0x00004550) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Invalid PE Signature (expected 'PE\\0\\0', got 0x" << std::hex << pe_sig << std::dec << ")\n";
            return false;
        }
        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset()
            << " PE Signature: 'PE\\0\\0' at offset 0x" << std::hex << pe_offset << std::dec << "\n";

        // COFF File Header
        const uint8_t* coff_hdr = buffer.data() + pe_offset + 4;
        uint16_t machine = 0, num_sections = 0, opt_hdr_size = 0, characteristics = 0;
        uint32_t timestamp = 0;
        std::memcpy(&machine, coff_hdr, 2);
        std::memcpy(&num_sections, coff_hdr + 2, 2);
        std::memcpy(&timestamp, coff_hdr + 4, 4);
        std::memcpy(&opt_hdr_size, coff_hdr + 16, 2);
        std::memcpy(&characteristics, coff_hdr + 18, 2);

        std::string pe_arch_str = (machine == 0x8664) ? "x86_64 (AMD64)" : ((machine == 0xAA64) ? "AArch64 (ARM64)" : ((machine == 0x5064) ? "RISC-V 64" : ((machine == 0x14C) ? "x86 (i386)" : "Unknown 0x" + std::to_string(machine))));
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Architecture: " << Terminal::bold() << pe_arch_str << Terminal::reset()
            << " | Sections: " << num_sections << " | OptHdrSize: " << opt_hdr_size << " bytes\n";

        if (machine != 0x8664 && machine != 0xAA64 && machine != 0x5064) {
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " Non-standard machine type (" << pe_arch_str << ")\n";
        }

        if (opt_hdr_size < 112) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Optional Header size too small for PE32+ (" << opt_hdr_size << " bytes)\n";
            return false;
        }

        // Optional Header
        const uint8_t* opt_hdr = coff_hdr + 20;
        uint16_t opt_magic = 0;
        std::memcpy(&opt_magic, opt_hdr, 2);
        if (opt_magic != 0x20B) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Not PE32+ 64-bit binary (magic=0x" << std::hex << opt_magic << std::dec << ")\n";
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

        uint64_t stack_reserve = 0, stack_commit = 0, heap_reserve = 0, heap_commit = 0;
        if (opt_hdr_size >= 112) {
            std::memcpy(&stack_reserve, opt_hdr + 72, 8);
            std::memcpy(&stack_commit, opt_hdr + 80, 8);
            std::memcpy(&heap_reserve, opt_hdr + 88, 8);
            std::memcpy(&heap_commit, opt_hdr + 96, 8);
        }

        out << "--------------------------------------------------------------------------------\n";
        out << "OPTIONAL HEADER (PE32+):\n";
        out << "  ImageBase          : 0x" << std::hex << image_base << "\n";
        out << "  AddressOfEntryPoint: 0x" << entry_rva << " (VA: 0x" << (image_base + entry_rva) << ")\n";
        out << "  SizeOfImage        : 0x" << image_size << " (" << std::dec << image_size << " bytes)\n";
        out << "  SizeOfHeaders      : 0x" << std::hex << headers_size << "\n";
        out << "  SectionAlignment   : 0x" << section_align << "\n";
        out << "  FileAlignment      : 0x" << file_align << "\n";
        out << "  Subsystem          : " << (subsystem == 3 ? "IMAGE_SUBSYSTEM_WINDOWS_CUI (Console)" : (subsystem == 2 ? "IMAGE_SUBSYSTEM_WINDOWS_GUI" : std::to_string(subsystem))) << std::dec << "\n";
        out << "  SizeOfStackReserve : 0x" << std::hex << stack_reserve << " (" << std::dec << (stack_reserve / 1024) << " KB)\n";
        out << "  SizeOfStackCommit  : 0x" << std::hex << stack_commit << " (" << std::dec << (stack_commit / 1024) << " KB)";
        if (stack_commit <= 0x10000) {
            out << " " << Terminal::green() << Terminal::bold() << "[LEAN - Probed on demand via __chkstk]" << Terminal::reset();
        } else if (stack_commit >= 0x800000) {
            out << " " << Terminal::yellow() << Terminal::bold() << "[WARN: HIGH COMMIT - Stack pre-committed]" << Terminal::reset();
        }
        out << "\n";
        out << "  SizeOfHeapReserve  : 0x" << std::hex << heap_reserve << " (" << std::dec << (heap_reserve / 1024) << " KB)\n";
        out << "  SizeOfHeapCommit   : 0x" << std::hex << heap_commit << " (" << std::dec << (heap_commit / 1024) << " KB)\n";

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
                << "0x" << std::setw(8) << std::setfill('0') << flags << "  ";

            if (status.find("[CRITICAL]") != std::string::npos) {
                out << Terminal::bg_red() << Terminal::bold() << status << Terminal::reset();
            } else {
                out << Terminal::green() << status << Terminal::reset();
            }
            out << "\n" << std::dec;
        }

        if (!entry_point_found) {
            out << "  " << Terminal::bg_red() << Terminal::bold() << "[CRITICAL ERROR]" << Terminal::reset()
                << " Entry point RVA 0x" << std::hex << entry_rva << std::dec << " does NOT fall into any section!\n";
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

                    out << "  [DLL " << ++dll_count << "] " << Terminal::bold() << dll_name << Terminal::reset()
                        << " (IAT RVA: 0x" << std::hex << iat_rva << ", ILT RVA: 0x" << ilt_rva << ")\n" << std::dec;

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
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " No Import Directory Table found.\n";
        }

        // Disassemble entry point
        out << "--------------------------------------------------------------------------------\n";
        out << "ENTRY POINT AUDIT & DISASSEMBLY (Starting at 0x" << std::hex << (image_base + entry_rva) << "):\n" << std::dec;
        size_t ep_off = rva_to_offset(entry_rva);
        if (ep_off != 0 && ep_off < file_size) {
            if (ep_off + 4 <= file_size) {
                const uint8_t* ep_bytes = buffer.data() + ep_off;
                if (ep_bytes[0] == 0x48 && ep_bytes[1] == 0x83 && ep_bytes[2] == 0xEC && ep_bytes[3] == 0x28) {
                    out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset()
                        << " Win64 Entry Trampoline: sub rsp, 0x28 (16-byte stack aligned per Win64 ABI)\n";
                } else if (ep_bytes[0] == 0x48 && ep_bytes[1] == 0x83 && ep_bytes[2] == 0xEC && ep_bytes[3] == 0x20) {
                    out << "  " << Terminal::red() << Terminal::bold() << "[CRITICAL ABI DEFECT]" << Terminal::reset()
                        << " Win64 Entry Trampoline: sub rsp, 0x20 violates 16-byte stack alignment (causes STATUS_ACCESS_VIOLATION)!\n";
                    has_critical_error = true;
                }
            }
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
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset()
                << " Cannot map entry point RVA to file offset!\n";
        }

        if (has_critical_error) {
            out << "\n" << Terminal::yellow() << Terminal::bold() << "  [!] ARCHITECTURAL DEFECT & REMEDIATION ADVICE:\n" << Terminal::reset()
                << "      The PE32+ executable contains fatal section layout defects. At runtime, the Windows\n"
                << "      OS loader will immediately fail to map this binary or trigger STATUS_ACCESS_VIOLATION.\n"
                << "      Recommendation: Verify PE section alignment in SovereignPELinker and ensure AddressOfEntryPoint\n"
                << "      points to a valid executable .text section.\n";
        }

        out << "================================================================================\n";
        out << "AUDIT RESULT: " << (has_critical_error ? (std::string(Terminal::red()) + Terminal::bold() + "[FAIL] Binary has critical layout defects!")
                                                     : (std::string(Terminal::green()) + Terminal::bold() + "[PASS] Binary structure is valid."))
            << Terminal::reset() << "\n";
        out << "================================================================================\n\n";

        return !has_critical_error;
    }
};

// ============================================================================
// UNIVERSAL FILE & SECTION INSPECTOR
// Supports: .a, .lib, .s, .asm, .so, .dll, .dylib, .obj, .o, .dgtl, .mzc,
//           .text, .data, .pdata, .xdata, .bss, .rdata, .rodata,
//           .myz, .mzi, .mzm, .my, .myzt, .myzb, .myzs, .mzp, .myd,
//           myzon.toml, myzon.lock, dgtl.toml, dgtl.lock
// ============================================================================
class FileInspector {
public:
    static void hex_ascii_dump(const uint8_t* data, size_t size, uint64_t base_addr, std::ostream& out, size_t max_bytes = 256) {
        size_t print_len = std::min(size, max_bytes);
        for (size_t row = 0; row < print_len; row += 16) {
            out << "  0x" << std::hex << std::setw(8) << std::setfill('0') << (base_addr + row) << ":  ";
            for (size_t col = 0; col < 16; ++col) {
                if (row + col < print_len) {
                    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[row + col]) << " ";
                } else {
                    out << "   ";
                }
                if (col == 7) out << " ";
            }
            out << " |";
            for (size_t col = 0; col < 16 && (row + col) < print_len; ++col) {
                uint8_t ch = data[row + col];
                out << (std::isprint(ch) ? static_cast<char>(ch) : '.');
            }
            out << "|\n";
        }
        if (size > max_bytes) {
            out << "  ... (" << (size - max_bytes) << " bytes truncated)\n";
        }
        out << std::dec;
    }

    static bool audit_archive(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "             DGTL SOVEREIGN STATIC ARCHIVE INSPECTOR (.a / .lib)                \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Archive Path: " << path.string() << " (" << buffer.size() << " bytes)\n\n";

        if (buffer.size() < 8 || std::memcmp(buffer.data(), "!<arch>\n", 8) != 0) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset() << " Invalid archive header magic.\n";
            return false;
        }

        out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " Valid static archive header (!<arch>\\n)\n\n";

        size_t off = 8;
        uint32_t member_idx = 0;
        out << "--------------------------------------------------------------------------------\n";
        out << "ARCHIVE MEMBERS CATALOG:\n";
        out << "Idx  Member Name                     Size (bytes)   File Offset\n";
        out << "--------------------------------------------------------------------------------\n";

        while (off + 60 <= buffer.size()) {
            char name_buf[17] = {0};
            std::memcpy(name_buf, buffer.data() + off, 16);
            std::string name = trim(name_buf);

            char size_buf[11] = {0};
            std::memcpy(size_buf, buffer.data() + off + 48, 10);
            size_t member_size = std::strtoull(size_buf, nullptr, 10);

            out << std::right << std::setw(3) << member_idx++ << "  "
                << std::left << std::setw(30) << name << "  "
                << std::right << std::setw(12) << member_size << "   0x"
                << std::hex << std::setw(8) << std::setfill('0') << (off + 60) << std::dec << "\n";

            off += 60 + ((member_size + 1) & ~1ULL);
        }
        out << "--------------------------------------------------------------------------------\n";
        out << "Total Archive Members Discovered: " << member_idx << "\n\n";
        return true;
    }

    static bool inspect_object(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        if (buffer.size() >= 4 && buffer[0] == 0x7F && buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F') {
            return PeInspector::audit_elf(path, buffer, out);
        }
        if (buffer.size() >= 4 && (*reinterpret_cast<const uint32_t*>(buffer.data()) == 0xFEEDFACF || *reinterpret_cast<const uint32_t*>(buffer.data()) == 0xCFFAEDFE)) {
            return PeInspector::audit_macho(path, buffer, out);
        }

        // COFF Relocatable Object File
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "             DGTL SOVEREIGN COFF RELOCATABLE OBJECT INSPECTOR (.obj)            \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Object File: " << path.string() << " (" << buffer.size() << " bytes)\n\n";

        if (buffer.size() < 20) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset() << " File too small for COFF header.\n";
            return false;
        }

        uint16_t machine = *reinterpret_cast<const uint16_t*>(buffer.data());
        uint16_t num_sections = *reinterpret_cast<const uint16_t*>(buffer.data() + 2);
        uint32_t sym_ptr = *reinterpret_cast<const uint32_t*>(buffer.data() + 8);
        uint32_t num_symbols = *reinterpret_cast<const uint32_t*>(buffer.data() + 12);
        uint16_t opt_hdr_sz = *reinterpret_cast<const uint16_t*>(buffer.data() + 16);

        std::string arch_name = (machine == 0x8664) ? "x86_64 (AMD64)" : ((machine == 0xAA64) ? "AArch64 (ARM64)" : ((machine == 0x5064) ? "RISC-V 64" : ("Machine 0x" + std::to_string(machine))));
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Architecture : " << Terminal::bold() << arch_name << Terminal::reset() << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Count: " << num_sections << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Symbol Count : " << num_symbols << " (at file offset 0x" << std::hex << sym_ptr << std::dec << ")\n\n";

        size_t sec_offset = 20 + opt_hdr_sz;
        out << "--------------------------------------------------------------------------------\n";
        out << "COFF SECTION TABLE:\n";
        out << "Idx  Name       RawSize    RawOffset  RelocPtr   RelocCnt   Flags\n";
        out << "--------------------------------------------------------------------------------\n";
        for (uint16_t i = 0; i < num_sections && (sec_offset + 40 <= buffer.size()); ++i) {
            char name_buf[9] = {0};
            std::memcpy(name_buf, buffer.data() + sec_offset, 8);
            uint32_t raw_sz = *reinterpret_cast<const uint32_t*>(buffer.data() + sec_offset + 16);
            uint32_t raw_off = *reinterpret_cast<const uint32_t*>(buffer.data() + sec_offset + 20);
            uint32_t reloc_ptr = *reinterpret_cast<const uint32_t*>(buffer.data() + sec_offset + 24);
            uint16_t reloc_cnt = *reinterpret_cast<const uint16_t*>(buffer.data() + sec_offset + 32);
            uint32_t sec_flags = *reinterpret_cast<const uint32_t*>(buffer.data() + sec_offset + 36);

            out << std::right << std::setw(3) << i << "  "
                << std::left << std::setw(10) << name_buf << " "
                << std::right << "0x" << std::hex << std::setw(8) << std::setfill('0') << raw_sz << " "
                << "0x" << std::setw(8) << std::setfill('0') << raw_off << " "
                << "0x" << std::setw(8) << std::setfill('0') << reloc_ptr << " "
                << std::dec << std::setw(8) << std::setfill(' ') << reloc_cnt << "   "
                << "0x" << std::hex << std::setw(8) << std::setfill('0') << sec_flags << std::dec << "\n";
            sec_offset += 40;
        }
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_raw_section(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        std::string ext = to_lower(path.extension().string());
        std::string fname = to_lower(path.filename().string());

        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "          DGTL SOVEREIGN SECTION BINARY INSPECTOR & FORENSICS                   \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Section Target: " << path.string() << " (" << buffer.size() << " bytes)\n\n";

        if (ext == ".text" || fname.find(".text") != std::string::npos) {
            out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Type: " << Terminal::bold() << "Executable Code (.text)" << Terminal::reset() << "\n";
            out << "--------------------------------------------------------------------------------\n";
            out << "DISASSEMBLY OF INSTRUCTION STREAM:\n";
            uint64_t cur_rip = 0;
            size_t off = 0;
            size_t max_inst = 50;
            while (off < buffer.size() && max_inst-- > 0) {
                auto instr = X64Disassembler::decode(buffer.data() + off, buffer.size() - off, cur_rip);
                out << "  0x" << std::hex << std::setw(8) << std::setfill('0') << cur_rip << ": ";
                for (size_t b = 0; b < 6; ++b) {
                    if (b < instr.length) {
                        out << std::setw(2) << std::setfill('0') << (int)buffer[off + b] << " ";
                    } else {
                        out << "   ";
                    }
                }
                out << "  " << instr.text << "\n" << std::dec;
                off += instr.length;
                cur_rip += instr.length;
            }
            if (off < buffer.size()) {
                out << "  ... (" << (buffer.size() - off) << " more bytes of code)\n";
            }
            out << "--------------------------------------------------------------------------------\n\n";
            return true;
        }

        if (ext == ".pdata" || fname.find(".pdata") != std::string::npos) {
            out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Type: " << Terminal::bold() << "Windows x64 Exception Directory (.pdata)" << Terminal::reset() << "\n";
            out << "--------------------------------------------------------------------------------\n";
            out << "RUNTIME_FUNCTION TABLE (12 bytes per function entry):\n";
            out << "Idx   Begin RVA    End RVA      Length (B)   UnwindInfo RVA\n";
            out << "--------------------------------------------------------------------------------\n";
            size_t count = buffer.size() / 12;
            for (size_t i = 0; i < count && i < 30; ++i) {
                const uint8_t* p = buffer.data() + (i * 12);
                uint32_t begin_rva = *reinterpret_cast<const uint32_t*>(p);
                uint32_t end_rva = *reinterpret_cast<const uint32_t*>(p + 4);
                uint32_t unwind_rva = *reinterpret_cast<const uint32_t*>(p + 8);
                uint32_t len = (end_rva >= begin_rva) ? (end_rva - begin_rva) : 0;

                out << std::right << std::setw(4) << i << "  "
                    << "0x" << std::hex << std::setw(8) << std::setfill('0') << begin_rva << "   "
                    << "0x" << std::setw(8) << std::setfill('0') << end_rva << "   "
                    << std::dec << std::setw(10) << std::setfill(' ') << len << "   "
                    << "0x" << std::hex << std::setw(8) << std::setfill('0') << unwind_rva << std::dec << "\n";
            }
            out << "--------------------------------------------------------------------------------\n";
            out << "Total Functions Indexed in .pdata: " << count << "\n\n";
            return true;
        }

        if (ext == ".xdata" || fname.find(".xdata") != std::string::npos) {
            out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Type: " << Terminal::bold() << "Windows x64 Exception Unwind Data (.xdata / UNWIND_INFO)" << Terminal::reset() << "\n";
            if (buffer.size() >= 4) {
                uint8_t ver_flags = buffer[0];
                uint8_t version = ver_flags & 0x7;
                uint8_t flags = (ver_flags >> 3) & 0x1F;
                uint8_t prolog_size = buffer[1];
                uint8_t code_count = buffer[2];
                uint8_t frame_reg = buffer[3] & 0xF;
                uint8_t frame_off = (buffer[3] >> 4) & 0xF;

                out << "  Version        : " << (int)version << "\n"
                    << "  Flags          : 0x" << std::hex << (int)flags << std::dec
                    << " (" << ((flags & 1) ? "EHANDLER " : "") << ((flags & 2) ? "UHANDLER " : "") << ((flags & 4) ? "CHAININFO" : "") << ")\n"
                    << "  Prolog Size    : " << (int)prolog_size << " bytes\n"
                    << "  Unwind Codes   : " << (int)code_count << "\n"
                    << "  Frame Register : " << (int)frame_reg << " (offset * 16 = " << (frame_off * 16) << ")\n";
            }
            out << "--------------------------------------------------------------------------------\n";
            out << "RAW HEX & ASCII DUMP:\n";
            hex_ascii_dump(buffer.data(), buffer.size(), 0, out);
            out << "--------------------------------------------------------------------------------\n\n";
            return true;
        }

        if (ext == ".bss" || fname.find(".bss") != std::string::npos) {
            out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Type: " << Terminal::bold() << "Uninitialized Block Started by Symbol (.bss)" << Terminal::reset() << "\n";
            out << "  Capacity: " << buffer.size() << " bytes (Zero-filled virtual memory)\n\n";
            return true;
        }

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Section Type: " << Terminal::bold() << "Raw Data Section (" << ext << ")" << Terminal::reset() << "\n";
        out << "--------------------------------------------------------------------------------\n";
        hex_ascii_dump(buffer.data(), buffer.size(), 0, out);
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_assembly(const fs::path& path, const std::string& content, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "             DGTL SOVEREIGN ASSEMBLY SOURCE INSPECTOR (.s / .asm)               \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Source File: " << path.string() << " (" << content.size() << " bytes)\n\n";

        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> symbols;
        size_t directives = 0, instructions = 0;

        while (std::getline(stream, line)) {
            std::string t = trim(line);
            if (t.starts_with(".globl") || t.starts_with(".global") || t.starts_with("global")) {
                symbols.push_back(t);
                ++directives;
            } else if (t.starts_with(".")) {
                ++directives;
            } else if (t.ends_with(":")) {
                symbols.push_back(t);
            } else if (!t.empty() && !t.starts_with("#") && !t.starts_with("//") && !t.starts_with(";")) {
                ++instructions;
            }
        }

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Directives  : " << directives << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Instructions: " << instructions << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Exported/Local Labels: " << symbols.size() << "\n\n";

        out << "--------------------------------------------------------------------------------\n";
        out << "SOURCE PREVIEW (with line numbers):\n";
        out << "--------------------------------------------------------------------------------\n";
        std::istringstream preview_stream(content);
        size_t line_no = 1;
        while (std::getline(preview_stream, line) && line_no <= 40) {
            out << std::right << std::setw(4) << line_no++ << " | " << line << "\n";
        }
        if (line_no > 40) {
            out << "  ... (more lines in file)\n";
        }
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_dgtl_ir(const fs::path& path, const std::string& content, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "           DGTL SOVEREIGN POLYHEDRAL IR & COMPUTE GRAPH INSPECTOR (.dgtl / .mzc)\n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Target File: " << path.string() << " (" << content.size() << " bytes)\n\n";

        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("POLYHEDRAL_DOMAIN") != std::string::npos ||
                line.find("SCHEDULE") != std::string::npos ||
                line.find("SIMD_VECTOR_WIDTH") != std::string::npos ||
                line.find("L1_CACHE_TILE_BYTES") != std::string::npos ||
                line.find("ENTRY") != std::string::npos ||
                line.find("FMA_F32") != std::string::npos ||
                line.find("FMA_F64") != std::string::npos ||
                line.find("GELU_F32") != std::string::npos ||
                line.find("SILU_F32") != std::string::npos ||
                line.find("RMS_NORM_F32") != std::string::npos ||
                line.find("FLASH_ATTN_F32") != std::string::npos ||
                line.find("FAST_INV_SQRT") != std::string::npos) {
                out << "  " << Terminal::green() << Terminal::bold() << "• " << line << Terminal::reset() << "\n";
            }
        }

        out << "\n--------------------------------------------------------------------------------\n";
        out << "IR CONTENT DUMP:\n";
        out << "--------------------------------------------------------------------------------\n";
        std::istringstream dump_stream(content);
        size_t line_no = 1;
        while (std::getline(dump_stream, line) && line_no <= 50) {
            out << std::right << std::setw(4) << line_no++ << " | " << line << "\n";
        }
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_myzon_slab(const fs::path& path, const std::vector<uint8_t>& buffer, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "       MYZON ZERO-COPY PRECOMPILED BINARY PACKAGE SLAB INSPECTOR (.mzp)         \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Binary Slab: " << path.string() << " (" << buffer.size() << " bytes)\n\n";

        if (buffer.size() < 64) {
            out << "  " << Terminal::red() << Terminal::bold() << "[ FAIL ]" << Terminal::reset() << " Slab too small for 64-byte aligned header.\n";
            return false;
        }

        if (std::memcmp(buffer.data(), "MYZON_P\0", 8) != 0) {
            out << "  " << Terminal::yellow() << Terminal::bold() << "[ WARN ]" << Terminal::reset() << " Magic header differs from canonical MYZON_P.\n";
        } else {
            out << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset() << " Canonical Slab Magic Verified: 'MYZON_P\\0'\n";
        }

        uint32_t str_off = *reinterpret_cast<const uint32_t*>(buffer.data() + 40);
        uint32_t str_sz  = *reinterpret_cast<const uint32_t*>(buffer.data() + 44);
        uint32_t sym_off = *reinterpret_cast<const uint32_t*>(buffer.data() + 48);
        uint32_t sym_cnt = *reinterpret_cast<const uint32_t*>(buffer.data() + 52);

        out << "  String Table : Offset=0x" << std::hex << str_off << ", Size=" << std::dec << str_sz << " bytes\n";
        out << "  Symbol Table : Offset=0x" << std::hex << sym_off << ", Count=" << std::dec << sym_cnt << " symbols\n\n";

        out << "--------------------------------------------------------------------------------\n";
        hex_ascii_dump(buffer.data(), buffer.size(), 0, out, 256);
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_myzon_source(const fs::path& path, const std::string& content, std::ostream& out) {
        std::string ext = to_lower(path.extension().string());
        std::string role = "Myzon Source";
        std::string desc = "Modern canonical source file";

        if (ext == ".mzi") { role = "Public Interface"; desc = "Module interface stub / public API contract (no function bodies)"; }
        else if (ext == ".mzm") { role = "Modular Unit"; desc = "Internal submodule partition / engine partition"; }
        else if (ext == ".my") { role = "Legacy Source"; desc = "Legacy v1.0 source file (backward compatibility mode)"; }
        else if (ext == ".myzt") { role = "Dedicated Unit Test"; desc = "Test harness target (stripped from production builds)"; }
        else if (ext == ".myzb") { role = "Performance Benchmark"; desc = "Micro-benchmark measuring throughput and CPU latency"; }
        else if (ext == ".myzs") { role = "Zero-Overhead Script"; desc = "DevOps automation executed standalone via Strike JIT (<1ms)"; }
        else if (ext == ".myd") { role = "Dynamic Native Module"; desc = "Dynamically loadable native module / shared plugin"; }

        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "               MYZON LANGUAGE SOURCE & MODULE INSPECTOR (" << ext << ")                 \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "File Target: " << path.string() << " (" << content.size() << " bytes)\n";
        out << "Role       : " << Terminal::bold() << role << Terminal::reset() << " (" << desc << ")\n\n";

        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> structs, traits, funcs, imports;

        while (std::getline(stream, line)) {
            std::string t = trim(line);
            if (t.starts_with("import ")) imports.push_back(t);
            else if (t.find("struct ") != std::string::npos) structs.push_back(t);
            else if (t.find("trait ") != std::string::npos) traits.push_back(t);
            else if (t.find("fn ") != std::string::npos) funcs.push_back(t);
        }

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Package Imports : " << imports.size() << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Struct Entities : " << structs.size() << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Trait Contracts : " << traits.size() << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Routines/Funcs  : " << funcs.size() << "\n\n";

        out << "--------------------------------------------------------------------------------\n";
        out << "SOURCE PREVIEW (with line numbers):\n";
        out << "--------------------------------------------------------------------------------\n";
        std::istringstream preview_stream(content);
        size_t line_no = 1;
        while (std::getline(preview_stream, line) && line_no <= 50) {
            out << std::right << std::setw(4) << line_no++ << " | " << line << "\n";
        }
        if (line_no > 50) {
            out << "  ... (more lines in file)\n";
        }
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool inspect_manifest(const fs::path& path, const std::string& content, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "             PACKAGE MANIFEST & REPRODUCIBLE LOCKFILE INSPECTOR                 \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "Manifest File: " << path.string() << " (" << content.size() << " bytes)\n\n";

        std::istringstream stream(content);
        std::string line;
        size_t line_no = 1;
        while (std::getline(stream, line) && line_no <= 50) {
            if (line.starts_with("[")) {
                out << Terminal::bold() << Terminal::yellow() << line << Terminal::reset() << "\n";
            } else {
                out << std::right << std::setw(4) << line_no << " | " << line << "\n";
            }
            ++line_no;
        }
        out << "\n";
        return true;
    }

    static bool inspect_smt_script(const fs::path& path, const std::string& content, std::ostream& out) {
        out << Terminal::bold() << Terminal::cyan()
            << "================================================================================\n"
            << "         DGTL SOVEREIGN SMT-LIB2 FORMAL VERIFICATION INSPECTOR                  \n"
            << "================================================================================\n"
            << Terminal::reset();
        out << "SMT Script: " << path.string() << " (" << content.size() << " bytes)\n\n";

        std::istringstream stream(content);
        std::string line;
        size_t asserts = 0, decls = 0, checks = 0;
        std::string logic;

        std::vector<std::string> lines;
        while (std::getline(stream, line)) {
            lines.push_back(line);
            std::string t = trim(line);
            if (t.starts_with("(set-logic")) {
                size_t p1 = t.find(' ');
                size_t p2 = t.find(')', p1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    logic = t.substr(p1 + 1, p2 - p1 - 1);
                }
            } else if (t.starts_with("(assert")) {
                asserts++;
            } else if (t.starts_with("(declare-")) {
                decls++;
            } else if (t.starts_with("(check-sat")) {
                checks++;
            }
        }

        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " SMT Logic     : " << Terminal::bold() << (logic.empty() ? "QF_BV (default/unspecified)" : logic) << Terminal::reset() << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Declarations  : " << decls << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Assertions    : " << asserts << "\n";
        out << "  " << Terminal::cyan() << "•" << Terminal::reset() << " Solvability   : " << checks << " check-sat queries\n\n";
        out << "--------------------------------------------------------------------------------\n";
        out << "SMT SCRIPT CONTENT (First 100 lines):\n";
        out << "--------------------------------------------------------------------------------\n";
        for (size_t i = 0; i < std::min<size_t>(lines.size(), 100); ++i) {
            out << std::right << std::setw(4) << (i + 1) << " | " << lines[i] << "\n";
        }
        if (lines.size() > 100) {
            out << "     ... (" << (lines.size() - 100) << " more lines)\n";
        }
        out << "--------------------------------------------------------------------------------\n\n";
        return true;
    }

    static bool open_and_inspect(const fs::path& path, std::ostream& out = std::cout) {
        if (!fs::exists(path)) {
            out << Terminal::red() << "[!] Error: File does not exist: " << path.string() << Terminal::reset() << "\n";
            return false;
        }

        std::string ext = to_lower(path.extension().string());
        std::string fname = to_lower(path.filename().string());

        // Read binary buffer
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            out << Terminal::red() << "[!] Error: Cannot open file: " << path.string() << Terminal::reset() << "\n";
            return false;
        }

        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // 1. Static archives (.a, .lib, or !<arch>)
        if (ext == ".a" || ext == ".lib" || (buffer.size() >= 8 && std::memcmp(buffer.data(), "!<arch>\n", 8) == 0)) {
            return audit_archive(path, buffer, out);
        }

        // 2. Relocatable object files (.obj, .o)
        if (ext == ".obj" || ext == ".o") {
            return inspect_object(path, buffer, out);
        }

        // 3. Executables / Shared Libraries (.exe, .so, .dll, .dylib, .elf, .wasm)
        if (ext == ".exe" || ext == ".so" || ext == ".dll" || ext == ".dylib" || ext == ".elf" || ext == ".wasm" ||
            (buffer.size() >= 4 && buffer[0] == 0x7F && buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F') ||
            (buffer.size() >= 2 && buffer[0] == 'M' && buffer[1] == 'Z')) {
            return PeInspector::audit(path, out);
        }

        // 4. Raw Section Dumps (.text, .data, .pdata, .xdata, .bss, .rdata, .rodata)
        if (ext == ".text" || ext == ".data" || ext == ".pdata" || ext == ".xdata" || ext == ".bss" || ext == ".rdata" || ext == ".rodata") {
            return inspect_raw_section(path, buffer, out);
        }

        // 5. Myzon Precompiled Slabs (.mzp)
        if (ext == ".mzp" || (buffer.size() >= 8 && std::memcmp(buffer.data(), "MYZON_P\0", 8) == 0)) {
            return inspect_myzon_slab(path, buffer, out);
        }

        std::string content(reinterpret_cast<const char*>(buffer.data()), buffer.size());

        // 6. SMT-LIB2 formal verification scripts (.smt, .smt2)
        if (ext == ".smt" || ext == ".smt2") {
            return inspect_smt_script(path, content, out);
        }

        // 7. Assembly source (.s, .asm)
        if (ext == ".s" || ext == ".asm") {
            return inspect_assembly(path, content, out);
        }

        // 8. DGTL IR & Compute Graph (.dgtl, .mzc)
        if (ext == ".dgtl" || ext == ".mzc") {
            return inspect_dgtl_ir(path, content, out);
        }

        // 8. Myzon source files (.myz, .mzi, .mzm, .my, .myzt, .myzb, .myzs, .myd)
        if (ext == ".myz" || ext == ".mzi" || ext == ".mzm" || ext == ".my" || ext == ".myzt" || ext == ".myzb" || ext == ".myzs" || ext == ".myd") {
            return inspect_myzon_source(path, content, out);
        }

        // 9. Project manifests & lockfiles
        if (fname == "myzon.toml" || fname == "myzon.lock" || fname == "dgtl.toml" || fname == "dgtl.lock" || ext == ".toml" || ext == ".lock") {
            return inspect_manifest(path, content, out);
        }

        // 10. Fallback: If printable text, inspect as source/text; otherwise hex dump
        bool is_text = true;
        for (size_t i = 0; i < std::min<size_t>(buffer.size(), 1024); ++i) {
            uint8_t c = buffer[i];
            if (c != '\n' && c != '\r' && c != '\t' && (c < 0x20 || c > 0x7E)) {
                is_text = false;
                break;
            }
        }

        if (is_text) {
            out << Terminal::bold() << Terminal::cyan()
                << "================================================================================\n"
                << "                  DGTL SOVEREIGN SOURCE / TEXT VIEWER                           \n"
                << "================================================================================\n"
                << Terminal::reset();
            out << "File: " << path.string() << " (" << buffer.size() << " bytes)\n\n";
            std::istringstream stream(content);
            std::string line;
            size_t line_no = 1;
            while (std::getline(stream, line) && line_no <= 80) {
                out << std::right << std::setw(4) << line_no++ << " | " << line << "\n";
            }
            return true;
        }

        return inspect_raw_section(path, buffer, out);
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

        auto exec_start_time = std::chrono::steady_clock::now();
        auto sys_start_time = std::chrono::system_clock::now();

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
                std::cout << Terminal::dim() << "[dgtl-debugger]" << Terminal::reset()
                          << " " << Terminal::green() << "[SPAWN]" << Terminal::reset()
                          << " Process created successfully (PID=" << dbg.dwProcessId << ")\n"
                          << "                 ImageBase   : 0x" << std::hex << image_base << "\n"
                          << "                 EntryPoint  : 0x" << entry_point << std::dec << "\n";
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
                std::cout << Terminal::dim() << "[dgtl-debugger]" << Terminal::reset()
                          << " " << Terminal::dim() << "[MODULE]" << Terminal::reset()
                          << " Loaded: " << std::left << std::setw(28) << dname << std::right
                          << " at 0x" << std::hex << dll_base << std::dec << "\n";
                break;
            }
            case EXCEPTION_DEBUG_EVENT: {
                const auto& rec = dbg.u.Exception.ExceptionRecord;
                DWORD code = rec.ExceptionCode;

                if (code == STATUS_BREAKPOINT) {
                    continue_status = DBG_CONTINUE;
                    break;
                }

                auto exec_fault_time = std::chrono::steady_clock::now();
                int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(exec_fault_time - exec_start_time).count();
                double elapsed_ms = elapsed_us / 1000.0;

                auto resolve_addr_owner = [&](uint64_t addr) -> std::string {
                    if (addr >= image_base && addr < image_base + 0x10000000) {
                        std::stringstream ss;
                        ss << std::hex << (addr - image_base);
                        return exe_path.filename().string() + "+0x" + ss.str();
                    }
                    for (const auto& dll : loaded_dlls) {
                        if (addr >= dll.base && addr < dll.base + 0x10000000) {
                            std::stringstream ss;
                            ss << std::hex << (addr - dll.base);
                            return dll.name + "+0x" + ss.str();
                        }
                    }
                    return "unmapped / dynamic code";
                };

                std::string exc_name = "UNKNOWN_EXCEPTION";
                std::string tax_code = "RUN-000";
                std::string root_cause;
                std::vector<std::string> checklist;

                if (code == 0xC0000005) {
                    exc_name = "STATUS_ACCESS_VIOLATION";
                    tax_code = "RUN-001";
                    root_cause = "Invalid memory read, write, or DEP/NX execution attempt.";
                    checklist.push_back("Verify null-pointer safeguards before dereferencing object handles or pointers.");
                    checklist.push_back("Ensure executable memory regions have PAGE_EXECUTE_READWRITE permissions if running JIT code.");
                    checklist.push_back("Check dynamic relocation entries and import table resolution in the linker.");
                } else if (code == 0xC0000139) {
                    exc_name = "STATUS_ENTRYPOINT_NOT_FOUND";
                    tax_code = "DLL-001";
                    root_cause = "A required dynamic symbol or DLL entrypoint could not be located in imported libraries.";
                    checklist.push_back("Inspect missing DLL dependencies using 'dgtl-debugger inspect-exe <target.exe>'.");
                    checklist.push_back("Confirm that the target DLL exists in the executable's directory or the system PATH.");
                    checklist.push_back("Check for mangled C++ name mismatches between export and import tables.");
                } else if (code == 0xC000001D) {
                    exc_name = "STATUS_ILLEGAL_INSTRUCTION";
                    tax_code = "CPU-001";
                    root_cause = "CPU attempted to execute an invalid, unsupported, or misaligned instruction opcode.";
                    checklist.push_back("Verify compiler target ISA architecture matches host CPU capabilities (e.g. AVX2, AVX-512, BMI2).");
                    checklist.push_back("Check machine code emitter encoding offsets and instruction length calculations.");
                    checklist.push_back("Confirm code section is not executing non-code data or unpadded jump tables.");
                } else if (code == 0xC0000094) {
                    exc_name = "STATUS_INTEGER_DIVIDE_BY_ZERO";
                    tax_code = "RUN-002";
                    root_cause = "Hardware division instruction encountered a divisor operand of zero.";
                    checklist.push_back("Add defensive zero checks prior to division (IDIV/DIV) operations.");
                    checklist.push_back("Inspect registers RAX, RDX, and divisor register at faulting RIP.");
                } else if (code == 0xC00000FD) {
                    exc_name = "STATUS_STACK_OVERFLOW";
                    tax_code = "RUN-003";
                    root_cause = "Thread consumed all allocated stack space; stack guard page exhausted.";
                    checklist.push_back("Check for unbounded or circular recursion in call graphs.");
                    checklist.push_back("Increase reserved stack size via linker flag /STACK or investigate oversized local stack buffers.");
                } else if (code == 0x80000003) {
                    exc_name = "STATUS_BREAKPOINT";
                    tax_code = "TRAP-001";
                    root_cause = "A software breakpoint instruction (INT3) was executed.";
                } else if (code == 0x80000004) {
                    exc_name = "STATUS_SINGLE_STEP";
                    tax_code = "TRAP-002";
                    root_cause = "Processor single-step trap flag encountered.";
                } else if (code == 0xC000008E) {
                    exc_name = "STATUS_FLOAT_DIVIDE_BY_ZERO";
                    tax_code = "FPU-001";
                    root_cause = "Floating-point division by zero occurred in FPU or SSE/AVX vector unit.";
                } else if (code == 0xC0000090) {
                    exc_name = "STATUS_FLOAT_INVALID_OPERATION";
                    tax_code = "FPU-002";
                    root_cause = "Floating-point invalid operation exception (e.g., NaN generation without mask).";
                } else if (code == 0xC0000096) {
                    exc_name = "STATUS_PRIVILEGED_INSTRUCTION";
                    tax_code = "CPU-002";
                    root_cause = "Target application attempted to execute a ring-0 privileged CPU instruction.";
                } else if (code == 0xC0000025) {
                    exc_name = "STATUS_NONCONTINUABLE_EXCEPTION";
                    tax_code = "RUN-004";
                    root_cause = "Exception handler returned exception continue execution on a non-continuable exception.";
                }

                uint64_t fault_rip = reinterpret_cast<uint64_t>(rec.ExceptionAddress);
                uint64_t fault_addr = 0;
                uint64_t access_type = 0;
                bool has_fault_addr = false;

                if (code == 0xC0000005 && rec.NumberParameters >= 2) {
                    has_fault_addr = true;
                    access_type = rec.ExceptionInformation[0];
                    fault_addr = rec.ExceptionInformation[1];
                }

                // Collect System Telemetry
                SYSTEM_INFO sys_info{};
                GetSystemInfo(&sys_info);

                // Collect Process Memory Telemetry
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                BOOL has_pmc = GetProcessMemoryInfo(pi.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));

                // Open Thread & Collect CPU Registers
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_ALL;
                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, dbg.dwThreadId);
                BOOL has_ctx = (hThread && GetThreadContext(hThread, &ctx));

                // Virtual memory query at fault address
                MEMORY_BASIC_INFORMATION mbi{};
                bool has_mbi = false;
                if (has_fault_addr && VirtualQueryEx(pi.hProcess, reinterpret_cast<LPCVOID>(fault_addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
                    has_mbi = true;
                    if (fault_addr == 0 || fault_addr < 0x1000) {
                        root_cause = "NULL pointer dereference (address = 0x" + ([&]{
                            std::stringstream ss; ss << std::hex << fault_addr; return ss.str();
                        })() + "). Attempted to access unallocated zero-page memory.";
                    } else if (mbi.State != MEM_COMMIT) {
                        root_cause = "Access to uncommitted virtual memory (address = 0x" + ([&]{
                            std::stringstream ss; ss << std::hex << fault_addr; return ss.str();
                        })() + ") outside process allocation bounds.";
                    } else if (access_type == 8) {
                        root_cause = "Data Execution Prevention (DEP / W^X) violation. Attempted to execute code in a non-executable page.";
                    }
                }

                // ============================================================
                // 1. BANNER & PRIMARY FAULT METRICS
                // ============================================================
                std::cout << "\n"
                          << Terminal::bg_red() << Terminal::bold()
                          << " ============================================================================== " << Terminal::reset() << "\n"
                          << Terminal::bg_red() << Terminal::bold()
                          << "   CRITICAL HARDWARE EXCEPTION TRAPPED  [" << std::left << std::setw(10) << tax_code << "]                     " << Terminal::reset() << "\n"
                          << Terminal::bg_red() << Terminal::bold()
                          << " ============================================================================== " << Terminal::reset() << "\n\n";

                std::cout << "  " << Terminal::bold() << "Exception Type     : " << Terminal::reset()
                          << Terminal::red() << Terminal::bold() << exc_name << " (0x" << std::hex << code << std::dec << ")" << Terminal::reset() << "\n";
                std::cout << "  " << Terminal::bold() << "Faulting Instruction: " << Terminal::reset()
                          << "0x" << std::hex << fault_rip << std::dec
                          << " (" << Terminal::cyan() << resolve_addr_owner(fault_rip) << Terminal::reset() << ")\n";

                if (has_fault_addr) {
                    const char* acc_str = (access_type == 0 ? "READ" : (access_type == 1 ? "WRITE" : "EXECUTE (DEP Violation)"));
                    std::cout << "  " << Terminal::bold() << "Memory Operation   : " << Terminal::reset()
                              << Terminal::yellow() << acc_str << Terminal::reset()
                              << " at address 0x" << std::hex << fault_addr << std::dec << "\n";
                }

                std::cout << "  " << Terminal::bold() << "Root Cause Analysis: " << Terminal::reset()
                          << root_cause << "\n\n";

                // ============================================================
                // 2. PROCESS & SYSTEM TELEMETRY CARD
                // ============================================================
                std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: PROCESS EXECUTION & SYSTEM PROFILE ]" << Terminal::reset() << "\n";
                std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";
                std::cout << "    Process PID      : " << std::left << std::setw(16) << dbg.dwProcessId
                          << "Thread ID        : " << dbg.dwThreadId << "\n";
                std::cout << "    Execution Uptime : " << std::left << std::setw(16) << (std::to_string(elapsed_ms) + " ms")
                          << "Total Microsecs  : " << elapsed_us << " us\n";
                std::cout << "    Host Architecture: " << std::left << std::setw(16) << (sys_info.wProcessorArchitecture == 9 ? "AMD64 (x86_64)" : (sys_info.wProcessorArchitecture == 12 ? "ARM64" : "x86"))
                          << "Processor Count  : " << sys_info.dwNumberOfProcessors << " logical cores\n";
                std::cout << "    System Page Size : " << std::left << std::setw(16) << (std::to_string(sys_info.dwPageSize) + " bytes")
                          << "App Address Min  : 0x" << std::hex << reinterpret_cast<uint64_t>(sys_info.lpMinimumApplicationAddress) << std::dec << "\n";
                std::cout << "    App Address Max  : 0x" << std::hex << reinterpret_cast<uint64_t>(sys_info.lpMaximumApplicationAddress) << std::dec << "\n";
                std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";

                // ============================================================
                // 3. PROCESS MEMORY METRICS & QUOTAS
                // ============================================================
                if (has_pmc) {
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: MEMORY CONSUMPTION & PAGE FAULTS ]" << Terminal::reset() << "\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";
                    double ws_mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
                    double peak_ws_mb = pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
                    double pf_mb = pmc.PagefileUsage / (1024.0 * 1024.0);
                    double peak_pf_mb = pmc.PeakPagefileUsage / (1024.0 * 1024.0);
                    std::cout << "    Working Set (RAM): " << std::fixed << std::setprecision(2) << std::left << std::setw(10) << ws_mb << " MB  "
                              << "Peak Working Set : " << std::left << std::setw(10) << peak_ws_mb << " MB\n";
                    std::cout << "    Commit (Pagefile): " << std::left << std::setw(10) << pf_mb << " MB  "
                              << "Peak Commit      : " << std::left << std::setw(10) << peak_pf_mb << " MB\n";
                    std::cout << "    Page Fault Count : " << std::left << std::setw(10) << pmc.PageFaultCount
                              << "Paged Pool Quota : " << (pmc.QuotaPagedPoolUsage / 1024) << " KB (Peak: " << (pmc.QuotaPeakPagedPoolUsage / 1024) << " KB)\n";
                    std::cout << "    NonPaged Pool    : " << (pmc.QuotaNonPagedPoolUsage / 1024) << " KB (Peak: " << (pmc.QuotaPeakNonPagedPoolUsage / 1024) << " KB)\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";
                }

                // ============================================================
                // 4. FAULT ADDRESS MEMORY PAGE FORENSICS
                // ============================================================
                if (has_fault_addr && has_mbi) {
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: FAULT ADDRESS VIRTUAL PAGE FORENSICS ]" << Terminal::reset() << "\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";
                    const char* state_str = (mbi.State == MEM_COMMIT) ? "COMMITTED" : ((mbi.State == MEM_RESERVE) ? "RESERVED" : "FREE (UNMAPPED)");
                    const char* type_str = (mbi.Type == MEM_IMAGE) ? "IMAGE (PE Section)" : ((mbi.Type == MEM_MAPPED) ? "MAPPED VIEW" : ((mbi.Type == MEM_PRIVATE) ? "PRIVATE HEAP/STACK" : "NONE"));

                    auto decode_protect = [](DWORD p) -> std::string {
                        std::string res;
                        if (p & PAGE_NOACCESS) res += "PAGE_NOACCESS ";
                        if (p & PAGE_READONLY) res += "PAGE_READONLY ";
                        if (p & PAGE_READWRITE) res += "PAGE_READWRITE ";
                        if (p & PAGE_WRITECOPY) res += "PAGE_WRITECOPY ";
                        if (p & PAGE_EXECUTE) res += "PAGE_EXECUTE ";
                        if (p & PAGE_EXECUTE_READ) res += "PAGE_EXECUTE_READ ";
                        if (p & PAGE_EXECUTE_READWRITE) res += "PAGE_EXECUTE_READWRITE ";
                        if (p & PAGE_GUARD) res += "[GUARD_PAGE] ";
                        if (p & PAGE_NOCACHE) res += "[NOCACHE] ";
                        return res.empty() ? ("0x" + ([&]{ std::stringstream ss; ss << std::hex << p; return ss.str(); })()) : res;
                    };

                    std::cout << "    Target Address   : 0x" << std::hex << fault_addr << std::dec << "\n";
                    std::cout << "    Region Base      : 0x" << std::hex << reinterpret_cast<uint64_t>(mbi.BaseAddress)
                              << " | AllocationBase=0x" << reinterpret_cast<uint64_t>(mbi.AllocationBase) << std::dec << "\n";
                    std::cout << "    Region Size      : 0x" << std::hex << mbi.RegionSize << std::dec << " (" << (mbi.RegionSize / 1024) << " KB)\n";
                    std::cout << "    Allocation State : " << state_str << " | Region Type: " << type_str << "\n";
                    std::cout << "    Page Protection  : " << decode_protect(mbi.Protect) << " (AllocProtect: " << decode_protect(mbi.AllocationProtect) << ")\n";

                    // Memory Hex + ASCII Dump around fault address (if committed)
                    if (mbi.State == MEM_COMMIT && fault_addr > 0x1000) {
                        uint64_t dump_start = (fault_addr >= 32) ? (fault_addr - 32) : 0;
                        dump_start &= ~0xFULL; // 16-byte align
                        uint8_t mem_buf[64] = {0};
                        SIZE_T bytes_read = 0;
                        if (ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(dump_start), mem_buf, sizeof(mem_buf), &bytes_read) && bytes_read > 0) {
                            std::cout << "    Hex Memory Dump around Fault Address:\n";
                            for (size_t r = 0; r < bytes_read; r += 16) {
                                uint64_t line_addr = dump_start + r;
                                std::cout << "      0x" << std::hex << std::setw(16) << std::setfill('0') << line_addr << ": ";
                                for (size_t c = 0; c < 16; ++c) {
                                    if (r + c < bytes_read) {
                                        if (line_addr + c == fault_addr) {
                                            std::cout << Terminal::red() << Terminal::bold() << std::hex << std::setw(2) << std::setfill('0') << (int)mem_buf[r + c] << Terminal::reset() << " ";
                                        } else {
                                            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mem_buf[r + c] << " ";
                                        }
                                    } else {
                                        std::cout << "   ";
                                    }
                                }
                                std::cout << " | ";
                                for (size_t c = 0; c < 16 && (r + c) < bytes_read; ++c) {
                                    uint8_t b = mem_buf[r + c];
                                    std::cout << ((b >= 32 && b <= 126) ? (char)b : '.');
                                }
                                std::cout << "\n" << std::dec;
                            }
                        }
                    }
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";
                }

                // ============================================================
                // 5. FULL 64-BIT GPR REGISTER TELEMETRY
                // ============================================================
                if (has_ctx) {
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: CPU GENERAL-PURPOSE REGISTERS (GPR) ]" << Terminal::reset() << "\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";

                    auto fmt_reg = [](const char* name, uint64_t val) {
                        std::stringstream ss;
                        ss << "  " << Terminal::bold() << std::left << std::setw(4) << name << Terminal::reset()
                           << " = 0x" << std::hex << std::setw(16) << std::setfill('0') << val << std::dec;
                        return ss.str();
                    };

                    std::cout << fmt_reg("RAX", ctx.Rax) << "  " << fmt_reg("RBX", ctx.Rbx) << "  " << fmt_reg("RCX", ctx.Rcx) << "\n";
                    std::cout << fmt_reg("RDX", ctx.Rdx) << "  " << fmt_reg("RSI", ctx.Rsi) << "  " << fmt_reg("RDI", ctx.Rdi) << "\n";
                    std::cout << fmt_reg("RBP", ctx.Rbp) << "  " << fmt_reg("RSP", ctx.Rsp) << "  " << fmt_reg("RIP", ctx.Rip) << "\n";
                    std::cout << fmt_reg("R8 ", ctx.R8)  << "  " << fmt_reg("R9 ", ctx.R9)  << "  " << fmt_reg("R10", ctx.R10) << "\n";
                    std::cout << fmt_reg("R11", ctx.R11) << "  " << fmt_reg("R12", ctx.R12) << "  " << fmt_reg("R13", ctx.R13) << "\n";
                    std::cout << fmt_reg("R14", ctx.R14) << "  " << fmt_reg("R15", ctx.R15) << "  "
                              << "  " << Terminal::bold() << std::left << std::setw(4) << "EFL" << Terminal::reset()
                              << " = 0x" << std::hex << std::setw(8) << std::setfill('0') << ctx.EFlags << std::dec << "          \n";

                    bool rsp_aligned = ((ctx.Rsp & 0xF) == 8 || (ctx.Rsp & 0xF) == 0);
                    if (!rsp_aligned) {
                        std::cout << "  " << Terminal::dim() << "|" << Terminal::reset()
                                  << "  " << Terminal::yellow() << "[!] WARNING: RSP (0x" << std::hex << ctx.Rsp << std::dec
                                  << ") is not 16-byte aligned (RSP % 16 = " << (ctx.Rsp & 0xF) << "), violating Windows x64 ABI!"
                                  << Terminal::reset() << "\n";
                    }
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";

                    // ============================================================
                    // 6. SEGMENT REGISTERS & PROCESSOR FLAGS DECOMPOSITION
                    // ============================================================
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: SEGMENT REGISTERS & PROCESSOR FLAGS (EFLAGS / MXCSR) ]" << Terminal::reset() << "\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";
                    std::cout << "    Segments : "
                              << "CS=0x" << std::hex << std::setw(4) << std::setfill('0') << ctx.SegCs << "  "
                              << "DS=0x" << std::setw(4) << std::setfill('0') << ctx.SegDs << "  "
                              << "ES=0x" << std::setw(4) << std::setfill('0') << ctx.SegEs << "  "
                              << "FS=0x" << std::setw(4) << std::setfill('0') << ctx.SegFs << "  "
                              << "GS=0x" << std::setw(4) << std::setfill('0') << ctx.SegGs << "  "
                              << "SS=0x" << std::setw(4) << std::setfill('0') << ctx.SegSs << std::dec << "\n";

                    // EFLAGS breakdown
                    uint32_t ef = ctx.EFlags;
                    std::cout << "    EFLAGS   : 0x" << std::hex << ef << std::dec << " [ "
                              << "CF=" << ((ef >> 0) & 1) << " "
                              << "PF=" << ((ef >> 2) & 1) << " "
                              << "AF=" << ((ef >> 4) & 1) << " "
                              << "ZF=" << ((ef >> 6) & 1) << " "
                              << "SF=" << ((ef >> 7) & 1) << " "
                              << "TF=" << ((ef >> 8) & 1) << " "
                              << "IF=" << ((ef >> 9) & 1) << " "
                              << "DF=" << ((ef >> 10) & 1) << " "
                              << "OF=" << ((ef >> 11) & 1) << " "
                              << "IOPL=" << ((ef >> 12) & 3) << " "
                              << "NT=" << ((ef >> 14) & 1) << " "
                              << "RF=" << ((ef >> 16) & 1) << " "
                              << "ID=" << ((ef >> 21) & 1) << " ]\n";

                    // MXCSR breakdown
                    uint32_t mx = ctx.MxCsr;
                    const char* round_modes[] = {"Nearest(00)", "Down(01)", "Up(10)", "Truncate(11)"};
                    int round_idx = (mx >> 13) & 3;
                    std::cout << "    MXCSR    : 0x" << std::hex << mx << std::dec << " [ "
                              << "Flags: IE=" << ((mx >> 0) & 1) << " DE=" << ((mx >> 1) & 1) << " ZE=" << ((mx >> 2) & 1)
                              << " OE=" << ((mx >> 3) & 1) << " UE=" << ((mx >> 4) & 1) << " PE=" << ((mx >> 5) & 1) << " DAZ=" << ((mx >> 6) & 1) << " | "
                              << "Masks: IM=" << ((mx >> 7) & 1) << " DM=" << ((mx >> 8) & 1) << " ZM=" << ((mx >> 9) & 1)
                              << " OM=" << ((mx >> 10) & 1) << " UM=" << ((mx >> 11) & 1) << " PM=" << ((mx >> 12) & 1) << " | "
                              << "Round=" << round_modes[round_idx] << " FTZ=" << ((mx >> 15) & 1) << " ]\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";

                    // ============================================================
                    // 7. SIMD / VECTOR REGISTERS (XMM0 - XMM15)
                    // ============================================================
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: 128-BIT SIMD / VECTOR REGISTERS (XMM0 - XMM15) ]" << Terminal::reset() << "\n";
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n";
                    for (int i = 0; i < 16; i += 2) {
                        for (int col = 0; col < 2; ++col) {
                            int reg_idx = i + col;
                            const auto& xmm = ctx.FltSave.XmmRegisters[reg_idx];
                            uint64_t low = xmm.Low;
                            uint64_t high = static_cast<uint64_t>(xmm.High);
                            const float* fvals = reinterpret_cast<const float*>(&xmm);
                            std::cout << "    " << Terminal::bold() << "XMM" << std::setw(2) << std::setfill('0') << reg_idx << Terminal::reset()
                                      << "=[0x" << std::hex << std::setw(16) << std::setfill('0') << high
                                      << ":" << std::setw(16) << std::setfill('0') << low << std::dec << "] "
                                      << "{" << std::setprecision(1) << fvals[0] << "," << fvals[1] << "," << fvals[2] << "," << fvals[3] << "}  ";
                        }
                        std::cout << "\n";
                    }
                    std::cout << "  " << Terminal::dim() << "+-----------------------------------------------------------------------------------------+" << Terminal::reset() << "\n\n";

                    // ============================================================
                    // 8. DISASSEMBLY AT FAULTING RIP
                    // ============================================================
                    std::cout << "  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: CODE DISASSEMBLY AT FAULTING RIP ]" << Terminal::reset() << "\n";
                    uint8_t code_buf[64] = {0};
                    SIZE_T read_bytes = 0;
                    if (ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(ctx.Rip), code_buf, sizeof(code_buf), &read_bytes) && read_bytes > 0) {
                        uint64_t cur_rip = ctx.Rip;
                        size_t cur_off = 0;
                        for (int i = 0; i < 6 && cur_off < read_bytes; ++i) {
                            auto instr = X64Disassembler::decode(code_buf + cur_off, read_bytes - cur_off, cur_rip);
                            if (i == 0) {
                                std::cout << "  " << Terminal::bold() << Terminal::red() << "  ---> 0x" << std::hex << std::setw(16) << std::setfill('0') << cur_rip << ": " << Terminal::reset();
                            } else {
                                std::cout << "       0x" << std::hex << std::setw(16) << std::setfill('0') << cur_rip << ": ";
                            }

                            std::stringstream bstream;
                            for (size_t b = 0; b < 6; ++b) {
                                if (b < instr.length) {
                                    bstream << std::hex << std::setw(2) << std::setfill('0') << (int)code_buf[cur_off + b] << " ";
                                } else {
                                    bstream << "   ";
                                }
                            }
                            std::cout << Terminal::dim() << bstream.str() << Terminal::reset() << "  ";
                            if (i == 0) {
                                std::cout << Terminal::bold() << Terminal::red() << instr.text
                                          << "  <-- [FAULT]" << Terminal::reset() << "\n" << std::dec;
                            } else {
                                std::cout << instr.text << "\n" << std::dec;
                            }
                            cur_off += instr.length;
                            cur_rip += instr.length;
                        }
                    } else {
                        std::cout << "    " << Terminal::dim() << "(Unable to read process memory at RIP - page uncommitted or unreadable)" << Terminal::reset() << "\n";
                    }

                    // ============================================================
                    // 9. CALL STACK TRACE & FRAME UNWINDING (TOP 24 FRAMES)
                    // ============================================================
                    std::cout << "\n  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: CALL STACK UNWIND & MEMORY BACKTRACE ]" << Terminal::reset() << "\n";
                    uint64_t stack_slots[32] = {0};
                    if (ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(ctx.Rsp), stack_slots, sizeof(stack_slots), &read_bytes)) {
                        size_t count = std::min<size_t>(read_bytes / 8, 24);
                        for (size_t i = 0; i < count; ++i) {
                            uint64_t val = stack_slots[i];
                            std::string owner = resolve_addr_owner(val);
                            // Preview printable ASCII
                            std::string ascii_prev;
                            const uint8_t* pval = reinterpret_cast<const uint8_t*>(&val);
                            for (size_t b = 0; b < 8; ++b) {
                                ascii_prev += (pval[b] >= 32 && pval[b] <= 126) ? (char)pval[b] : '.';
                            }

                            std::cout << "    #" << std::setw(2) << i << "  [RSP+" << std::setw(3) << (i * 8) << "]  0x"
                                      << std::hex << std::setw(16) << std::setfill('0') << val << std::dec
                                      << " [" << ascii_prev << "] "
                                      << Terminal::dim() << "in" << Terminal::reset() << " " << Terminal::cyan() << owner << Terminal::reset() << "\n";
                        }
                    }

                    CloseHandle(hThread);
                }

                // ============================================================
                // 10. LOADED MODULE INVENTORY
                // ============================================================
                std::cout << "\n  " << Terminal::bold() << Terminal::cyan() << "[ TELEMETRY: LOADED MODULES & DEPENDENCY INVENTORY ]" << Terminal::reset() << "\n";
                for (const auto& dll : loaded_dlls) {
                    std::cout << "    0x" << std::hex << std::setw(16) << std::setfill('0') << dll.base << std::dec
                              << " | " << dll.name << "\n";
                }

                // ============================================================
                // 11. ACTIONABLE REMEDIATION CHECKLIST
                // ============================================================
                if (!checklist.empty()) {
                    std::cout << "\n  " << Terminal::bold() << Terminal::green() << "[ ACTIONABLE REMEDIATION CHECKLIST ]" << Terminal::reset() << "\n";
                    for (const auto& item : checklist) {
                        std::cout << "    " << Terminal::cyan() << "[ ]" << Terminal::reset() << " " << item << "\n";
                    }
                }

                // ============================================================
                // 12. AUTOMATIC STRUCTURED JSON TELEMETRY EXPORT
                // ============================================================
                auto epoch_now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::string json_filename = "crash_telemetry_pid" + std::to_string(dbg.dwProcessId) + "_" + std::to_string(epoch_now) + ".json";
                std::ofstream json_out(json_filename);
                if (json_out.is_open()) {
                    json_out << "{\n"
                             << "  \"telemetry_version\": \"2.0-enterprise\",\n"
                             << "  \"process\": {\n"
                             << "    \"pid\": " << dbg.dwProcessId << ",\n"
                             << "    \"tid\": " << dbg.dwThreadId << ",\n"
                             << "    \"executable\": \"" << exe_path.string() << "\",\n"
                             << "    \"uptime_ms\": " << elapsed_ms << ",\n"
                             << "    \"uptime_us\": " << elapsed_us << "\n"
                             << "  },\n"
                             << "  \"exception\": {\n"
                             << "    \"code\": " << code << ",\n"
                             << "    \"code_hex\": \"0x" << std::hex << code << std::dec << "\",\n"
                             << "    \"name\": \"" << exc_name << "\",\n"
                             << "    \"taxonomy\": \"" << tax_code << "\",\n"
                             << "    \"fault_rip\": \"0x" << std::hex << fault_rip << std::dec << "\",\n"
                             << "    \"fault_owner\": \"" << resolve_addr_owner(fault_rip) << "\",\n"
                             << "    \"has_memory_fault\": " << (has_fault_addr ? "true" : "false") << ",\n"
                             << "    \"memory_access_type\": " << access_type << ",\n"
                             << "    \"fault_address\": \"0x" << std::hex << fault_addr << std::dec << "\",\n"
                             << "    \"root_cause\": \"" << root_cause << "\"\n"
                             << "  },\n";

                    if (has_pmc) {
                        json_out << "  \"memory_counters\": {\n"
                                 << "    \"working_set_bytes\": " << pmc.WorkingSetSize << ",\n"
                                 << "    \"peak_working_set_bytes\": " << pmc.PeakWorkingSetSize << ",\n"
                                 << "    \"pagefile_bytes\": " << pmc.PagefileUsage << ",\n"
                                 << "    \"peak_pagefile_bytes\": " << pmc.PeakPagefileUsage << ",\n"
                                 << "    \"page_faults\": " << pmc.PageFaultCount << "\n"
                                 << "  },\n";
                    }

                    if (has_ctx) {
                        json_out << "  \"registers\": {\n"
                                 << "    \"rax\": \"0x" << std::hex << ctx.Rax << "\",\n"
                                 << "    \"rbx\": \"0x" << ctx.Rbx << "\",\n"
                                 << "    \"rcx\": \"0x" << ctx.Rcx << "\",\n"
                                 << "    \"rdx\": \"0x" << ctx.Rdx << "\",\n"
                                 << "    \"rsi\": \"0x" << ctx.Rsi << "\",\n"
                                 << "    \"rdi\": \"0x" << ctx.Rdi << "\",\n"
                                 << "    \"rbp\": \"0x" << ctx.Rbp << "\",\n"
                                 << "    \"rsp\": \"0x" << ctx.Rsp << "\",\n"
                                 << "    \"rip\": \"0x" << ctx.Rip << "\",\n"
                                 << "    \"r8\": \"0x"  << ctx.R8  << "\",\n"
                                 << "    \"r9\": \"0x"  << ctx.R9  << "\",\n"
                                 << "    \"r10\": \"0x\"" << ctx.R10 << "\",\n"
                                 << "    \"r11\": \"0x\"" << ctx.R11 << "\",\n"
                                 << "    \"r12\": \"0x\"" << ctx.R12 << "\",\n"
                                 << "    \"r13\": \"0x\"" << ctx.R13 << "\",\n"
                                 << "    \"r14\": \"0x\"" << ctx.R14 << "\",\n"
                                 << "    \"r15\": \"0x\"" << ctx.R15 << "\",\n"
                                 << "    \"eflags\": \"0x\"" << ctx.EFlags << "\",\n"
                                 << "    \"mxcsr\": \"0x\"" << ctx.MxCsr << std::dec << "\"\n"
                                 << "  },\n";
                    }

                    json_out << "  \"loaded_modules_count\": " << loaded_dlls.size() << "\n"
                             << "}\n";
                    json_out.close();

                    std::cout << "\n  " << Terminal::green() << Terminal::bold() << "[ TELEMETRY EXPORT ]" << Terminal::reset()
                              << " Structured telemetry persisted to: " << Terminal::bold() << json_filename << Terminal::reset() << "\n";
                }

                std::cout << "\n"
                          << Terminal::bg_red() << Terminal::bold()
                          << " ============================================================================== " << Terminal::reset() << "\n\n";

                TerminateProcess(pi.hProcess, code);
                running = false;
                exit_code = code;
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT: {
                exit_code = dbg.u.ExitProcess.dwExitCode;
                running = false;
                auto exec_end = std::chrono::steady_clock::now();
                int64_t total_us = std::chrono::duration_cast<std::chrono::microseconds>(exec_end - exec_start_time).count();
                double total_ms = total_us / 1000.0;
                std::cout << "\n" << Terminal::dim() << "[dgtl-debugger]" << Terminal::reset()
                          << " Process execution finished in " << std::fixed << std::setprecision(2) << total_ms << " ms.\n";
                if (exit_code == 0) {
                    std::cout << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset()
                              << " Target application terminated normally with exit code 0.\n\n";
                } else {
                    std::cout << "  " << Terminal::yellow() << Terminal::bold() << "[ EXIT ]" << Terminal::reset()
                              << " Target application terminated with non-zero exit code: " << exit_code
                              << " (0x" << std::hex << exit_code << std::dec << ")\n\n";
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
        std::cout << "\n"
                  << Terminal::bg_blue() << Terminal::bold()
                  << " ============================================================================== " << Terminal::reset() << "\n"
                  << Terminal::bg_blue() << Terminal::bold()
                  << "   DGTL POSIX EXECUTION & SIGNAL TRAP MONITOR                                   " << Terminal::reset() << "\n"
                  << Terminal::bg_blue() << Terminal::bold()
                  << " ============================================================================== " << Terminal::reset() << "\n\n";
        std::cout << "  " << Terminal::bold() << "Target Executable : " << Terminal::reset() << exe_path.string() << "\n\n";

        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << Terminal::red() << "[dgtl-debugger] Failed to fork child process: " << strerror(errno) << Terminal::reset() << "\n";
            return 1;
        }

        if (pid == 0) {
            std::vector<char*> child_argv;
            for (int i = 1; i < argc; ++i) {
                child_argv.push_back(argv[i]);
            }
            child_argv.push_back(nullptr);
            execv(exe_path.c_str(), child_argv.data());
            std::cerr << "[dgtl-debugger] Failed to execv target: " << strerror(errno) << "\n";
            _exit(127);
        }

        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                std::cout << "  " << Terminal::green() << Terminal::bold() << "[ PASS ]" << Terminal::reset()
                          << " Target process exited normally with return code 0.\n\n";
            } else {
                std::cout << "  " << Terminal::yellow() << Terminal::bold() << "[ EXIT ]" << Terminal::reset()
                          << " Target process exited with non-zero code: " << exit_code << "\n\n";
            }
            return exit_code;
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            std::string sig_name = "Signal " + std::to_string(sig);
            std::string tax_code = "SIG-000";
            std::string diagnosis = "Process terminated abruptly by unhandled POSIX signal.";
            std::vector<std::string> checklist;

            if (sig == SIGSEGV) {
                sig_name = "SIGSEGV (Segmentation Fault)";
                tax_code = "RUN-001";
                diagnosis = "Process accessed unmapped virtual memory or dereferenced a null/wild pointer.";
                checklist.push_back("Check for NULL pointer dereference or uninitialized memory pointers.");
                checklist.push_back("Ensure memory segments have PROT_READ / PROT_WRITE / PROT_EXEC appropriately.");
                checklist.push_back("Verify stack bounds and buffer allocations.");
            } else if (sig == SIGFPE) {
                sig_name = "SIGFPE (Floating Point / Arithmetic Exception)";
                tax_code = "RUN-002";
                diagnosis = "Hardware arithmetic exception, typically division by zero or integer overflow.";
                checklist.push_back("Add guards before integer division operations to prevent divisor == 0.");
                checklist.push_back("Check floating point control word and division operands.");
            } else if (sig == SIGILL) {
                sig_name = "SIGILL (Illegal Instruction)";
                tax_code = "CPU-001";
                diagnosis = "CPU attempted to execute an illegal, malformed, or unpermitted instruction opcode.";
                checklist.push_back("Verify target ISA architecture and CPU flag flags match the host processor.");
                checklist.push_back("Ensure instruction encoding logic in the machine code emitter is accurate.");
            } else if (sig == SIGBUS) {
                sig_name = "SIGBUS (Bus Error / Unaligned Access)";
                tax_code = "RUN-004";
                diagnosis = "Process attempted an unaligned memory access on an architecture requiring strict alignment, or physical memory fault.";
                checklist.push_back("Enforce natural alignment on multi-byte loads and stores.");
                checklist.push_back("Verify memory-mapped file bounds and underlying file descriptor validities.");
            } else if (sig == SIGABRT) {
                sig_name = "SIGABRT (Abnormal Process Abort)";
                tax_code = "RUN-005";
                diagnosis = "Process called abort(), or an unhandled C++ exception / assertion triggered termination.";
                checklist.push_back("Check application assertion statements and diagnostic output preceding the crash.");
                checklist.push_back("Inspect C++ exception handling and ensure all thrown exceptions are caught.");
            }

            std::cout << "\n"
                      << Terminal::bg_red() << Terminal::bold()
                      << " ============================================================================== " << Terminal::reset() << "\n"
                      << Terminal::bg_red() << Terminal::bold()
                      << "   FATAL POSIX SIGNAL INTERCEPTED  [" << tax_code << "]                                    " << Terminal::reset() << "\n"
                      << Terminal::bg_red() << Terminal::bold()
                      << " ============================================================================== " << Terminal::reset() << "\n\n";

            std::cout << "  " << Terminal::bold() << "Trapped Signal   : " << Terminal::reset()
                      << Terminal::red() << Terminal::bold() << sig_name << " (Signal " << sig << ")" << Terminal::reset() << "\n";
            std::cout << "  " << Terminal::bold() << "Diagnosis        : " << Terminal::reset() << diagnosis << "\n\n";

            if (!checklist.empty()) {
                std::cout << "  " << Terminal::bold() << Terminal::green() << "Actionable Remediation Checklist:" << Terminal::reset() << "\n";
                for (const auto& item : checklist) {
                    std::cout << "    " << Terminal::cyan() << "[ ]" << Terminal::reset() << " " << item << "\n";
                }
                std::cout << "\n";
            }

            std::cout << Terminal::bg_red() << Terminal::bold()
                      << " ============================================================================== " << Terminal::reset() << "\n\n";
            return 128 + sig;
        }

        return 1;
#endif
    }
};

} // namespace dgtl::debug

int dgtl_debugger_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as) {
    Terminal::init(true);

    std::vector<std::string> expanded = dgtl::tools::ArgExpander::expand(raw_args);
    if (expanded.empty()) {
        expanded.push_back(std::string(invoked_as));
    } else {
        expanded.insert(expanded.begin(), std::string(invoked_as));
    }

    int argc = static_cast<int>(expanded.size());
    std::vector<char*> c_argv;
    c_argv.reserve(expanded.size());
    for (auto& s : expanded) {
        c_argv.push_back(s.data());
    }
    char** argv = c_argv.data();

    if (argc >= 2) {
        std::string_view subcmd = argv[1];
        if (subcmd == "run" || subcmd == "exec") {
            return dgtl::debug::NativeDebugger::run(argc - 1, argv + 1);
        }
        if (subcmd == "inspect-exe" || subcmd == "check-exe" || subcmd == "audit-bin" || subcmd == "inspect-bin") {
            if (argc < 3) {
                std::cerr << "Usage: dgtl-debugger inspect-exe <path_to_binary>\n";
                return 1;
            }
            return dgtl::debug::PeInspector::audit(argv[2]) ? 0 : 1;
        }
        if (subcmd == "inspect" || subcmd == "open" || subcmd == "view") {
            if (argc < 3) {
                std::cerr << "Usage: dgtl-debugger " << subcmd << " <file_path>\n";
                return 1;
            }
            return dgtl::debug::FileInspector::open_and_inspect(argv[2]) ? 0 : 1;
        }
        if (!subcmd.starts_with("-") && fs::exists(subcmd) && fs::is_regular_file(subcmd)) {
            if (subcmd.ends_with(".exe") || subcmd.ends_with(".elf") || subcmd.ends_with(".macho")) {
                return dgtl::debug::NativeDebugger::run(argc, argv);
            } else {
                return dgtl::debug::FileInspector::open_and_inspect(subcmd) ? 0 : 1;
            }
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

    Terminal::init(opts.color);

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