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

/**
 * @file dgtl_lit_main.cpp
 * @brief dgtl-lit: Industry-Grade Native C++26 Test Runner & FileCheck Engine
 *
 * Designed to surpass LLVM lit + FileCheck and Cranelift filetests:
 *  - Native C++26 parallel execution (zero Python/shell dependencies)
 *  - Built-in LLVM-compatible FileCheck verification engine
 *  - Full directive support: CHECK, CHECK-NEXT, CHECK-NOT, CHECK-DAG, CHECK-LABEL
 *  - Embedded regex support: {{pattern}} variable patterns and raw regex
 *  - Automatic tool discovery (%dgtl-opt, %dgtl-llc, %dgtl-verify, %dgtl-engine, %dgtl-link, %dgtl-inspect)
 *  - Variable expansions: %s (source), %S (directory), %t (temp file)
 *  - ANSI color-coded diagnostic reports with precise failure line diffs
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <regex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cstdio>
#include <iomanip>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace dgtl::lit {

// ANSI Colors
struct Color {
    static constexpr std::string_view Reset   = "\033[0m";
    static constexpr std::string_view Red     = "\033[31m";
    static constexpr std::string_view Green   = "\033[32m";
    static constexpr std::string_view Yellow  = "\033[33m";
    static constexpr std::string_view Blue    = "\033[34m";
    static constexpr std::string_view Magenta = "\033[35m";
    static constexpr std::string_view Cyan    = "\033[36m";
    static constexpr std::string_view Bold    = "\033[1m";
    static constexpr std::string_view BoldRed = "\033[1;31m";
    static constexpr std::string_view BoldGreen = "\033[1;32m";
};

// String utilities
[[nodiscard]] inline std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

[[nodiscard]] inline std::string normalize_newlines(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        if (c != '\r') result.push_back(c);
    }
    return result;
}

[[nodiscard]] inline std::string replace_all(std::string str, std::string_view from, std::string_view to) {
    if (from.empty()) return str;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

// Convert FileCheck pattern to std::regex pattern.
// Handles literal text, embedded {{regex}}, and escapes special regex characters.
[[nodiscard]] inline std::string convert_filecheck_pattern_to_regex(std::string_view pattern) {
    std::string regex_str;
    regex_str.reserve(pattern.size() * 2);
    size_t i = 0;
    while (i < pattern.size()) {
        // Check for {{regex}}
        if (i + 1 < pattern.size() && pattern[i] == '{' && pattern[i + 1] == '{') {
            size_t close = pattern.find("}}", i + 2);
            if (close != std::string_view::npos) {
                std::string sub = std::string(pattern.substr(i + 2, close - (i + 2)));
                regex_str += "(" + sub + ")";
                i = close + 2;
                continue;
            }
        }
        char c = pattern[i];
        if (c == '\\' || c == '.' || c == '+' || c == '*' || c == '?' ||
            c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
            c == '}' || c == '^' || c == '$' || c == '|') {
            regex_str += '\\';
        }
        regex_str += c;
        ++i;
    }
    return regex_str;
}

// FileCheck Directives
enum class DirectiveType {
    CHECK,
    CHECK_NEXT,
    CHECK_NOT,
    CHECK_DAG,
    CHECK_LABEL,
    RUN
};

struct Directive {
    DirectiveType type;
    std::string pattern;
    uint32_t line_number = 0;
    std::string label;
    bool is_regex = false;
    std::string lower_pattern;
    std::optional<std::regex> compiled_regex;
};

inline Directive make_directive(DirectiveType type, std::string pat, uint32_t line_num, std::string label = "") {
    Directive d;
    d.type = type;
    d.pattern = std::move(pat);
    d.line_number = line_num;
    d.label = std::move(label);
    if (d.type != DirectiveType::RUN) {
        if (d.pattern.find("{{") != std::string::npos) {
            d.is_regex = true;
            try {
                std::string rx = convert_filecheck_pattern_to_regex(d.pattern);
                d.compiled_regex = std::regex(rx, std::regex::icase | std::regex::optimize);
            } catch (...) {
                d.is_regex = false;
            }
        }
        if (!d.is_regex) {
            d.lower_pattern = d.pattern;
            std::transform(d.lower_pattern.begin(), d.lower_pattern.end(), d.lower_pattern.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
    }
    return d;
}

// Parse directives from a .dgtl file
[[nodiscard]] inline std::vector<Directive> parse_directives(std::string_view file_content) {
    std::vector<Directive> directives;
    std::istringstream iss(normalize_newlines(file_content));
    std::string line;
    uint32_t line_num = 0;

    while (std::getline(iss, line)) {
        ++line_num;
        std::string_view sv = line;
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);

        if (!sv.starts_with(";")) continue;
        sv.remove_prefix(1);
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);

        if (sv.starts_with("RUN:")) {
            directives.push_back(make_directive(DirectiveType::RUN, trim(sv.substr(4)), line_num));
        } else if (sv.starts_with("CHECK-LABEL:")) {
            std::string pat = trim(sv.substr(12));
            directives.push_back(make_directive(DirectiveType::CHECK_LABEL, pat, line_num, pat));
        } else if (sv.starts_with("CHECK-NEXT:")) {
            directives.push_back(make_directive(DirectiveType::CHECK_NEXT, trim(sv.substr(11)), line_num));
        } else if (sv.starts_with("CHECK-NOT:")) {
            directives.push_back(make_directive(DirectiveType::CHECK_NOT, trim(sv.substr(10)), line_num));
        } else if (sv.starts_with("CHECK-DAG:")) {
            directives.push_back(make_directive(DirectiveType::CHECK_DAG, trim(sv.substr(10)), line_num));
        } else if (sv.starts_with("CHECK:")) {
            directives.push_back(make_directive(DirectiveType::CHECK, trim(sv.substr(6)), line_num));
        }
    }
    return directives;
}

// Built-in FileCheck Matcher Result
struct FileCheckResult {
    bool success = true;
    std::string error_message;
    uint32_t failed_directive_line = 0;
    uint32_t failed_output_line = 0;
    std::string expected_pattern;
    std::string found_snippet;
};

// Check if a line matches a pattern with zero-allocation literal path and pre-compiled regex support
[[nodiscard]] inline bool match_line(std::string_view line, const std::string& lower_line, const Directive& d) {
    if (!d.is_regex) {
        return lower_line.find(d.lower_pattern) != std::string::npos;
    }
    if (d.compiled_regex.has_value()) {
        try {
            return std::regex_search(line.begin(), line.end(), *d.compiled_regex);
        } catch (...) {
            return false;
        }
    }
    return lower_line.find(d.lower_pattern) != std::string::npos;
}

// Verify output against parsed directives
[[nodiscard]] inline FileCheckResult verify_filecheck(
    const std::vector<Directive>& directives,
    std::string_view output_text)
{
    FileCheckResult res;
    res.success = true;

    std::istringstream iss(normalize_newlines(output_text));
    std::vector<std::string> output_lines;
    std::vector<std::string> lower_output_lines;
    std::string cur_line;
    while (std::getline(iss, cur_line)) {
        output_lines.push_back(cur_line);
        std::string lower_l = cur_line;
        std::transform(lower_l.begin(), lower_l.end(), lower_l.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        lower_output_lines.push_back(std::move(lower_l));
    }

    size_t line_cursor = 0;
    std::vector<const Directive*> pending_not_directives;

    auto check_pending_not = [&](size_t up_to_index) -> bool {
        for (const auto* not_dir : pending_not_directives) {
            for (size_t l = line_cursor; l < up_to_index && l < output_lines.size(); ++l) {
                if (match_line(output_lines[l], lower_output_lines[l], *not_dir)) {
                    res.success = false;
                    res.error_message = "CHECK-NOT pattern found but should not exist: '" + not_dir->pattern + "'";
                    res.failed_directive_line = not_dir->line_number;
                    res.failed_output_line = static_cast<uint32_t>(l + 1);
                    res.expected_pattern = not_dir->pattern;
                    res.found_snippet = output_lines[l];
                    return false;
                }
            }
        }
        pending_not_directives.clear();
        return true;
    };

    for (size_t d_idx = 0; d_idx < directives.size(); ++d_idx) {
        const auto& d = directives[d_idx];
        if (d.type == DirectiveType::RUN) continue;

        if (d.type == DirectiveType::CHECK_NOT) {
            pending_not_directives.push_back(&d);
            continue;
        }

        if (d.type == DirectiveType::CHECK_LABEL) {
            // Find label in output from line_cursor
            size_t match_pos = output_lines.size();
            for (size_t l = line_cursor; l < output_lines.size(); ++l) {
                if (match_line(output_lines[l], lower_output_lines[l], d)) {
                    match_pos = l;
                    break;
                }
            }
            if (match_pos == output_lines.size()) {
                res.success = false;
                res.error_message = "CHECK-LABEL not found: '" + d.pattern + "'";
                res.failed_directive_line = d.line_number;
                res.expected_pattern = d.pattern;
                return res;
            }
            if (!check_pending_not(match_pos)) return res;
            line_cursor = match_pos + 1;
            continue;
        }

        if (d.type == DirectiveType::CHECK_NEXT) {
            if (line_cursor >= output_lines.size()) {
                res.success = false;
                res.error_message = "CHECK-NEXT reached end of output looking for: '" + d.pattern + "'";
                res.failed_directive_line = d.line_number;
                res.failed_output_line = static_cast<uint32_t>(output_lines.size());
                res.expected_pattern = d.pattern;
                return res;
            }
            if (!check_pending_not(line_cursor)) return res;

            if (!match_line(output_lines[line_cursor], lower_output_lines[line_cursor], d)) {
                res.success = false;
                res.error_message = "CHECK-NEXT did not match on line " + std::to_string(line_cursor + 1) +
                                    ": expected '" + d.pattern + "'";
                res.failed_directive_line = d.line_number;
                res.failed_output_line = static_cast<uint32_t>(line_cursor + 1);
                res.expected_pattern = d.pattern;
                res.found_snippet = output_lines[line_cursor];
                return res;
            }
            ++line_cursor;
            continue;
        }

        if (d.type == DirectiveType::CHECK_DAG) {
            // Collect contiguous block of CHECK-DAGs
            std::vector<const Directive*> dag_block;
            dag_block.push_back(&d);
            while (d_idx + 1 < directives.size() && directives[d_idx + 1].type == DirectiveType::CHECK_DAG) {
                dag_block.push_back(&directives[++d_idx]);
            }

            // Find match for each DAG directive anywhere from line_cursor
            size_t max_dag_match = line_cursor;
            for (const auto* dag_dir : dag_block) {
                bool found = false;
                for (size_t l = line_cursor; l < output_lines.size(); ++l) {
                    if (match_line(output_lines[l], lower_output_lines[l], *dag_dir)) {
                        found = true;
                        max_dag_match = std::max(max_dag_match, l + 1);
                        break;
                    }
                }
                if (!found) {
                    res.success = false;
                    res.error_message = "CHECK-DAG pattern not found: '" + dag_dir->pattern + "'";
                    res.failed_directive_line = dag_dir->line_number;
                    res.expected_pattern = dag_dir->pattern;
                    return res;
                }
            }
            if (!check_pending_not(max_dag_match)) return res;
            line_cursor = max_dag_match;
            continue;
        }

        if (d.type == DirectiveType::CHECK) {
            size_t match_pos = output_lines.size();
            for (size_t l = line_cursor; l < output_lines.size(); ++l) {
                if (match_line(output_lines[l], lower_output_lines[l], d)) {
                    match_pos = l;
                    break;
                }
            }
            if (match_pos == output_lines.size()) {
                res.success = false;
                res.error_message = "CHECK pattern not found: '" + d.pattern + "'";
                res.failed_directive_line = d.line_number;
                res.expected_pattern = d.pattern;
                return res;
            }
            if (!check_pending_not(match_pos)) return res;
            line_cursor = match_pos + 1;
            continue;
        }
    }

    // Check any remaining CHECK-NOT against the rest of output
    if (!check_pending_not(output_lines.size())) return res;

    return res;
}

// Configuration options for dgtl-lit
struct LitConfig {
    fs::path root_dir;
    fs::path dgtl_opt_path;
    fs::path dgtl_llc_path;
    fs::path dgtl_verify_path;
    fs::path dgtl_engine_path;
    fs::path dgtl_link_path;
    fs::path dgtl_inspect_path;
    std::vector<fs::path> test_paths;
    std::string filter_regex;
    uint32_t num_threads = 0;
    bool verbose = false;
    bool show_all = false;
    bool use_color = true;
};

// Execute process and capture standard output & error
struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
};

#if defined(_WIN32) || defined(_WIN64)
[[nodiscard]] inline ProcessResult run_process_win32(const std::string& command) {
    ProcessResult res;

    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        res.exit_code = -1;
        res.stdout_output = "Error: Failed to create pipe for command: " + command;
        return res;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd_copy = command;
    BOOL ok = CreateProcessA(
        NULL,
        cmd_copy.data(),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    // Parent must close its copy of the write end so ReadFile reaches EOF when child finishes
    CloseHandle(hWritePipe);

    if (!ok) {
        CloseHandle(hReadPipe);
        // Fallback to _popen if direct CreateProcess fails
        std::string wrapped_cmd = "\"" + command + " 2>&1\"";
        FILE* pipe = _popen(wrapped_cmd.c_str(), "r");
        if (!pipe) {
            res.exit_code = -1;
            res.stdout_output = "Error: Failed to spawn process: " + command;
            return res;
        }
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            res.stdout_output += buffer;
        }
        res.exit_code = _pclose(pipe);
        return res;
    }

    // Read child output from pipe
    char buffer[8192];
    DWORD bytes_read = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
        res.stdout_output.append(buffer, bytes_read);
    }
    CloseHandle(hReadPipe);

    // Wait for child process to terminate
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    res.exit_code = static_cast<int>(exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return res;
}
#endif

[[nodiscard]] inline ProcessResult run_process(const std::string& command) {
#if defined(_WIN32) || defined(_WIN64)
    return run_process_win32(command);
#else
    ProcessResult res;
    std::string cmd_with_redirect = command + " 2>&1";
    FILE* pipe = popen(cmd_with_redirect.c_str(), "r");
    if (!pipe) {
        res.exit_code = -1;
        res.stdout_output = "Error: Failed to spawn process: " + command;
        return res;
    }

    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        res.stdout_output += buffer;
    }

    res.exit_code = pclose(pipe);
    return res;
#endif
}

inline void prewarm_binaries(const LitConfig& config) {
    if (!config.dgtl_opt_path.empty() && fs::exists(config.dgtl_opt_path)) {
        (void)run_process("\"" + config.dgtl_opt_path.string() + "\" --help");
    }
    if (!config.dgtl_llc_path.empty() && fs::exists(config.dgtl_llc_path)) {
        (void)run_process("\"" + config.dgtl_llc_path.string() + "\" --help");
    }
    if (!config.dgtl_engine_path.empty() && fs::exists(config.dgtl_engine_path)) {
        (void)run_process("\"" + config.dgtl_engine_path.string() + "\" --help");
    }
}

// Find repository root by searching upward for dgtl.root or include/dgtl
[[nodiscard]] fs::path detect_dgtl_root() {
    fs::path cur = fs::current_path();
    while (!cur.empty()) {
        if (fs::exists(cur / "dgtl.root") || fs::exists(cur / "include" / "dgtl")) {
            return cur;
        }
        if (cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }
    return fs::current_path();
}

// Locate tool binary across candidate directories
[[nodiscard]] fs::path locate_tool(const fs::path& root, const std::string& tool_name) {
    const std::vector<fs::path> candidates = {
        root / "build" / (tool_name + ".exe"),
        root / "build" / tool_name,
        root / "build" / "tools" / (tool_name + ".exe"),
        root / "build" / "tools" / tool_name,
        root / "bin" / (tool_name + ".exe"),
        root / "bin" / tool_name
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return tool_name; // fallback to PATH
}

// Test Outcome
struct TestResult {
    fs::path test_file;
    bool passed = false;
    double elapsed_ms = 0.0;
    std::string error_message;
    std::string executed_command;
    std::string full_output;
    uint32_t directive_line = 0;
};

// Run a single test file
TestResult execute_test(const fs::path& test_file, const LitConfig& config) {
    TestResult result;
    result.test_file = test_file;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Read test file
    std::ifstream file(test_file, std::ios::binary);
    if (!file.is_open()) {
        result.passed = false;
        result.error_message = "Failed to open test file: " + test_file.string();
        return result;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Parse directives
    const auto directives = parse_directives(content);

    // Extract RUN lines
    std::vector<std::string> run_commands;
    for (const auto& d : directives) {
        if (d.type == DirectiveType::RUN) {
            run_commands.push_back(d.pattern);
        }
    }

    // Default RUN line if none specified
    if (run_commands.empty()) {
        run_commands.push_back("%dgtl-opt --filecheck %s");
    }

    // Prepare temp file path
    fs::path temp_dir = fs::temp_directory_path();
    std::string test_stem = test_file.stem().string();
    uint64_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    fs::path temp_file = temp_dir / ("dgtl_test_" + test_stem + "_" + std::to_string(tid) + ".tmp");

    std::string s_sub = "\"" + test_file.string() + "\"";
    std::string s_dir_sub = "\"" + test_file.parent_path().string() + "\"";
    std::string opt_sub = "\"" + config.dgtl_opt_path.string() + "\"";
    std::string llc_sub = "\"" + config.dgtl_llc_path.string() + "\"";
    std::string ver_sub = "\"" + config.dgtl_verify_path.string() + "\"";
    std::string eng_sub = "\"" + config.dgtl_engine_path.string() + "\"";
    std::string lnk_sub = "\"" + config.dgtl_link_path.string() + "\"";
    std::string insp_sub = "\"" + config.dgtl_inspect_path.string() + "\"";
    std::string t_sub = "\"" + temp_file.string() + "\"";

    bool all_runs_passed = true;

    auto replace_token = [](std::string& s, std::string_view token, const std::string& rep) {
        size_t pos = 0;
        while ((pos = s.find(token, pos)) != std::string::npos) {
            if (token.front() != '%') {
                if (pos > 0 && (s[pos - 1] == '/' || s[pos - 1] == '\\' || s[pos - 1] == '"' || s[pos - 1] == '%')) {
                    pos += token.size();
                    continue;
                }
            }
            s.replace(pos, token.size(), rep);
            pos += rep.size();
        }
    };

    for (const auto& raw_cmd : run_commands) {
        std::string cmd = raw_cmd;
        replace_token(cmd, "%dgtl-opt", opt_sub);
        replace_token(cmd, "dgtl-opt", opt_sub);
        replace_token(cmd, "%dgtl-llc", llc_sub);
        replace_token(cmd, "dgtl-llc", llc_sub);
        replace_token(cmd, "%dgtl-verify", ver_sub);
        replace_token(cmd, "dgtl-verify", ver_sub);
        replace_token(cmd, "%dgtl-engine", eng_sub);
        replace_token(cmd, "dgtl-engine", eng_sub);
        replace_token(cmd, "%dgtl-link", lnk_sub);
        replace_token(cmd, "dgtl-link", lnk_sub);
        replace_token(cmd, "%dgtl-inspect", insp_sub);
        replace_token(cmd, "dgtl-inspect", insp_sub);
        replace_token(cmd, "%s", s_sub);
        replace_token(cmd, "%S", s_dir_sub);

        // Handle %t with optional extension
        std::regex t_re("%t(\\.[a-zA-Z0-9_]+)?");
        std::string t_replaced;
        std::sregex_iterator it(cmd.begin(), cmd.end(), t_re);
        std::sregex_iterator end_it;
        size_t last_pos = 0;
        for (; it != end_it; ++it) {
            t_replaced.append(cmd, last_pos, it->position() - last_pos);
            std::string suffix = it->str(1);
            fs::path cur_t = temp_file;
            if (!suffix.empty()) cur_t += suffix;
            t_replaced += "\"" + cur_t.string() + "\"";
            last_pos = it->position() + it->length();
        }
        t_replaced.append(cmd, last_pos, cmd.size() - last_pos);
        cmd = std::move(t_replaced);

        result.executed_command = cmd;

        // Check if command is piped to FileCheck
        size_t pipe_pos = cmd.find("|");
        bool piped_to_filecheck = false;
        std::string exec_cmd = cmd;

        if (pipe_pos != std::string::npos) {
            std::string right = trim(cmd.substr(pipe_pos + 1));
            if (right.find("FileCheck") != std::string::npos || right.find("filecheck") != std::string::npos) {
                piped_to_filecheck = true;
                exec_cmd = trim(cmd.substr(0, pipe_pos));
            }
        }

        // Run primary process
        auto proc_res = run_process(exec_cmd);
        result.full_output = proc_res.stdout_output;

        if (piped_to_filecheck) {
            // Check exit code of left process
            if (proc_res.exit_code != 0) {
                result.passed = false;
                result.error_message = "Command failed with exit code " + std::to_string(proc_res.exit_code) + "\n" + proc_res.stdout_output;
                all_runs_passed = false;
                break;
            }

            // Run built-in FileCheck against output
            auto fc_res = verify_filecheck(directives, proc_res.stdout_output);
            if (!fc_res.success) {
                result.passed = false;
                result.error_message = fc_res.error_message;
                result.directive_line = fc_res.failed_directive_line;
                all_runs_passed = false;
                break;
            }
        } else {
            // Executed directly (e.g. dgtl-opt --filecheck %s)
            if (proc_res.exit_code != 0) {
                result.passed = false;
                result.error_message = "Command failed with exit code " + std::to_string(proc_res.exit_code) + "\n" + proc_res.stdout_output;
                all_runs_passed = false;
                break;
            }
        }
    }

    // Clean up temporary file
    std::error_code ec;
    if (fs::exists(temp_file)) fs::remove(temp_file, ec);
    fs::path temp_s = temp_file; temp_s.replace_extension(".s");
    if (fs::exists(temp_s)) fs::remove(temp_s, ec);
    fs::path temp_o = temp_file; temp_o.replace_extension(".o");
    if (fs::exists(temp_o)) fs::remove(temp_o, ec);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    result.passed = all_runs_passed;

    return result;
}

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Native Test Runner & FileCheck Engine (dgtl-lit)\n"
              << "Ultra-high-performance C++26 test orchestrator for .dgtl compiler tests\n\n"
              << "USAGE:\n"
              << "  " << prog_name << " [OPTIONS] [path...]\n\n"
              << "OPTIONS:\n"
              << "  -j, --jobs=<n>        Number of concurrent test threads (default: hardware concurrency)\n"
              << "  -k, --filter=<regex>  Run only tests matching regex pattern\n"
              << "  -v, --verbose         Display executed command and output for all tests\n"
              << "  -a, --show-all        Print output for passed tests as well as failed tests\n"
              << "  --no-color            Disable ANSI color output\n"
              << "  --root=<path>         Explicitly set DGTL repository root\n"
              << "  --dgtl-opt=<path>     Path to dgtl-opt binary\n"
              << "  --dgtl-llc=<path>     Path to dgtl-llc binary\n"
              << "  --dgtl-verify=<path>  Path to dgtl-verify binary\n"
              << "  -h, --help            Display this help message\n\n"
              << "EXAMPLES:\n"
              << "  dgtl-lit tests/lit_filecheck\n"
              << "  dgtl-lit -j 8 tests/lit_filecheck/transforms\n"
              << "  dgtl-lit -k gvn tests/lit_filecheck\n";
}

} // namespace dgtl::lit

int main(int argc, char** argv) {
    dgtl::lit::LitConfig config;
    config.root_dir = dgtl::lit::detect_dgtl_root();
    config.num_threads = std::clamp(std::thread::hardware_concurrency(), 1u, 32u);

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            dgtl::lit::print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "-a" || arg == "--show-all") {
            config.show_all = true;
        } else if (arg == "--no-color") {
            config.use_color = false;
        } else if (arg.rfind("-j", 0) == 0) {
            if (arg.size() > 2) config.num_threads = std::max(1u, static_cast<uint32_t>(std::stoul(arg.substr(2))));
            else if (i + 1 < argc) config.num_threads = std::max(1u, static_cast<uint32_t>(std::stoul(argv[++i])));
        } else if (arg.rfind("--jobs=", 0) == 0) {
            config.num_threads = std::max(1u, static_cast<uint32_t>(std::stoul(arg.substr(7))));
        } else if (arg.rfind("-k", 0) == 0) {
            if (arg.size() > 2) config.filter_regex = arg.substr(2);
            else if (i + 1 < argc) config.filter_regex = argv[++i];
        } else if (arg.rfind("--filter=", 0) == 0) {
            config.filter_regex = arg.substr(9);
        } else if (arg.rfind("--root=", 0) == 0) {
            config.root_dir = arg.substr(7);
        } else if (arg.rfind("--dgtl-opt=", 0) == 0) {
            config.dgtl_opt_path = arg.substr(11);
        } else if (arg.rfind("--dgtl-llc=", 0) == 0) {
            config.dgtl_llc_path = arg.substr(11);
        } else if (arg.rfind("--dgtl-verify=", 0) == 0) {
            config.dgtl_verify_path = arg.substr(14);
        } else if (!arg.starts_with("-")) {
            config.test_paths.push_back(arg);
        }
    }

    // Resolve tool paths
    if (config.dgtl_opt_path.empty()) config.dgtl_opt_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-opt");
    if (config.dgtl_llc_path.empty()) config.dgtl_llc_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-llc");
    if (config.dgtl_verify_path.empty()) config.dgtl_verify_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-verify");
    if (config.dgtl_engine_path.empty()) config.dgtl_engine_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-engine");
    if (config.dgtl_link_path.empty()) config.dgtl_link_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-link");
    if (config.dgtl_inspect_path.empty()) config.dgtl_inspect_path = dgtl::lit::locate_tool(config.root_dir, "dgtl-inspect");

    // Default test directory
    if (config.test_paths.empty()) {
        fs::path default_dir = config.root_dir / "tests" / "lit_filecheck";
        if (fs::exists(default_dir)) {
            config.test_paths.push_back(default_dir);
        } else {
            std::cerr << "Error: No test directory specified and default not found: "
                      << default_dir.string() << "\n";
            return 1;
        }
    }

    // Discover .dgtl files
    std::vector<fs::path> discovered_tests;
    std::regex filter_re;
    bool has_filter = false;
    if (!config.filter_regex.empty()) {
        try {
            filter_re = std::regex(config.filter_regex, std::regex::icase);
            has_filter = true;
        } catch (const std::exception& e) {
            std::cerr << "Error: Invalid filter regex: " << e.what() << "\n";
            return 1;
        }
    }

    for (const auto& tp : config.test_paths) {
        if (fs::is_regular_file(tp) && tp.extension() == ".dgtl") {
            std::string name = tp.string();
            if (!has_filter || std::regex_search(name, filter_re)) {
                discovered_tests.push_back(tp);
            }
        } else if (fs::is_directory(tp)) {
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(tp, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".dgtl") {
                    std::string name = entry.path().string();
                    if (!has_filter || std::regex_search(name, filter_re)) {
                        discovered_tests.push_back(entry.path());
                    }
                }
            }
        }
    }

    std::sort(discovered_tests.begin(), discovered_tests.end());

    if (discovered_tests.empty()) {
        std::cout << "[dgtl-lit] No matching .dgtl tests discovered.\n";
        return 0;
    }

    std::cout << "================================================================================\n"
              << "                      DGTL Native Test Runner (dgtl-lit)                        \n"
              << "================================================================================\n"
              << "Discovered " << discovered_tests.size() << " test(s) | Workers: "
              << config.num_threads << " | Root: " << config.root_dir.string() << "\n\n";

    // Pre-warm primary binaries to eliminate cold-start Windows Defender & CSRSS lockup
    dgtl::lit::prewarm_binaries(config);

    auto suite_start = std::chrono::high_resolution_clock::now();

    // Multithreaded execution queue
    std::mutex queue_mutex;
    std::queue<size_t> test_indices;
    for (size_t i = 0; i < discovered_tests.size(); ++i) {
        test_indices.push(i);
    }

    std::vector<dgtl::lit::TestResult> results(discovered_tests.size());
    std::atomic<size_t> completed_count{0};
    std::atomic<size_t> passed_count{0};
    std::atomic<size_t> failed_count{0};
    std::mutex print_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = 0;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (test_indices.empty()) return;
                idx = test_indices.front();
                test_indices.pop();
            }

            auto res = dgtl::lit::execute_test(discovered_tests[idx], config);
            results[idx] = res;

            size_t done = ++completed_count;
            if (res.passed) ++passed_count;
            else ++failed_count;

            // Compute relative display path
            std::string rel_path;
            try {
                rel_path = fs::relative(res.test_file, config.root_dir).string();
            } catch (...) {
                rel_path = res.test_file.string();
            }

            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::ostringstream oss;
                oss << "[" << std::setw(3) << done << "/" << std::setw(3) << discovered_tests.size() << "] ";

                if (res.passed) {
                    if (config.use_color) oss << dgtl::lit::Color::BoldGreen << "PASS: " << dgtl::lit::Color::Reset;
                    else oss << "PASS: ";
                } else {
                    if (config.use_color) oss << dgtl::lit::Color::BoldRed << "FAIL: " << dgtl::lit::Color::Reset;
                    else oss << "FAIL: ";
                }

                oss << rel_path << " (" << std::fixed << std::setprecision(1) << res.elapsed_ms << " ms)\n";

                if (!res.passed || config.show_all) {
                    oss << "--------------------------------------------------------------------------------\n";
                    if (!res.executed_command.empty()) {
                        oss << "Command: " << res.executed_command << "\n";
                    }
                    if (res.directive_line > 0) {
                        oss << "Directive Line: " << res.directive_line << "\n";
                    }
                    if (!res.error_message.empty()) {
                        oss << "Error:\n" << res.error_message << "\n";
                    }
                    if (config.verbose && !res.full_output.empty()) {
                        oss << "Output:\n" << res.full_output << "\n";
                    }
                    oss << "--------------------------------------------------------------------------------\n";
                }

                std::cout << oss.str() << std::flush;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(config.num_threads);
    for (uint32_t i = 0; i < config.num_threads; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    auto suite_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(suite_end - suite_start).count();
    double tests_per_sec = (total_sec > 0.0) ? (static_cast<double>(discovered_tests.size()) / total_sec) : 0.0;

    std::cout << "\n================================================================================\n"
              << "                             LIT TEST SUITE SUMMARY                             \n"
              << "================================================================================\n";

    if (failed_count == 0) {
        if (config.use_color) std::cout << dgtl::lit::Color::BoldGreen;
        std::cout << "All " << passed_count << " test(s) passed successfully!";
        if (config.use_color) std::cout << dgtl::lit::Color::Reset;
        std::cout << "\n";
    } else {
        if (config.use_color) std::cout << dgtl::lit::Color::BoldRed;
        std::cout << failed_count << " of " << discovered_tests.size() << " test(s) FAILED.";
        if (config.use_color) std::cout << dgtl::lit::Color::Reset;
        std::cout << "\n";
    }

    std::cout << "Time Elapsed: " << std::fixed << std::setprecision(2) << total_sec << "s ("
              << std::fixed << std::setprecision(1) << tests_per_sec << " tests/sec)\n"
              << "================================================================================\n";

    return (failed_count == 0) ? 0 : 1;
}
