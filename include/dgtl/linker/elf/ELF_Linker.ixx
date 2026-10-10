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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module dgtl.linker.elf;

import dgtl.object.writers.elf;
import dgtl.object.readers.elf;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.mir;

export namespace dgtl::linker::elf {

class ELFLinker {
public:
    // Sovereign MIR-native linking
    bool link_relocatable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write_machine_function(fn, out_bytes, entry_symbol);
    }

    bool link_relocatable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link_static_archive(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write_machine_function(fn, obj_bytes, fn.name())) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    bool link_static_archive(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write(obj, obj_bytes)) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        uint64_t image_base = 0x400000,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {

        const auto* text_sec = obj.find_section(".text");
        if (!text_sec || text_sec->bytes.empty()) {
            for (const auto& s : obj.sections()) {
                if (s.is_code() && !s.bytes.empty()) {
                    text_sec = &s;
                    break;
                }
            }
        }
        if (!text_sec || text_sec->bytes.empty()) {
            return false;
        }

        std::vector<uint8_t> code = text_sec->bytes;

        // Data sections (.rodata, .data)
        std::vector<uint8_t> data;
        struct SecOffset { std::string name; uint64_t offset; };
        std::vector<SecOffset> data_offsets;
        for (const auto& sec : obj.sections()) {
            if (sec.name == ".rodata" || sec.name == ".data") {
                while ((data.size() % 16) != 0) data.push_back(0);
                data_offsets.push_back({sec.name, data.size()});
                data.insert(data.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }

        auto find_data_offset = [&](std::string_view name) -> std::optional<uint64_t> {
            for (const auto& item : data_offsets) {
                if (item.name == name) return item.offset;
            }
            return std::nullopt;
        };

        constexpr uint64_t kPageSize = 0x1000;
        const uint64_t code_file_off = kPageSize;
        const uint64_t code_vaddr = image_base + code_file_off;

        uint64_t data_file_off = 0;
        uint64_t data_vaddr = 0;
        const bool has_data = !data.empty();
        if (has_data) {
            data_file_off = (code_file_off + code.size() + (kPageSize - 1)) & ~(kPageSize - 1);
            data_vaddr = image_base + data_file_off;
        }

        // Find entry point
        uint64_t entry_offset = 0;
        if (!entry_symbol.empty()) {
            if (const auto* sym = obj.find_symbol(entry_symbol)) {
                entry_offset = sym->value;
            }
        }
        if (entry_offset == 0) {
            if (const auto* sym = obj.find_symbol("DGTL_kernel_entry")) {
                entry_offset = sym->value;
            } else if (const auto* sym_m = obj.find_symbol("main")) {
                entry_offset = sym_m->value;
            } else if (const auto* sym_s = obj.find_symbol("_start")) {
                entry_offset = sym_s->value;
            }
        }
        const uint64_t entry_vaddr = code_vaddr + entry_offset;

        // Apply relocations
        struct SymVAddr { std::string name; uint64_t vaddr; };
        std::vector<SymVAddr> symbol_vaddrs;
        symbol_vaddrs.reserve(obj.symbols().size());
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined()) {
                uint64_t base = code_vaddr;
                if (sym.section_index < obj.sections().size()) {
                    const auto& sec_name = obj.sections()[sym.section_index].name;
                    if (sec_name != ".text") {
                        if (auto off = find_data_offset(sec_name)) {
                            base = data_vaddr + *off;
                        }
                    }
                }
                symbol_vaddrs.push_back({sym.name, base + sym.value});
            }
        }
        std::sort(symbol_vaddrs.begin(), symbol_vaddrs.end(), [](const SymVAddr& a, const SymVAddr& b) {
            return a.name < b.name;
        });

        auto find_symbol_vaddr = [&](std::string_view name) -> std::optional<uint64_t> {
            auto it = std::lower_bound(symbol_vaddrs.begin(), symbol_vaddrs.end(), name,
                [](const SymVAddr& item, std::string_view n) { return item.name < n; });
            if (it != symbol_vaddrs.end() && it->name == name) return it->vaddr;
            return std::nullopt;
        };

        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            auto sym_opt = find_symbol_vaddr(rel.symbol_name);
            if (!sym_opt) continue;
            const uint64_t target_vaddr = *sym_opt;

            std::optional<uint64_t> rel_data_off = std::nullopt;
            if (rel.section_index < obj.sections().size()) {
                rel_data_off = find_data_offset(obj.sections()[rel.section_index].name);
            }

            if (rel_data_off) {
                const uint64_t place_off = *rel_data_off + rel.offset;
                if (place_off + 8 <= data.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::Absolute64) {
                        const uint64_t val = target_vaddr + rel.addend;
                        for (int i = 0; i < 8; ++i) data[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    }
                }
            } else {
                const uint64_t place_off = rel.offset;
                const uint64_t place_vaddr = code_vaddr + place_off;
                if (place_off + 4 <= code.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::PCRel32 || rel.kind == dgtl::object::RelocationKind::PLT32) {
                        const uint64_t pc = place_vaddr + 4;
                        const int64_t delta = static_cast<int64_t>(target_vaddr) + rel.addend - static_cast<int64_t>(pc);
                        const auto val = static_cast<int32_t>(delta);
                        for (int i = 0; i < 4; ++i) code[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute64 && place_off + 8 <= code.size()) {
                        const uint64_t val = target_vaddr + rel.addend;
                        for (int i = 0; i < 8; ++i) code[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    }
                }
            }
        }

        // Build ELF64 binary image
        const uint64_t total_size = has_data ? (data_file_off + data.size()) : (code_file_off + code.size());
        out_bytes.assign(total_size, 0);

        // 1. ELF Header (64 bytes)
        out_bytes[0] = 0x7F; out_bytes[1] = 'E'; out_bytes[2] = 'L'; out_bytes[3] = 'F';
        out_bytes[4] = 2; // ELFCLASS64
        out_bytes[5] = 1; // ELFDATA2LSB
        out_bytes[6] = 1; // EV_CURRENT
        out_bytes[7] = 0; // ELFOSABI_NONE (System V)

        auto write_u16 = [&](size_t off, uint16_t val) {
            out_bytes[off + 0] = static_cast<uint8_t>(val & 0xFF);
            out_bytes[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        };
        auto write_u32 = [&](size_t off, uint32_t val) {
            for (int i = 0; i < 4; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };
        auto write_u64 = [&](size_t off, uint64_t val) {
            for (int i = 0; i < 8; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };

        write_u16(16, 2);              // e_type = ET_EXEC (2)
        write_u16(18, 62);             // e_machine = EM_X86_64 (62)
        write_u32(20, 1);              // e_version = 1
        write_u64(24, entry_vaddr);    // e_entry
        write_u64(32, 64);             // e_phoff = 64
        write_u64(40, 0);              // e_shoff = 0
        write_u32(48, 0);              // e_flags = 0
        write_u16(52, 64);             // e_ehsize = 64
        write_u16(54, 56);             // e_phentsize = 56
        write_u16(56, has_data ? 2 : 1);// e_phnum
        write_u16(58, 0);              // e_shentsize = 0
        write_u16(60, 0);              // e_shnum = 0
        write_u16(62, 0);              // e_shstrndx = 0

        // 2. Program Headers (56 bytes each)
        // Segment 0: Code (.text)
        write_u32(64 + 0,  1);                  // p_type = PT_LOAD (1)
        write_u32(64 + 4,  5);                  // p_flags = PF_R | PF_X (5)
        write_u64(64 + 8,  0);                  // p_offset = 0 (includes header)
        write_u64(64 + 16, image_base);         // p_vaddr
        write_u64(64 + 24, image_base);         // p_paddr
        write_u64(64 + 32, code_file_off + code.size()); // p_filesz
        write_u64(64 + 40, code_file_off + code.size()); // p_memsz
        write_u64(64 + 48, kPageSize);          // p_align = 0x1000

        // Segment 1: Data (if present)
        if (has_data) {
            const size_t ph1 = 64 + 56;
            write_u32(ph1 + 0,  1);             // p_type = PT_LOAD (1)
            write_u32(ph1 + 4,  6);             // p_flags = PF_R | PF_W (6)
            write_u64(ph1 + 8,  data_file_off); // p_offset
            write_u64(ph1 + 16, data_vaddr);    // p_vaddr
            write_u64(ph1 + 24, data_vaddr);    // p_paddr
            write_u64(ph1 + 32, data.size());   // p_filesz
            write_u64(ph1 + 40, data.size());   // p_memsz
            write_u64(ph1 + 48, kPageSize);     // p_align = 0x1000
        }

        // 3. Write Section Bytes
        std::copy(code.begin(), code.end(), out_bytes.begin() + code_file_off);
        if (has_data) {
            std::copy(data.begin(), data.end(), out_bytes.begin() + data_file_off);
        }

        return true;
    }

    bool link_executable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write_machine_function(fn, obj_bytes, entry_symbol)) return false;
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::ELF);
        dgtl::object::readers::ELFObjectReader reader;
        if (!reader.read(obj_bytes.data(), obj_bytes.size(), obj)) return false;
        return link_executable(obj, out_bytes, 0x400000, entry_symbol);
    }
};

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using ELFLinker = dgtl::linker::elf::ELFLinker;
}



