/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   PE/COFF Object and Executable Writer
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstdio>

export module dgtl.object.writers.pe;

import dgtl.object.file;
import dgtl.object.writer;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.mir.machine_ir;
import dgtl.linker.pe;

export namespace dgtl::object::writers {

class PEObjectWriter final : public dgtl::object::ObjectWriter {
public:
    [[nodiscard]] std::string_view format_name() const noexcept override {
        return "PE/COFF";
    }

    [[nodiscard]] bool write(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out) const override {

        out.clear();

        std::vector<const dgtl::object::Section*> emit_sections;
        for (const auto& sec : obj.sections()) {
            if (!sec.empty() || sec.is_code()) {
                emit_sections.push_back(&sec);
            }
        }

        const std::size_t file_header_size = 20;
        const std::size_t section_header_size = 40;
        const std::uint16_t num_sections = static_cast<std::uint16_t>(emit_sections.size());

        std::size_t cur_offset = file_header_size + (num_sections * section_header_size);

        std::vector<std::size_t> section_offsets;
        for (const auto* sec : emit_sections) {
            const std::size_t align = std::max<std::size_t>(4, static_cast<std::size_t>(sec->alignment));
            cur_offset = (cur_offset + (align - 1)) & ~(align - 1);
            section_offsets.push_back(cur_offset);
            cur_offset += sec->bytes.size();
        }

        // Symbol Table & String Table
        const std::size_t symtab_offset = cur_offset;
        const std::uint32_t num_symbols = static_cast<std::uint32_t>(obj.symbols().size());

        std::vector<std::uint8_t> strtab;
        strtab.resize(4, 0); // 4-byte size header

        std::vector<std::uint32_t> strtab_offsets;
        for (const auto& sym : obj.symbols()) {
            if (sym.name.size() > 8) {
                strtab_offsets.push_back(static_cast<std::uint32_t>(strtab.size()));
                for (char c : sym.name) strtab.push_back(static_cast<std::uint8_t>(c));
                strtab.push_back(0);
            } else {
                strtab_offsets.push_back(0);
            }
        }
        const std::uint32_t strtab_size = static_cast<std::uint32_t>(strtab.size());
        for (int i = 0; i < 4; ++i) strtab[i] = static_cast<std::uint8_t>((strtab_size >> (i * 8)) & 0xFF);

        auto emit_u16 = [](std::vector<std::uint8_t>& buf, std::uint16_t v) {
            buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
            buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        };
        auto emit_u32 = [](std::vector<std::uint8_t>& buf, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        };

        // 1. COFF File Header (20 bytes)
        emit_u16(out, 0x8664); // IMAGE_FILE_MACHINE_AMD64
        emit_u16(out, num_sections);
        emit_u32(out, 0); // TimeDateStamp
        emit_u32(out, static_cast<std::uint32_t>(symtab_offset));
        emit_u32(out, num_symbols);
        emit_u16(out, 0); // SizeOfOptionalHeader
        emit_u16(out, 0); // Characteristics

        // 2. Section Headers (40 bytes each)
        for (std::size_t i = 0; i < emit_sections.size(); ++i) {
            const auto* sec = emit_sections[i];
            char name_buf[8] = {0};
            std::strncpy(name_buf, sec->name.c_str(), 8);
            for (int b = 0; b < 8; ++b) out.push_back(static_cast<std::uint8_t>(name_buf[b]));

            emit_u32(out, 0); // VirtualSize
            emit_u32(out, 0); // VirtualAddress
            emit_u32(out, static_cast<std::uint32_t>(sec->bytes.size())); // SizeOfRawData
            emit_u32(out, static_cast<std::uint32_t>(section_offsets[i])); // PointerToRawData
            emit_u32(out, 0); // PointerToRelocations
            emit_u32(out, 0); // PointerToLinenumbers
            emit_u16(out, 0); // NumberOfRelocations
            emit_u16(out, 0); // NumberOfLinenumbers

            std::uint32_t flags = 0x00000040 | 0x40000000; // IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ
            if (sec->is_code()) {
                flags = 0x00000020 | 0x20000000 | 0x40000000; // IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ
            } else if (sec->is_writable()) {
                flags |= 0x80000000; // IMAGE_SCN_MEM_WRITE
            }
            emit_u32(out, flags);
        }

        // 3. Section Data
        for (std::size_t i = 0; i < emit_sections.size(); ++i) {
            if (out.size() < section_offsets[i]) out.resize(section_offsets[i], 0);
            out.insert(out.end(), emit_sections[i]->bytes.begin(), emit_sections[i]->bytes.end());
        }

        // 4. Symbol Table (18 bytes each)
        if (out.size() < symtab_offset) out.resize(symtab_offset, 0);
        for (std::size_t i = 0; i < obj.symbols().size(); ++i) {
            const auto& sym = obj.symbols()[i];
            if (sym.name.size() <= 8) {
                char sname[8] = {0};
                std::strncpy(sname, sym.name.c_str(), 8);
                for (int b = 0; b < 8; ++b) out.push_back(static_cast<std::uint8_t>(sname[b]));
            } else {
                emit_u32(out, 0);
                emit_u32(out, strtab_offsets[i]);
            }
            emit_u32(out, static_cast<std::uint32_t>(sym.value));
            emit_u16(out, sym.external ? 0 : 1); // SectionNumber
            emit_u16(out, sym.is_function() ? 0x20 : 0); // Type
            out.push_back(sym.is_global() ? 2 : 3); // StorageClass: IMAGE_SYM_CLASS_EXTERNAL / STATIC
            out.push_back(0); // NumberOfAuxSymbols
        }

        // 5. String Table
        out.insert(out.end(), strtab.begin(), strtab.end());

        return true;
    }

