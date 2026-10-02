/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ELF64 Relocatable Object File Writer
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
#include <concepts>
#include <type_traits>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstdio>

export module dgtl.object.writers.elf;

import dgtl.object.file;
import dgtl.object.writer;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.mir.machine_ir;

export namespace dgtl::object::writers {

class ELFObjectWriter final : public dgtl::object::ObjectWriter {
public:
    [[nodiscard]] std::string_view format_name() const noexcept override {
        return "ELF64";
    }

    [[nodiscard]] bool write(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out) const override {

        out.clear();

        // 1. Gather sections to emit
        std::vector<const dgtl::object::Section*> emit_sections;
        for (const auto& sec : obj.sections()) {
            if (!sec.empty() || sec.is_code()) {
                emit_sections.push_back(&sec);
            }
        }

        // Build Section Header String Table (.shstrtab)
        std::vector<std::uint8_t> shstrtab;
        shstrtab.push_back(0); // initial null byte

        std::vector<std::uint32_t> shstrtab_offsets;
        for (const auto* sec : emit_sections) {
            shstrtab_offsets.push_back(static_cast<std::uint32_t>(shstrtab.size()));
            for (char c : sec->name) shstrtab.push_back(static_cast<std::uint8_t>(c));
            shstrtab.push_back(0);
        }

        const std::uint32_t shstrtab_name_offset = static_cast<std::uint32_t>(shstrtab.size());
        const char shstrtab_name[] = ".shstrtab";
        for (char c : shstrtab_name) shstrtab.push_back(static_cast<std::uint8_t>(c));
        shstrtab.push_back(0);

        const std::uint32_t strtab_name_offset = static_cast<std::uint32_t>(shstrtab.size());
        const char strtab_name[] = ".strtab";
        for (char c : strtab_name) shstrtab.push_back(static_cast<std::uint8_t>(c));
        shstrtab.push_back(0);

        const std::uint32_t symtab_name_offset = static_cast<std::uint32_t>(shstrtab.size());
        const char symtab_name[] = ".symtab";
        for (char c : symtab_name) shstrtab.push_back(static_cast<std::uint8_t>(c));
        shstrtab.push_back(0);

        // Build Symbol String Table (.strtab)
        std::vector<std::uint8_t> strtab;
        strtab.push_back(0); // initial null

        std::vector<std::uint32_t> strtab_offsets;
        for (const auto& sym : obj.symbols()) {
            strtab_offsets.push_back(static_cast<std::uint32_t>(strtab.size()));
            for (char c : sym.name) strtab.push_back(static_cast<std::uint8_t>(c));
            strtab.push_back(0);
        }

        // Build Symbol Table (.symtab, 24 bytes per Elf64_Sym)
        std::vector<std::uint8_t> symtab;
        // Null symbol entry
        for (int i = 0; i < 24; ++i) symtab.push_back(0);

        auto emit_u16 = [](std::vector<std::uint8_t>& buf, std::uint16_t v) {
            buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
            buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        };
        auto emit_u32 = [](std::vector<std::uint8_t>& buf, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        };
        auto emit_u64 = [](std::vector<std::uint8_t>& buf, std::uint64_t v) {
            for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        };

        for (std::size_t i = 0; i < obj.symbols().size(); ++i) {
            const auto& sym = obj.symbols()[i];
            emit_u32(symtab, strtab_offsets[i]); // st_name
            std::uint8_t info = (sym.is_global() ? 1 : 0) << 4; // STB_GLOBAL / STB_LOCAL
            info |= (sym.is_function() ? 2 : (sym.is_object() ? 1 : 0)); // STT_FUNC / STT_OBJECT
            symtab.push_back(info); // st_info
            symtab.push_back(0);    // st_other
            emit_u16(symtab, sym.external ? 0 : 1); // st_shndx (1 = .text)
            emit_u64(symtab, sym.value); // st_value
            emit_u64(symtab, sym.size);  // st_size
        }

        // Layout offsets
        const std::size_t ehdr_size = 64;
        std::size_t cur_offset = ehdr_size;

        std::vector<std::size_t> section_offsets;
        for (const auto* sec : emit_sections) {
            const std::size_t align = std::max<std::size_t>(1, static_cast<std::size_t>(sec->alignment));
            cur_offset = (cur_offset + (align - 1)) & ~(align - 1);
            section_offsets.push_back(cur_offset);
            cur_offset += sec->bytes.size();
        }

        // .symtab offset
        cur_offset = (cur_offset + 7) & ~7;
        const std::size_t symtab_offset = cur_offset;
        cur_offset += symtab.size();

        // .strtab offset
        const std::size_t strtab_offset = cur_offset;
        cur_offset += strtab.size();

        // .shstrtab offset
        const std::size_t shstrtab_offset = cur_offset;
        cur_offset += shstrtab.size();

        // Section header table offset
        cur_offset = (cur_offset + 7) & ~7;
        const std::size_t shdr_offset = cur_offset;

        // Total sections: null + emit_sections + .symtab + .strtab + .shstrtab
        const std::uint16_t num_shdrs = static_cast<std::uint16_t>(1 + emit_sections.size() + 3);
        const std::uint16_t shstrtab_idx = static_cast<std::uint16_t>(num_shdrs - 1);

        // 2. Emit ELF64 Header (64 bytes)
        out.resize(ehdr_size, 0);
        out[0] = 0x7F; out[1] = 'E'; out[2] = 'L'; out[3] = 'F';
        out[4] = 2; // ELFCLASS64
        out[5] = 1; // ELFDATA2LSB (little-endian)
        out[6] = 1; // EV_CURRENT
        out[7] = 0; // ELFOSABI_NONE (System V)

        auto write_u16_at = [&](std::size_t at, std::uint16_t v) {
            out[at] = static_cast<std::uint8_t>(v & 0xFF);
            out[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };
        auto write_u32_at = [&](std::size_t at, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
        };
        auto write_u64_at = [&](std::size_t at, std::uint64_t v) {
            for (int i = 0; i < 8; ++i) out[at + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
        };

        write_u16_at(16, 1);    // e_type = ET_REL (1)
        write_u16_at(18, 62);   // e_machine = EM_X86_64 (62)
        write_u32_at(20, 1);    // e_version = 1
        write_u64_at(24, 0);    // e_entry = 0
        write_u64_at(32, 0);    // e_phoff = 0
        write_u64_at(40, shdr_offset); // e_shoff
        write_u32_at(48, 0);    // e_flags = 0
        write_u16_at(52, 64);   // e_ehsize = 64
        write_u16_at(54, 0);    // e_phentsize = 0
        write_u16_at(56, 0);    // e_phnum = 0
        write_u16_at(58, 64);   // e_shentsize = 64
        write_u16_at(60, num_shdrs);   // e_shnum
        write_u16_at(62, shstrtab_idx); // e_shstrndx

        // 3. Emit Section Payloads
        for (std::size_t i = 0; i < emit_sections.size(); ++i) {
            if (out.size() < section_offsets[i]) out.resize(section_offsets[i], 0);
            out.insert(out.end(), emit_sections[i]->bytes.begin(), emit_sections[i]->bytes.end());
        }

        if (out.size() < symtab_offset) out.resize(symtab_offset, 0);
        out.insert(out.end(), symtab.begin(), symtab.end());

        if (out.size() < strtab_offset) out.resize(strtab_offset, 0);
        out.insert(out.end(), strtab.begin(), strtab.end());

        if (out.size() < shstrtab_offset) out.resize(shstrtab_offset, 0);
        out.insert(out.end(), shstrtab.begin(), shstrtab.end());

        // 4. Emit Section Header Table (64 bytes each)
        if (out.size() < shdr_offset) out.resize(shdr_offset, 0);

        auto emit_shdr = [&](std::uint32_t name, std::uint32_t type, std::uint64_t flags,
                             std::uint64_t addr, std::uint64_t offset, std::uint64_t size,
                             std::uint32_t link, std::uint32_t info, std::uint64_t addralign,
                             std::uint64_t entsize) {
            emit_u32(out, name);
            emit_u32(out, type);
            emit_u64(out, flags);
            emit_u64(out, addr);
            emit_u64(out, offset);
            emit_u64(out, size);
            emit_u32(out, link);
            emit_u32(out, info);
            emit_u64(out, addralign);
            emit_u64(out, entsize);
        };

        // Section 0: SHN_UNDEF
        emit_shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

        // Defined Sections
        for (std::size_t i = 0; i < emit_sections.size(); ++i) {
            const auto* sec = emit_sections[i];
            const std::uint32_t type = (sec->kind == dgtl::object::SectionKind::BSS) ? 8 : 1; // SHT_NOBITS / SHT_PROGBITS
            std::uint64_t flags = 0x2; // SHF_ALLOC
            if (sec->is_code()) flags |= 0x4; // SHF_EXECINSTR
            if (sec->is_writable()) flags |= 0x1; // SHF_WRITE
            emit_shdr(shstrtab_offsets[i], type, flags, 0, section_offsets[i], sec->bytes.size(), 0, 0, sec->alignment, 0);
        }

        // .symtab
        const std::uint32_t strtab_shndx = static_cast<std::uint32_t>(1 + emit_sections.size() + 1);
        emit_shdr(symtab_name_offset, 2, 0, 0, symtab_offset, symtab.size(), strtab_shndx, 1, 8, 24); // SHT_SYMTAB

        // .strtab
        emit_shdr(strtab_name_offset, 3, 0, 0, strtab_offset, strtab.size(), 0, 0, 1, 0); // SHT_STRTAB

        // .shstrtab
        emit_shdr(shstrtab_name_offset, 3, 0, 0, shstrtab_offset, shstrtab.size(), 0, 0, 1, 0); // SHT_STRTAB

        return true;
    }

    [[nodiscard]] bool write(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry = "DGTL_kernel_entry") const {
        auto obj = dgtl::object::ObjectFile::from_machine_function(function, dgtl::object::ObjectFormat::ELF);
        if (!entry.empty()) {
            if (auto* s = obj.find_symbol("DGTL_kernel_entry")) {
                s->name = std::string(entry);
            }
        }
        return write(obj, out);
    }

    [[nodiscard]] bool write(
        std::span<const std::uint8_t> code,
        std::vector<std::uint8_t>& out,
        std::string_view entry = "DGTL_kernel_entry") const {
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::ELF);
        auto& text = obj.get_text_section();
        text.append_bytes(code.data(), code.size());
        obj.add_function_symbol(std::string(entry), 0u, code.size(), true);
        return write(obj, out);
    }

    // Preserved compatibility entry point, constrained to MachineFunction.
    template <typename MachineFunctionLike>
        requires std::same_as<std::remove_cvref_t<MachineFunctionLike>,
                              machine::MachineFunction>
    [[nodiscard]] bool write_machine_function_legacy(
        const MachineFunctionLike& function,
        std::vector<std::uint8_t>& out) const {
        return write(static_cast<const machine::MachineFunction&>(function), out);
    }

    [[nodiscard]] bool write_machine_function(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry = "DGTL_kernel_entry") const {
        return write(function, out, entry);
    }

    [[nodiscard]] bool build_archive(
        const std::vector<std::uint8_t>& object_bytes,
        std::vector<std::uint8_t>& out,
        std::string_view member_name = "module.o") const {
        out.clear();
        std::string header_name(member_name);
        if (header_name.empty()) header_name = "module.o";
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
    using ELFObjectWriter = dgtl::object::writers::ELFObjectWriter;
}


