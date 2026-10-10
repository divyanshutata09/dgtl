/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   DGTL Configuration & Discovery Tool (dgtl-config)
 *
 * Standards:   C++20 / C++26 Compatible
 * Description: Modelled strictly after llvm-config to provide build flags,
 *              component libraries, target architectures, and installation paths.
 */

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <algorithm>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::string_view DGTL_VERSION_STR     = "2.1.0-SOVEREIGN";
constexpr std::string_view DGTL_ABI_VERSION_STR = "4";
constexpr std::string_view DGTL_HOST_TRIPLE     = "x86_64-pc-windows-msvc";
constexpr std::string_view DGTL_TARGETS_BUILT   = "x86_64 arm64 riscv64 wasm cuda rocm";
constexpr std::string_view DGTL_COMPONENTS_LIST = "core ir opt mir isa codegen object jit linker runtime sdk all";

struct PathContext {
    fs::path sdk_root;
    fs::path bin_dir;
    fs::path include_dir;
    fs::path lib_dir;
};

fs::path get_executable_dir() {
#if defined(_WIN32) || defined(_WIN64)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0) {
        return fs::path(buf).parent_path();
    }
#else
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return fs::path(buf).parent_path();
    }
#endif
    return fs::current_path();
}

PathContext resolve_paths() {
    fs::path exe_dir = get_executable_dir();
    PathContext ctx;

    // 1. If inside build/DGTL_SDK/bin
    std::error_code ec;
    if (exe_dir.filename() == "bin" && fs::exists(exe_dir / ".." / "lib" / "x64" / "dgtl.lib", ec)) {
        ctx.sdk_root = (exe_dir / "..").lexically_normal();
    } else if (fs::exists("D:/TataG/DGTL/build/DGTL_SDK", ec)) {
        ctx.sdk_root = fs::path("D:/TataG/DGTL/build/DGTL_SDK");
    } else {
        ctx.sdk_root = exe_dir;
    }

    ctx.bin_dir     = ctx.sdk_root / "bin";
    ctx.include_dir = ctx.sdk_root / "include";
    ctx.lib_dir     = ctx.sdk_root / "lib" / "x64";
    return ctx;
}

void print_help() {
    std::cout << "Usage: dgtl-config [OPTIONS...]\n\n"
              << "Options:\n"
              << "  --version          Print DGTL version\n"
              << "  --api-version      Print DGTL C-ABI version\n"
              << "  --cflags           C compiler flags for files that include DGTL headers\n"
              << "  --cxxflags         C++ compiler flags for files that include DGTL headers\n"
              << "  --ldflags          Print Linker flags (library search paths)\n"
              << "  --libs [COMP...]   Libraries needed to link against DGTL\n"
              << "  --libnames [COMP...] Bare library names for given components\n"
              << "  --libfiles [COMP...] Full library file paths for given components\n"
              << "  --components       List all available DGTL components\n"
              << "  --targets-built    List target architectures supported by DGTL\n"
              << "  --host-target      Print host target triple\n"
              << "  --prefix           Print DGTL SDK installation root\n"
              << "  --bindir           Directory containing DGTL executables\n"
              << "  --includedir       Directory containing DGTL headers\n"
              << "  --libdir           Directory containing DGTL libraries\n"
              << "  --has-rtti         Print whether RTTI was enabled during build (YES/NO)\n"
              << "  --shared-mode      Print default linkage mode (static/shared)\n"
              << "  --help, -h         Print this help message\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    const PathContext ctx = resolve_paths();
    std::string inc_path = ctx.include_dir.generic_string();
    std::string lib_path = ctx.lib_dir.generic_string();

    std::vector<std::string> requested_components;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else if (arg == "--version") {
            std::cout << DGTL_VERSION_STR << "\n";
        } else if (arg == "--api-version") {
            std::cout << DGTL_ABI_VERSION_STR << "\n";
        } else if (arg == "--cflags") {
            std::cout << "-I\"" << inc_path << "\"\n";
        } else if (arg == "--cxxflags") {
            std::cout << "-std=c++26 -I\"" << inc_path << "\"\n";
        } else if (arg == "--ldflags") {
            std::cout << "-L\"" << lib_path << "\"\n";
        } else if (arg == "--prefix") {
            std::cout << ctx.sdk_root.generic_string() << "\n";
        } else if (arg == "--bindir") {
            std::cout << ctx.bin_dir.generic_string() << "\n";
        } else if (arg == "--includedir") {
            std::cout << inc_path << "\n";
        } else if (arg == "--libdir") {
            std::cout << lib_path << "\n";
        } else if (arg == "--components") {
            std::cout << DGTL_COMPONENTS_LIST << "\n";
        } else if (arg == "--targets-built") {
            std::cout << DGTL_TARGETS_BUILT << "\n";
        } else if (arg == "--host-target") {
            std::cout << DGTL_HOST_TRIPLE << "\n";
        } else if (arg == "--has-rtti") {
            std::cout << "NO\n";
        } else if (arg == "--shared-mode") {
            std::cout << "static\n";
        } else if (arg == "--libs") {
            std::vector<std::string> comps;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                comps.emplace_back(argv[++i]);
            }
            if (comps.empty() || std::find(comps.begin(), comps.end(), "all") != comps.end()) {
                std::cout << "-ldgtl\n";
            } else {
                std::string res;
                for (const auto& c : comps) {
                    if (!res.empty()) res += " ";
                    res += "-ldgtl_" + c;
                }
                std::cout << res << "\n";
            }
        } else if (arg == "--libnames") {
            std::vector<std::string> comps;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                comps.emplace_back(argv[++i]);
            }
            if (comps.empty() || std::find(comps.begin(), comps.end(), "all") != comps.end()) {
                std::cout << "dgtl.lib\n";
            } else {
                std::string res;
                for (const auto& c : comps) {
                    if (!res.empty()) res += " ";
                    res += "libdgtl_" + c + ".a";
                }
                std::cout << res << "\n";
            }
        } else if (arg == "--libfiles") {
            std::vector<std::string> comps;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                comps.emplace_back(argv[++i]);
            }
            if (comps.empty() || std::find(comps.begin(), comps.end(), "all") != comps.end()) {
                std::cout << (ctx.lib_dir / "dgtl.lib").generic_string() << "\n";
            } else {
                std::string res;
                for (const auto& c : comps) {
                    if (!res.empty()) res += " ";
                    res += (ctx.lib_dir / ("libdgtl_" + c + ".a")).generic_string();
                }
                std::cout << res << "\n";
            }
        } else {
            std::cerr << "dgtl-config: unknown option: " << arg << "\n";
            return 1;
        }
    }

    return 0;
}