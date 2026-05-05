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
#include <cstring>
#include <optional>
#include <stdexcept>

namespace dgtl {
namespace execution {

/**
 * @enum BinaryFormatType
 * @brief Supported compiled binary formats.
 */
enum class BinaryFormatType : uint8_t {
    ELF64 = 0,      ///< ELF 64-bit (Linux/BSD)
    MachO64 = 1,    ///< Mach-O 64-bit (macOS)
    WASM = 2,       ///< WebAssembly
    PE64 = 3        ///< Portable Executable 64-bit (Windows)
};

/**
 * @struct Binary_Format
 * @brief Encapsulates binary object data with format detection and validation.
 */
struct Binary_Format {
    BinaryFormatType format_type = BinaryFormatType::ELF64;
    uint64_t entry_point = 0;  ///< Entry point address
    std::vector<uint8_t> data;  ///< Raw binary data

    /**
     * @brief Check binary validity based on format magic numbers.
     * @return true if binary header is valid for detected format
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (data.empty()) return false;
        
        switch (format_type) {
            case BinaryFormatType::ELF64:
                // ELF magic: 0x7f 'E' 'L' 'F'
                return data.size() >= 64 && 
                       data[0] == 0x7f && data[1] == 'E' && 
                       data[2] == 'L' && data[3] == 'F';
                       
            case BinaryFormatType::MachO64:
                // Mach-O magic: 0xfeedfacf (little-endian)
                return data.size() >= 32 && 
                       data[0] == 0xcf && data[1] == 0xfa &&
                       data[2] == 0xed && data[3] == 0xfe;
                       
            case BinaryFormatType::PE64:
                // PE magic: 'M' 'Z' at offset 0
                return data.size() >= 64 &&
                       data[0] == 'M' && data[1] == 'Z';
                       
            case BinaryFormatType::WASM:
                // WASM magic: 0x00 0x61 0x73 0x6d ("\0asm")
                return data.size() >= 8 &&
                       data[0] == 0x00 && data[1] == 0x61 &&
                       data[2] == 0x73 && data[3] == 0x6d;
                       
            default:
                return false;
        }
    }

    /**
     * @brief Get code pointer.
     */
    [[nodiscard]] const void* get_code_ptr() const noexcept { 
        return data.data(); 
    }
    
    [[nodiscard]] void* get_code_ptr() noexcept { 
        return data.data(); 
    }

    /**
     * @brief Get code size in bytes.
     */
    [[nodiscard]] size_t get_code_size() const noexcept { 
        return data.size(); 
    }

    /**
     * @brief Get entry point address.
     */
    [[nodiscard]] uint64_t get_entry_point() const noexcept {
        return entry_point;
    }
};

} // namespace execution
} // namespace dgtl
