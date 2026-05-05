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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 15: DIRECT-TO-BINARY WEAVER (ELF64 / MACH-O / WASM)
 * FINAL FIX: Synchronized with Tri-Tier IR & Precise mmap alignment.
 * PERFORMANCE: Zero-copy executable generation via memory-mapped I/O.
 */

#pragma once
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include <string_view>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <array>
#include <algorithm>
#include <cstdio>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <io.h>
    #define DGTL_MAP_FAILED NULL
    struct Elf64_Ehdr { 
        unsigned char e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version; 
        uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags; 
        uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; 
    };
    struct Elf64_Phdr { 
        uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; 
    };
    struct Elf64_Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint64_t sh_flags;
        uint64_t sh_addr;
        uint64_t sh_offset;
        uint64_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint64_t sh_addralign;
        uint64_t sh_entsize;
    };
    struct Elf64_Sym {
        uint32_t st_name;
        unsigned char st_info;
        unsigned char st_other;
        uint16_t st_shndx;
        uint64_t st_value;
        uint64_t st_size;
    };
    struct Elf64_Rela {
        uint64_t r_offset;
        uint64_t r_info;
        int64_t  r_addend;
    };
    #ifndef ELFMAG
        #define ELFMAG "\177ELF"
        #define SELFMAG 4
        #define ELFCLASS64 2
        #define ELFDATA2LSB 1
        #define EV_CURRENT 1
        #define ET_REL 1
        #define ET_EXEC 2
        #define EM_X86_64 62
        #define PT_LOAD 1
        #define SHT_NULL 0
        #define SHT_PROGBITS 1
        #define SHT_SYMTAB 2
        #define SHT_STRTAB 3
        #define SHT_RELA 4
        #define SHF_ALLOC 0x2
        #define SHF_EXECINSTR 0x4
        #define SHF_INFO_LINK 0x40
        #define STB_LOCAL 0
        #define STB_GLOBAL 1
        #define STT_SECTION 3
        #define STT_FUNC 2
        #define SHN_UNDEF 0
        #define PF_X 1
        #define PF_R 4
        #define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0x0F))
        #define ELF64_R_INFO(sym, type) (((uint64_t)(sym) << 32) + ((uint64_t)(type)))
    #endif
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <elf.h>
    #define DGTL_MAP_FAILED MAP_FAILED
#endif

// --- DGTL COFF Object Format (Platform-Independent Cross-Compilation) ---
// Enables COFF .obj / .lib emission from any host for MSVC link.exe compatibility.
namespace dgtl::exec::coff {

static constexpr uint16_t MACHINE_AMD64       = 0x8664;
static constexpr uint32_t SCN_CNT_CODE        = 0x00000020;
static constexpr uint32_t SCN_MEM_EXECUTE     = 0x20000000;
static constexpr uint32_t SCN_MEM_READ        = 0x40000000;
static constexpr uint32_t SCN_ALIGN_16BYTES   = 0x00500000;
static constexpr uint8_t  SYM_CLASS_EXTERNAL  = 2;
static constexpr uint8_t  SYM_CLASS_STATIC    = 3;
static constexpr uint16_t SYM_DTYPE_FUNCTION  = 0x0020;

#pragma pack(push, 1)
struct FileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct SectionHeader {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct Symbol {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeroes;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    int16_t  SectionNumber;
    uint16_t Type;
    uint8_t  StorageClass;
    uint8_t  NumberOfAuxSymbols;
};
#pragma pack(pop)

} // namespace dgtl::exec::coff

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"   // Explicit for inline methods
#include "dgtl/isa/target/Wasm_Encoder.hpp"

namespace dgtl::exec {

/**
 * @class DGTLLinker
 * @brief Weaves IR directly into executable binary formats without .obj overhead.
 */
class DGTLLinker {
protected:
    intptr_t fd_handle = -1; 
    uint8_t* output_ptr = nullptr;
    size_t file_size = 0;

    void reset_mapping_state() noexcept {
        output_ptr = nullptr;
        fd_handle = -1;
        file_size = 0;
    }

    void release_mapping() noexcept {
        if (!output_ptr || output_ptr == DGTL_MAP_FAILED) {
            output_ptr = nullptr;
            if (fd_handle != -1) {
#if defined(_WIN32)
                CloseHandle(reinterpret_cast<HANDLE>(fd_handle));
#else
                close(static_cast<int>(fd_handle));
#endif
                fd_handle = -1;
            }
            return;
        }

#if defined(_WIN32)
        UnmapViewOfFile(output_ptr);
        if (fd_handle != -1) CloseHandle(reinterpret_cast<HANDLE>(fd_handle));
#else
        munmap(output_ptr, file_size);
        if (fd_handle != -1) close(static_cast<int>(fd_handle));
#endif

        output_ptr = nullptr;
        fd_handle = -1;
        file_size = 0;
    }

    /**
     * @brief Encodes values into Little-Endian Base 128 for WASM compatibility.
     */
    inline void write_leb128(std::vector<uint8_t>& buf, uint64_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            buf.push_back(byte);
        } while (value != 0);
    }

    static size_t align_up(size_t value, size_t alignment) {
        if (alignment <= 1) {
            return value;
        }
        const size_t rem = value % alignment;
        return rem == 0 ? value : (value + (alignment - rem));
    }

    static bool write_binary_file(std::string_view path, const std::vector<uint8_t>& bytes) {
#if defined(_WIN32)
        int out = _open(path.data(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
        if (out == -1) {
            return false;
        }
        if (!bytes.empty()) {
            const int wrote = _write(out, bytes.data(), static_cast<unsigned int>(bytes.size()));
            _close(out);
            return wrote == static_cast<int>(bytes.size());
        }
        _close(out);
        return true;
#else
        int out = open(path.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out == -1) {
            return false;
        }
        if (!bytes.empty()) {
            const ssize_t wrote = write(out, bytes.data(), bytes.size());
            close(out);
            return wrote == static_cast<ssize_t>(bytes.size());
        }
        close(out);
        return true;
#endif
    }

    static uint32_t to_big_endian_u32(uint32_t val) {
        return ((val & 0xFF000000u) >> 24) |
               ((val & 0x00FF0000u) >> 8)  |
               ((val & 0x0000FF00u) << 8)  |
               ((val & 0x000000FFu) << 24);
    }

    bool build_elf_relocatable_binary(const ir::IRStream& stream,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) {
            return false;
        }

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 16);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) {
            return false;
        }
        text_bytes.resize(text_size);
        if (text_bytes.empty()) {
            return false;
        }

