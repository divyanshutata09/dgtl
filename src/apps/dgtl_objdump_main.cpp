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
#include <unordered_map>

import dgtl.tools.common;

extern int dgtl_inspect_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as);

namespace fs = std::filesystem;

namespace dgtl::objdump {

enum class BinaryFormat {
    Unknown,
    PE_COFF,
    ELF64,
    MachO64,
    WASM,
    Archive,
    SMT
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
    uint64_t reloc_offset{0};
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
    std::string type_name;
    int64_t addend{0};
    std::string section_name;
};

struct DumpReport {
    std::string filename;
    BinaryFormat format{BinaryFormat::Unknown};
    std::string arch{"x86_64"};
    uint64_t entry_point{0};
    uint64_t image_base{0};
    uint64_t stack_reserve{0};
    uint64_t stack_commit{0};
    uint64_t heap_reserve{0};
    uint64_t heap_commit{0};
    uint32_t import_rva{0};
    uint32_t import_size{0};
    uint32_t reloc_rva{0};
    uint32_t reloc_size{0};
    uint32_t iat_rva{0};
    uint32_t iat_size{0};
    std::vector<std::string> imported_dlls;
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

inline std::string coff_reloc_type_to_string(uint16_t type) {
    switch (type) {
        case 0x0000: return "IMAGE_REL_AMD64_ABSOLUTE";
        case 0x0001: return "IMAGE_REL_AMD64_ADDR64";
        case 0x0002: return "IMAGE_REL_AMD64_ADDR32";
        case 0x0003: return "IMAGE_REL_AMD64_ADDR32NB";
        case 0x0004: return "IMAGE_REL_AMD64_REL32";
        case 0x0005: return "IMAGE_REL_AMD64_REL32_1";
        case 0x0006: return "IMAGE_REL_AMD64_REL32_2";
        case 0x0007: return "IMAGE_REL_AMD64_REL32_3";
        case 0x0008: return "IMAGE_REL_AMD64_REL32_4";
        case 0x0009: return "IMAGE_REL_AMD64_REL32_5";
        case 0x000A: return "IMAGE_REL_AMD64_SECTION";
        case 0x000B: return "IMAGE_REL_AMD64_SECREL";
        default: return "0x" + std::to_string(type);
    }
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

    // ─── SMT Formal Verification Script Detection (.smt, .smt2) ───────────
    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::string text_preview(reinterpret_cast<const char*>(raw), std::min<size_t>(file_size, 512));
    if (ext == ".smt" || ext == ".smt2" ||
        text_preview.find("(set-logic") != std::string::npos ||
        text_preview.find("(assert") != std::string::npos ||
        text_preview.find("(declare-") != std::string::npos) {
        report.format = BinaryFormat::SMT;
        report.arch = "smt2-solver";
        SectionInfo s;
        s.name = ".smt2";
        s.virtual_size = file_size;
        s.file_size = file_size;
        s.is_code = false;
        s.is_readable = true;
        s.data = buffer;
        report.sections.push_back(std::move(s));
        return report;
    }

    // ─── 1. Archive Detection ──────────────────────────────────────────────
    if (file_size >= 8 && std::memcmp(raw, "!<arch>\n", 8) == 0) {
        report.format = BinaryFormat::Archive;
        return report;
    }

    if (file_size >= 4 && raw[0] == 0x7F && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F') {
        report.format = BinaryFormat::ELF64;
        if (file_size >= 64) {
            const uint16_t e_machine = read16_le(raw + 18);
            if (e_machine == 0x3E) report.arch = "x86_64";
            else if (e_machine == 0xB7) report.arch = "aarch64";
            else if (e_machine == 0xF3) report.arch = "riscv64";
            else report.arch = "elf-" + std::to_string(e_machine);
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
                    s.is_zerofill = (sh_type == 8 /* SHT_NOBITS */);
                    s.file_size = s.is_zerofill ? 0 : sh_size;
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

            report.arch = (machine == 0x8664) ? "x86_64" : ((machine == 0xAA64) ? "arm64" : ((machine == 0x5064) ? "riscv64" : "x86"));

            if (opt_hdr_size >= 24) {
                const uint32_t opt_off = coff_off + 20;
                const uint16_t magic = read16_le(raw + opt_off);
                report.entry_point = read32_le(raw + opt_off + 16);
                if (magic == 0x020B && opt_hdr_size >= 120) { // PE32+
                    report.image_base = read64_le(raw + opt_off + 24);
                    report.stack_reserve = read64_le(raw + opt_off + 72);
                    report.stack_commit  = read64_le(raw + opt_off + 80);
                    report.heap_reserve  = read64_le(raw + opt_off + 88);
                    report.heap_commit   = read64_le(raw + opt_off + 96);
                    if (opt_hdr_size >= 240) {
                        const uint32_t dirs = opt_off + 112;
                        report.import_rva  = read32_le(raw + dirs + 8);
                        report.import_size = read32_le(raw + dirs + 12);
                        report.reloc_rva   = read32_le(raw + dirs + 40);
                        report.reloc_size  = read32_le(raw + dirs + 44);
                        report.iat_rva     = read32_le(raw + dirs + 96);
                        report.iat_size    = read32_le(raw + dirs + 100);
                    }
                } else if (magic == 0x010B && opt_hdr_size >= 104) { // PE32
                    report.image_base = read32_le(raw + opt_off + 28);
                    report.stack_reserve = read32_le(raw + opt_off + 72);
                    report.stack_commit  = read32_le(raw + opt_off + 76);
                    report.heap_reserve  = read32_le(raw + opt_off + 80);
                    report.heap_commit   = read32_le(raw + opt_off + 84);
                    if (opt_hdr_size >= 224) {
                        const uint32_t dirs = opt_off + 96;
                        report.import_rva  = read32_le(raw + dirs + 8);
                        report.import_size = read32_le(raw + dirs + 12);
                        report.reloc_rva   = read32_le(raw + dirs + 40);
                        report.reloc_size  = read32_le(raw + dirs + 44);
                        report.iat_rva     = read32_le(raw + dirs + 96);
                        report.iat_size    = read32_le(raw + dirs + 100);
                    }
                }
            }

            const uint32_t symtab_offset = read32_le(raw + coff_off + 8);
            const uint32_t num_symbols = read32_le(raw + coff_off + 12);
            const size_t string_table_offset = symtab_offset + (static_cast<size_t>(num_symbols) * 18);

            const auto get_string = [&](uint32_t str_offset) -> std::string {
                if (str_offset < 4 || string_table_offset + str_offset >= file_size) {
                    return {};
                }
                const char* start = reinterpret_cast<const char*>(raw + string_table_offset + str_offset);
                const char* end = reinterpret_cast<const char*>(raw + file_size);
                const char* p = start;
                while (p < end && *p != '\0') ++p;
                return std::string(start, static_cast<size_t>(p - start));
            };

            const uint32_t sec_table_off = coff_off + 20 + opt_hdr_size;
            for (uint16_t i = 0; i < num_sections && sec_table_off + (i + 1) * 40 <= file_size; ++i) {
                const uint8_t* sec = raw + sec_table_off + (i * 40);
                char name[9] = {0};
                std::memcpy(name, sec, 8);

                SectionInfo s;
                if (name[0] == '/') {
                    uint32_t str_idx = static_cast<uint32_t>(std::strtoul(name + 1, nullptr, 10));
                    s.name = get_string(str_idx);
                    if (s.name.empty()) s.name = name;
                } else {
                    s.name = name;
                }

                s.virtual_size = read32_le(sec + 8);
                s.virtual_address = read32_le(sec + 12);
                s.file_size = read32_le(sec + 16);
                s.file_offset = read32_le(sec + 20);
                s.reloc_offset = read32_le(sec + 24);
                s.reloc_count = read16_le(sec + 32);
                s.flags = read32_le(sec + 36);

                s.is_code = (s.flags & 0x00000020) != 0;
                s.is_readable = (s.flags & 0x40000000) != 0;
                s.is_writable = (s.flags & 0x80000000) != 0;
                s.is_zerofill = (s.flags & 0x00000080) != 0 || s.name == ".bss";
                if (s.is_zerofill) {
                    s.file_size = 0;
                }

                if (s.file_offset + s.file_size <= file_size && s.file_size > 0) {
                    s.data.assign(raw + s.file_offset, raw + s.file_offset + s.file_size);
                }
                report.sections.push_back(std::move(s));
            }

            auto rva_to_offset = [&](uint32_t rva) -> size_t {
                for (const auto& s : report.sections) {
                    uint32_t limit = (s.virtual_size > 0) ? s.virtual_size : static_cast<uint32_t>(s.file_size);
                    if (rva >= s.virtual_address && rva < s.virtual_address + limit) {
                        return s.file_offset + (rva - s.virtual_address);
                    }
                }
                return 0;
            };

            if (report.import_rva != 0 && report.import_rva < file_size) {
                size_t idt_off = rva_to_offset(report.import_rva);
                if (idt_off != 0 && idt_off + 20 <= file_size) {
                    size_t curr = idt_off;
                    while (curr + 20 <= file_size) {
                        uint32_t ilt_rva = read32_le(raw + curr);
                        uint32_t name_rva = read32_le(raw + curr + 12);
                        uint32_t iat_rva = read32_le(raw + curr + 16);
                        if (ilt_rva == 0 && name_rva == 0 && iat_rva == 0) break;
                        size_t name_off = rva_to_offset(name_rva);
                        if (name_off != 0 && name_off < file_size) {
                            report.imported_dlls.push_back(reinterpret_cast<const char*>(raw + name_off));
                        }
                        curr += 20;
                    }
                }
            }

            // Decode COFF Symbol Table
            std::vector<std::string> coff_symbol_names;
            if (symtab_offset != 0 && symtab_offset + (static_cast<size_t>(num_symbols) * 18) <= file_size) {
                coff_symbol_names.resize(num_symbols);
                for (uint32_t i = 0; i < num_symbols; ++i) {
                    const size_t soff = symtab_offset + (static_cast<size_t>(i) * 18);
                    const uint8_t* sptr = raw + soff;

                    uint32_t zero_check = 0;
                    std::memcpy(&zero_check, sptr, 4);
                    std::string sym_name;
                    if (zero_check == 0) {
                        uint32_t str_off = 0;
                        std::memcpy(&str_off, sptr + 4, 4);
                        sym_name = get_string(str_off);
                    } else {
                        char sname[9] = {0};
                        std::memcpy(sname, sptr, 8);
                        sym_name = sname;
                    }

                    uint32_t val = 0;
                    int16_t sec_num = 0;
                    uint16_t stype = 0;
                    uint8_t sclass = sptr[16];
                    uint8_t num_aux = sptr[17];
                    std::memcpy(&val, sptr + 8, 4);
                    std::memcpy(&sec_num, sptr + 12, 2);
                    std::memcpy(&stype, sptr + 14, 2);

                    if (sym_name.empty() && sec_num > 0 && static_cast<size_t>(sec_num - 1) < report.sections.size()) {
                        sym_name = report.sections[static_cast<size_t>(sec_num - 1)].name;
                    }

                    coff_symbol_names[i] = sym_name;

                    if (!sym_name.empty()) {
                        SymbolInfo sym;
                        sym.name = sym_name;
                        sym.value = val;
                        sym.section_index = sec_num;
                        if (sec_num > 0 && static_cast<size_t>(sec_num - 1) < report.sections.size()) {
                            sym.section_name = report.sections[static_cast<size_t>(sec_num - 1)].name;
                        } else if (sec_num == 0) {
                            sym.section_name = "*UND*";
                            sym.is_external = true;
                        } else {
                            sym.section_name = "*ABS*";
                        }
                        sym.is_global = (sclass == 2);
                        sym.is_function = (stype == 0x20);
                        report.symbols.push_back(std::move(sym));
                    }

                    for (uint8_t a = 1; a <= num_aux && (i + a) < num_symbols; ++a) {
                        coff_symbol_names[i + a] = {};
                    }
                    i += num_aux;
                }
            }

            // Decode COFF Relocations
            for (const auto& s : report.sections) {
                if (s.reloc_offset != 0 && s.reloc_count > 0 &&
                    s.reloc_offset + (static_cast<size_t>(s.reloc_count) * 10) <= file_size) {
                    for (uint16_t r = 0; r < s.reloc_count; ++r) {
                        const size_t roff = s.reloc_offset + (static_cast<size_t>(r) * 10);
                        const uint8_t* rptr = raw + roff;

                        uint32_t rva = read32_le(rptr);
                        uint32_t sym_idx = read32_le(rptr + 4);
                        uint16_t rtype = read16_le(rptr + 8);

                        RelocationInfo rel;
                        rel.offset = rva;
                        rel.type = rtype;
                        rel.type_name = coff_reloc_type_to_string(rtype);
                        rel.section_name = s.name;
                        if (sym_idx < coff_symbol_names.size()) {
                            rel.symbol_name = coff_symbol_names[sym_idx];
                        }
                        if (s.file_offset + rva + 4 <= file_size) {
                            if (rtype == 0x0001 && s.file_offset + rva + 8 <= file_size) {
                                std::memcpy(&rel.addend, raw + s.file_offset + rva, 8);
                            } else {
                                int32_t a32 = 0;
                                std::memcpy(&a32, raw + s.file_offset + rva, 4);
                                rel.addend = a32;
                            }
                        }
                        report.relocations.push_back(std::move(rel));
                    }
                }
            }
        }
        return report;
    }

    // ─── 4. Mach-O 64 Detection ────────────────────────────────────────────
    if (file_size >= 32 && (read32_le(raw) == 0xFEEDFACF || read32_le(raw) == 0xCFFAEDFE)) {
        report.format = BinaryFormat::MachO64;
        const int32_t cputype = static_cast<int32_t>(read32_le(raw + 4));
        report.arch = (cputype == 0x0100000C) ? "arm64" : ((cputype == 0x01000007) ? "x86_64" : "macho64");

        const uint32_t ncmds = read32_le(raw + 16);
        size_t cursor = 32;

        for (uint32_t c = 0; c < ncmds && cursor + 8 <= file_size; ++c) {
            const uint32_t cmd = read32_le(raw + cursor);
            const uint32_t cmdsize = read32_le(raw + cursor + 4);

            if (cmd == 0x19 /* LC_SEGMENT_64 */) {
                if (cursor + 72 <= file_size) {
                    const uint32_t nsects = read32_le(raw + cursor + 64);
                    size_t sect_cursor = cursor + 72;

                    for (uint32_t s = 0; s < nsects && sect_cursor + 80 <= file_size; ++s) {
                        char sectname[17] = {0};
                        char segname[17] = {0};
                        std::memcpy(sectname, raw + sect_cursor, 16);
                        std::memcpy(segname,  raw + sect_cursor + 16, 16);

                        SectionInfo sec;
                        sec.name = sectname;
                        sec.segment = segname;
                        sec.virtual_address = read64_le(raw + sect_cursor + 32);
                        sec.virtual_size    = read64_le(raw + sect_cursor + 40);
                        sec.file_offset     = read32_le(raw + sect_cursor + 48);
                        const uint32_t align_p2 = read32_le(raw + sect_cursor + 52);
                        sec.alignment       = 1u << align_p2;
                        sec.reloc_offset    = read32_le(raw + sect_cursor + 56);
                        sec.reloc_count     = read32_le(raw + sect_cursor + 60);
                        sec.flags           = read32_le(raw + sect_cursor + 64);

                        const uint32_t type = sec.flags & 0xFF;
                        sec.is_zerofill = (type == 0x1 || type == 0x12);
                        sec.file_size = sec.is_zerofill ? 0 : sec.virtual_size;
                        sec.is_code = (sec.flags & 0x80000000) != 0 || sec.name == "__text";
                        sec.is_readable = true;
                        sec.is_writable = (sec.segment == "__DATA");

                        if (sec.file_offset != 0 && sec.file_offset + sec.file_size <= file_size && sec.file_size > 0) {
                            sec.data.assign(raw + sec.file_offset, raw + sec.file_offset + sec.file_size);
                        }

                        report.sections.push_back(std::move(sec));
                        sect_cursor += 80;
                    }
                }
            } else if (cmd == 0x2 /* LC_SYMTAB */ && cursor + 24 <= file_size) {
                const uint32_t symoff = read32_le(raw + cursor + 8);
                const uint32_t nsyms  = read32_le(raw + cursor + 12);
                const uint32_t stroff = read32_le(raw + cursor + 16);
                const uint32_t strsize= read32_le(raw + cursor + 20);

                if (symoff + static_cast<size_t>(nsyms) * 16 <= file_size && stroff + strsize <= file_size) {
                    for (uint32_t i = 0; i < nsyms; ++i) {
                        size_t soff = symoff + static_cast<size_t>(i) * 16;
                        uint32_t n_strx = read32_le(raw + soff);
                        uint8_t n_type  = raw[soff + 4];
                        uint8_t n_sect  = raw[soff + 5];
                        uint64_t n_value= read64_le(raw + soff + 8);

                        std::string sym_name;
                        if (n_strx < strsize && stroff + n_strx < file_size) {
                            sym_name = reinterpret_cast<const char*>(raw + stroff + n_strx);
                        }
                        if (!sym_name.empty()) {
                            SymbolInfo sym;
                            sym.name = sym_name;
                            sym.value = n_value;
                            sym.section_index = n_sect;
                            if (n_sect > 0 && static_cast<size_t>(n_sect - 1) < report.sections.size()) {
                                sym.section_name = report.sections[static_cast<size_t>(n_sect - 1)].name;
                            }
                            sym.is_external = (n_type & 0x01) != 0;
                            sym.is_global = sym.is_external;
                            report.symbols.push_back(std::move(sym));
                        }
                    }
                }
            }
            cursor += cmdsize;
        }
        return report;
    }

    // ─── 5. WASM Detection ─────────────────────────────────────────────────
    if (file_size >= 8 && raw[0] == 0x00 && raw[1] == 'a' && raw[2] == 's' && raw[3] == 'm') {
        report.format = BinaryFormat::WASM;
        report.arch = "wasm32";

        size_t cursor = 8;
        uint32_t sec_index = 0;
        while (cursor < file_size) {
            const uint8_t sec_id = raw[cursor++];
            uint32_t sec_len = 0;
            uint32_t shift = 0;
            while (cursor < file_size) {
                uint8_t b = raw[cursor++];
                sec_len |= (b & 0x7F) << shift;
                if ((b & 0x80) == 0) break;
                shift += 7;
            }

            SectionInfo sec;
            switch (sec_id) {
                case 0:  sec.name = "custom"; break;
                case 1:  sec.name = "type"; break;
                case 2:  sec.name = "import"; break;
                case 3:  sec.name = "function"; break;
                case 4:  sec.name = "table"; break;
                case 5:  sec.name = "memory"; break;
                case 6:  sec.name = "global"; break;
                case 7:  sec.name = "export"; break;
                case 8:  sec.name = "start"; break;
                case 9:  sec.name = "element"; break;
                case 10: sec.name = "code"; sec.is_code = true; break;
                case 11: sec.name = "data"; sec.is_writable = true; break;
                case 12: sec.name = "datacount"; break;
                default: sec.name = "sec_" + std::to_string(sec_id); break;
            }

            sec.file_offset = cursor;
            sec.file_size = sec_len;
            sec.virtual_size = sec_len;
            sec.virtual_address = sec_index * 0x1000;
            sec.is_readable = true;

            if (cursor + sec_len <= file_size) {
                sec.data.assign(raw + cursor, raw + cursor + sec_len);
            }

            // Export symbols
            if (sec_id == 7 && !sec.data.empty()) {
                size_t p = 0;
                auto read_u32_leb = [&]() -> uint32_t {
                    uint32_t res = 0, sh = 0;
                    while (p < sec.data.size()) {
                        uint8_t b = sec.data[p++];
                        res |= (b & 0x7F) << sh;
                        if ((b & 0x80) == 0) break;
                        sh += 7;
                    }
                    return res;
                };
                uint32_t count = read_u32_leb();
                for (uint32_t i = 0; i < count && p < sec.data.size(); ++i) {
                    uint32_t name_len = read_u32_leb();
                    if (p + name_len <= sec.data.size()) {
                        std::string name(reinterpret_cast<const char*>(&sec.data[p]), name_len);
                        p += name_len;
                        if (p < sec.data.size()) {
                            uint8_t kind = sec.data[p++];
                            uint32_t idx = read_u32_leb();
                            SymbolInfo sym;
                            sym.name = name;
                            sym.value = idx;
                            sym.is_global = true;
                            sym.is_function = (kind == 0x00);
                            sym.section_name = "export";
                            report.symbols.push_back(std::move(sym));
                        }
                    }
                }
            }

            report.sections.push_back(std::move(sec));
            cursor += sec_len;
            sec_index++;
        }
        return report;
    }

    report.format = BinaryFormat::Unknown;
    return report;
}

struct DisasmInstr {
    std::string text;
    size_t length{1};
};

inline DisasmInstr decode_x86_instruction(const uint8_t* code, size_t max_len, uint64_t rip) {
    DisasmInstr res;
    if (!code || max_len == 0) {
        res.text = "db 0x00";
        res.length = 1;
        return res;
    }

    size_t offset = 0;
    bool rex_w = false, rex_r = false, rex_x = false, rex_b = false;
    bool has_66 = false, has_67 = false;

    while (offset < max_len) {
        uint8_t pfx = code[offset];
        if (pfx == 0x66) { has_66 = true; ++offset; }
        else if (pfx == 0x67) { has_67 = true; ++offset; }
        else if (pfx == 0xF2 || pfx == 0xF3) { ++offset; }
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
        res.text = "db 0x" + std::to_string(code[0]);
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
    } else if (op == 0xC2 && offset + 2 <= max_len) {
        uint16_t imm16 = read16_le(code + offset); offset += 2;
        res.text = "ret 0x" + std::to_string(imm16);
    } else if (op == 0x90) {
        res.text = "nop";
    } else if (op == 0xCC) {
        res.text = "int3";
    } else if (op == 0xC9) {
        res.text = "leave";
    } else if (op == 0xE8 && offset + 4 <= max_len) {
        int32_t rel = 0; std::memcpy(&rel, &code[offset], 4); offset += 4;
        uint64_t target = rip + offset + rel;
        std::ostringstream ss; ss << "call 0x" << std::hex << target;
        res.text = ss.str();
    } else if (op == 0xE9 && offset + 4 <= max_len) {
        int32_t rel = 0; std::memcpy(&rel, &code[offset], 4); offset += 4;
        uint64_t target = rip + offset + rel;
        std::ostringstream ss; ss << "jmp 0x" << std::hex << target;
        res.text = ss.str();
    } else if (op == 0xEB && offset < max_len) {
        int8_t rel = static_cast<int8_t>(code[offset++]);
        uint64_t target = rip + offset + rel;
        std::ostringstream ss; ss << "jmp 0x" << std::hex << target;
        res.text = ss.str();
    } else if (op >= 0x70 && op <= 0x7F && offset < max_len) {
        static const char* jcc[] = { "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja",
                                     "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg" };
        int8_t rel = static_cast<int8_t>(code[offset++]);
        uint64_t target = rip + offset + rel;
        std::ostringstream ss; ss << jcc[op - 0x70] << " 0x" << std::hex << target;
        res.text = ss.str();
    } else if (op == 0x89 || op == 0x8B || op == 0x8D || op == 0x31 || op == 0x33 || op == 0x85 || op == 0x01 || op == 0x29) {
        std::string rm_str, reg_str;
        uint8_t r_id = 0, rm_id = 0;
        if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
            if (op == 0x89) res.text = "mov " + rm_str + ", " + reg_str;
            else if (op == 0x8B) res.text = "mov " + reg_str + ", " + rm_str;
            else if (op == 0x8D) res.text = "lea " + reg_str + ", " + rm_str;
            else if (op == 0x31) res.text = "xor " + rm_str + ", " + reg_str;
            else if (op == 0x33) res.text = "xor " + reg_str + ", " + rm_str;
            else if (op == 0x85) res.text = "test " + rm_str + ", " + reg_str;
            else if (op == 0x01) res.text = "add " + rm_str + ", " + reg_str;
            else if (op == 0x29) res.text = "sub " + rm_str + ", " + reg_str;
        }
    } else if (op == 0x83 || op == 0x81) {
        std::string rm_str, reg_str;
        uint8_t r_id = 0, rm_id = 0;
        if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id)) {
            static const char* mnem[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
            if (op == 0x83 && offset < max_len) {
                int8_t imm = static_cast<int8_t>(code[offset++]);
                std::ostringstream ss; ss << mnem[r_id & 7] << " " << rm_str << ", 0x" << std::hex << (int)(uint8_t)imm;
                res.text = ss.str();
            } else if (op == 0x81 && offset + 4 <= max_len) {
                uint32_t imm = read32_le(code + offset); offset += 4;
                std::ostringstream ss; ss << mnem[r_id & 7] << " " << rm_str << ", 0x" << std::hex << imm;
                res.text = ss.str();
            }
        }
    } else if (op == 0xC7) {
        std::string rm_str, reg_str;
        uint8_t r_id = 0, rm_id = 0;
        if (decode_modrm(rex_w, has_66, false, rm_str, reg_str, r_id, rm_id) && offset + 4 <= max_len) {
            uint32_t imm = read32_le(code + offset); offset += 4;
            std::ostringstream ss; ss << "mov " << rm_str << ", 0x" << std::hex << imm;
            res.text = ss.str();
        }
    } else if (op >= 0xB8 && op <= 0xBF) {
        uint8_t r = (op - 0xB8) | (rex_b ? 8 : 0);
        if (rex_w && offset + 8 <= max_len) {
            uint64_t imm = read64_le(code + offset); offset += 8;
            std::ostringstream ss; ss << "movabs " << r64[r] << ", 0x" << std::hex << imm;
            res.text = ss.str();
        } else if (offset + 4 <= max_len) {
            uint32_t imm = read32_le(code + offset); offset += 4;
            std::ostringstream ss; ss << "mov " << r32[r] << ", 0x" << std::hex << imm;
            res.text = ss.str();
        }
    } else if (op == 0x0F && offset < max_len) {
        uint8_t op2 = code[offset++];
        if (op2 == 0x05) res.text = "syscall";
        else if (op2 == 0x0B) res.text = "ud2";
        else if (op2 == 0x1F) {
            std::string rm_str, reg_str;
            uint8_t r_id = 0, rm_id = 0;
            decode_modrm(false, false, false, rm_str, reg_str, r_id, rm_id);
            res.text = "nop " + rm_str;
        } else if (op2 >= 0x80 && op2 <= 0x8F && offset + 4 <= max_len) {
            static const char* jcc[] = { "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja",
                                         "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg" };
            int32_t rel = 0; std::memcpy(&rel, &code[offset], 4); offset += 4;
            uint64_t target = rip + offset + rel;
            std::ostringstream ss; ss << jcc[op2 - 0x80] << " 0x" << std::hex << target;
            res.text = ss.str();
        } else {
            std::ostringstream ss; ss << "db 0x0f, 0x" << std::hex << (int)op2;
            res.text = ss.str();
        }
    } else {
        std::ostringstream ss; ss << ".byte 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)op;
        res.text = ss.str();
    }

