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

export import dgtl.object.section;
export import dgtl.object.symbol;
export import dgtl.object.relocation;
export import dgtl.target;

export namespace dgtl::object {

enum class ObjectFormat : std::uint8_t {
    ELF,
    COFF,
    PE,
    MachO,
    WASM
};

class ObjectFile {
    ObjectFormat format_ = ObjectFormat::COFF;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
    std::vector<Relocation> relocations_;

public:
    ObjectFile() noexcept;
    explicit ObjectFile(ObjectFormat fmt) noexcept;

    ObjectFile(ObjectFile&& other) noexcept;
    ObjectFile& operator=(ObjectFile&& other) noexcept;
    ObjectFile(const ObjectFile& other);
    ObjectFile& operator=(const ObjectFile& other);
    ~ObjectFile();

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
        return get_or_create_section((format_ == ObjectFormat::COFF || format_ == ObjectFormat::PE) ? ".rdata" : (format_ == ObjectFormat::MachO ? "__const" : ".rodata"), SectionKind::RData);
    }

    Section& get_rodata_section() {
        return get_rdata_section();
    }

    Section& get_pdata_section() {
        return get_or_create_section((format_ == ObjectFormat::COFF || format_ == ObjectFormat::PE) ? ".pdata" : (format_ == ObjectFormat::MachO ? "__unwind_info" : ".eh_frame"), SectionKind::ExceptionTable);
    }

    Section& get_xdata_section() {
        return get_or_create_section((format_ == ObjectFormat::COFF || format_ == ObjectFormat::PE) ? ".xdata" : (format_ == ObjectFormat::MachO ? "__eh_frame" : ".eh_frame_hdr"), SectionKind::ExceptionInfo);
    }

    Section& get_eh_frame_section() {
        return get_or_create_section((format_ == ObjectFormat::COFF || format_ == ObjectFormat::PE) ? ".pdata" : (format_ == ObjectFormat::MachO ? "__eh_frame" : ".eh_frame"), SectionKind::ExceptionTable);
    }

    Section& get_tls_section() {
        return get_or_create_section((format_ == ObjectFormat::COFF || format_ == ObjectFormat::PE) ? ".tls$" : (format_ == ObjectFormat::MachO ? "__thread_data" : ".tdata"), SectionKind::TLS);
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

    void add_relocation(std::uint64_t offset, std::string sym_name, std::int64_t addend = 0, std::uint32_t type = 0, RelocationKind kind = RelocationKind::PCRel32, std::uint32_t section_index = 0) {
        relocations_.emplace_back(offset, std::move(sym_name), addend, type, kind, section_index);
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

};
 
ObjectFile::ObjectFile() noexcept = default;
ObjectFile::ObjectFile(ObjectFormat fmt) noexcept : format_(fmt) {}

ObjectFile::ObjectFile(ObjectFile&& other) noexcept
    : format_(other.format_)
    , sections_(std::move(other.sections_))
    , symbols_(std::move(other.symbols_))
    , relocations_(std::move(other.relocations_)) {}

ObjectFile& ObjectFile::operator=(ObjectFile&& other) noexcept {
    if (this != &other) {
        format_ = other.format_;
        sections_ = std::move(other.sections_);
        symbols_ = std::move(other.symbols_);
        relocations_ = std::move(other.relocations_);
    }
    return *this;
}

ObjectFile::ObjectFile(const ObjectFile& other)
    : format_(other.format_)
    , sections_(other.sections_)
    , symbols_(other.symbols_)
    , relocations_(other.relocations_) {}

ObjectFile& ObjectFile::operator=(const ObjectFile& other) {
    if (this != &other) {
        format_ = other.format_;
        sections_ = other.sections_;
        symbols_ = other.symbols_;
        relocations_ = other.relocations_;
    }
    return *this;
}

ObjectFile::~ObjectFile() = default;

} // namespace dgtl::object
