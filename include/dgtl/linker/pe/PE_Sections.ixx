/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>

export module dgtl.linker.pe.sections;

export namespace dgtl::linker::pe {

struct SectionDescriptor {
    const char* name;
    uint64_t alignment;
    uint32_t flags;
};

inline constexpr uint32_t IMAGE_SCN_CNT_CODE               = 0x00000020;
inline constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA   = 0x00000040;
inline constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
inline constexpr uint32_t IMAGE_SCN_MEM_EXECUTE            = 0x20000000;
inline constexpr uint32_t IMAGE_SCN_MEM_READ               = 0x40000000;
inline constexpr uint32_t IMAGE_SCN_MEM_WRITE              = 0x80000000;
inline constexpr uint32_t IMAGE_SCN_ALIGN_16BYTES          = 0x00500000;

inline constexpr uint32_t SCN_CNT_CODE                     = IMAGE_SCN_CNT_CODE;
inline constexpr uint32_t SCN_CNT_INITIALIZED_DATA         = IMAGE_SCN_CNT_INITIALIZED_DATA;
inline constexpr uint32_t SCN_MEM_EXECUTE                  = IMAGE_SCN_MEM_EXECUTE;
inline constexpr uint32_t SCN_MEM_READ                     = IMAGE_SCN_MEM_READ;
inline constexpr uint32_t SCN_ALIGN_16BYTES                = IMAGE_SCN_ALIGN_16BYTES;

inline constexpr uint16_t IMAGE_FILE_MACHINE_AMD64         = 0x8664;
inline constexpr uint16_t MACHINE_AMD64                    = 0x8664;

inline constexpr uint8_t  IMAGE_SYM_CLASS_EXTERNAL         = 2;
inline constexpr uint8_t  IMAGE_SYM_CLASS_STATIC           = 3;
inline constexpr uint8_t  SYM_CLASS_EXTERNAL               = 2;
inline constexpr uint8_t  SYM_CLASS_STATIC                 = 3;

inline constexpr uint16_t IMAGE_SYM_DTYPE_FUNCTION         = 0x0020;
inline constexpr uint16_t SYM_DTYPE_FUNCTION               = 0x0020;
inline constexpr uint16_t SYM_TYPE_FUNCTION                = 0x0020;

inline constexpr uint16_t IMAGE_REL_AMD64_ABSOLUTE         = 0x0000;
inline constexpr uint16_t IMAGE_REL_AMD64_ADDR64           = 0x0001;
inline constexpr uint16_t IMAGE_REL_AMD64_ADDR32           = 0x0002;
inline constexpr uint16_t IMAGE_REL_AMD64_REL32            = 0x0004;
inline constexpr uint16_t REL_AMD64_REL32                  = 0x0004;

#pragma pack(push, 1)
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
using DosHeader = ImageDosHeader;

struct FileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct ImageDataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};
using DataDirectory = ImageDataDirectory;

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
    union {
        ImageDataDirectory DataDirectory[16];
        ImageDataDirectory DataDirectories[16];
    };
};
using OptionalHeader64 = ImageOptionalHeader64;

struct SectionHeader {
    union {
        char     Name[8];
        uint8_t  NameBytes[8];
    };
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

struct Relocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
};

struct Symbol {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeroes;
            uint32_t Offset;
        } LongName;
    } N;
    uint32_t Value;
    int16_t  SectionNumber;
    uint16_t Type;
    uint8_t  StorageClass;
    uint8_t  NumberOfAuxSymbols;
};

struct ImportDescriptor {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
};
#pragma pack(pop)

} // namespace dgtl::linker::pe

export namespace dgtl::linker::coff {
    using namespace dgtl::linker::pe;
}

export namespace dgtl::backend::linker::pe {
    using namespace dgtl::linker::pe;
} // namespace dgtl::backend::linker::pe



