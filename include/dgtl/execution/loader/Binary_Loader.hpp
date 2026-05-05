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

#include "dgtl/execution/format/Binary_Format.hpp"
#include <string>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace dgtl {
namespace execution {

/**
 * @class Binary_Loader
 * @brief Loads and parses binary executable files in various formats.
 * 
 * Supports ELF64, Mach-O64, PE64, and WebAssembly formats with
 * automatic format detection and entry point parsing.
 */
class Binary_Loader {
public:
    Binary_Loader() = default;
    ~Binary_Loader() = default;

    Binary_Loader(const Binary_Loader&) = delete;
    Binary_Loader& operator=(const Binary_Loader&) = delete;

    /**
     * @brief Load binary from file.
     * @param filepath Path to binary file
     * @return Unique pointer to parsed Binary_Format
     * @throws std::runtime_error on load/parse failure
     */
    [[nodiscard]] std::unique_ptr<Binary_Format> load(const std::string& filepath) {
        auto binary = std::make_unique<Binary_Format>();
        
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open binary file: " + filepath);
        
        std::streamsize size = file.tellg();
        if (size < 64) throw std::runtime_error("Binary file too small");
        
        file.seekg(0, std::ios::beg);
        binary->data.resize(size);
        
        if (!file.read(reinterpret_cast<char*>(binary->data.data()), size)) {
            throw std::runtime_error("Failed to read binary file");
        }
        
        detect_format(binary.get());
        if (!binary->is_valid()) {
            throw std::runtime_error("Invalid binary format");
        }
        
        parse_entry_point(binary.get());
        return binary;
    }

    /**
     * @brief Load binary from memory.
     * @param data Pointer to binary data
     * @param size Size in bytes
     * @return Unique pointer to parsed Binary_Format
     * @throws std::runtime_error on parse failure
     */
    [[nodiscard]] std::unique_ptr<Binary_Format> load_from_memory(const void* data, size_t size) {
        if (!data || size < 64) throw std::runtime_error("Invalid binary data");
        
        auto binary = std::make_unique<Binary_Format>();
        binary->data.assign(static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + size);
        
        detect_format(binary.get());
        if (!binary->is_valid()) {
            throw std::runtime_error("Invalid binary format");
        }
        
        parse_entry_point(binary.get());
        return binary;
    }

private:
    /**
     * @brief Detect binary format from magic number.
     */
    static void detect_format(Binary_Format* fmt) noexcept {
        if (!fmt || fmt->data.size() < 4) {
            fmt->format_type = BinaryFormatType::ELF64;
            return;
        }
        
        const uint8_t* bytes = fmt->data.data();
        
        // ELF magic: 0x7f 'E' 'L' 'F'
        if (bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
            fmt->format_type = BinaryFormatType::ELF64;
        }
        // Mach-O magic: 0xfeedfacf (little-endian)
        else if (bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe) {
            fmt->format_type = BinaryFormatType::MachO64;
        }
        // PE magic: 'M' 'Z'
        else if (bytes[0] == 'M' && bytes[1] == 'Z') {
            fmt->format_type = BinaryFormatType::PE64;
        }
        // WASM magic: 0x00 0x61 0x73 0x6d
        else if (bytes[0] == 0x00 && bytes[1] == 0x61 && bytes[2] == 0x73 && bytes[3] == 0x6d) {
            fmt->format_type = BinaryFormatType::WASM;
        }
        else {
            fmt->format_type = BinaryFormatType::ELF64; // default
        }
    }

    /**
     * @brief Parse entry point from binary headers.
     */
    static void parse_entry_point(Binary_Format* fmt) noexcept {
        if (!fmt || fmt->data.size() < 64) {
            fmt->entry_point = 0;
            return;
        }
        
        const uint8_t* bytes = fmt->data.data();
        
        switch (fmt->format_type) {
            case BinaryFormatType::ELF64:
                // ELF64 entry point at offset 24 (8 bytes)
                if (fmt->data.size() >= 32) {
                    fmt->entry_point = *reinterpret_cast<const uint64_t*>(&bytes[24]);
                }
                break;
                
            case BinaryFormatType::MachO64:
                // Mach-O entry point at offset 32 (8 bytes)
                if (fmt->data.size() >= 40) {
                    fmt->entry_point = *reinterpret_cast<const uint64_t*>(&bytes[32]);
                }
                break;
                
            case BinaryFormatType::PE64:
                // PE entry point at offset 40 (4 bytes)
                if (fmt->data.size() >= 44) {
                    fmt->entry_point = *reinterpret_cast<const uint32_t*>(&bytes[40]);
                }
                break;
                
            case BinaryFormatType::WASM:
                // WASM entry point is typically 0 (module/function based)
                fmt->entry_point = 0;
                break;
                
            default:
                fmt->entry_point = 0;
                break;
        }
    }
};

} // namespace execution
} // namespace dgtl
