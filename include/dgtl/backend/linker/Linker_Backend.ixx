/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 15: DIRECT-TO-BINARY WEAVER (ELF64 / MACH-O / WASM / COFF PE-OBJ / STATIC LIB)
 * FULL SYNCHRONIZATION: Aligned with 178 IR OpCodes, ISel DSL Bootstrap Rules & Precise mmap alignment.
 * PERFORMANCE: Zero-copy executable generation via memory-mapped I/O.
 * ARCHITECTURE: Staff-Level Upgraded with AVX-512 / FMA32 / Vector512 / AI / Spatial Math Native Emission.
 */

module;

#include <cstdlib>

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
#include <fstream> 
#include <limits>
#include <unordered_map>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <stdlib.h> // For _byteswap_ulong
    #undef CONST    // Prevents Windows.h CONST macro from corrupting ir::OpCode::CONST
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <elf.h>
#endif

export module dgtl.ir_core.linker_backend;

import dgtl.ir_core;
import dgtl.ir_core.isel_gen;
import dgtl.ir_core.binary_loader;
import dgtl.ir_core.wasm_encoder;
import dgtl.ir_core.runtime_contract;


#if defined(_WIN32) || defined(_WIN64)
    #define DGTL_MAP_FAILED nullptr
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
    #define DGTL_MAP_FAILED MAP_FAILED
#endif

// --- DGTL COFF Object Format (Platform-Independent Cross-Compilation) ---
export namespace dgtl::backend::linker::coff {

inline constexpr uint16_t MACHINE_AMD64       = 0x8664;
inline constexpr uint32_t SCN_CNT_CODE        = 0x00000020;
inline constexpr uint32_t SCN_MEM_EXECUTE     = 0x20000000;
inline constexpr uint32_t SCN_MEM_READ        = 0x40000000;
inline constexpr uint32_t SCN_ALIGN_16BYTES   = 0x00500000;
inline constexpr uint8_t  SYM_CLASS_EXTERNAL  = 2;
inline constexpr uint8_t  SYM_CLASS_STATIC    = 3;
inline constexpr uint16_t SYM_DTYPE_FUNCTION  = 0x0020;

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

} // namespace dgtl::backend::linker::coff

export namespace dgtl::backend::linker {

/**
 * @class DGTLLinker
 * @brief Weaves IR directly into executable binary formats without .obj overhead.
 */
class DGTLLinker {
protected:
    intptr_t fd_handle = -1; 
    uint8_t* output_ptr = nullptr;
    size_t file_size = 0;
    std::vector<std::string> fallback_diagnostics_;

    inline void clear_fallback_diagnostics() noexcept {
        fallback_diagnostics_.clear();
    }

    inline void record_fallback_opcode(size_t node_index, ir::OpCode opcode) {
        std::string msg;
        msg.reserve(64);
        msg.append("node#").append(std::to_string(node_index))
           .append(" opcode=").append(ir::opcode_name(opcode))
           .append(" (#").append(std::to_string(static_cast<uint16_t>(opcode)))
           .append(")");
        fallback_diagnostics_.push_back(std::move(msg));
    }

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

    static inline void write_leb128(std::vector<uint8_t>& buf, uint64_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            buf.push_back(byte);
        } while (value != 0);
    }

    [[nodiscard]] static constexpr size_t align_up(size_t value, size_t alignment) noexcept {
        if (alignment <= 1) [[unlikely]] {
            return value;
        }
        const size_t rem = value % alignment;
        return rem == 0 ? value : (value + (alignment - rem));
    }

    [[nodiscard]] static bool write_binary_file(std::string_view path, const std::vector<uint8_t>& bytes) {
        if (path.empty()) [[unlikely]] return false;
        std::ofstream out(path.data(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) [[unlikely]] return false;
        
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        
        out.close();
        return out.good();
    }

    [[nodiscard]] static constexpr uint32_t to_big_endian_u32(uint32_t val) noexcept {
#if defined(_MSC_VER)
        return _byteswap_ulong(val);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap32(val);
#else
        return ((val & 0xFF000000u) >> 24) |
               ((val & 0x00FF0000u) >> 8)  |
               ((val & 0x0000FF00u) << 8)  |
               ((val & 0x000000FFu) << 24);
#endif
    }

    [[nodiscard]] static inline size_t x64_prologue_size(const ir::IRStream& stream) noexcept {
        size_t sz = 11;
        if (!stream.is_null() && stream.size() > 0 && stream.get_node(0).opcode == ir::OpCode::LABEL) {
            const auto& fn_hdr = stream.get_node(0);
            if (fn_hdr.op_a != ir::INVALID_SLOT && fn_hdr.op_a != 0u) sz += 7;
            else sz += 7;
            if (fn_hdr.op_b != ir::INVALID_SLOT && fn_hdr.op_b != 0u) sz += 7;
            else sz += 7;
            if (fn_hdr.op_c != ir::INVALID_SLOT && fn_hdr.op_c != 0u) sz += 7;
            if (fn_hdr.logic_flags != 0u && fn_hdr.logic_flags != ir::INVALID_SLOT) sz += 7;
        } else {
            sz += 14;
        }
        return sz;
    }

    [[nodiscard]] static constexpr size_t x64_prologue_size() noexcept {
        return 25;
    }

    [[nodiscard]] static constexpr size_t x64_node_size(const ir::IRNode& node) noexcept {
        using namespace dgtl::isa::lowering;
        const MachineForm form = generated::classify_machine_form(node.opcode);

        if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
            const size_t arg1_size = (node.op_a != ir::INVALID_SLOT) ? 7 : 2;
            const size_t arg2_size = (node.op_b != ir::INVALID_SLOT) ? 7 : 2;
            const size_t arg3_size = (node.op_c != ir::INVALID_SLOT) ? 7 : 3;
            const size_t result_size = (node.result_id != ir::INVALID_SLOT) ? 7 : 1;
            return arg1_size + arg2_size + arg3_size + 10 + 4 + 14 + 4 + result_size + 10;
        }

        switch (form) {
            case MachineForm::NoOp:
                if (node.opcode == ir::OpCode::LABEL) {
                    size_t lbl_bytes = 0;
                    if ((node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0) {
                        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) lbl_bytes += 7;
                        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) lbl_bytes += 7;
                        if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) lbl_bytes += 7;
                        if (node.logic_flags != 0u && node.logic_flags != ir::INVALID_SLOT) lbl_bytes += 7;
                    }
                    return lbl_bytes;
                }
                return 1;
            case MachineForm::Move:
            case MachineForm::LoadStack:
            case MachineForm::StoreStack:
                return 14;
            case MachineForm::LoadImmediate: {
                const bool is_float_const = (node.opcode == ir::OpCode::CONST && ((node.flags & ir::Flags::IS_SPEC_CONST) != 0));
                return is_float_const ? 23 : 17; // Corrected size estimation to avoid layout drift
            }
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                const bool is64 = (form == MachineForm::IntegerBinary64);
                const auto kind = is64 ? static_cast<generated::X64IntegerBinary32Kind>(generated::integer_binary64_kind(node.opcode))
                                       : generated::integer_binary32_kind(node.opcode);
                if (kind == generated::X64IntegerBinary32Kind::Shl || kind == generated::X64IntegerBinary32Kind::Shr) {
                    return is64 ? 24 : 23;
                }
                if (kind == generated::X64IntegerBinary32Kind::Mul) {
                    return is64 ? 22 : 21;
                }
                if (kind == generated::X64IntegerBinary32Kind::Div || kind == generated::X64IntegerBinary32Kind::Mod) {
                    return is64 ? 23 : 21;
                }
                return is64 ? 21 : 20;
            }
            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64:
                return 24;
            case MachineForm::FloatTernaryFma32:
                return 32;
            case MachineForm::VectorBinary512:
                return 28;
            case MachineForm::VectorStore512:
                return 22;
            case MachineForm::VectorGather512:
                return 32;
            case MachineForm::Compare32:
                return 27;
            case MachineForm::Call: {
                const size_t arg1_size = (node.op_b != ir::INVALID_SLOT) ? 7 : 2;
                const size_t arg2_size = (node.op_c != ir::INVALID_SLOT) ? 7 : 2;
                const size_t result_size = (node.result_id != ir::INVALID_SLOT) ? 7 : 1;
                return arg1_size + arg2_size + 10 + 4 + 14 + 4 + result_size + 10;
            }
            case MachineForm::Return:
                return (node.op_a != ir::INVALID_SLOT) ? 9 : 4;
            case MachineForm::Goto:
                return 5;
            case MachineForm::BranchCond:
                return 16;
            case MachineForm::BranchCompare:
                return 23;
            case MachineForm::Syscall: {
                const size_t arg0_size = (node.op_b != ir::INVALID_SLOT) ? 7 : 3;
                const size_t arg1_size = (node.op_c != ir::INVALID_SLOT) ? 7 : 3;
                const size_t result_size = (node.result_id != ir::INVALID_SLOT) ? 7 : 0;
                return 7 + arg0_size + arg1_size + 2 + result_size;
            }
            default:
                return 1;
        }
    }

    [[nodiscard]] static constexpr size_t x64_node_size(ir::OpCode opcode) noexcept {
        ir::IRNode dummy{};
        dummy.opcode = opcode;
        return x64_node_size(dummy);
    }

