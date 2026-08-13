/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Universal Native Binary Assembler Engine (PE/ELF/Mach-O)
 * Purpose:     Direct in-memory section synthesis for PE/COFF, ELF, and Mach-O binaries.
 * Architect:   Staff Level Compiler Architecture Blueprint
 */

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #undef CONST // Neutralize legacy Windows preprocessor macro
#endif

#include "dgtl_sdk/dgtl.h"

namespace {

static dgtl_status_t make_ok() {
    dgtl_status_t s{};
    s.ok = 1;
    s.error_code = 0;
    s.message[0] = '\0';
    return s;
}

static dgtl_status_t make_err(int code, const char* msg) {
    dgtl_status_t s{};
    s.ok = 0;
    s.error_code = code;
    if (msg) {
        std::strncpy(s.message, msg, sizeof(s.message) - 1);
    }
    return s;
}

template <typename HeaderStruct>
inline void write_header_struct(uint8_t* dest, const HeaderStruct& header) noexcept {
    std::memcpy(dest, &header, sizeof(HeaderStruct));
}

// ---------------------------------------------------------------------------
// 1. PE/COFF (Windows x64) Direct In-Memory Section Emission
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct IMAGE_DOS_HEADER_LITE {
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

    [[nodiscard]] bool is_valid_dos_header() const noexcept {
        return e_magic == 0x5A4D && e_lfanew != 0;
    }
};

struct IMAGE_FILE_HEADER_LITE {
    uint16_t Machine;              // 0x8664 = x64
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;

    [[nodiscard]] bool is_x64_machine() const noexcept {
        return Machine == 0x8664;
    }
};

struct IMAGE_DATA_DIRECTORY_LITE {
    uint32_t VirtualAddress;
    uint32_t Size;

    [[nodiscard]] bool is_present() const noexcept {
        return VirtualAddress != 0 && Size != 0;
    }
};

struct IMAGE_OPTIONAL_HEADER64_LITE {
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
    IMAGE_DATA_DIRECTORY_LITE DataDirectory[16];
};

struct IMAGE_SECTION_HEADER_LITE {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;

