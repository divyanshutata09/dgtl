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
    #undef CONST    // Prevents Windows.h CONST macro from corrupting dgtl::linker::mir::MachineLinkOpcode::CONST
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <elf.h>
#endif

export module dgtl.linker.backend;

import dgtl.object.file;
import dgtl.mir.machine_ir;
import dgtl.mir.instruction_selector;
import dgtl.mir.isel_gen;
import dgtl.isa.x64.isel_gen;
import dgtl.isa.isel_dispatch;
import dgtl.isa.wasm.wasm_machine_form_lowering;
import dgtl.isa.wasm.wasm_abi;
import dgtl.isa.wasm.wasm_instruction_info;
import dgtl.isa.wasm.reg_map;
import dgtl.execution.loader;
import dgtl.target;
import dgtl.execution.cache;
import dgtl.execution.dispatcher;
import dgtl.isa.wasm.encoder;
import dgtl.mir.runtime_contract;
import dgtl.linker.driver;

namespace runtime_contract = dgtl::mir::runtime_contract;

using dgtl::mir::MachineForm;
using dgtl::mir::MachineSemanticKind;

namespace generated = dgtl::isa::x64::generated;

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
export namespace dgtl::linker::coff {

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

} // namespace dgtl::linker::coff

export namespace dgtl::linker {

namespace generated = dgtl::isa::lowering::x64::generated;

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

