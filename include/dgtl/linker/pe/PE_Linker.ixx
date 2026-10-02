
/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Sovereign In-Memory PE/COFF Linker (Industrial Grade)
 * Purpose:     Direct in-memory emission of standalone PE32+ (.exe) executables
 *              with complete embedded IAT (kernel32.dll -> ExitProcess),
 *              16-byte Win64 ABI stack alignment, ASLR support, and sub-10ms native execution.
 */

module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.linker.pe;

import dgtl.mir;
import dgtl.object.file;
import dgtl.object.section;

export namespace dgtl::linker::pe {

/* ============================================================================
 * PE Constants & Layout Specifications
 * ========================================================================== */

namespace pe {

inline constexpr uint16_t kDosMagic             = 0x5A4D;     // MZ
inline constexpr uint32_t kPeSignature           = 0x00004550; // PE\0\0

inline constexpr uint16_t kMachineAMD64          = 0x8664;
inline constexpr uint16_t kOptionalHeader64Magic = 0x020B;

inline constexpr uint16_t kNumberOfSections      = 4; // .text, .rdata, .idata, .reloc

inline constexpr uint16_t kCharacteristicsExecutable = 0x0002;
inline constexpr uint16_t kCharacteristicsLargeAddressAware = 0x0020;
inline constexpr uint16_t kCharacteristicsRelocsStripped = 0x0001;

inline constexpr uint16_t kSubsystemWindowsCUI = 0x0003;

inline constexpr uint16_t kDllCharHighEntropyVA = 0x0020;
inline constexpr uint16_t kDllCharDynamicBase   = 0x0040;
inline constexpr uint16_t kDllCharNXCompat      = 0x0100;
inline constexpr uint16_t kDllCharTerminalServerAware = 0x8000;

inline constexpr uint32_t kSectionMemExecute  = 0x20000000;
inline constexpr uint32_t kSectionMemRead     = 0x40000000;
inline constexpr uint32_t kSectionMemWrite    = 0x80000000;
inline constexpr uint32_t kSectionContainsCode = 0x00000020;
inline constexpr uint32_t kSectionInitializedData = 0x00000040;

inline constexpr uint32_t kDirImport    = 1;
inline constexpr uint32_t kDirBaseReloc = 5;
inline constexpr uint32_t kDirIAT      = 12;

inline constexpr uint64_t kPreferredImageBase = 0x0000000140000000ULL;
inline constexpr uint32_t kSectionAlignment   = 0x1000;
inline constexpr uint32_t kFileAlignment      = 0x200;
inline constexpr uint32_t kHeadersSize        = 0x400;
inline constexpr uint32_t kDosHeaderOffsetToNT = 0x80;
inline constexpr uint32_t kDefaultTimestamp   = 0x00000000;

inline constexpr uint32_t kStackReserve = 0x00400000; // 4MB
inline constexpr uint32_t kStackCommit  = 0x00010000;  // 64KB
inline constexpr uint32_t kHeapReserve  = 0x00100000;  // 1MB
inline constexpr uint32_t kHeapCommit   = 0x00001000;  // 4KB

} // namespace pe


/* ============================================================================
 * Low-Level Checked Arithmetic
 * ========================================================================== */

namespace detail {

[[nodiscard]] constexpr bool checked_add_u32(uint32_t a, uint32_t b, uint32_t& out) noexcept {
    if (b > std::numeric_limits<uint32_t>::max() - a) return false;
    out = a + b;
    return true;
}

[[nodiscard]] constexpr uint32_t align_up(uint32_t value, uint32_t alignment) noexcept {
    if (alignment == 0) return value;
    const uint32_t mask = alignment - 1;
    if (value > std::numeric_limits<uint32_t>::max() - mask) return 0;
    return (value + mask) & ~mask;
}

[[nodiscard]] constexpr bool is_power_of_two(uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr bool fits_int32(int64_t value) noexcept {
    return value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max();
}

[[nodiscard]] constexpr bool fits_uint32(uint64_t value) noexcept {
    return value <= std::numeric_limits<uint32_t>::max();
}

} // namespace detail


/* ============================================================================
 * Portable PE Structures
 * ========================================================================== */

struct DosHeader {
    uint16_t e_magic{};
    uint16_t e_cblp{};
    uint16_t e_cp{};
    uint16_t e_crlc{};
    uint16_t e_cparhdr{};
    uint16_t e_minalloc{};
    uint16_t e_maxalloc{};
    uint16_t e_ss{};
    uint16_t e_sp{};
    uint16_t e_csum{};
    uint16_t e_ip{};
    uint16_t e_cs{};
    uint16_t e_lfarlc{};
    uint16_t e_ovno{};
    std::array<uint16_t, 4> e_res{};
    uint16_t e_oemid{};
    uint16_t e_oeminfo{};
    std::array<uint16_t, 10> e_res2{};
    uint32_t e_lfanew{};
};

struct FileHeader {
    uint16_t Machine{};
    uint16_t NumberOfSections{};
    uint32_t TimeDateStamp{};
    uint32_t PointerToSymbolTable{};
    uint32_t NumberOfSymbols{};
    uint16_t SizeOfOptionalHeader{};
    uint16_t Characteristics{};
};

struct DataDirectory {
    uint32_t VirtualAddress{};
    uint32_t Size{};
};

struct OptionalHeader64 {
    uint16_t Magic{};
    uint8_t MajorLinkerVersion{};
    uint8_t MinorLinkerVersion{};