        std::string shstrtab;
        shstrtab.push_back('\0');
        const uint32_t sh_name_text = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".text";
        shstrtab.push_back('\0');
        const uint32_t sh_name_shstrtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".shstrtab";
        shstrtab.push_back('\0');
        const uint32_t sh_name_strtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".strtab";
        shstrtab.push_back('\0');
        const uint32_t sh_name_symtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".symtab";
        shstrtab.push_back('\0');
        const uint32_t sh_name_relatext = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".rela.text";
        shstrtab.push_back('\0');

        std::string strtab;
        strtab.push_back('\0');
        const uint32_t sym_name_off = static_cast<uint32_t>(strtab.size());
        strtab.append(entry_symbol.data(), entry_symbol.size());
        strtab.push_back('\0');

        std::array<Elf64_Sym, 3> symtab{};
        symtab[0] = Elf64_Sym{};
        symtab[1].st_name = 0;
        symtab[1].st_info = static_cast<unsigned char>(ELF64_ST_INFO(STB_LOCAL, STT_SECTION));
        symtab[1].st_other = 0;
        symtab[1].st_shndx = static_cast<uint16_t>(1);
        symtab[1].st_value = 0;
        symtab[1].st_size = 0;

        symtab[2].st_name = sym_name_off;
        symtab[2].st_info = static_cast<unsigned char>(ELF64_ST_INFO(STB_GLOBAL, STT_FUNC));
        symtab[2].st_other = 0;
        symtab[2].st_shndx = static_cast<uint16_t>(1);
        symtab[2].st_value = 0;
        symtab[2].st_size = text_bytes.size();

        const size_t ehdr_size = sizeof(Elf64_Ehdr);
        const size_t text_off = align_up(ehdr_size, 16);
        const size_t shstrtab_off = align_up(text_off + text_bytes.size(), 1);
        const size_t strtab_off = align_up(shstrtab_off + shstrtab.size(), 1);
        const size_t symtab_off = align_up(strtab_off + strtab.size(), 8);
        const size_t shoff = align_up(symtab_off + sizeof(symtab), 8);

        const size_t section_count = 6;
        const size_t total_size = shoff + section_count * sizeof(Elf64_Shdr);
        out.assign(total_size, 0);

        auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(out.data());
        std::memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
        ehdr->e_ident[4] = ELFCLASS64;
        ehdr->e_ident[5] = ELFDATA2LSB;
        ehdr->e_ident[6] = EV_CURRENT;
        ehdr->e_type = ET_REL;
        ehdr->e_machine = EM_X86_64;
        ehdr->e_version = EV_CURRENT;
        ehdr->e_entry = 0;
        ehdr->e_phoff = 0;
        ehdr->e_shoff = shoff;
        ehdr->e_flags = 0;
        ehdr->e_ehsize = sizeof(Elf64_Ehdr);
        ehdr->e_phentsize = 0;
        ehdr->e_phnum = 0;
        ehdr->e_shentsize = sizeof(Elf64_Shdr);
        ehdr->e_shnum = static_cast<uint16_t>(section_count);
        ehdr->e_shstrndx = 2;

        std::memcpy(out.data() + text_off, text_bytes.data(), text_bytes.size());
        std::memcpy(out.data() + shstrtab_off, shstrtab.data(), shstrtab.size());
        std::memcpy(out.data() + strtab_off, strtab.data(), strtab.size());
        std::memcpy(out.data() + symtab_off, symtab.data(), sizeof(symtab));

        auto* shdr = reinterpret_cast<Elf64_Shdr*>(out.data() + shoff);
        shdr[0] = Elf64_Shdr{};

        shdr[1].sh_name = sh_name_text;
        shdr[1].sh_type = SHT_PROGBITS;
        shdr[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        shdr[1].sh_addr = 0;
        shdr[1].sh_offset = text_off;
        shdr[1].sh_size = text_bytes.size();
        shdr[1].sh_link = 0;
        shdr[1].sh_info = 0;
        shdr[1].sh_addralign = 16;
        shdr[1].sh_entsize = 0;

        shdr[2].sh_name = sh_name_shstrtab;
        shdr[2].sh_type = SHT_STRTAB;
        shdr[2].sh_flags = 0;
        shdr[2].sh_addr = 0;
        shdr[2].sh_offset = shstrtab_off;
        shdr[2].sh_size = shstrtab.size();
        shdr[2].sh_link = 0;
        shdr[2].sh_info = 0;
        shdr[2].sh_addralign = 1;
        shdr[2].sh_entsize = 0;

        shdr[3].sh_name = sh_name_strtab;
        shdr[3].sh_type = SHT_STRTAB;
        shdr[3].sh_flags = 0;
        shdr[3].sh_addr = 0;
        shdr[3].sh_offset = strtab_off;
        shdr[3].sh_size = strtab.size();
        shdr[3].sh_link = 0;
        shdr[3].sh_info = 0;
        shdr[3].sh_addralign = 1;
        shdr[3].sh_entsize = 0;

        shdr[4].sh_name = sh_name_symtab;
        shdr[4].sh_type = SHT_SYMTAB;
        shdr[4].sh_flags = 0;
        shdr[4].sh_addr = 0;
        shdr[4].sh_offset = symtab_off;
        shdr[4].sh_size = sizeof(symtab);
        shdr[4].sh_link = 3;
        shdr[4].sh_info = 2;
        shdr[4].sh_addralign = 8;
        shdr[4].sh_entsize = sizeof(Elf64_Sym);

        // .rela.text Ã¢â‚¬â€ relocation section (structurally present; no external fixups in current codegen)
        shdr[5].sh_name = sh_name_relatext;
        shdr[5].sh_type = SHT_RELA;
        shdr[5].sh_flags = SHF_INFO_LINK;
        shdr[5].sh_addr = 0;
        shdr[5].sh_offset = 0;
        shdr[5].sh_size = 0;
        shdr[5].sh_link = 4;  // .symtab section index
        shdr[5].sh_info = 1;  // .text section index
        shdr[5].sh_addralign = 8;
        shdr[5].sh_entsize = sizeof(Elf64_Rela);

        return true;
    }

