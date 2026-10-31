/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Toolchain Common Infrastructure
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#define DGTL_TOOLS_ISATTY() (_isatty(_fileno(stdout)) != 0)
#else
#include <unistd.h>
#define DGTL_TOOLS_ISATTY() (isatty(fileno(stdout)) != 0)
#endif

export module dgtl.tools.common;

export namespace dgtl::tools {

// ============================================================================
// Standardized Exit Codes & Signatures
// ============================================================================

enum class ExitCode : int {
    Success       = 0,
    GeneralError  = 1,
    InvalidArgs   = 2,
    FileNotFound  = 3,
    IOError       = 4,
    CompileError  = 5,
    LinkError     = 6,
    InternalError = 7,
};

/// @brief Canonical C++26 tool function contract
using ToolMainFn = int(*)(std::span<const std::string_view> args, std::string_view invoked_as);

// ============================================================================
// ANSI Terminal Formatter
// ============================================================================

struct Terminal {
    static inline bool enabled = true;

    static void init(bool enable_color = true) {
        if (!enable_color || std::getenv("NO_COLOR") != nullptr) {
            enabled = false;
            return;
        }
        enabled = DGTL_TOOLS_ISATTY();
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
};

// ============================================================================
// Tool Name Normalization (argv[0] Stem Matching)
// ============================================================================

inline std::string normalize_tool_name(std::string_view raw_invoked) {
    if (raw_invoked.empty()) return "";
    std::filesystem::path p(raw_invoked);
    std::string stem = p.stem().string();
    std::ranges::transform(stem, stem.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (stem.starts_with("dgtl-") || stem.starts_with("dgtl_")) {
        stem = stem.substr(5);
    }
    return stem;
}

// ============================================================================
// Common File & IO Utilities
// ============================================================================

struct FileUtils {
    static bool read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes) {
        out_bytes.clear();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;

        in.seekg(0, std::ios::end);
        auto sz = in.tellg();
        if (sz < 0) return false;
        in.seekg(0, std::ios::beg);

        out_bytes.resize(static_cast<size_t>(sz));
        if (sz > 0) {
            in.read(reinterpret_cast<char*>(out_bytes.data()), static_cast<std::streamsize>(sz));
        }
        return in.good() || in.eof();
    }

    static bool write_binary_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;

        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        return out.good();
    }

    static std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in.is_open()) return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    static std::string read_all_text(const std::filesystem::path& path) {
        return read_text_file(path);
    }
};

// ============================================================================
// Universal Response File & Argument Expander
// ============================================================================

struct ArgExpander {
    static std::vector<std::string> expand_args(int argc, char* argv[], int start_index = 1) {
        std::vector<std::string> out;
        for (int i = start_index; i < argc; ++i) {
            std::string_view a = argv[i];
            expand_single(a, out);
        }
        return out;
    }

    static std::vector<std::string> expand_args(std::span<const std::string_view> raw_args) {
        std::vector<std::string> out;
        for (std::string_view a : raw_args) {
            expand_single(a, out);
        }
        return out;
    }

    static std::vector<std::string> expand(std::span<const std::string_view> raw_args) {
        return expand_args(raw_args);
    }

    static std::vector<std::string> expand(int argc, char* argv[], int start_index = 1) {
        return expand_args(argc, argv, start_index);
    }

private:
    static void expand_single(std::string_view arg, std::vector<std::string>& out) {
        if (arg.starts_with('@')) {
            std::string rsp_path = std::string(arg.substr(1));
            std::ifstream in(rsp_path);
            if (in.is_open()) {
                std::string line;
                while (std::getline(in, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                        line.pop_back();
                    }
                    if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
                        line = line.substr(1, line.size() - 2);
                    }
                    if (!line.empty()) {
                        out.push_back(line);
                    }
                }
                return;
            }
        }
        out.emplace_back(arg);
    }
};

} // namespace dgtl::tools
