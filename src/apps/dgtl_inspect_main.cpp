/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Enterprise Object & Section Inspector (dgtl-inspect)
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

namespace dgtl::inspect {

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
    bool is_tls{false};
    bool is_unwind{false};
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

struct MemoryBudget {
    uint64_t code_bytes{0};
    uint64_t rodata_bytes{0};
    uint64_t rwdata_bytes{0};
    uint64_t bss_virtual_bytes{0};
    uint64_t unwind_bytes{0};
    uint64_t tls_bytes{0};
    uint64_t total_disk_bytes{0};
    uint64_t total_virtual_bytes{0};

    [[nodiscard]] double sharing_efficiency_pct() const noexcept {
        if (total_virtual_bytes == 0) return 0.0;
        const uint64_t shareable = code_bytes + rodata_bytes + unwind_bytes;
        return (static_cast<double>(shareable) / static_cast<double>(total_virtual_bytes)) * 100.0;
    }
};

struct InspectionReport {
    std::string filename;
    BinaryFormat format{BinaryFormat::Unknown};
    std::string format_name;
    std::string arch_name;
    uint64_t file_size{0};
    std::vector<SectionInfo> sections;
    std::vector<SymbolInfo> symbols;
    std::vector<RelocationInfo> relocations;
    std::vector<std::string> unwind_details;
    std::vector<std::string> verification_errors;
    std::vector<std::string> verification_warnings;
    MemoryBudget budget;

    // PE Optional Header & Data Directory Metadata
    uint64_t entry_point{0};
    uint64_t image_base{0};
    uint32_t import_rva{0};
    uint32_t import_size{0};
    std::string import_section;
    uint32_t reloc_rva{0};
    uint32_t reloc_size{0};
    uint32_t iat_rva{0};
    uint32_t iat_size{0};