    bool build_static_archive_binary(const ir::IRStream& stream,
                                     std::vector<uint8_t>& out,
                                     std::string_view member_name = "module.o") {
        out.clear();

        std::vector<uint8_t> object_bytes;
        if (!build_elf_relocatable_binary(stream, object_bytes, "DGTL_kernel_entry")) {
            return false;
        }

        std::string header_name(member_name);
        if (header_name.empty()) {
            header_name = "module.o";
        }
        if (header_name.size() > 15) {
            header_name.resize(15);
        }
        header_name.push_back('/');

        std::array<char, 60> hdr{};
        std::fill(hdr.begin(), hdr.end(), ' ');

        std::snprintf(hdr.data(), 17, "%-16s", header_name.c_str());
        std::snprintf(hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(hdr.data() + 40, 9, "%-8o", 0644);
        std::snprintf(hdr.data() + 48, 11, "%-10u", static_cast<unsigned int>(object_bytes.size()));
        hdr[58] = '`';
        hdr[59] = '\n';

        static constexpr char kArMagic[] = "!<arch>\n";
        out.insert(out.end(), kArMagic, kArMagic + 8);
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), object_bytes.begin(), object_bytes.end());
        if ((object_bytes.size() & 1U) != 0U) {
            out.push_back('\n');
        }

        return true;
    }

    // ---------------------------------------------------------------------
    // COFF Relocation structure (IMAGE_REL_AMD64_REL32 = 4)
    // ---------------------------------------------------------------------
    struct CoffCallReloc {
        uint32_t text_offset;   // byte offset inside .text where the 4-byte displacement sits
        std::string callee_name; // external symbol to resolve
    };