    [[nodiscard]] bool write(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        auto obj = dgtl::object::ObjectFile::from_machine_function(function, dgtl::object::ObjectFormat::COFF);
        if (!entry_symbol.empty()) {
            if (auto* s = obj.find_symbol("DGTL_kernel_entry")) {
                s->name = std::string(entry_symbol);
            }
        }
        return write(obj, out);
    }

    [[nodiscard]] bool write_machine_function(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        return write(function, out, entry_symbol);
    }

    [[nodiscard]] bool write_standalone_executable(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        return write(function, out, entry_symbol);
    }

    [[nodiscard]] bool write_standalone_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out) const {
        return write(obj, out);
    }

    [[nodiscard]] bool write_executable(
        const std::string& output_path,
        std::span<const std::uint8_t> code,
        std::span<const std::uint8_t> user_rdata = {},
        std::uint32_t main_func_offset = 0) const {
        const auto result =
            dgtl::linker::pe::SovereignPELinker::
                emit_standalone_executable(output_path, code, user_rdata, main_func_offset);
        return static_cast<bool>(result);
    }

    [[nodiscard]] bool write_executable(
        const std::string& output_path,
        const machine::MachineFunction& function) const {
        auto obj = dgtl::object::ObjectFile::from_machine_function(function, dgtl::object::ObjectFormat::COFF);
        const auto& text = obj.get_text_section();
        const auto& rdata = obj.get_rdata_section();
        return write_executable(output_path, text.bytes, rdata.bytes, 0);
    }

    [[nodiscard]] bool build_archive(
        const std::vector<std::uint8_t>& object_bytes,
        std::vector<std::uint8_t>& out,
        std::string_view member_name = "module.obj") const {
        out.clear();
        std::string header_name(member_name);
        if (header_name.empty()) header_name = "module.obj";
        if (header_name.size() > 15) header_name.resize(15);
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
};

} // namespace dgtl::backend::object

export namespace dgtl::object::writers {
} namespace dgtl::backend::object {
    using PEObjectWriter = dgtl::object::writers::PEObjectWriter;
}


