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

/*
 * ============================================================================
 * DGTL EXECUTION UNIFIED MODULE
 * ============================================================================
 *
 * Consolidates functionality from:
 *   - execution/cache/Code_Cache.hpp
 *   - execution/dispatcher/Execution_Dispatcher.hpp
 *   - execution/format/Binary_Loader_object.hpp
 *
 * Design Goals:
 *   1. Keep a single include surface for execution, dispatch, and binary loading
 *   2. Preserve source compatibility through wrapper headers
 *   3. Maintain high-concurrency execution behavior and zero-allocation lookups
 *   4. Keep format/object loader logic header-only for simplified integration
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dgtl/runtime/Runtime_Intrinsics.hpp"

namespace dgtl {
namespace execution {

// ============================================================================
// SECTION 1: CODE CACHE
// ============================================================================

// --- C++20 Transparent Hashing for Zero-Allocation Cache Lookups ---
struct CacheStringViewHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct CacheStringViewEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

/**
 * @class Code_Cache
 * @brief Thread-safe JIT code cache optimized for high-concurrency read access.
 */
class Code_Cache {
public:
    Code_Cache(size_t max_size = 16 * 1024 * 1024) noexcept
        : max_size_(max_size), current_size_(0) {}
    ~Code_Cache() = default;

    Code_Cache(const Code_Cache&) = delete;
    Code_Cache& operator=(const Code_Cache&) = delete;

    /**
     * @brief Store compiled machine code into the cache.
        * Removed `noexcept` as allocations and locks can throw.
     * @return true if successfully cached, false if capacity exceeded or invalid.
     */
    [[nodiscard]] bool store(std::string_view key, const void* code, size_t size) {
        if (key.empty() || !code || size == 0) return false;
        if (size > max_size_) return false;

        // Exclusive write lock: Blocks other threads ONLY when modifying the cache
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto existing = cache_.find(key);
        if (existing != cache_.end()) {
            current_size_ -= existing->second.second;
        }

        if (size > max_size_ - current_size_) {
            // Rollback size math if we don't have enough room
            if (existing != cache_.end()) {
                current_size_ += existing->second.second;
            }
            return false;
        }

        auto buf = std::make_unique<uint8_t[]>(size);
        std::memcpy(buf.get(), code, size);

        if (existing != cache_.end()) {
            existing->second = {std::move(buf), size};
        } else {
            cache_.emplace(std::string(key), std::make_pair(std::move(buf), size));
        }

        current_size_ += size;
        return true;
    }

    /**
     * @brief Retrieve code from cache using zero-allocation lookup.
     */
    [[nodiscard]] const void* retrieve(std::string_view key) const {
        if (key.empty()) return nullptr;

        // Shared read lock: Allows infinite concurrent reads across cores
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.first.get() : nullptr;
    }

    /**
     * @brief Check if code exists in cache using zero-allocation lookup.
     */
    [[nodiscard]] bool exists(std::string_view key) const {
        if (key.empty()) return false;

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.find(key) != cache_.end();
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
        current_size_ = 0;
    }

    [[nodiscard]] size_t get_usage() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return current_size_;
    }

    [[nodiscard]] bool is_full() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return current_size_ >= max_size_;
    }

private:
    // HFT Optimization: alignas(64) prevents false sharing on the R/W lock
    alignas(64) mutable std::shared_mutex mutex_;
    // Upgraded map using transparent hashing to allow string_view queries
    std::unordered_map<std::string, std::pair<std::unique_ptr<uint8_t[]>, size_t>,
                       CacheStringViewHash, CacheStringViewEqual> cache_;
    size_t max_size_;
    size_t current_size_;
};

// ============================================================================
// SECTION 2: EXECUTION DISPATCHER
// ============================================================================

enum class DispatchTarget : uint8_t {
    X64_Native = 0,
    ARM64_Native = 1,
    WASM_Interpreter = 2,
    WASM_JIT = 3,
    Count = 4
};

enum class DispatchError : int64_t {
    Success = 0,
    NullContext = -1,
    NoExecutor = -2,
    ExecutionFailed = -3,
    InvalidTarget = -4
};

/**
 * @class Execution_Dispatcher
 * @brief Manages routing of compiled payloads to target-specific execution engines.
 */
class Execution_Dispatcher {
public:
    Execution_Dispatcher() = default;
    ~Execution_Dispatcher() = default;

    Execution_Dispatcher(const Execution_Dispatcher&) = delete;
    Execution_Dispatcher& operator=(const Execution_Dispatcher&) = delete;

