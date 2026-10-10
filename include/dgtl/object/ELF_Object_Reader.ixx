/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ELF64 Relocatable Object Reader & Parser
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.object.readers.elf;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;

export namespace dgtl::object::readers {

class ELFObjectReader {
public:
    ELFObjectReader() = default;
    ~ELFObjectReader() = default;

    ELFObjectReader(const ELFObjectReader&) = delete;
    ELFObjectReader& operator=(const ELFObjectReader&) = delete;

    [[nodiscard]] static bool is_elf_format(const uint8_t* data, size_t size) noexcept {
        return size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
    }

    [[nodiscard]] static bool is_elf_format(const std::vector<uint8_t>& buffer) noexcept {
        return is_elf_format(buffer.data(), buffer.size());
    }

    [[nodiscard]] bool read(const void* data, size_t size, dgtl::object::ObjectFile& out) const {
        if (data == nullptr || size < 64) {
            return false;
        }

        const auto* byte_data = static_cast<const uint8_t*>(data);
        if (!is_elf_format(byte_data, size)) {
            return false;
        }

        std::vector<uint8_t> buffer(byte_data, byte_data + size);
        return parse_elf_object(buffer, out);
    }

    [[nodiscard]] bool read_file(const std::string& filepath, dgtl::object::ObjectFile& out) const {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size < 64) {
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return false;
        }

        if (!is_elf_format(buffer)) {
            return false;
        }

        return parse_elf_object(buffer, out);
    }

