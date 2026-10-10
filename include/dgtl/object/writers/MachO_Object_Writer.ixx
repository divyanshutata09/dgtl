/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Mach-O 64-bit Relocatable Object File Writer
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

export module dgtl.object.writers.macho;

import dgtl.object.file;
import dgtl.object.writer;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.mir.machine_ir;
import dgtl.isa.arm64.arm64_emitter;
import dgtl.linker.macho.sections;
import dgtl.linker.macho.relocations;

export namespace dgtl::object::writers::macho {

using namespace dgtl::linker::macho;

} // namespace dgtl::object::writers::macho

export namespace dgtl::object::writers {

class MachOObjectWriter final : public dgtl::object::ObjectWriter {
public:
    struct MachOResult {
        std::vector<uint8_t> binary_data;
        bool success{false};
    };

    MachOObjectWriter() = default;

    explicit MachOObjectWriter(const dgtl::machine::MachineFunction& function)
        : m_function(&function) {}

    template <typename AnyT>
    explicit MachOObjectWriter(const AnyT& function_or_other) {
        if constexpr (std::is_same_v<std::decay_t<AnyT>, dgtl::machine::MachineFunction>) {
            m_function = &function_or_other;
        }
    }

    template <typename StreamT, typename SymbolTableT>
    MachOObjectWriter(const StreamT&, const SymbolTableT&)
        : m_function(nullptr) {}

    [[nodiscard]] std::string_view format_name() const noexcept override {
        return "Mach-O 64";
    }

    [[nodiscard]] bool write_machine_function(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out,
        std::string_view entry_name = "_DGTL_kernel_entry") const {

        namespace m = dgtl::object::writers::macho;

        dgtl::isa::arm64::ARM64Emitter::Arm64CodegenResult codegen =
            dgtl::isa::arm64::ARM64Emitter::generate_arm64_binary(function);

        if (codegen.code.empty()) {
            const uint32_t ret_insn = 0xD65F03C0u; // ARM64 RET
            codegen.code.resize(4);
            std::memcpy(codegen.code.data(), &ret_insn, 4);
        }

        // Build fully synthesized Mach-O ObjectFile with unwind and frame info
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::MachO);
        auto& text_sec = obj.get_text_section();
        text_sec.bytes = std::move(codegen.code);

        for (const auto& r : codegen.runtime_relocs) {
            if (!obj.find_symbol(r.symbol)) {
                obj.add_external_symbol(r.symbol);
            }
            obj.add_relocation(r.text_offset, r.symbol, 0, m::ARM64_RELOC_BRANCH26, dgtl::object::RelocationKind::PCRel32);
        }

        return write(obj, out);
    }

