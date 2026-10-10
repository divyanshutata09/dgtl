/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Universal Sovereign Native Static Archiver (Writer & Reader)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 * Module:      dgtl.object.archive
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module dgtl.object.archive;

import dgtl.object.file;
import dgtl.object.symbol;
import dgtl.object.section;
import dgtl.object.relocation;
import dgtl.object.readers.pe;
import dgtl.object.readers.elf;

export namespace dgtl::object::archive {

// ─────────────────────────────────────────────────────────────────────────────
// AR Common Structures & Constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr char kArGlobalMagic[8] = {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'};

#pragma pack(push, 1)
struct ArHeaderRaw {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char fmag[2]; // `\n
};
#pragma pack(pop)

struct ArchiveMember {
    std::string filename;
    std::vector<uint8_t> data;
    std::vector<std::string> symbols;
    uint32_t header_offset = 0;
    uint32_t data_offset = 0;
};

struct ArchiveResult {
    bool ok = false;
    std::string error_message;
    std::size_t members_count = 0;
    std::size_t symbols_count = 0;
    std::size_t total_bytes = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Low-Level Byte Helpers
// ─────────────────────────────────────────────────────────────────────────────

inline uint16_t read_le_u16(const uint8_t* ptr) noexcept {
    return static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
}

inline int16_t read_le_i16(const uint8_t* ptr) noexcept {
    return static_cast<int16_t>(read_le_u16(ptr));
}

inline uint32_t read_le_u32(const uint8_t* ptr) noexcept {
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

inline uint64_t read_le_u64(const uint8_t* ptr) noexcept {
    return static_cast<uint64_t>(read_le_u32(ptr)) |
          (static_cast<uint64_t>(read_le_u32(ptr + 4)) << 32);
}

inline uint32_t read_be_u32(const uint8_t* ptr) noexcept {
    return (static_cast<uint32_t>(ptr[0]) << 24) |
           (static_cast<uint32_t>(ptr[1]) << 16) |
           (static_cast<uint32_t>(ptr[2]) << 8) |
           static_cast<uint32_t>(ptr[3]);
}

inline void write_be_u32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

inline void format_ar_field(char* dst, std::size_t max_len, const std::string& val) {
    std::size_t len = std::min(val.size(), max_len);
    std::memcpy(dst, val.data(), len);
    if (len < max_len) {
        std::memset(dst + len, ' ', max_len - len);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Universal Binary Symbol Extractors (COFF, ELF, Mach-O)
// ─────────────────────────────────────────────────────────────────────────────

inline void extract_coff_symbols(const std::vector<uint8_t>& data, std::vector<std::string>& out_symbols) {
    if (data.size() < 20) return;

    const uint8_t* raw = data.data();
    const uint32_t sym_ptr = read_le_u32(raw + 8);
    const uint32_t num_syms = read_le_u32(raw + 12);

    if (sym_ptr == 0 || num_syms == 0 || sym_ptr >= data.size()) {
        return;
    }

    const uint64_t sym_tab_bytes = static_cast<uint64_t>(num_syms) * 18ULL;
    const uint64_t sym_tab_end = static_cast<uint64_t>(sym_ptr) + sym_tab_bytes;
    if (sym_tab_end > data.size()) return;

    const uint8_t* str_tab = nullptr;
    uint32_t str_tab_len = 0;
    if (sym_tab_end + 4 <= data.size()) {
        str_tab = raw + sym_tab_end;
        str_tab_len = read_le_u32(str_tab);
        if (sym_tab_end + str_tab_len > data.size()) {
            str_tab_len = static_cast<uint32_t>(data.size() - sym_tab_end);
        }
    }

    for (uint32_t i = 0; i < num_syms; ) {
        const uint8_t* entry = raw + sym_ptr + (static_cast<uint64_t>(i) * 18ULL);
        const int16_t sec_num = read_le_i16(entry + 12);
        const uint8_t storage_class = entry[16];
        const uint8_t num_aux = entry[17];

        if ((storage_class == 2 || storage_class == 107) && sec_num != 0) {
            std::string name;
            const uint32_t zero_field = read_le_u32(entry);
            if (zero_field == 0) {
                const uint32_t str_off = read_le_u32(entry + 4);
                if (str_tab && str_off < str_tab_len) {
                    name = reinterpret_cast<const char*>(str_tab + str_off);
                }
            } else {
                char short_name[9] = {0};
                std::memcpy(short_name, entry, 8);
                name = short_name;
            }

            if (!name.empty() && name != ".file" && !name.starts_with(".debug") && !name.starts_with(".drectve")) {
                out_symbols.push_back(std::move(name));
            }
        }
        i += 1 + num_aux;
    }
}

inline void extract_elf_symbols(const std::vector<uint8_t>& data, std::vector<std::string>& out_symbols) {
    if (data.size() < 64) return;
    const uint8_t* raw = data.data();
    if (raw[0] != 0x7F || raw[1] != 'E' || raw[2] != 'L' || raw[3] != 'F') return;

    const bool is_64 = (raw[4] == 2);
    if (!is_64) return;

    const uint64_t shoff = read_le_u64(raw + 40);
    const uint16_t shentsize = read_le_u16(raw + 58);
    const uint16_t shnum = read_le_u16(raw + 60);

    if (shoff == 0 || shentsize < 64 || shnum == 0) return;
    if (shoff + static_cast<uint64_t>(shnum) * shentsize > data.size()) return;

    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* shdr = raw + shoff + (static_cast<uint64_t>(i) * shentsize);
        const uint32_t sh_type = read_le_u32(shdr + 4);
        if (sh_type == 2) { // SHT_SYMTAB
            const uint64_t sym_off = read_le_u64(shdr + 24);
            const uint64_t sym_size = read_le_u64(shdr + 32);
            const uint32_t str_link = read_le_u32(shdr + 40);
            const uint64_t entsize = read_le_u64(shdr + 56);

            if (entsize == 0 || str_link >= shnum) continue;
            const uint8_t* str_shdr = raw + shoff + (static_cast<uint64_t>(str_link) * shentsize);
            const uint64_t str_off = read_le_u64(str_shdr + 24);
            const uint64_t str_size = read_le_u64(str_shdr + 32);

            if (sym_off + sym_size > data.size() || str_off + str_size > data.size()) continue;

            const uint8_t* str_table = raw + str_off;
            const uint64_t count = sym_size / entsize;

            for (uint64_t s = 0; s < count; ++s) {
                const uint8_t* sym = raw + sym_off + (s * entsize);
                const uint32_t st_name = read_le_u32(sym);
                const uint8_t st_info = sym[4];
                const uint16_t st_shndx = read_le_u16(sym + 6);

                const uint8_t bind = st_info >> 4;
                if ((bind == 1 || bind == 2) && st_shndx != 0 && st_name < str_size) {
                    const char* name = reinterpret_cast<const char*>(str_table + st_name);
                    if (name && name[0] != '\0') {
                        out_symbols.push_back(name);
                    }
                }
            }
        }
    }
}

inline void extract_macho_symbols(const std::vector<uint8_t>& data, std::vector<std::string>& out_symbols) {
    if (data.size() < 32) return;
    const uint8_t* raw = data.data();
    const uint32_t magic = read_le_u32(raw);

    const bool is_macho64 = (magic == 0xFEEDFACF || magic == 0xCFFAEDFE);
    if (!is_macho64) return;

    const uint32_t ncmds = read_le_u32(raw + 16);
    uint32_t offset = 32;

    for (uint32_t c = 0; c < ncmds && offset + 8 <= data.size(); ++c) {
        const uint32_t cmd = read_le_u32(raw + offset);
        const uint32_t cmdsize = read_le_u32(raw + offset + 4);
        if (cmdsize == 0) break;

        if (cmd == 0x02) { // LC_SYMTAB
            if (offset + 24 > data.size()) break;
            const uint32_t symoff = read_le_u32(raw + offset + 8);
            const uint32_t nsyms = read_le_u32(raw + offset + 12);
            const uint32_t stroff = read_le_u32(raw + offset + 16);
            const uint32_t strsize = read_le_u32(raw + offset + 20);

            if (symoff + static_cast<uint64_t>(nsyms) * 16ULL <= data.size() &&
                stroff + strsize <= data.size()) {
                const uint8_t* str_table = raw + stroff;
                for (uint32_t s = 0; s < nsyms; ++s) {
                    const uint8_t* nlist = raw + symoff + (s * 16);
                    const uint32_t strx = read_le_u32(nlist);
                    const uint8_t n_type = nlist[4];
                    const uint8_t n_sect = nlist[5];

                    if ((n_type & 0x01) && n_sect != 0 && strx < strsize) {
                        const char* sym_name = reinterpret_cast<const char*>(str_table + strx);
                        if (sym_name && sym_name[0] != '\0') {
                            if (sym_name[0] == '_') {
                                out_symbols.push_back(sym_name + 1);
                            } else {
                                out_symbols.push_back(sym_name);
                            }
                        }
                    }
                }
            }
            break;
        }
        offset += cmdsize;
    }
}

inline void extract_universal_symbols(const std::vector<uint8_t>& data, std::vector<std::string>& out_symbols) {
    if (data.size() >= 4) {
        if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            extract_elf_symbols(data, out_symbols);
            return;
        }
        const uint32_t magic = read_le_u32(data.data());
        if (magic == 0xFEEDFACF || magic == 0xCFFAEDFE || magic == 0xFEEDFACE || magic == 0xCEFAEDFE) {
            extract_macho_symbols(data, out_symbols);
            return;
        }
    }
    extract_coff_symbols(data, out_symbols);
}

// ─────────────────────────────────────────────────────────────────────────────
// Universal Archive Writer (Packer)
// ─────────────────────────────────────────────────────────────────────────────

inline ArchiveResult create_static_archive_from_memory(
    const std::string& archive_output_path,
    const std::vector<ArchiveMember>& members) {

    ArchiveResult result;
    if (members.empty()) {
        result.error_message = "No object members provided for archive creation";
        return result;
    }

    std::string long_names_data;
    std::vector<std::size_t> long_name_offsets(members.size(), 0);

    for (std::size_t idx = 0; idx < members.size(); ++idx) {
        const auto& m = members[idx];
        if (m.filename.size() > 15) {
            long_name_offsets[idx] = long_names_data.size();
            long_names_data.append(m.filename);
            long_names_data.append("/\n");
        }
    }

    struct SymbolEntry {
        std::string name;
        std::size_t member_index;
    };
    std::vector<SymbolEntry> all_symbols;
    std::unordered_set<std::string> seen_symbols;

    for (std::size_t idx = 0; idx < members.size(); ++idx) {
        for (const auto& sym : members[idx].symbols) {
            if (seen_symbols.insert(sym).second) {
                all_symbols.push_back(SymbolEntry{sym, idx});
            }
        }
    }

    std::sort(all_symbols.begin(), all_symbols.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) { return a.name < b.name; });

    const std::size_t global_hdr_len = 8;
    const std::size_t member_hdr_len = 60;

    std::size_t sym_str_len = 0;
    for (const auto& s : all_symbols) {
        sym_str_len += s.name.size() + 1;
    }

    const std::size_t sym_dir_body_len = 4 + (all_symbols.size() * 4) + sym_str_len;
    const std::size_t sym_dir_total_len = member_hdr_len + sym_dir_body_len + (sym_dir_body_len % 2);

    std::size_t long_names_total_len = 0;
    if (!long_names_data.empty()) {
        long_names_total_len = member_hdr_len + long_names_data.size() + (long_names_data.size() % 2);
    }

    std::vector<uint32_t> member_offsets(members.size(), 0);
    uint32_t current_offset = static_cast<uint32_t>(global_hdr_len + sym_dir_total_len + long_names_total_len);

    for (std::size_t idx = 0; idx < members.size(); ++idx) {
        member_offsets[idx] = current_offset;
        const std::size_t data_len = members[idx].data.size();
        current_offset += static_cast<uint32_t>(member_hdr_len + data_len + (data_len % 2));
    }

    std::vector<uint8_t> sym_dir_body;
    sym_dir_body.reserve(sym_dir_body_len);
    write_be_u32(sym_dir_body, static_cast<uint32_t>(all_symbols.size()));
    for (const auto& s : all_symbols) {
        const uint32_t m_off = member_offsets[s.member_index];
        write_be_u32(sym_dir_body, m_off);
    }
    for (const auto& s : all_symbols) {
        for (char c : s.name) {
            sym_dir_body.push_back(static_cast<uint8_t>(c));
        }
        sym_dir_body.push_back(0);
    }

    std::filesystem::path out_p(archive_output_path);
    if (out_p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_p.parent_path(), ec);
    }

