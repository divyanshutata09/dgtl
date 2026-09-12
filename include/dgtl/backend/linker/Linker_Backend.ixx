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
import dgtl.ir_core.code_cache;
import dgtl.ir_core.execution_dispatcher;
import dgtl.ir_core.wasm_encoder;
import dgtl.ir_core.runtime_contract;
import dgtl.backend.aot_linker;


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
inline constexpr uint32_t SCN_CNT_INITIALIZED_DATA = 0x00000040;
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

struct ImageDosHeader {
    uint16_t e_magic;    // "MZ" (0x5A4D)
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;   // Offset to NT headers
};

struct ImageDataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct ImageOptionalHeader64 {
    uint16_t Magic;                // 0x020B = PE32+
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    ImageDataDirectory DataDirectory[16];
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
            if (fn_hdr.op_a != ir::INVALID_SLOT) sz += 7;
            if (fn_hdr.op_b != ir::INVALID_SLOT) sz += 7;
            if (fn_hdr.op_c != ir::INVALID_SLOT) sz += 7;
            if (fn_hdr.logic_flags != ir::INVALID_SLOT && fn_hdr.logic_flags != 0u) sz += 7;
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
            const size_t arg1_size = (node.op_b != ir::INVALID_SLOT) ? 7 : 2;
            const size_t arg2_size = (node.op_c != ir::INVALID_SLOT) ? 7 : 2;
            const size_t arg3_size = (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) ? 7 : 3;
            const size_t result_size = (node.result_id != ir::INVALID_SLOT) ? 7 : 1;
            return arg1_size + arg2_size + arg3_size + 10 + 4 + 5 + 4 + result_size;
        }

        switch (form) {
            case MachineForm::NoOp:
                if (node.opcode == ir::OpCode::LABEL) {
                    size_t lbl_bytes = 0;
                    if ((node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0) {
                        if (node.op_a != ir::INVALID_SLOT) lbl_bytes += 7;
                        if (node.op_b != ir::INVALID_SLOT) lbl_bytes += 7;
                        if (node.op_c != ir::INVALID_SLOT) lbl_bytes += 7;
                        if (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) lbl_bytes += 7;
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
                return is_float_const ? 23 : 17;
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
                    return is64 ? 80 : 40;
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
                const size_t arg1_size = (node.op_b != ir::INVALID_SLOT) ? 7 : 3;
                const size_t arg2_size = (node.op_c != ir::INVALID_SLOT) ? 7 : 3;
                const size_t arg3_size = (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) ? 7 : 3;
                const size_t result_size = (node.result_id != ir::INVALID_SLOT) ? 24 : 1;
                return arg1_size + arg2_size + arg3_size + 24 + 4 + 10 + 4 + result_size + 32;
            }
            case MachineForm::Return:
                return (node.op_a != ir::INVALID_SLOT) ? 14 : 8;
            case MachineForm::Goto:
                return 5;
            case MachineForm::BranchCond:
                return 16;
            case MachineForm::BranchCompare:
                return 20;
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

    [[nodiscard]] static bool inspect_binary_format_from_memory(
        const void* data,
        size_t size,
        dgtl::execution::format::Binary_Format& out_binary) {
        if (data == nullptr || size == 0) return false;
        try {
            dgtl::execution::format::Binary_Loader loader;
            auto bin = loader.load_from_memory(data, size);
            if (!bin) return false;
            out_binary = *bin;
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] static bool build_cached_machine_code(
        dgtl::execution::cache::Code_Cache& cache,
        std::string_view key,
        const ir::IRStream& stream,
        std::vector<uint8_t>& out_bytes) {
        if (!key.empty() && cache.retrieve_copy(key, out_bytes)) {
            return true;
        }
        DGTLLinker linker;
        if (!linker.build_raw_x64_machine_code(stream, out_bytes)) {
            return false;
        }
        if (!key.empty()) {
            (void)cache.store(key, out_bytes.data(), out_bytes.size());
        }
        return true;
    }

    using CoffCallReloc = dgtl::backend::aot::CoffCallReloc;
    using CoffReloc     = dgtl::backend::aot::CoffReloc;
    using MagicMultiplierS64 = dgtl::backend::aot::MagicMultiplierS64;
    using DGTLAOTLinker = dgtl::backend::aot::DGTLAOTLinker;

    bool build_elf_relocatable_binary(const ir::IRStream& stream,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_elf_relocatable_binary(stream, out, entry_symbol);
    }

    bool build_static_archive_binary(const ir::IRStream& stream,
                                     std::vector<uint8_t>& out,
                                     std::string_view member_name = "module.o") {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_static_archive_binary(stream, out, member_name);
    }

    bool build_coff_relocatable_binary_with_relocs(
        const ir::IRStream& stream,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol,
        const std::vector<CoffCallReloc>& call_relocs,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary_with_relocs(
            stream, out, entry_symbol, call_relocs, slot_to_callee, rdata_payload);
    }

    bool build_module_coff_with_relocs(
        const std::vector<std::pair<std::string, ir::IRStream>>& module_streams,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee,
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_module_coff_with_relocs(module_streams, out, slot_to_callee, rdata_payload);
    }

    bool build_coff_relocatable_binary(const ir::IRStream& stream,
                                       std::vector<uint8_t>& out,
                                       std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary(stream, out, entry_symbol);
    }

    bool build_coff_archive_binary(const ir::IRStream& stream,
                                   std::vector<uint8_t>& out,
                                   std::string_view member_name = "module.obj",
                                   std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_coff_archive_binary(stream, out, member_name, entry_symbol);
    }

    bool build_pe_executable_buffer(const ir::IRStream& ir_stream,
                                    std::vector<uint8_t>& pe_image) {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_pe_executable_buffer(ir_stream, pe_image);
    }

    bool build_pe_executable(std::string_view path, const ir::IRStream& ir_stream) {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, ir_stream);
    }

    bool emit_pe_executable(std::string_view path, const ir::IRStream& ir_stream) {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, ir_stream);
    }

    bool emit_aot_machine_code(
        uint8_t* dest,
        size_t dest_capacity,
        const ir::IRStream& stream,
        size_t* emitted_size = nullptr,
        const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
        std::vector<CoffCallReloc>* out_relocs = nullptr,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.emit_aot_machine_code(dest, dest_capacity, stream, emitted_size, slot_map, out_relocs, slot_to_callee);
    }

    [[nodiscard]] const std::vector<std::string>& fallback_diagnostics() const noexcept {
        return fallback_diagnostics_;
    }

    std::vector<CoffCallReloc> collect_call_relocs(
        const ir::IRStream& stream,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        dgtl::backend::aot::DGTLAOTLinker aot;
        return aot.collect_call_relocs(stream, slot_to_callee);
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

    bool emit_machine_code(uint8_t* dest,
                           size_t dest_capacity,
                           const ir::IRStream& stream,
                           size_t* emitted_size = nullptr,
                           const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
                           std::vector<CoffCallReloc>* out_relocs = nullptr,
                           const std::unordered_map<uint32_t, std::string>& slot_to_callee = {}) noexcept {
        if (stream.is_null() || !dest || stream.size() == 0) [[unlikely]] return false;
        clear_fallback_diagnostics();
        const size_t count = stream.size();

        std::unordered_map<uint32_t, uint64_t> known_constants;
        known_constants.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::CONST || node.opcode == ir::OpCode::IMMEDIATE) {
                uint64_t imm = (static_cast<uint64_t>(node.op_b) << 32u) | (static_cast<uint64_t>(node.op_a) & 0xFFFFFFFFu);
                if (imm == 0 && node.intent_hash != 0) {
                    imm = node.intent_hash;
                }
                if (node.result_id != ir::INVALID_SLOT) {
                    known_constants[node.result_id] = imm;
                }
            }
        }

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
                const bool is_func_header = (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                if (!is_func_header) {
                    if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                        label_map[node.op_a] = i;
                    }
                    if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                        label_map[node.op_b] = i;
                    }
                }
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
        auto emit_mov_r9_from_slot  = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x4C, 0x8D, slot_id); };
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
            if (fn_hdr.op_a != ir::INVALID_SLOT) emit_store_rcx_to_slot(fn_hdr.op_a);
            if (fn_hdr.op_b != ir::INVALID_SLOT) emit_store_rdx_to_slot(fn_hdr.op_b);
            if (fn_hdr.op_c != ir::INVALID_SLOT) emit_store_r8_to_slot(fn_hdr.op_c);
            if (fn_hdr.logic_flags != ir::INVALID_SLOT && fn_hdr.logic_flags != 0u) emit_store_r9_to_slot(fn_hdr.logic_flags);
            const auto fn_overflow = stream.get_call_overflow_args(0);
            for (size_t k = 0; k < fn_overflow.size(); ++k) {
                if (fn_overflow[k] != ir::INVALID_SLOT && fn_overflow[k] != 0u) {
                    write_u8(0x48); write_u8(0x8B); write_u8(0x85); write_i32(static_cast<int32_t>(48 + k * 8)); // mov rax, [rbp + 48 + k*8]
                    emit_store_rax_to_slot(fn_overflow[k]);
                }
            }
        }

        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = offset;
            const auto& node = stream.get_node(i);
            using namespace dgtl::isa::lowering;
            const MachineForm form = generated::classify_machine_form(node.opcode);

            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) emit_mov_rcx_from_slot(node.op_a);
                else { write_u8(0x48); write_u8(0x31); write_u8(0xC9); } // xor rcx, rcx
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) emit_mov_rdx_from_slot(node.op_b);
                else { write_u8(0x48); write_u8(0x31); write_u8(0xD2); } // xor rdx, rdx
                if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) emit_mov_r8_from_slot(node.op_c);
                else { write_u8(0x4D); write_u8(0x31); write_u8(0xC0); } // xor r8, r8
                if (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) {
                    emit_mov_reg_from_slot(0x4C, 0x8D, node.logic_flags); // mov r9, [rbp-disp]
                }
                else { write_u8(0x4D); write_u8(0x31); write_u8(0xC9); } // xor r9, r9

                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx

                write_u8(0x48); write_u8(0x83); write_u8(0xEC); write_u8(0x20); // sub rsp, 32

                uintptr_t rt_target_addr = 0;
                if (out_relocs == nullptr) {
                    if (!slot_map.empty()) {
                        auto it = slot_map.find(static_cast<uint32_t>(node.opcode));
                        if (it != slot_map.end()) {
                            rt_target_addr = it->second;
                        }
                    }
                    if (rt_target_addr == 0 && node.intent_hash > 0x7FFF00000000ULL) {
                        rt_target_addr = static_cast<uintptr_t>(node.intent_hash);
                    }
                }

                if (rt_target_addr != 0) {
                    write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(rt_target_addr));
                    write_u8(0xFF); write_u8(0xD0);
                } else {
                    write_u8(0xE8);
                    const size_t reloc_disp_offset = offset;
                    write_u8(0x00); write_u8(0x00); write_u8(0x00); write_u8(0x00);
                    if (out_relocs != nullptr) {
                        const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                        if (symbol != nullptr) {
                            out_relocs->push_back({ static_cast<uint32_t>(reloc_disp_offset), std::string(symbol) });
                        }
                    }
                }
                write_u8(0x48); write_u8(0x83); write_u8(0xC4); write_u8(0x20); // add rsp, 32

                if (node.result_id != ir::INVALID_SLOT) emit_store_rax_to_slot(node.result_id);
                else write_u8(0x90);
                continue;
            }

            switch (form) {
                case MachineForm::NoOp:
                    if (node.opcode == ir::OpCode::LABEL) {
                        const bool is_func_header = (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                        if (is_func_header && i > 0) {
                            if (node.op_a != ir::INVALID_SLOT) emit_store_rcx_to_slot(node.op_a);
                            if (node.op_b != ir::INVALID_SLOT) emit_store_rdx_to_slot(node.op_b);
                            if (node.op_c != ir::INVALID_SLOT) emit_store_r8_to_slot(node.op_c);
                            if (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) emit_store_r9_to_slot(node.logic_flags);
                            const auto fn_overflow = stream.get_call_overflow_args(i);
                            for (size_t k = 0; k < fn_overflow.size(); ++k) {
                                if (fn_overflow[k] != ir::INVALID_SLOT && fn_overflow[k] != 0u) {
                                    write_u8(0x48); write_u8(0x8B); write_u8(0x85); write_i32(static_cast<int32_t>(48 + k * 8));
                                    emit_store_rax_to_slot(fn_overflow[k]);
                                }
                            }
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
                        write_u8(kind == generated::X64IntegerBinary32Kind::Shl ? 0xE0 : 0xF8);
                        emit_store_rax_to_slot(node.result_id);
                    }
                    else if (kind == generated::X64IntegerBinary32Kind::Div || 
                             kind == generated::X64IntegerBinary32Kind::Mod) {
                        auto const_it = (node.op_b != ir::INVALID_SLOT) ? known_constants.find(node.op_b) : known_constants.end();
                        bool has_const_divisor = (const_it != known_constants.end() && const_it->second > 0);
                        int64_t divisor = has_const_divisor ? static_cast<int64_t>(const_it->second) : 0;
                        auto magic = (has_const_divisor && is64) ? dgtl::backend::aot::compute_magic_s64(divisor) : dgtl::backend::aot::MagicMultiplierS64{};

                        if (is64 && has_const_divisor && divisor == 1) {
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_mov_rax_from_slot(node.op_a);
                                emit_store_rax_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0x31); write_u8(0xC0); // xor rax, rax
                                emit_store_rax_to_slot(node.result_id);
                            }
                        }
                        else if (is64 && has_const_divisor && magic.valid && !magic.is_power_of_2) {
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0xB9); write_u64(magic.M);
                            write_u8(0x48); write_u8(0xF7); write_u8(0xE9);
                            if (magic.s > 0) {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xFA); write_u8(magic.s);
                            }
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0xC1); write_u8(0xF8); write_u8(0x3F);
                            write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_store_rdx_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0x89); write_u8(0xD0);
                                write_u8(0x48); write_u8(0xB9); write_u64(static_cast<uint64_t>(divisor));
                                write_u8(0x48); write_u8(0x0F); write_u8(0xAF); write_u8(0xC1);
                                emit_mov_rdx_from_slot(node.op_a);
                                write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                                emit_store_rdx_to_slot(node.result_id);
                            }
                        }
                        else if (is64 && has_const_divisor && magic.is_power_of_2 && divisor > 1) {
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0x99);
                            uint8_t k = magic.s;
                            if (k == 1) {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xEA); write_u8(0x3F);
                            } else if (k < 31) {
                                write_u8(0x48); write_u8(0x81); write_u8(0xE2); write_u32(static_cast<uint32_t>((1ULL << k) - 1));
                            } else {
                                write_u8(0x48); write_u8(0xB9); write_u64((1ULL << k) - 1);
                                write_u8(0x48); write_u8(0x21); write_u8(0xCA);
                            }
                            write_u8(0x48); write_u8(0x01); write_u8(0xD0);
                            write_u8(0x48); write_u8(0xC1); write_u8(0xF8); write_u8(k);
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_store_rax_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xE0); write_u8(k);
                                emit_mov_rdx_from_slot(node.op_a);
                                write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                                emit_store_rdx_to_slot(node.result_id);
                            }
                        }
                        else {
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
                        write_u8(0x48); write_u8(0x31); write_u8(0xC9); // xor rcx, rcx
                    }

                    if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) {
                        emit_mov_rdx_from_slot(node.op_c);
                    } else { 
                        write_u8(0x48); write_u8(0x31); write_u8(0xD2); // xor rdx, rdx
                    }

                    if (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) {
                        emit_mov_r8_from_slot(node.logic_flags);
                    } else { 
                        write_u8(0x4D); write_u8(0x31); write_u8(0xC0); // xor r8, r8
                    }

                    const auto overflow = stream.get_call_overflow_args(i);
                    if (!overflow.empty() && overflow[0] != ir::INVALID_SLOT && overflow[0] != 0u) {
                        emit_mov_r9_from_slot(overflow[0]); // mov r9, [rbp-disp]
                    } else {
                        write_u8(0x4D); write_u8(0x31); write_u8(0xC9); // xor r9, r9
                    }

                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx
                    write_u8(0x66); write_u8(0x49); write_u8(0x0F); write_u8(0x6E); write_u8(0xD0); // movq xmm2, r8
                    write_u8(0x66); write_u8(0x49); write_u8(0x0F); write_u8(0x6E); write_u8(0xD9); // movq xmm3, r9

                    const size_t num_stack_args = (overflow.size() > 1) ? (overflow.size() - 1) : 0;
                    const size_t stack_alloc = 32 + ((num_stack_args + 1) / 2) * 16;
                    write_u8(0x48); write_u8(0x81); write_u8(0xEC); write_u32(static_cast<uint32_t>(stack_alloc)); // sub rsp, stack_alloc

                    for (size_t k = 1; k < overflow.size(); ++k) {
                        if (overflow[k] != ir::INVALID_SLOT && overflow[k] != 0u) {
                            emit_mov_rax_from_slot(overflow[k]);
                            write_u8(0x48); write_u8(0x89); write_u8(0x84); write_u8(0x24);
                            write_i32(static_cast<int32_t>(32 + (k - 1) * 8)); // mov [rsp + 32 + (k-1)*8], rax
                        }
                    }

                    uintptr_t target_addr = 0;
                    if (out_relocs == nullptr) {
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
                    }

                    if (target_addr != 0) {
                        write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(target_addr));
                        write_u8(0xFF); write_u8(0xD0);
                    } else {
                        write_u8(0xE8);
                        const size_t patch_disp_offset = offset;
                        write_u32(0);
                        const size_t patch_source_end = offset;
                        bool is_external_reloc = false;
                        if (out_relocs != nullptr) {
                            auto it = slot_to_callee.find(node.op_a);
                            if (it != slot_to_callee.end() && !it->second.empty()) {
                                out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), it->second });
                                is_external_reloc = true;
                            }
                        }
                        if (!is_external_reloc) {
                            patches.push_back({patch_disp_offset, patch_source_end, resolve_target(node.op_a)});
                        }
                    }
                    write_u8(0x48); write_u8(0x81); write_u8(0xC4); write_u32(static_cast<uint32_t>(stack_alloc)); // add rsp, stack_alloc

                    if (node.result_id != ir::INVALID_SLOT) {
                        bool returns_in_xmm0 = false;
                        auto it_callee = slot_to_callee.find(node.op_a);
                        if (it_callee != slot_to_callee.end() && !it_callee->second.empty()) {
                            const std::string& name = it_callee->second;
                            if (name.find("_bits") != std::string::npos) {
                                returns_in_xmm0 = false;
                            } else if (name == "pow" || name == "sqrt" || name == "sin" ||
                                       name == "cos" || name == "tan" || name == "exp" ||
                                       name == "log" || name == "fabs" || name == "floor" ||
                                       name == "ceil") {
                                returns_in_xmm0 = true;
                            } else {
                                const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                                returns_in_xmm0 = ir::is_floating_type_code(tc);
                            }
                        } else {
                            const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                            returns_in_xmm0 = ir::is_floating_type_code(tc);
                        }

                        if (returns_in_xmm0) {
                            write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x7E); write_u8(0xC0); // movq rax, xmm0
                        }
                        emit_store_rax_to_slot(node.result_id);
                    } else {
                        write_u8(0x90);
                    }
                    continue;
                }

                case MachineForm::Goto: {
                    write_u8(0xE9);
                    const uint32_t target_slot = (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) ? node.op_a : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCond: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x85); write_u8(0xC0); // test rax, rax
                    write_u8(0x0F);
                    write_u8(node.opcode == ir::OpCode::BRANCH_UNLIKELY ? 0x84 : 0x85);
                    const uint32_t target_slot = (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) ? node.op_b : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCompare: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x3B); write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x0F); write_u8(0x85);
                    const uint32_t target_slot = (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) ? node.op_c : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::Return: {
                    if (node.op_a != ir::INVALID_SLOT) {
                        emit_mov_rax_from_slot(node.op_a);
                        write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC0); // movq xmm0, rax (Dual ABI Return)
                    } else {
                        write_u8(0x31); write_u8(0xC0);
                        write_u8(0x66); write_u8(0x0F); write_u8(0xEF); write_u8(0xC0); // pxor xmm0, xmm0
                    }
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

        // Defensive fallthrough safety: guarantee clean leave; ret if last instruction was not Return
        if (count == 0 || stream.get_node(count - 1).opcode != ir::OpCode::RET) {
            write_u8(0x31); write_u8(0xC0); // xor eax, eax
            write_u8(0xC9); write_u8(0xC3); // leave; ret
        }

        node_offsets[count] = offset;
        for (const auto& p : patches) {
            if (p.target_node >= count) {
                continue; // Do not patch external calls; preserve zero addend for external COFF relocations
            }
            const size_t tgt_byte = node_offsets[p.target_node];
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