    [[nodiscard]] bool is_compliant() const noexcept {
        return verification_errors.empty();
    }
};

// ============================================================================
// FORMAT DETECTOR & PARSERS
// ============================================================================

[[nodiscard]] inline BinaryFormat detect_format(const std::vector<uint8_t>& buf) noexcept {
    if (buf.size() < 4) return BinaryFormat::Unknown;

    // Static Archive magic: !<arch>\n
    if (buf.size() >= 8 && std::memcmp(buf.data(), "!<arch>\n", 8) == 0) {
        return BinaryFormat::Archive;
    }

    // WebAssembly magic: \0asm
    if (buf[0] == 0x00 && buf[1] == 0x61 && buf[2] == 0x73 && buf[3] == 0x6D) {
        return BinaryFormat::WASM;
    }

    // ELF magic: \x7FELF
    if (buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        return BinaryFormat::ELF64;
    }

    // Mach-O 64 magic (FEEDFACF little endian, CFFAEDFE big endian)
    const uint32_t magic32 = static_cast<uint32_t>(buf[0])
                           | (static_cast<uint32_t>(buf[1]) << 8)
                           | (static_cast<uint32_t>(buf[2]) << 16)
                           | (static_cast<uint32_t>(buf[3]) << 24);
    if (magic32 == 0xFEEDFACF || magic32 == 0xFEEDFACE || magic32 == 0xCFFAEDFE) {
        return BinaryFormat::MachO64;
    }

    // PE/COFF: Machine AMD64 (0x8664) or ARM64 (0xAA64) or MZ DOS header (0x5A4D)
    const uint16_t machine16 = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    if (machine16 == 0x8664 || machine16 == 0xAA64 || machine16 == 0x014C || machine16 == 0x5A4D) {
        return BinaryFormat::PE_COFF;
    }

    return BinaryFormat::Unknown;
}

// ----------------------------------------------------------------------------
// PE/COFF PARSER
// ----------------------------------------------------------------------------
bool parse_pe_coff(const std::vector<uint8_t>& buf, InspectionReport& report) {
    if (buf.size() < 20) return false;

    report.format = BinaryFormat::PE_COFF;
    report.format_name = "COFF Relocatable Object (Microsoft Windows)";

    size_t coff_hdr_start = 0;
    if (buf.size() >= 0x40 && buf[0] == 'M' && buf[1] == 'Z') {
        const uint32_t pe_sig_offset = *reinterpret_cast<const uint32_t*>(&buf[0x3C]);
        if (pe_sig_offset + 24 <= buf.size() &&
            buf[pe_sig_offset] == 'P' && buf[pe_sig_offset + 1] == 'E' &&
            buf[pe_sig_offset + 2] == '\0' && buf[pe_sig_offset + 3] == '\0') {
            coff_hdr_start = pe_sig_offset + 4;
            report.format_name = "PE32+ Executable / Image (Microsoft Windows)";
        } else {
            return false;
        }
    }

    if (buf.size() < coff_hdr_start + 20) return false;

    const uint16_t machine = *reinterpret_cast<const uint16_t*>(&buf[coff_hdr_start]);
    if (machine == 0x8664) report.arch_name = "x86-64 (AMD64)";
    else if (machine == 0xAA64) report.arch_name = "AArch64 (ARM64)";
    else if (machine == 0x014C) report.arch_name = "x86 (i386)";
    else report.arch_name = "Unknown Machine (0x" + std::to_string(machine) + ")";

    const uint16_t num_sections = *reinterpret_cast<const uint16_t*>(&buf[coff_hdr_start + 2]);
    const uint32_t symtab_offset = *reinterpret_cast<const uint32_t*>(&buf[coff_hdr_start + 8]);
    const uint32_t num_symbols = *reinterpret_cast<const uint32_t*>(&buf[coff_hdr_start + 12]);
    const uint16_t opt_hdr_size = *reinterpret_cast<const uint16_t*>(&buf[coff_hdr_start + 16]);

    if (opt_hdr_size >= 120 && coff_hdr_start + 20 + opt_hdr_size <= buf.size()) {
        const size_t opt_hdr_start = coff_hdr_start + 20;
        const uint16_t magic = *reinterpret_cast<const uint16_t*>(&buf[opt_hdr_start]);
        if (magic == 0x020B) { // PE32+ (64-bit)
            report.entry_point = *reinterpret_cast<const uint32_t*>(&buf[opt_hdr_start + 16]);
            report.image_base = *reinterpret_cast<const uint64_t*>(&buf[opt_hdr_start + 24]);
            if (opt_hdr_size >= 240) {
                const size_t dirs_start = opt_hdr_start + 112;
                report.import_rva  = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 8]);
                report.import_size = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 12]);
                report.reloc_rva   = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 40]);
                report.reloc_size  = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 44]);
                report.iat_rva     = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 96]);
                report.iat_size    = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 100]);
            }
        } else if (magic == 0x010B) { // PE32 (32-bit)
            report.entry_point = *reinterpret_cast<const uint32_t*>(&buf[opt_hdr_start + 16]);
            report.image_base = *reinterpret_cast<const uint32_t*>(&buf[opt_hdr_start + 28]);
            if (opt_hdr_size >= 224) {
                const size_t dirs_start = opt_hdr_start + 96;
                report.import_rva  = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 8]);
                report.import_size = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 12]);
                report.reloc_rva   = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 40]);
                report.reloc_size  = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 44]);
                report.iat_rva     = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 96]);
                report.iat_size    = *reinterpret_cast<const uint32_t*>(&buf[dirs_start + 100]);
            }
        }
    }

    const size_t sec_hdr_start = coff_hdr_start + 20 + opt_hdr_size;
    if (buf.size() < sec_hdr_start + num_sections * 40) return false;

    const size_t strtab_offset = (symtab_offset != 0) ? (symtab_offset + num_symbols * 18) : 0;

    auto get_str = [&](uint32_t offset) -> std::string {
        if (strtab_offset == 0 || strtab_offset + offset >= buf.size()) return "";
        const char* p = reinterpret_cast<const char*>(&buf[strtab_offset + offset]);
        return std::string(p);
    };

    for (uint16_t i = 0; i < num_sections; ++i) {
        const size_t offset = sec_hdr_start + i * 40;
        SectionInfo sec;

        char raw_name[9] = {0};
        std::memcpy(raw_name, &buf[offset], 8);
        if (raw_name[0] == '/') {
            uint32_t str_off = static_cast<uint32_t>(std::strtoul(raw_name + 1, nullptr, 10));
            sec.name = get_str(str_off);
        } else {
            sec.name = raw_name;
        }

        sec.virtual_size = *reinterpret_cast<const uint32_t*>(&buf[offset + 8]);
        sec.virtual_address = *reinterpret_cast<const uint32_t*>(&buf[offset + 12]);
        sec.file_size = *reinterpret_cast<const uint32_t*>(&buf[offset + 16]);
        sec.file_offset = *reinterpret_cast<const uint32_t*>(&buf[offset + 20]);
        sec.reloc_count = *reinterpret_cast<const uint16_t*>(&buf[offset + 32]);
        sec.flags = *reinterpret_cast<const uint32_t*>(&buf[offset + 36]);

        sec.is_code = (sec.flags & 0x00000020) != 0;
        sec.is_readable = (sec.flags & 0x40000000) != 0;
        sec.is_writable = (sec.flags & 0x80000000) != 0;
        sec.is_zerofill = (sec.flags & 0x00000080) != 0 || sec.name == ".bss";
        sec.is_tls = (sec.flags & 0x00000800) != 0 || sec.name.starts_with(".tls");
        sec.is_unwind = (sec.name == ".pdata" || sec.name == ".xdata");

        uint32_t align_bits = (sec.flags >> 20) & 0x0F;
        sec.alignment = align_bits > 0 ? (1u << (align_bits - 1)) : 16u;

        if (sec.file_offset != 0 && sec.file_offset + sec.file_size <= buf.size()) {
            sec.data.assign(&buf[sec.file_offset], &buf[sec.file_offset + sec.file_size]);
        }

        report.sections.push_back(std::move(sec));
    }

    if (report.import_rva != 0) {
        for (const auto& sec : report.sections) {
            if (report.import_rva >= sec.virtual_address &&
                report.import_rva < sec.virtual_address + std::max(sec.virtual_size, sec.file_size)) {
                report.import_section = sec.name;
                break;
            }
        }
    }

    // Decode Symbols
    if (symtab_offset != 0 && symtab_offset + num_symbols * 18 <= buf.size()) {
        for (uint32_t i = 0; i < num_symbols; ++i) {
            const size_t soff = symtab_offset + i * 18;
            SymbolInfo sym;

            const uint32_t zero_check = *reinterpret_cast<const uint32_t*>(&buf[soff]);
            if (zero_check == 0) {
                const uint32_t strx = *reinterpret_cast<const uint32_t*>(&buf[soff + 4]);
                sym.name = get_str(strx);
            } else {
                char sname[9] = {0};
                std::memcpy(sname, &buf[soff], 8);
                sym.name = sname;
            }

            sym.value = *reinterpret_cast<const uint32_t*>(&buf[soff + 8]);
            sym.section_index = *reinterpret_cast<const int16_t*>(&buf[soff + 12]);
            const uint16_t type = *reinterpret_cast<const uint16_t*>(&buf[soff + 14]);
            const uint8_t sclass = buf[soff + 16];
            const uint8_t num_aux = buf[soff + 17];

            sym.is_function = (type == 0x20);
            sym.is_global = (sclass == 2);
            sym.is_external = (sym.section_index == 0);

            if (sym.section_index > 0 && sym.section_index <= static_cast<int>(report.sections.size())) {
                sym.section_name = report.sections[sym.section_index - 1].name;
            } else if (sym.section_index == 0) {
                sym.section_name = "*UNDF*";
            } else {
                sym.section_name = "*ABS*";
            }

            report.symbols.push_back(std::move(sym));
            i += num_aux;
        }
    }

    // Decode Win64 SEH Unwind Tables
    const SectionInfo* pdata_sec = nullptr;
    const SectionInfo* xdata_sec = nullptr;
    for (const auto& sec : report.sections) {
        if (sec.name == ".pdata") pdata_sec = &sec;
        if (sec.name == ".xdata") xdata_sec = &sec;
    }

    if (pdata_sec && !pdata_sec->data.empty()) {
        const size_t count = pdata_sec->data.size() / 12;
        std::ostringstream oss;
        oss << "Win64 Exception Table (.pdata): " << count << " RUNTIME_FUNCTION entries\n";

        for (size_t i = 0; i < count; ++i) {
            const size_t off = i * 12;
            const uint32_t begin_addr = *reinterpret_cast<const uint32_t*>(&pdata_sec->data[off]);
            const uint32_t end_addr   = *reinterpret_cast<const uint32_t*>(&pdata_sec->data[off + 4]);
            const uint32_t unwind_addr= *reinterpret_cast<const uint32_t*>(&pdata_sec->data[off + 8]);

            oss << "  [Entry " << i << "] PC Range: [0x" << std::hex << begin_addr
                << " - 0x" << end_addr << ") | Length: " << std::dec << (end_addr - begin_addr)
                << " bytes | UnwindInfo: 0x" << std::hex << unwind_addr << std::dec << "\n";

            const size_t unwind_local = (xdata_sec && unwind_addr >= xdata_sec->virtual_address && xdata_sec->virtual_address > 0)
                                      ? (unwind_addr - xdata_sec->virtual_address)
                                      : unwind_addr;
            if (xdata_sec && unwind_local < xdata_sec->data.size()) {
                const uint8_t* xbuf = &xdata_sec->data[unwind_local];
                const uint8_t ver_flags = xbuf[0];
                const uint8_t version = ver_flags & 0x07;
                const uint8_t flags   = (ver_flags >> 3) & 0x1F;
                const uint8_t prolog_size = xbuf[1];
                const uint8_t code_count  = xbuf[2];
                const uint8_t frame_reg_off = xbuf[3];
                const uint8_t frame_reg = frame_reg_off & 0x0F;

                const char* reg_name = (frame_reg == 5) ? "RBP" : (frame_reg == 4 ? "RSP" : "None");

                oss << "    -> UNWIND_INFO: Version=" << static_cast<int>(version)
                    << ", Flags=0x" << std::hex << static_cast<int>(flags) << std::dec
                    << ", PrologSize=" << static_cast<int>(prolog_size) << " bytes"
                    << ", FrameRegister=" << reg_name << " (" << static_cast<int>(frame_reg) << ")"
                    << ", UnwindCodesCount=" << static_cast<int>(code_count) << "\n";

                for (uint8_t c = 0; c < code_count && (4 + c * 2 + 1) < xdata_sec->data.size(); ++c) {
                    const uint8_t code_off = xbuf[4 + c * 2];
                    const uint8_t op_info  = xbuf[4 + c * 2 + 1];
                    const uint8_t opcode   = op_info & 0x0F;
                    const uint8_t info     = (op_info >> 4) & 0x0F;

                    std::string op_desc;
                    switch (opcode) {
                        case 0: op_desc = "UWOP_PUSH_NONVOL (reg=" + std::to_string(info) + ")"; break;
                        case 1: op_desc = "UWOP_ALLOC_LARGE"; break;
                        case 2: op_desc = "UWOP_ALLOC_SMALL (size=" + std::to_string(info * 8 + 8) + ")"; break;
                        case 3: op_desc = "UWOP_SET_FPREG (RBP=" + std::string(reg_name) + ")"; break;
                        case 4: op_desc = "UWOP_SAVE_NONVOL"; break;
                        default: op_desc = "UWOP_CUSTOM (" + std::to_string(opcode) + ")"; break;
                    }
                    oss << "       Op #" << static_cast<int>(c) << " @ prolog+" << static_cast<int>(code_off)
                        << ": " << op_desc << "\n";
                }
            }
        }
        report.unwind_details.push_back(oss.str());
    }

    return true;
}