    using ExecutionFunc = std::function<int64_t(void*)>;

    /**
     * @brief Register an executor for a specific target.
     * Removed `noexcept` as std::function may allocate and locks can throw.
     * @param target Target ISA to bind
     * @param func The execution function/closure
     */
    void set_executor(DispatchTarget target, ExecutionFunc func) {
        // Exclusive write lock: safely blocks concurrent dispatches while updating
        std::unique_lock<std::shared_mutex> lock(mutex_);
        const uint8_t idx = static_cast<uint8_t>(target);
        if (idx < static_cast<uint8_t>(DispatchTarget::Count)) {
            executors_[idx] = std::move(func);
        }
    }

    /**
        * @brief Dispatch execution to registered executor.
     * @param target Target ISA/executor to use
     * @param context Opaque execution context
        * @return Result code (negative = error, >= 0 = success)
     */
    [[nodiscard]] int64_t dispatch(DispatchTarget target, void* context) const noexcept {
        if (!context) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::NullContext);
        }

        const uint8_t idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::InvalidTarget);
        }

        // HFT Upgrade: Shared lock allows fully concurrent, lock-free-like dispatching
        // across multiple parallel executions. Threads will NOT block each other here.
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (!executors_[idx]) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::NoExecutor);
        }

        try {
            return executors_[idx](context);
        } catch (...) {
            // Safely catches any internal executor exceptions to maintain C-ABI boundaries
            return static_cast<int64_t>(DispatchError::ExecutionFailed);
        }
    }

    /**
        * @brief Check if executor is registered for target.
     */
    [[nodiscard]] bool has_executor(DispatchTarget target) const noexcept {
        const uint8_t idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) return false;

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return static_cast<bool>(executors_[idx]);
    }

private:
    // HFT Optimization: alignas(64) pads these members to fit perfectly into L1 cache lines,
    // preventing "false sharing" cache invalidations across CPU cores during concurrent JIT execution.
    alignas(64) mutable std::shared_mutex mutex_;
    alignas(64) std::array<ExecutionFunc, static_cast<size_t>(DispatchTarget::Count)> executors_{};
};

// ============================================================================
// SECTION 3: BINARY FORMAT TYPES & STRUCTURES
// ============================================================================

enum class BinaryFormatType : uint8_t {
    ELF64 = 0,        ///< ELF 64-bit (Linux/BSD)
    MachO64 = 1,      ///< Mach-O 64-bit (macOS)
    WASM = 2,         ///< WebAssembly
    PE64 = 3,         ///< Portable Executable 64-bit (Windows)
    MYZN_PAYLOAD = 4  ///< Native MYZON Frontend IR + String Pool Payload
};

struct Binary_Format {
    /**
     * @brief Encapsulates executable payload bytes and associated format metadata.
     */
    BinaryFormatType format_type = BinaryFormatType::ELF64;
    uint64_t entry_point = 0;
    std::vector<uint8_t> data;

    /**
     * @brief Validate by format-specific magic numbers.
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (data.empty()) return false;

        switch (format_type) {
            case BinaryFormatType::ELF64:
                return data.size() >= 64
                    && data[0] == 0x7f && data[1] == 'E'
                    && data[2] == 'L' && data[3] == 'F';
            case BinaryFormatType::MachO64:
                return data.size() >= 32
                    && data[0] == 0xcf && data[1] == 0xfa
                    && data[2] == 0xed && data[3] == 0xfe;
            case BinaryFormatType::PE64:
                return data.size() >= 64
                    && data[0] == 'M' && data[1] == 'Z';
            case BinaryFormatType::WASM:
                return data.size() >= 8
                    && data[0] == 0x00 && data[1] == 0x61
                    && data[2] == 0x73 && data[3] == 0x6d;
            case BinaryFormatType::MYZN_PAYLOAD:
                if (data.size() < 8) return false;
                {
                    uint32_t magic = 0;
                    std::memcpy(&magic, data.data(), sizeof(uint32_t));
                    return magic == 0x4D595A4E;
                }
            default:
                return false;
        }
    }

    /** @brief Get code pointer (const). */
    [[nodiscard]] const void* get_code_ptr() const noexcept { return data.data(); }
    /** @brief Get code pointer (mutable). */
    [[nodiscard]] void* get_code_ptr() noexcept { return data.data(); }
    /** @brief Get code size in bytes. */
    [[nodiscard]] size_t get_code_size() const noexcept { return data.size(); }
    /** @brief Get entry point address. */
    [[nodiscard]] uint64_t get_entry_point() const noexcept { return entry_point; }
};