    std::ofstream out(archive_output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error_message = "Failed to create archive file: " + archive_output_path;
        return result;
    }

    out.write(kArGlobalMagic, sizeof(kArGlobalMagic));

    ArHeaderRaw sym_hdr{};
    format_ar_field(sym_hdr.name, sizeof(sym_hdr.name), "/");
    format_ar_field(sym_hdr.date, sizeof(sym_hdr.date), "0");
    format_ar_field(sym_hdr.uid,  sizeof(sym_hdr.uid),  "0");
    format_ar_field(sym_hdr.gid,  sizeof(sym_hdr.gid),  "0");
    format_ar_field(sym_hdr.mode, sizeof(sym_hdr.mode), "0");
    format_ar_field(sym_hdr.size, sizeof(sym_hdr.size), std::to_string(sym_dir_body.size()));
    sym_hdr.fmag[0] = '`';
    sym_hdr.fmag[1] = '\n';
    out.write(reinterpret_cast<const char*>(&sym_hdr), sizeof(sym_hdr));
    out.write(reinterpret_cast<const char*>(sym_dir_body.data()), sym_dir_body.size());
    if (sym_dir_body.size() % 2 != 0) {
        char pad = '\n';
        out.write(&pad, 1);
    }

    if (!long_names_data.empty()) {
        ArHeaderRaw ln_hdr{};
        format_ar_field(ln_hdr.name, sizeof(ln_hdr.name), "//");
        format_ar_field(ln_hdr.date, sizeof(ln_hdr.date), "0");
        format_ar_field(ln_hdr.uid,  sizeof(ln_hdr.uid),  "0");
        format_ar_field(ln_hdr.gid,  sizeof(ln_hdr.gid),  "0");
        format_ar_field(ln_hdr.mode, sizeof(ln_hdr.mode), "0");
        format_ar_field(ln_hdr.size, sizeof(ln_hdr.size), std::to_string(long_names_data.size()));
        ln_hdr.fmag[0] = '`';
        ln_hdr.fmag[1] = '\n';
        out.write(reinterpret_cast<const char*>(&ln_hdr), sizeof(ln_hdr));
        out.write(long_names_data.data(), long_names_data.size());
        if (long_names_data.size() % 2 != 0) {
            char pad = '\n';
            out.write(&pad, 1);
        }
    }