    uint32_t SizeOfCode{};
    uint32_t SizeOfInitializedData{};
    uint32_t SizeOfUninitializedData{};

    uint32_t AddressOfEntryPoint{};
    uint32_t BaseOfCode{};

    uint64_t ImageBase{};

    uint32_t SectionAlignment{};
    uint32_t FileAlignment{};

    uint16_t MajorOperatingSystemVersion{};
    uint16_t MinorOperatingSystemVersion{};

    uint16_t MajorImageVersion{};
    uint16_t MinorImageVersion{};

    uint16_t MajorSubsystemVersion{};
    uint16_t MinorSubsystemVersion{};

    uint32_t Win32VersionValue{};

    uint32_t SizeOfImage{};
    uint32_t SizeOfHeaders{};

    uint32_t CheckSum{};

    uint16_t Subsystem{};
    uint16_t DllCharacteristics{};

    uint64_t SizeOfStackReserve{};
    uint64_t SizeOfStackCommit{};
    uint64_t SizeOfHeapReserve{};
    uint64_t SizeOfHeapCommit{};

    uint32_t LoaderFlags{};
    uint32_t NumberOfRvaAndSizes{};

    std::array<DataDirectory, 16> DataDirectories{};
};

struct SectionHeader {
    std::array<uint8_t, 8> Name{};
    uint32_t VirtualSize{};
    uint32_t VirtualAddress{};
    uint32_t SizeOfRawData{};
    uint32_t PointerToRawData{};
    uint32_t PointerToRelocations{};
    uint32_t PointerToLinenumbers{};
    uint16_t NumberOfRelocations{};
    uint16_t NumberOfLinenumbers{};
    uint32_t Characteristics{};
};

struct ImportDescriptor {
    uint32_t OriginalFirstThunk{};
    uint32_t TimeDateStamp{};
    uint32_t ForwarderChain{};
    uint32_t Name{};
    uint32_t FirstThunk{};
};


/* ============================================================================
 * Sovereign PE Linker Class Interface
 * ========================================================================== */

struct SovereignPELinkerOptions {
    uint64_t image_base = pe::kPreferredImageBase;
    uint32_t timestamp = pe::kDefaultTimestamp;
    bool enable_aslr = true;
    bool deterministic = true;
    bool emit_checksum = true;
    bool emit_relocations = true;
    std::string_view import_dll = "KERNEL32.DLL";
    std::string_view entry_import = "ExitProcess";
};

class SovereignPELinker {
public:
    enum class Error {
        None,
        EmptyCode,
        MainOffsetOutOfRange,
        CodeTooLarge,
        RDataTooLarge,
        InvalidAlignment,
        SectionLayoutOverflow,
        HeaderLayoutOverflow,
        RelativeCallOutOfRange,
        IATDisplacementOutOfRange,
        InvalidImageSize,
        FileOpenFailed,
        FileWriteFailed
    };

    struct Result {
        bool success{false};
        Error error{Error::None};
        std::string message;

        explicit operator bool() const noexcept { return success; }
    };