    /// Collect CALL sites from a lowered stream so we can emit COFF relocations.
    /// Each CALL node emits sub rsp,32 + E8 00 00 00 00 + add rsp,32 = 13 bytes total.
    /// The displacement starts at text_offset + 5 (4 bytes for sub rsp,32 + 1 for 0xE8).
    std::vector<CoffCallReloc> collect_call_relocs(
        const ir::IRStream& stream,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        std::vector<CoffCallReloc> relocs;
        if (stream.is_null()) return relocs;

        size_t text_offset = 0;
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            switch (node.opcode) {
                case ir::OpCode::LOAD_IMM:    text_offset += 7;  break;
                case ir::OpCode::REG_MOVE:    text_offset += 3;  break;
                case ir::OpCode::ADD_I32:     text_offset += 2;  break;
                case ir::OpCode::SUB_I32:     text_offset += 2;  break;
                case ir::OpCode::MUL_I32:     text_offset += 3;  break;
                case ir::OpCode::AND_I32:     text_offset += 2;  break;
                case ir::OpCode::OR_I32:      text_offset += 2;  break;
                case ir::OpCode::XOR_I32:     text_offset += 2;  break;
                case ir::OpCode::SHL_I32:     text_offset += 2;  break;
                case ir::OpCode::SHR_I32:     text_offset += 2;  break;
                case ir::OpCode::CALL: {
                    // sub rsp,32 (4) + E8 (1) + displacement (4) + add rsp,32 (4) = 13
                    const size_t disp_offset = text_offset + 5; // past sub rsp,32 + 0xE8
                    auto it = slot_to_callee.find(node.op_b);
                    if (it != slot_to_callee.end()) {
                        relocs.push_back({static_cast<uint32_t>(disp_offset), it->second});
                    }
                    text_offset += 13;
                    break;
                }
                case ir::OpCode::BRANCH:
                case ir::OpCode::GOTO:        text_offset += 5;  break;
                case ir::OpCode::BRANCH_COND: text_offset += 8;  break;
                case ir::OpCode::RET:         text_offset += 1;  break;
                case ir::OpCode::NOP:         text_offset += 1;  break;
                case ir::OpCode::LABEL:       text_offset += 0;  break; // pseudo-op: no bytes
                case ir::OpCode::PHI_NODE:    text_offset += 0;  break; // pseudo-op: no bytes
                case ir::OpCode::IMMEDIATE:               text_offset += 7;  break;
                case ir::OpCode::BRANCH_COMPARE_FUSED:    text_offset += 8;  break;
                case ir::OpCode::CMP_EQ_I32:
                case ir::OpCode::CMP_NE_I32:
                case ir::OpCode::CMP_LT_I32:
                case ir::OpCode::CMP_LE_I32:
                case ir::OpCode::CMP_GT_I32:
                case ir::OpCode::CMP_GE_I32:              text_offset += 5;  break;
                case ir::OpCode::DIV_I32:
                case ir::OpCode::MOD_I32:                 text_offset += 3;  break;
                default:                      text_offset += 1;  break; // NOP fallback
            }
        }
        return relocs;
    }

    /// Build a COFF relocatable object for one function, including IMAGE_REL_AMD64_REL32
    /// entries for all CALL instructions so the linker can resolve external/cross-obj calls.
    bool build_coff_relocatable_binary_with_relocs(
        const ir::IRStream& stream,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol,
        const std::vector<CoffCallReloc>& call_relocs)
    {
        out.clear();
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) return false;

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 20 + 64);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) return false;
        text_bytes.resize(text_size);
        if (text_bytes.empty()) return false;

        using namespace dgtl::exec::coff;

        // -- Build relocation table + callee symbol table entries --------------
        // COFF relocation entry (10 bytes each)
        struct CoffReloc { uint32_t VirtualAddress; uint32_t SymbolTableIndex; uint16_t Type; };
        static constexpr uint16_t kCoffRelAmd64Rel32 = 4;

        // Collect unique callee names ? assign symbol indices (after entry symbol at index 1)
        std::vector<std::string> extern_names;
        for (const auto& r : call_relocs) {
            bool found = false;
            for (const auto& e : extern_names) { if (e == r.callee_name) { found = true; break; } }
            if (!found) extern_names.push_back(r.callee_name);
        }

        // Build COFF relocation records
        std::vector<CoffReloc> coff_relocs;
        for (const auto& r : call_relocs) {
            uint32_t sym_idx = 2; // entry symbol at idx 1, externals start at idx 2
            for (const auto& e : extern_names) {
                if (e == r.callee_name) break;
                ++sym_idx;
            }
            coff_relocs.push_back({r.text_offset, sym_idx, kCoffRelAmd64Rel32});
        }

        // -- String table ------------------------------------------------------
        std::string sym_name(entry_symbol);
        std::string strtab_data; // entries for names > 8 chars
        struct SymStrInfo { bool use_long; uint32_t strtab_off; };
        auto make_sym_str = [&](const std::string& n) -> SymStrInfo {
            if (n.size() <= 8) return {false, 0};
            uint32_t off = 4 + static_cast<uint32_t>(strtab_data.size());
            strtab_data.append(n);
            strtab_data.push_back('\0');
            return {true, off};
        };
        const auto entry_str = make_sym_str(sym_name);
        std::vector<SymStrInfo> extern_str_infos;
        for (const auto& e : extern_names) extern_str_infos.push_back(make_sym_str(e));

        const size_t num_symbols = 1        // .text section
                                 + 1        // entry_symbol
                                 + extern_names.size();
        const uint32_t strtab_total_size = 4 + static_cast<uint32_t>(strtab_data.size());

        // -- Layout -----------------------------------------------------------
        const size_t file_hdr_size  = sizeof(FileHeader);
        const size_t sect_hdr_size  = sizeof(SectionHeader);
        const size_t text_off       = file_hdr_size + sect_hdr_size;
        const size_t reloc_off      = text_off + text_size;
        const size_t symtab_off     = reloc_off + coff_relocs.size() * 10;
        const size_t symtab_size    = num_symbols * sizeof(Symbol);
        const size_t total_size     = symtab_off + symtab_size + strtab_total_size;
        out.assign(total_size, 0);

        // -- File Header -------------------------------------------------------
        auto* fhdr = reinterpret_cast<FileHeader*>(out.data());
        fhdr->Machine              = MACHINE_AMD64;
        fhdr->NumberOfSections     = 1;
        fhdr->TimeDateStamp        = 0;
        fhdr->PointerToSymbolTable = static_cast<uint32_t>(symtab_off);
        fhdr->NumberOfSymbols      = static_cast<uint32_t>(num_symbols);
        fhdr->SizeOfOptionalHeader = 0;
        fhdr->Characteristics      = 0;

        // -- Section Header ----------------------------------------------------
        auto* shdr = reinterpret_cast<SectionHeader*>(out.data() + file_hdr_size);
        std::memset(shdr, 0, sizeof(SectionHeader));
        std::memcpy(shdr->Name, ".text\0\0\0", 8);
        shdr->SizeOfRawData          = static_cast<uint32_t>(text_size);
        shdr->PointerToRawData       = static_cast<uint32_t>(text_off);
        shdr->PointerToRelocations   = coff_relocs.empty() ? 0 : static_cast<uint32_t>(reloc_off);
        shdr->NumberOfRelocations    = static_cast<uint16_t>(coff_relocs.size());
        shdr->Characteristics        = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_ALIGN_16BYTES;

        // -- .text bytes -------------------------------------------------------
        std::memcpy(out.data() + text_off, text_bytes.data(), text_size);

        // -- Relocation records ------------------------------------------------
        for (size_t ri = 0; ri < coff_relocs.size(); ++ri) {
            uint8_t* rptr = out.data() + reloc_off + ri * 10;
            std::memcpy(rptr,     &coff_relocs[ri].VirtualAddress, 4);
            std::memcpy(rptr + 4, &coff_relocs[ri].SymbolTableIndex, 4);
            std::memcpy(rptr + 8, &coff_relocs[ri].Type, 2);
        }

        // -- Symbol Table ------------------------------------------------------
        auto fill_sym = [&](Symbol& s, const std::string& name, const SymStrInfo& si,
                            uint32_t value, int16_t sect, uint8_t cls) {
            std::memset(&s, 0, sizeof(Symbol));
            if (si.use_long) {
                s.Name.LongName.Zeroes = 0;
                s.Name.LongName.Offset = si.strtab_off;
            } else {
                const size_t n = name.size() > 8 ? 8 : name.size();
                std::memcpy(s.Name.ShortName, name.data(), n);
            }
            s.Value         = value;
            s.SectionNumber = sect;
            s.Type          = (cls == SYM_CLASS_EXTERNAL) ? SYM_DTYPE_FUNCTION : 0;
            s.StorageClass  = cls;
        };

        auto* sym_tbl = reinterpret_cast<Symbol*>(out.data() + symtab_off);
        // sym[0]: .text section
        fill_sym(sym_tbl[0], ".text", {false, 0}, 0, 1, SYM_CLASS_STATIC);
        // sym[1]: entry function symbol
        fill_sym(sym_tbl[1], sym_name, entry_str, 0, 1, SYM_CLASS_EXTERNAL);
        // sym[2..]: external callee symbols (section=0 = undefined)
        for (size_t ei = 0; ei < extern_names.size(); ++ei) {
            fill_sym(sym_tbl[2 + ei], extern_names[ei], extern_str_infos[ei], 0, 0, SYM_CLASS_EXTERNAL);
        }

        // -- String Table ------------------------------------------------------
        uint8_t* strtab_ptr = out.data() + symtab_off + symtab_size;
        std::memcpy(strtab_ptr, &strtab_total_size, 4);
        if (!strtab_data.empty()) {
            std::memcpy(strtab_ptr + 4, strtab_data.data(), strtab_data.size());
        }

        return true;
    }

    bool build_coff_relocatable_binary(const ir::IRStream& stream,
                                       std::vector<uint8_t>& out,
                                       std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) return false;

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 16);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) return false;
        text_bytes.resize(text_size);
        if (text_bytes.empty()) return false;

        using namespace dgtl::exec::coff;

        const size_t file_hdr_size = sizeof(FileHeader);
        const size_t sect_hdr_size = sizeof(SectionHeader);
        const size_t num_sections = 1;
        const size_t text_offset = file_hdr_size + sect_hdr_size * num_sections;
        const size_t symtab_offset = text_offset + text_size;

        // String table: 4-byte size + optional long name entries
        std::string sym_name(entry_symbol);
        bool use_long_name = sym_name.size() > 8;
        uint32_t sym_strtab_offset = 0;
        std::string strtab_data;
        if (use_long_name) {
            sym_strtab_offset = 4; // offset from start of string table
            strtab_data.append(sym_name);
            strtab_data.push_back('\0');
        }

        const size_t num_symbols = 2;
        const size_t symtab_size = num_symbols * sizeof(Symbol);
        const uint32_t strtab_total_size = 4 + static_cast<uint32_t>(strtab_data.size());
        const size_t total_size = symtab_offset + symtab_size + strtab_total_size;
        out.assign(total_size, 0);

        // COFF File Header
        auto* fhdr = reinterpret_cast<FileHeader*>(out.data());
        fhdr->Machine = MACHINE_AMD64;
        fhdr->NumberOfSections = static_cast<uint16_t>(num_sections);
        fhdr->TimeDateStamp = 0;
        fhdr->PointerToSymbolTable = static_cast<uint32_t>(symtab_offset);
        fhdr->NumberOfSymbols = static_cast<uint32_t>(num_symbols);
        fhdr->SizeOfOptionalHeader = 0;
        fhdr->Characteristics = 0;

        // .text Section Header
        auto* shdr = reinterpret_cast<SectionHeader*>(out.data() + file_hdr_size);
        std::memset(shdr, 0, sizeof(SectionHeader));
        std::memcpy(shdr->Name, ".text\0\0\0", 8);
        shdr->VirtualSize = 0;
        shdr->VirtualAddress = 0;
        shdr->SizeOfRawData = static_cast<uint32_t>(text_size);
        shdr->PointerToRawData = static_cast<uint32_t>(text_offset);
        shdr->PointerToRelocations = 0;
        shdr->PointerToLinenumbers = 0;
        shdr->NumberOfRelocations = 0;
        shdr->NumberOfLinenumbers = 0;
        shdr->Characteristics = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_ALIGN_16BYTES;

        // .text data
        std::memcpy(out.data() + text_offset, text_bytes.data(), text_size);

        // COFF Symbol Table
        auto* sym = reinterpret_cast<Symbol*>(out.data() + symtab_offset);

        // Symbol 0: .text section symbol
        std::memset(&sym[0], 0, sizeof(Symbol));
        std::memcpy(sym[0].Name.ShortName, ".text\0\0\0", 8);
        sym[0].Value = 0;
        sym[0].SectionNumber = 1;
        sym[0].Type = 0;
        sym[0].StorageClass = SYM_CLASS_STATIC;
        sym[0].NumberOfAuxSymbols = 0;

        // Symbol 1: entry point (EXTERNAL)
        std::memset(&sym[1], 0, sizeof(Symbol));
        if (use_long_name) {
            sym[1].Name.LongName.Zeroes = 0;
            sym[1].Name.LongName.Offset = sym_strtab_offset;
        } else {
            const size_t copy_len = sym_name.size() > 8 ? 8 : sym_name.size();
            std::memcpy(sym[1].Name.ShortName, sym_name.data(), copy_len);
        }
        sym[1].Value = 0;
        sym[1].SectionNumber = 1;
        sym[1].Type = SYM_DTYPE_FUNCTION;
        sym[1].StorageClass = SYM_CLASS_EXTERNAL;
        sym[1].NumberOfAuxSymbols = 0;

        // String Table (4-byte size prefix + data)
        uint8_t* strtab_ptr = out.data() + symtab_offset + symtab_size;
        std::memcpy(strtab_ptr, &strtab_total_size, 4);
        if (!strtab_data.empty()) {
            std::memcpy(strtab_ptr + 4, strtab_data.data(), strtab_data.size());
        }

        return true;
    }

    bool build_coff_archive_binary(const ir::IRStream& stream,
                                   std::vector<uint8_t>& out,
                                   std::string_view member_name = "module.obj",
                                   std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();

        std::vector<uint8_t> object_bytes;
        if (!build_coff_relocatable_binary(stream, object_bytes, entry_symbol)) return false;

        // First Linker Member: big-endian symbol index for MSVC link.exe
        std::string sym_name(entry_symbol);
        sym_name.push_back('\0');

        const uint32_t num_symbols_be = to_big_endian_u32(1);
        const size_t first_member_content_size = 4 + 4 + sym_name.size();

        // ar header for first linker member (name = "/")
        std::array<char, 60> lm_hdr{};
        std::fill(lm_hdr.begin(), lm_hdr.end(), ' ');
        std::snprintf(lm_hdr.data(), 17, "%-16s", "/");
        std::snprintf(lm_hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(lm_hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(lm_hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(lm_hdr.data() + 40, 9, "%-8o", 0);
        std::snprintf(lm_hdr.data() + 48, 11, "%-10u",
                      static_cast<unsigned int>(first_member_content_size));
        lm_hdr[58] = '`';
        lm_hdr[59] = '\n';

        const size_t lm_pad = (first_member_content_size & 1U) ? 1 : 0;
        const size_t obj_member_offset = 8 + 60 + first_member_content_size + lm_pad;
        const uint32_t obj_offset_be = to_big_endian_u32(static_cast<uint32_t>(obj_member_offset));

        // Object member ar header
        std::string obj_name(member_name);
        if (obj_name.size() > 15) obj_name.resize(15);
        obj_name.push_back('/');

        std::array<char, 60> obj_hdr{};
        std::fill(obj_hdr.begin(), obj_hdr.end(), ' ');
        std::snprintf(obj_hdr.data(), 17, "%-16s", obj_name.c_str());
        std::snprintf(obj_hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(obj_hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(obj_hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(obj_hdr.data() + 40, 9, "%-8o", 0644);
        std::snprintf(obj_hdr.data() + 48, 11, "%-10u",
                      static_cast<unsigned int>(object_bytes.size()));
        obj_hdr[58] = '`';
        obj_hdr[59] = '\n';

        const size_t obj_pad = (object_bytes.size() & 1U) ? 1 : 0;

        // Assemble COFF archive
        static constexpr char kArMagic[] = "!<arch>\n";
        out.reserve(8 + 60 + first_member_content_size + lm_pad
                    + 60 + object_bytes.size() + obj_pad);

        out.insert(out.end(), kArMagic, kArMagic + 8);
        out.insert(out.end(), lm_hdr.begin(), lm_hdr.end());
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&num_symbols_be),
                   reinterpret_cast<const uint8_t*>(&num_symbols_be) + 4);
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&obj_offset_be),
                   reinterpret_cast<const uint8_t*>(&obj_offset_be) + 4);
        out.insert(out.end(), sym_name.begin(), sym_name.end());
        if (lm_pad) out.push_back('\n');
        out.insert(out.end(), obj_hdr.begin(), obj_hdr.end());
        out.insert(out.end(), object_bytes.begin(), object_bytes.end());
        if (obj_pad) out.push_back('\n');

        return true;
    }

public:
    virtual ~DGTLLinker() {
    release_mapping();
    }

    /**
     * @brief Generates a standalone ELF64 executable strike.
     */
    void emit_elf64(std::string_view path, const ir::IRStream& ir_stream) {
        if (ir_stream.is_null()) [[unlikely]] return;
        release_mapping();
        reset_mapping_state();
        
        // Calculate file size: Header + Code Segment (aligned to 4KB page)
        file_size = (ir_stream.size() * 16) + 0x2000; 
        file_size = (file_size + 0xFFF) & ~0xFFF; 

#if defined(_WIN32)
        HANDLE hFile = CreateFileA(path.data(), GENERIC_READ | GENERIC_WRITE, 0, NULL, 
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;

        HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, (DWORD)file_size, NULL);
        if (!hMap) {
            CloseHandle(hFile);
            return;
        }
        output_ptr = (uint8_t*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, file_size);
        fd_handle = reinterpret_cast<intptr_t>(hFile);
        CloseHandle(hMap);
#else
        int fd = open(path.data(), O_RDWR | O_CREAT | O_TRUNC, 0755);
        if (fd == -1) return;
        if (ftruncate(fd, file_size) != 0) { close(fd); return; }

        output_ptr = static_cast<uint8_t*>(mmap(nullptr, file_size, 
                                           PROT_READ | PROT_WRITE, 
                                           MAP_SHARED, fd, 0));
        fd_handle = static_cast<intptr_t>(fd);
#endif

        if (output_ptr == DGTL_MAP_FAILED || !output_ptr) {
            output_ptr = nullptr;
            if (fd_handle != -1) {
#if defined(_WIN32)
                CloseHandle(reinterpret_cast<HANDLE>(fd_handle));
#else
                close(static_cast<int>(fd_handle));
#endif
                fd_handle = -1;
            }
            return;
        }

        // --- ELF Header Strike ---
        auto* elf_hdr = reinterpret_cast<Elf64_Ehdr*>(output_ptr);
        std::memset(elf_hdr, 0, sizeof(Elf64_Ehdr));
        std::memcpy(elf_hdr->e_ident, ELFMAG, SELFMAG);
        elf_hdr->e_ident[4] = ELFCLASS64;
        elf_hdr->e_ident[5] = ELFDATA2LSB;
        elf_hdr->e_ident[6] = EV_CURRENT;
        elf_hdr->e_type = ET_EXEC;
        elf_hdr->e_machine = EM_X86_64;
        elf_hdr->e_version = EV_CURRENT;
        elf_hdr->e_entry = 0x401000;
        elf_hdr->e_phoff = sizeof(Elf64_Ehdr);
        elf_hdr->e_ehsize = sizeof(Elf64_Ehdr);
        elf_hdr->e_phentsize = sizeof(Elf64_Phdr);
        elf_hdr->e_phnum = 1;

        // --- Program Header Strike ---
        auto* prog_hdr = reinterpret_cast<Elf64_Phdr*>(output_ptr + elf_hdr->e_phoff);
        prog_hdr->p_type = PT_LOAD;
        prog_hdr->p_offset = 0x1000; 
        prog_hdr->p_vaddr = 0x401000;
        prog_hdr->p_filesz = static_cast<uint64_t>(ir_stream.size()) * 16;
        prog_hdr->p_memsz = prog_hdr->p_filesz;
        prog_hdr->p_flags = PF_R | PF_X;
        prog_hdr->p_align = 0x1000;

        size_t emitted_size = 0;
        const bool ok = emit_machine_code(output_ptr + prog_hdr->p_offset,
                                          static_cast<size_t>(prog_hdr->p_filesz),
                                          ir_stream,
                                          &emitted_size);
        if (!ok) {
            release_mapping();
            return;
        }
        prog_hdr->p_filesz = emitted_size;
        prog_hdr->p_memsz = emitted_size;

#if !defined(_WIN32)
        msync(output_ptr, file_size, MS_SYNC);
#endif
    }

protected:
    bool emit_machine_code(uint8_t* dest,
                           size_t dest_capacity,
                           const ir::IRStream& stream,
                           size_t* emitted_size = nullptr) {
        if (stream.is_null() || !dest || !stream.validate_shadow_integrity()) return false;
        const size_t count = stream.size();

        // --- Pass 0: Build label_map (frontend label IDs → stream node indices) ---
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(count / 4 + 4);
        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LABEL) {
                uint32_t lid = (node.result_id != ir::INVALID_SLOT) ? node.result_id : node.op_a;
                label_map[lid] = i;
            }
        }
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < count) return static_cast<size_t>(raw);
            return count;
        };

        // --- Pass 1: Pre-compute per-node byte offsets ---
        std::vector<size_t> node_offsets(count + 1, 0);
        size_t probe = 0;
        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = probe;
            const auto& node = stream.get_node(i);
            switch (node.opcode) {
                case ir::OpCode::LOAD_IMM:    probe += 7;  break;
                case ir::OpCode::REG_MOVE:    probe += 3;  break;
                case ir::OpCode::ADD_I32:     probe += 2;  break;
                case ir::OpCode::SUB_I32:     probe += 2;  break;
                case ir::OpCode::MUL_I32:     probe += 3;  break;
                case ir::OpCode::AND_I32:     probe += 2;  break;
                case ir::OpCode::OR_I32:      probe += 2;  break;
                case ir::OpCode::XOR_I32:     probe += 2;  break;
                case ir::OpCode::SHL_I32:     probe += 2;  break;
                case ir::OpCode::SHR_I32:     probe += 2;  break;
                case ir::OpCode::CALL:        probe += 13; break;
                case ir::OpCode::BRANCH:
                case ir::OpCode::GOTO:        probe += 5;  break;
                case ir::OpCode::BRANCH_COND: probe += 8;  break;
                case ir::OpCode::RET:         probe += 1;  break;
                case ir::OpCode::NOP:         probe += 1;  break;
                case ir::OpCode::LABEL:       probe += 0;  break;
                case ir::OpCode::PHI_NODE:    probe += 0;  break;
                case ir::OpCode::IMMEDIATE:              probe += 7;  break;
                case ir::OpCode::BRANCH_COMPARE_FUSED:   probe += 8;  break;
                case ir::OpCode::CMP_EQ_I32:
                case ir::OpCode::CMP_NE_I32:
                case ir::OpCode::CMP_LT_I32:
                case ir::OpCode::CMP_LE_I32:
                case ir::OpCode::CMP_GT_I32:
                case ir::OpCode::CMP_GE_I32:             probe += 5;  break;
                case ir::OpCode::DIV_I32:
                case ir::OpCode::MOD_I32:                probe += 3;  break;
                default:                      probe += 1;  break;
            }
        }
        node_offsets[count] = probe;

        if (probe > dest_capacity) return false;

        // --- Pass 2: Emit code with placeholder branch displacements ---
        struct BranchPatch { size_t disp_offset; size_t source_end; size_t target_node; };
        std::vector<BranchPatch> patches;
        patches.reserve(count / 8 + 4);

        size_t offset = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            switch (node.opcode) {
                case ir::OpCode::LOAD_IMM: {
                    dest[offset++] = 0x48;
                    dest[offset++] = 0xC7;
                    dest[offset++] = 0xC0;
                    const uint32_t imm = node.op_b;
                    std::memcpy(dest + offset, &imm, sizeof(imm));
                    offset += sizeof(imm);
                    break;
                }
                case ir::OpCode::REG_MOVE:
                    dest[offset++] = 0x48; dest[offset++] = 0x89; dest[offset++] = 0xD8; break;
                case ir::OpCode::ADD_I32:
                    dest[offset++] = 0x01; dest[offset++] = 0xD8; break;
                case ir::OpCode::SUB_I32:
                    dest[offset++] = 0x29; dest[offset++] = 0xD8; break;
                case ir::OpCode::MUL_I32:
                    dest[offset++] = 0x0F; dest[offset++] = 0xAF; dest[offset++] = 0xC3; break;
                case ir::OpCode::AND_I32:
                    dest[offset++] = 0x21; dest[offset++] = 0xD8; break;
                case ir::OpCode::OR_I32:
                    dest[offset++] = 0x09; dest[offset++] = 0xD8; break;
                case ir::OpCode::XOR_I32:
                    dest[offset++] = 0x31; dest[offset++] = 0xD8; break;
                case ir::OpCode::SHL_I32:
                    dest[offset++] = 0xD3; dest[offset++] = 0xE0; break;
                case ir::OpCode::SHR_I32:
                    dest[offset++] = 0xD3; dest[offset++] = 0xF8; break;
                case ir::OpCode::CALL: {
                    // sub rsp,32 (shadow space) + call rel32 (0, COFF reloc) + add rsp,32
                    dest[offset++] = 0x48; dest[offset++] = 0x83;
                    dest[offset++] = 0xEC; dest[offset++] = 0x20;
                    dest[offset++] = 0xE8;
                    dest[offset++] = 0x00; dest[offset++] = 0x00;
                    dest[offset++] = 0x00; dest[offset++] = 0x00;
                    dest[offset++] = 0x48; dest[offset++] = 0x83;
                    dest[offset++] = 0xC4; dest[offset++] = 0x20;
                    break;
                }
                case ir::OpCode::BRANCH:
                case ir::OpCode::GOTO: {
                    dest[offset++] = 0xE9; // JMP rel32
                    patches.push_back({offset, offset + 4, resolve_target(node.op_a)});
                    dest[offset++] = 0; dest[offset++] = 0;
                    dest[offset++] = 0; dest[offset++] = 0;
                    break;
                }
                case ir::OpCode::BRANCH_COND: {
                    dest[offset++] = 0x85; dest[offset++] = 0xC0; // TEST EAX, EAX
                    dest[offset++] = 0x0F; dest[offset++] = 0x85; // JNZ rel32
                    patches.push_back({offset, offset + 4, resolve_target(node.op_b)});
                    dest[offset++] = 0; dest[offset++] = 0;
                    dest[offset++] = 0; dest[offset++] = 0;
                    break;
                }
                case ir::OpCode::BRANCH_COMPARE_FUSED: {
                    dest[offset++] = 0x39; dest[offset++] = 0xD8; // CMP EAX, EBX
                    dest[offset++] = 0x0F; dest[offset++] = 0x85; // JNE rel32
                    patches.push_back({offset, offset + 4, resolve_target(node.op_c)});
                    dest[offset++] = 0; dest[offset++] = 0;
                    dest[offset++] = 0; dest[offset++] = 0;
                    break;
                }
                case ir::OpCode::IMMEDIATE: {
                    // IMMEDIATE is alias for LOAD_IMM
                    dest[offset++] = 0x48; dest[offset++] = 0xC7; dest[offset++] = 0xC0;
                    const uint32_t imm_i = node.op_b;
                    std::memcpy(dest + offset, &imm_i, 4); offset += 4;
                    break;
                }
                case ir::OpCode::CMP_EQ_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8; // CMP EAX,EBX
                    dest[offset++]=0x0F; dest[offset++]=0x94; dest[offset++]=0xC0; // SETE AL
                    break;
                case ir::OpCode::CMP_NE_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8;
                    dest[offset++]=0x0F; dest[offset++]=0x95; dest[offset++]=0xC0; // SETNE AL
                    break;
                case ir::OpCode::CMP_LT_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8;
                    dest[offset++]=0x0F; dest[offset++]=0x9C; dest[offset++]=0xC0; // SETL AL
                    break;
                case ir::OpCode::CMP_LE_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8;
                    dest[offset++]=0x0F; dest[offset++]=0x9E; dest[offset++]=0xC0; // SETLE AL
                    break;
                case ir::OpCode::CMP_GT_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8;
                    dest[offset++]=0x0F; dest[offset++]=0x9F; dest[offset++]=0xC0; // SETG AL
                    break;
                case ir::OpCode::CMP_GE_I32:
                    dest[offset++]=0x39; dest[offset++]=0xD8;
                    dest[offset++]=0x0F; dest[offset++]=0x9D; dest[offset++]=0xC0; // SETGE AL
                    break;
                case ir::OpCode::DIV_I32:
                    dest[offset++]=0x99;                       // CDQ
                    dest[offset++]=0xF7; dest[offset++]=0xFB; // IDIV EBX
                    break;
                case ir::OpCode::MOD_I32:
                    dest[offset++]=0x99;                       // CDQ
                    dest[offset++]=0xF7; dest[offset++]=0xFB; // IDIV EBX (remainder in EDX)
                    break;
                case ir::OpCode::RET:
                    dest[offset++] = 0xC3; break;
                case ir::OpCode::NOP:
                    dest[offset++] = 0x90; break;
                case ir::OpCode::LABEL:
                case ir::OpCode::PHI_NODE:
                    break; // pseudo-ops: no bytes emitted
                default:
                    // NOP fallback — keeps COFF structure valid
                    dest[offset++] = 0x90; break;
            }
        }

        // --- Pass 3: Patch branch displacements ---
        for (const auto& p : patches) {
            const size_t tgt_byte = (p.target_node <= count)
                                        ? node_offsets[p.target_node]
                                        : offset;
            const int64_t rel64 = static_cast<int64_t>(tgt_byte) -
                                  static_cast<int64_t>(p.source_end);
            const int32_t rel32 = static_cast<int32_t>(rel64);
            std::memcpy(dest + p.disp_offset, &rel32, sizeof(rel32));
        }

        if (emitted_size) *emitted_size = offset;
        return true;
    }