    [[nodiscard]] bool write(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out) const override {

        namespace m = dgtl::object::writers::macho;

        // 1. Collect all emit sections
        std::vector<const dgtl::object::Section*> emit_sections;
        for (const auto& sec : obj.sections()) {
            if (!sec.bytes.empty() || sec.is_bss() || sec.kind == dgtl::object::SectionKind::BSS) {
                emit_sections.push_back(&sec);
            }
        }

        // Ensure at least a text section exists
        dgtl::object::Section fallback_text("__text", 4u, 
            dgtl::object::SectionFlags::Code | dgtl::object::SectionFlags::Read | dgtl::object::SectionFlags::Executable,
            dgtl::object::SectionKind::Text);
        const uint32_t ret_insn = 0xD65F03C0u; // ARM64 RET
        fallback_text.bytes.resize(4);
        std::memcpy(fallback_text.bytes.data(), &ret_insn, 4);

        if (emit_sections.empty()) {
            emit_sections.push_back(&fallback_text);
        } else {
            bool has_text = false;
            for (const auto* s : emit_sections) {
                if (s->is_code() || s->name == "__text" || s->name == ".text") {
                    has_text = true;
                    break;
                }
            }
            if (!has_text) {
                emit_sections.insert(emit_sections.begin(), &fallback_text);
            }
        }

        // 2. Map sections to Mach-O segment/section metadata
        struct SectionLayout {
            const dgtl::object::Section* sec{nullptr};
            char sectname[16]{};
            char segname[16]{};
            uint64_t vm_addr{0};
            uint64_t size{0};
            uint32_t file_offset{0};
            uint32_t align_p2{0};
            uint32_t flags{0};
            uint32_t reloff{0};
            uint32_t nreloc{0};
            bool is_zerofill{false};
        };

        std::vector<SectionLayout> layouts;
        layouts.reserve(emit_sections.size());

        for (const auto* sec : emit_sections) {
            SectionLayout l{};
            l.sec = sec;
            l.is_zerofill = sec->is_bss() || (sec->kind == dgtl::object::SectionKind::BSS);

            // Compute power-of-2 alignment (e.g. 1->0, 2->1, 4->2, 8->3, 16->4)
            uint32_t p = 0;
            uint64_t a = (sec->alignment <= 1) ? 1 : sec->alignment;
            while ((1ULL << p) < a && p < 15) ++p;
            l.align_p2 = p;

            std::string_view sname = sec->name;
            if (sec->kind == dgtl::object::SectionKind::Text || sname == "__text" || sname == ".text") {
                std::memcpy(l.sectname, "__text", 6);
                std::memcpy(l.segname,  "__TEXT", 6);
                l.align_p2 = std::max(l.align_p2, 2u);
                l.flags = m::S_ATTR_PURE_INSTRUCTIONS | m::S_ATTR_SOME_INSTRUCTIONS;
            } else if (sec->kind == dgtl::object::SectionKind::RData || sname == "__const" || sname == ".rdata" || sname == ".rodata") {
                std::memcpy(l.sectname, "__const", 7);
                std::memcpy(l.segname,  "__TEXT", 6);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_REGULAR;
            } else if (sname == "__cstring" || sname == ".cstring") {
                std::memcpy(l.sectname, "__cstring", 9);
                std::memcpy(l.segname,  "__TEXT", 6);
                l.flags = m::S_CSTRING_LITERALS;
            } else if (sec->kind == dgtl::object::SectionKind::Data || sname == "__data" || sname == ".data") {
                std::memcpy(l.sectname, "__data", 6);
                std::memcpy(l.segname,  "__DATA", 6);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_REGULAR;
            } else if (l.is_zerofill || sname == "__bss" || sname == ".bss") {
                std::memcpy(l.sectname, "__bss", 5);
                std::memcpy(l.segname,  "__DATA", 6);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_ZEROFILL;
                l.is_zerofill = true;
            } else if (sname == "__compact_unwind" || sname == ".compact_unwind") {
                std::memcpy(l.sectname, "__compact_unwind", 16);
                std::memcpy(l.segname,  "__LD", 4);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_REGULAR;
            } else if (sec->kind == dgtl::object::SectionKind::ExceptionTable || sname == "__unwind_info" || sname == ".pdata") {
                std::memcpy(l.sectname, "__unwind_info", 13);
                std::memcpy(l.segname,  "__TEXT", 6);
                l.align_p2 = std::max(l.align_p2, 2u);
                l.flags = m::S_REGULAR;
            } else if (sec->kind == dgtl::object::SectionKind::ExceptionInfo || sname == "__eh_frame" || sname == ".eh_frame" || sname == ".xdata") {
                std::memcpy(l.sectname, "__eh_frame", 10);
                std::memcpy(l.segname,  "__TEXT", 6);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_REGULAR;
            } else if (sec->kind == dgtl::object::SectionKind::TLS || sname == "__thread_data" || sname == ".tls$" || sname == ".tdata") {
                std::memcpy(l.sectname, "__thread_data", 13);
                std::memcpy(l.segname,  "__DATA", 6);
                l.align_p2 = std::max(l.align_p2, 3u);
                l.flags = m::S_THREAD_LOCAL_REGULAR;
            } else {
                std::size_t len = std::min(sname.size(), std::size_t(15));
                if (sname.starts_with('.')) {
                    std::string converted = "__" + std::string(sname.substr(1));
                    len = std::min(converted.size(), std::size_t(15));
                    std::memcpy(l.sectname, converted.data(), len);
                } else {
                    std::memcpy(l.sectname, sname.data(), len);
                }
                std::memcpy(l.segname, sec->is_writable() ? "__DATA" : "__TEXT", 6);
                l.flags = sec->is_code() ? (m::S_ATTR_PURE_INSTRUCTIONS | m::S_ATTR_SOME_INSTRUCTIONS) : m::S_REGULAR;
            }
            layouts.push_back(l);
        }

        // 3. Layout calculation
        const uint32_t header_size    = sizeof(m::Header64);
        const uint32_t nsects         = static_cast<uint32_t>(layouts.size());
        const uint32_t seg_cmd_size   = sizeof(m::SegmentCommand64) + nsects * sizeof(m::Section64);
        const uint32_t symtab_cmd_sz  = sizeof(m::SymtabCommand);
        const uint32_t total_cmds_sz  = seg_cmd_size + symtab_cmd_sz;

        uint32_t cur_offset = header_size + total_cmds_sz;
        uint64_t cur_vmaddr = 0;
        uint64_t total_filesize = 0;

        for (auto& layout : layouts) {
            const uint64_t align_bytes = 1ULL << layout.align_p2;
            cur_vmaddr = (cur_vmaddr + (align_bytes - 1)) & ~(align_bytes - 1);
            layout.vm_addr = cur_vmaddr;

            if (layout.is_zerofill) {
                layout.file_offset = 0;
                layout.size = layout.sec->bytes.empty() ? 64 : layout.sec->bytes.size();
            } else {
                cur_offset = static_cast<uint32_t>((cur_offset + (align_bytes - 1)) & ~(align_bytes - 1));
                layout.file_offset = cur_offset;
                layout.size = layout.sec->bytes.size();
                cur_offset += static_cast<uint32_t>(layout.size);
                total_filesize = std::max<uint64_t>(total_filesize, static_cast<uint64_t>(cur_offset));
            }
            cur_vmaddr += layout.size;
        }

        // 4. Symbols and Strings table
        std::string strtab;
        strtab.push_back('\0'); // Mach-O string table starts with \0

        struct SymRecord {
            uint32_t strx{0};
            uint8_t  type{0};
            uint8_t  sect_idx{0}; // 1-based index
            uint16_t desc{0};
            uint64_t value{0};
        };
        std::vector<SymRecord> sym_records;

        for (const auto& sym : obj.symbols()) {
            std::string macho_name = sym.name;
            if (!macho_name.empty() && macho_name.front() != '_') {
                macho_name.insert(macho_name.begin(), '_');
            }
            const uint32_t strx = static_cast<uint32_t>(strtab.size());
            strtab.append(macho_name);
            strtab.push_back('\0');

            SymRecord rec{};
            rec.strx = strx;
            rec.desc = 0;

            if (sym.external) {
                rec.type = m::N_UNDF_TYPE | m::N_EXT_FLAG;
                rec.sect_idx = 0;
                rec.value = 0;
            } else {
                rec.type = m::N_SECT_TYPE | (sym.is_global() ? m::N_EXT_FLAG : 0);
                uint8_t target_sect = 1;
                if (sym.section_index < obj.sections().size()) {
                    const auto* target_sec = &obj.sections()[sym.section_index];
                    for (size_t k = 0; k < layouts.size(); ++k) {
                        if (layouts[k].sec == target_sec) {
                            target_sect = static_cast<uint8_t>(k + 1);
                            break;
                        }
                    }
                }
                rec.sect_idx = target_sect;
                rec.value = layouts[target_sect - 1].vm_addr + sym.value;
            }
            sym_records.push_back(rec);
        }

        // Ensure at least entry symbol exists
        if (sym_records.empty()) {
            const std::string entry_name = "_DGTL_kernel_entry";
            const uint32_t strx = static_cast<uint32_t>(strtab.size());
            strtab.append(entry_name);
            strtab.push_back('\0');

            SymRecord rec{};
            rec.strx = strx;
            rec.type = m::N_SECT_TYPE | m::N_EXT_FLAG;
            rec.sect_idx = 1;
            rec.desc = 0;
            rec.value = layouts[0].vm_addr;
            sym_records.push_back(rec);
        }

        // 5. Relocations layout
        cur_offset = (cur_offset + 3) & ~3;
        std::vector<std::vector<m::RelocationInfo>> sec_relocs(nsects);
        for (const auto& r : obj.relocations()) {
            m::RelocationInfo ri{};
            ri.r_address = static_cast<int32_t>(r.offset);
            uint32_t s_idx = 0;
            for (uint32_t i = 0; i < obj.symbols().size(); ++i) {
                if (obj.symbols()[i].name == r.symbol_name) {
                    s_idx = i;
                    break;
                }
            }

            uint32_t pcrel = 1u;
            uint32_t length = 2u; // 2^2 = 4 bytes
            uint32_t r_type = m::ARM64_RELOC_BRANCH26;

            if (r.kind == dgtl::object::RelocationKind::Absolute64) {
                pcrel = 0u;
                length = 3u; // 2^3 = 8 bytes
                r_type = m::ARM64_RELOC_UNSIGNED;
            } else if (r.kind == dgtl::object::RelocationKind::Absolute32) {
                pcrel = 0u;
                length = 2u;
                r_type = m::ARM64_RELOC_UNSIGNED;
            } else if (r.kind == dgtl::object::RelocationKind::Branch26 || r.kind == dgtl::object::RelocationKind::Call26) {
                pcrel = 1u;
                length = 2u;
                r_type = m::ARM64_RELOC_BRANCH26;
            }

            ri.r_info = (s_idx & 0x00FFFFFFu)
                      | (pcrel << 24)
                      | (length << 25)
                      | (1u << 27) // r_extern = 1
                      | (r_type << 28);

            uint32_t target_sec_idx = 0;
            if (r.section_index < obj.sections().size()) {
                const auto* sec_ptr = &obj.sections()[r.section_index];
                for (size_t k = 0; k < layouts.size(); ++k) {
                    if (layouts[k].sec == sec_ptr) {
                        target_sec_idx = static_cast<uint32_t>(k);
                        break;
                    }
                }
            }
            if (target_sec_idx < nsects) {
                sec_relocs[target_sec_idx].push_back(ri);
            }
        }

        for (uint32_t i = 0; i < nsects; ++i) {
            if (!sec_relocs[i].empty()) {
                layouts[i].reloff = cur_offset;
                layouts[i].nreloc = static_cast<uint32_t>(sec_relocs[i].size());
                cur_offset += layouts[i].nreloc * sizeof(m::RelocationInfo);
            }
        }

        // 6. Symtab layout
        cur_offset = (cur_offset + 7) & ~7;
        const uint32_t symtab_offset = cur_offset;
        const uint32_t nsyms = static_cast<uint32_t>(sym_records.size());
        const uint32_t strtab_offset = symtab_offset + nsyms * sizeof(m::Nlist64);
        const uint32_t strtab_size = static_cast<uint32_t>(strtab.size());
        const uint32_t total_binary_sz = strtab_offset + strtab_size;

        out.assign(total_binary_sz, 0);
        uint8_t* buf = out.data();

        // 7. Write Header64
        auto* hdr = reinterpret_cast<m::Header64*>(buf);
        hdr->magic      = m::MH_MAGIC_64_VAL;
        hdr->cputype    = m::CPU_TYPE_ARM64_VAL;
        hdr->cpusubtype = m::CPU_SUBTYPE_ALL_VAL;
        hdr->filetype   = m::MH_OBJECT_TYPE;
        hdr->ncmds      = 2;
        hdr->sizeofcmds = total_cmds_sz;
        hdr->flags      = m::MH_SUBSECTIONS_VIA_SYMBOLS_FLAG;
        hdr->reserved   = 0;

        // 8. Write SegmentCommand64
        auto* seg = reinterpret_cast<m::SegmentCommand64*>(buf + header_size);
        seg->cmd      = m::LC_SEGMENT_64_CMD;
        seg->cmdsize  = seg_cmd_size;
        std::memset(seg->segname, 0, 16);
        seg->vmaddr   = 0;
        seg->vmsize   = cur_vmaddr;
        seg->fileoff  = layouts.empty() ? 0 : layouts[0].file_offset;
        seg->filesize = total_filesize;
        seg->maxprot  = 7;
        seg->initprot = 7;
        seg->nsects   = nsects;
        seg->flags    = 0;

        // 9. Write Section64 entries
        auto* sect_headers = reinterpret_cast<m::Section64*>(buf + header_size + sizeof(m::SegmentCommand64));
        for (uint32_t i = 0; i < nsects; ++i) {
            auto& sh = sect_headers[i];
            std::memset(&sh, 0, sizeof(m::Section64));
            std::memcpy(sh.sectname, layouts[i].sectname, 16);
            std::memcpy(sh.segname,  layouts[i].segname, 16);
            sh.addr     = layouts[i].vm_addr;
            sh.size     = layouts[i].size;
            sh.offset   = layouts[i].file_offset;
            sh.align    = layouts[i].align_p2;
            sh.reloff   = layouts[i].reloff;
            sh.nreloc   = layouts[i].nreloc;
            sh.flags    = layouts[i].flags;
        }

        // 10. Write SymtabCommand
        auto* symcmd = reinterpret_cast<m::SymtabCommand*>(buf + header_size + seg_cmd_size);
        symcmd->cmd     = m::LC_SYMTAB_CMD;
        symcmd->cmdsize = symtab_cmd_sz;
        symcmd->symoff  = symtab_offset;
        symcmd->nsyms   = nsyms;
        symcmd->stroff  = strtab_offset;
        symcmd->strsize = strtab_size;

        // 11. Write Section Data
        for (uint32_t i = 0; i < nsects; ++i) {
            if (!layouts[i].is_zerofill && layouts[i].file_offset != 0 && layouts[i].sec && !layouts[i].sec->bytes.empty()) {
                std::memcpy(buf + layouts[i].file_offset, layouts[i].sec->bytes.data(), layouts[i].sec->bytes.size());
            }
        }

        // 12. Write Relocations
        for (uint32_t i = 0; i < nsects; ++i) {
            if (layouts[i].nreloc > 0) {
                std::memcpy(buf + layouts[i].reloff, sec_relocs[i].data(), layouts[i].nreloc * sizeof(m::RelocationInfo));
            }
        }

        // 13. Write Symbol Table
        auto* symtab = reinterpret_cast<m::Nlist64*>(buf + symtab_offset);
        for (uint32_t i = 0; i < nsyms; ++i) {
            symtab[i].n_strx  = sym_records[i].strx;
            symtab[i].n_type  = sym_records[i].type;
            symtab[i].n_sect  = sym_records[i].sect_idx;
            symtab[i].n_desc  = sym_records[i].desc;
            symtab[i].n_value = sym_records[i].value;
        }

        // 14. Write String Table
        std::memcpy(buf + strtab_offset, strtab.data(), strtab_size);

        return true;
    }