public:
    [[nodiscard]] static bool build_wasm_binary(const ir::IRStream& stream,
                                                 std::vector<uint8_t>& out) {
        out.clear();
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) [[unlikely]] {
            return false;
        }
        dgtl::exec::DGTLWasmEncoder encoder;
        encoder.encode(stream);
        out = encoder.get_binary();
        return !out.empty();
    }

    [[nodiscard]] static bool inspect_elf_object_from_memory(
        const void* data,
        size_t size,
        dgtl::execution::format::Object_Format& out_object) {
        if (data == nullptr || size == 0) {
            return false;
        }
        try {
            dgtl::execution::format::Object_Loader loader;
            auto obj = loader.load_from_memory(data, size);
            if (!obj) {
                return false;
            }

            for (const auto& s : obj->get_symbols()) {
                if (s.is_extern) {
                    out_object.add_extern_symbol(s.name, s.binding);
                } else {
                    out_object.add_symbol(s.name, s.address, s.size, s.binding, s.type);
                }
            }
            for (const auto& r : obj->get_relocations()) {
                out_object.add_relocation(r.offset, r.symbol_name, r.addend, r.type);
            }
            out_object.get_text_section() = obj->get_text_section();
            out_object.get_data_section() = obj->get_data_section();
            out_object.get_rodata_section() = obj->get_rodata_section();
            out_object.get_bss_section() = obj->get_bss_section();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool build_elf_relocatable_binary(const ir::IRStream& stream,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();
        if (stream.is_null() || stream.size() == 0 || !stream.validate_shadow_integrity()) [[unlikely]] {
            return false;
        }

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 32);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) {
            return false;
        }
        text_bytes.resize(text_size);
        if (text_bytes.empty()) {
            return false;
        }

        std::string shstrtab;
        shstrtab.reserve(64);
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
        strtab.reserve(entry_symbol.size() + 2);
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

        shdr[5].sh_name = sh_name_relatext;
        shdr[5].sh_type = SHT_RELA;
        shdr[5].sh_flags = SHF_INFO_LINK;
        shdr[5].sh_addr = 0;
        shdr[5].sh_offset = 0;
        shdr[5].sh_size = 0;
        shdr[5].sh_link = 4;
        shdr[5].sh_info = 1;
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

    struct CoffCallReloc {
        uint32_t text_offset = 0;
        std::string callee_name;
    };

    static std::vector<CoffCallReloc> collect_call_relocs(
        const ir::IRStream& stream,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        std::vector<CoffCallReloc> relocs;
        if (stream.is_null()) [[unlikely]] return relocs;
        relocs.reserve(stream.size() / 10);

        size_t text_offset = x64_prologue_size();
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                const size_t rcx_size = (node.op_a != ir::INVALID_SLOT) ? 7u : 2u;
                const size_t rdx_size = (node.op_b != ir::INVALID_SLOT) ? 7u : 2u;
                const size_t r8_size = (node.op_c != ir::INVALID_SLOT) ? 7u : 3u;
                const size_t disp_offset = text_offset + rcx_size + rdx_size + r8_size + 10u + 4u + 1u;
                const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                if (symbol != nullptr) {
                    relocs.push_back({static_cast<uint32_t>(disp_offset), std::string(symbol)});
                }
                text_offset += x64_node_size(node);
            } else {
                switch (node.opcode) {
                    case ir::OpCode::CALL: {
                        const size_t rcx_size = (node.op_b != ir::INVALID_SLOT) ? 7u : 2u;
                        const size_t rdx_size = (node.op_c != ir::INVALID_SLOT) ? 7u : 2u;
                        const size_t disp_offset = text_offset + rcx_size + rdx_size + 10u + 4u + 1u;
                        auto it = slot_to_callee.find(node.op_a);
                        if (it != slot_to_callee.end()) {
                            relocs.push_back({static_cast<uint32_t>(disp_offset), it->second});
                        }
                        text_offset += x64_node_size(node);
                        break;
                    }
                    default:
                        text_offset += x64_node_size(node);
                        break;
                }
            }
        }
        return relocs;
    }

    bool build_coff_relocatable_binary_with_relocs(
        const ir::IRStream& stream,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol,
        const std::vector<CoffCallReloc>& call_relocs)
    {
        out.clear();
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return false;

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 64 + 256);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) return false;
        text_bytes.resize(text_size);
        if (text_bytes.empty()) return false;

        using namespace dgtl::backend::linker::coff;

        struct CoffReloc { uint32_t VirtualAddress; uint32_t SymbolTableIndex; uint16_t Type; };
        static constexpr uint16_t kCoffRelAmd64Rel32 = 4;

        std::vector<std::string> extern_names;
        extern_names.reserve(call_relocs.size());
        std::unordered_map<std::string, uint32_t> extern_sym_index;
        extern_sym_index.reserve(call_relocs.size());
        for (const auto& r : call_relocs) {
            auto [it, inserted] = extern_sym_index.emplace(r.callee_name, 0u);
            if (inserted) {
                extern_names.push_back(r.callee_name);
            }
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(extern_names.size()); ++i) {
            extern_sym_index[extern_names[i]] = 2u + i;
        }

        std::vector<CoffReloc> coff_relocs;
        coff_relocs.reserve(call_relocs.size());
        for (const auto& r : call_relocs) {
            uint32_t offset = r.text_offset;
            if (offset + sizeof(uint32_t) > text_size) {
                if (text_size >= 4) {
                    offset = static_cast<uint32_t>(text_size - 4);
                } else {
                    continue;
                }
            }
            const auto it = extern_sym_index.find(r.callee_name);
            if (it == extern_sym_index.end()) {
                continue;
            }
            const uint32_t sym_idx = it->second;
            coff_relocs.push_back({offset, sym_idx, kCoffRelAmd64Rel32});
        }
        if (coff_relocs.size() > std::numeric_limits<uint16_t>::max()) {
            return false;
        }

        std::string sym_name(entry_symbol);
        std::string strtab_data; 
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
        extern_str_infos.reserve(extern_names.size());
        std::transform(extern_names.begin(), extern_names.end(), std::back_inserter(extern_str_infos),
                       [&make_sym_str](const auto& e) { return make_sym_str(e); });

        const size_t num_symbols = 1 + 1 + extern_names.size();
        const uint32_t strtab_total_size = 4 + static_cast<uint32_t>(strtab_data.size());

        const size_t file_hdr_size  = sizeof(FileHeader);
        const size_t sect_hdr_size  = sizeof(SectionHeader);
        const size_t text_off       = file_hdr_size + sect_hdr_size;
        const size_t reloc_off      = text_off + text_size;
        const size_t symtab_off     = reloc_off + coff_relocs.size() * 10;
        const size_t symtab_size    = num_symbols * sizeof(Symbol);
        const size_t total_size     = symtab_off + symtab_size + strtab_total_size;
        out.assign(total_size, 0);

        auto* fhdr = reinterpret_cast<FileHeader*>(out.data());
        fhdr->Machine              = MACHINE_AMD64;
        fhdr->NumberOfSections     = 1;
        fhdr->TimeDateStamp        = 0;
        fhdr->PointerToSymbolTable = static_cast<uint32_t>(symtab_off);
        fhdr->NumberOfSymbols      = static_cast<uint32_t>(num_symbols);
        fhdr->SizeOfOptionalHeader = 0;
        fhdr->Characteristics      = 0;

        auto* shdr = reinterpret_cast<SectionHeader*>(out.data() + file_hdr_size);
        std::memset(shdr, 0, sizeof(SectionHeader));
        std::memcpy(shdr->Name, ".text\0\0\0", 8);
        shdr->VirtualSize          = 0;
        shdr->VirtualAddress       = 0;
        shdr->SizeOfRawData        = static_cast<uint32_t>(text_size);
        shdr->PointerToRawData     = static_cast<uint32_t>(text_off);
        shdr->PointerToRelocations = coff_relocs.empty() ? 0 : static_cast<uint32_t>(reloc_off);
        shdr->NumberOfRelocations  = static_cast<uint16_t>(coff_relocs.size());
        shdr->Characteristics      = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_ALIGN_16BYTES;

        std::memcpy(out.data() + text_off, text_bytes.data(), text_size);

        for (size_t ri = 0; ri < coff_relocs.size(); ++ri) {
            uint8_t* rptr = out.data() + reloc_off + ri * 10;
            std::memcpy(rptr,     &coff_relocs[ri].VirtualAddress, 4);
            std::memcpy(rptr + 4, &coff_relocs[ri].SymbolTableIndex, 4);
            std::memcpy(rptr + 8, &coff_relocs[ri].Type, 2);
        }

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
        fill_sym(sym_tbl[0], ".text", {false, 0}, 0, 1, SYM_CLASS_STATIC);
        fill_sym(sym_tbl[1], sym_name, entry_str, 0, 1, SYM_CLASS_EXTERNAL);
        for (size_t ei = 0; ei < extern_names.size(); ++ei) {
            fill_sym(sym_tbl[2 + ei], extern_names[ei], extern_str_infos[ei], 0, 0, SYM_CLASS_EXTERNAL);
        }

        uint8_t* strtab_ptr = out.data() + size_t(symtab_off) + symtab_size;
        std::memcpy(strtab_ptr, &strtab_total_size, 4);
        if (!strtab_data.empty()) {
            std::memcpy(strtab_ptr + 4, strtab_data.data(), strtab_data.size());
        }

        return true;
    }

    bool build_module_coff_with_relocs(
        const std::vector<std::pair<std::string, ir::IRStream>>& module_streams,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        out.clear();
        if (module_streams.empty()) [[unlikely]] return false;

        using namespace dgtl::backend::linker::coff;
        struct CoffReloc { uint32_t VirtualAddress; uint32_t SymbolTableIndex; uint16_t Type; };
        static constexpr uint16_t kCoffRelAmd64Rel32 = 4;

        std::vector<uint8_t> master_text_bytes;
        master_text_bytes.reserve(1024 * 1024);

        struct ModuleFunc { std::string name; size_t text_offset = 0; size_t size = 0; };
        std::vector<ModuleFunc> functions;
        std::vector<CoffCallReloc> master_relocs;
        std::unordered_map<std::string, uint32_t> internal_func_index;

        internal_func_index.reserve(module_streams.size());

        for (const auto& [name, stream] : module_streams) {
            if (stream.is_null() || stream.size() == 0) continue;
            
            size_t func_start_offset = master_text_bytes.size();
            
            auto local_relocs = collect_call_relocs(stream, slot_to_callee);
            for (auto& r : local_relocs) {
                r.text_offset += static_cast<uint32_t>(func_start_offset);
                master_relocs.push_back(std::move(r));
            }

            size_t emitted = 0;
            size_t capacity_needed = stream.size() * 64 + 256;
            master_text_bytes.resize(func_start_offset + capacity_needed);
            
            if (!emit_machine_code(master_text_bytes.data() + func_start_offset, capacity_needed, stream, &emitted)) {
                return false;
            }
            
            master_text_bytes.resize(func_start_offset + emitted);
            internal_func_index.emplace(name, static_cast<uint32_t>(functions.size()));
            functions.push_back({name, func_start_offset, emitted});
        }

        if (master_text_bytes.empty()) return false;

        std::vector<std::string> extern_names;
        std::unordered_map<std::string, uint32_t> extern_index;
        extern_index.reserve(master_relocs.size());

        for (const auto& r : master_relocs) {
            if (internal_func_index.find(r.callee_name) != internal_func_index.end()) {
                continue;
            }
            auto [it, inserted] = extern_index.emplace(r.callee_name, 0u);
            if (inserted) {
                extern_names.push_back(r.callee_name);
            }
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(extern_names.size()); ++i) {
            extern_index[extern_names[i]] = i;
        }

        std::vector<CoffReloc> coff_relocs;
        coff_relocs.reserve(master_relocs.size());

        for (const auto& r : master_relocs) {
            uint32_t offset = r.text_offset;
            if (offset + sizeof(uint32_t) > master_text_bytes.size()) {
                if (master_text_bytes.size() >= 4) {
                    offset = static_cast<uint32_t>(master_text_bytes.size() - 4);
                } else {
                    continue;
                }
            }
            uint32_t sym_idx = 1;
            const auto it_internal = internal_func_index.find(r.callee_name);
            if (it_internal != internal_func_index.end()) {
                sym_idx += it_internal->second;
            } else {
                sym_idx += static_cast<uint32_t>(functions.size());
                const auto it_external = extern_index.find(r.callee_name);
                if (it_external == extern_index.end()) {
                    continue;
                }
                sym_idx += it_external->second;
            }
            coff_relocs.push_back({offset, sym_idx, kCoffRelAmd64Rel32});
        }
        if (coff_relocs.size() > std::numeric_limits<uint16_t>::max()) {
            return false;
        }

        std::string strtab_data; 
        struct SymStrInfo { bool use_long; uint32_t strtab_off; };
        auto make_sym_str = [&](const std::string& n) -> SymStrInfo {
            if (n.size() <= 8) return {false, 0};
            uint32_t off = 4 + static_cast<uint32_t>(strtab_data.size());
            strtab_data.append(n);
            strtab_data.push_back('\0');
            return {true, off};
        };

        std::vector<SymStrInfo> func_str_infos;
        func_str_infos.reserve(functions.size());
        std::transform(functions.begin(), functions.end(), std::back_inserter(func_str_infos),
                       [&make_sym_str](const auto& f) { return make_sym_str(f.name); });
        
        std::vector<SymStrInfo> extern_str_infos;
        extern_str_infos.reserve(extern_names.size());
        std::transform(extern_names.begin(), extern_names.end(), std::back_inserter(extern_str_infos),
                       [&make_sym_str](const auto& e) { return make_sym_str(e); });

        const size_t num_symbols = 1 + functions.size() + extern_names.size(); 
        const uint32_t strtab_total_size = 4 + static_cast<uint32_t>(strtab_data.size());

        const size_t file_hdr_size  = sizeof(FileHeader);
        const size_t sect_hdr_size  = sizeof(SectionHeader);
        const size_t text_off       = file_hdr_size + sect_hdr_size;
        const size_t reloc_off      = text_off + master_text_bytes.size();
        const size_t symtab_off     = reloc_off + coff_relocs.size() * 10;
        const size_t symtab_size    = num_symbols * sizeof(Symbol);
        const size_t total_size     = symtab_off + symtab_size + strtab_total_size;
        
        out.assign(total_size, 0);

        auto* fhdr = reinterpret_cast<FileHeader*>(out.data());
        fhdr->Machine              = MACHINE_AMD64;
        fhdr->NumberOfSections     = 1;
        fhdr->TimeDateStamp        = 0;
        fhdr->PointerToSymbolTable = static_cast<uint32_t>(symtab_off);
        fhdr->NumberOfSymbols      = static_cast<uint32_t>(num_symbols);
        fhdr->SizeOfOptionalHeader = 0;
        fhdr->Characteristics      = 0;

        auto* shdr = reinterpret_cast<SectionHeader*>(out.data() + file_hdr_size);
        std::memset(shdr, 0, sizeof(SectionHeader));
        std::memcpy(shdr->Name, ".text\0\0\0", 8);
        shdr->VirtualSize          = 0;
        shdr->VirtualAddress       = 0;
        shdr->SizeOfRawData        = static_cast<uint32_t>(master_text_bytes.size());
        shdr->PointerToRawData     = static_cast<uint32_t>(text_off);
        shdr->PointerToRelocations = coff_relocs.empty() ? 0 : static_cast<uint32_t>(reloc_off);
        shdr->NumberOfRelocations  = static_cast<uint16_t>(coff_relocs.size());
        shdr->Characteristics      = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_ALIGN_16BYTES;

        std::memcpy(out.data() + text_off, master_text_bytes.data(), master_text_bytes.size());

        for (size_t ri = 0; ri < coff_relocs.size(); ++ri) {
            uint8_t* rptr = out.data() + reloc_off + ri * 10;
            std::memcpy(rptr,     &coff_relocs[ri].VirtualAddress, 4);
            std::memcpy(rptr + 4, &coff_relocs[ri].SymbolTableIndex, 4);
            std::memcpy(rptr + 8, &coff_relocs[ri].Type, 2);
        }

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
        fill_sym(sym_tbl[0], ".text", {false, 0}, 0, 1, SYM_CLASS_STATIC);
        
        size_t current_sym_idx = 1;
        for (size_t i = 0; i < functions.size(); ++i) {
            fill_sym(sym_tbl[current_sym_idx++], functions[i].name, func_str_infos[i], 
                     static_cast<uint32_t>(functions[i].text_offset), 1, SYM_CLASS_EXTERNAL);
        }
        for (size_t i = 0; i < extern_names.size(); ++i) {
            fill_sym(sym_tbl[current_sym_idx++], extern_names[i], extern_str_infos[i], 
                     0, 0, SYM_CLASS_EXTERNAL);
        }

        uint8_t* strtab_ptr = out.data() + size_t(symtab_off) + symtab_size;
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
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return false;

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 32);
        size_t text_size = 0;
        if (!emit_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) return false;
        text_bytes.resize(text_size);
        if (text_bytes.empty()) return false;

        using namespace dgtl::backend::linker::coff;

        const size_t file_hdr_size = sizeof(FileHeader);
        const size_t sect_hdr_size = sizeof(SectionHeader);
        const size_t num_sections = 1;
        const size_t text_offset = file_hdr_size + sect_hdr_size * num_sections;
        const size_t symtab_offset = text_offset + text_size;

        std::string sym_name(entry_symbol);
        bool use_long_name = sym_name.size() > 8;
        uint32_t sym_strtab_offset = 0;
        std::string strtab_data;
        if (use_long_name) {
            sym_strtab_offset = 4;
            strtab_data.append(sym_name);
            strtab_data.push_back('\0');
        }

        const size_t num_symbols = 2;
        const size_t symtab_size = num_symbols * sizeof(Symbol);
        const uint32_t strtab_total_size = 4 + static_cast<uint32_t>(strtab_data.size());
        const size_t total_size = symtab_offset + symtab_size + strtab_total_size;
        out.assign(total_size, 0);

        auto* fhdr = reinterpret_cast<FileHeader*>(out.data());
        fhdr->Machine = MACHINE_AMD64;
        fhdr->NumberOfSections = static_cast<uint16_t>(num_sections);
        fhdr->TimeDateStamp = 0;
        fhdr->PointerToSymbolTable = static_cast<uint32_t>(symtab_offset);
        fhdr->NumberOfSymbols = static_cast<uint32_t>(num_symbols);
        fhdr->SizeOfOptionalHeader = 0;
        fhdr->Characteristics = 0;

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

        std::memcpy(out.data() + text_offset, text_bytes.data(), text_size);

        auto* sym = reinterpret_cast<Symbol*>(out.data() + symtab_offset);

        std::memset(&sym[0], 0, sizeof(Symbol));
        std::memcpy(sym[0].Name.ShortName, ".text\0\0\0", 8);
        sym[0].Value = 0;
        sym[0].SectionNumber = 1;
        sym[0].Type = 0;
        sym[0].StorageClass = SYM_CLASS_STATIC;
        sym[0].NumberOfAuxSymbols = 0;

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

        uint8_t* strtab_ptr = out.data() + size_t(symtab_offset) + symtab_size;
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

        std::string sym_name(entry_symbol);
        sym_name.push_back('\0');

        const uint32_t num_symbols_be = to_big_endian_u32(1);
        const size_t first_member_content_size = 4 + 4 + sym_name.size();

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

    [[nodiscard]] const std::vector<std::string>& fallback_diagnostics() const noexcept {
        return fallback_diagnostics_;
    }

    void emit_elf64(std::string_view path, const ir::IRStream& ir_stream) {
        if (ir_stream.is_null()) [[unlikely]] return;
        release_mapping();
        reset_mapping_state();
        
        file_size = (ir_stream.size() * 32) + 0x2000; 
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
        output_ptr = static_cast<uint8_t*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, file_size));
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

        if (!output_ptr || output_ptr == DGTL_MAP_FAILED) [[unlikely]] {
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

        auto* prog_hdr = reinterpret_cast<Elf64_Phdr*>(output_ptr + elf_hdr->e_phoff);
        prog_hdr->p_type = PT_LOAD;
        prog_hdr->p_offset = 0x1000; 
        prog_hdr->p_vaddr = 0x401000;
        prog_hdr->p_filesz = static_cast<uint64_t>(ir_stream.size()) * 32;
        prog_hdr->p_memsz = prog_hdr->p_filesz;
        prog_hdr->p_flags = PF_R | PF_X;
        prog_hdr->p_align = 0x1000;

        size_t emitted_size = 0;
        const bool ok = emit_machine_code(output_ptr + prog_hdr->p_offset,
                                          static_cast<size_t>(prog_hdr->p_filesz),
                                          ir_stream,
                                          &emitted_size);
        if (!ok) [[unlikely]] {
            release_mapping();
            return;
        }
        prog_hdr->p_filesz = emitted_size;
        prog_hdr->p_memsz = emitted_size;

#if !defined(_WIN32)
        msync(output_ptr, file_size, MS_SYNC);
#endif
    }

    bool build_raw_x64_machine_code(const ir::IRStream& stream,
                                    std::vector<uint8_t>& out_bytes,
                                    const std::unordered_map<uint32_t, uintptr_t>& slot_map = {}) {
        out_bytes.resize(stream.size() * 64 + 1024);
        size_t emitted_size = 0;
        if (!emit_machine_code(out_bytes.data(), out_bytes.size(), stream, &emitted_size, slot_map)) {
            return false;
        }
        out_bytes.resize(emitted_size);
        return true;
    }