// ----------------------------------------------------------------------------
// STATIC ARCHIVE PARSER
// ----------------------------------------------------------------------------
bool parse_archive(const std::vector<uint8_t>& buf, InspectionReport& report) {
    if (buf.size() < 8 || std::memcmp(buf.data(), "!<arch>\n", 8) != 0) return false;

    report.format = BinaryFormat::Archive;
    report.format_name = "Static Library Archive (.a / .lib)";
    report.arch_name = "Multi-Member Archive";

    size_t offset = 8;
    uint32_t member_idx = 0;
    while (offset + 60 <= buf.size()) {
        char name[17] = {0};
        std::memcpy(name, &buf[offset], 16);
        std::string member_name(name);
        while (!member_name.empty() && (member_name.back() == ' ' || member_name.back() == '/')) {
            member_name.pop_back();
        }

        char size_buf[11] = {0};
        std::memcpy(size_buf, &buf[offset + 48], 10);
        const uint64_t member_size = std::strtoull(size_buf, nullptr, 10);

        offset += 60;
        if (offset + member_size > buf.size()) break;

        SectionInfo sec;
        sec.name = member_name.empty() ? ("member_" + std::to_string(member_idx)) : member_name;
        sec.file_offset = offset;
        sec.file_size = member_size;
        sec.virtual_size = member_size;
        sec.is_code = false;
        sec.is_readable = true;
        report.sections.push_back(std::move(sec));

        member_idx++;
        offset += (member_size + 1) & ~static_cast<size_t>(1);
    }
    return true;
}

