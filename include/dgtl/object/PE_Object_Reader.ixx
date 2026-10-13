/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   PE/COFF Object & Executable Reader (Industrial Grade)
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
#include <algorithm>

export module dgtl.object.readers.pe;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;

export namespace dgtl::object::readers {

class PEObjectReader {
public:
    PEObjectReader() = default;
    ~PEObjectReader() = default;

    PEObjectReader(const PEObjectReader&) = delete;
    PEObjectReader& operator=(const PEObjectReader&) = delete;

    [[nodiscard]] static bool is_pe_format(const uint8_t* data, size_t size) noexcept {
        if (data == nullptr || size < 64) {
            return false;
        }
        // Check DOS 'MZ' Header
        if (data[0] != 0x4D || data[1] != 0x5A) {
            return false;
        }
        uint32_t e_lfanew = 0;
        std::memcpy(&e_lfanew, data + 60, 4);
        if (e_lfanew + 4 > size) {
            return false;
        }
        // Check "PE\0\0" (0x00004550)
        return data[e_lfanew] == 'P' && data[e_lfanew + 1] == 'E' &&
               data[e_lfanew + 2] == '\0' && data[e_lfanew + 3] == '\0';
    }

    [[nodiscard]] static bool is_coff_format(const uint8_t* data, size_t size) noexcept {
        if (data == nullptr || size < 20) {
            return false;
        }
        uint16_t machine = 0;
        std::memcpy(&machine, data, 2);
        // Standard COFF machines: AMD64 (0x8664), i386 (0x014C), ARM64 (0xAA64), ARM (0x01C4)
        if (machine == 0x8664 || machine == 0x014C || machine == 0xAA64 || machine == 0x01C4) {
            uint16_t opt_size = 0;
            std::memcpy(&opt_size, data + 16, 2);
            // In unlinked COFF object files, SizeOfOptionalHeader is 0
            if (opt_size == 0) return true;
        }
        return false;
    }

    [[nodiscard]] static bool is_supported_format(const uint8_t* data, size_t size) noexcept {
        return is_pe_format(data, size) || is_coff_format(data, size);
    }

    [[nodiscard]] static bool is_supported_format(const std::vector<uint8_t>& buffer) noexcept {
        return is_supported_format(buffer.data(), buffer.size());
    }

    [[nodiscard]] bool read(const void* data, size_t size, dgtl::object::ObjectFile& out) const {
        if (data == nullptr || size < 20) {
            return false;
        }
        const auto* byte_data = static_cast<const uint8_t*>(data);
        if (!is_supported_format(byte_data, size)) {
            return false;
        }
        std::vector<uint8_t> buffer(byte_data, byte_data + size);
        return parse_pe_coff_object(buffer, out);
    }

    [[nodiscard]] bool read_file(const std::string& filepath, dgtl::object::ObjectFile& out) const {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size < 20) {
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return false;
        }
        if (!is_supported_format(buffer)) {
            return false;
        }
        return parse_pe_coff_object(buffer, out);
    }