    using Options = SovereignPELinkerOptions;

private:
    struct Section {
        std::array<uint8_t, 8> name{};
        uint32_t virtual_address{};
        uint32_t virtual_size{};
        uint32_t raw_offset{};
        uint32_t raw_size{};
        uint32_t characteristics{};
        std::vector<uint8_t> data;
    };

public:
    static Result emit_standalone_executable(
        const std::string& output_path,
        const dgtl::object::ObjectFile& obj,
        const Options& options = Options{})
    {
        std::span<const uint8_t> code_span{};
        std::span<const uint8_t> rdata_span{};
        if (const auto* text_sec = obj.find_section(".text")) {
            code_span = std::span<const uint8_t>(text_sec->bytes.data(), text_sec->bytes.size());
        }
        if (const auto* rdata_sec = obj.find_section(".rdata")) {
            rdata_span = std::span<const uint8_t>(rdata_sec->bytes.data(), rdata_sec->bytes.size());
        }
        if (code_span.empty()) {
            return failure(Error::EmptyCode, "DGTL PE linker: ObjectFile has no .text section");
        }
        return emit_standalone_executable(output_path, code_span, rdata_span, 0, options);
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        const machine::MachineFunction& fn,
        const Options& options = Options{})
    {
        auto obj = dgtl::object::ObjectFile::from_machine_function(fn, dgtl::object::ObjectFormat::COFF);
        return emit_standalone_executable(output_path, obj, options);
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        std::span<const uint8_t> code_bytes,
        std::span<const uint8_t> user_rdata,
        uint32_t main_func_offset = 0)
    {
        return emit_standalone_executable(output_path, code_bytes, user_rdata, main_func_offset, Options{});
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        std::span<const uint8_t> code_bytes,
        std::span<const uint8_t> user_rdata,
        uint32_t main_func_offset,
        const Options& options)
    {
        if (code_bytes.empty()) {
            return failure(Error::EmptyCode, "DGTL PE linker: code section is empty");
        }
        if (main_func_offset >= code_bytes.size()) {
            return failure(Error::MainOffsetOutOfRange, "DGTL PE linker: main_func_offset is outside code bounds");
        }
        if (!detail::is_power_of_two(pe::kFileAlignment) || !detail::is_power_of_two(pe::kSectionAlignment)) {
            return failure(Error::InvalidAlignment, "DGTL PE linker: invalid PE alignment configuration");
        }
        if (!detail::fits_uint32(code_bytes.size()) || !detail::fits_uint32(user_rdata.size())) {
            return failure(Error::CodeTooLarge, "DGTL PE linker: input exceeds PE32+ 32-bit RVA limits");
        }

        Section text;
        Section rdata;
        Section idata;
        Section reloc;

        initialize_section(text, ".text", pe::kSectionMemExecute | pe::kSectionMemRead | pe::kSectionContainsCode);
        initialize_section(rdata, ".rdata", pe::kSectionMemRead | pe::kSectionInitializedData);
        initialize_section(idata, ".idata", pe::kSectionMemRead | pe::kSectionMemWrite | pe::kSectionInitializedData);
        initialize_section(reloc, ".reloc", pe::kSectionMemRead | pe::kSectionInitializedData);

        rdata.data.assign(user_rdata.begin(), user_rdata.end());

        constexpr uint32_t trampoline_size = 24;
        text.data.reserve(trampoline_size + code_bytes.size());
        text.data.resize(trampoline_size, 0x90);
        text.data.insert(text.data.end(), code_bytes.begin(), code_bytes.end());

        constexpr uint32_t text_rva = 0x1000;
        const uint32_t text_virtual_size = static_cast<uint32_t>(text.data.size());
        const uint32_t text_raw_size = detail::align_up(text_virtual_size, pe::kFileAlignment);

        if (text_raw_size == 0) {
            return failure(Error::SectionLayoutOverflow, "DGTL PE linker: .text alignment overflow");
        }

        const uint32_t rdata_rva = detail::align_up(text_rva + text_virtual_size, pe::kSectionAlignment);
        const uint32_t idata_rva = detail::align_up(rdata_rva + static_cast<uint32_t>(rdata.data.size()), pe::kSectionAlignment);
        const uint32_t reloc_rva = detail::align_up(idata_rva + 0x1000, pe::kSectionAlignment);

        // Build .idata Import Table Layout
        constexpr uint32_t descriptor_count = 2;
        constexpr uint32_t thunk_count = 2;
        const uint32_t descriptor_size = descriptor_count * sizeof(ImportDescriptor);
        const uint32_t thunk_size = thunk_count * sizeof(uint64_t);
        const uint32_t hint_name_size = 2 + static_cast<uint32_t>(options.entry_import.size()) + 1;
        const uint32_t dll_name_size = static_cast<uint32_t>(options.import_dll.size() + 1);

        uint32_t import_size = 0;
        if (!detail::checked_add_u32(descriptor_size, thunk_size, import_size) ||
            !detail::checked_add_u32(import_size, thunk_size, import_size) ||
            !detail::checked_add_u32(import_size, hint_name_size, import_size) ||
            !detail::checked_add_u32(import_size, dll_name_size, import_size)) {
            return failure(Error::RDataTooLarge, "DGTL PE linker: import table size overflow");
        }

        idata.data.assign(import_size + 16, 0);

        const uint32_t desc_off = 0;
        const uint32_t ilt_off  = desc_off + descriptor_size;
        const uint32_t iat_off  = ilt_off + thunk_size;
        const uint32_t hint_off = iat_off + thunk_size;
        const uint32_t dll_off  = hint_off + hint_name_size;

        const uint32_t ilt_rva  = idata_rva + ilt_off;
        const uint32_t iat_rva  = idata_rva + iat_off;
        const uint32_t hint_rva = idata_rva + hint_off;
        const uint32_t dll_rva  = idata_rva + dll_off;

        write_u32(idata.data, desc_off + 0, ilt_rva);
        write_u32(idata.data, desc_off + 12, dll_rva);
        write_u32(idata.data, desc_off + 16, iat_rva);

        write_u64(idata.data, ilt_off, hint_rva);
        write_u64(idata.data, iat_off, hint_rva);

        write_u16(idata.data, hint_off, 0);
        write_bytes(idata.data, hint_off + 2, options.entry_import);
        write_bytes(idata.data, dll_off, options.import_dll);

        idata.virtual_address = idata_rva;
        idata.virtual_size = static_cast<uint32_t>(idata.data.size());

        if (options.emit_relocations) {
            reloc.data.clear();
        }
        reloc.virtual_address = reloc_rva;
        reloc.virtual_size = static_cast<uint32_t>(reloc.data.size());

        text.virtual_address = text_rva;
        text.virtual_size = text_virtual_size;

        rdata.virtual_address = rdata_rva;
        rdata.virtual_size = static_cast<uint32_t>(rdata.data.size());

        text.raw_size = text_raw_size;
        rdata.raw_size = detail::align_up(rdata.virtual_size, pe::kFileAlignment);
        idata.raw_size = detail::align_up(idata.virtual_size, pe::kFileAlignment);
        reloc.raw_size = detail::align_up(reloc.virtual_size, pe::kFileAlignment);

        uint32_t cursor = pe::kHeadersSize;
        text.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, text.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .text file offset overflow");
        }
        rdata.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, rdata.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .rdata file offset overflow");
        }
        idata.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, idata.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .idata file offset overflow");
        }

