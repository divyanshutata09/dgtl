/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Tool Forwarder Launcher Stub (dgtl_tool_forwarder)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
    wchar_t self_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        std::cerr << "error: unable to determine launcher executable path\n";
        return 1;
    }

    fs::path self(self_path);
    fs::path parent_dir = self.parent_path();
    std::string stem = self.stem().string();

    // Target multicall binary
    fs::path target_tools = parent_dir / "dgtl-tools.exe";
    if (!fs::exists(target_tools)) {
        // Fallback: check build directory
        target_tools = parent_dir / "build" / "bin" / "dgtl-tools.exe";
    }

    if (!fs::exists(target_tools)) {
        std::cerr << "error: unified multicall binary 'dgtl-tools.exe' not found relative to " 
                  << parent_dir.string() << "\n";
        return 1;
    }

    // Build command line: "dgtl-tools.exe" <stem> <args...>
    std::wstring cmdline = L"\"" + target_tools.wstring() + L"\" " + fs::path(stem).wstring();
    for (int i = 1; i < argc; ++i) {
        cmdline += L" \"";
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, nullptr, 0);
        if (wlen > 0) {
            std::vector<wchar_t> warg(wlen);
            MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, warg.data(), wlen);
            cmdline += warg.data();
        }
        cmdline += L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back(L'\0');

    BOOL success = CreateProcessW(
        nullptr,
        cmdline_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!success) {
        std::cerr << "error: failed to spawn dgtl-tools.exe (error code: " << GetLastError() << ")\n";
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exit_code);
#else
    // POSIX Implementation
    fs::path self = fs::canonical("/proc/self/exe");
    fs::path parent_dir = self.parent_path();
    std::string stem = self.stem().string();

    fs::path target_tools = parent_dir / "dgtl-tools";
    if (!fs::exists(target_tools)) {
        target_tools = parent_dir / "build" / "bin" / "dgtl-tools";
    }

    if (!fs::exists(target_tools)) {
        std::cerr << "error: unified multicall binary 'dgtl-tools' not found\n";
        return 1;
    }

    std::vector<std::string> child_args;
    child_args.push_back(target_tools.string());
    child_args.push_back(stem);
    for (int i = 1; i < argc; ++i) {
        child_args.push_back(argv[i]);
    }

    std::vector<char*> c_argv;
    for (auto& s : child_args) {
        c_argv.push_back(s.data());
    }
    c_argv.push_back(nullptr);

    execv(target_tools.c_str(), c_argv.data());
    std::cerr << "error: failed to execv dgtl-tools\n";
    return 1;
#endif
}
