/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_all(const fs::path& p, std::string_view text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

}  // namespace

int main(int argc, char** argv) {
    fs::path repo = fs::current_path();
    if (argc > 1) {
        repo = fs::path(argv[1]);
    }
    repo = fs::weakly_canonical(repo);

#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile == nullptr) {
        std::cerr << "USERPROFILE is not set.\n";
        return 1;
    }
    const fs::path profile_path = fs::path(user_profile) / "Documents/WindowsPowerShell/Microsoft.PowerShell_profile.ps1";
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        std::cerr << "HOME is not set.\n";
        return 1;
    }
    const fs::path profile_path = fs::path(home) / ".profile";
#endif

    fs::create_directories(profile_path.parent_path());
    if (!fs::exists(profile_path)) {
        std::ofstream touch(profile_path);
    }

    const std::string begin_marker = "# DGTL_COMMAND_BEGIN";
    const std::string end_marker = "# DGTL_COMMAND_END";

#ifdef _WIN32
    const std::string block = begin_marker + "\n"
        + "function dgtl {\n"
        + "    & '" + (repo / "dgtl.ps1").string() + "' @args\n"
        + "}\n"
        + end_marker + "\n";
#else
    const std::string block = begin_marker + "\n"
        + "dgtl() {\n"
        + "    \"" + (repo / "dgtl.sh").string() + "\" \"$@\"\n"
        + "}\n"
        + end_marker + "\n";
#endif

    std::string existing = read_all(profile_path);
    const auto begin_pos = existing.find(begin_marker);
    const auto end_pos = existing.find(end_marker);

    if (begin_pos != std::string::npos && end_pos != std::string::npos && end_pos >= begin_pos) {
        const auto replace_end = end_pos + end_marker.size();
        existing.replace(begin_pos, replace_end - begin_pos, block);
    } else {
        if (!existing.empty() && existing.back() != '\n') {
            existing.push_back('\n');
        }
        existing += "\n" + block;
    }

    if (!write_all(profile_path, existing)) {
        std::cerr << "Failed to update profile: " << profile_path.string() << "\n";
        return 1;
    }

    std::cout << "Installed 'dgtl' into " << profile_path.string() << "\n";
#ifdef _WIN32
    std::cout << "Open a new PowerShell session, then run: dgtl help\n";
#else
    std::cout << "Open a new shell session, then run: dgtl help\n";
#endif
    return 0;
}
