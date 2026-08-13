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
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

void search(const fs::path& p, const std::string& query) {
    std::ifstream file(p);
    if (!file.is_open()) return;
    std::string line;
    int line_num = 1;
    while (std::getline(file, line)) {
        std::string lower_line = line;
        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
        if (lower_line.find(query) != std::string::npos) {
            std::cout << p.generic_string() << ":" << line_num << ": " << line << "\n";
        }
        line_num++;
    }
}

int main() {
    std::string query = "affinity";
    for (const auto& entry : fs::recursive_directory_iterator(".")) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".ixx" || ext == ".cpp" || ext == ".h") {
                search(entry.path(), query);
            }
        }
    }
    return 0;
}