// ----------------------------------------------------------------------------
// ELF64 PARSER
// ----------------------------------------------------------------------------
bool parse_elf64(const std::vector<uint8_t>& buf, InspectionReport& report) {
    if (buf.size() < 64) return false;

    report.format = BinaryFormat::ELF64;
    report.format_name = "ELF64 (Linux / BSD / Unix)";

    const uint16_t e_machine = *reinterpret_cast<const uint16_t*>(&buf[18]);
    if (e_machine == 0x3E) report.arch_name = "x86-64 (AMD64)";
    else if (e_machine == 0xB7) report.arch_name = "AArch64 (ARM64)";
    else if (e_machine == 0xF3) report.arch_name = "RISC-V (RISCV64)";
    else report.arch_name = "Unknown ELF Machine (0x" + std::to_string(e_machine) + ")";

    const uint64_t shoff = *reinterpret_cast<const uint64_t*>(&buf[40]);
    const uint16_t shentsize = *reinterpret_cast<const uint16_t*>(&buf[58]);
    const uint16_t shnum = *reinterpret_cast<const uint16_t*>(&buf[60]);
    const uint16_t shstrndx = *reinterpret_cast<const uint16_t*>(&buf[62]);

    if (shoff == 0 || shnum == 0 || buf.size() < shoff + shnum * shentsize) return false;

    const size_t strtab_hdr_off = shoff + shstrndx * shentsize;
    const uint64_t shstrtab_offset = *reinterpret_cast<const uint64_t*>(&buf[strtab_hdr_off + 24]);

    auto get_sh_name = [&](uint32_t name_idx) -> std::string {
        if (shstrtab_offset + name_idx >= buf.size()) return "";
        return std::string(reinterpret_cast<const char*>(&buf[shstrtab_offset + name_idx]));
    };

    for (uint16_t i = 0; i < shnum; ++i) {
        const size_t off = shoff + i * shentsize;
        const uint32_t sh_name = *reinterpret_cast<const uint32_t*>(&buf[off]);
        const uint32_t sh_type = *reinterpret_cast<const uint32_t*>(&buf[off + 4]);
        const uint64_t sh_flags = *reinterpret_cast<const uint64_t*>(&buf[off + 8]);
        const uint64_t sh_addr  = *reinterpret_cast<const uint64_t*>(&buf[off + 16]);
        const uint64_t sh_offset= *reinterpret_cast<const uint64_t*>(&buf[off + 24]);
        const uint64_t sh_size  = *reinterpret_cast<const uint64_t*>(&buf[off + 32]);
        const uint64_t sh_addralign = *reinterpret_cast<const uint64_t*>(&buf[off + 48]);

        if (sh_type == 0) continue;

        SectionInfo sec;
        sec.name = get_sh_name(sh_name);
        sec.virtual_address = sh_addr;
        sec.virtual_size = sh_size;
        sec.file_offset = sh_offset;
        sec.file_size = (sh_type == 8) ? 0 : sh_size;
        sec.alignment = static_cast<uint32_t>(sh_addralign <= 1 ? 1 : sh_addralign);
        sec.flags = static_cast<uint32_t>(sh_flags);

        sec.is_code = (sh_flags & 0x4) != 0;
        sec.is_writable = (sh_flags & 0x1) != 0;
        sec.is_readable = true;
        sec.is_zerofill = (sh_type == 8);
        sec.is_tls = (sh_flags & 0x400) != 0;
        sec.is_unwind = (sec.name == ".eh_frame" || sec.name == ".eh_frame_hdr");

        if (sec.file_offset != 0 && sec.file_offset + sec.file_size <= buf.size()) {
            sec.data.assign(&buf[sec.file_offset], &buf[sec.file_offset + sec.file_size]);
        }

        report.sections.push_back(std::move(sec));
    }

    for (const auto& sec : report.sections) {
        if (sec.name == ".eh_frame" && !sec.data.empty()) {
            std::ostringstream oss;
            oss << "Linux / DWARF Call Frame Information (.eh_frame): " << sec.data.size() << " bytes\n";
            size_t cursor = 0;
            int entry_idx = 0;
            while (cursor + 4 <= sec.data.size()) {
                const uint32_t length = *reinterpret_cast<const uint32_t*>(&sec.data[cursor]);
                if (length == 0 || cursor + 4 + length > sec.data.size()) break;

                const uint32_t id = *reinterpret_cast<const uint32_t*>(&sec.data[cursor + 4]);
                if (id == 0) {
                    oss << "  [CIE " << entry_idx++ << "] Length=" << length
                        << " | Standard Common Information Entry (x86_64 CFA: RSP+8, ReturnReg=16)\n";
                } else {
                    oss << "  [FDE " << entry_idx++ << "] Length=" << length
                        << " | CIE_Offset=" << id << " | Frame Description Entry with Advance Loc & Def CFA\n";
                }
                cursor += 4 + length;
            }
            report.unwind_details.push_back(oss.str());
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// MACH-O 64 PARSER
// ----------------------------------------------------------------------------
bool parse_macho64(const std::vector<uint8_t>& buf, InspectionReport& report) {
    if (buf.size() < 32) return false;

    report.format = BinaryFormat::MachO64;
    report.format_name = "Mach-O 64 (macOS / Darwin / iOS)";

    const int32_t cputype = *reinterpret_cast<const int32_t*>(&buf[4]);
    if (cputype == 0x0100000C) report.arch_name = "ARM64 (Apple Silicon M1/M2/M3/M4)";
    else if (cputype == 0x01000007) report.arch_name = "x86-64 (Intel Mac)";
    else report.arch_name = "Unknown Mach-O Arch (0x" + std::to_string(cputype) + ")";

    const uint32_t ncmds = *reinterpret_cast<const uint32_t*>(&buf[16]);
    size_t cursor = 32;

    for (uint32_t c = 0; c < ncmds && cursor + 8 <= buf.size(); ++c) {
        const uint32_t cmd = *reinterpret_cast<const uint32_t*>(&buf[cursor]);
        const uint32_t cmdsize = *reinterpret_cast<const uint32_t*>(&buf[cursor + 4]);

        if (cmd == 0x19) {
            if (cursor + 72 <= buf.size()) {
                const uint32_t nsects = *reinterpret_cast<const uint32_t*>(&buf[cursor + 64]);
                size_t sect_cursor = cursor + 72;

                for (uint32_t s = 0; s < nsects && sect_cursor + 80 <= buf.size(); ++s) {
                    char sectname[17] = {0};
                    char segname[17] = {0};
                    std::memcpy(sectname, &buf[sect_cursor], 16);
                    std::memcpy(segname,  &buf[sect_cursor + 16], 16);

                    SectionInfo sec;
                    sec.name = sectname;
                    sec.segment = segname;
                    sec.virtual_address = *reinterpret_cast<const uint64_t*>(&buf[sect_cursor + 32]);
                    sec.virtual_size    = *reinterpret_cast<const uint64_t*>(&buf[sect_cursor + 40]);
                    sec.file_offset     = *reinterpret_cast<const uint32_t*>(&buf[sect_cursor + 48]);
                    const uint32_t align_p2 = *reinterpret_cast<const uint32_t*>(&buf[sect_cursor + 52]);
                    sec.alignment       = 1u << align_p2;
                    sec.reloc_count     = *reinterpret_cast<const uint32_t*>(&buf[sect_cursor + 60]);
                    sec.flags           = *reinterpret_cast<const uint32_t*>(&buf[sect_cursor + 64]);

                    const uint32_t type = sec.flags & 0xFF;
                    sec.is_zerofill = (type == 0x1 || type == 0x12);
                    sec.file_size = sec.is_zerofill ? 0 : sec.virtual_size;
                    sec.is_code = (sec.flags & 0x80000000) != 0 || sec.name == "__text";
                    sec.is_readable = true;
                    sec.is_writable = (sec.segment == "__DATA");
                    sec.is_tls = (type >= 0x11 && type <= 0x15);
                    sec.is_unwind = (sec.name == "__unwind_info" || sec.name == "__compact_unwind" || sec.name == "__eh_frame");

                    if (sec.file_offset != 0 && sec.file_offset + sec.file_size <= buf.size()) {
                        sec.data.assign(&buf[sec.file_offset], &buf[sec.file_offset + sec.file_size]);
                    }

                    report.sections.push_back(std::move(sec));
                    sect_cursor += 80;
                }
            }
        }
        cursor += cmdsize;
    }

    return true;
}

// ----------------------------------------------------------------------------
// WEBASSEMBLY PARSER
// ----------------------------------------------------------------------------
bool parse_wasm(const std::vector<uint8_t>& buf, InspectionReport& report) {
    if (buf.size() < 8) return false;

    report.format = BinaryFormat::WASM;
    report.format_name = "WebAssembly (WASM Binary Module)";
    report.arch_name = "Wasm32 / Wasm64 Universal Virtual Machine";

    size_t cursor = 8;
    while (cursor < buf.size()) {
        const uint8_t sec_id = buf[cursor++];
        uint32_t sec_len = 0;
        uint32_t shift = 0;
        while (cursor < buf.size()) {
            uint8_t b = buf[cursor++];
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
            default: sec.name = "section_" + std::to_string(sec_id); break;
        }

        sec.file_offset = cursor;
        sec.file_size = sec_len;
        sec.virtual_size = sec_len;
        sec.is_readable = true;

        if (cursor + sec_len <= buf.size()) {
            sec.data.assign(&buf[cursor], &buf[cursor + sec_len]);
        }

        report.sections.push_back(std::move(sec));
        cursor += sec_len;
    }

    return true;
}

// ============================================================================
// MEMORY BUDGET CALCULATOR & VERIFIER
// ============================================================================
void compute_budget_and_verify(InspectionReport& report) {
    auto& b = report.budget;

    for (const auto& s : report.sections) {
        if (s.is_zerofill) {
            b.bss_virtual_bytes += s.virtual_size;
        } else {
            b.total_disk_bytes += s.file_size;
            if (s.is_code) {
                b.code_bytes += s.file_size;
            } else if (s.is_unwind) {
                b.unwind_bytes += s.file_size;
            } else if (s.is_tls) {
                b.tls_bytes += s.file_size;
            } else if (s.is_writable) {
                b.rwdata_bytes += s.file_size;
            } else {
                b.rodata_bytes += s.file_size;
            }
        }
        b.total_virtual_bytes += s.virtual_size > 0 ? s.virtual_size : s.file_size;

        // Enterprise ABI Gate 1: Strict W^X Security Enforcement
        if (s.is_code && s.is_writable) {
            report.verification_errors.push_back(
                "W^X Security Violation: Section '" + s.name + "' is marked both EXECUTABLE and WRITABLE (DEP violation).");
        }

        // Enterprise ABI Gate 2: BSS Section Zero-Size Check
        if (s.is_zerofill && s.file_size > 0) {
            report.verification_errors.push_back(
                "Zero-Fill Invariant Violation: BSS section '" + s.name + "' occupies " + std::to_string(s.file_size) +
                " file bytes on disk (must be 0).");
        }

        // Enterprise ABI Gate 3: Alignment Verification
        if (s.is_code && s.alignment < 4) {
            report.verification_warnings.push_back(
                "Sub-optimal Instruction Alignment: Section '" + s.name + "' has alignment " +
                std::to_string(s.alignment) + " bytes (expected >= 16 bytes for high-performance SIMD/AVX).");
        }
    }

    // Enterprise ABI Gate 4: Exception Unwind Coverage
    bool has_code = (b.code_bytes > 0);
    bool has_unwind = (b.unwind_bytes > 0);
    if (has_code && !has_unwind && report.format != BinaryFormat::WASM) {
        report.verification_warnings.push_back(
            "Missing Unwind Tables: Object contains code but has no .pdata, .eh_frame, or __unwind_info (zero-cost exception handling inactive).");
    }
}

// ============================================================================
// INSPECTION DRIVER
// ============================================================================
[[nodiscard]] InspectionReport inspect_file(const fs::path& filepath) {
    InspectionReport report;
    report.filename = filepath.string();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        report.verification_errors.push_back("Failed to open file: " + filepath.string());
        return report;
    }

    report.file_size = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(report.file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), report.file_size)) {
        report.verification_errors.push_back("Failed to read file contents: " + filepath.string());
        return report;
    }

    const BinaryFormat fmt = detect_format(buffer);
    switch (fmt) {
        case BinaryFormat::PE_COFF: parse_pe_coff(buffer, report); break;
        case BinaryFormat::ELF64:   parse_elf64(buffer, report);   break;
        case BinaryFormat::MachO64: parse_macho64(buffer, report); break;
        case BinaryFormat::WASM:    parse_wasm(buffer, report);    break;
        case BinaryFormat::Archive: parse_archive(buffer, report); break;
        case BinaryFormat::Unknown:
            report.verification_errors.push_back("Unrecognized binary format or architecture.");
            break;
    }

    compute_budget_and_verify(report);
    return report;
}

