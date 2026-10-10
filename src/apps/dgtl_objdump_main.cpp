/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Sovereign Object & Binary Inspector (dgtl-objdump)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace dgtl::objdump {

enum class BinaryFormat {
    Unknown,
    PE_COFF,
    ELF64,
    MachO64,
    WASM,
    Archive
};

struct SectionInfo {
    std::string name;
    std::string segment;
    uint64_t virtual_address{0};
    uint64_t virtual_size{0};
    uint64_t file_offset{0};
    uint64_t file_size{0};
    uint32_t alignment{1};
    uint32_t flags{0};
    uint32_t reloc_count{0};
    bool is_code{false};
    bool is_readable{true};
    bool is_writable{false};
    bool is_zerofill{false};
    std::vector<uint8_t> data;
};

struct SymbolInfo {
    std::string name;
    uint64_t value{0};
    uint64_t size{0};
    std::string section_name;
    int section_index{0};
    bool is_global{false};
    bool is_external{false};
    bool is_function{false};
};

struct RelocationInfo {
    uint64_t offset{0};
    std::string symbol_name;
    uint32_t type{0};
    int64_t addend{0};
    std::string section_name;
};

struct DumpReport {
    std::string filename;
    BinaryFormat format{BinaryFormat::Unknown};
    std::string arch{"x86_64"};
    uint64_t entry_point{0};
    std::vector<SectionInfo> sections;
    std::vector<SymbolInfo> symbols;
    std::vector<RelocationInfo> relocations;
    bool has_error{false};
    std::string error_message;
};

inline uint16_t read16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t read64_le(const uint8_t* p) {
    return static_cast<uint64_t>(read32_le(p)) |
          (static_cast<uint64_t>(read32_le(p + 4)) << 32);
}

DumpReport inspect_object(const fs::path& file_path) {
    DumpReport report;
    report.filename = file_path.string();

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        report.has_error = true;
        report.error_message = "Cannot open file: " + file_path.string();
        return report;
    }

    file.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (file_size < 4) {
        report.has_error = true;
        report.error_message = "File too small to be a valid object or binary.";
        return report;
    }

    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size));
    file.close();

    const uint8_t* raw = buffer.data();

    // ─── 1. Archive Detection ──────────────────────────────────────────────
    if (file_size >= 8 && std::memcmp(raw, "!<arch>\n", 8) == 0) {
        report.format = BinaryFormat::Archive;
        return report;
    }

    // ─── 2. ELF Detection ──────────────────────────────────────────────────
    if (file_size >= 4 && raw[0] == 0x7F && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F') {
        report.format = BinaryFormat::ELF64;
        if (file_size >= 64) {
            report.entry_point = read64_le(raw + 24);
            const uint64_t shoff = read64_le(raw + 40);
            const uint16_t shentsize = read16_le(raw + 58);
            const uint16_t shnum = read16_le(raw + 60);
            const uint16_t shstrndx = read16_le(raw + 62);

            if (shoff + static_cast<uint64_t>(shnum) * shentsize <= file_size && shstrndx < shnum) {
                const uint8_t* str_shdr = raw + shoff + (static_cast<uint64_t>(shstrndx) * shentsize);
                const uint64_t str_off = read64_le(str_shdr + 24);
                const uint64_t str_sz  = read64_le(str_shdr + 32);

                for (uint16_t i = 0; i < shnum; ++i) {
                    const uint8_t* shdr = raw + shoff + (static_cast<uint64_t>(i) * shentsize);
                    const uint32_t name_idx = read32_le(shdr);
                    const uint32_t sh_type  = read32_le(shdr + 4);
                    const uint64_t sh_flags = read64_le(shdr + 8);
                    const uint64_t sh_addr  = read64_le(shdr + 16);
                    const uint64_t sh_offset= read64_le(shdr + 24);
                    const uint64_t sh_size  = read64_le(shdr + 32);

                    std::string sec_name;
                    if (str_off + name_idx < file_size) {
                        sec_name = reinterpret_cast<const char*>(raw + str_off + name_idx);
                    }

                    SectionInfo s;
                    s.name = sec_name.empty() ? ("[" + std::to_string(i) + "]") : sec_name;
                    s.virtual_address = sh_addr;
                    s.virtual_size = sh_size;
                    s.file_offset = sh_offset;
                    s.file_size = (sh_type == 8 /* SHT_NOBITS */) ? 0 : sh_size;
                    s.is_code = (sh_flags & 0x4 /* SHF_EXECINSTR */) != 0;
                    s.is_writable = (sh_flags & 0x1 /* SHF_WRITE */) != 0;
                    s.is_readable = (sh_flags & 0x2 /* SHF_ALLOC */) != 0;

                    if (sh_offset + s.file_size <= file_size && s.file_size > 0) {
                        s.data.assign(raw + sh_offset, raw + sh_offset + s.file_size);
                    }
                    report.sections.push_back(std::move(s));

                    // Symbols
                    if (sh_type == 2 /* SHT_SYMTAB */ || sh_type == 11 /* SHT_DYNSYM */) {
                        const uint32_t link = read32_le(shdr + 40);
                        const uint64_t entsize = read64_le(shdr + 56);
                        if (link < shnum && entsize > 0) {
                            const uint8_t* sym_str_shdr = raw + shoff + (static_cast<uint64_t>(link) * shentsize);
                            const uint64_t sym_str_off = read64_le(sym_str_shdr + 24);
                            const uint64_t count = sh_size / entsize;

                            for (uint64_t sym_i = 0; sym_i < count; ++sym_i) {
                                const uint8_t* sym_p = raw + sh_offset + (sym_i * entsize);
                                const uint32_t st_name = read32_le(sym_p);
                                const uint8_t st_info = sym_p[4];
                                const uint16_t st_shndx = read16_le(sym_p + 6);
                                const uint64_t st_value = read64_le(sym_p + 8);
                                const uint64_t st_size  = read64_le(sym_p + 16);

                                if (sym_str_off + st_name < file_size) {
                                    SymbolInfo sym;
                                    sym.name = reinterpret_cast<const char*>(raw + sym_str_off + st_name);
                                    sym.value = st_value;
                                    sym.size = st_size;
                                    sym.section_index = st_shndx;
                                    sym.is_global = ((st_info >> 4) == 1);
                                    sym.is_function = ((st_info & 0xF) == 2);
                                    sym.is_external = (st_shndx == 0);
                                    if (!sym.name.empty()) {
                                        report.symbols.push_back(std::move(sym));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return report;
    }

    // ─── 3. PE/COFF Detection ──────────────────────────────────────────────
    if ((raw[0] == 'M' && raw[1] == 'Z') || (raw[0] == 0x64 && raw[1] == 0x86) || (raw[0] == 0x4C && raw[1] == 0x01)) {
        report.format = BinaryFormat::PE_COFF;

        uint32_t pe_offset = 0;
        if (raw[0] == 'M' && raw[1] == 'Z' && file_size >= 0x40) {
            pe_offset = read32_le(raw + 0x3C);
        }

        if (pe_offset + 24 <= file_size && (pe_offset == 0 || std::memcmp(raw + pe_offset, "PE\0\0", 4) == 0)) {
            const uint32_t coff_off = (pe_offset == 0) ? 0 : (pe_offset + 4);
            const uint16_t machine = read16_le(raw + coff_off);
            const uint16_t num_sections = read16_le(raw + coff_off + 2);
            const uint16_t opt_hdr_size = read16_le(raw + coff_off + 16);

            report.arch = (machine == 0x8664) ? "x86_64" : ((machine == 0xAA64) ? "arm64" : "x86");

            if (opt_hdr_size >= 24) {
                const uint32_t opt_off = coff_off + 20;
                report.entry_point = read32_le(raw + opt_off + 16);
            }

            const uint32_t sec_table_off = coff_off + 20 + opt_hdr_size;
            for (uint16_t i = 0; i < num_sections && sec_table_off + (i + 1) * 40 <= file_size; ++i) {
                const uint8_t* sec = raw + sec_table_off + (i * 40);
                char name[9] = {0};
                std::memcpy(name, sec, 8);

                SectionInfo s;
                s.name = name;
                s.virtual_size = read32_le(sec + 8);
                s.virtual_address = read32_le(sec + 12);
                s.file_size = read32_le(sec + 16);
                s.file_offset = read32_le(sec + 20);
                s.flags = read32_le(sec + 36);

                s.is_code = (s.flags & 0x00000020) != 0;
                s.is_readable = (s.flags & 0x40000000) != 0;
                s.is_writable = (s.flags & 0x80000000) != 0;

                if (s.file_offset + s.file_size <= file_size && s.file_size > 0) {
                    s.data.assign(raw + s.file_offset, raw + s.file_offset + s.file_size);
                }
                report.sections.push_back(std::move(s));
            }
        }
        return report;
    }

    report.format = BinaryFormat::Unknown;
    return report;
}

void print_hex_dump(const uint8_t* data, size_t size, uint64_t base_addr) {
    for (size_t i = 0; i < size; i += 16) {
        std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << (base_addr + i) << ": ";
        
        // Hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]) << " ";
            } else {
                std::cout << "   ";
            }
            if (j == 7) std::cout << " ";
        }
        
        // ASCII representation
        std::cout << " |";
        for (size_t j = 0; j < 16 && (i + j) < size; ++j) {
            char c = static_cast<char>(data[i + j]);
            std::cout << (std::isprint(static_cast<unsigned char>(c)) ? c : '.');
        }
        std::cout << "|\n" << std::dec;
    }
}

} // namespace dgtl::objdump

void print_version() {
    std::cout << "dgtl-objdump: DGTL Sovereign Object & Binary Inspector v2.1.0 (TataG Systems)\n"
              << "Standards: Multi-Format Binary Forensics, Symbol & Relocation Inspection Engine\n";
}

void print_usage(std::string_view prog) {
    std::cout << "DGTL Sovereign Object & Binary Inspector (dgtl-objdump)\n"
              << "\nUSAGE:\n"
              << "  " << prog << " [OPTIONS] <input-file>\n"
              << "\nOPTIONS:\n"
              << "  -h, --section-headers, --headers  Display summary of the section headers\n"
              << "  -t, --syms, --symbols             Display the symbol table entries\n"
              << "  -r, --reloc                       Display relocation entries\n"
              << "  -d, -D, --disassemble             Display assembler mnemonics / code disassembly\n"
              << "  -s, --full-contents               Display full contents of all sections requested\n"
              << "  -f, --file-headers                Display summary of the overall file header\n"
              << "  -x, --all-headers                 Display all available header information\n"
              << "  --json                            Output metrics as machine-readable JSON\n"
              << "  --version, -v                     Display version information\n"
              << "  --help, -H                        Display this help menu\n\n"
              << "EXAMPLES:\n"
              << "  " << prog << " -h kernel.o\n"
              << "  " << prog << " -t -r math.obj\n"
              << "  " << prog << " -d dgtl_release.exe\n";
}

using namespace dgtl::objdump;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    bool show_headers = false;
    bool show_symbols = false;
    bool show_relocs = false;
    bool show_disasm = false;
    bool show_contents = false;
    bool show_file_header = false;
    bool show_all = false;
    bool json_mode = false;
    fs::path input_file;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--section-headers" || arg == "--headers") {
            show_headers = true;
        } else if (arg == "-t" || arg == "--syms" || arg == "--symbols") {
            show_symbols = true;
        } else if (arg == "-r" || arg == "--reloc" || arg == "--relocs") {
            show_relocs = true;
        } else if (arg == "-d" || arg == "-D" || arg == "--disassemble" || arg == "--disassemble-all") {
            show_disasm = true;
        } else if (arg == "-s" || arg == "--full-contents") {
            show_contents = true;
        } else if (arg == "-f" || arg == "--file-headers") {
            show_file_header = true;
        } else if (arg == "-x" || arg == "--all-headers") {
            show_all = true;
            show_headers = true;
            show_symbols = true;
            show_relocs = true;
            show_file_header = true;
        } else if (arg == "--json") {
            json_mode = true;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-H" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (input_file.empty()) {
            input_file = argv[i];
        }
    }

    if (input_file.empty()) {
        std::cerr << "error[dgtl-objdump]: no input file specified\n";
        return 1;
    }

    if (!fs::exists(input_file)) {
        std::cerr << "error[dgtl-objdump]: file not found: " << input_file.string() << "\n";
        return 2;
    }

    // Default to section headers if no dump operation specified
    if (!show_headers && !show_symbols && !show_relocs && !show_disasm && !show_contents && !show_file_header) {
        show_headers = true;
    }

    const auto report = dgtl::objdump::inspect_object(input_file);
    if (report.has_error) {
        std::cerr << "error[dgtl-objdump]: " << report.error_message << "\n";
        return 3;
    }

    std::cout << "\n" << input_file.string() << ":     file format ";
    switch (report.format) {
        case dgtl::objdump::BinaryFormat::PE_COFF: std::cout << "pe-x86-64"; break;
        case dgtl::objdump::BinaryFormat::ELF64:   std::cout << "elf64-x86-64"; break;
        case dgtl::objdump::BinaryFormat::MachO64: std::cout << "mach-o-x86-64"; break;
        case dgtl::objdump::BinaryFormat::Archive: std::cout << "archive"; break;
        default: std::cout << "unknown"; break;
    }
    std::cout << "\narchitecture: " << report.arch << ", flags: 0x00000000\n";
    std::cout << "start address: 0x" << std::hex << std::setw(16) << std::setfill('0') << report.entry_point << std::dec << "\n\n";

    if (show_headers || show_all) {
        std::cout << "Sections:\n";
        std::cout << "Idx Name          Size      VMA               LMA               File off  Algn\n";
        for (size_t idx = 0; idx < report.sections.size(); ++idx) {
            const auto& s = report.sections[idx];
            std::cout << std::right << std::setw(3) << idx << " "
                      << std::left << std::setw(13) << s.name << " "
                      << std::hex << std::setw(8) << std::setfill('0') << s.virtual_size << "  "
                      << std::setw(16) << std::setfill('0') << s.virtual_address << "  "
                      << std::setw(16) << std::setfill('0') << s.virtual_address << "  "
                      << std::setw(8) << std::setfill('0') << s.file_offset << "  "
                      << "2**" << std::dec << 4 << "\n";
            std::cout << "                  CONTENTS, " 
                      << (s.is_code ? "ALLOC, LOAD, CODE" : (s.is_writable ? "ALLOC, LOAD, DATA" : "ALLOC, LOAD, READONLY, DATA"))
                      << "\n";
        }
        std::cout << "\n";
    }

    if (show_symbols && !report.symbols.empty()) {
        std::cout << "SYMBOL TABLE:\n";
        for (const auto& sym : report.symbols) {
            std::cout << std::hex << std::setw(16) << std::setfill('0') << sym.value << " "
                      << (sym.is_global ? "g" : "l")
                      << (sym.is_function ? "     F " : "       ")
                      << std::left << std::setw(12) << (sym.section_index == 0 ? "*UND*" : sym.section_name) << " "
                      << std::hex << std::setw(8) << std::setfill('0') << sym.size << " "
                      << sym.name << "\n";
        }
        std::cout << std::dec << "\n";
    }

    if (show_disasm || show_contents) {
        for (const auto& s : report.sections) {
            if (show_disasm && !s.is_code) continue;
            if (s.data.empty()) continue;

            std::cout << "Contents of section " << s.name << ":\n";
            print_hex_dump(s.data.data(), s.data.size(), s.virtual_address);
            std::cout << "\n";
        }
    }

    return 0;
}