    [[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load(const std::string& filepath) const {
        auto obj = std::make_unique<dgtl::object::ObjectFile>(dgtl::object::ObjectFormat::PE);
        if (!read_file(filepath, *obj)) {
            return nullptr;
        }
        return obj;
    }

    [[nodiscard]] std::unique_ptr<dgtl::object::ObjectFile> load_from_memory(const void* data, size_t size) const {
        auto obj = std::make_unique<dgtl::object::ObjectFile>(dgtl::object::ObjectFormat::PE);
        if (!read(data, size, *obj)) {
            return nullptr;
        }
        return obj;
    }

private:
    struct SectionHeaderView {
        std::string name;
        uint32_t virtual_size = 0;
        uint32_t virtual_address = 0;
        uint32_t raw_data_size = 0;
        uint32_t raw_data_offset = 0;
        uint32_t reloc_offset = 0;
        uint16_t num_relocs = 0;
        uint32_t characteristics = 0;
    };

    struct CoffSymbolView {
        std::string name;
        uint32_t value = 0;
        int16_t section_number = 0;
        uint16_t type = 0;
        uint8_t storage_class = 0;
        uint8_t num_aux = 0;
    };

    static bool parse_pe_coff_object(const std::vector<uint8_t>& buffer, dgtl::object::ObjectFile& out) {
        const bool is_pe = is_pe_format(buffer.data(), buffer.size());
        out.set_format(is_pe ? dgtl::object::ObjectFormat::PE : dgtl::object::ObjectFormat::COFF);

        size_t coff_header_offset = 0;
        uint32_t export_dir_rva = 0;
        uint32_t export_dir_size = 0;
        uint32_t entry_point_rva = 0;

        if (is_pe) {
            uint32_t e_lfanew = 0;
            std::memcpy(&e_lfanew, buffer.data() + 60, 4);
            coff_header_offset = e_lfanew + 4; // Skip "PE\0\0"
        }

        if (coff_header_offset + 20 > buffer.size()) {
            return false;
        }

        const uint8_t* ch = buffer.data() + coff_header_offset;
        uint16_t num_sections = 0;
        uint32_t symtab_offset = 0;
        uint32_t num_symbols = 0;
        uint16_t opt_header_size = 0;

        std::memcpy(&num_sections, ch + 2, 2);
        std::memcpy(&symtab_offset, ch + 8, 4);
        std::memcpy(&num_symbols, ch + 12, 4);
        std::memcpy(&opt_header_size, ch + 16, 2);

        // Read Optional Header if present
        if (is_pe && opt_header_size >= 112) {
            const size_t opt_offset = coff_header_offset + 20;
            if (opt_offset + opt_header_size <= buffer.size()) {
                const uint8_t* opt = buffer.data() + opt_offset;
                uint16_t magic = 0;
                std::memcpy(&magic, opt, 2);
                std::memcpy(&entry_point_rva, opt + 16, 4);

                const size_t data_dirs_offset = (magic == 0x020B) ? 112 : 96; // PE32+ vs PE32
                if (opt_header_size >= data_dirs_offset + 8) {
                    std::memcpy(&export_dir_rva, opt + data_dirs_offset, 4);
                    std::memcpy(&export_dir_size, opt + data_dirs_offset + 4, 4);
                }
            }
        }

        // String table starts immediately after COFF symbol table
        const size_t string_table_offset = symtab_offset + (static_cast<size_t>(num_symbols) * 18);
        const auto get_string = [&](uint32_t str_offset) -> std::string {
            if (str_offset < 4 || string_table_offset + str_offset >= buffer.size()) {
                return {};
            }
            const char* start = reinterpret_cast<const char*>(buffer.data() + string_table_offset + str_offset);
            const char* end = reinterpret_cast<const char*>(buffer.data() + buffer.size());
            const char* p = start;
            while (p < end && *p != '\0') ++p;
            return std::string(start, static_cast<size_t>(p - start));
        };

        // Section Headers
        const size_t section_headers_offset = coff_header_offset + 20 + opt_header_size;
        if (section_headers_offset + (static_cast<size_t>(num_sections) * 40) > buffer.size()) {
            return false;
        }

        std::vector<SectionHeaderView> section_headers;
        section_headers.reserve(num_sections);

        for (uint16_t i = 0; i < num_sections; ++i) {
            const size_t sh_off = section_headers_offset + (static_cast<size_t>(i) * 40);
            const uint8_t* sh = buffer.data() + sh_off;

            SectionHeaderView view;
            char short_name[9] = {0};
            std::memcpy(short_name, sh, 8);

            if (short_name[0] == '/') {
                uint32_t str_idx = static_cast<uint32_t>(std::strtoul(short_name + 1, nullptr, 10));
                view.name = get_string(str_idx);
            } else {
                view.name = short_name;
            }

            std::memcpy(&view.virtual_size, sh + 8, 4);
            std::memcpy(&view.virtual_address, sh + 12, 4);
            std::memcpy(&view.raw_data_size, sh + 16, 4);
            std::memcpy(&view.raw_data_offset, sh + 20, 4);
            std::memcpy(&view.reloc_offset, sh + 24, 4);
            std::memcpy(&view.num_relocs, sh + 32, 2);
            std::memcpy(&view.characteristics, sh + 36, 4);

            section_headers.push_back(std::move(view));
        }

        // RVA to File Offset Helper for PE
        auto rva_to_offset = [&](uint32_t rva) -> size_t {
            for (const auto& sh : section_headers) {
                const uint32_t va_size = (sh.virtual_size != 0) ? sh.virtual_size : sh.raw_data_size;
                if (rva >= sh.virtual_address && rva < sh.virtual_address + va_size) {
                    return sh.raw_data_offset + (rva - sh.virtual_address);
                }
            }
            return 0;
        };

        // Populate Sections in ObjectFile
        for (const auto& sh : section_headers) {
            dgtl::object::SectionKind kind = dgtl::object::SectionKind::Custom;
            uint32_t flags = 0;

            if (sh.characteristics & 0x00000020) flags |= dgtl::object::SectionFlags::Code;
            if (sh.characteristics & 0x40000000) flags |= dgtl::object::SectionFlags::Read;
            if (sh.characteristics & 0x80000000) flags |= dgtl::object::SectionFlags::Write;
            if (sh.characteristics & 0x20000000) flags |= dgtl::object::SectionFlags::Executable;
            if (sh.characteristics & 0x00000080) flags |= dgtl::object::SectionFlags::ZeroFill;

            if (sh.name == ".text" || (flags & dgtl::object::SectionFlags::Code)) {
                kind = dgtl::object::SectionKind::Text;
            } else if (sh.name == ".data") {
                kind = dgtl::object::SectionKind::Data;
            } else if (sh.name == ".rdata" || sh.name == ".rodata") {
                kind = dgtl::object::SectionKind::RData;
            } else if (sh.name == ".bss" || (flags & dgtl::object::SectionFlags::ZeroFill)) {
                kind = dgtl::object::SectionKind::BSS;
            } else if (sh.name == ".pdata") {
                kind = dgtl::object::SectionKind::ExceptionTable;
            } else if (sh.name == ".xdata") {
                kind = dgtl::object::SectionKind::ExceptionInfo;
            } else if (sh.name.starts_with(".tls")) {
                kind = dgtl::object::SectionKind::TLS;
            }

            uint32_t align_bits = (sh.characteristics >> 20) & 0x0F;
            uint64_t alignment = (align_bits > 0) ? (1ULL << (align_bits - 1)) : 16ULL;

            auto& sec = out.add_section(sh.name, alignment, flags, kind);
            sec.address = sh.virtual_address;
            const uint32_t sec_size = (sh.virtual_size > 0) ? sh.virtual_size : sh.raw_data_size;
            sec.bss_size = (kind == dgtl::object::SectionKind::BSS) ? sec_size : 0;

            if (kind != dgtl::object::SectionKind::BSS &&
                sh.raw_data_offset != 0 && sh.raw_data_size > 0 &&
                sh.raw_data_offset + sh.raw_data_size <= buffer.size()) {
                sec.append_bytes(buffer.data() + sh.raw_data_offset, sh.raw_data_size);
            }
        }

        // Decode COFF Symbol Table
        std::vector<CoffSymbolView> parsed_symbols;
        if (symtab_offset != 0 && symtab_offset + (static_cast<size_t>(num_symbols) * 18) <= buffer.size()) {
            parsed_symbols.resize(num_symbols);
            for (uint32_t i = 0; i < num_symbols; ++i) {
                const size_t soff = symtab_offset + (static_cast<size_t>(i) * 18);
                const uint8_t* sptr = buffer.data() + soff;

                CoffSymbolView sym;
                uint32_t zero_check = 0;
                std::memcpy(&zero_check, sptr, 4);

                if (zero_check == 0) {
                    uint32_t str_off = 0;
                    std::memcpy(&str_off, sptr + 4, 4);
                    sym.name = get_string(str_off);
                } else {
                    char sname[9] = {0};
                    std::memcpy(sname, sptr, 8);
                    sym.name = sname;
                }

                std::memcpy(&sym.value, sptr + 8, 4);
                std::memcpy(&sym.section_number, sptr + 12, 2);
                std::memcpy(&sym.type, sptr + 14, 2);
                sym.storage_class = sptr[16];
                sym.num_aux = sptr[17];

                if (sym.name.empty() && sym.section_number > 0 &&
                    static_cast<size_t>(sym.section_number) <= section_headers.size()) {
                    sym.name = section_headers[static_cast<size_t>(sym.section_number - 1)].name;
                }

                if (!sym.name.empty()) {
                    const bool is_func = (sym.type == 0x20);
                    const bool is_global = (sym.storage_class == 2);
                    const bool is_ext = (sym.section_number == 0);

                    if (is_ext) {
                        out.add_external_symbol(sym.name, is_global ? 1 : 0, is_func ? 2 : 0);
                    } else if (is_func) {
                        auto& s = out.add_function_symbol(sym.name, sym.value, 0, is_global);
                        if (sym.section_number > 0) {
                            s.section_index = static_cast<uint32_t>(sym.section_number - 1);
                        }
                    } else {
                        auto& s = out.add_symbol(sym.name, sym.value, 0, is_global ? 1 : 0, 0);
                        if (sym.section_number > 0) {
                            s.section_index = static_cast<uint32_t>(sym.section_number - 1);
                        }
                    }
                }

                parsed_symbols[i] = sym;
                for (uint8_t a = 1; a <= sym.num_aux && (i + a) < num_symbols; ++a) {
                    parsed_symbols[i + a] = CoffSymbolView{};
                }
                i += sym.num_aux;
            }
        }

        // Decode COFF Relocations
        for (size_t sec_idx = 0; sec_idx < section_headers.size(); ++sec_idx) {
            const auto& sh = section_headers[sec_idx];
            if (sh.reloc_offset != 0 && sh.num_relocs > 0 &&
                sh.reloc_offset + (static_cast<size_t>(sh.num_relocs) * 10) <= buffer.size()) {
                for (uint16_t r = 0; r < sh.num_relocs; ++r) {
                    const size_t roff = sh.reloc_offset + (static_cast<size_t>(r) * 10);
                    const uint8_t* rptr = buffer.data() + roff;

                    uint32_t rva = 0;
                    uint32_t sym_idx = 0;
                    uint16_t rtype = 0;

                    std::memcpy(&rva, rptr, 4);
                    std::memcpy(&sym_idx, rptr + 4, 4);
                    std::memcpy(&rtype, rptr + 8, 2);

                    std::string target_sym;
                    if (sym_idx < parsed_symbols.size()) {
                        target_sym = parsed_symbols[sym_idx].name;
                    }

                    // Extract in-place addend from section raw data
                    int64_t in_place_addend = 0;
                    if (sh.raw_data_offset != 0 && rva < sh.raw_data_size) {
                        const size_t place_in_buf = sh.raw_data_offset + rva;
                        if (rtype == 0x0001) { // IMAGE_REL_AMD64_ADDR64
                            if (place_in_buf + 8 <= buffer.size()) {
                                std::memcpy(&in_place_addend, buffer.data() + place_in_buf, 8);
                            }
                        } else {
                            if (place_in_buf + 4 <= buffer.size()) {
                                int32_t add32 = 0;
                                std::memcpy(&add32, buffer.data() + place_in_buf, 4);
                                in_place_addend = add32;
                            }
                        }
                    }

                    dgtl::object::RelocationKind kind = dgtl::object::RelocationKind::PCRel32;
                    switch (rtype) {
                        case 0x0001: // IMAGE_REL_AMD64_ADDR64
                            kind = dgtl::object::RelocationKind::Absolute64;
                            break;
                        case 0x0002: // IMAGE_REL_AMD64_ADDR32
                            kind = dgtl::object::RelocationKind::Absolute32;
                            break;
                        case 0x0003: // IMAGE_REL_AMD64_ADDR32NB
                        case 0x000B: // IMAGE_REL_AMD64_SECREL
                            kind = dgtl::object::RelocationKind::SecRel32;
                            break;
                        case 0x0004: // IMAGE_REL_AMD64_REL32
                        case 0x0005: // IMAGE_REL_AMD64_REL32_1
                        case 0x0006: // IMAGE_REL_AMD64_REL32_2
                        case 0x0007: // IMAGE_REL_AMD64_REL32_3
                        case 0x0008: // IMAGE_REL_AMD64_REL32_4
                        case 0x0009: // IMAGE_REL_AMD64_REL32_5
                            kind = dgtl::object::RelocationKind::PCRel32;
                            break;
                        default:
                            kind = dgtl::object::RelocationKind::PCRel32;
                            break;
                    }

                    int64_t universal_addend = in_place_addend;
                    if (rtype >= 0x0004 && rtype <= 0x0009) {
                        const int32_t extra = static_cast<int32_t>(rtype - 0x0004);
                        universal_addend -= (4 + extra);
                    }

                    dgtl::object::Relocation rel(rva, std::move(target_sym), universal_addend, rtype, kind, static_cast<uint32_t>(sec_idx));
                    rel.symbol_index = sym_idx;
                    out.add_relocation(std::move(rel));
                }
            }
        }

        // Decode PE Export Directory Table (if executable/DLL has exports)
        if (is_pe && export_dir_rva != 0 && export_dir_size > 0) {
            const size_t exp_offset = rva_to_offset(export_dir_rva);
            if (exp_offset != 0 && exp_offset + 40 <= buffer.size()) {
                const uint8_t* exp = buffer.data() + exp_offset;
                uint32_t num_funcs = 0;
                uint32_t num_names = 0;
                uint32_t funcs_rva = 0;
                uint32_t names_rva = 0;
                uint32_t ords_rva = 0;

                std::memcpy(&num_funcs, exp + 20, 4);
                std::memcpy(&num_names, exp + 24, 4);
                std::memcpy(&funcs_rva, exp + 28, 4);
                std::memcpy(&names_rva, exp + 32, 4);
                std::memcpy(&ords_rva, exp + 36, 4);

                const size_t names_offset = rva_to_offset(names_rva);
                const size_t funcs_offset = rva_to_offset(funcs_rva);
                const size_t ords_offset = rva_to_offset(ords_rva);

                if (names_offset != 0 && funcs_offset != 0 && ords_offset != 0) {
                    for (uint32_t n = 0; n < num_names; ++n) {
                        if (names_offset + (n * 4) + 4 > buffer.size() ||
                            ords_offset + (n * 2) + 2 > buffer.size()) break;

                        uint32_t name_rva = 0;
                        uint16_t ordinal = 0;
                        std::memcpy(&name_rva, buffer.data() + names_offset + (n * 4), 4);
                        std::memcpy(&ordinal, buffer.data() + ords_offset + (n * 2), 2);

                        if (ordinal >= num_funcs) continue;
                        if (funcs_offset + (ordinal * 4) + 4 > buffer.size()) continue;

                        uint32_t func_rva = 0;
                        std::memcpy(&func_rva, buffer.data() + funcs_offset + (ordinal * 4), 4);

                        const size_t str_off = rva_to_offset(name_rva);
                        if (str_off != 0 && str_off < buffer.size()) {
                            const char* s_begin = reinterpret_cast<const char*>(buffer.data() + str_off);
                            const char* s_end = reinterpret_cast<const char*>(buffer.data() + buffer.size());
                            const char* p = s_begin;
                            while (p < s_end && *p != '\0') ++p;
                            std::string exp_name(s_begin, static_cast<size_t>(p - s_begin));

                            if (!out.find_symbol(exp_name)) {
                                out.add_function_symbol(exp_name, func_rva, 0, true);
                            }
                        }
                    }
                }
            }
        }

        // If PE entry point RVA is present, register entry point symbol
        if (is_pe && entry_point_rva != 0) {
            if (!out.find_symbol("main") && !out.find_symbol("DGTL_kernel_entry")) {
                out.add_function_symbol("DGTL_kernel_entry", entry_point_rva, 0, true);
            }
        }

        return true;
    }
};

} // namespace dgtl::object::readers

export namespace dgtl::backend::object {
    using PEObjectReader = dgtl::object::readers::PEObjectReader;
}