    for (std::size_t idx = 0; idx < members.size(); ++idx) {
        const auto& m = members[idx];
        ArHeaderRaw m_hdr{};

        std::string hdr_name;
        if (m.filename.size() <= 15) {
            hdr_name = m.filename + "/";
        } else {
            hdr_name = "/" + std::to_string(long_name_offsets[idx]);
        }

        format_ar_field(m_hdr.name, sizeof(m_hdr.name), hdr_name);
        format_ar_field(m_hdr.date, sizeof(m_hdr.date), "0");
        format_ar_field(m_hdr.uid,  sizeof(m_hdr.uid),  "0");
        format_ar_field(m_hdr.gid,  sizeof(m_hdr.gid),  "0");
        format_ar_field(m_hdr.mode, sizeof(m_hdr.mode), "644");
        format_ar_field(m_hdr.size, sizeof(m_hdr.size), std::to_string(m.data.size()));
        m_hdr.fmag[0] = '`';
        m_hdr.fmag[1] = '\n';

        out.write(reinterpret_cast<const char*>(&m_hdr), sizeof(m_hdr));
        out.write(reinterpret_cast<const char*>(m.data.data()), m.data.size());
        if (m.data.size() % 2 != 0) {
            char pad = '\n';
            out.write(&pad, 1);
        }
    }

