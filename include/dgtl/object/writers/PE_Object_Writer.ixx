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
#include <unordered_map>

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

        // 0. Gather all symbols, including external symbols from relocations
        std::vector<dgtl::object::Symbol> all_symbols = obj.symbols();
        std::unordered_map<std::string, std::uint32_t> symbol_indices;
        for (std::size_t i = 0; i < all_symbols.size(); ++i) {
            symbol_indices[all_symbols[i].name] = static_cast<std::uint32_t>(i);
        }

        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            if (!symbol_indices.contains(rel.symbol_name)) {
                symbol_indices[rel.symbol_name] = static_cast<std::uint32_t>(all_symbols.size());
                all_symbols.push_back(dgtl::object::Symbol::make_external(
                    rel.symbol_name, dgtl::object::SymbolType::Function, 1));
            }
        }

        // Locate text section index (for relocations)
        std::size_t text_sec_idx = 0;
        for (std::size_t i = 0; i < emit_sections.size(); ++i) {
            if (emit_sections[i]->is_code() || emit_sections[i]->name == ".text") {
                text_sec_idx = i;
                break;
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

        // Relocations offset (associated with text section)
        std::size_t reloc_offset = 0;
        const std::size_t num_relocs = obj.relocations().size();
        if (num_relocs > 0) {
            cur_offset = (cur_offset + 3) & ~3;
            reloc_offset = cur_offset;
            cur_offset += num_relocs * 10;
        }

        // Symbol Table & String Table
        cur_offset = (cur_offset + 3) & ~3;
        const std::size_t symtab_offset = cur_offset;
        const std::uint32_t num_symbols = static_cast<std::uint32_t>(all_symbols.size());

        std::vector<std::uint8_t> strtab;
        strtab.resize(4, 0); // 4-byte size header

        std::vector<std::uint32_t> strtab_offsets;
        for (const auto& sym : all_symbols) {
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

            if (i == text_sec_idx && num_relocs > 0) {
                emit_u32(out, static_cast<std::uint32_t>(reloc_offset)); // PointerToRelocations
                emit_u32(out, 0); // PointerToLinenumbers
                emit_u16(out, static_cast<std::uint16_t>(num_relocs)); // NumberOfRelocations
                emit_u16(out, 0); // NumberOfLinenumbers
            } else {
                emit_u32(out, 0); // PointerToRelocations
                emit_u32(out, 0); // PointerToLinenumbers
                emit_u16(out, 0); // NumberOfRelocations
                emit_u16(out, 0); // NumberOfLinenumbers
            }

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

        // 4. Relocations Data
        if (num_relocs > 0) {
            if (out.size() < reloc_offset) out.resize(reloc_offset, 0);
            for (const auto& rel : obj.relocations()) {
                emit_u32(out, static_cast<std::uint32_t>(rel.offset));
                std::uint32_t sym_idx = 0;
                auto sit = symbol_indices.find(rel.symbol_name);
                if (sit != symbol_indices.end()) sym_idx = sit->second;
                emit_u32(out, sym_idx);
                std::uint16_t r_type = rel.type != 0 ? static_cast<std::uint16_t>(rel.type) : 4u;
                emit_u16(out, r_type);
            }
        }

        // 5. Symbol Table (18 bytes each)
        if (out.size() < symtab_offset) out.resize(symtab_offset, 0);
        for (std::size_t i = 0; i < all_symbols.size(); ++i) {
            const auto& sym = all_symbols[i];
            if (sym.name.size() <= 8) {
                char sname[8] = {0};
                std::strncpy(sname, sym.name.c_str(), 8);
                for (int b = 0; b < 8; ++b) out.push_back(static_cast<std::uint8_t>(sname[b]));
            } else {
                emit_u32(out, 0);
                emit_u32(out, strtab_offsets[i]);
            }
            emit_u32(out, static_cast<std::uint32_t>(sym.value));
            std::uint16_t sec_num = 0;
            if (!sym.external) {
                sec_num = 1;
                if (sym.section_index < obj.sections().size()) {
                    const auto* target_sec = &obj.sections()[sym.section_index];
                    for (std::size_t k = 0; k < emit_sections.size(); ++k) {
                        if (emit_sections[k] == target_sec) {
                            sec_num = static_cast<std::uint16_t>(k + 1);
                            break;
                        }
                    }
                }
            }
            emit_u16(out, sec_num); // SectionNumber: 0 = UNDEFINED, 1-based index
            emit_u16(out, sym.is_function() ? 0x20 : 0); // Type
            out.push_back(sym.is_global() || sym.external ? 2 : 3); // StorageClass: 2 = EXTERNAL, 3 = STATIC
            out.push_back(0); // NumberOfAuxSymbols
        }

        // 6. String Table
        out.insert(out.end(), strtab.begin(), strtab.end());

        return true;
    }

    [[nodiscard]] bool write(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::COFF);
        if (!entry_symbol.empty()) {
            obj.add_function_symbol(std::string(entry_symbol), 0u, 0u, true);
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
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::PE);
        if (!entry_symbol.empty()) {
            obj.add_function_symbol(std::string(entry_symbol), 0u, 0u, true);
        }
        return write_standalone_executable(obj, out);
    }

    [[nodiscard]] bool write_standalone_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out,
        const dgtl::linker::pe::SovereignPELinkerOptions& options = {}) const {
        const auto res = dgtl::linker::pe::SovereignPELinker::emit_standalone_binary(obj, out, options);
        return static_cast<bool>(res);
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
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::COFF);
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

} // namespace dgtl::object::writers

export namespace dgtl::backend::object {
    using PEObjectWriter = dgtl::object::writers::PEObjectWriter;
}



