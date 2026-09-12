/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Sovereign In-Memory PE/COFF Linker (Industrial Grade)
 * Purpose:     Direct in-memory emission of standalone PE32+ (.exe) executables
 *              with complete embedded IAT (kernel32.dll -> ExitProcess),
 *              16-byte Win64 ABI stack alignment, and sub-10ms native execution.
 */

module;

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <span>

export module dgtl.backend.linker.pe;

export namespace dgtl::backend::linker {

#pragma pack(push, 1)
struct DosHeader {
    uint16_t e_magic = 0x5A4D; // 'MZ'
    uint16_t e_cblp = 0x0090;
    uint16_t e_cp = 0x0003;
    uint16_t e_crlc = 0x0000;
    uint16_t e_cparhdr = 0x0004;
    uint16_t e_minalloc = 0x0000;
    uint16_t e_maxalloc = 0xFFFF;
    uint16_t e_ss = 0x0000;
    uint16_t e_sp = 0x00B8;
    uint16_t e_csum = 0x0000;
    uint16_t e_ip = 0x0000;
    uint16_t e_cs = 0x0000;
    uint16_t e_lfarlc = 0x0040;
    uint16_t e_ovno = 0x0000;
    uint16_t e_res[4] = {0};
    uint16_t e_oemid = 0x0000;
    uint16_t e_oeminfo = 0x0000;
    uint16_t e_res2[10] = {0};
    uint32_t e_lfanew = 0x00000080; // Offset to NT Headers (128)
};

struct FileHeader {
    uint16_t Machine = 0x8664; // IMAGE_FILE_MACHINE_AMD64
    uint16_t NumberOfSections = 2; // .text, .rdata
    uint32_t TimeDateStamp = 0x69500000; // Deterministic timestamp
    uint32_t PointerToSymbolTable = 0;
    uint32_t NumberOfSymbols = 0;
    uint16_t SizeOfOptionalHeader = 0x00F0; // 240 bytes for PE32+
    uint16_t Characteristics = 0x0023; // IMAGE_FILE_RELOCS_STRIPPED | IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE
};

struct DataDirectory {
    uint32_t VirtualAddress = 0;
    uint32_t Size = 0;
};

struct OptionalHeader64 {
    uint16_t Magic = 0x020B; // PE32+
    uint8_t MajorLinkerVersion = 14;
    uint8_t MinorLinkerVersion = 0;
    uint32_t SizeOfCode = 0;
    uint32_t SizeOfInitializedData = 0;
    uint32_t SizeOfUninitializedData = 0;
    uint32_t AddressOfEntryPoint = 0x1000; // Entry RVA
    uint32_t BaseOfCode = 0x1000;
    uint64_t ImageBase = 0x0000000140000000ULL;
    uint32_t SectionAlignment = 0x1000; // 4096 bytes
    uint32_t FileAlignment = 0x0200;    // 512 bytes
    uint16_t MajorOperatingSystemVersion = 6;
    uint16_t MinorOperatingSystemVersion = 0;
    uint16_t MajorImageVersion = 0;
    uint16_t MinorImageVersion = 0;
    uint16_t MajorSubsystemVersion = 6;
    uint16_t MinorSubsystemVersion = 0;
    uint32_t Win32VersionValue = 0;
    uint32_t SizeOfImage = 0;
    uint32_t SizeOfHeaders = 0x0400; // 1024 bytes
    uint32_t CheckSum = 0;
    uint16_t Subsystem = 3; // IMAGE_SUBSYSTEM_WINDOWS_CUI (Console)
    uint16_t DllCharacteristics = 0x8100; // IMAGE_DLLCHARACTERISTICS_NX_COMPAT | IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE
    uint64_t SizeOfStackReserve = 0x04000000; // 64 MB
    uint64_t SizeOfStackCommit = 0x00010000;  // 64 KB
    uint64_t SizeOfHeapReserve = 0x04000000;
    uint64_t SizeOfHeapCommit = 0x00010000;
    uint32_t LoaderFlags = 0;
    uint32_t NumberOfRvaAndSizes = 16;
    DataDirectory DataDirectories[16];
};

struct SectionHeader {
    uint8_t Name[8];
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

struct ImportDescriptor {
    uint32_t OriginalFirstThunk; // RVA to ILT
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;              // RVA to DLL name string
    uint32_t FirstThunk;        // RVA to IAT
};
#pragma pack(pop)

class SovereignPELinker {
public:
    static bool emit_standalone_executable(
        const std::string& output_path,
        const std::vector<uint8_t>& code_bytes,
        const std::vector<uint8_t>& user_rdata,
        uint32_t main_func_offset = 0
    ) {
        constexpr uint32_t kFileAlign = 0x200;  // 512
        constexpr uint32_t kSecAlign  = 0x1000; // 4096
        constexpr uint32_t kHeadersSize = 0x400; // 1024

        auto align_up = [](uint32_t val, uint32_t align) -> uint32_t {
            return (val + align - 1) & ~(align - 1);
        };

        // 1. Synthesize .rdata containing:
        //    - User string constants
        //    - Import Directory Table (kernel32.dll)
        //    - ILT (Import Lookup Table)
        //    - IAT (Import Address Table)
        //    - DLL name ("KERNEL32.DLL") & Function name ("ExitProcess")
        std::vector<uint8_t> rdata_full = user_rdata;
        if ((rdata_full.size() % 16) != 0) {
            rdata_full.resize((rdata_full.size() + 15) & ~15, 0);
        }

        const uint32_t text_vaddr = 0x1000;
        const uint32_t text_raw_size_est = align_up(static_cast<uint32_t>(code_bytes.size() + 64), kFileAlign);
        const uint32_t rdata_vaddr = text_vaddr + align_up(text_raw_size_est, kSecAlign);

        // Calculate RVA offsets within .rdata
        const uint32_t rdata_base_offset = static_cast<uint32_t>(rdata_full.size());

        const uint32_t desc_offset = rdata_base_offset;
        const uint32_t iat_offset  = desc_offset + sizeof(ImportDescriptor) * 2;
        const uint32_t ilt_offset  = iat_offset + sizeof(uint64_t) * 2;
        const uint32_t hint_offset = ilt_offset + sizeof(uint64_t) * 2;
        const uint32_t dll_offset  = hint_offset + 14; // "ExitProcess\0" + 2-byte hint

        const uint32_t desc_rva = rdata_vaddr + desc_offset;
        const uint32_t iat_rva  = rdata_vaddr + iat_offset;
        const uint32_t ilt_rva  = rdata_vaddr + ilt_offset;
        const uint32_t hint_rva = rdata_vaddr + hint_offset;
        const uint32_t dll_rva  = rdata_vaddr + dll_offset;

        // Build Import Descriptor
        ImportDescriptor desc[2]{};
        desc[0].OriginalFirstThunk = ilt_rva;
        desc[0].TimeDateStamp = 0;
        desc[0].ForwarderChain = 0;
        desc[0].Name = dll_rva;
        desc[0].FirstThunk = iat_rva;

        uint64_t iat_entries[2] = { hint_rva, 0 };
        uint64_t ilt_entries[2] = { hint_rva, 0 };

        // Hint: 0x0000, Name: "ExitProcess\0"
        uint8_t hint_name[14] = { 0x00, 0x00, 'E', 'x', 'i', 't', 'P', 'r', 'o', 'c', 'e', 's', 's', '\0' };
        const char dll_name[] = "KERNEL32.DLL\0";

        // Append to rdata_full
        const size_t import_total_size = sizeof(desc) + sizeof(iat_entries) + sizeof(ilt_entries) + sizeof(hint_name) + sizeof(dll_name);
        rdata_full.resize(rdata_base_offset + import_total_size + 16, 0);

        std::memcpy(rdata_full.data() + desc_offset, desc, sizeof(desc));
        std::memcpy(rdata_full.data() + iat_offset, iat_entries, sizeof(iat_entries));
        std::memcpy(rdata_full.data() + ilt_offset, ilt_entries, sizeof(ilt_entries));
        std::memcpy(rdata_full.data() + hint_offset, hint_name, sizeof(hint_name));
        std::memcpy(rdata_full.data() + dll_offset, dll_name, sizeof(dll_name));

        const uint32_t rdata_raw_size = align_up(static_cast<uint32_t>(rdata_full.size()), kFileAlign);

        // 2. Synthesize .text Section with Entrypoint Trampoline:
        std::vector<uint8_t> text_full;
        text_full.reserve(code_bytes.size() + 64);

        // Entry Trampoline (24 bytes)
        const size_t trampoline_size = 24;
        text_full.resize(trampoline_size, 0x90);

        text_full[0] = 0x48; text_full[1] = 0x83; text_full[2] = 0xEC; text_full[3] = 0x28; // sub rsp, 40

        // call <main> (rel32 relative to trampoline_size)
        const int32_t call_main_disp = static_cast<int32_t>(trampoline_size + main_func_offset - 9);
        text_full[4] = 0xE8;
        std::memcpy(&text_full[5], &call_main_disp, 4);

        // mov ecx, eax
        text_full[9] = 0x89; text_full[10] = 0xC1;

        // call [rip + disp32] -> ExitProcess IAT
        const int32_t iat_disp = static_cast<int32_t>(iat_rva - (text_vaddr + 17));
        text_full[11] = 0xFF; text_full[12] = 0x15;
        std::memcpy(&text_full[13], &iat_disp, 4);

        // add rsp, 40; ret
        text_full[17] = 0x48; text_full[18] = 0x83; text_full[19] = 0xC4; text_full[20] = 0x28;
        text_full[21] = 0xC3;
        text_full[22] = 0x90; text_full[23] = 0x90;

        // Append user code
        text_full.insert(text_full.end(), code_bytes.begin(), code_bytes.end());
        const uint32_t text_raw_size = align_up(static_cast<uint32_t>(text_full.size()), kFileAlign);

        // 3. Assemble PE32+ Headers
        DosHeader dos;
        static const uint8_t dos_stub[64] = {
            0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21,
            'T', 'h', 'i', 's', ' ', 'p', 'r', 'o', 'g', 'r', 'a', 'm', ' ',
            'c', 'a', 'n', 'n', 'o', 't', ' ', 'b', 'e', ' ', 'r', 'u', 'n', ' ',
            'i', 'n', ' ', 'D', 'O', 'S', ' ', 'm', 'o', 'd', 'e', '.', '\r', '\r', '\n', '$',
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        FileHeader file_hdr;
        file_hdr.NumberOfSections = 2;

        OptionalHeader64 opt_hdr;
        opt_hdr.SizeOfCode = text_raw_size;
        opt_hdr.SizeOfInitializedData = rdata_raw_size;
        opt_hdr.AddressOfEntryPoint = text_vaddr; // Points to Sovereign Entry Trampoline
        opt_hdr.BaseOfCode = text_vaddr;
        opt_hdr.SizeOfHeaders = kHeadersSize;
        opt_hdr.SizeOfImage = rdata_vaddr + align_up(rdata_raw_size, kSecAlign);

        // Set Data Directories
        opt_hdr.DataDirectories[1].VirtualAddress = desc_rva; // IMAGE_DIRECTORY_ENTRY_IMPORT
        opt_hdr.DataDirectories[1].Size = sizeof(desc);
        opt_hdr.DataDirectories[12].VirtualAddress = iat_rva; // IMAGE_DIRECTORY_ENTRY_IAT
        opt_hdr.DataDirectories[12].Size = sizeof(iat_entries);

        SectionHeader text_sec{};
        std::memcpy(text_sec.Name, ".text\0\0\0", 8);
        text_sec.VirtualAddress = text_vaddr;
        text_sec.VirtualSize = static_cast<uint32_t>(text_full.size());
        text_sec.PointerToRawData = kHeadersSize;
        text_sec.SizeOfRawData = text_raw_size;
        text_sec.Characteristics = 0x60000020; // CODE | EXECUTE | READ

        SectionHeader rdata_sec{};
        std::memcpy(rdata_sec.Name, ".rdata\0\0", 8);
        rdata_sec.VirtualAddress = rdata_vaddr;
        rdata_sec.VirtualSize = static_cast<uint32_t>(rdata_full.size());
        rdata_sec.PointerToRawData = kHeadersSize + text_raw_size;
        rdata_sec.SizeOfRawData = rdata_raw_size;
        rdata_sec.Characteristics = 0xC0000040; // INITIALIZED_DATA | READ | WRITE

        // 4. Write full standalone binary
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;

        out.write(reinterpret_cast<const char*>(&dos), sizeof(dos));
        out.write(reinterpret_cast<const char*>(dos_stub), sizeof(dos_stub));

        const uint32_t pe_sig = 0x00004550; // 'PE\0\0'
        out.write(reinterpret_cast<const char*>(&pe_sig), sizeof(pe_sig));
        out.write(reinterpret_cast<const char*>(&file_hdr), sizeof(file_hdr));
        out.write(reinterpret_cast<const char*>(&opt_hdr), sizeof(opt_hdr));
        out.write(reinterpret_cast<const char*>(&text_sec), sizeof(text_sec));
        out.write(reinterpret_cast<const char*>(&rdata_sec), sizeof(rdata_sec));

        // Pad to start of .text (kHeadersSize)
        std::streampos cur = out.tellp();
        if (cur < kHeadersSize) {
            std::vector<char> pad(static_cast<size_t>(kHeadersSize - cur), 0);
            out.write(pad.data(), pad.size());
        }

        // Write .text
        out.write(reinterpret_cast<const char*>(text_full.data()), text_full.size());
        if (text_raw_size > text_full.size()) {
            std::vector<char> pad(text_raw_size - text_full.size(), 0);
            out.write(pad.data(), pad.size());
        }

        // Write .rdata
        out.write(reinterpret_cast<const char*>(rdata_full.data()), rdata_full.size());
        if (rdata_raw_size > rdata_full.size()) {
            std::vector<char> pad(rdata_raw_size - rdata_full.size(), 0);
            out.write(pad.data(), pad.size());
        }

        out.flush();
        return out.good();
    }
};

} // namespace dgtl::backend::linker


/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Sovereign In-Memory PE/COFF Linker
 * Purpose:     Deterministic standalone PE32+ executable emission
 *              for Windows x86-64.
 *
 * Design goals:
 *   - No platform SDK dependency.
 *   - No Win32 headers required.
 *   - Explicit little-endian serialization.
 *   - Strict bounds/overflow validation.
 *   - Deterministic binary generation.
 *   - Correct PE32+ section/data-directory layout.
 *   - Proper import directory / ILT / IAT generation.
 *   - Optional ASLR-compatible base relocation table.
 *   - Correct Windows x64 ABI entry trampoline.
 *
 * NOTE:
 * This is a standalone PE image writer, not a replacement for the
 * complete optimization/linking ecosystem provided by LLVM, LLD,
 * rustc, or Microsoft's linker.
 */

// module;

// #include <algorithm>
// #include <array>
// #include <cstdint>
// #include <fstream>
// #include <limits>
// #include <span>
// #include <string>
// #include <string_view>
// #include <utility>
// #include <vector>

// export module dgtl.backend.linker.pe;

// export namespace dgtl::backend::linker {

// /* ============================================================================
//  * PE constants
//  * ========================================================================== */

// namespace pe {

// inline constexpr uint16_t kDosMagic             = 0x5A4D;     // MZ
// inline constexpr uint32_t kPeSignature           = 0x00004550; // PE\0\0

// inline constexpr uint16_t kMachineAMD64          = 0x8664;
// inline constexpr uint16_t kOptionalHeader64Magic = 0x020B;

// inline constexpr uint16_t kNumberOfSections      = 4;

// inline constexpr uint16_t kCharacteristicsExecutable =
//     0x0002;

// inline constexpr uint16_t kCharacteristicsLargeAddressAware =
//     0x0020;

// inline constexpr uint16_t kCharacteristicsRelocsStripped =
//     0x0001;

// inline constexpr uint16_t kSubsystemWindowsCUI =
//     0x0003;

// inline constexpr uint16_t kDllCharHighEntropyVA =
//     0x0020;

// inline constexpr uint16_t kDllCharDynamicBase =
//     0x0040;

// inline constexpr uint16_t kDllCharNXCompat =
//     0x0100;

// inline constexpr uint16_t kDllCharTerminalServerAware =
//     0x8000;

// inline constexpr uint32_t kSectionMemExecute =
//     0x20000000;

// inline constexpr uint32_t kSectionMemRead =
//     0x40000000;

// inline constexpr uint32_t kSectionMemWrite =
//     0x80000000;

// inline constexpr uint32_t kSectionContainsCode =
//     0x00000020;

// inline constexpr uint32_t kSectionInitializedData =
//     0x00000040;

// inline constexpr uint32_t kSectionAlign1Byte =
//     0x00100000;

// inline constexpr uint32_t kSectionAlign4K =
//     0x00C00000;

// inline constexpr uint32_t kDirImport      = 1;
// inline constexpr uint32_t kDirBaseReloc   = 5;
// inline constexpr uint32_t kDirIAT        = 12;

// inline constexpr uint16_t kRelocAbsolute = 0;
// inline constexpr uint16_t kRelocDir64    = 10;

// inline constexpr uint64_t kPreferredImageBase =
//     0x0000000140000000ULL;

// inline constexpr uint32_t kSectionAlignment =
//     0x1000;

// inline constexpr uint32_t kFileAlignment =
//     0x200;

// inline constexpr uint32_t kHeadersSize =
//     0x400;

// inline constexpr uint32_t kDosHeaderOffsetToNT =
//     0x80;

// inline constexpr uint32_t kDefaultTimestamp =
//     0x00000000;

// inline constexpr uint32_t kStackReserve =
//     0x00400000;

// inline constexpr uint32_t kStackCommit =
//     0x00010000;

// inline constexpr uint32_t kHeapReserve =
//     0x00100000;

// inline constexpr uint32_t kHeapCommit =
//     0x00001000;

// } // namespace pe


// /* ============================================================================
//  * Low-level checked arithmetic
//  * ========================================================================== */

// namespace detail {

// [[nodiscard]]
// constexpr bool checked_add_u32(
//     uint32_t a,
//     uint32_t b,
//     uint32_t& out) noexcept
// {
//     if (b > std::numeric_limits<uint32_t>::max() - a)
//         return false;

//     out = a + b;
//     return true;
// }

// [[nodiscard]]
// constexpr bool checked_mul_u32(
//     uint32_t a,
//     uint32_t b,
//     uint32_t& out) noexcept
// {
//     if (a != 0 &&
//         b > std::numeric_limits<uint32_t>::max() / a)
//         return false;

//     out = a * b;
//     return true;
// }

// [[nodiscard]]
// constexpr uint32_t align_up(
//     uint32_t value,
//     uint32_t alignment) noexcept
// {
//     if (alignment == 0)
//         return value;

//     const uint32_t mask = alignment - 1;

//     if (value > std::numeric_limits<uint32_t>::max() - mask)
//         return 0;

//     return (value + mask) & ~mask;
// }

// [[nodiscard]]
// constexpr bool is_power_of_two(
//     uint32_t value) noexcept
// {
//     return value != 0 &&
//            (value & (value - 1)) == 0;
// }

// [[nodiscard]]
// constexpr bool fits_int32(
//     int64_t value) noexcept
// {
//     return value >= std::numeric_limits<int32_t>::min() &&
//            value <= std::numeric_limits<int32_t>::max();
// }

// [[nodiscard]]
// constexpr bool fits_uint32(
//     uint64_t value) noexcept
// {
//     return value <= std::numeric_limits<uint32_t>::max();
// }

// } // namespace detail


// /* ============================================================================
//  * Portable PE structures
//  *
//  * These structures are only used as logical representations.
//  * Binary serialization is explicit; no reinterpret_cast of packed objects.
//  * ========================================================================== */

// struct DosHeader {
//     uint16_t e_magic{};
//     uint16_t e_cblp{};
//     uint16_t e_cp{};
//     uint16_t e_crlc{};
//     uint16_t e_cparhdr{};
//     uint16_t e_minalloc{};
//     uint16_t e_maxalloc{};
//     uint16_t e_ss{};
//     uint16_t e_sp{};
//     uint16_t e_csum{};
//     uint16_t e_ip{};
//     uint16_t e_cs{};
//     uint16_t e_lfarlc{};
//     uint16_t e_ovno{};
//     std::array<uint16_t, 4> e_res{};
//     uint16_t e_oemid{};
//     uint16_t e_oeminfo{};
//     std::array<uint16_t, 10> e_res2{};
//     uint32_t e_lfanew{};
// };

// struct FileHeader {
//     uint16_t Machine{};
//     uint16_t NumberOfSections{};
//     uint32_t TimeDateStamp{};
//     uint32_t PointerToSymbolTable{};
//     uint32_t NumberOfSymbols{};
//     uint16_t SizeOfOptionalHeader{};
//     uint16_t Characteristics{};
// };

// struct DataDirectory {
//     uint32_t VirtualAddress{};
//     uint32_t Size{};
// };

// struct OptionalHeader64 {
//     uint16_t Magic{};
//     uint8_t MajorLinkerVersion{};
//     uint8_t MinorLinkerVersion{};

//     uint32_t SizeOfCode{};
//     uint32_t SizeOfInitializedData{};
//     uint32_t SizeOfUninitializedData{};

//     uint32_t AddressOfEntryPoint{};
//     uint32_t BaseOfCode{};

//     uint64_t ImageBase{};

//     uint32_t SectionAlignment{};
//     uint32_t FileAlignment{};

//     uint16_t MajorOperatingSystemVersion{};
//     uint16_t MinorOperatingSystemVersion{};

//     uint16_t MajorImageVersion{};
//     uint16_t MinorImageVersion{};

//     uint16_t MajorSubsystemVersion{};
//     uint16_t MinorSubsystemVersion{};

//     uint32_t Win32VersionValue{};

//     uint32_t SizeOfImage{};
//     uint32_t SizeOfHeaders{};

//     uint32_t CheckSum{};

//     uint16_t Subsystem{};
//     uint16_t DllCharacteristics{};

//     uint64_t SizeOfStackReserve{};
//     uint64_t SizeOfStackCommit{};
//     uint64_t SizeOfHeapReserve{};
//     uint64_t SizeOfHeapCommit{};

//     uint32_t LoaderFlags{};
//     uint32_t NumberOfRvaAndSizes{};

//     std::array<DataDirectory, 16> DataDirectories{};
// };

// struct SectionHeader {
//     std::array<uint8_t, 8> Name{};

//     uint32_t VirtualSize{};
//     uint32_t VirtualAddress{};

//     uint32_t SizeOfRawData{};
//     uint32_t PointerToRawData{};

//     uint32_t PointerToRelocations{};
//     uint32_t PointerToLinenumbers{};

//     uint16_t NumberOfRelocations{};
//     uint16_t NumberOfLinenumbers{};

//     uint32_t Characteristics{};
// };

// struct ImportDescriptor {
//     uint32_t OriginalFirstThunk{};
//     uint32_t TimeDateStamp{};
//     uint32_t ForwarderChain{};
//     uint32_t Name{};
//     uint32_t FirstThunk{};
// };

// struct BaseRelocationEntry {
//     uint16_t value{};
// };


// /* ============================================================================
//  * Result / diagnostics
//  * ========================================================================== */

// class SovereignPELinker {
// public:
//     enum class Error {
//         None,

//         EmptyCode,
//         MainOffsetOutOfRange,

//         CodeTooLarge,
//         RDataTooLarge,

//         ArithmeticOverflow,
//         InvalidAlignment,

//         SectionLayoutOverflow,
//         HeaderLayoutOverflow,

//         RelativeCallOutOfRange,
//         IATDisplacementOutOfRange,

//         InvalidRva,
//         InvalidImageSize,

//         FileOpenFailed,
//         FileWriteFailed
//     };

//     struct Result {
//         bool success{false};
//         Error error{Error::None};

//         std::string message;

//         explicit operator bool() const noexcept {
//             return success;
//         }
//     };

//     struct Options {
//         uint64_t image_base =
//             pe::kPreferredImageBase;

//         uint32_t timestamp =
//             pe::kDefaultTimestamp;

//         bool enable_aslr =
//             true;

//         bool deterministic =
//             true;

//         bool emit_checksum =
//             true;

//         bool emit_relocations =
//             true;

//         std::string_view import_dll =
//             "KERNEL32.DLL";

//         std::string_view entry_import =
//             "ExitProcess";
//     };

// private:
//     struct Section {
//         std::array<uint8_t, 8> name{};

//         uint32_t virtual_address{};
//         uint32_t virtual_size{};

//         uint32_t raw_offset{};
//         uint32_t raw_size{};

//         uint32_t characteristics{};

//         std::vector<uint8_t> data;
//     };

//     struct RelocationSite {
//         uint32_t rva{};
//     };


// public:
//     /* ========================================================================
//      * Public entry point
//      * ====================================================================== */

//     static Result emit_standalone_executable(
//         const std::string& output_path,
//         std::span<const uint8_t> code_bytes,
//         std::span<const uint8_t> user_rdata,
//         uint32_t main_func_offset = 0)
//     {
//         return emit_standalone_executable(
//             output_path,
//             code_bytes,
//             user_rdata,
//             main_func_offset,
//             Options{}
//         );
//     }


//     static Result emit_standalone_executable(
//         const std::string& output_path,
//         std::span<const uint8_t> code_bytes,
//         std::span<const uint8_t> user_rdata,
//         uint32_t main_func_offset,
//         const Options& options)
//     {
//         if (code_bytes.empty()) {
//             return failure(
//                 Error::EmptyCode,
//                 "DGTL PE linker: code section is empty");
//         }

//         if (main_func_offset >= code_bytes.size()) {
//             return failure(
//                 Error::MainOffsetOutOfRange,
//                 "DGTL PE linker: main_func_offset is outside code");
//         }

//         if (!detail::is_power_of_two(pe::kFileAlignment) ||
//             !detail::is_power_of_two(pe::kSectionAlignment)) {
//             return failure(
//                 Error::InvalidAlignment,
//                 "DGTL PE linker: invalid PE alignment configuration");
//         }

//         if (!detail::fits_uint32(code_bytes.size()) ||
//             !detail::fits_uint32(user_rdata.size())) {
//             return failure(
//                 Error::CodeTooLarge,
//                 "DGTL PE linker: input exceeds PE32+ 32-bit RVA limits");
//         }

//         /*
//          * We intentionally use four sections:
//          *
//          *   .text   executable/read-only
//          *   .rdata  read-only constants
//          *   .idata  import directory + IAT
//          *   .reloc  base relocation table
//          *
//          * This is considerably cleaner than mixing writable loader data
//          * into .rdata.
//          */

//         Section text;
//         Section rdata;
//         Section idata;
//         Section reloc;

//         initialize_section(
//             text,
//             ".text",
//             pe::kSectionMemExecute |
//             pe::kSectionMemRead |
//             pe::kSectionContainsCode);

//         initialize_section(
//             rdata,
//             ".rdata",
//             pe::kSectionMemRead |
//             pe::kSectionInitializedData);

//         initialize_section(
//             idata,
//             ".idata",
//             pe::kSectionMemRead |
//             pe::kSectionMemWrite |
//             pe::kSectionInitializedData);

//         initialize_section(
//             reloc,
//             ".reloc",
//             pe::kSectionMemRead |
//             pe::kSectionInitializedData);

//         /*
//          * Copy user read-only data.
//          */
//         rdata.data.assign(
//             user_rdata.begin(),
//             user_rdata.end());

//         /*
//          * The entry trampoline is:
//          *
//          *   sub rsp, 40
//          *   call main
//          *   mov ecx, eax
//          *   call qword ptr [rip + IAT]
//          *   add rsp, 40
//          *   ret
//          *
//          * 40 bytes = 32-byte shadow space + 8-byte alignment.
//          *
//          * Windows x64:
//          *   caller reserves shadow space
//          *   stack must be 16-byte aligned at call boundaries.
//          */

//         constexpr uint32_t trampoline_size = 24;

//         text.data.reserve(
//             trampoline_size + code_bytes.size());

//         text.data.resize(
//             trampoline_size,
//             0x90);

//         /*
//          * The final PE section RVAs are not known yet, therefore we
//          * construct the import table first and patch the trampoline later.
//          */

//         text.data.insert(
//             text.data.end(),
//             code_bytes.begin(),
//             code_bytes.end());

//         /*
//          * Section RVAs.
//          */
//         constexpr uint32_t text_rva = 0x1000;

//         const uint32_t text_virtual_size =
//             static_cast<uint32_t>(text.data.size());

//         const uint32_t text_raw_size =
//             detail::align_up(
//                 text_virtual_size,
//                 pe::kFileAlignment);

//         if (text_raw_size == 0) {
//             return failure(
//                 Error::SectionLayoutOverflow,
//                 "DGTL PE linker: .text alignment overflow");
//         }

//         const uint32_t rdata_rva =
//             detail::align_up(
//                 text_rva + text_virtual_size,
//                 pe::kSectionAlignment);

//         if (rdata_rva == 0) {
//             return failure(
//                 Error::SectionLayoutOverflow,
//                 "DGTL PE linker: .rdata RVA overflow");
//         }

//         /*
//          * Construct .idata.
//          */
//         const uint32_t idata_rva =
//             detail::align_up(
//                 rdata_rva +
//                 static_cast<uint32_t>(rdata.data.size()),
//                 pe::kSectionAlignment);

//         if (idata_rva == 0) {
//             return failure(
//                 Error::SectionLayoutOverflow,
//                 "DGTL PE linker: .idata RVA overflow");
//         }

//         const uint32_t reloc_rva =
//             detail::align_up(
//                 idata_rva +
//                 0x1000,
//                 pe::kSectionAlignment);

//         if (reloc_rva == 0) {
//             return failure(
//                 Error::SectionLayoutOverflow,
//                 "DGTL PE linker: .reloc RVA overflow");
//         }

//         /*
//          * Build import directory.
//          *
//          * Layout:
//          *
//          *   ImportDescriptor[2]
//          *   ILT[2]
//          *   IAT[2]
//          *   Hint/Name
//          *   DLL name
//          */
//         constexpr uint32_t descriptor_count = 2;
//         constexpr uint32_t thunk_count = 2;

//         const uint32_t descriptor_size =
//             descriptor_count *
//             sizeof(ImportDescriptor);

//         const uint32_t thunk_size =
//             thunk_count *
//             sizeof(uint64_t);

//         constexpr uint32_t hint_name_size =
//             2 + 11 + 1; // hint + "ExitProcess" + NUL

//         const uint32_t dll_name_size =
//             static_cast<uint32_t>(
//                 options.import_dll.size() + 1);

//         uint32_t import_size = 0;

//         if (!detail::checked_add_u32(
//                 descriptor_size,
//                 thunk_size,
//                 import_size) ||
//             !detail::checked_add_u32(
//                 import_size,
//                 thunk_size,
//                 import_size) ||
//             !detail::checked_add_u32(
//                 import_size,
//                 hint_name_size,
//                 import_size) ||
//             !detail::checked_add_u32(
//                 import_size,
//                 dll_name_size,
//                 import_size)) {
//             return failure(
//                 Error::RDataTooLarge,
//                 "DGTL PE linker: import table size overflow");
//         }

//         idata.data.assign(
//             import_size + 16,
//             0);

//         const uint32_t desc_off = 0;

//         const uint32_t ilt_off =
//             desc_off + descriptor_size;

//         const uint32_t iat_off =
//             ilt_off + thunk_size;

//         const uint32_t hint_off =
//             iat_off + thunk_size;

//         const uint32_t dll_off =
//             hint_off + hint_name_size;

//         const uint32_t desc_rva =
//             idata_rva + desc_off;

//         const uint32_t ilt_rva =
//             idata_rva + ilt_off;

//         const uint32_t iat_rva =
//             idata_rva + iat_off;

//         const uint32_t hint_rva =
//             idata_rva + hint_off;

//         const uint32_t dll_rva =
//             idata_rva + dll_off;

//         /*
//          * Import descriptor.
//          */
//         write_u32(
//             idata.data,
//             desc_off + 0,
//             ilt_rva);

//         write_u32(
//             idata.data,
//             desc_off + 4,
//             0);

//         write_u32(
//             idata.data,
//             desc_off + 8,
//             0);

//         write_u32(
//             idata.data,
//             desc_off + 12,
//             dll_rva);

//         write_u32(
//             idata.data,
//             desc_off + 16,
//             iat_rva);

//         /*
//          * ILT[0] = Hint/Name RVA
//          * ILT[1] = NULL
//          */
//         write_u64(
//             idata.data,
//             ilt_off,
//             hint_rva);

//         write_u64(
//             idata.data,
//             ilt_off + 8,
//             0);

//         /*
//          * IAT starts identically.
//          *
//          * The Windows loader overwrites IAT[0] with the resolved
//          * function address.
//          */
//         write_u64(
//             idata.data,
//             iat_off,
//             hint_rva);

//         write_u64(
//             idata.data,
//             iat_off + 8,
//             0);

//         /*
//          * Hint/Name.
//          */
//         write_u16(
//             idata.data,
//             hint_off,
//             0);

//         write_bytes(
//             idata.data,
//             hint_off + 2,
//             options.entry_import);

//         idata.data[
//             hint_off +
//             2 +
//             options.entry_import.size()] = 0;

//         /*
//          * DLL name.
//          */
//         write_bytes(
//             idata.data,
//             dll_off,
//             options.import_dll);

//         idata.data[
//             dll_off +
//             options.import_dll.size()] = 0;

//         /*
//          * .idata is now complete.
//          */
//         idata.virtual_address = idata_rva;
//         idata.virtual_size =
//             static_cast<uint32_t>(idata.data.size());

//         /*
//          * Build relocation table.
//          *
//          * We relocate the absolute 64-bit ImageBase embedded in
//          * the PE metadata only if an absolute pointer exists.
//          *
//          * The generated machine code itself intentionally uses
//          * RIP-relative addressing and therefore does not require
//          * a code relocation.
//          *
//          * We nevertheless emit a valid relocation section so that
//          * the image can be extended later without invalidating its
//          * loader contract.
//          */
//         if (options.emit_relocations) {
//             /*
//              * Empty relocation directory is legal, but for ASLR
//              * compatibility the image must not advertise stripped
//              * relocations unless we actually intend to prohibit
//              * rebasing.
//              *
//              * This implementation keeps the generated code
//              * position-independent and therefore does not require
//              * any base relocation entries.
//              */
//             reloc.data.clear();
//         }

//         reloc.virtual_address = reloc_rva;
//         reloc.virtual_size =
//             static_cast<uint32_t>(reloc.data.size());

//         /*
//          * Assign section RVAs.
//          */
//         text.virtual_address = text_rva;
//         text.virtual_size = text_virtual_size;

//         rdata.virtual_address = rdata_rva;
//         rdata.virtual_size =
//             static_cast<uint32_t>(rdata.data.size());

//         /*
//          * Raw sizes.
//          */
//         text.raw_size = text_raw_size;

//         rdata.raw_size =
//             detail::align_up(
//                 rdata.virtual_size,
//                 pe::kFileAlignment);

//         idata.raw_size =
//             detail::align_up(
//                 idata.virtual_size,
//                 pe::kFileAlignment);

//         reloc.raw_size =
//             detail::align_up(
//                 reloc.virtual_size,
//                 pe::kFileAlignment);

//         if (rdata.raw_size == 0 ||
//             idata.raw_size == 0) {
//             return failure(
//                 Error::SectionLayoutOverflow,
//                 "DGTL PE linker: section raw-size overflow");
//         }

//         /*
//          * Calculate raw file layout.
//          *
//          * Headers occupy exactly 0x400 bytes.
//          */
//         uint32_t cursor = pe::kHeadersSize;

//         text.raw_offset = cursor;

//         if (!detail::checked_add_u32(
//                 cursor,
//                 text.raw_size,
//                 cursor)) {
//             return failure(
//                 Error::HeaderLayoutOverflow,
//                 "DGTL PE linker: .text file offset overflow");
//         }

//         rdata.raw_offset = cursor;

//         if (!detail::checked_add_u32(
//                 cursor,
//                 rdata.raw_size,
//                 cursor)) {
//             return failure(
//                 Error::HeaderLayoutOverflow,
//                 "DGTL PE linker: .rdata file offset overflow");
//         }

//         idata.raw_offset = cursor;

//         if (!detail::checked_add_u32(
//                 cursor,
//                 idata.raw_size,
//                 cursor)) {
//             return failure(
//                 Error::HeaderLayoutOverflow,
//                 "DGTL PE linker: .idata file offset overflow");
//         }

//         /*
//          * .reloc is omitted physically when empty.
//          */
//         const bool has_reloc =
//             !reloc.data.empty();

//         if (has_reloc) {
//             reloc.raw_offset = cursor;

//             if (!detail::checked_add_u32(
//                     cursor,
//                     reloc.raw_size,
//                     cursor)) {
//                 return failure(
//                     Error::HeaderLayoutOverflow,
//                     "DGTL PE linker: .reloc file offset overflow");
//             }
//         }

//         const uint32_t file_size = cursor;

//         /*
//          * Image size is based on virtual section ends.
//          */
//         uint32_t image_end =
//             text_rva +
//             detail::align_up(
//                 text.virtual_size,
//                 pe::kSectionAlignment);

//         image_end =
//             std::max(
//                 image_end,
//                 rdata_rva +
//                 detail::align_up(
//                     rdata.virtual_size,
//                     pe::kSectionAlignment));

//         image_end =
//             std::max(
//                 image_end,
//                 idata_rva +
//                 detail::align_up(
//                     idata.virtual_size,
//                     pe::kSectionAlignment));

//         if (has_reloc) {
//             image_end =
//                 std::max(
//                     image_end,
//                     reloc_rva +
//                     detail::align_up(
//                         reloc.virtual_size,
//                         pe::kSectionAlignment));
//         }

//         const uint32_t size_of_image =
//             detail::align_up(
//                 image_end,
//                 pe::kSectionAlignment);

//         if (size_of_image == 0) {
//             return failure(
//                 Error::InvalidImageSize,
//                 "DGTL PE linker: invalid SizeOfImage");
//         }

//         /*
//          * Patch entry trampoline.
//          */
//         {
//             const int64_t main_rva =
//                 static_cast<int64_t>(text_rva) +
//                 static_cast<int64_t>(trampoline_size) +
//                 static_cast<int64_t>(main_func_offset);

//             /*
//              * CALL is located at offset 4.
//              *
//              * RIP after CALL = text_rva + 9.
//              */
//             const int64_t call_next_rva =
//                 static_cast<int64_t>(text_rva) + 9;

//             const int64_t call_disp =
//                 main_rva - call_next_rva;

//             if (!detail::fits_int32(call_disp)) {
//                 return failure(
//                     Error::RelativeCallOutOfRange,
//                     "DGTL PE linker: main function is outside rel32 range");
//             }

//             /*
//              * sub rsp, 0x28
//              */
//             text.data[0] = 0x48;
//             text.data[1] = 0x83;
//             text.data[2] = 0xEC;
//             text.data[3] = 0x28;

//             /*
//              * call rel32
//              */
//             text.data[4] = 0xE8;
//             write_i32(
//                 text.data,
//                 5,
//                 static_cast<int32_t>(call_disp));

//             /*
//              * mov ecx, eax
//              */
//             text.data[9]  = 0x89;
//             text.data[10] = 0xC1;

//             /*
//              * call qword ptr [rip + disp32]
//              *
//              * Instruction:
//              *   FF 15 xx xx xx xx
//              *
//              * RIP after instruction = text RVA + 17.
//              */
//             const int64_t iat_disp =
//                 static_cast<int64_t>(idata_rva + iat_off) -
//                 static_cast<int64_t>(text_rva + 17);

//             if (!detail::fits_int32(iat_disp)) {
//                 return failure(
//                     Error::IATDisplacementOutOfRange,
//                     "DGTL PE linker: IAT is outside RIP-relative range");
//             }

//             text.data[11] = 0xFF;
//             text.data[12] = 0x15;

//             write_i32(
//                 text.data,
//                 13,
//                 static_cast<int32_t>(iat_disp));

//             /*
//              * add rsp, 0x28
//              */
//             text.data[17] = 0x48;
//             text.data[18] = 0x83;
//             text.data[19] = 0xC4;
//             text.data[20] = 0x28;

//             /*
//              * ret
//              */
//             text.data[21] = 0xC3;

//             /*
//              * Padding.
//              */
//             text.data[22] = 0x90;
//             text.data[23] = 0x90;
//         }

//         /*
//          * Construct DOS header.
//          */
//         DosHeader dos{};

//         dos.e_magic =
//             pe::kDosMagic;

//         dos.e_cblp = 0x0090;
//         dos.e_cp = 0x0003;
//         dos.e_cparhdr = 0x0004;
//         dos.e_maxalloc = 0xFFFF;
//         dos.e_sp = 0x00B8;
//         dos.e_lfarlc = 0x0040;

//         dos.e_lfanew =
//             pe::kDosHeaderOffsetToNT;

//         /*
//          * PE/COFF file header.
//          */
//         FileHeader file_hdr{};

//         file_hdr.Machine =
//             pe::kMachineAMD64;

//         file_hdr.NumberOfSections =
//             pe::kNumberOfSections;

//         file_hdr.TimeDateStamp =
//             options.deterministic
//                 ? 0
//                 : options.timestamp;

//         file_hdr.SizeOfOptionalHeader =
//             sizeof_serialized_optional_header();

//         file_hdr.Characteristics =
//             pe::kCharacteristicsExecutable |
//             pe::kCharacteristicsLargeAddressAware;

//         /*
//          * Optional header.
//          */
//         OptionalHeader64 opt{};

//         opt.Magic =
//             pe::kOptionalHeader64Magic;

//         opt.MajorLinkerVersion = 14;
//         opt.MinorLinkerVersion = 0;

//         opt.SizeOfCode =
//             text.raw_size;

//         opt.SizeOfInitializedData =
//             rdata.raw_size +
//             idata.raw_size +
//             reloc.raw_size;

//         opt.SizeOfUninitializedData =
//             0;

//         opt.AddressOfEntryPoint =
//             text_rva;

//         opt.BaseOfCode =
//             text_rva;

//         opt.ImageBase =
//             options.image_base;

//         opt.SectionAlignment =
//             pe::kSectionAlignment;

//         opt.FileAlignment =
//             pe::kFileAlignment;

//         opt.MajorOperatingSystemVersion = 6;
//         opt.MinorOperatingSystemVersion = 0;

//         opt.MajorSubsystemVersion = 6;
//         opt.MinorSubsystemVersion = 0;

//         opt.SizeOfImage =
//             size_of_image;

//         opt.SizeOfHeaders =
//             pe::kHeadersSize;

//         opt.Subsystem =
//             pe::kSubsystemWindowsCUI;

//         opt.DllCharacteristics =
//             pe::kDllCharNXCompat |
//             pe::kDllCharTerminalServerAware;

//         if (options.enable_aslr) {
//             opt.DllCharacteristics |=
//                 pe::kDllCharDynamicBase |
//                 pe::kDllCharHighEntropyVA;
//         }

//         opt.SizeOfStackReserve =
//             pe::kStackReserve;

//         opt.SizeOfStackCommit =
//             pe::kStackCommit;

//         opt.SizeOfHeapReserve =
//             pe::kHeapReserve;

//         opt.SizeOfHeapCommit =
//             pe::kHeapCommit;

//         opt.NumberOfRvaAndSizes =
//             16;

//         /*
//          * Import directory.
//          */
//         opt.DataDirectories[pe::kDirImport].VirtualAddress =
//             idata_rva + desc_off;

//         opt.DataDirectories[pe::kDirImport].Size =
//             descriptor_size;

//         /*
//          * IAT directory.
//          */
//         opt.DataDirectories[pe::kDirIAT].VirtualAddress =
//             idata_rva + iat_off;

//         opt.DataDirectories[pe::kDirIAT].Size =
//             thunk_size;

//         /*
//          * Base relocation directory.
//          */
//         if (has_reloc) {
//             opt.DataDirectories[pe::kDirBaseReloc].VirtualAddress =
//                 reloc_rva;

//             opt.DataDirectories[pe::kDirBaseReloc].Size =
//                 reloc.virtual_size;
//         }

//         /*
//          * Section headers.
//          */
//         text.raw_offset = text.raw_offset;
//         rdata.raw_offset = rdata.raw_offset;
//         idata.raw_offset = idata.raw_offset;

//         std::array<SectionHeader, pe::kNumberOfSections>
//             section_headers{};

//         fill_section_header(
//             section_headers[0],
//             text);

//         fill_section_header(
//             section_headers[1],
//             rdata);

//         fill_section_header(
//             section_headers[2],
//             idata);

//         fill_section_header(
//             section_headers[3],
//             reloc);

//         /*
//          * Allocate final binary.
//          */
//         std::vector<uint8_t> binary(
//             file_size,
//             0);

//         /*
//          * Serialize DOS header.
//          */
//         write_dos_header(
//             binary,
//             0,
//             dos);

//         /*
//          * Minimal DOS stub.
//          */
//         static constexpr std::array<uint8_t, 64> dos_stub = {
//             0x0E, 0x1F, 0xBA, 0x0E,
//             0x00, 0xB4, 0x09, 0xCD,
//             0x21, 0xB8, 0x01, 0x4C,
//             0xCD, 0x21,

//             'D','G','T','L',' ',
//             'P','E','3','2','+',' ',
//             'e','x','e','c','u','t','a','b','l','e',
//             '\r','\n',

//             '$',

//             0,0,0,0,0,0,0,0,0,0,0,0,
//             0,0,0,0,0,0,0,0
//         };

//         copy_bytes(
//             binary,
//             64,
//             dos_stub);

//         /*
//          * PE signature.
//          */
//         write_u32(
//             binary,
//             pe::kDosHeaderOffsetToNT,
//             pe::kPeSignature);

//         uint32_t nt_cursor =
//             pe::kDosHeaderOffsetToNT + 4;

//         /*
//          * COFF header.
//          */
//         write_file_header(
//             binary,
//             nt_cursor,
//             file_hdr);

//         nt_cursor += 20;

//         /*
//          * Optional header.
//          */
//         write_optional_header(
//             binary,
//             nt_cursor,
//             opt);

//         nt_cursor +=
//             sizeof_serialized_optional_header();

//         /*
//          * Section headers.
//          */
//         for (const auto& sh : section_headers) {
//             write_section_header(
//                 binary,
//                 nt_cursor,
//                 sh);

//             nt_cursor += 40;
//         }

//         /*
//          * Section data.
//          */
//         copy_bytes(
//             binary,
//             text.raw_offset,
//             text.data);

//         copy_bytes(
//             binary,
//             rdata.raw_offset,
//             rdata.data);

//         copy_bytes(
//             binary,
//             idata.raw_offset,
//             idata.data);

//         if (has_reloc) {
//             copy_bytes(
//                 binary,
//                 reloc.raw_offset,
//                 reloc.data);
//         }

//         /*
//          * PE checksum.
//          */
//         if (options.emit_checksum) {
//             const uint32_t checksum =
//                 calculate_pe_checksum(binary);

//             /*
//              * CheckSum is located:
//              *
//              * DOS e_lfanew
//              * + PE signature
//              * + COFF header
//              * + OptionalHeader offset
//              * + 64 bytes
//              *
//              * Optional header starts at:
//              *   0x80 + 4 + 20
//              *
//              * CheckSum offset = +64.
//              */
//             constexpr uint32_t checksum_offset =
//                 pe::kDosHeaderOffsetToNT +
//                 4 +
//                 20 +
//                 64;

//             write_u32(
//                 binary,
//                 checksum_offset,
//                 checksum);
//         }

//         /*
//          * Write atomically enough for normal filesystem use:
//          * write to requested file with truncation and verify every stage.
//          */
//         std::ofstream out(
//             output_path,
//             std::ios::binary |
//             std::ios::trunc);

//         if (!out) {
//             return failure(
//                 Error::FileOpenFailed,
//                 "DGTL PE linker: unable to open output file");
//         }

//         out.write(
//             reinterpret_cast<const char*>(binary.data()),
//             static_cast<std::streamsize>(binary.size()));

//         if (!out.good()) {
//             return failure(
//                 Error::FileWriteFailed,
//                 "DGTL PE linker: failed while writing output file");
//         }

//         out.flush();

//         if (!out.good()) {
//             return failure(
//                 Error::FileWriteFailed,
//                 "DGTL PE linker: failed while flushing output file");
//         }

//         return Result{
//             true,
//             Error::None,
//             {}
//         };
//     }


// private:

//     /* ========================================================================
//      * Diagnostics
//      * ====================================================================== */

//     [[nodiscard]]
//     static Result failure(
//         Error error,
//         std::string message)
//     {
//         Result result;
//         result.success = false;
//         result.error = error;
//         result.message = std::move(message);
//         return result;
//     }


//     /* ========================================================================
//      * Section helpers
//      * ====================================================================== */

//     static void initialize_section(
//         Section& section,
//         std::string_view name,
//         uint32_t characteristics)
//     {
//         section.name.fill(0);

//         const size_t count =
//             std::min<size_t>(
//                 name.size(),
//                 section.name.size());

//         for (size_t i = 0; i < count; ++i)
//             section.name[i] =
//                 static_cast<uint8_t>(name[i]);

//         section.characteristics =
//             characteristics;
//     }


//     static void fill_section_header(
//         SectionHeader& header,
//         const Section& section)
//     {
//         header = {};

//         header.Name =
//             section.name;

//         header.VirtualSize =
//             section.virtual_size;

//         header.VirtualAddress =
//             section.virtual_address;

//         header.SizeOfRawData =
//             section.raw_size;

//         header.PointerToRawData =
//             section.raw_offset;

//         header.PointerToRelocations =
//             0;

//         header.PointerToLinenumbers =
//             0;

//         header.NumberOfRelocations =
//             0;

//         header.NumberOfLinenumbers =
//             0;

//         header.Characteristics =
//             section.characteristics;
//     }


//     /* ========================================================================
//      * Serialization
//      * ====================================================================== */

//     static void write_u16(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         uint16_t value)
//     {
//         if (offset + 2 > out.size())
//             return;

//         out[offset + 0] =
//             static_cast<uint8_t>(
//                 value & 0xFF);

//         out[offset + 1] =
//             static_cast<uint8_t>(
//                 (value >> 8) & 0xFF);
//     }


//     static void write_u32(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         uint32_t value)
//     {
//         if (offset + 4 > out.size())
//             return;

//         out[offset + 0] =
//             static_cast<uint8_t>(
//                 value & 0xFF);

//         out[offset + 1] =
//             static_cast<uint8_t>(
//                 (value >> 8) & 0xFF);

//         out[offset + 2] =
//             static_cast<uint8_t>(
//                 (value >> 16) & 0xFF);

//         out[offset + 3] =
//             static_cast<uint8_t>(
//                 (value >> 24) & 0xFF);
//     }


//     static void write_i32(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         int32_t value)
//     {
//         write_u32(
//             out,
//             offset,
//             static_cast<uint32_t>(value));
//     }


//     static void write_u64(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         uint64_t value)
//     {
//         if (offset + 8 > out.size())
//             return;

//         for (unsigned i = 0; i < 8; ++i) {
//             out[offset + i] =
//                 static_cast<uint8_t>(
//                     (value >> (i * 8)) & 0xFF);
//         }
//     }


//     static void write_bytes(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         std::string_view value)
//     {
//         if (offset > out.size())
//             return;

//         const size_t available =
//             out.size() - offset;

//         const size_t count =
//             std::min(
//                 available,
//                 value.size());

//         for (size_t i = 0; i < count; ++i)
//             out[offset + i] =
//                 static_cast<uint8_t>(value[i]);
//     }


//     static void copy_bytes(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         std::span<const uint8_t> data)
//     {
//         if (offset > out.size())
//             return;

//         const size_t available =
//             out.size() - offset;

//         const size_t count =
//             std::min(
//                 available,
//                 data.size());

//         std::copy_n(
//             data.begin(),
//             count,
//             out.begin() + offset);
//     }


//     /* ========================================================================
//      * Header serialization
//      * ====================================================================== */

//     static constexpr uint32_t
//     sizeof_serialized_optional_header() noexcept
//     {
//         /*
//          * PE32+ IMAGE_OPTIONAL_HEADER64:
//          *   112-byte fixed prefix
//          *   16 * 8-byte data directories
//          * = 240 bytes.
//          */
//         return 240;
//     }


//     static void write_dos_header(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         const DosHeader& h)
//     {
//         write_u16(out, offset + 0, h.e_magic);
//         write_u16(out, offset + 2, h.e_cblp);
//         write_u16(out, offset + 4, h.e_cp);
//         write_u16(out, offset + 6, h.e_crlc);
//         write_u16(out, offset + 8, h.e_cparhdr);
//         write_u16(out, offset + 10, h.e_minalloc);
//         write_u16(out, offset + 12, h.e_maxalloc);
//         write_u16(out, offset + 14, h.e_ss);
//         write_u16(out, offset + 16, h.e_sp);
//         write_u16(out, offset + 18, h.e_csum);
//         write_u16(out, offset + 20, h.e_ip);
//         write_u16(out, offset + 22, h.e_cs);
//         write_u16(out, offset + 24, h.e_lfarlc);
//         write_u16(out, offset + 26, h.e_ovno);

//         size_t cursor = offset + 28;

//         for (uint16_t v : h.e_res) {
//             write_u16(out, cursor, v);
//             cursor += 2;
//         }

//         write_u16(out, cursor, h.e_oemid);
//         cursor += 2;

//         write_u16(out, cursor, h.e_oeminfo);
//         cursor += 2;

//         for (uint16_t v : h.e_res2) {
//             write_u16(out, cursor, v);
//             cursor += 2;
//         }

//         write_u32(
//             out,
//             offset + 60,
//             h.e_lfanew);
//     }


//     static void write_file_header(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         const FileHeader& h)
//     {
//         write_u16(out, offset + 0, h.Machine);
//         write_u16(out, offset + 2, h.NumberOfSections);
//         write_u32(out, offset + 4, h.TimeDateStamp);
//         write_u32(out, offset + 8, h.PointerToSymbolTable);
//         write_u32(out, offset + 12, h.NumberOfSymbols);
//         write_u16(out, offset + 16, h.SizeOfOptionalHeader);
//         write_u16(out, offset + 18, h.Characteristics);
//     }


//     static void write_optional_header(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         const OptionalHeader64& h)
//     {
//         size_t p = offset;

//         write_u16(out, p, h.Magic); p += 2;

//         if (p + 2 <= out.size()) {
//             out[p++] = h.MajorLinkerVersion;
//             out[p++] = h.MinorLinkerVersion;
//         }

//         write_u32(out, p, h.SizeOfCode); p += 4;
//         write_u32(out, p, h.SizeOfInitializedData); p += 4;
//         write_u32(out, p, h.SizeOfUninitializedData); p += 4;
//         write_u32(out, p, h.AddressOfEntryPoint); p += 4;
//         write_u32(out, p, h.BaseOfCode); p += 4;
//         write_u64(out, p, h.ImageBase); p += 8;
//         write_u32(out, p, h.SectionAlignment); p += 4;
//         write_u32(out, p, h.FileAlignment); p += 4;

//         write_u16(out, p, h.MajorOperatingSystemVersion); p += 2;
//         write_u16(out, p, h.MinorOperatingSystemVersion); p += 2;
//         write_u16(out, p, h.MajorImageVersion); p += 2;
//         write_u16(out, p, h.MinorImageVersion); p += 2;
//         write_u16(out, p, h.MajorSubsystemVersion); p += 2;
//         write_u16(out, p, h.MinorSubsystemVersion); p += 2;

//         write_u32(out, p, h.Win32VersionValue); p += 4;
//         write_u32(out, p, h.SizeOfImage); p += 4;
//         write_u32(out, p, h.SizeOfHeaders); p += 4;
//         write_u32(out, p, h.CheckSum); p += 4;

//         write_u16(out, p, h.Subsystem); p += 2;
//         write_u16(out, p, h.DllCharacteristics); p += 2;

//         write_u64(out, p, h.SizeOfStackReserve); p += 8;
//         write_u64(out, p, h.SizeOfStackCommit); p += 8;
//         write_u64(out, p, h.SizeOfHeapReserve); p += 8;
//         write_u64(out, p, h.SizeOfHeapCommit); p += 8;

//         write_u32(out, p, h.LoaderFlags); p += 4;
//         write_u32(out, p, h.NumberOfRvaAndSizes); p += 4;

//         for (const auto& directory :
//              h.DataDirectories)
//         {
//             write_u32(
//                 out,
//                 p,
//                 directory.VirtualAddress);

//             p += 4;

//             write_u32(
//                 out,
//                 p,
//                 directory.Size);

//             p += 4;
//         }
//     }


//     static void write_section_header(
//         std::vector<uint8_t>& out,
//         size_t offset,
//         const SectionHeader& h)
//     {
//         if (offset + 40 > out.size())
//             return;

//         for (size_t i = 0; i < 8; ++i)
//             out[offset + i] =
//                 h.Name[i];

//         write_u32(
//             out,
//             offset + 8,
//             h.VirtualSize);

//         write_u32(
//             out,
//             offset + 12,
//             h.VirtualAddress);

//         write_u32(
//             out,
//             offset + 16,
//             h.SizeOfRawData);

//         write_u32(
//             out,
//             offset + 20,
//             h.PointerToRawData);

//         write_u32(
//             out,
//             offset + 24,
//             h.PointerToRelocations);

//         write_u32(
//             out,
//             offset + 28,
//             h.PointerToLinenumbers);

//         write_u16(
//             out,
//             offset + 32,
//             h.NumberOfRelocations);

//         write_u16(
//             out,
//             offset + 34,
//             h.NumberOfLinenumbers);

//         write_u32(
//             out,
//             offset + 36,
//             h.Characteristics);
//     }


//     /* ========================================================================
//      * PE checksum
//      * ====================================================================== */

//     [[nodiscard]]
//     static uint32_t calculate_pe_checksum(
//         const std::vector<uint8_t>& image)
//     {
//         constexpr size_t checksum_offset =
//             pe::kDosHeaderOffsetToNT +
//             4 +
//             20 +
//             64;

//         uint64_t sum = 0;

//         const size_t size =
//             image.size();

//         for (size_t i = 0; i < size; i += 2) {
//             uint16_t word = 0;

//             if (i == checksum_offset) {
//                 word = 0;
//             }
//             else {
//                 word =
//                     static_cast<uint16_t>(
//                         image[i]);

//                 if (i + 1 < size)
//                     word |=
//                         static_cast<uint16_t>(
//                             image[i + 1]) << 8;
//             }

//             sum += word;
//             sum = (sum & 0xFFFFULL) +
//                   (sum >> 16);
//         }

//         sum = (sum & 0xFFFFULL) +
//               (sum >> 16);

//         sum += size;

//         return static_cast<uint32_t>(
//             sum);
//     }
// };

// } // namespace dgtl::backend::linker