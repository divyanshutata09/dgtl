/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Universal Object File Container (ELF/COFF/Mach-O/WASM)
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.object.file;

import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.target;
import dgtl.mir.machine_ir;

export namespace dgtl::object {

enum class ObjectFormat : std::uint8_t {
    ELF,
    COFF,
    MachO,
    WASM
};

class ObjectFile {
    ObjectFormat format_ = ObjectFormat::COFF;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
    std::vector<Relocation> relocations_;

public:
    ObjectFile() = default;
    explicit ObjectFile(ObjectFormat fmt) : format_(fmt) {}

    [[nodiscard]] ObjectFormat format() const noexcept { return format_; }
    void set_format(ObjectFormat fmt) noexcept { format_ = fmt; }

    Section& add_section(std::string name, std::uint64_t align = 16u, std::uint32_t flags = SectionFlags::Code | SectionFlags::Read | SectionFlags::Executable, SectionKind kind = SectionKind::Text) {
        sections_.emplace_back(std::move(name), align, flags, kind);
        return sections_.back();
    }

    [[nodiscard]] Section* find_section(std::string_view name) noexcept {
        for (auto& sec : sections_) {
            if (sec.name == name) return &sec;
        }
        return nullptr;
    }

    [[nodiscard]] const Section* find_section(std::string_view name) const noexcept {
        for (const auto& sec : sections_) {
            if (sec.name == name) return &sec;
        }
        return nullptr;
    }

    Section& get_or_create_section(std::string_view name, SectionKind kind = SectionKind::Text) {
        if (auto* sec = find_section(name)) return *sec;
        std::uint32_t flags = SectionFlags::Read;
        if (kind == SectionKind::Text) flags |= SectionFlags::Code | SectionFlags::Executable;
        else if (kind == SectionKind::Data || kind == SectionKind::BSS) flags |= SectionFlags::Write;
        return add_section(std::string(name), 16u, flags, kind);
    }

    Section& get_text_section() {
        return get_or_create_section(format_ == ObjectFormat::MachO ? "__text" : ".text", SectionKind::Text);
    }

    Section& get_data_section() {
        return get_or_create_section(format_ == ObjectFormat::MachO ? "__data" : ".data", SectionKind::Data);
    }

    Section& get_bss_section() {
        return get_or_create_section(format_ == ObjectFormat::MachO ? "__bss" : ".bss", SectionKind::BSS);
    }

    Section& get_rdata_section() {
        return get_or_create_section(format_ == ObjectFormat::COFF ? ".rdata" : (format_ == ObjectFormat::MachO ? "__const" : ".rodata"), SectionKind::RData);
    }

    Section& get_rodata_section() {
        return get_rdata_section();
    }

    Section& get_pdata_section() {
        return get_or_create_section(format_ == ObjectFormat::COFF ? ".pdata" : (format_ == ObjectFormat::MachO ? "__unwind_info" : ".eh_frame"), SectionKind::ExceptionTable);
    }

    Section& get_xdata_section() {
        return get_or_create_section(format_ == ObjectFormat::COFF ? ".xdata" : (format_ == ObjectFormat::MachO ? "__eh_frame" : ".eh_frame_hdr"), SectionKind::ExceptionInfo);
    }

    Section& get_eh_frame_section() {
        return get_or_create_section(format_ == ObjectFormat::COFF ? ".pdata" : (format_ == ObjectFormat::MachO ? "__eh_frame" : ".eh_frame"), SectionKind::ExceptionTable);
    }

    Section& get_tls_section() {
        return get_or_create_section(format_ == ObjectFormat::COFF ? ".tls$" : (format_ == ObjectFormat::MachO ? "__thread_data" : ".tdata"), SectionKind::TLS);
    }

    Symbol& add_symbol(std::string name, std::uint64_t value, std::uint64_t size, std::uint32_t binding = 0, std::uint32_t type = 0) {
        symbols_.emplace_back(std::move(name), value, size, binding, type, false);
        return symbols_.back();
    }

    Symbol& add_function_symbol(std::string name, std::uint64_t offset, std::uint64_t size, bool is_global = true) {
        symbols_.push_back(Symbol::make_function(std::move(name), offset, size, is_global));
        return symbols_.back();
    }