    [[nodiscard]] bool write(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out) const {
        return write_machine_function(function, out, "_DGTL_kernel_entry");
    }

    [[nodiscard]] bool write(
        const uint8_t* code,
        size_t size,
        std::vector<uint8_t>& out) const {
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::MachO);
        auto& text_sec = obj.get_text_section();
        if (code && size > 0) {
            text_sec.bytes.assign(code, code + size);
        } else {
            const uint32_t ret_insn = 0xD65F03C0u;
            text_sec.bytes.resize(4);
            std::memcpy(text_sec.bytes.data(), &ret_insn, 4);
        }
        obj.add_function_symbol("DGTL_kernel_entry", 0, text_sec.bytes.size(), true);
        return write(obj, out);
    }

    [[nodiscard]] MachOResult emit_to_binary() const {
        MachOResult result;
        if (m_function != nullptr) {
            result.success = write_machine_function(*m_function, result.binary_data, "_DGTL_kernel_entry");
        } else {
            const uint32_t ret_insn = 0xD65F03C0u;
            result.success = write(reinterpret_cast<const uint8_t*>(&ret_insn), sizeof(ret_insn), result.binary_data);
        }
        return result;
    }

    [[nodiscard]] bool save_to_file(const std::string& filename) const {
        auto result = emit_to_binary();
        if (!result.success) return false;

        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(result.binary_data.data()), 
                   result.binary_data.size());
        return file.good();
    }

