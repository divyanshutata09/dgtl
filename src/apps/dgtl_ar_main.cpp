/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Sovereign Universal Archive Librarian (dgtl-ar)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import dgtl.object.archive;
import dgtl.object.file;

namespace fs = std::filesystem;
using namespace dgtl::object::archive;

void print_version() {
    std::cout << "dgtl-ar: DGTL Sovereign Universal Archive Librarian v2.1.0 (TataG Systems)\n"
              << "Standards: Universal GNU/BSD/COFF Static Archive Synthesis Engine\n";
}

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Sovereign Universal Archive Librarian (dgtl-ar)\n"
              << "Multi-Format Static Library & Universal Archive Synthesizer\n\n"
              << "USAGE:\n"
              << "  " << prog_name << " [-]{dmpqrstx}[abcDfiklMNoOPsSTuvV] [member-name] <archive-file> [files...]\n"
              << "  " << prog_name << " rcs <archive.a> <file1.o> [file2.o...]\n"
              << "  " << prog_name << " t[v] <archive.a>\n"
              << "  " << prog_name << " x <archive.a> [members...]\n\n"
              << "OPERATIONS:\n"
              << "  r, rcs, cr     Insert/replace object files in the archive (create if missing, index symbols)\n"
              << "  q, qs          Quick append object files to the archive\n"
              << "  t, tv          Table of contents: list members in the archive (v: verbose with sizes/symbols)\n"
              << "  x              Extract members from the archive to the current directory\n"
              << "  d              Delete specified members from the archive\n"
              << "  s              Force write/update of the archive symbol table index\n"
              << "  --version, -V  Display archiver version information\n"
              << "  --help, -h     Display this help menu\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " rcs libcore.a kernel.o math.o runtime.o\n"
              << "  " << prog_name << " tv libcore.a\n"
              << "  " << prog_name << " x libcore.a kernel.o\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string op;
    std::string archive_path;
    std::vector<std::string> member_files;

    int arg_idx = 1;
    std::string first_arg = argv[arg_idx++];

    if (first_arg == "--help" || first_arg == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    if (first_arg == "--version" || first_arg == "-V" || first_arg == "-v") {
        print_version();
        return 0;
    }

    // Strip leading hyphen if present (e.g. -rcs -> rcs)
    if (!first_arg.empty() && first_arg[0] == '-') {
        first_arg = first_arg.substr(1);
    }
    op = first_arg;

    if (arg_idx < argc) {
        archive_path = argv[arg_idx++];
    } else {
        std::cerr << "error[dgtl-ar]: no archive file specified\n";
        return 1;
    }

    while (arg_idx < argc) {
        member_files.emplace_back(argv[arg_idx++]);
    }

    bool is_insert = (op.find('r') != std::string::npos || op.find('q') != std::string::npos || op.find('c') != std::string::npos);
    bool is_list   = (op.find('t') != std::string::npos);
    bool is_extract = (op.find('x') != std::string::npos);
    bool is_delete = (op.find('d') != std::string::npos);
    bool is_verbose = (op.find('v') != std::string::npos);

    // ─── Table of contents (t / tv) ───────────────────────────────────────────
    if (is_list) {
        UniversalArchiveReader reader;
        if (!reader.load_archive(archive_path)) {
            std::cerr << "error[dgtl-ar]: failed to load or read archive: " << archive_path << "\n";
            return 2;
        }

        if (is_verbose) {
            std::cout << "[dgtl-ar] Archive: " << archive_path 
                      << " (" << reader.members().size() << " members, "
                      << reader.symbol_map().size() << " indexed symbols)\n";
            std::cout << "----------------------------------------------------------------------\n";
            for (const auto& m : reader.members()) {
                std::cout << std::left << std::setw(28) << m.filename
                          << " " << std::right << std::setw(10) << m.data.size() << " bytes"
                          << "  [" << m.symbols.size() << " symbols]\n";
            }
        } else {
            for (const auto& m : reader.members()) {
                std::cout << m.filename << "\n";
            }
        }
        return 0;
    }

    // ─── Extraction (x) ──────────────────────────────────────────────────────
    if (is_extract) {
        UniversalArchiveReader reader;
        if (!reader.load_archive(archive_path)) {
            std::cerr << "error[dgtl-ar]: failed to open archive for extraction: " << archive_path << "\n";
            return 2;
        }

        std::unordered_set<std::string> requested(member_files.begin(), member_files.end());
        size_t extracted_count = 0;

        for (const auto& m : reader.members()) {
            if (!requested.empty() && !requested.contains(m.filename)) {
                continue;
            }

            std::ofstream out(m.filename, std::ios::binary | std::ios::trunc);
            if (!out) {
                std::cerr << "error[dgtl-ar]: failed to write extracted member: " << m.filename << "\n";
                continue;
            }

            out.write(reinterpret_cast<const char*>(m.data.data()), static_cast<std::streamsize>(m.data.size()));
            out.close();
            extracted_count++;
            if (is_verbose) {
                std::cout << "[dgtl-ar] Extracted: " << m.filename << " (" << m.data.size() << " bytes)\n";
            }
        }

        if (extracted_count == 0 && !requested.empty()) {
            std::cerr << "error[dgtl-ar]: specified member(s) not found in archive\n";
            return 1;
        }
        return 0;
    }

    // ─── Deletion (d) ────────────────────────────────────────────────────────
    if (is_delete) {
        UniversalArchiveReader reader;
        if (!reader.load_archive(archive_path)) {
            std::cerr << "error[dgtl-ar]: failed to open archive: " << archive_path << "\n";
            return 2;
        }

        std::unordered_set<std::string> to_delete(member_files.begin(), member_files.end());
        std::vector<ArchiveMember> retained;
        for (const auto& m : reader.members()) {
            if (!to_delete.contains(m.filename)) {
                retained.push_back(m);
            }
        }

        auto res = UniversalArchiveWriter::create_static_archive_from_memory(archive_path, retained);
        if (!res.ok) {
            std::cerr << "error[dgtl-ar]: failed to write updated archive: " << res.error_message << "\n";
            return 3;
        }
        return 0;
    }

    // ─── Insertion / Creation (r, rcs, cr, q) ────────────────────────────────
    if (is_insert) {
        if (member_files.empty()) {
            std::cerr << "error[dgtl-ar]: no object files provided for archive creation\n";
            return 1;
        }

        // Expand any response files (@response.rsp)
        std::vector<std::string> expanded_files;
        for (const auto& item : member_files) {
            if (!item.empty() && item[0] == '@') {
                std::string rsp_path = item.substr(1);
                std::ifstream rsp_in(rsp_path);
                if (rsp_in) {
                    std::string line;
                    while (std::getline(rsp_in, line)) {
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                            line.pop_back();
                        }
                        if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
                            line = line.substr(1, line.size() - 2);
                        }
                        if (!line.empty()) {
                            expanded_files.push_back(line);
                        }
                    }
                }
            } else {
                expanded_files.push_back(item);
            }
        }

        // If archive does not exist yet, build directly
        if (!fs::exists(archive_path)) {
            auto res = UniversalArchiveWriter::create_static_archive(archive_path, expanded_files);
            if (!res.ok) {
                std::cerr << "error[dgtl-ar]: " << res.error_message << "\n";
                return 3;
            }
            if (is_verbose) {
                std::cout << "[dgtl-ar] Created archive: " << archive_path
                          << " (" << res.total_bytes << " bytes, "
                          << res.symbols_count << " symbols, "
                          << res.members_count << " members)\n";
            }
            return 0;
        }

        // Archive already exists: merge existing members with new ones
        UniversalArchiveReader reader;
        if (!reader.load_archive(archive_path)) {
            // Overwrite cleanly if corrupted
            auto res = UniversalArchiveWriter::create_static_archive(archive_path, expanded_files);
            if (!res.ok) {
                std::cerr << "error[dgtl-ar]: " << res.error_message << "\n";
                return 3;
            }
            return 0;
        }

        std::vector<ArchiveMember> updated_members = reader.members();
        for (const auto& path_str : expanded_files) {
            fs::path p(path_str);
            if (!fs::exists(p)) {
                std::cerr << "error[dgtl-ar]: member file not found: " << path_str << "\n";
                return 2;
            }

            std::ifstream in(path_str, std::ios::binary);
            if (!in) {
                std::cerr << "error[dgtl-ar]: failed to read file: " << path_str << "\n";
                return 2;
            }

            in.seekg(0, std::ios::end);
            size_t sz = static_cast<size_t>(in.tellg());
            in.seekg(0, std::ios::beg);

            ArchiveMember m;
            m.filename = p.filename().string();
            m.data.resize(sz);
            if (sz > 0) {
                in.read(reinterpret_cast<char*>(m.data.data()), static_cast<std::streamsize>(sz));
            }
            in.close();

            extract_universal_symbols(m.data, m.symbols);

            // Replace existing member with same filename or append
            auto it = std::find_if(updated_members.begin(), updated_members.end(),
                                  [&](const ArchiveMember& existing) {
                                      return existing.filename == m.filename;
                                  });
            if (it != updated_members.end()) {
                *it = std::move(m);
            } else {
                updated_members.push_back(std::move(m));
            }
        }

        auto res = UniversalArchiveWriter::create_static_archive_from_memory(archive_path, updated_members);
        if (!res.ok) {
            std::cerr << "error[dgtl-ar]: " << res.error_message << "\n";
            return 3;
        }

        if (is_verbose) {
            std::cout << "[dgtl-ar] Updated archive: " << archive_path
                      << " (" << res.total_bytes << " bytes, "
                      << res.symbols_count << " symbols, "
                      << res.members_count << " members)\n";
        }
        return 0;
    }

    std::cerr << "error[dgtl-ar]: unrecognized operation: " << op << "\n";
    print_usage(argv[0]);
    return 1;
}