// ============================================================================
// SECTION 4: OBJECT FORMAT TYPES & STRUCTURES
// ============================================================================

enum class RelocationType : uint8_t {
    Absolute = 0,
    PCRelative = 1,
    Relative = 2,
    GOT = 3,
    PLT = 4
};

struct Symbol {
    std::string name;
    uint64_t address = 0;       ///< Symbol value (address)
    uint64_t size = 0;          ///< Symbol size in bytes
    uint8_t binding = 0;        ///< STB_LOCAL, STB_GLOBAL, STB_WEAK
    uint8_t type = 0;           ///< STT_NOTYPE, STT_OBJECT, STT_FUNC, etc.
    uint16_t section_idx = 0;   ///< Section index (SHN_*)
    bool is_extern = false;     ///< External symbol reference
};

struct Relocation {
    uint64_t offset = 0;        ///< Offset within section
    std::string symbol_name;
    int64_t addend = 0;         ///< Addend for RELA-type relocations
    RelocationType type = RelocationType::Absolute;
};

struct Section {
    std::vector<uint8_t> data;
    uint64_t base_address = 0;
    uint64_t alignment = 1;
    uint32_t flags = 0;         ///< SHF_* flags (alloc, write, exec)
};

struct StringViewHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct StringViewEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

// ============================================================================
// SECTION 5: OBJECT FORMAT CLASS
// ============================================================================

class Object_Format {
public:
    Object_Format() = default;
    ~Object_Format() = default;

    Object_Format(const Object_Format&) = delete;
    Object_Format& operator=(const Object_Format&) = delete;