    res.length = offset;
    return res;
}

void disassemble_section(const SectionInfo& s, const DumpReport& report) {
    if (s.data.empty()) return;

    std::unordered_map<uint64_t, std::string> sym_map;
    for (const auto& sym : report.symbols) {
        if (sym.section_name == s.name || (sym.section_index > 0 && sym.value >= s.virtual_address && sym.value < s.virtual_address + s.virtual_size)) {
            sym_map[sym.value] = sym.name;
        }
    }

    uint64_t cur_rip = s.virtual_address;
    size_t off = 0;
    const uint8_t* p = s.data.data();
    const size_t sz = s.data.size();

    while (off < sz) {
        if (sym_map.contains(cur_rip)) {
            std::cout << "\n" << std::hex << std::setw(16) << std::setfill('0') << cur_rip << " <" << sym_map[cur_rip] << ">:\n" << std::dec;
        }

        DisasmInstr instr = decode_x86_instruction(p + off, sz - off, cur_rip);
        std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << cur_rip << ": ";

        for (size_t b = 0; b < 7; ++b) {
            if (b < instr.length) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)p[off + b] << " ";
            } else {
                std::cout << "   ";
            }
        }
        std::cout << "    " << instr.text << "\n" << std::dec;

        off += instr.length;
        cur_rip += instr.length;
    }
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