public:
    bool build_elf_relocatable(std::vector<uint8_t>& out,
                               const ir::IRStream& stream,
                               std::string_view entry_symbol = "DGTL_kernel_entry") {
        return build_elf_relocatable_binary(stream, out, entry_symbol);
    }

    bool emit_elf_relocatable(std::string_view path,
                              const ir::IRStream& stream,
                              std::string_view entry_symbol = "DGTL_kernel_entry") {
        std::vector<uint8_t> payload;
        if (!build_elf_relocatable_binary(stream, payload, entry_symbol)) {
            return false;
        }
        return write_binary_file(path, payload);
    }

    bool build_static_archive(std::vector<uint8_t>& out,
                              const ir::IRStream& stream,
                              std::string_view member_name = "module.o") {
        return build_static_archive_binary(stream, out, member_name);
    }

    bool emit_static_archive(std::string_view path,
                             const ir::IRStream& stream,
                             std::string_view member_name = "module.o") {
        std::vector<uint8_t> payload;
        if (!build_static_archive_binary(stream, payload, member_name)) {
            return false;
        }
        return write_binary_file(path, payload);
    }

    bool build_coff_relocatable(std::vector<uint8_t>& out,
                                const ir::IRStream& stream,
                                std::string_view entry_symbol = "DGTL_kernel_entry") {
        return build_coff_relocatable_binary(stream, out, entry_symbol);
    }

    /// Build a COFF object for one function, emitting IMAGE_REL_AMD64_REL32
    /// relocations for all CALL instructions so the linker resolves them.
    bool build_coff_with_relocs(std::vector<uint8_t>& out,
                                const ir::IRStream& stream,
                                std::string_view entry_symbol,
                                const std::unordered_map<uint32_t, std::string>& slot_to_callee) {
        auto relocs = collect_call_relocs(stream, slot_to_callee);
        return build_coff_relocatable_binary_with_relocs(stream, out, entry_symbol, relocs);
    }

    bool emit_coff_relocatable(std::string_view path,
                               const ir::IRStream& stream,
                               std::string_view entry_symbol = "DGTL_kernel_entry") {
        std::vector<uint8_t> payload;
        if (!build_coff_relocatable_binary(stream, payload, entry_symbol)) return false;
        return write_binary_file(path, payload);
    }

    bool build_coff_archive(std::vector<uint8_t>& out,
                            const ir::IRStream& stream,
                            std::string_view member_name = "module.obj",
                            std::string_view entry_symbol = "DGTL_kernel_entry") {
        return build_coff_archive_binary(stream, out, member_name, entry_symbol);
    }

    bool emit_coff_archive(std::string_view path,
                           const ir::IRStream& stream,
                           std::string_view member_name = "module.obj",
                           std::string_view entry_symbol = "DGTL_kernel_entry") {
        std::vector<uint8_t> payload;
        if (!build_coff_archive_binary(stream, payload, member_name, entry_symbol)) return false;
        return write_binary_file(path, payload);
    }
};

} // namespace dgtl::exec

