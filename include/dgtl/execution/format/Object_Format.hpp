/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <memory>

namespace dgtl {
namespace execution {

/**
 * @enum RelocationType
 * @brief ELF relocation types (x86-64 specific).
 */
enum class RelocationType : uint8_t {
    Absolute = 0,      ///< R_X86_64_64 (direct 64-bit)
    PCRelative = 1,    ///< R_X86_64_PC32 (32-bit PC-relative)
    Relative = 2,      ///< R_X86_64_RELATIVE (load-time constant)
    GOT = 3,           ///< R_X86_64_GOT32 (Global Offset Table)
    PLT = 4            ///< R_X86_64_PLT32 (Procedure Linkage Table)
};

/**
 * @struct Symbol
 * @brief ELF symbol table entry.
 */
struct Symbol {
    std::string name;
    uint64_t address = 0;       ///< Symbol value (address)
    uint64_t size = 0;          ///< Symbol size in bytes
    uint8_t binding = 0;        ///< STB_LOCAL, STB_GLOBAL, STB_WEAK
    uint8_t type = 0;           ///< STT_NOTYPE, STT_OBJECT, STT_FUNC, etc.
    uint16_t section_idx = 0;   ///< Section index (SHN_*)
    bool is_extern = false;     ///< External symbol reference
};

/**
 * @struct Relocation
 * @brief ELF relocation record.
 */
struct Relocation {
    uint64_t offset = 0;        ///< Offset within section
    std::string symbol_name;    ///< Symbol to apply relocation against
    int64_t addend = 0;         ///< Addend for RELA-type relocations
    RelocationType type = RelocationType::Absolute;
};

/**
 * @struct Section
 * @brief ELF section container.
 */
struct Section {
    std::vector<uint8_t> data;
    uint64_t base_address = 0;
    uint64_t alignment = 1;
    uint32_t flags = 0;         ///< SHF_* flags (alloc, write, exec)
};

/**
 * @class Object_Format
 * @brief Manages ELF object file format with sections, symbols, and relocations.
 */
class Object_Format {
public:
    Object_Format() = default;
    ~Object_Format() = default;

    Object_Format(const Object_Format&) = delete;
    Object_Format& operator=(const Object_Format&) = delete;

    /**
     * @brief Find symbol address by name (O(1) lookup).
     * @param name Symbol name
     * @return Address if found, std::nullopt otherwise
     */
    [[nodiscard]] std::optional<uint64_t> find_symbol_address(std::string_view name) const noexcept {
        auto it = symbol_map_.find(std::string(name));
        if (it != symbol_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Add a symbol to the symbol table.
     * @param name Symbol name
     * @param addr Symbol address
     * @param size Symbol size
     * @param binding Symbol binding (local/global/weak)
     * @param type Symbol type (object/func/etc.)
     */
    void add_symbol(std::string_view name, uint64_t addr, uint64_t size, 
                   uint8_t binding = 0, uint8_t type = 0) noexcept {
        Symbol sym{std::string(name), addr, size, binding, type, 0, false};
        symbols.push_back(sym);
        symbol_map_[std::string(name)] = addr;
    }

    /**
     * @brief Add an external symbol reference.
     * @param name External symbol name
     * @param binding Symbol binding
     */
    void add_extern_symbol(std::string_view name, uint8_t binding = 1) noexcept {
        Symbol sym{std::string(name), 0, 0, binding, 0, 0, true};
        symbols.push_back(sym);
    }

    /**
     * @brief Add a relocation record.
     * @param offset Offset within section
     * @param symbol Symbol name to relocate against
     * @param addend Addend (for RELA)
     * @param type Relocation type
     */
    void add_relocation(uint64_t offset, std::string_view symbol, 
                       int64_t addend = 0, RelocationType type = RelocationType::Absolute) noexcept {
        relocations.push_back({offset, std::string(symbol), addend, type});
    }

    // ===== SECTION ACCESS =====

    /**
     * @brief Get mutable text section (.text).
     */
    [[nodiscard]] Section& get_text_section() noexcept { return text_section; }
    
    /**
     * @brief Get mutable data section (.data).
     */
    [[nodiscard]] Section& get_data_section() noexcept { return data_section; }
    
    /**
     * @brief Get mutable read-only data section (.rodata).
     */
    [[nodiscard]] Section& get_rodata_section() noexcept { return rodata_section; }
    
    /**
     * @brief Get mutable BSS section (.bss).
     */
    [[nodiscard]] Section& get_bss_section() noexcept { return bss_section; }

    /**
     * @brief Get const text section.
     */
    [[nodiscard]] const Section& get_text_section() const noexcept { return text_section; }
    
    /**
     * @brief Get const data section.
     */
    [[nodiscard]] const Section& get_data_section() const noexcept { return data_section; }
    
    /**
     * @brief Get const rodata section.
     */
    [[nodiscard]] const Section& get_rodata_section() const noexcept { return rodata_section; }
    
    /**
     * @brief Get const BSS section.
     */
    [[nodiscard]] const Section& get_bss_section() const noexcept { return bss_section; }

    // ===== SYMBOL/RELOCATION ACCESS =====

    /**
     * @brief Get all symbols.
     */
    [[nodiscard]] const std::vector<Symbol>& get_symbols() const noexcept { return symbols; }
    
    /**
     * @brief Get all relocations.
     */
    [[nodiscard]] const std::vector<Relocation>& get_relocations() const noexcept { return relocations; }

    /**
     * @brief Get symbol count.
     */
    [[nodiscard]] size_t symbol_count() const noexcept { return symbols.size(); }

    /**
     * @brief Get relocation count.
     */
    [[nodiscard]] size_t relocation_count() const noexcept { return relocations.size(); }

private:
    Section text_section;
    Section data_section;
    Section rodata_section;
    Section bss_section;
    
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    std::unordered_map<std::string, uint64_t> symbol_map_;
};

} // namespace execution
} // namespace dgtl