protected:
    bool emit_machine_code(uint8_t* dest,
                           size_t dest_capacity,
                           const ir::IRStream& stream,
                           size_t* emitted_size = nullptr,
                           const std::unordered_map<uint32_t, uintptr_t>& slot_map = {}) noexcept {
        if (stream.is_null() || !dest || stream.size() == 0) [[unlikely]] return false;
        clear_fallback_diagnostics();
        const size_t count = stream.size();

        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(count / 4 + 4);

        std::unordered_map<uint32_t, int32_t> slot_offsets;
        slot_offsets.reserve(count * 2 + 8);
        uint32_t next_slot_index = 0;
        auto assign_slot = [&](uint32_t slot_id) {
            if (slot_id == ir::INVALID_SLOT) return;
            if (slot_offsets.find(slot_id) != slot_offsets.end()) return;
            slot_offsets.emplace(slot_id, -8 * static_cast<int32_t>(++next_slot_index));
        };

        assign_slot(2u);
        assign_slot(3u);

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LABEL) {
                if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                    label_map[node.result_id] = i;
                }
                if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                    label_map[node.op_a] = i;
                }
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                    label_map[node.op_b] = i;
                }
                label_map[static_cast<uint32_t>(i)] = i;
                continue;
            }

            assign_slot(node.result_id);
            assign_slot(node.op_a);
            assign_slot(node.op_b);
            assign_slot(node.op_c);
            if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
                assign_slot(node.logic_flags);
            }
        }

        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            for (size_t k = 0; k < count; ++k) {
                const auto& knode = stream.get_node(k);
                if (knode.opcode == ir::OpCode::LABEL) {
                    if (knode.result_id == raw || knode.op_a == raw || knode.op_b == raw) {
                        return k;
                    }
                }
            }
            return count;
        };

        const uint32_t raw_frame_size = (next_slot_index + 2u) * 8u;
        const uint32_t frame_size = (raw_frame_size + 15u) & ~15u;

        std::vector<size_t> node_offsets(count + 1, 0);
        size_t probe = x64_prologue_size(stream);
        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = probe;
            const auto& node = stream.get_node(i);
            probe += x64_node_size(node);
        }
        node_offsets[count] = probe;

        if (probe > dest_capacity) [[unlikely]] return false;

        struct BranchPatch { size_t disp_offset; size_t source_end; size_t target_node; };
        std::vector<BranchPatch> patches;

        const int32_t fallback_offset = -8 * static_cast<int32_t>(next_slot_index + 1u);
        auto slot_disp = [&](uint32_t slot_id) -> int32_t {
            if (slot_id == ir::INVALID_SLOT) return fallback_offset;
            const auto it = slot_offsets.find(slot_id);
            return (it != slot_offsets.end()) ? it->second : fallback_offset;
        };
        patches.reserve(count / 8 + 4);

        size_t offset = 0;
        auto write_u8 = [&](uint8_t byte) { dest[offset++] = byte; };
        auto write_u32 = [&](uint32_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        auto write_i32 = [&](int32_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        auto write_u64 = [&](uint64_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        auto emit_mov_reg_from_slot = [&](uint8_t rex, uint8_t modrm, uint32_t slot_id) {
            write_u8(rex);
            write_u8(0x8B);
            write_u8(modrm);
            write_i32(slot_disp(slot_id));
        };
        auto emit_mov_slot_from_reg = [&](uint8_t rex, uint8_t modrm, uint32_t slot_id) {
            write_u8(rex);
            write_u8(0x89);
            write_u8(modrm);
            write_i32(slot_disp(slot_id));
        };
        auto emit_mov_rax_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x85, slot_id); };
        auto emit_mov_rcx_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x8D, slot_id); };
        auto emit_mov_rdx_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x95, slot_id); };
        auto emit_mov_r8_from_slot  = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x4C, 0x85, slot_id); };
        auto emit_store_rax_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x85, slot_id); };
        auto emit_store_rdx_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x95, slot_id); };
        auto emit_store_rcx_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x8D, slot_id); };
        auto emit_store_r8_to_slot  = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x4C, 0x85, slot_id); };
        auto emit_store_r9_to_slot  = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x4C, 0x8D, slot_id); };

        write_u8(0x55);
        write_u8(0x48); write_u8(0x89); write_u8(0xE5);
        write_u8(0x48); write_u8(0x81); write_u8(0xEC); write_u32(frame_size);
        if (count > 0 && stream.get_node(0).opcode == ir::OpCode::LABEL) {
            const auto& fn_hdr = stream.get_node(0);
            if (fn_hdr.op_a != ir::INVALID_SLOT && fn_hdr.op_a != 0u) emit_store_rcx_to_slot(fn_hdr.op_a);
            if (fn_hdr.op_b != ir::INVALID_SLOT && fn_hdr.op_b != 0u) emit_store_rdx_to_slot(fn_hdr.op_b);
            if (fn_hdr.op_c != ir::INVALID_SLOT && fn_hdr.op_c != 0u) emit_store_r8_to_slot(fn_hdr.op_c);
            if (fn_hdr.logic_flags != 0u && fn_hdr.logic_flags != ir::INVALID_SLOT) emit_store_r9_to_slot(fn_hdr.logic_flags);
        }

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            using namespace dgtl::isa::lowering;
            const MachineForm form = generated::classify_machine_form(node.opcode);

            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) emit_mov_rcx_from_slot(node.op_b);
                else { write_u8(0x31); write_u8(0xC9); }
                if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) emit_mov_rdx_from_slot(node.op_c);
                else { write_u8(0x31); write_u8(0xD2); }
                if (node.logic_flags != 0u && node.logic_flags != ir::INVALID_SLOT) {
                    write_u8(0x4C); write_u8(0x8B); write_u8(0x85);
                    write_i32(slot_disp(node.logic_flags));
                }
                else { write_u8(0x4D); write_u8(0x31); write_u8(0xC0); }

                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx

                write_u8(0x48); write_u8(0x83); write_u8(0xEC); write_u8(0x20);

                uintptr_t rt_target_addr = 0;
                if (!slot_map.empty()) {
                    auto it = slot_map.find(static_cast<uint32_t>(node.opcode));
                    if (it != slot_map.end()) {
                        rt_target_addr = it->second;
                    }
                }
                if (rt_target_addr == 0 && node.intent_hash > 0x7FFF00000000ULL) {
                    rt_target_addr = static_cast<uintptr_t>(node.intent_hash);
                }

                if (rt_target_addr != 0) {
                    write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(rt_target_addr));
                    write_u8(0xFF); write_u8(0xD0);
                } else {
                    write_u8(0xE8);
                    write_u8(0x00); write_u8(0x00); write_u8(0x00); write_u8(0x00);
                }
                write_u8(0x48); write_u8(0x83); write_u8(0xC4); write_u8(0x20); // add rsp, 32

                if (node.result_id != ir::INVALID_SLOT) emit_store_rax_to_slot(node.result_id);
                else write_u8(0x90);
                continue;
            }

            switch (form) {
                case MachineForm::NoOp:
                    if (node.opcode == ir::OpCode::LABEL) {
                        if ((node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0) {
                            if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) emit_store_rcx_to_slot(node.op_a);
                            if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) emit_store_rdx_to_slot(node.op_b);
                            if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) emit_store_r8_to_slot(node.op_c);
                            if (node.logic_flags != 0u && node.logic_flags != ir::INVALID_SLOT) emit_store_r9_to_slot(node.logic_flags);
                        }
                    } else {
                        write_u8(0x90);
                    }
                    break;

                case MachineForm::Move:
                case MachineForm::LoadStack:
                case MachineForm::StoreStack: {
                    if (form == MachineForm::StoreStack) {
                        emit_mov_rax_from_slot(node.op_b);
                        emit_store_rax_to_slot(node.op_a);
                    } else {
                        emit_mov_rax_from_slot(node.op_a);
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::LoadImmediate: {
                    uint64_t imm = (static_cast<uint64_t>(node.op_b) << 32u) | (static_cast<uint64_t>(node.op_a) & 0xFFFFFFFFu);
                    if (imm == 0 && node.intent_hash != 0) {
                        imm = node.intent_hash;
                    }

                    const bool is_float_const = (node.opcode == ir::OpCode::CONST && ((node.flags & ir::Flags::IS_SPEC_CONST) != 0));

                    if (is_float_const) {
                        write_u8(0x48); write_u8(0xB8); write_u64(imm); // movabs rax, imm64
                        write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC0); // movq xmm0, rax
                        write_u8(0xF2); write_u8(0x0F); write_u8(0x11); write_u8(0x85); // movsd [rbp-disp], xmm0
                        write_i32(slot_disp(node.result_id));
                    } else {
                        write_u8(0x48); write_u8(0xB8); write_u64(imm); // movabs rax, imm64
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::IntegerBinary32:
                case MachineForm::IntegerBinary64: {
                    const bool is64 = (form == MachineForm::IntegerBinary64);
                    generated::X64IntegerBinary32Kind kind = generated::X64IntegerBinary32Kind::None;
                    if (is64) {
                        switch (generated::integer_binary64_kind(node.opcode)) {
                            case generated::X64IntegerBinary64Kind::Add: kind = generated::X64IntegerBinary32Kind::Add; break;
                            case generated::X64IntegerBinary64Kind::Sub: kind = generated::X64IntegerBinary32Kind::Sub; break;
                            case generated::X64IntegerBinary64Kind::Mul: kind = generated::X64IntegerBinary32Kind::Mul; break;
                            case generated::X64IntegerBinary64Kind::Div: kind = generated::X64IntegerBinary32Kind::Div; break;
                            case generated::X64IntegerBinary64Kind::Mod: kind = generated::X64IntegerBinary32Kind::Mod; break;
                            case generated::X64IntegerBinary64Kind::And: kind = generated::X64IntegerBinary32Kind::And; break;
                            case generated::X64IntegerBinary64Kind::Or:  kind = generated::X64IntegerBinary32Kind::Or;  break;
                            case generated::X64IntegerBinary64Kind::Xor: kind = generated::X64IntegerBinary32Kind::Xor; break;
                            case generated::X64IntegerBinary64Kind::Shl: kind = generated::X64IntegerBinary32Kind::Shl; break;
                            case generated::X64IntegerBinary64Kind::Shr: kind = generated::X64IntegerBinary32Kind::Shr; break;
                            default: break;
                        }
                    } else {
                        kind = generated::integer_binary32_kind(node.opcode);
                    }
                    if (kind == generated::X64IntegerBinary32Kind::Shl || 
                        kind == generated::X64IntegerBinary32Kind::Shr) {
                        emit_mov_rax_from_slot(node.op_a);
                        emit_mov_rcx_from_slot(node.op_b);
                        if (is64) write_u8(0x48);
                        write_u8(0xD3);
                        write_u8(kind == generated::X64IntegerBinary32Kind::Shl ? 0xE0 : 0xE8);
                        emit_store_rax_to_slot(node.result_id);
                    }
                    else if (kind == generated::X64IntegerBinary32Kind::Div || 
                             kind == generated::X64IntegerBinary32Kind::Mod) {
                        emit_mov_rax_from_slot(node.op_a);
                        if (is64) {
                            write_u8(0x48); write_u8(0x99);
                            write_u8(0x48); write_u8(0xF7); write_u8(0xBD);
                        } else {
                            write_u8(0x99);
                            write_u8(0xF7); write_u8(0xBD);
                        }
                        write_i32(slot_disp(node.op_b));
                        if (kind == generated::X64IntegerBinary32Kind::Div)
                            emit_store_rax_to_slot(node.result_id);
                        else emit_store_rdx_to_slot(node.result_id);
                    }
                    else {
                        emit_mov_rax_from_slot(node.op_a);
                        if (is64) {
                            write_u8(0x48);
                        }
                        if (kind == generated::X64IntegerBinary32Kind::Add) {
                            write_u8(0x03); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Sub) {
                            write_u8(0x2B); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::And) {
                            write_u8(0x23); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Or) {
                            write_u8(0x0B); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Xor) {
                            write_u8(0x33); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Mul) {
                            write_u8(0x0F); write_u8(0xAF); write_u8(0x85);
                        }
                        write_i32(slot_disp(node.op_b));
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::FloatBinary32:
                case MachineForm::FloatBinary64: {
                    const bool is64 = (form == MachineForm::FloatBinary64);
                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F); write_u8(0x10); write_u8(0x85);
                    write_i32(slot_disp(node.op_a));

                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F);
                    if (is64) {
                        const auto kind = generated::float_binary64_kind(node.opcode);
                        using K = generated::X64FloatBinary64Kind;
                        if (kind == K::Add) write_u8(0x58);
                        else if (kind == K::Sub) write_u8(0x5C);
                        else if (kind == K::Mul) write_u8(0x59);
                        else if (kind == K::Div) write_u8(0x5E);
                        else write_u8(0x58);
                    } else {
                        const auto kind = generated::float_binary32_kind(node.opcode);
                        using K = generated::X64FloatBinary32Kind;
                        if (kind == K::Add) write_u8(0x58);
                        else if (kind == K::Sub) write_u8(0x5C);
                        else if (kind == K::Mul) write_u8(0x59);
                        else if (kind == K::Div) write_u8(0x5E);
                        else write_u8(0x58);
                    }
                    write_u8(0x85);
                    write_i32(slot_disp(node.op_b));

                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F); write_u8(0x11); write_u8(0x85);
                    write_i32(slot_disp(node.result_id));
                    break;
                }

                case MachineForm::FloatTernaryFma32: {
                    write_u8(0xF3); write_u8(0x0F); write_u8(0x10); write_u8(0x85); write_i32(slot_disp(node.op_a));
                    write_u8(0xF3); write_u8(0x0F); write_u8(0x10); write_u8(0x8D); write_i32(slot_disp(node.op_b));
                    write_u8(0xC4); write_u8(0xE2); write_u8(0x79); write_u8(0x98); write_u8(0x85); write_i32(slot_disp(node.op_c));
                    write_u8(0xF3); write_u8(0x0F); write_u8(0x11); write_u8(0x85); write_i32(slot_disp(node.result_id));
                    break;
                }

                case MachineForm::VectorBinary512: {
                    write_u8(0x62); write_u8(0xF1); write_u8(0x7C); write_u8(0x48);
                    write_u8(node.opcode == ir::OpCode::VEC_ADD_F32_512 ? 0x58 : 0x59);
                    write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x62); write_u8(0xF1); write_u8(0x7C); write_u8(0x48); write_u8(0x11); write_u8(0x85);
                    write_i32(slot_disp(node.result_id));
                    break;
                }

                case MachineForm::VectorStore512: {
                    write_u8(0x62); write_u8(0xF1); write_u8(0x7C); write_u8(0x48); write_u8(0x11); write_u8(0x85);
                    write_i32(slot_disp(node.op_a));
                    break;
                }

                case MachineForm::VectorGather512: {
                    write_u8(0x62); write_u8(0xF2); write_u8(0x7D); write_u8(0x48); write_u8(0x38); write_u8(0x85);
                    write_i32(slot_disp(node.op_a));
                    break;
                }

                case MachineForm::Compare32: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x3B); write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x0F);
                    switch (node.opcode) {
                        case ir::OpCode::CMP_EQ_I32: write_u8(0x94); break;
                        case ir::OpCode::CMP_NE_I32: write_u8(0x95); break;
                        case ir::OpCode::CMP_LT_I32: write_u8(0x9C); break;
                        case ir::OpCode::CMP_LE_I32: write_u8(0x9E); break;
                        case ir::OpCode::CMP_GT_I32: write_u8(0x9F); break;
                        case ir::OpCode::CMP_GE_I32: write_u8(0x9D); break;
                        default: write_u8(0x9E); break;
                    }
                    write_u8(0xC0);
                    write_u8(0x0F); write_u8(0xB6); write_u8(0xC0);
                    emit_store_rax_to_slot(node.result_id);
                    break;
                }

                case MachineForm::Call: {
                    if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                        emit_mov_rcx_from_slot(node.op_b);
                    } else { 
                        write_u8(0x31); write_u8(0xC9); 
                    }

                    if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) {
                        emit_mov_rdx_from_slot(node.op_c);
                    } else { 
                        write_u8(0x31); write_u8(0xD2); 
                    }

                    if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
                        emit_mov_r8_from_slot(node.logic_flags);
                    } else { 
                        write_u8(0x4D); write_u8(0x31); write_u8(0xC0); 
                    }

                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx

                    write_u8(0x48); write_u8(0x83); write_u8(0xEC); write_u8(0x20);

                    uintptr_t target_addr = 0;
                    if (!slot_map.empty()) {
                        auto it = slot_map.find(static_cast<uint32_t>(node.op_a));
                        if (it != slot_map.end() && it->second != 0) {
                            target_addr = it->second;
                        }
                    }
                    if (target_addr == 0 && node.op_a > 0x7FFF00000000ULL) {
                        target_addr = static_cast<uintptr_t>(node.op_a);
                    }
                    if (target_addr == 0 && node.intent_hash > 0x7FFF00000000ULL) {
                        target_addr = static_cast<uintptr_t>(node.intent_hash);
                    }

                    if (target_addr != 0) {
                        write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(target_addr));
                        write_u8(0xFF); write_u8(0xD0);
                    } else {
                        write_u8(0xE8);
                        const size_t patch_disp_offset = offset;
                        write_u32(0);
                        const size_t patch_source_end = offset;
                        patches.push_back({patch_disp_offset, patch_source_end, resolve_target(node.op_a)});
                    }
                    write_u8(0x48); write_u8(0x83); write_u8(0xC4); write_u8(0x20); // add rsp, 32

                    if (node.result_id != ir::INVALID_SLOT) emit_store_rax_to_slot(node.result_id);
                    else write_u8(0x90);
                    continue;
                }

                case MachineForm::Goto: {
                    write_u8(0xE9);
                    const uint32_t target_slot = (label_map.find(node.op_a) != label_map.end()) ? node.op_a : ((label_map.find(node.result_id) != label_map.end()) ? node.result_id : node.op_a);
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCond: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x85); write_u8(0xC0);
                    write_u8(0x0F);
                    write_u8(node.opcode == ir::OpCode::BRANCH_UNLIKELY ? 0x84 : 0x85);
                    const uint32_t target_slot = (label_map.find(node.op_b) != label_map.end()) ? node.op_b : ((label_map.find(node.result_id) != label_map.end()) ? node.result_id : node.op_b);
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCompare: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x3B); write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x0F); write_u8(0x85);
                    const uint32_t target_slot = (label_map.find(node.op_c) != label_map.end()) ? node.op_c : ((label_map.find(node.result_id) != label_map.end()) ? node.result_id : node.op_c);
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::Return: {
                    if (node.op_a != ir::INVALID_SLOT) emit_mov_rax_from_slot(node.op_a);
                    else { write_u8(0x31); write_u8(0xC0); }
                    write_u8(0xC9); write_u8(0xC3);
                    break;
                }

                case MachineForm::Syscall: {
                    emit_mov_rax_from_slot(node.op_a);
                    if (node.op_b != ir::INVALID_SLOT) {
                        emit_mov_reg_from_slot(0x48, 0xBD, node.op_b);
                    } else {
                        write_u8(0x48); write_u8(0x31); write_u8(0xFF);
                    }
                    if (node.op_c != ir::INVALID_SLOT) {
                        emit_mov_reg_from_slot(0x48, 0xB5, node.op_c);
                    } else {
                        write_u8(0x48); write_u8(0x31); write_u8(0xF6);
                    }
                    write_u8(0x0F); write_u8(0x05);
                    if (node.result_id != ir::INVALID_SLOT) {
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                default:
                    record_fallback_opcode(i, node.opcode);
                    write_u8(0x90);
                    break;
            }
        }

        for (const auto& p : patches) {
            const size_t tgt_byte = (p.target_node <= count) ? node_offsets[p.target_node] : offset;
            const int64_t rel64 = static_cast<int64_t>(tgt_byte) - static_cast<int64_t>(p.source_end);
            const int32_t rel32 = static_cast<int32_t>(rel64);
            std::memcpy(dest + p.disp_offset, &rel32, sizeof(rel32));
        }

        if (emitted_size) *emitted_size = offset;
        return true;
    }

}; // class DGTLLinker

} // namespace dgtl::backend::linker

// ========================================================================================
// BACKWARD COMPATIBILITY ALIASES (dgtl::exec)
// ========================================================================================
export namespace dgtl::exec {
    using DGTLLinker = dgtl::backend::linker::DGTLLinker;
    namespace coff = dgtl::backend::linker::coff;
} // namespace dgtl::exec