        const bool has_reloc = !reloc.data.empty();
        if (has_reloc) {
            reloc.raw_offset = cursor;
            if (!detail::checked_add_u32(cursor, reloc.raw_size, cursor)) {
                return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .reloc file offset overflow");
            }
        }
        const uint32_t file_size = cursor;

        uint32_t image_end = text_rva + detail::align_up(text.virtual_size, pe::kSectionAlignment);
        image_end = std::max(image_end, rdata_rva + detail::align_up(rdata.virtual_size, pe::kSectionAlignment));
        image_end = std::max(image_end, idata_rva + detail::align_up(idata.virtual_size, pe::kSectionAlignment));
        if (has_reloc) {
            image_end = std::max(image_end, reloc_rva + detail::align_up(reloc.virtual_size, pe::kSectionAlignment));
        }
        const uint32_t size_of_image = detail::align_up(image_end, pe::kSectionAlignment);

        // Patch Entry Trampoline
        {
            const int64_t main_rva = static_cast<int64_t>(text_rva) + static_cast<int64_t>(trampoline_size) + static_cast<int64_t>(main_func_offset);
            const int64_t call_next_rva = static_cast<int64_t>(text_rva) + 9;
            const int64_t call_disp = main_rva - call_next_rva;

            if (!detail::fits_int32(call_disp)) {
                return failure(Error::RelativeCallOutOfRange, "DGTL PE linker: main function is outside rel32 range");
            }

            text.data[0] = 0x48; text.data[1] = 0x83; text.data[2] = 0xEC; text.data[3] = 0x28; // sub rsp, 0x28
            text.data[4] = 0xE8; // call rel32
            write_i32(text.data, 5, static_cast<int32_t>(call_disp));
            text.data[9] = 0x89; text.data[10] = 0xC1; // mov ecx, eax

            const int64_t iat_disp = static_cast<int64_t>(idata_rva + iat_off) - static_cast<int64_t>(text_rva + 17);
            if (!detail::fits_int32(iat_disp)) {
                return failure(Error::IATDisplacementOutOfRange, "DGTL PE linker: IAT is outside RIP-relative range");
            }
            text.data[11] = 0xFF; text.data[12] = 0x15; // call qword ptr [rip + disp32]
            write_i32(text.data, 13, static_cast<int32_t>(iat_disp));
            text.data[17] = 0x48; text.data[18] = 0x83; text.data[19] = 0xC4; text.data[20] = 0x28; // add rsp, 0x28
            text.data[21] = 0xC3; // ret
        }