namespace {

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

} // namespace

using namespace dgtl::objdump;

int dgtl_objdump_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as) {
    const std::string prog_name = invoked_as.empty() ? "dgtl-objdump" : std::string(invoked_as);
    auto expanded = dgtl::tools::ArgExpander::expand_args(raw_args);

    if (expanded.empty()) {
        print_usage(prog_name);
        return 1;
    }

    // If inspect mode requested, forward directly to dgtl_inspect_tool_main
    for (const auto& a : expanded) {
        if (a == "--inspect" || a == "inspect") {
            return dgtl_inspect_tool_main(raw_args, invoked_as);
        }
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

    for (size_t i = 0; i < expanded.size(); ++i) {
        std::string_view arg = expanded[i];
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
            print_usage(prog_name);
            return 0;
        } else if (input_file.empty()) {
            input_file = expanded[i];
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

    if (json_mode) {
        std::cout << "{\n"
                  << "  \"file\": \"" << input_file.string() << "\",\n"
                  << "  \"arch\": \"" << report.arch << "\",\n"
                  << "  \"entry_point\": " << report.entry_point << ",\n"
                  << "  \"image_base\": " << report.image_base << ",\n"
                  << "  \"stack_reserve\": " << report.stack_reserve << ",\n"
                  << "  \"stack_commit\": " << report.stack_commit << ",\n"
                  << "  \"sections_count\": " << report.sections.size() << ",\n"
                  << "  \"symbols_count\": " << report.symbols.size() << "\n"
                  << "}\n";
        return 0;
    }

    std::cout << "\n" << input_file.string() << ":     file format ";
    switch (report.format) {
        case dgtl::objdump::BinaryFormat::PE_COFF: std::cout << "pe-" << report.arch; break;
        case dgtl::objdump::BinaryFormat::ELF64:   std::cout << "elf64-" << report.arch; break;
        case dgtl::objdump::BinaryFormat::MachO64: std::cout << "mach-o-" << report.arch; break;
        case dgtl::objdump::BinaryFormat::WASM:    std::cout << "wasm-binary"; break;
        case dgtl::objdump::BinaryFormat::Archive: std::cout << "archive"; break;
        case dgtl::objdump::BinaryFormat::SMT:     std::cout << "smt2-formal-verification-script"; break;
        default: std::cout << "unknown"; break;
    }
    std::cout << "\narchitecture: " << report.arch << ", flags: 0x00000000\n";
    std::cout << "start address: 0x" << std::hex << std::setw(16) << std::setfill('0') << report.entry_point << std::dec << "\n\n";

    if (report.format == dgtl::objdump::BinaryFormat::SMT) {
        std::cout << "SMT-LIB2 FORMAL VERIFICATION SCRIPT:\n";
        if (!report.sections.empty()) {
            std::cout << "  Script Size: " << report.sections.front().data.size() << " bytes\n";
            std::cout << "--------------------------------------------------------------------------------\n";
            std::string smt_str(reinterpret_cast<const char*>(report.sections.front().data.data()), report.sections.front().data.size());
            std::cout << smt_str << "\n";
            std::cout << "--------------------------------------------------------------------------------\n";
        }
        return 0;
    }

    if ((show_file_header || show_all) && report.format == dgtl::objdump::BinaryFormat::PE_COFF && report.image_base > 0) {
        std::cout << "PE/COFF Header & Data Directories:\n";
        std::cout << "  Image Base:         0x" << std::hex << report.image_base << std::dec << "\n";
        std::cout << "  Entry Point (RVA):  0x" << std::hex << report.entry_point << std::dec << "\n";
        if (report.stack_reserve > 0) {
            std::cout << "  Stack Reserve:      0x" << std::hex << report.stack_reserve << " (" << std::dec << (report.stack_reserve / 1024) << " KB)\n";
            std::cout << "  Stack Commit:       0x" << std::hex << report.stack_commit << " (" << std::dec << (report.stack_commit / 1024) << " KB)";
            if (report.stack_commit <= 0x10000) {
                std::cout << " [Lean Sovereign - Probed via __chkstk]";
            }
            std::cout << "\n";
            std::cout << "  Heap Reserve:       0x" << std::hex << report.heap_reserve << " (" << std::dec << (report.heap_reserve / 1024) << " KB)\n";
            std::cout << "  Heap Commit:        0x" << std::hex << report.heap_commit << " (" << std::dec << (report.heap_commit / 1024) << " KB)\n";
        }
        std::cout << "  Import Table:       RVA 0x" << std::hex << report.import_rva << " (0x" << report.import_size << " bytes)" << std::dec;
        for (const auto& s : report.sections) {
            if (report.import_rva != 0 && s.virtual_address <= report.import_rva &&
                report.import_rva < s.virtual_address + s.virtual_size) {
                std::cout << " [in " << s.name << "]";
                if (s.name == ".rdata") std::cout << " (Modern Consolidated)";
                break;
            }
        }
        std::cout << "\n";
        std::cout << "  Base Relocations:   RVA 0x" << std::hex << report.reloc_rva << " (0x" << report.reloc_size << " bytes)" << std::dec << "\n";
        std::cout << "  Import Address Tab: RVA 0x" << std::hex << report.iat_rva << " (0x" << report.iat_size << " bytes)" << std::dec << "\n";
        if (!report.imported_dlls.empty()) {
            std::cout << "  Imported DLLs:      ";
            for (size_t i = 0; i < report.imported_dlls.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << report.imported_dlls[i];
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    if (show_headers || show_all) {
        std::cout << "Sections:\n";
        std::cout << "Idx Name          Size      VMA               LMA               File off  Algn\n";
        for (size_t idx = 0; idx < report.sections.size(); ++idx) {
            const auto& s = report.sections[idx];
            std::cout << std::right << std::setw(3) << idx << " "
                      << std::left << std::setw(13) << s.name << " "
                      << std::right << std::hex << std::setw(8) << std::setfill('0') << s.virtual_size << "  "
                      << std::setw(16) << std::setfill('0') << s.virtual_address << "  "
                      << std::setw(16) << std::setfill('0') << s.virtual_address << "  "
                      << std::setw(8) << std::setfill('0') << s.file_offset << "  "
                      << "2**" << std::dec << 4 << "\n";
            if (s.is_zerofill) {
                std::cout << "                  ALLOC, ZEROFILL\n";
            } else {
                std::cout << "                  CONTENTS, " 
                          << (s.is_code ? "ALLOC, LOAD, CODE" : (s.is_writable ? "ALLOC, LOAD, DATA" : "ALLOC, LOAD, READONLY, DATA"))
                          << "\n";
            }
        }
        std::cout << "\n";
    }

    if (show_symbols && !report.symbols.empty()) {
        std::cout << "SYMBOL TABLE:\n";
        for (const auto& sym : report.symbols) {
            std::cout << std::right << std::hex << std::setw(16) << std::setfill('0') << sym.value << " "
                      << (sym.is_global ? "g" : "l")
                      << (sym.is_function ? "     F " : "       ")
                      << std::left << std::setw(12) << (sym.section_index == 0 ? "*UND*" : sym.section_name) << " "
                      << std::right << std::hex << std::setw(8) << std::setfill('0') << sym.size << " "
                      << sym.name << "\n";
        }
        std::cout << std::dec << "\n";
    }

    if ((show_relocs || show_all) && !report.relocations.empty()) {
        std::cout << "RELOCATION RECORDS:\n";
        std::string current_sec;
        for (const auto& rel : report.relocations) {
            if (rel.section_name != current_sec) {
                current_sec = rel.section_name;
                std::cout << "RELOCATION RECORDS FOR [" << current_sec << "]:\n";
                std::cout << "OFFSET           TYPE                     VALUE\n";
            }
            std::cout << std::hex << std::setw(16) << std::setfill('0') << rel.offset << " "
                      << std::left << std::setw(24) << (rel.type_name.empty() ? ("0x" + std::to_string(rel.type)) : rel.type_name) << " "
                      << rel.symbol_name;
            if (rel.addend != 0) {
                if (rel.addend > 0) {
                    std::cout << " + 0x" << std::hex << rel.addend;
                } else {
                    std::cout << " - 0x" << std::hex << (-rel.addend);
                }
            }
            std::cout << "\n";
        }
        std::cout << std::dec << "\n";
    }

    if (show_disasm || show_contents) {
        for (const auto& s : report.sections) {
            if (show_disasm && !s.is_code && !show_contents) continue;
            if (s.data.empty()) continue;

            if (show_disasm && s.is_code) {
                std::cout << "Disassembly of section " << s.name << ":\n";
                disassemble_section(s, report);
                std::cout << "\n";
            } else {
                std::cout << "Contents of section " << s.name << ":\n";
                print_hex_dump(s.data.data(), s.data.size(), s.virtual_address);
                std::cout << "\n";
            }
        }
    }

    return 0;
}