    [[nodiscard]] bool is_valid_section() const noexcept {
        return VirtualAddress != 0 || SizeOfRawData != 0;
    }
};

struct RUNTIME_FUNCTION_LITE {
    uint32_t BeginAddress;
    uint32_t EndAddress;
    uint32_t UnwindInfoAddress;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// 2. ELF (Linux x86-64 / ARM64) Direct In-Memory Section Emission
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct ELF64_EHDR_LITE {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct ELF64_PHDR_LITE {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// 3. Mach-O (macOS x86-64 / ARM64) Direct In-Memory Section Emission
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct MACH_HEADER_64_LITE {
    uint32_t magic;      // 0xFEEDFACF
    uint32_t cputype;    // 0x01000007 (x64) or 0x0100000C (ARM64)
    uint32_t cpusubtype; // 3 or 0
    uint32_t filetype;   // 2 = MH_EXECUTE
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct SEGMENT_COMMAND_64_LITE {
    uint32_t cmd;        // 0x19 = LC_SEGMENT_64
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct SECTION_64_LITE {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;

    [[nodiscard]] bool is_valid_macho_section() const noexcept {
        return size != 0;
    }
};
#pragma pack(pop)

} // namespace

extern "C" {

DGTL_API uint32_t dgtl_emit_string_const_rodata_rip(dgtl_stream_t* s, const char* str, size_t len) {
    (void)len;
    if (!s || !str) return 0u;
    return dgtl_emit_string_const_utf8(s, str);
}

DGTL_API dgtl_status_t dgtl_emit_pe_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid argument");

    size_t code_bytes = dgtl_stream_node_count(s) * 16 + 64;
    size_t rodata_bytes = 4096;
    size_t pdata_bytes = sizeof(RUNTIME_FUNCTION_LITE);
    
    size_t headers_size = 512;
    size_t total_size = headers_size + code_bytes + rodata_bytes + pdata_bytes;
    
    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buffer) return make_err(2, "allocation failed");
    std::memset(buffer, 0, total_size);

    // Write complete DOS Header end-to-end
    IMAGE_DOS_HEADER_LITE dos{};
    dos.e_magic = 0x5A4D; // "MZ"
    dos.e_cblp = 0x0090;
    dos.e_cp = 0x0003;
    dos.e_crlc = 0x0000;
    dos.e_cparhdr = 0x0004;
    dos.e_minalloc = 0x0000;
    dos.e_maxalloc = 0xFFFF;
    dos.e_ss = 0x0000;
    dos.e_sp = 0x00B8;
    dos.e_csum = 0x0000;
    dos.e_ip = 0x0000;
    dos.e_cs = 0x0000;
    dos.e_lfarlc = 0x0040;
    dos.e_ovno = 0x0000;
    std::memset(dos.e_res, 0, sizeof(dos.e_res));
    dos.e_oemid = 0x0000;
    dos.e_oeminfo = 0x0000;
    std::memset(dos.e_res2, 0, sizeof(dos.e_res2));
    dos.e_lfanew = 128; // Offset to NT headers
    write_header_struct(buffer, dos);

    // Write NT Signature
    uint32_t pe_sig = 0x00004550; // "PE\0\0"
    write_header_struct(buffer + 128, pe_sig);

    // File Header
    IMAGE_FILE_HEADER_LITE file_hdr{};
    file_hdr.Machine = 0x8664; // AMD64
    file_hdr.NumberOfSections = 3; // .text, .rodata, .pdata
    file_hdr.TimeDateStamp = 0x69A12345;
    file_hdr.PointerToSymbolTable = 0;
    file_hdr.NumberOfSymbols = 0;
    file_hdr.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64_LITE);
    file_hdr.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    write_header_struct(buffer + 132, file_hdr);

    // Optional Header
    IMAGE_OPTIONAL_HEADER64_LITE opt{};
    opt.Magic = 0x020B; // PE32+
    opt.MajorLinkerVersion = 14;
    opt.MinorLinkerVersion = 0;
    opt.SizeOfCode = static_cast<uint32_t>(code_bytes);
    opt.SizeOfInitializedData = static_cast<uint32_t>(rodata_bytes + pdata_bytes);
    opt.SizeOfUninitializedData = 0;
    opt.AddressOfEntryPoint = 0x1000;
    opt.BaseOfCode = 0x1000;
    opt.ImageBase = 0x0000000140000000ULL;
    opt.SectionAlignment = 0x1000;
    opt.FileAlignment = 0x200;
    opt.MajorOperatingSystemVersion = 6;
    opt.MinorOperatingSystemVersion = 0;
    opt.MajorImageVersion = 0;
    opt.MinorImageVersion = 0;
    opt.MajorSubsystemVersion = 6;
    opt.MinorSubsystemVersion = 0;
    opt.Win32VersionValue = 0;
    opt.SizeOfImage = static_cast<uint32_t>(headers_size + code_bytes + rodata_bytes + pdata_bytes);
    opt.SizeOfHeaders = static_cast<uint32_t>(headers_size);
    opt.CheckSum = 0;
    opt.Subsystem = 3; // CUI
    opt.DllCharacteristics = 0x0160; // HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT
    opt.SizeOfStackReserve = 0x100000;
    opt.SizeOfStackCommit = 0x1000;
    opt.SizeOfHeapReserve = 0x100000;
    opt.SizeOfHeapCommit = 0x1000;
    opt.LoaderFlags = 0;
    opt.NumberOfRvaAndSizes = 16;
    write_header_struct(buffer + 152, opt);

    // Section Headers
    IMAGE_SECTION_HEADER_LITE text_sec{};
    std::memcpy(text_sec.Name, ".text\0\0\0", 8);
    text_sec.VirtualAddress = 0x1000;
    text_sec.VirtualSize = static_cast<uint32_t>(code_bytes);
    text_sec.PointerToRawData = 0x200;
    text_sec.SizeOfRawData = static_cast<uint32_t>((code_bytes + 511) & ~511);
    text_sec.PointerToRelocations = 0;
    text_sec.PointerToLinenumbers = 0;
    text_sec.NumberOfRelocations = 0;
    text_sec.NumberOfLinenumbers = 0;
    text_sec.Characteristics = 0x60000020; // CODE | EXECUTE | READ
    write_header_struct(buffer + 152 + sizeof(IMAGE_OPTIONAL_HEADER64_LITE), text_sec);

    IMAGE_SECTION_HEADER_LITE rodata_sec{};
    std::memcpy(rodata_sec.Name, ".rodata\0", 8);
    rodata_sec.VirtualAddress = 0x2000;
    rodata_sec.VirtualSize = static_cast<uint32_t>(rodata_bytes);
    rodata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;
    rodata_sec.SizeOfRawData = 0x200;
    rodata_sec.PointerToRelocations = 0;
    rodata_sec.PointerToLinenumbers = 0;
    rodata_sec.NumberOfRelocations = 0;
    rodata_sec.NumberOfLinenumbers = 0;
    rodata_sec.Characteristics = 0x40000040; // INITIALIZED_DATA | READ
    write_header_struct(buffer + 152 + sizeof(IMAGE_OPTIONAL_HEADER64_LITE) + sizeof(IMAGE_SECTION_HEADER_LITE), rodata_sec);

    IMAGE_SECTION_HEADER_LITE pdata_sec{};
    std::memcpy(pdata_sec.Name, ".pdata\0\0", 8);
    pdata_sec.VirtualAddress = 0x3000;
    pdata_sec.VirtualSize = static_cast<uint32_t>(pdata_bytes);
    pdata_sec.PointerToRawData = rodata_sec.PointerToRawData + rodata_sec.SizeOfRawData;
    pdata_sec.SizeOfRawData = 0x200;
    pdata_sec.PointerToRelocations = 0;
    pdata_sec.PointerToLinenumbers = 0;
    pdata_sec.NumberOfRelocations = 0;
    pdata_sec.NumberOfLinenumbers = 0;
    pdata_sec.Characteristics = 0x40000040; // INITIALIZED_DATA | READ
    write_header_struct(buffer + 152 + sizeof(IMAGE_OPTIONAL_HEADER64_LITE) + sizeof(IMAGE_SECTION_HEADER_LITE) * 2, pdata_sec);

    *out_buf = buffer;
    *out_size = total_size;
    return make_ok();
}

DGTL_API dgtl_status_t dgtl_emit_elf_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid argument");

    size_t code_bytes = dgtl_stream_node_count(s) * 16 + 64;
    size_t total_size = sizeof(ELF64_EHDR_LITE) + sizeof(ELF64_PHDR_LITE) * 2 + code_bytes + 4096;

    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buffer) return make_err(2, "allocation failed");
    std::memset(buffer, 0, total_size);

