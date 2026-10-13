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
#include <iomanip>
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

enum class BinaryContainerKind {
    Unknown,
    StaticArchive,
    PEExecutable,
    ELFExecutable,
    COFFObject
};

struct ImportedDllInfo {
    std::string dll_name;
    std::vector<std::string> functions;
};

class UniversalArchiveReader {
    std::vector<uint8_t> raw_bytes_;
    std::string long_names_str_;
    std::unordered_map<std::string, uint32_t> symbol_to_member_offset_;
    std::vector<ArchiveMember> members_;
    bool loaded_ = false;
    BinaryContainerKind container_kind_ = BinaryContainerKind::Unknown;
    std::string container_format_desc_;
    uint64_t entry_point_rva_ = 0;
    uint64_t image_base_ = 0;
    std::vector<ImportedDllInfo> imported_dlls_;

public:
    UniversalArchiveReader() = default;

    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] BinaryContainerKind container_kind() const noexcept { return container_kind_; }
    [[nodiscard]] bool is_executable() const noexcept {
        return container_kind_ == BinaryContainerKind::PEExecutable || container_kind_ == BinaryContainerKind::ELFExecutable;
    }
    [[nodiscard]] const std::string& format_description() const noexcept { return container_format_desc_; }
    [[nodiscard]] uint64_t entry_point_rva() const noexcept { return entry_point_rva_; }
    [[nodiscard]] uint64_t image_base() const noexcept { return image_base_; }
    [[nodiscard]] const std::vector<ImportedDllInfo>& imported_dlls() const noexcept { return imported_dlls_; }

    bool load_archive(const std::string& archive_path) {
        raw_bytes_.clear();
        long_names_str_.clear();
        symbol_to_member_offset_.clear();
        members_.clear();
        imported_dlls_.clear();
        container_kind_ = BinaryContainerKind::Unknown;
        container_format_desc_.clear();
        entry_point_rva_ = 0;
        image_base_ = 0;
        loaded_ = false;

        std::ifstream in(archive_path, std::ios::binary);
        if (!in) return false;

        in.seekg(0, std::ios::end);
        const std::size_t sz = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        if (sz < 4) return false;
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
        imported_dlls_.clear();
        container_kind_ = BinaryContainerKind::Unknown;
        container_format_desc_.clear();
        entry_point_rva_ = 0;
        image_base_ = 0;
        loaded_ = false;

        if (!data || size < 4) return false;
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
        if (raw_bytes_.size() < 4) return false;

        if (raw_bytes_.size() >= 8 && std::memcmp(raw_bytes_.data(), kArGlobalMagic, 8) == 0) {
            container_kind_ = BinaryContainerKind::StaticArchive;
            container_format_desc_ = "GNU/COFF Static Archive";

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
                    // First Symbol Directory: parse Big-Endian GNU / SysV table (with Little-Endian fallback)
                    if (member_size >= 4) {
                        const uint8_t* p = raw_bytes_.data() + data_offset;
                        uint32_t num_symbols = read_be_u32(p);
                        bool is_be = true;
                        if (4 + static_cast<size_t>(num_symbols) * 4 > member_size) {
                            uint32_t le_num = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                              (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
                            if (4 + static_cast<size_t>(le_num) * 4 <= member_size) {
                                num_symbols = le_num;
                                is_be = false;
                            }
                        }
                        const size_t offsets_bytes = static_cast<size_t>(num_symbols) * 4;
                        if (4 + offsets_bytes <= member_size) {
                            std::vector<uint32_t> offsets(num_symbols);
                            for (uint32_t i = 0; i < num_symbols; ++i) {
                                if (is_be) {
                                    offsets[i] = read_be_u32(p + 4 + (i * 4));
                                } else {
                                    offsets[i] = static_cast<uint32_t>(p[4 + i * 4]) |
                                                 (static_cast<uint32_t>(p[4 + i * 4 + 1]) << 8) |
                                                 (static_cast<uint32_t>(p[4 + i * 4 + 2]) << 16) |
                                                 (static_cast<uint32_t>(p[4 + i * 4 + 3]) << 24);
                                }
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
                    if (symbol_to_member_offset_.empty()) {
                        // Fallback for un-indexed archives only
                        extract_universal_symbols(m.data, m.symbols);
                        for (const auto& sym : m.symbols) {
                            symbol_to_member_offset_.try_emplace(sym, m.header_offset);
                        }
                    }
                    members_.push_back(std::move(m));
                }

                // Align to 2-byte boundary
                cursor = data_offset + member_size + (member_size % 2);
            }

            loaded_ = true;
            return true;
        }

        // PE Executable / Image (MZ magic)
        if (raw_bytes_.size() >= 64 && raw_bytes_[0] == 'M' && raw_bytes_[1] == 'Z') {
            return parse_pe_binary();
        }

        // ELF Executable / Object (\x7FELF magic)
        if (raw_bytes_.size() >= 4 && raw_bytes_[0] == 0x7F && raw_bytes_[1] == 'E' && raw_bytes_[2] == 'L' && raw_bytes_[3] == 'F') {
            return parse_elf_binary();
        }

        // COFF Object
        if (raw_bytes_.size() >= 20 && (read_le_u16(raw_bytes_.data()) == 0x8664 || read_le_u16(raw_bytes_.data()) == 0x14C)) {
            return parse_coff_binary();
        }

        return false;
    }

    bool parse_pe_binary() {
        if (raw_bytes_.size() < 0x40) return false;
        const uint32_t pe_offset = read_le_u32(raw_bytes_.data() + 0x3C);
        if (pe_offset + 24 > raw_bytes_.size()) return false;
        if (std::memcmp(raw_bytes_.data() + pe_offset, "PE\0\0", 4) != 0) return false;

        container_kind_ = BinaryContainerKind::PEExecutable;
        const uint8_t* pe_base = raw_bytes_.data() + pe_offset;
        const uint16_t machine = read_le_u16(pe_base + 4);
        const uint16_t num_sections = read_le_u16(pe_base + 6);
        const uint16_t opt_hdr_size = read_le_u16(pe_base + 20);
        const uint16_t characteristics = read_le_u16(pe_base + 22);

        const bool is_exe = (characteristics & 0x0002) != 0;
        bool is_64 = false;
        uint32_t entry_rva = 0;
        uint64_t img_base = 0;
        uint32_t import_dir_rva = 0;
        uint32_t import_dir_size = 0;
        uint32_t export_dir_rva = 0;

        if (opt_hdr_size >= 2) {
            const uint16_t magic = read_le_u16(pe_base + 24);
            is_64 = (magic == 0x020B);
            if (opt_hdr_size >= 20) {
                entry_rva = read_le_u32(pe_base + 24 + 16);
                entry_point_rva_ = entry_rva;
            }
            if (is_64 && opt_hdr_size >= 128) {
                img_base = read_le_u64(pe_base + 24 + 24);
                image_base_ = img_base;
                export_dir_rva = read_le_u32(pe_base + 24 + 112);
                import_dir_rva = read_le_u32(pe_base + 24 + 112 + 8);
                import_dir_size = read_le_u32(pe_base + 24 + 112 + 12);
            } else if (!is_64 && opt_hdr_size >= 112) {
                img_base = read_le_u32(pe_base + 24 + 28);
                image_base_ = img_base;
                export_dir_rva = read_le_u32(pe_base + 24 + 96);
                import_dir_rva = read_le_u32(pe_base + 24 + 96 + 8);
                import_dir_size = read_le_u32(pe_base + 24 + 96 + 12);
            }
        }

        std::string mach_str = (machine == 0x8664) ? "x86_64" : (machine == 0x14C ? "x86" : (machine == 0xAA64 ? "ARM64" : "Unknown"));
        container_format_desc_ = (is_64 ? "PE32+ (" : "PE32 (") + mach_str + (is_exe ? " Executable)" : " DLL/Image)");

        const size_t sec_hdr_start = pe_offset + 24 + opt_hdr_size;
        struct SecRecord {
            std::string name;
            uint32_t v_size;
            uint32_t v_rva;
            uint32_t raw_size;
            uint32_t raw_off;
        };
        std::vector<SecRecord> sec_records;

        for (uint16_t i = 0; i < num_sections; ++i) {
            const size_t sh_off = sec_hdr_start + (static_cast<size_t>(i) * 40);
            if (sh_off + 40 > raw_bytes_.size()) break;
            const uint8_t* sh = raw_bytes_.data() + sh_off;
            char name_buf[9] = {0};
            std::memcpy(name_buf, sh, 8);
            std::string sec_name(name_buf);
            while (!sec_name.empty() && (sec_name.back() == '\0' || sec_name.back() == ' ')) sec_name.pop_back();

            const uint32_t v_size = read_le_u32(sh + 8);
            const uint32_t v_rva = read_le_u32(sh + 12);
            const uint32_t raw_size = read_le_u32(sh + 16);
            const uint32_t raw_off = read_le_u32(sh + 20);

            sec_records.push_back({sec_name, v_size, v_rva, raw_size, raw_off});

            ArchiveMember m;
            m.filename = sec_name;
            m.header_offset = static_cast<uint32_t>(sh_off);
            m.data_offset = raw_off;
            if (raw_off < raw_bytes_.size()) {
                const size_t actual_copy = std::min<size_t>(raw_size, raw_bytes_.size() - raw_off);
                m.data.assign(raw_bytes_.data() + raw_off, raw_bytes_.data() + raw_off + actual_copy);
            }
            members_.push_back(std::move(m));
        }

        auto rva_to_off = [&](uint32_t rva) -> std::optional<size_t> {
            for (const auto& sec : sec_records) {
                if (rva >= sec.v_rva && rva < sec.v_rva + std::max(sec.v_size, sec.raw_size)) {
                    return static_cast<size_t>(sec.raw_off + (rva - sec.v_rva));
                }
            }
            return std::nullopt;
        };

        // Parse Import Table (Directory 1)
        if (import_dir_rva != 0 && import_dir_size > 0) {
            if (auto id_off = rva_to_off(import_dir_rva)) {
                size_t cur_desc = *id_off;
                while (cur_desc + 20 <= raw_bytes_.size()) {
                    const uint32_t ilt_rva = read_le_u32(raw_bytes_.data() + cur_desc);
                    const uint32_t name_rva = read_le_u32(raw_bytes_.data() + cur_desc + 12);
                    const uint32_t iat_rva = read_le_u32(raw_bytes_.data() + cur_desc + 16);
                    if (ilt_rva == 0 && name_rva == 0 && iat_rva == 0) break;

                    std::string dll_name;
                    if (auto n_off = rva_to_off(name_rva)) {
                        const char* s_start = reinterpret_cast<const char*>(raw_bytes_.data() + *n_off);
                        const size_t max_l = raw_bytes_.size() - *n_off;
                        dll_name.assign(s_start, ::strnlen(s_start, max_l));
                    }

                    ImportedDllInfo imp_dll;
                    imp_dll.dll_name = dll_name;

                    const uint32_t lookup_rva = (ilt_rva != 0) ? ilt_rva : iat_rva;
                    if (auto l_off = rva_to_off(lookup_rva)) {
                        size_t thunk_off = *l_off;
                        const size_t step = is_64 ? 8 : 4;
                        while (thunk_off + step <= raw_bytes_.size()) {
                            const uint64_t val = is_64 ? read_le_u64(raw_bytes_.data() + thunk_off)
                                                       : read_le_u32(raw_bytes_.data() + thunk_off);
                            if (val == 0) break;

                            const bool is_ordinal = is_64 ? ((val & 0x8000000000000000ULL) != 0)
                                                          : ((val & 0x80000000U) != 0);
                            std::string fn_name;
                            if (is_ordinal) {
                                fn_name = "Ordinal#" + std::to_string(val & 0xFFFF);
                            } else {
                                const uint32_t hint_rva = static_cast<uint32_t>(val & 0x7FFFFFFFULL);
                                if (auto h_off = rva_to_off(hint_rva)) {
                                    if (*h_off + 2 < raw_bytes_.size()) {
                                        const char* f_start = reinterpret_cast<const char*>(raw_bytes_.data() + *h_off + 2);
                                        const size_t max_f = raw_bytes_.size() - (*h_off + 2);
                                        fn_name.assign(f_start, ::strnlen(f_start, max_f));
                                    }
                                }
                            }
                            if (!fn_name.empty()) {
                                imp_dll.functions.push_back(fn_name);
                                symbol_to_member_offset_[fn_name] = static_cast<uint32_t>(thunk_off);
                            }
                            thunk_off += step;
                        }
                    }

                    if (!imp_dll.dll_name.empty()) {
                        imported_dlls_.push_back(std::move(imp_dll));
                    }
                    cur_desc += 20;
                }
            }
        }

        // Add standard entry symbols
        if (entry_rva != 0) {
            if (auto e_off = rva_to_off(entry_rva)) {
                symbol_to_member_offset_["main"] = static_cast<uint32_t>(*e_off);
                symbol_to_member_offset_["entry"] = static_cast<uint32_t>(*e_off);
                symbol_to_member_offset_["DGTL_kernel_entry"] = static_cast<uint32_t>(*e_off);
            }
        }

        // Attach imported functions to .rdata (or legacy .idata) member
        for (auto& m : members_) {
            if (m.filename == ".idata" || (m.filename == ".rdata" && import_dir_rva != 0)) {
                for (const auto& dll : imported_dlls_) {
                    for (const auto& fn : dll.functions) {
                        m.symbols.push_back(dll.dll_name + "!" + fn);
                    }
                }
            } else if (m.filename == ".text" && entry_rva != 0) {
                char b[16]; std::snprintf(b, sizeof(b), "%X", entry_rva);
                m.symbols.push_back("entry (RVA 0x" + std::string(b) + ")");
            }
        }

        loaded_ = true;
        return true;
    }

    bool parse_elf_binary() {
        if (raw_bytes_.size() < 52) return false;
        container_kind_ = BinaryContainerKind::ELFExecutable;
        const bool is_64 = (raw_bytes_[4] == 2);
        container_format_desc_ = is_64 ? "ELF64 Binary" : "ELF32 Binary";
        ArchiveMember m;
        m.filename = ".text";
        m.data = raw_bytes_;
        extract_elf_symbols(m.data, m.symbols);
        for (const auto& s : m.symbols) {
            symbol_to_member_offset_[s] = 0;
        }
        members_.push_back(std::move(m));
        loaded_ = true;
        return true;
    }

    bool parse_coff_binary() {
        if (raw_bytes_.size() < 20) return false;
        container_kind_ = BinaryContainerKind::COFFObject;
        container_format_desc_ = "COFF Relocatable Object (.obj)";
        ArchiveMember m;
        m.filename = "module.obj";
        m.data = raw_bytes_;
        extract_coff_symbols(m.data, m.symbols);
        for (const auto& s : m.symbols) {
            symbol_to_member_offset_[s] = 0;
        }
        members_.push_back(std::move(m));
        loaded_ = true;
        return true;
    }
};

inline bool inspect_archive_or_binary(const std::string& path, bool verbose = true) {
    UniversalArchiveReader reader;
    if (!reader.load_archive(path)) {
        std::cerr << "error[dgtl-ar]: failed to load or parse container: " << path << "\n";
        return false;
    }

    if (reader.is_executable()) {
        std::cout << "[dgtl-ar] Binary Executable Container: " << path << "\n";
        std::cout << "[dgtl-ar] Architecture : " << reader.format_description() << "\n";
        std::cout << "[dgtl-ar] EntryPoint   : 0x" << std::hex << reader.entry_point_rva()
                  << " (ImageBase: 0x" << reader.image_base() << ")" << std::dec << "\n";
        std::cout << "[dgtl-ar] Sections     : " << reader.members().size() << "\n";

        if (!reader.imported_dlls().empty()) {
            size_t total_imports = 0;
            for (const auto& dll : reader.imported_dlls()) total_imports += dll.functions.size();
            std::cout << "[dgtl-ar] Dynamic Linked Libraries (" << reader.imported_dlls().size()
                      << " DLLs, " << total_imports << " imported symbols):\n";
            for (const auto& dll : reader.imported_dlls()) {
                std::cout << "  - " << std::left << std::setw(16) << dll.dll_name
                          << " (" << dll.functions.size() << " imports";
                if (verbose && !dll.functions.empty()) {
                    std::cout << ": ";
                    for (size_t fi = 0; fi < std::min<size_t>(dll.functions.size(), 8); ++fi) {
                        if (fi > 0) std::cout << ", ";
                        std::cout << dll.functions[fi];
                    }
                    if (dll.functions.size() > 8) {
                        std::cout << ", ... (" << (dll.functions.size() - 8) << " more)";
                    }
                }
                std::cout << ")\n";
            }
        }

        std::cout << "\n[dgtl-ar] Sections Table:\n";
        std::cout << "----------------------------------------------------------------------\n";
        for (const auto& m : reader.members()) {
            std::cout << std::left << std::setw(24) << m.filename
                      << " " << std::right << std::setw(10) << m.data.size() << " bytes";
            if (verbose && !m.symbols.empty()) {
                std::cout << " (" << m.symbols.size() << " symbols)";
            }
            std::cout << "\n";
        }
        return true;
    }

    std::cout << "[dgtl-ar] Static Archive: " << path
              << " (" << reader.members().size() << " members, "
              << reader.symbol_map().size() << " indexed symbols)\n";
    std::cout << "----------------------------------------------------------------------\n";
    for (const auto& m : reader.members()) {
        std::cout << std::left << std::setw(28) << m.filename
                  << " " << std::right << std::setw(10) << m.data.size() << " bytes"
                  << "  [" << m.symbols.size() << " symbols]\n";
    }
    return true;
}

inline int execute_test_binary(const std::string& exe_path) {
    UniversalArchiveReader reader;
    if (!reader.load_archive(exe_path)) {
        std::cerr << "error[dgtl-ar]: failed to load binary executable: " << exe_path << "\n";
        return 2;
    }

    std::cout << "======================================================================\n";
    std::cout << "[dgtl-ar] Executing Native Test Binary: " << exe_path << "\n";
    std::cout << "[dgtl-ar] Format: " << reader.format_description()
              << " | EntryPoint RVA: 0x" << std::hex << reader.entry_point_rva() << std::dec << "\n";
    std::cout << "======================================================================\n";

    std::string run_cmd;
#if defined(_WIN32)
    run_cmd = "\"\"" + std::filesystem::path(exe_path).make_preferred().string() + "\"\"";
#else
    run_cmd = "\"" + std::filesystem::path(exe_path).make_preferred().string() + "\"";
#endif
    int run_rc = std::system(run_cmd.c_str());
    std::cout << "\n======================================================================\n";
    std::cout << "[dgtl-ar] Execution Result: exit code " << run_rc;
    if (run_rc == 0) {
        std::cout << " (SUCCESS: 0 errors)\n";
    } else if (run_rc == -1073741515 || run_rc == static_cast<int>(0xC0000139)) {
        std::cout << " (FATAL: STATUS_ENTRYPOINT_NOT_FOUND 0xC0000139 - missing DLL export)\n";
    } else {
        std::cout << " (ABNORMAL TERMINATION)\n";
    }
    std::cout << "======================================================================\n";
    return run_rc;
}

} // namespace dgtl::object::archive