    Symbol& add_external_symbol(std::string name,
                                 std::uint32_t binding = 1,
                                 std::uint32_t type = 0) {
        const SymbolType symbol_type =
            type == 2 ? SymbolType::Function :
            type == 1 ? SymbolType::Object :
            type == 3 ? SymbolType::Section :
            type == 4 ? SymbolType::File :
            type == 5 ? SymbolType::Common :
            type == 6 ? SymbolType::TLS :
                        SymbolType::NoType;
        symbols_.push_back(Symbol::make_external(
            std::move(name), symbol_type, binding));
        return symbols_.back();
    }

    [[nodiscard]] Symbol* find_symbol(std::string_view name) noexcept {
        for (auto& sym : symbols_) {
            if (sym.name == name) return &sym;
        }
        return nullptr;
    }

    [[nodiscard]] const Symbol* find_symbol(std::string_view name) const noexcept {
        for (const auto& sym : symbols_) {
            if (sym.name == name) return &sym;
        }
        return nullptr;
    }

    void add_relocation(std::uint64_t offset, std::string sym_name, std::int64_t addend = 0, std::uint32_t type = 0, RelocationKind kind = RelocationKind::PCRel32) {
        relocations_.emplace_back(offset, std::move(sym_name), addend, type, kind);
    }

    void add_relocation(Relocation rel) {
        relocations_.push_back(std::move(rel));
    }

    [[nodiscard]] const std::vector<Section>& sections() const noexcept { return sections_; }
    [[nodiscard]] std::vector<Section>& sections() noexcept { return sections_; }

    [[nodiscard]] const std::vector<Symbol>& symbols() const noexcept { return symbols_; }
    [[nodiscard]] std::vector<Symbol>& symbols() noexcept { return symbols_; }

    [[nodiscard]] const std::vector<Relocation>& relocations() const noexcept { return relocations_; }
    [[nodiscard]] std::vector<Relocation>& relocations() noexcept { return relocations_; }