    ELF64_EHDR_LITE ehdr{};
    ehdr.e_ident[0] = 0x7F;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; // 64-bit
    ehdr.e_ident[5] = 1; // Little endian
    ehdr.e_ident[6] = 1; // ELF version
    std::memset(&ehdr.e_ident[7], 0, 9);
    ehdr.e_type = 2;     // ET_EXEC
    ehdr.e_machine = 0x3E; // x86-64
    ehdr.e_version = 1;
    ehdr.e_entry = 0x400000 + sizeof(ELF64_EHDR_LITE) + sizeof(ELF64_PHDR_LITE) * 2;
    ehdr.e_phoff = sizeof(ELF64_EHDR_LITE);
    ehdr.e_shoff = 0;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(ELF64_EHDR_LITE);
    ehdr.e_phentsize = sizeof(ELF64_PHDR_LITE);
    ehdr.e_phnum = 2;
    ehdr.e_shentsize = 64;
    ehdr.e_shnum = 0;
    ehdr.e_shstrndx = 0;
    write_header_struct(buffer, ehdr);

    ELF64_PHDR_LITE phdr_text{};
    phdr_text.p_type = 1; // PT_LOAD
    phdr_text.p_flags = 5; // PF_R | PF_X
    phdr_text.p_offset = 0;
    phdr_text.p_vaddr = 0x400000;
    phdr_text.p_paddr = 0x400000;
    phdr_text.p_filesz = total_size;
    phdr_text.p_memsz = total_size;
    phdr_text.p_align = 0x1000;
    write_header_struct(buffer + sizeof(ELF64_EHDR_LITE), phdr_text);

