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

#include "dgtl/execution/format/Object_Format.hpp"
#include <string>
#include <string_view>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <vector>

namespace dgtl {
namespace execution {

/**
 * @class Object_Loader
 * @brief Loads and parses ELF object files (.o format).
 * 
 * Extracts sections, symbol tables, and relocations from ELF binaries
 * for linker input or JIT compilation.
 */
class Object_Loader {
public:
    Object_Loader() = default;
    ~Object_Loader() = default;

    Object_Loader(const Object_Loader&) = delete;
    Object_Loader& operator=(const Object_Loader&) = delete;

    /**
     * @brief Load ELF object file from path.
     * @param filepath Path to .o file
     * @return Unique pointer to parsed Object_Format
     * @throws std::runtime_error on load/parse failure
     */
    [[nodiscard]] std::unique_ptr<Object_Format> load(const std::string& filepath) {
        auto obj = std::make_unique<Object_Format>();
        
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open object file: " + filepath);
        
        std::streamsize size = file.tellg();
        if (size < 64) throw std::runtime_error("Object file too small");
        
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            throw std::runtime_error("Failed to read object file");
        }
        
        if (!is_elf_format(buffer)) {
            throw std::runtime_error("Unsupported object format (not ELF)");
        }
        
        parse_elf_object(obj.get(), buffer);
        return obj;
    }

    /**
     * @brief Load ELF object from memory.
     * @param data Pointer to object data
     * @param size Size in bytes
     * @return Unique pointer to parsed Object_Format
     * @throws std::runtime_error on parse failure
     */
    [[nodiscard]] std::unique_ptr<Object_Format> load_from_memory(const void* data, size_t size) {
        if (!data || size < 64) throw std::runtime_error("Invalid object data");
        
        std::vector<uint8_t> buffer(static_cast<const uint8_t*>(data),
                                    static_cast<const uint8_t*>(data) + size);
        
        if (!is_elf_format(buffer)) {
            throw std::runtime_error("Unsupported object format");
        }
        
        auto obj = std::make_unique<Object_Format>();
        parse_elf_object(obj.get(), buffer);
        return obj;
    }

private:
    /**
     * @brief Check if buffer is ELF format.
     */
    [[nodiscard]] static bool is_elf_format(const std::vector<uint8_t>& buffer) noexcept {
        return buffer.size() >= 4 && 
               buffer[0] == 0x7f && buffer[1] == 'E' && 
               buffer[2] == 'L' && buffer[3] == 'F';
    }

    /**
     * @brief Parse complete ELF object file.
     */
    static void parse_elf_object(Object_Format* obj, const std::vector<uint8_t>& buffer) {
        if (buffer.size() < 64) throw std::runtime_error("ELF header too small");
        
        // Read ELF header fields (64-bit)
        uint64_t e_shoff = 0;
        uint16_t e_shnum = 0, e_shentsize = 0, e_shstrndx = 0;
        
        std::memcpy(&e_shoff, buffer.data() + 32, 8);
        std::memcpy(&e_shentsize, buffer.data() + 46, 2);
        std::memcpy(&e_shnum, buffer.data() + 48, 2);
        std::memcpy(&e_shstrndx, buffer.data() + 50, 2);
        
        if (e_shoff == 0 || e_shnum == 0) {
            throw std::runtime_error("Invalid ELF section headers");
        }
        
        parse_sections(obj, buffer, e_shoff, e_shentsize, e_shnum);
        parse_symbols(obj, buffer, e_shoff, e_shentsize, e_shnum);
        parse_relocations(obj, buffer, e_shoff, e_shentsize, e_shnum);
    }

    /**
     * @brief Extract sections from ELF section headers.
     */
    static void parse_sections(Object_Format* obj, const std::vector<uint8_t>& buffer,
                              uint64_t sh_offset, uint16_t sh_entsize, uint16_t sh_num) noexcept {
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) break;
            
            // Section header (64-bit ELF):
            // Offset 0: sh_name (4)
            // Offset 4: sh_type (4)
            // Offset 8: sh_flags (8)
            // Offset 16: sh_addr (8)
            // Offset 24: sh_offset (8)
            // Offset 32: sh_size (8)
            
            const uint8_t* sh = buffer.data() + offset;
            
            uint32_t sh_type = 0;
            uint64_t sh_data_offset = 0, sh_size = 0, sh_addr = 0;
            
            std::memcpy(&sh_type, sh + 4, 4);
            std::memcpy(&sh_addr, sh + 16, 8);
            std::memcpy(&sh_data_offset, sh + 24, 8);
            std::memcpy(&sh_size, sh + 32, 8);
            
            if (sh_data_offset + sh_size > buffer.size()) continue;
            
            Section sec;
            sec.data.assign(buffer.begin() + sh_data_offset,
                          buffer.begin() + sh_data_offset + sh_size);
            sec.base_address = sh_addr;
            