    /**
     * @brief Construct an ObjectFile directly from a finalized MIR MachineFunction.
     */
    static ObjectFile from_machine_function(
        const machine::MachineFunction& function,
        ObjectFormat fmt = ObjectFormat::COFF) {

        ObjectFile obj(fmt);
        auto& text = obj.get_text_section();
        const std::uint64_t func_offset = text.size();

        // Register function symbol
        obj.add_function_symbol("DGTL_kernel_entry", func_offset, 0u, true);

        // Scan machine instructions for symbol references and record relocations
        for (const auto& inst : function.instructions()) {
            for (const auto& op : inst.operands) {
                if (op.kind == machine::MachineOperandKind::Symbol) {
                    const std::string sym_name = "sym_" + std::to_string(op.symbol);
                    if (!obj.find_symbol(sym_name)) {
                        obj.add_external_symbol(sym_name);
                    }
                    (void)sym_name;
                }
            }
        }

        const auto& frame = function.frame_info();

        if (fmt == ObjectFormat::COFF) {
            // Synthesize Win64 SEH Exception Unwind structures (.xdata and .pdata)
            auto& xdata = obj.get_xdata_section();
            const std::uint32_t xdata_rva = static_cast<std::uint32_t>(xdata.size());

            // UNWIND_INFO: Version=1, Flags=0, SizeOfProlog, CountOfCodes, FrameRegister=5 (RBP), FrameOffset=0
            const std::uint8_t prolog_size = frame.total_frame_bytes > 0 ? 8 : 4;
            xdata.emit_u8(0x01); // Version 1, Flags 0
            xdata.emit_u8(prolog_size);
            xdata.emit_u8(2);    // 2 unwind codes
            xdata.emit_u8(0x05); // RBP frame pointer

            // UWOP_SET_FPREG (3): offset prolog_size
            xdata.emit_u8(prolog_size);
            xdata.emit_u8(0x03);

            // UWOP_PUSH_NONVOL (0): reg 5 (RBP), offset 1
            xdata.emit_u8(0x01);
            xdata.emit_u8(0x50); // (5 << 4) | 0

            // RUNTIME_FUNCTION in .pdata (12 bytes: BeginAddress, EndAddress, UnwindInfoAddress)
            auto& pdata = obj.get_pdata_section();
            pdata.emit_u32(static_cast<std::uint32_t>(func_offset));
            pdata.emit_u32(static_cast<std::uint32_t>(func_offset + (text.empty() ? 64 : text.size())));
            pdata.emit_u32(xdata_rva);
        } else if (fmt == ObjectFormat::ELF) {
            // Synthesize Linux/ELF DWARF Call Frame Information (.eh_frame)
            auto& eh_frame = obj.get_eh_frame_section();

            // DWARF CIE (Common Information Entry)
            eh_frame.emit_u32(20); // Length
            eh_frame.emit_u32(0);  // CIE ID
            eh_frame.emit_u8(1);   // Version
            eh_frame.emit_u8('z'); eh_frame.emit_u8('R'); eh_frame.emit_u8('\0');
            eh_frame.emit_u8(1);   // Code alignment
            eh_frame.emit_u8(static_cast<std::uint8_t>(-8)); // Data alignment (-8)
            eh_frame.emit_u8(16);  // Return register (x86_64 RIP)
            eh_frame.emit_u8(1);   // Augmentation data length
            eh_frame.emit_u8(0x1B); // DW_EH_PE_pcrel | DW_EH_PE_sdata4
            eh_frame.emit_u8(0x0C); eh_frame.emit_u8(7); eh_frame.emit_u8(8); // DW_CFA_def_cfa rsp, 8
            eh_frame.emit_u8(0x90); eh_frame.emit_u8(1); // DW_CFA_offset rip, 1
            eh_frame.emit_u8(0x00); // padding

            // DWARF FDE (Frame Description Entry)
            eh_frame.emit_u32(24); // Length
            eh_frame.emit_u32(28); // CIE offset
            eh_frame.emit_u32(static_cast<std::uint32_t>(func_offset)); // PC Begin
            eh_frame.emit_u32(static_cast<std::uint32_t>(text.empty() ? 64 : text.size())); // PC Range
            eh_frame.emit_u8(0);   // Augmentation data length
            eh_frame.emit_u8(0x41); // DW_CFA_advance_loc 1
            eh_frame.emit_u8(0x0E); eh_frame.emit_u8(16); // DW_CFA_def_cfa_offset 16
            eh_frame.emit_u8(0x86); eh_frame.emit_u8(2);  // DW_CFA_offset rbp, 2
            eh_frame.emit_u8(0x00); eh_frame.emit_u8(0x00); // padding
        } else if (fmt == ObjectFormat::MachO) {
            // Synthesize Darwin/Mach-O Compact Unwind (__unwind_info) and DWARF fallback (__eh_frame)
            auto& unwind_sec = obj.get_pdata_section(); // resolves to __unwind_info on Mach-O

            // Compact Unwind section header (28 bytes)
            unwind_sec.emit_u32(1);  // version = 1
            unwind_sec.emit_u32(28); // commonEncodingsArraySectionOffset
            unwind_sec.emit_u32(1);  // commonEncodingsArrayCount (1 common encoding)
            unwind_sec.emit_u32(32); // personalityArraySectionOffset
            unwind_sec.emit_u32(0);  // personalityArrayCount (0)
            unwind_sec.emit_u32(32); // indicesSectionOffset
            unwind_sec.emit_u32(1);  // indicesCount (1 index entry)

            // Common encoding 0: Standard frame-based encoding (0x01000000)
            unwind_sec.emit_u32(0x01000000);

            // Index entry: function offset, second-level page offset, LSDA offset
            unwind_sec.emit_u32(static_cast<std::uint32_t>(func_offset));
            unwind_sec.emit_u32(0);
            unwind_sec.emit_u32(0);

            // Also emit __eh_frame for Mach-O DWARF fallback
            auto& eh_frame = obj.get_xdata_section(); // resolves to __eh_frame on Mach-O
            eh_frame.emit_u32(20);
            eh_frame.emit_u32(0);
            eh_frame.emit_u8(1);
            eh_frame.emit_u8('z'); eh_frame.emit_u8('R'); eh_frame.emit_u8('\0');
            eh_frame.emit_u8(1);
            eh_frame.emit_u8(static_cast<std::uint8_t>(-8));
            eh_frame.emit_u8(16);
            eh_frame.emit_u8(1);
            eh_frame.emit_u8(0x1B);
            eh_frame.emit_u8(0x0C); eh_frame.emit_u8(7); eh_frame.emit_u8(8);
            eh_frame.emit_u8(0x90); eh_frame.emit_u8(1);
            eh_frame.emit_u8(0x00);
        }

        return obj;
    }
};

} // namespace dgtl::object