    out.flush();
    const std::size_t final_size = static_cast<std::size_t>(out.tellp());
    out.close();

    result.ok = true;
    result.members_count = members.size();
    result.symbols_count = all_symbols.size();
    result.total_bytes = final_size;
    return result;
}

inline ArchiveResult create_static_archive(
    const std::string& archive_output_path,
    const std::vector<std::string>& object_paths) {

    ArchiveResult result;
    if (object_paths.empty()) {
        result.error_message = "No object paths specified for archive creation";
        return result;
    }

    std::vector<std::string> expanded_paths;
    for (const auto& p_str : object_paths) {
        if (!p_str.empty() && p_str.front() == '@') {
            std::string rsp_file = p_str.substr(1);
            std::ifstream rsp_in(rsp_file);
            if (!rsp_in) {
                result.error_message = "Response file not found: " + rsp_file;
                return result;
            }
            std::string line;
            while (std::getline(rsp_in, line)) {
                std::istringstream iss(line);
                std::string item;
                while (iss >> item) {
                    if (!item.empty()) expanded_paths.push_back(item);
                }
            }
        } else {
            expanded_paths.push_back(p_str);
        }
    }

    std::vector<ArchiveMember> members;
    members.reserve(expanded_paths.size());

    for (const auto& path_str : expanded_paths) {
        std::filesystem::path p(path_str);
        if (!std::filesystem::exists(p)) {
            result.error_message = "Object file not found: " + path_str;
            return result;
        }

        std::ifstream in(path_str, std::ios::binary);
        if (!in) {
            result.error_message = "Could not open object file for reading: " + path_str;
            return result;
        }

        in.seekg(0, std::ios::end);
        const std::size_t sz = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        ArchiveMember m;
        m.filename = p.filename().string();
        m.data.resize(sz);
        if (sz > 0) {
            in.read(reinterpret_cast<char*>(m.data.data()), sz);
        }
        in.close();

        extract_universal_symbols(m.data, m.symbols);
        members.push_back(std::move(m));
    }

    return create_static_archive_from_memory(archive_output_path, members);
}

struct UniversalArchiveWriter {
    static ArchiveResult create_static_archive(const std::string& archive_output_path,
                                               const std::vector<std::string>& object_paths) {
        return dgtl::object::archive::create_static_archive(archive_output_path, object_paths);
    }
    static ArchiveResult create_static_archive_from_memory(const std::string& archive_output_path,
                                                          const std::vector<ArchiveMember>& members) {
        return dgtl::object::archive::create_static_archive_from_memory(archive_output_path, members);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Universal Archive Reader & Extractor (Unpacker)
// ─────────────────────────────────────────────────────────────────────────────

class UniversalArchiveReader {
    std::vector<uint8_t> raw_bytes_;
    std::string long_names_str_;
    std::unordered_map<std::string, uint32_t> symbol_to_member_offset_;
    std::vector<ArchiveMember> members_;
    bool loaded_ = false;

public:
    UniversalArchiveReader() = default;

    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }

    bool load_archive(const std::string& archive_path) {
        raw_bytes_.clear();
        long_names_str_.clear();
        symbol_to_member_offset_.clear();
        members_.clear();
        loaded_ = false;

        std::ifstream in(archive_path, std::ios::binary);
        if (!in) return false;

        in.seekg(0, std::ios::end);
        const std::size_t sz = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        if (sz < 8) return false;
        raw_bytes_.resize(sz);
        in.read(reinterpret_cast<char*>(raw_bytes_.data()), sz);
        in.close();

        return parse_archive();
    }

    bool load_archive_from_memory(const uint8_t* data, size_t size) {
        raw_bytes_.clear();
        long_names_str_.clear();
        symbol_to_member_offset_.clear();
        members_.clear();
        loaded_ = false;

        if (!data || size < 8) return false;
        raw_bytes_.assign(data, data + size);
        return parse_archive();
    }

    [[nodiscard]] bool has_symbol(std::string_view sym) const {
        return symbol_to_member_offset_.contains(std::string(sym));
    }

    [[nodiscard]] const std::unordered_map<std::string, uint32_t>& symbol_map() const noexcept {
        return symbol_to_member_offset_;
    }

    [[nodiscard]] const std::vector<ArchiveMember>& members() const noexcept {
        return members_;
    }

    [[nodiscard]] std::optional<std::span<const uint8_t>> extract_member_for_symbol(std::string_view sym) const {
        auto it = symbol_to_member_offset_.find(std::string(sym));
        if (it == symbol_to_member_offset_.end()) return std::nullopt;

        const uint32_t header_off = it->second;
        if (header_off + sizeof(ArHeaderRaw) > raw_bytes_.size()) return std::nullopt;

        const auto* hdr = reinterpret_cast<const ArHeaderRaw*>(raw_bytes_.data() + header_off);
        char size_buf[11] = {0};
        std::memcpy(size_buf, hdr->size, 10);
        const size_t member_size = std::strtoul(size_buf, nullptr, 10);
        const size_t data_off = header_off + sizeof(ArHeaderRaw);

        if (data_off + member_size > raw_bytes_.size()) return std::nullopt;
        return std::span<const uint8_t>(raw_bytes_.data() + data_off, member_size);
    }

    bool extract_object_file_for_symbol(std::string_view sym, dgtl::object::ObjectFile& out_obj) const {
        auto member_span = extract_member_for_symbol(sym);
        if (!member_span.has_value()) return false;

        const auto& span = *member_span;
        if (span.size() < 4) return false;

        // Try PE/COFF
        dgtl::object::readers::PEObjectReader pe_reader;
        dgtl::object::ObjectFile pe_obj(dgtl::object::ObjectFormat::PE);
        if (pe_reader.read(span.data(), span.size(), pe_obj)) {
            out_obj = std::move(pe_obj);
            return true;
        }

        // Try ELF
        dgtl::object::readers::ELFObjectReader elf_reader;
        dgtl::object::ObjectFile elf_obj(dgtl::object::ObjectFormat::ELF);
        if (elf_reader.read(span.data(), span.size(), elf_obj)) {
            out_obj = std::move(elf_obj);
            return true;
        }

        return false;
    }

private:
    bool parse_archive() {
        if (raw_bytes_.size() < 8) return false;
        if (std::memcmp(raw_bytes_.data(), kArGlobalMagic, 8) != 0) return false;

        size_t cursor = 8;
        while (cursor + sizeof(ArHeaderRaw) <= raw_bytes_.size()) {
            const auto* hdr = reinterpret_cast<const ArHeaderRaw*>(raw_bytes_.data() + cursor);
            if (hdr->fmag[0] != '`' || hdr->fmag[1] != '\n') {
                break;
            }

            char size_buf[11] = {0};
            std::memcpy(size_buf, hdr->size, 10);
            const size_t member_size = std::strtoul(size_buf, nullptr, 10);
            const size_t data_offset = cursor + sizeof(ArHeaderRaw);

            if (data_offset + member_size > raw_bytes_.size()) {
                break;
            }

            std::string name(hdr->name, 16);
            while (!name.empty() && name.back() == ' ') name.pop_back();

            if (name == "/" || name == "/       ") {
                // First Symbol Directory: parse Big-Endian GNU / SysV table
                if (member_size >= 4) {
                    const uint8_t* p = raw_bytes_.data() + data_offset;
                    const uint32_t num_symbols = read_be_u32(p);
                    const size_t offsets_bytes = static_cast<size_t>(num_symbols) * 4;
                    if (4 + offsets_bytes <= member_size) {
                        std::vector<uint32_t> offsets(num_symbols);
                        for (uint32_t i = 0; i < num_symbols; ++i) {
                            offsets[i] = read_be_u32(p + 4 + (i * 4));
                        }
                        const char* str_cursor = reinterpret_cast<const char*>(p + 4 + offsets_bytes);
                        const char* str_end = reinterpret_cast<const char*>(p + member_size);

                        for (uint32_t i = 0; i < num_symbols && str_cursor < str_end; ++i) {
                            std::string sym_name(str_cursor);
                            str_cursor += sym_name.size() + 1;
                            if (!sym_name.empty()) {
                                symbol_to_member_offset_[sym_name] = offsets[i];
                            }
                        }
                    }
                }
            } else if (name == "//" || name == "//      ") {
                // Long Names Table
                long_names_str_.assign(reinterpret_cast<const char*>(raw_bytes_.data() + data_offset), member_size);
            } else {
                // Regular member object
                std::string member_filename = name;
                if (!member_filename.empty() && member_filename.back() == '/') {
                    member_filename.pop_back();
                } else if (!member_filename.empty() && member_filename.front() == '/' && member_filename.size() > 1 && std::isdigit(static_cast<unsigned char>(member_filename[1]))) {
                    size_t str_off = std::strtoul(member_filename.c_str() + 1, nullptr, 10);
                    if (str_off < long_names_str_.size()) {
                        size_t end_slash = long_names_str_.find('/', str_off);
                        if (end_slash != std::string::npos) {
                            member_filename = long_names_str_.substr(str_off, end_slash - str_off);
                        }
                    }
                }

                ArchiveMember m;
                m.filename = member_filename;
                m.header_offset = static_cast<uint32_t>(cursor);
                m.data_offset = static_cast<uint32_t>(data_offset);
                m.data.assign(raw_bytes_.data() + data_offset, raw_bytes_.data() + data_offset + member_size);
                extract_universal_symbols(m.data, m.symbols);
                for (const auto& sym : m.symbols) {
                    symbol_to_member_offset_.try_emplace(sym, m.header_offset);
                }
                members_.push_back(std::move(m));
            }

            // Align to 2-byte boundary
            cursor = data_offset + member_size + (member_size % 2);
        }

        loaded_ = true;
        return true;
    }
};

} // namespace dgtl::object::archive