private:
    const dgtl::machine::MachineFunction* m_function{nullptr};

    [[nodiscard]] static bool serialize_macho(
        const std::vector<uint8_t>& text_data,
        const std::vector<dgtl::isa::arm64::ARM64Emitter::RuntimeBranchReloc>& runtime_relocs,
        std::string_view entry_name,
        std::vector<uint8_t>& out) {

        namespace m = dgtl::object::writers::macho;

        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::MachO);
        auto& text_sec = obj.get_text_section();
        text_sec.bytes = text_data;

        std::string ename = entry_name.empty() ? "_DGTL_kernel_entry" : std::string(entry_name);
        obj.add_function_symbol(ename, 0, text_data.size(), true);

        for (const auto& r : runtime_relocs) {
            obj.add_external_symbol(r.symbol);
            obj.add_relocation(r.text_offset, r.symbol, 0, m::ARM64_RELOC_BRANCH26, dgtl::object::RelocationKind::PCRel32);
        }

        MachOObjectWriter writer;
        return writer.write(obj, out);
    }
};

} // namespace dgtl::object::writers

export namespace dgtl::backend::object {
    using MachOObjectWriter = dgtl::object::writers::MachOObjectWriter;
}

// Canonical backward compatibility aliases
export namespace dgtl::isa::macho {
    using MachOEmitter = dgtl::object::writers::MachOObjectWriter;
    using MachOEmitterAlias = dgtl::object::writers::MachOObjectWriter;
    using Header64 = dgtl::object::writers::macho::Header64;
    using SegmentCommand64 = dgtl::object::writers::macho::SegmentCommand64;
    using Section64 = dgtl::object::writers::macho::Section64;
    using SymtabCommand = dgtl::object::writers::macho::SymtabCommand;
    using Nlist64 = dgtl::object::writers::macho::Nlist64;
    using RelocationInfo = dgtl::object::writers::macho::RelocationInfo;
}

export namespace dgtl::isa {
    using MachOEmitter = dgtl::object::writers::MachOObjectWriter;
}