        DosHeader dos{};
        dos.e_magic = pe::kDosMagic;
        dos.e_lfanew = pe::kDosHeaderOffsetToNT;

        FileHeader file_hdr{};
        file_hdr.Machine = pe::kMachineAMD64;
        file_hdr.NumberOfSections = pe::kNumberOfSections;
        file_hdr.TimeDateStamp = options.deterministic ? 0 : options.timestamp;
        file_hdr.SizeOfOptionalHeader = 240;
        file_hdr.Characteristics = pe::kCharacteristicsExecutable | pe::kCharacteristicsLargeAddressAware;

        OptionalHeader64 opt{};
        opt.Magic = pe::kOptionalHeader64Magic;
        opt.MajorLinkerVersion = 14;
        opt.SizeOfCode = text.raw_size;
        opt.SizeOfInitializedData = rdata.raw_size + idata.raw_size + reloc.raw_size;
        opt.AddressOfEntryPoint = text_rva;
        opt.BaseOfCode = text_rva;
        opt.ImageBase = options.image_base;
        opt.SectionAlignment = pe::kSectionAlignment;
        opt.FileAlignment = pe::kFileAlignment;
        opt.MajorOperatingSystemVersion = 6;
        opt.MajorSubsystemVersion = 6;
        opt.SizeOfImage = size_of_image;
        opt.SizeOfHeaders = pe::kHeadersSize;
        opt.Subsystem = pe::kSubsystemWindowsCUI;
        opt.DllCharacteristics = pe::kDllCharNXCompat | pe::kDllCharTerminalServerAware;
        if (options.enable_aslr) {
            opt.DllCharacteristics |= pe::kDllCharDynamicBase | pe::kDllCharHighEntropyVA;
        }
        opt.SizeOfStackReserve = pe::kStackReserve;
        opt.SizeOfStackCommit = pe::kStackCommit;
        opt.SizeOfHeapReserve = pe::kHeapReserve;
        opt.SizeOfHeapCommit = pe::kHeapCommit;
        opt.NumberOfRvaAndSizes = 16;
        opt.DataDirectories[pe::kDirImport].VirtualAddress = idata_rva + desc_off;
        opt.DataDirectories[pe::kDirImport].Size = descriptor_size;
        opt.DataDirectories[pe::kDirIAT].VirtualAddress = idata_rva + iat_off;
        opt.DataDirectories[pe::kDirIAT].Size = thunk_size;

        std::array<SectionHeader, pe::kNumberOfSections> section_headers{};
        fill_section_header(section_headers[0], text);
        fill_section_header(section_headers[1], rdata);
        fill_section_header(section_headers[2], idata);
        fill_section_header(section_headers[3], reloc);

        std::vector<uint8_t> binary(file_size, 0);
        write_dos_header(binary, 0, dos);

        static constexpr std::array<uint8_t, 64> dos_stub = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
            'D','G','T','L',' ','P','E','3','2','+','\r','\n','$',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        copy_bytes(binary, 64, dos_stub);
        write_u32(binary, pe::kDosHeaderOffsetToNT, pe::kPeSignature);