    /**
     * @brief Find symbol address by name (O(1), heterogeneous lookup).
     */
    [[nodiscard]] std::optional<uint64_t> find_symbol_address(std::string_view name) const noexcept {
        auto it = symbol_map_.find(name);
        if (it != symbol_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /** @brief Add a symbol to the symbol table. */
    void add_symbol(std::string_view name, uint64_t addr, uint64_t size,
                    uint8_t binding = 0, uint8_t type = 0) {
        std::string str_name{name};
        symbols.push_back({str_name, addr, size, binding, type, 0, false});
        symbol_map_.try_emplace(std::move(str_name), addr);
    }

    /** @brief Add an external symbol reference. */
    void add_extern_symbol(std::string_view name, uint8_t binding = 1) {
        symbols.push_back({std::string(name), 0, 0, binding, 0, 0, true});
    }

    /** @brief Add a relocation record. */
    void add_relocation(uint64_t offset, std::string_view symbol,
                        int64_t addend = 0, RelocationType type = RelocationType::Absolute) {
        relocations.push_back({offset, std::string(symbol), addend, type});
    }

    /** @brief Get mutable text section (.text). */
    [[nodiscard]] Section& get_text_section() noexcept { return text_section; }
    /** @brief Get mutable data section (.data). */
    [[nodiscard]] Section& get_data_section() noexcept { return data_section; }
    /** @brief Get mutable read-only data section (.rodata). */
    [[nodiscard]] Section& get_rodata_section() noexcept { return rodata_section; }
    /** @brief Get mutable BSS section (.bss). */
    [[nodiscard]] Section& get_bss_section() noexcept { return bss_section; }

    /** @brief Get const text section. */
    [[nodiscard]] const Section& get_text_section() const noexcept { return text_section; }
    /** @brief Get const data section. */
    [[nodiscard]] const Section& get_data_section() const noexcept { return data_section; }
    /** @brief Get const rodata section. */
    [[nodiscard]] const Section& get_rodata_section() const noexcept { return rodata_section; }
    /** @brief Get const BSS section. */
    [[nodiscard]] const Section& get_bss_section() const noexcept { return bss_section; }

    /** @brief Get all symbols. */
    [[nodiscard]] const std::vector<Symbol>& get_symbols() const noexcept { return symbols; }
    /** @brief Get all relocations. */
    [[nodiscard]] const std::vector<Relocation>& get_relocations() const noexcept { return relocations; }
    /** @brief Get symbol count. */
    [[nodiscard]] size_t symbol_count() const noexcept { return symbols.size(); }
    /** @brief Get relocation count. */
    [[nodiscard]] size_t relocation_count() const noexcept { return relocations.size(); }

private:
    Section text_section;
    Section data_section;
    Section rodata_section;
    Section bss_section;

    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    // Transparent hashing map for O(1) symbol lookup
    std::unordered_map<std::string, uint64_t, StringViewHash, StringViewEqual> symbol_map_;
};

// ============================================================================
// SECTION 6: BINARY LOADER CLASS
// ============================================================================

class Binary_Loader {
public:
    Binary_Loader() = default;
    ~Binary_Loader() = default;

    Binary_Loader(const Binary_Loader&) = delete;
    Binary_Loader& operator=(const Binary_Loader&) = delete;

    /**
     * @brief Load binary from file path and parse header/entry point.
     */
    [[nodiscard]] std::unique_ptr<Binary_Format> load(const std::string& filepath) {
        auto binary = std::make_unique<Binary_Format>();

        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open binary file: " + filepath);

        std::streamsize size = file.tellg();
        if (size < 64) throw std::runtime_error("Binary file too small");

        file.seekg(0, std::ios::beg);
        binary->data.resize(static_cast<size_t>(size));

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
     * @brief Load MYZON frontend payload and register string table entries.
     */
    [[nodiscard]] inline bool load_frontend_payload(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        if (size < 8) return false;

        file.seekg(0, std::ios::beg);
        payload_buffer_.resize(static_cast<size_t>(size));
        if (!file.read(payload_buffer_.data(), size)) return false;

        const char* ptr = payload_buffer_.data();
        const char* end = ptr + size;

        uint32_t magic = read_u32(ptr);
        ptr += sizeof(uint32_t);
        if (magic != 0x4D595A4E) return false;

        uint32_t string_count = read_u32(ptr);
        ptr += sizeof(uint32_t);

        for (uint32_t i = 0; i < string_count; ++i) {
            if (ptr + (sizeof(uint32_t) * 2) > end) break;

            uint32_t id = read_u32(ptr);
            ptr += sizeof(uint32_t);

            uint32_t len = read_u32(ptr);
            ptr += sizeof(uint32_t);

            if (ptr + len > end) break;

            // NOTE: MYZON runtime registration removed for DGTL boundary enforcement
            // dgtl::runtime::register_runtime_string(id, ptr, len);
            ptr += len;
        }

        return true;
    }

    /**
     * @brief Load binary from memory and parse header/entry point.
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
    std::vector<char> payload_buffer_;

    [[nodiscard]] static uint32_t read_u32(const void* ptr) noexcept {
        uint32_t v = 0;
        std::memcpy(&v, ptr, sizeof(uint32_t));
        return v;
    }

    [[nodiscard]] static uint64_t read_u64(const void* ptr) noexcept {
        uint64_t v = 0;
        std::memcpy(&v, ptr, sizeof(uint64_t));
        return v;
    }

    /**
     * @brief Detect binary format from magic number.
     */
    static void detect_format(Binary_Format* fmt) noexcept {
        if (!fmt || fmt->data.size() < 4) {
            if (fmt) fmt->format_type = BinaryFormatType::ELF64;
            return;
        }

        const uint8_t* bytes = fmt->data.data();

        if (bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
            fmt->format_type = BinaryFormatType::ELF64;
        } else if (bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe) {
            fmt->format_type = BinaryFormatType::MachO64;
        } else if (bytes[0] == 'M' && bytes[1] == 'Z') {
            fmt->format_type = BinaryFormatType::PE64;
        } else if (bytes[0] == 0x00 && bytes[1] == 0x61 && bytes[2] == 0x73 && bytes[3] == 0x6d) {
            fmt->format_type = BinaryFormatType::WASM;
        } else if (bytes[0] == 'M' && bytes[1] == 'Y' && bytes[2] == 'Z' && bytes[3] == 'N') {
            fmt->format_type = BinaryFormatType::MYZN_PAYLOAD;
        } else {
            fmt->format_type = BinaryFormatType::ELF64;
        }
    }

    /**
     * @brief Parse entry point from binary headers.
     */
    static void parse_entry_point(Binary_Format* fmt) noexcept {
        if (!fmt || fmt->data.size() < 64) {
            if (fmt) fmt->entry_point = 0;
            return;
        }

        const uint8_t* bytes = fmt->data.data();

        switch (fmt->format_type) {
            case BinaryFormatType::ELF64:
                if (fmt->data.size() >= 32) {
                    fmt->entry_point = read_u64(&bytes[24]);
                }
                break;
            case BinaryFormatType::MachO64:
                if (fmt->data.size() >= 40) {
                    fmt->entry_point = read_u64(&bytes[32]);
                }
                break;
            case BinaryFormatType::PE64:
                if (fmt->data.size() >= 44) {
                    fmt->entry_point = read_u32(&bytes[40]);
                }
                break;
            case BinaryFormatType::WASM:
                fmt->entry_point = 0;
                break;
            default:
                fmt->entry_point = 0;
                break;
        }
    }
};

// ============================================================================
// SECTION 7: OBJECT LOADER CLASS
// ============================================================================

class Object_Loader {
public:
    Object_Loader() = default;
    ~Object_Loader() = default;

    Object_Loader(const Object_Loader&) = delete;
    Object_Loader& operator=(const Object_Loader&) = delete;

    /**
     * @brief Load ELF object file from path.
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
     * @brief Load ELF object file from memory.
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
        return buffer.size() >= 4
            && buffer[0] == 0x7f && buffer[1] == 'E'
            && buffer[2] == 'L' && buffer[3] == 'F';
    }

    /**
     * @brief Parse complete ELF object file.
     */
    static void parse_elf_object(Object_Format* obj, const std::vector<uint8_t>& buffer) {
        if (buffer.size() < 64) throw std::runtime_error("ELF header too small");

        uint64_t e_shoff = 0;
        uint16_t e_shnum = 0;
        uint16_t e_shentsize = 0;
        uint16_t e_shstrndx = 0;

        std::memcpy(&e_shoff, buffer.data() + 32, 8);
        std::memcpy(&e_shentsize, buffer.data() + 46, 2);
        std::memcpy(&e_shnum, buffer.data() + 48, 2);
        std::memcpy(&e_shstrndx, buffer.data() + 50, 2);

        (void)e_shstrndx;

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

            const uint8_t* sh = buffer.data() + offset;

            uint32_t sh_type = 0;
            uint64_t sh_data_offset = 0;
            uint64_t sh_size = 0;
            uint64_t sh_addr = 0;

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
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) break;

            const uint8_t* sh = buffer.data() + offset;
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, sh + 4, 4);

            if (sh_type != 3) continue; // SHT_SYMTAB

            uint64_t sym_offset = 0;
            uint64_t sym_size = 0;
            uint64_t sym_entsize = 0;
            std::memcpy(&sym_offset, sh + 24, 8);
            std::memcpy(&sym_size, sh + 32, 8);
            std::memcpy(&sym_entsize, sh + 56, 8);

            if (sym_entsize == 0 || sym_size == 0) continue;

            uint64_t sym_count = sym_size / sym_entsize;
            for (uint64_t j = 0; j < sym_count; ++j) {
                uint64_t sym_entry_offset = sym_offset + (j * sym_entsize);
                if (sym_entry_offset + 24 > buffer.size()) break;

                const uint8_t* sym = buffer.data() + sym_entry_offset;

                uint32_t st_name = 0;
                uint8_t st_info = 0;
                uint16_t st_shndx = 0;
                uint64_t st_value = 0;
                uint64_t st_size = 0;

                std::memcpy(&st_name, sym + 0, 4);
                std::memcpy(&st_info, sym + 4, 1);
                std::memcpy(&st_shndx, sym + 6, 2);
                std::memcpy(&st_value, sym + 8, 8);
                std::memcpy(&st_size, sym + 16, 8);

                (void)st_name;
                (void)st_shndx;

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
        for (uint16_t i = 0; i < sh_num; ++i) {
            uint64_t offset = sh_offset + (static_cast<uint64_t>(i) * sh_entsize);
            if (offset + 64 > buffer.size()) break;

            const uint8_t* sh = buffer.data() + offset;
            uint32_t sh_type = 0;
            std::memcpy(&sh_type, sh + 4, 4);

            // SHT_REL = 4, SHT_RELA = 9
            const bool is_rela = (sh_type == 9);
            if (sh_type != 4 && sh_type != 9) continue;

            uint64_t rel_offset = 0;
            uint64_t rel_size = 0;
            uint64_t rel_entsize = 0;
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

                RelocationType reloc_type = RelocationType::Absolute;
                if (rel_type == 1) reloc_type = RelocationType::PCRelative;
                else if (rel_type == 8) reloc_type = RelocationType::Relative;

                obj->add_relocation(r_offset, "symbol", r_addend, reloc_type);
            }
        }
    }
};

} // namespace execution
} // namespace dgtl