    *out_buf = buffer;
    *out_size = total_size;
    return make_ok();
}

DGTL_API dgtl_status_t dgtl_emit_macho_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid argument");

    size_t code_bytes = dgtl_stream_node_count(s) * 16 + 64;
    size_t total_size = sizeof(MACH_HEADER_64_LITE) + sizeof(SEGMENT_COMMAND_64_LITE) * 2 + sizeof(SECTION_64_LITE) + code_bytes + 4096;

    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buffer) return make_err(2, "allocation failed");
    std::memset(buffer, 0, total_size);

    MACH_HEADER_64_LITE hdr{};
    hdr.magic = 0xFEEDFACF; // MH_MAGIC_64
    hdr.cputype = 0x01000007; // CPU_TYPE_X86_64
    hdr.cpusubtype = 3;
    hdr.filetype = 2; // MH_EXECUTE
    hdr.ncmds = 2;
    hdr.sizeofcmds = sizeof(SEGMENT_COMMAND_64_LITE) * 2 + sizeof(SECTION_64_LITE);
    hdr.flags = 0x00200085;
    hdr.reserved = 0;
    write_header_struct(buffer, hdr);

    SEGMENT_COMMAND_64_LITE seg_text{};
    seg_text.cmd = 0x19; // LC_SEGMENT_64
    seg_text.cmdsize = sizeof(SEGMENT_COMMAND_64_LITE) + sizeof(SECTION_64_LITE);
    std::memcpy(seg_text.segname, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    seg_text.vmaddr = 0x100000000ULL;
    seg_text.vmsize = 0x1000;
    seg_text.fileoff = 0;
    seg_text.filesize = total_size;
    seg_text.maxprot = 7;
    seg_text.initprot = 5;
    seg_text.nsects = 1;
    seg_text.flags = 0;
    write_header_struct(buffer + sizeof(MACH_HEADER_64_LITE), seg_text);

    SECTION_64_LITE sec_text{};
    std::memcpy(sec_text.sectname, "__text\0\0\0\0\0\0\0\0\0\0", 16);
    std::memcpy(sec_text.segname, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    sec_text.addr = 0x100000000ULL;
    sec_text.size = static_cast<uint64_t>(code_bytes);
    sec_text.offset = static_cast<uint32_t>(sizeof(MACH_HEADER_64_LITE) + sizeof(SEGMENT_COMMAND_64_LITE) * 2 + sizeof(SECTION_64_LITE));
    sec_text.align = 4;
    sec_text.reloff = 0;
    sec_text.nreloc = 0;
    sec_text.flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    sec_text.reserved1 = 0;
    sec_text.reserved2 = 0;
    sec_text.reserved3 = 0;
    write_header_struct(buffer + sizeof(MACH_HEADER_64_LITE) + sizeof(SEGMENT_COMMAND_64_LITE), sec_text);

    *out_buf = buffer;
    *out_size = total_size;
    return make_ok();
}

DGTL_API void dgtl_free_binary_buffer(void* buf) {
    if (buf) {
        std::free(buf);
    }
}

static std::unordered_map<uint32_t, std::string>& get_global_string_handle_registry() {
    static std::unordered_map<uint32_t, std::string> registry;
    return registry;
}

static std::mutex& get_global_string_handle_registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

DGTL_API void dgtl_register_string_handle(uint32_t handle, const char* data, size_t len) {
    if (!data || len == 0) return;
    std::lock_guard<std::mutex> lock(get_global_string_handle_registry_mutex());
    get_global_string_handle_registry()[handle] = std::string(data, len);
}

DGTL_API const char* dgtl_try_lookup_string_handle(uint32_t handle, size_t* out_len) {
    std::lock_guard<std::mutex> lock(get_global_string_handle_registry_mutex());
    auto& reg = get_global_string_handle_registry();
    auto it = reg.find(handle);
    if (it == reg.end()) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    if (out_len) *out_len = it->second.size();
    return it->second.c_str();
}

} // extern "C"