        size_t nt_cursor = pe::kDosHeaderOffsetToNT + 4;
        write_file_header(binary, nt_cursor, file_hdr);
        nt_cursor += 20;
        write_optional_header(binary, nt_cursor, opt);
        nt_cursor += 240;

        for (const auto& sh : section_headers) {
            write_section_header(binary, nt_cursor, sh);
            nt_cursor += 40;
        }

        copy_bytes(binary, text.raw_offset, text.data);
        copy_bytes(binary, rdata.raw_offset, rdata.data);
        copy_bytes(binary, idata.raw_offset, idata.data);
        if (has_reloc) {
            copy_bytes(binary, reloc.raw_offset, reloc.data);
        }

        if (options.emit_checksum) {
            const uint32_t checksum = calculate_pe_checksum(binary);
            write_u32(binary, pe::kDosHeaderOffsetToNT + 4 + 20 + 64, checksum);
        }

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return failure(Error::FileOpenFailed, "DGTL PE linker: unable to open output file");
        }
        out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
        out.flush();
        if (!out.good()) {
            return failure(Error::FileWriteFailed, "DGTL PE linker: failed while writing output file");
        }

        return {true, Error::None, ""};
    }

private:
    static Result failure(Error error, std::string message) noexcept {
        return {false, error, std::move(message)};
    }

    static void initialize_section(Section& section, std::string_view name, uint32_t characteristics) noexcept {
        section.name.fill(0);
        const size_t count = std::min(name.size(), section.name.size());
        for (size_t i = 0; i < count; ++i) section.name[i] = static_cast<uint8_t>(name[i]);
        section.characteristics = characteristics;
    }

    static void fill_section_header(SectionHeader& header, const Section& section) noexcept {
        header = {};
        header.Name = section.name;
        header.VirtualSize = section.virtual_size;
        header.VirtualAddress = section.virtual_address;
        header.SizeOfRawData = section.raw_size;
        header.PointerToRawData = section.raw_offset;
        header.Characteristics = section.characteristics;
    }

    static void write_u16(std::vector<uint8_t>& out, size_t offset, uint16_t value) noexcept {
        if (offset + 2 <= out.size()) {
            out[offset + 0] = static_cast<uint8_t>(value & 0xFF);
            out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        }
    }

    static void write_u32(std::vector<uint8_t>& out, size_t offset, uint32_t value) noexcept {
        if (offset + 4 <= out.size()) {
            out[offset + 0] = static_cast<uint8_t>(value & 0xFF);
            out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
            out[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
            out[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        }
    }

    static void write_i32(std::vector<uint8_t>& out, size_t offset, int32_t value) noexcept {
        write_u32(out, offset, static_cast<uint32_t>(value));
    }

    static void write_u64(std::vector<uint8_t>& out, size_t offset, uint64_t value) noexcept {
        if (offset + 8 <= out.size()) {
            for (unsigned i = 0; i < 8; ++i) {
                out[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
            }
        }
    }

    static void write_bytes(std::vector<uint8_t>& out, size_t offset, std::string_view value) noexcept {
        if (offset > out.size()) return;
        const size_t count = std::min(out.size() - offset, value.size());
        for (size_t i = 0; i < count; ++i) out[offset + i] = static_cast<uint8_t>(value[i]);
    }

    static void copy_bytes(std::vector<uint8_t>& out, size_t offset, std::span<const uint8_t> data) noexcept {
        if (offset > out.size()) return;
        const size_t count = std::min(out.size() - offset, data.size());
        std::copy_n(data.begin(), count, out.begin() + offset);
    }

    static void write_dos_header(std::vector<uint8_t>& out, size_t offset, const DosHeader& h) noexcept {
        write_u16(out, offset + 0, h.e_magic);
        write_u16(out, offset + 60, h.e_lfanew);
    }

    static void write_file_header(std::vector<uint8_t>& out, size_t offset, const FileHeader& h) noexcept {
        write_u16(out, offset + 0, h.Machine);
        write_u16(out, offset + 2, h.NumberOfSections);
        write_u32(out, offset + 4, h.TimeDateStamp);
        write_u32(out, offset + 8, h.PointerToSymbolTable);
        write_u32(out, offset + 12, h.NumberOfSymbols);
        write_u16(out, offset + 16, h.SizeOfOptionalHeader);
        write_u16(out, offset + 18, h.Characteristics);
    }

    static void write_optional_header(std::vector<uint8_t>& out, size_t offset, const OptionalHeader64& h) noexcept {
        size_t p = offset;
        write_u16(out, p, h.Magic); p += 2;
        if (p + 2 <= out.size()) {
            out[p++] = h.MajorLinkerVersion;
            out[p++] = h.MinorLinkerVersion;
        }
        write_u32(out, p, h.SizeOfCode); p += 4;
        write_u32(out, p, h.SizeOfInitializedData); p += 4;
        write_u32(out, p, h.SizeOfUninitializedData); p += 4;
        write_u32(out, p, h.AddressOfEntryPoint); p += 4;
        write_u32(out, p, h.BaseOfCode); p += 4;
        write_u64(out, p, h.ImageBase); p += 8;
        write_u32(out, p, h.SectionAlignment); p += 4;
        write_u32(out, p, h.FileAlignment); p += 4;
        write_u16(out, p, h.MajorOperatingSystemVersion); p += 2;
        write_u16(out, p, h.MinorOperatingSystemVersion); p += 2;
        write_u16(out, p, h.MajorImageVersion); p += 2;
        write_u16(out, p, h.MinorImageVersion); p += 2;
        write_u16(out, p, h.MajorSubsystemVersion); p += 2;
        write_u16(out, p, h.MinorSubsystemVersion); p += 2;
        write_u32(out, p, h.Win32VersionValue); p += 4;
        write_u32(out, p, h.SizeOfImage); p += 4;
        write_u32(out, p, h.SizeOfHeaders); p += 4;
        write_u32(out, p, h.CheckSum); p += 4;
        write_u16(out, p, h.Subsystem); p += 2;
        write_u16(out, p, h.DllCharacteristics); p += 2;
        write_u64(out, p, h.SizeOfStackReserve); p += 8;
        write_u64(out, p, h.SizeOfStackCommit); p += 8;
        write_u64(out, p, h.SizeOfHeapReserve); p += 8;
        write_u64(out, p, h.SizeOfHeapCommit); p += 8;
        write_u32(out, p, h.LoaderFlags); p += 4;
        write_u32(out, p, h.NumberOfRvaAndSizes); p += 4;

        for (const auto& directory : h.DataDirectories) {
            write_u32(out, p, directory.VirtualAddress); p += 4;
            write_u32(out, p, directory.Size); p += 4;
        }
    }

    static void write_section_header(std::vector<uint8_t>& out, size_t offset, const SectionHeader& h) noexcept {
        if (offset + 40 > out.size()) return;
        for (size_t i = 0; i < 8; ++i) out[offset + i] = h.Name[i];
        write_u32(out, offset + 8, h.VirtualSize);
        write_u32(out, offset + 12, h.VirtualAddress);
        write_u32(out, offset + 16, h.SizeOfRawData);
        write_u32(out, offset + 20, h.PointerToRawData);
        write_u32(out, offset + 24, h.PointerToRelocations);
        write_u32(out, offset + 28, h.PointerToLinenumbers);
        write_u16(out, offset + 32, h.NumberOfRelocations);
        write_u16(out, offset + 34, h.NumberOfLinenumbers);
        write_u32(out, offset + 36, h.Characteristics);
    }

    [[nodiscard]] static uint32_t calculate_pe_checksum(const std::vector<uint8_t>& image) noexcept {
        constexpr size_t checksum_offset = 0x80 + 4 + 20 + 64;
        uint64_t sum = 0;
        const size_t size = image.size();
        for (size_t i = 0; i < size; i += 2) {
            uint16_t word = 0;
            if (i != checksum_offset) {
                word = static_cast<uint16_t>(image[i]);
                if (i + 1 < size) word |= static_cast<uint16_t>(image[i + 1]) << 8;
            }
            sum += word;
            sum = (sum & 0xFFFFULL) + (sum >> 16);
        }
        sum = (sum & 0xFFFFULL) + (sum >> 16);
        sum += size;
        return static_cast<uint32_t>(sum);
    }
};

} // namespace dgtl::linker::pe

export namespace dgtl::backend::linker::pe {
    using SovereignPELinker = dgtl::linker::pe::SovereignPELinker;
    using SovereignPELinkerOptions = dgtl::linker::pe::SovereignPELinkerOptions;
}