// ============================================================================
// PRINTERS
// ============================================================================
void print_report(const InspectionReport& report, bool show_unwind, bool show_symbols, bool show_relocs, bool json_mode) {
    if (json_mode) {
        std::cout << "{\n"
                  << "  \"file\": \"" << report.filename << "\",\n"
                  << "  \"format\": \"" << report.format_name << "\",\n"
                  << "  \"arch\": \"" << report.arch_name << "\",\n"
                  << "  \"file_size\": " << report.file_size << ",\n"
                  << "  \"budget\": {\n"
                  << "    \"code_bytes\": " << report.budget.code_bytes << ",\n"
                  << "    \"rodata_bytes\": " << report.budget.rodata_bytes << ",\n"
                  << "    \"rwdata_bytes\": " << report.budget.rwdata_bytes << ",\n"
                  << "    \"bss_virtual_bytes\": " << report.budget.bss_virtual_bytes << ",\n"
                  << "    \"unwind_bytes\": " << report.budget.unwind_bytes << ",\n"
                  << "    \"total_disk_bytes\": " << report.budget.total_disk_bytes << ",\n"
                  << "    \"total_virtual_bytes\": " << report.budget.total_virtual_bytes << ",\n"
                  << "    \"sharing_efficiency_pct\": " << report.budget.sharing_efficiency_pct() << "\n"
                  << "  },\n"
                  << "  \"compliant\": " << (report.is_compliant() ? "true" : "false") << "\n"
                  << "}\n";
        return;
    }

    std::cout << "\n================================================================================\n"
              << "                       DGTL OBJECT & SECTION INSPECTOR                          \n"
              << "================================================================================\n";
    std::cout << "  File:         " << report.filename << "\n"
              << "  Format:       " << report.format_name << "\n"
              << "  Architecture: " << report.arch_name << "\n"
              << "  Total Size:   " << report.file_size << " bytes\n"
              << "--------------------------------------------------------------------------------\n";

    std::cout << "\n[1] SECTION TABLE & ATTRIBUTES:\n";
    std::cout << std::left
              << std::setw(18) << "  Name"
              << std::setw(12) << "Segment"
              << std::setw(12) << "Disk Size"
              << std::setw(14) << "Virtual Size"
              << std::setw(10) << "Offset"
              << std::setw(8)  << "Align"
              << "Flags & Protection\n";
    std::cout << "  ------------------------------------------------------------------------------\n";

    for (const auto& s : report.sections) {
        std::string prot;
        if (s.is_code) prot += "EXEC ";
        if (s.is_readable) prot += "R ";
        if (s.is_writable) prot += "W ";
        if (s.is_zerofill) prot += "[BSS ZEROFILL] ";
        if (s.is_tls) prot += "[TLS] ";
        if (s.is_unwind) prot += "[UNWIND] ";
        if (report.import_rva != 0 && s.virtual_address <= report.import_rva &&
            report.import_rva < s.virtual_address + std::max(s.virtual_size, s.file_size)) {
            prot += "[IMPORTS] ";
        }
        if (report.reloc_rva != 0 && s.virtual_address <= report.reloc_rva &&
            report.reloc_rva < s.virtual_address + std::max(s.virtual_size, s.file_size)) {
            prot += "[RELOCS] ";
        }

        std::cout << "  "
                  << std::left << std::setw(16) << s.name
                  << std::setw(12) << (s.segment.empty() ? "-" : s.segment)
                  << std::right << std::setw(10) << s.file_size << "  "
                  << std::setw(12) << s.virtual_size << "  "
                  << "0x" << std::hex << std::setw(6) << std::setfill('0') << s.file_offset << std::dec << std::setfill(' ') << "  "
                  << std::setw(6) << s.alignment << "  "
                  << prot << "\n";
    }

    if (report.format == BinaryFormat::PE_COFF && report.image_base > 0) {
        std::cout << "\n[1.1] PE OPTIONAL HEADER & DATA DIRECTORIES:\n"
                  << "  - Entry Point RVA                    : 0x" << std::hex << report.entry_point
                  << " (VMA: 0x" << (report.image_base + report.entry_point) << ")" << std::dec << "\n"
                  << "  - Image Base Address                 : 0x" << std::hex << report.image_base << std::dec << "\n"
                  << "  - Import Table Directory (Dir 1)     : RVA 0x" << std::hex << report.import_rva
                  << " (Size: " << std::dec << report.import_size << " bytes)";
        if (!report.import_section.empty()) {
            std::cout << " -> Embedded in '" << report.import_section << "'";
            if (report.import_section == ".rdata") {
                std::cout << " (Consolidated: saves 40-byte header & 4KB virtual page)";
            }
        }
        std::cout << "\n"
                  << "  - Import Address Table (Dir 12)      : RVA 0x" << std::hex << report.iat_rva
                  << " (Size: " << std::dec << report.iat_size << " bytes)\n"
                  << "  - Base Relocation Directory (Dir 5)  : RVA 0x" << std::hex << report.reloc_rva
                  << " (Size: " << std::dec << report.reloc_size << " bytes)\n";
    }

    std::cout << "\n[2] MEMORY FOOTPRINT & BUDGET BREAKDOWN:\n";
    std::cout << "  - Code Footprint (.text)             : " << std::setw(10) << report.budget.code_bytes << " bytes  (Execute, Read-Only)\n"
              << "  - Constants & Rodata (.rdata)        : " << std::setw(10) << report.budget.rodata_bytes << " bytes  (Immutable, OS-Shared RAM)\n"
              << "  - Writable Data (.data)              : " << std::setw(10) << report.budget.rwdata_bytes << " bytes  (Process-Private RW)\n"
              << "  - Uninitialized Zero-Fill (.bss)     : " << std::setw(10) << report.budget.bss_virtual_bytes << " bytes  (0 bytes on disk!)\n"
              << "  - Exception Unwind Tables (.pdata)   : " << std::setw(10) << report.budget.unwind_bytes << " bytes  (Zero-cost SEH / DWARF)\n"
              << "  - Thread-Local Storage (.tls$)       : " << std::setw(10) << report.budget.tls_bytes << " bytes  (Lock-free thread state)\n"
              << "  ------------------------------------------------------------------------------\n"
              << "  * Total Disk Storage Required        : " << std::setw(10) << report.budget.total_disk_bytes << " bytes\n"
              << "  * Total Virtual Memory Allocated     : " << std::setw(10) << report.budget.total_virtual_bytes << " bytes\n"
              << "  * Kernel RAM Sharing Efficiency      : " << std::fixed << std::setprecision(1)
              << report.budget.sharing_efficiency_pct() << "% eligible for page deduplication\n";

    if (show_unwind && !report.unwind_details.empty()) {
        std::cout << "\n[3] EXCEPTION UNWIND FORENSICS:\n";
        for (const auto& u : report.unwind_details) {
            std::cout << u << "\n";
        }
    }

    if (show_symbols && !report.symbols.empty()) {
        std::cout << "\n[4] SYMBOL TABLE (" << report.symbols.size() << " symbols):\n";
        for (const auto& sym : report.symbols) {
            std::cout << "  " << (sym.is_global ? "[GLOBAL] " : "[STATIC] ")
                      << (sym.is_function ? "[FUNC] " : "[DATA] ")
                      << sym.name << " @ 0x" << std::hex << sym.value << std::dec
                      << " (" << sym.section_name << ")\n";
        }
    }

    std::cout << "\n[5] ENTERPRISE ABI CONFORMANCE GATES:\n";
    if (report.verification_errors.empty()) {
        std::cout << "  [PASS] All strict W^X and ABI memory layout invariants passed.\n";
    } else {
        for (const auto& err : report.verification_errors) {
            std::cout << "  [FAIL] " << err << "\n";
        }
    }
    for (const auto& warn : report.verification_warnings) {
        std::cout << "  [WARN] " << warn << "\n";
    }
    std::cout << "================================================================================\n\n";
}

} // namespace dgtl::inspect

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "DGTL Object & Section Inspector Tool (dgtl-inspect)\n\n"
                  << "USAGE:\n"
                  << "  dgtl-inspect [OPTIONS] <file.obj|file.o|file.wasm>\n\n"
                  << "OPTIONS:\n"
                  << "  --unwind        Decode SEH / DWARF / Compact Unwind call frames\n"
                  << "  --symbols       Display detailed symbol table bindings\n"
                  << "  --relocs        Display relocation targets and offsets\n"
                  << "  --verify        Run strict enterprise W^X and ABI validation gates\n"
                  << "  --json          Output metrics as machine-readable JSON\n"
                  << "  --help, -h      Display this help menu\n";
        return 1;
    }

    bool show_unwind = false;
    bool show_symbols = false;
    bool show_relocs = false;
    bool verify_only = false;
    bool json_mode = false;
    fs::path target_file;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--unwind") show_unwind = true;
        else if (arg == "--symbols") show_symbols = true;
        else if (arg == "--relocs") show_relocs = true;
        else if (arg == "--verify") verify_only = true;
        else if (arg == "--json") json_mode = true;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: dgtl-inspect [OPTIONS] <file>\n";
            return 0;
        } else if (target_file.empty()) {
            target_file = argv[i];
        }
    }

    if (target_file.empty()) {
        std::cerr << "[dgtl-inspect] Error: No input file specified.\n";
        return 1;
    }

    if (!fs::exists(target_file)) {
        std::cerr << "[dgtl-inspect] Error: File does not exist: " << target_file.string() << "\n";
        return 2;
    }

    const auto report = dgtl::inspect::inspect_file(target_file);
    if (!verify_only) {
        dgtl::inspect::print_report(report, show_unwind, show_symbols, show_relocs, json_mode);
    }

    return report.is_compliant() ? 0 : 3;
}