    inline void record_fallback_opcode(size_t node_index, uint16_t opcode) {
        std::string msg;
        msg.reserve(48);
        msg.append("node#").append(std::to_string(node_index))
           .append(" opcode=#").append(std::to_string(opcode));
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

    [[nodiscard]] static inline size_t x64_prologue_size(const machine::MachineFunction& function) noexcept {
        return dgtl::linker::driver::DGTLAOTLinker::x64_prologue_size(function);
    }

    [[nodiscard]] static constexpr size_t x64_prologue_size() noexcept {
        return 25;
    }

public:
    [[nodiscard]] static bool build_wasm_binary(
        const dgtl::machine::MachineFunction& function,
        std::vector<uint8_t>& out,
        std::string& diagnostic)
    {
        out.clear();
        diagnostic.clear();

        // Validate the canonical MIR directly.
        const auto verification =
            dgtl::machine::MachineVerifier::verify(function);

        if (!verification.ok()) {
            diagnostic = "WASM linker rejected invalid MachineFunction";
            return false;
        }

        if (function.instructions().empty()) {
            diagnostic = "WASM linker received an empty MachineFunction";
            return false;
        }

        // The linker must not reconstruct MIR from IR or run ISel again.
        dgtl::exec::DGTLWasmEncoder encoder;
        encoder.encode(function);

        out = encoder.get_binary();

        if (out.empty()) {
            diagnostic = "WASM binary emission produced no output";
            return false;
        }

        return true;
    }

    [[nodiscard]] static bool build_wasm_binary(
        const dgtl::machine::MachineFunction& function,
        std::vector<uint8_t>& out)
    {
        std::string diagnostic;
        return build_wasm_binary(function, out, diagnostic);
    }

    [[nodiscard]] static bool inspect_elf_object_from_memory(
        const void* data,
        size_t size,
        dgtl::execution::loader::Object_Format& out_object) {
        if (data == nullptr || size == 0) {
            return false;
        }
        try {
            dgtl::execution::loader::Object_Loader loader;
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
        dgtl::execution::loader::Binary_Format& out_binary) {
        if (data == nullptr || size == 0) return false;
        try {
            dgtl::execution::loader::Binary_Loader loader;
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
        const machine::MachineFunction& function,
        std::vector<uint8_t>& out_bytes) {
        if (!key.empty() && cache.retrieve_copy(key, out_bytes)) {
            return true;
        }
        DGTLLinker linker;
        if (!linker.build_raw_x64_machine_code(function, out_bytes)) {
            return false;
        }
        if (!key.empty()) {
            (void)cache.store(key, out_bytes.data(), out_bytes.size());
        }
        return true;
    }

    using CoffCallReloc = dgtl::linker::driver::CoffCallReloc;
    using CoffReloc     = dgtl::linker::driver::CoffReloc;
    using MagicMultiplierS64 = dgtl::linker::driver::MagicMultiplierS64;
    using DGTLAOTLinker = dgtl::linker::driver::DGTLAOTLinker;

    bool build_elf_relocatable_binary(const machine::MachineFunction& function,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_elf_relocatable_binary(function, out, entry_symbol);
    }

    bool build_static_archive_binary(const machine::MachineFunction& function,
                                     std::vector<uint8_t>& out,
                                     std::string_view member_name = "module.o") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_static_archive_binary(function, out, member_name);
    }

    bool build_coff_relocatable_binary_with_relocs(
        const machine::MachineFunction& function,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol,
        const std::vector<CoffCallReloc>& call_relocs,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary_with_relocs(
            function, out, entry_symbol, call_relocs, slot_to_callee, rdata_payload);
    }

    bool build_module_coff_with_relocs(
        const std::vector<const machine::MachineFunction*>& module_functions,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_module_coff_with_relocs(module_functions, out, slot_to_callee, rdata_payload);
    }

    bool build_module_coff_with_relocs(
        const std::vector<std::pair<std::string, const machine::MachineFunction*>>& module_streams,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee,
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_module_coff_with_relocs(module_streams, out, slot_to_callee, rdata_payload);
    }

    bool build_coff_relocatable_binary(const machine::MachineFunction& function,
                                       std::vector<uint8_t>& out,
                                       std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary(function, out, entry_symbol);
    }

    bool build_coff_archive_binary(const machine::MachineFunction& function,
                                   std::vector<uint8_t>& out,
                                   std::string_view member_name = "module.obj",
                                   std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_archive_binary(function, out, member_name, entry_symbol);
    }

    bool build_pe_executable_buffer(const machine::MachineFunction& function,
                                    std::vector<uint8_t>& pe_image) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable_buffer(function, pe_image);
    }

    bool build_pe_executable(std::string_view path, const machine::MachineFunction& function) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, function);
    }

    bool emit_pe_executable(std::string_view path, const machine::MachineFunction& function) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, function);
    }

    bool emit_aot_machine_code(
        uint8_t* dest,
        size_t dest_capacity,
        const machine::MachineFunction& function,
        size_t* emitted_size = nullptr,
        const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
        std::vector<CoffCallReloc>* out_relocs = nullptr,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.emit_aot_machine_code(dest, dest_capacity, function, emitted_size, slot_map, out_relocs, slot_to_callee);
    }

    [[nodiscard]] const std::vector<std::string>& fallback_diagnostics() const noexcept {
        return fallback_diagnostics_;
    }

    std::vector<CoffCallReloc> collect_call_relocs(
        const machine::MachineFunction& function,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.collect_call_relocs(function, slot_to_callee);
    }

    bool build_raw_x64_machine_code(const machine::MachineFunction& function,
                                    std::vector<uint8_t>& out_bytes,
                                    const std::unordered_map<uint32_t, uintptr_t>& slot_map = {}) {
        out_bytes.resize(function.instructions().size() * 64 + 1024);
        size_t emitted_size = 0;
        if (!emit_machine_code(out_bytes.data(), out_bytes.size(), function, &emitted_size, slot_map)) {
            return false;
        }
        out_bytes.resize(emitted_size);
        return true;
    }

    bool emit_machine_code(uint8_t* dest,
                           size_t dest_capacity,
                           const machine::MachineFunction& function,
                           size_t* emitted_size = nullptr,
                           const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
                           std::vector<CoffCallReloc>* out_relocs = nullptr,
                           const std::unordered_map<uint32_t, std::string>& slot_to_callee = {}) noexcept {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.emit_aot_machine_code(dest, dest_capacity, function, emitted_size, slot_map, out_relocs, slot_to_callee);
    }
}; // class DGTLLinker

} // namespace dgtl::linker

// ========================================================================================
// BACKWARD COMPATIBILITY ALIASES (dgtl::exec)
// ========================================================================================
export namespace dgtl::exec {
    using DGTLLinker = dgtl::linker::DGTLLinker;
} // namespace dgtl::exec

export namespace dgtl::backend::linker {
    using DGTLLinker = dgtl::linker::DGTLLinker;
}