namespace dgtl::exec {

/**
 * @class DGTLUniversalLinker
 * @brief Extends Linker with WebAssembly (WASM) and multi-arch support.
 */
class DGTLUniversalLinker : public DGTLLinker {
public:
    DGTLUniversalLinker() = default;

    // --- Specialized WebAssembly Support ---
    
    /**
     * @brief Generates a WebAssembly module binary.
     */
    void emit_wasm(std::string_view path, const ir::IRStream& stream) {
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) [[unlikely]] {
            return;
        }

        DGTLWasmEncoder encoder;
        encoder.encode(stream);

        const auto& wasm_buf = encoder.get_binary();
        if (wasm_buf.empty()) {
            return;
        }

        // Uses the base class protected method for disk I/O
        (void)this->write_binary_file(path, wasm_buf);
    }

    // --- Extended Multi-Arch API for Orchestrator ---

    /**
     * @brief ELF64 executable emission.
     */
    void emit_elf64(std::string_view path, const ir::IRStream& stream) {
        DGTLLinker::emit_elf64(path, stream);
    }

    /**
     * @brief Generates a relocatable ELF object (.o).
     */
    bool emit_elf_relocatable(std::string_view path, const ir::IRStream& stream, const char* entry) {
        return DGTLLinker::emit_elf_relocatable(path, stream, entry);
    }

    /**
     * @brief Generates a relocatable COFF object (.obj).
     */
    bool emit_coff_relocatable(std::string_view path, const ir::IRStream& stream, const char* entry) {
        return DGTLLinker::emit_coff_relocatable(path, stream, entry);
    }

    /**
     * @brief Packages IR into a static archive (.a).
     */
    bool emit_static_archive(std::string_view path, const ir::IRStream& stream, const char* obj_name) {
        return DGTLLinker::emit_static_archive(path, stream, obj_name);
    }

    /**
     * @brief Packages IR into a COFF static library (.lib).
     */
    bool emit_coff_archive(std::string_view path, const ir::IRStream& stream, const char* obj_name, const char* entry) {
        return DGTLLinker::emit_coff_archive(path, stream, obj_name, entry);
    }
};

} // namespace dgtl::exec