    [[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load(const std::string& filepath) const {
        auto obj = std::make_unique<dgtl::object::ObjectFile>(dgtl::object::ObjectFormat::ELF);
        if (!read_file(filepath, *obj)) {
            return nullptr;
        }
        return obj;
    }

    [[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load_from_memory(const void* data, size_t size) const {
        auto obj = std::make_unique<dgtl::object::ObjectFile>(dgtl::object::ObjectFormat::ELF);
        if (!read(data, size, *obj)) {
            return nullptr;
        }
        return obj;
    }

private:
    struct ElfSectionHeaderView {
        uint32_t name_offset = 0;
        uint32_t type = 0;
        uint64_t flags = 0;
        uint64_t addr = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t link = 0;
        uint32_t info = 0;
        uint64_t addralign = 1;
        uint64_t entsize = 0;
    };

    static constexpr uint32_t kShtNull   = 0;
    static constexpr uint32_t kShtSymTab = 2;
    static constexpr uint32_t kShtStrTab = 3;
    static constexpr uint32_t kShtRela   = 4;
    static constexpr uint32_t kShtNoBits = 8;
    static constexpr uint32_t kShtRel    = 9;
    static constexpr uint32_t kShtDynSym = 11;
    static constexpr uint16_t kShnUndef  = 0;

    [[nodiscard]] static std::string read_c_string(const uint8_t* table, size_t table_size, uint32_t offset) {
        if (table == nullptr || offset >= table_size) {
            return {};
        }
        const char* begin = reinterpret_cast<const char*>(table + offset);
        const char* end = reinterpret_cast<const char*>(table + table_size);
        const char* p = begin;
        while (p < end && *p != '\0') {
            ++p;
        }
        return std::string(begin, static_cast<size_t>(p - begin));
    }

    [[nodiscard]] static std::vector<ElfSectionHeaderView> read_section_headers(
        const std::vector<uint8_t>& buffer,
        uint64_t sh_offset,
        uint16_t sh_entsize,
        uint16_t sh_num) {
        std::vector<ElfSectionHeaderView> headers;
        headers.reserve(sh_num);

        for (uint16_t i = 0; i < sh_num; ++i) {
            const uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) {
                break;
            }
            const uint8_t* sh = buffer.data() + offset;

            ElfSectionHeaderView view;
            std::memcpy(&view.name_offset, sh + 0, 4);
            std::memcpy(&view.type, sh + 4, 4);
            std::memcpy(&view.flags, sh + 8, 8);
            std::memcpy(&view.addr, sh + 16, 8);
            std::memcpy(&view.offset, sh + 24, 8);
            std::memcpy(&view.size, sh + 32, 8);
            std::memcpy(&view.link, sh + 40, 4);
            std::memcpy(&view.info, sh + 44, 4);
            std::memcpy(&view.addralign, sh + 48, 8);
            std::memcpy(&view.entsize, sh + 56, 8);
            headers.push_back(view);
        }
        return headers;
    }

    [[nodiscard]] static std::vector<std::string> build_section_name_table(
        const std::vector<uint8_t>& buffer,
        const std::vector<ElfSectionHeaderView>& headers,
        uint16_t shstrndx) {
        std::vector<std::string> names(headers.size());
        if (shstrndx >= headers.size()) {
            return names;
        }

        const auto& strtab = headers[shstrndx];
        if (strtab.type != kShtStrTab || strtab.offset + strtab.size > buffer.size()) {
            return names;
        }

        const uint8_t* table = buffer.data() + strtab.offset;
        const size_t table_size = static_cast<size_t>(strtab.size);
        for (size_t i = 0; i < headers.size(); ++i) {
            names[i] = read_c_string(table, table_size, headers[i].name_offset);
        }
        return names;
    }

    static bool parse_elf_object(const std::vector<uint8_t>& buffer, dgtl::object::ObjectFile& out) {
        if (buffer.size() < 64) {
            return false;
        }

        out.set_format(dgtl::object::ObjectFormat::ELF);

        uint64_t e_shoff = 0;
        uint16_t e_shnum = 0;
        uint16_t e_shentsize = 0;
        uint16_t e_shstrndx = 0;

        const bool is_64 = (buffer[4] == 2);
        if (!is_64) {
            uint32_t shoff32 = 0;
            std::memcpy(&shoff32, buffer.data() + 32, 4);
            e_shoff = shoff32;
            std::memcpy(&e_shentsize, buffer.data() + 46, 2);
            std::memcpy(&e_shnum, buffer.data() + 48, 2);
            std::memcpy(&e_shstrndx, buffer.data() + 50, 2);
        } else {
            std::memcpy(&e_shoff, buffer.data() + 40, 8);
            std::memcpy(&e_shentsize, buffer.data() + 58, 2);
            std::memcpy(&e_shnum, buffer.data() + 60, 2);
            std::memcpy(&e_shstrndx, buffer.data() + 62, 2);
        }

        if (e_shoff == 0 || e_shnum == 0 || e_shentsize < (is_64 ? 64 : 40)) {
            return false;
        }

        const auto section_headers = read_section_headers(buffer, e_shoff, e_shentsize, e_shnum);
        if (section_headers.empty()) {
            return false;
        }

        const auto section_names = build_section_name_table(buffer, section_headers, e_shstrndx);
        const auto header_to_out_sec = parse_sections(out, buffer, section_headers, section_names);
        const auto symbol_tables = parse_symbols(out, buffer, section_headers, header_to_out_sec);
        parse_relocations(out, buffer, section_headers, symbol_tables, header_to_out_sec);
        return true;
    }

    static std::unordered_map<size_t, uint32_t> parse_sections(
        dgtl::object::ObjectFile& out,
        const std::vector<uint8_t>& buffer,
        const std::vector<ElfSectionHeaderView>& headers,
        const std::vector<std::string>& section_names) noexcept {
        std::unordered_map<size_t, uint32_t> header_to_out_sec;
        for (size_t i = 0; i < headers.size(); ++i) {
            const auto& sh = headers[i];
            if (sh.type == kShtNull) {
                continue;
            }

            const std::string_view name = (i < section_names.size()) ? std::string_view(section_names[i]) : std::string_view{};
            if (name.empty() || name == ".symtab" || name == ".strtab" || name == ".shstrtab") {
                continue;
            }

            dgtl::object::SectionKind kind = dgtl::object::SectionKind::Text;
            if (name == ".text" || (sh.flags & 0x4u) != 0) {
                kind = dgtl::object::SectionKind::Text;
            } else if (name == ".data") {
                kind = dgtl::object::SectionKind::Data;
            } else if (name == ".rodata") {
                kind = dgtl::object::SectionKind::RData;
            } else if (name == ".bss" || sh.type == kShtNoBits) {
                kind = dgtl::object::SectionKind::BSS;
            } else {
                kind = dgtl::object::SectionKind::Custom;
            }

            uint32_t flags = static_cast<uint32_t>(sh.flags & 0xFFFFFFFFu);
            uint64_t alignment = (sh.addralign == 0) ? 16u : sh.addralign;

            uint32_t sec_index = static_cast<uint32_t>(out.sections().size());
            auto& sec = out.add_section(std::string(name), alignment, flags, kind);
            sec.address = sh.addr;

            if (sh.type == kShtNoBits) {
                sec.bss_size = sh.size;
            } else {
                if (sh.offset + sh.size <= buffer.size()) {
                    sec.bytes.assign(buffer.begin() + sh.offset, buffer.begin() + sh.offset + sh.size);
                }
            }
            header_to_out_sec[i] = sec_index;
        }
        return header_to_out_sec;
    }

    static std::unordered_map<uint32_t, std::vector<std::string>> parse_symbols(
        dgtl::object::ObjectFile& out,
        const std::vector<uint8_t>& buffer,
        const std::vector<ElfSectionHeaderView>& headers,
        const std::unordered_map<size_t, uint32_t>& header_to_out_sec) noexcept {
        std::unordered_map<uint32_t, std::vector<std::string>> symbol_tables;
        symbol_tables.reserve(2);

        for (uint32_t section_index = 0; section_index < static_cast<uint32_t>(headers.size()); ++section_index) {
            const auto& sh = headers[section_index];
            if (sh.type != kShtSymTab && sh.type != kShtDynSym) {
                continue;
            }
            if (sh.entsize < 24 || sh.size == 0 || sh.offset + sh.size > buffer.size() || sh.link >= headers.size()) {
                continue;
            }

            const auto& strtab = headers[sh.link];
            if (strtab.type != kShtStrTab || strtab.offset + strtab.size > buffer.size()) {
                continue;
            }

            const uint8_t* strtab_data = buffer.data() + strtab.offset;
            const size_t strtab_size = static_cast<size_t>(strtab.size);
            const uint64_t sym_count = sh.size / sh.entsize;

            auto& symbol_names = symbol_tables[section_index];
            symbol_names.clear();
            symbol_names.resize(static_cast<size_t>(sym_count));

            for (uint64_t j = 0; j < sym_count; ++j) {
                const uint64_t sym_entry_offset = sh.offset + (j * sh.entsize);
                if (sym_entry_offset + 24 > buffer.size()) {
                    break;
                }
                const uint8_t* sym = buffer.data() + sym_entry_offset;

                uint32_t st_name = 0;
                uint8_t st_info = 0;
                uint16_t st_shndx = 0;
                uint64_t st_value = 0;
                uint64_t st_size = 0;

                std::memcpy(&st_name, sym + 0, 4);
                std::memcpy(&st_info, sym + 4, 1);
                std::memcpy(&st_shndx, sym + 6, 2);
                std::memcpy(&st_value, sym + 8, 8);
                std::memcpy(&st_size, sym + 16, 8);

                std::string name = read_c_string(strtab_data, strtab_size, st_name);
                if (name.empty()) {
                    name = std::string("symbol_") + std::to_string(j);
                }

                symbol_names[static_cast<size_t>(j)] = name;

                const uint8_t binding = st_info >> 4;
                const uint8_t type = st_info & 0x0F;
                if (st_shndx == kShnUndef) {
                    out.add_external_symbol(name, binding == 0 ? 1 : binding, type);
                } else {
                    auto& sym_obj = out.add_symbol(name, st_value, st_size, binding, type);
                    auto it = header_to_out_sec.find(st_shndx);
                    if (it != header_to_out_sec.end()) {
                        sym_obj.section_index = it->second;
                    }
                }
            }
        }

        return symbol_tables;
    }

    static void parse_relocations(
        dgtl::object::ObjectFile& out,
        const std::vector<uint8_t>& buffer,
        const std::vector<ElfSectionHeaderView>& headers,
        const std::unordered_map<uint32_t, std::vector<std::string>>& symbol_tables,
        const std::unordered_map<size_t, uint32_t>& header_to_out_sec) noexcept {
        for (const auto& sh : headers) {
            const bool is_rela = (sh.type == kShtRela);
            if (sh.type != kShtRel && sh.type != kShtRela) {
                continue;
            }
            if (sh.entsize == 0 || sh.size == 0 || sh.offset + sh.size > buffer.size()) {
                continue;
            }

            uint32_t target_sec_index = 0;
            auto it_sec = header_to_out_sec.find(sh.info);
            if (it_sec != header_to_out_sec.end()) {
                target_sec_index = it_sec->second;
            }

            const uint64_t rel_count = sh.size / sh.entsize;
            for (uint64_t j = 0; j < rel_count; ++j) {
                const uint64_t rel_entry_offset = sh.offset + (j * sh.entsize);
                if (rel_entry_offset + 16 > buffer.size()) {
                    break;
                }

                const uint8_t* rel = buffer.data() + rel_entry_offset;
                uint64_t r_offset = 0;
                uint64_t r_info = 0;
                int64_t r_addend = 0;

                std::memcpy(&r_offset, rel + 0, 8);
                std::memcpy(&r_info, rel + 8, 8);
                if (is_rela && rel_entry_offset + 24 <= buffer.size()) {
                    std::memcpy(&r_addend, rel + 16, 8);
                }

                const uint32_t rel_type = static_cast<uint32_t>(r_info & 0xFFFFFFFFu);
                const uint32_t sym_index = static_cast<uint32_t>((r_info >> 32) & 0xFFFFFFFFu);

                dgtl::object::RelocationKind rk = dgtl::object::RelocationKind::Absolute64;
                if (rel_type == 1u) {
                    rk = dgtl::object::RelocationKind::PCRel32;
                } else if (rel_type == 2u) {
                    rk = dgtl::object::RelocationKind::PCRel32;
                } else if (rel_type == 4u) {
                    rk = dgtl::object::RelocationKind::PLT32;
                } else if (rel_type == 8u) {
                    rk = dgtl::object::RelocationKind::Relative64;
                }

                std::string symbol_name;
                const auto table_it = symbol_tables.find(sh.link);
                if (table_it != symbol_tables.end() &&
                    sym_index < table_it->second.size() &&
                    !table_it->second[sym_index].empty()) {
                    symbol_name = table_it->second[sym_index];
                } else {
                    symbol_name = std::string("symbol_") + std::to_string(sym_index);
                }

                dgtl::object::Relocation r(r_offset, std::move(symbol_name), r_addend, rel_type, rk);
                r.section_index = target_sec_index;
                r.symbol_index = sym_index;
                out.add_relocation(std::move(r));
            }
        }
    }
};

} // namespace dgtl::object::readers

export namespace dgtl::object {
    using ELFObjectReader = dgtl::object::readers::ELFObjectReader;
}
