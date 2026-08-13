// Copyright (c) 2026 TataG Systems. All rights reserved.
//
// DGTL Phase E Regression: Deterministic Replay Test
//
// Runs production driver twice with deterministic replay enabled and asserts
// identical combined stdout/stderr logs and identical exit codes.

#include <cstdlib>
#include <direct.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string read_all(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    constexpr const char* kLog1 = "D:\\TataG\\DGTL\\build\\replay_log1.txt";
    constexpr const char* kLog2 = "D:\\TataG\\DGTL\\build\\replay_log2.txt";

    char old_cwd[1024] = {0};
    if (_getcwd(old_cwd, sizeof(old_cwd)) == nullptr) {
        std::cerr << "[DGTL] FAIL  replay_regression: unable to read current directory\n";
        return 1;
    }
    if (_chdir("D:/TataG/DGTL/build") != 0) {
        std::cerr << "[DGTL] FAIL  replay_regression: unable to enter build directory\n";
        return 1;
    }

    const int rc1 = std::system(".\\dgtl_release.exe --deterministic-replay > replay_log1.txt 2>&1");
    const int rc2 = std::system(".\\dgtl_release.exe --deterministic-replay > replay_log2.txt 2>&1");

    const int restore_rc = _chdir(old_cwd);
    if (restore_rc != 0) {
        std::cerr << "[DGTL] FAIL  replay_regression: unable to restore working directory\n";
        return 1;
    }

    if (rc1 == -1 || rc2 == -1) {
        std::cerr << "[DGTL] FAIL  replay_regression: failed to launch driver process\n";
        std::cerr << "[DGTL] rc1=" << rc1 << ", rc2=" << rc2 << "\n";
        return 1;
    }

    if (rc1 != rc2) {
        std::cerr << "[DGTL] FAIL  replay_regression: non-deterministic exit code\n";
        std::cerr << "[DGTL] rc1=" << rc1 << ", rc2=" << rc2 << "\n";
        return 1;
    }

    std::ifstream check1(kLog1, std::ios::binary);
    std::ifstream check2(kLog2, std::ios::binary);
    if (!check1.good() || !check2.good()) {
        std::cerr << "[DGTL] FAIL  replay_regression: replay logs are unreadable\n";
        std::cerr << "[DGTL] log1=" << kLog1 << "\n";
        std::cerr << "[DGTL] log2=" << kLog2 << "\n";
        return 1;
    }
    check1.close();
    check2.close();

    const std::string log1 = read_all(kLog1);
    const std::string log2 = read_all(kLog2);

    if (log1 != log2) {
        std::cerr << "[DGTL] FAIL  replay_regression: replay logs differ\n";
        std::cerr << "[DGTL] log1=" << kLog1 << "\n";
        std::cerr << "[DGTL] log2=" << kLog2 << "\n";
        return 1;
    }

    std::cout << "[DGTL] PASS  replay_regression: logs and exit codes are deterministic"
              << " (rc=" << rc1 << ")\n";
    return 0;
}