            // sh_type: 1 = PROGBITS (.text, .data), 8 = NOBITS (.bss)
            if (sh_type == 1) {
                obj->get_text_section() = sec;
            }
        }
    }

    /**
     * @brief Extract symbol table.
     */
    static void parse_symbols(Object_Format* obj, const std::vector<uint8_t>& buffer,
                             uint64_t sh_offset, uint16_t sh_entsize, uint16_t sh_num) noexcept {
        // Find .symtab section (sh_type == 3)
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) break;
            
            const uint8_t* sh = buffer.data() + offset;
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, sh + 4, 4);
            
            if (sh_type != 3) continue; // SHT_SYMTAB
            
            // Parse symbol entries in this section
            uint64_t sym_offset = 0, sym_size = 0, sym_entsize = 0;
            std::memcpy(&sym_offset, sh + 24, 8);
            std::memcpy(&sym_size, sh + 32, 8);
            std::memcpy(&sym_entsize, sh + 56, 8);
            
            if (sym_entsize == 0 || sym_size == 0) continue;
            
            uint64_t sym_count = sym_size / sym_entsize;
            for (uint64_t j = 0; j < sym_count; ++j) {
                uint64_t sym_entry_offset = sym_offset + (j * sym_entsize);
                if (sym_entry_offset + 24 > buffer.size()) break;
                
                const uint8_t* sym = buffer.data() + sym_entry_offset;
                
                // Symbol entry (64-bit):
                // Offset 0: st_name (4)
                // Offset 4: st_info (1)
                // Offset 5: st_other (1)
                // Offset 6: st_shndx (2)
                // Offset 8: st_value (8)
                // Offset 16: st_size (8)
                
                uint32_t st_name = 0;
                uint8_t st_info = 0;
                uint16_t st_shndx = 0;
                uint64_t st_value = 0, st_size = 0;
                
                std::memcpy(&st_name, sym + 0, 4);
                std::memcpy(&st_info, sym + 4, 1);
                std::memcpy(&st_shndx, sym + 6, 2);
                std::memcpy(&st_value, sym + 8, 8);
                std::memcpy(&st_size, sym + 16, 8);
                
                uint8_t binding = st_info >> 4;
                uint8_t type = st_info & 0xF;
                
                obj->add_symbol("symbol", st_value, st_size, binding, type);
            }
        }
    }

    /**
     * @brief Extract relocation records.
     */
    static void parse_relocations(Object_Format* obj, const std::vector<uint8_t>& buffer,
                                 uint64_t sh_offset, uint16_t sh_entsize, uint16_t sh_num) noexcept {
        // Find .rel / .rela sections (sh_type == 4 or 9)
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) break;
            
            const uint8_t* sh = buffer.data() + offset;
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, sh + 4, 4);
            
            // SHT_REL = 4, SHT_RELA = 9
            bool is_rela = (sh_type == 9);
            if (sh_type != 4 && sh_type != 9) continue;
            
            uint64_t rel_offset = 0, rel_size = 0, rel_entsize = 0;
            std::memcpy(&rel_offset, sh + 24, 8);
            std::memcpy(&rel_size, sh + 32, 8);
            std::memcpy(&rel_entsize, sh + 56, 8);
            
            if (rel_entsize == 0 || rel_size == 0) continue;
            
            uint64_t rel_count = rel_size / rel_entsize;
            for (uint64_t j = 0; j < rel_count; ++j) {
                uint64_t rel_entry_offset = rel_offset + (j * rel_entsize);
                if (rel_entry_offset + 16 > buffer.size()) break;
                
                const uint8_t* rel = buffer.data() + rel_entry_offset;
                
                uint64_t r_offset = 0;
                uint64_t r_info = 0;
                int64_t r_addend = 0;
                
                std::memcpy(&r_offset, rel + 0, 8);
                std::memcpy(&r_info, rel + 8, 8);
                
                if (is_rela && rel_entry_offset + 24 <= buffer.size()) {
                    std::memcpy(&r_addend, rel + 16, 8);
                }
                
                uint32_t rel_type = static_cast<uint32_t>(r_info & 0xFFFFFFFF);
                // sym_idx = static_cast<uint32_t>(r_info >> 32); // TODO: Use for symbol table lookup
                
                RelocationType reloc_type = RelocationType::Absolute;
                if (rel_type == 1) reloc_type = RelocationType::PCRelative;
                else if (rel_type == 8) reloc_type = RelocationType::Relative;
                
                obj->add_relocation(r_offset, "symbol", r_addend, reloc_type);
            }
        }
    }

    void parse_symbols([[maybe_unused]] Object_Format* obj, const std::vector<uint8_t>& buffer,
                       uint32_t sh_offset, uint16_t sh_entsize, uint16_t sh_num) noexcept {
        // Symbol table parsing (ELF .symtab)
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint32_t offset = sh_offset + (i * sh_entsize);
            if (offset + 32 > buffer.size()) break;
            
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, buffer.data() + offset + 4, 4);
            
            if (sh_type == 3) { // SHT_SYMTAB
                // Would parse actual symbols here in production
            }
        }
    }

    void parse_relocations([[maybe_unused]] Object_Format* obj, const std::vector<uint8_t>& buffer,
                           uint32_t sh_offset, uint16_t sh_entsize, uint16_t sh_num) noexcept {
        // Relocation table parsing (ELF .rel/.rela)
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint32_t offset = sh_offset + (i * sh_entsize);
            if (offset + 32 > buffer.size()) break;
            
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, buffer.data() + offset + 4, 4);
            
            if (sh_type == 4 || sh_type == 9) { // SHT_REL or SHT_RELA
                // Would parse actual relocations here in production
            }
        }
    }
};

} // namespace execution
} // namespace dgtl
