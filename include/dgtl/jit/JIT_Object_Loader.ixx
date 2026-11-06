/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   JIT Object Loader (Loads ObjectFile into Executable W^X Memory)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

export module dgtl.jit.object_loader;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.linker.symbols;

export namespace dgtl::jit {

class JITLoadedModule {
public:
    JITLoadedModule() = default;
    JITLoadedModule(void* mem, std::size_t size, std::uintptr_t entry)
        : memory_(mem), size_(size), entry_addr_(entry) {}

    ~JITLoadedModule() {
        destroy();
    }

    JITLoadedModule(const JITLoadedModule&) = delete;
    JITLoadedModule& operator=(const JITLoadedModule&) = delete;

    JITLoadedModule(JITLoadedModule&& other) noexcept
        : memory_(other.memory_), size_(other.size_), entry_addr_(other.entry_addr_) {
        other.memory_ = nullptr;
        other.size_ = 0;
        other.entry_addr_ = 0;
    }

    JITLoadedModule& operator=(JITLoadedModule&& other) noexcept {
        if (this != &other) {
            destroy();
            memory_ = other.memory_;
            size_ = other.size_;
            entry_addr_ = other.entry_addr_;
            other.memory_ = nullptr;
            other.size_ = 0;
            other.entry_addr_ = 0;
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return memory_ != nullptr; }
    [[nodiscard]] void* memory() const noexcept { return memory_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uintptr_t entry_point() const noexcept { return entry_addr_; }

    void release() noexcept {
        memory_ = nullptr;
        size_ = 0;
        entry_addr_ = 0;
    }

private:
    void destroy() noexcept {
        if (memory_) {
#if defined(_WIN32) || defined(_WIN64)
            VirtualFree(memory_, 0, MEM_RELEASE);
#else
            munmap(memory_, size_);
#endif
            memory_ = nullptr;
            size_ = 0;
            entry_addr_ = 0;
        }
    }

    void* memory_ = nullptr;
    std::size_t size_ = 0;
    std::uintptr_t entry_addr_ = 0;
};

struct JITLoadResult {
    bool success = false;
    std::string error_message;
    JITLoadedModule module;
};

class JITObjectLoader {
public:
    static JITLoadResult load(
        const dgtl::object::ObjectFile& object,
        const std::unordered_map<std::string, std::uintptr_t>& host_symbols,
        std::string_view entry_name = "main") {

        JITLoadResult result{};

        // 1. Locate .text section
        const dgtl::object::Section* text_sec = object.find_section(".text");
        if (!text_sec) {
            text_sec = object.find_section("__text");
        }
        if (!text_sec || text_sec->empty()) {
            result.error_message = "No .text section found in ObjectFile";
            return result;
        }

        const std::size_t text_size = text_sec->size();

        // Locate .rdata / .rodata section
        const dgtl::object::Section* rdata_sec = object.find_section(".rdata");
        if (!rdata_sec) rdata_sec = object.find_section(".rodata");
        if (!rdata_sec) rdata_sec = object.find_section("__const");
        const std::size_t rdata_size = (rdata_sec && !rdata_sec->empty()) ? rdata_sec->size() : 0u;

        // Locate .data section
        const dgtl::object::Section* data_sec = object.find_section(".data");
        if (!data_sec) data_sec = object.find_section("__data");
        const std::size_t data_size = (data_sec && !data_sec->empty()) ? data_sec->size() : 0u;

        // Locate .bss section
        const dgtl::object::Section* bss_sec = object.find_section(".bss");
        if (!bss_sec) bss_sec = object.find_section("__bss");
        const std::size_t bss_size = bss_sec ? (bss_sec->bss_size > 0 ? bss_sec->bss_size : bss_sec->size()) : 0u;

        // 2. Identify GOT slots needed for external relocations (Internal .rdata, constants and literals never use GOT)
        std::unordered_map<std::string, std::size_t> got_slot_map;
        for (const auto& rel : object.relocations()) {
            const auto* sym = object.find_symbol(rel.symbol_name);
            const bool is_data_sym = rel.symbol_name.starts_with("__str_") ||
                                     rel.symbol_name.starts_with("__myzon_str_") ||
                                     rel.symbol_name.starts_with(".rdata") ||
                                     rel.symbol_name.starts_with(".rodata") ||
                                     rel.symbol_name.starts_with(".data") ||
                                     rel.symbol_name.starts_with(".bss");
            const bool is_external = !is_data_sym &&
                                     (rel.symbol_name.starts_with("__imp_") ||
                                      (sym == nullptr) || !sym->is_defined());
            if (is_external && got_slot_map.find(rel.symbol_name) == got_slot_map.end()) {
                const std::size_t slot_idx = got_slot_map.size();
                got_slot_map[rel.symbol_name] = slot_idx;
            }
        }

        // 3. Compute layout: .text + .rdata + .data + .bss + .plt + .got
        constexpr std::size_t kPltEntrySize = 16u;
        const std::size_t text_alloc = (text_size + 15u) & ~std::size_t(15u);
        const std::size_t rdata_alloc = (rdata_size + 15u) & ~std::size_t(15u);
        const std::size_t data_alloc = (data_size + 15u) & ~std::size_t(15u);
        const std::size_t bss_alloc = (bss_size + 15u) & ~std::size_t(15u);
        const std::size_t plt_alloc = (got_slot_map.size() * kPltEntrySize + 15u) & ~std::size_t(15u);
        const std::size_t got_alloc = (got_slot_map.size() * sizeof(std::uintptr_t) + 15u) & ~std::size_t(15u);
        const std::size_t total_payload = text_alloc + rdata_alloc + data_alloc + bss_alloc + plt_alloc + got_alloc;
        const std::size_t total_alloc = (total_payload + 4095u) & ~std::size_t(4095u);

        // 4. Allocate RW memory
#if defined(_WIN32) || defined(_WIN64)
        void* mem = VirtualAlloc(nullptr, total_alloc, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        void* mem = mmap(nullptr, total_alloc, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mem == MAP_FAILED) mem = nullptr;
#endif
        if (!mem) {
            result.error_message = "Failed to allocate memory for JIT code";
            return result;
        }

        std::uint8_t* text_base = static_cast<std::uint8_t*>(mem);
        std::memcpy(text_base, text_sec->bytes.data(), text_size);

        std::uint8_t* rdata_base = text_base + text_alloc;
        if (rdata_sec && rdata_size > 0) {
            std::memcpy(rdata_base, rdata_sec->bytes.data(), rdata_size);
        }

        std::uint8_t* data_base = rdata_base + rdata_alloc;
        if (data_sec && data_size > 0) {
            std::memcpy(data_base, data_sec->bytes.data(), data_size);
        }

        std::uint8_t* bss_base = data_base + data_alloc;
        if (bss_alloc > 0) {
            std::memset(bss_base, 0, bss_alloc);
        }

        std::uint8_t* plt_base = bss_base + bss_alloc;
        auto* got_base = reinterpret_cast<std::uintptr_t*>(plt_base + plt_alloc);

        // 5. Populate GOT & PLT Trampolines
        for (const auto& [sym_name, slot_idx] : got_slot_map) {
            std::uintptr_t addr = 0;
            auto it = host_symbols.find(sym_name);
            if (it != host_symbols.end()) {
                addr = it->second;
            } else if (sym_name.starts_with("__imp_")) {
                std::string stripped = sym_name.substr(6);
                auto it2 = host_symbols.find(stripped);
                if (it2 != host_symbols.end()) {
                    addr = it2->second;
                }
            } else {
                std::string imp = "__imp_" + sym_name;
                auto it3 = host_symbols.find(imp);
                if (it3 != host_symbols.end()) {
                    addr = it3->second;
                }
            }
            if (addr == 0) {
                uint64_t builtin_addr = 0;
                std::string_view lookup_name = sym_name;
                if (lookup_name.starts_with("__imp_")) {
                    lookup_name.remove_prefix(6);
                }
                if (dgtl::linker::SymbolResolver::resolve_builtin_runtime_symbol(lookup_name, builtin_addr)) {
                    addr = static_cast<std::uintptr_t>(builtin_addr);
                }
                if (addr == 0 && lookup_name.starts_with('_')) {
                    std::string_view un_name = lookup_name;
                    while (un_name.starts_with('_')) un_name.remove_prefix(1);
                    auto it_un = host_symbols.find(std::string(un_name));
                    if (it_un != host_symbols.end()) {
                        addr = it_un->second;
                    } else if (dgtl::linker::SymbolResolver::resolve_builtin_runtime_symbol(un_name, builtin_addr)) {
                        addr = static_cast<std::uintptr_t>(builtin_addr);
                    }
                }
            }
            if (addr == 0) {
                result.error_message = "DGTL JIT Linker Error: Unresolved external host symbol '" + sym_name + "' (no host shim registered and symbol not defined in module)";
#if defined(_WIN32) || defined(_WIN64)
                VirtualFree(mem, 0, MEM_RELEASE);
#else
                munmap(mem, total_alloc);
#endif
                return result;
            }

            got_base[slot_idx] = addr;

            // Synthesize PLT trampoline stub:
            // FF 25 <disp32> (jmp *[rip + disp32]) followed by NOP padding
            std::uint8_t* stub = plt_base + slot_idx * kPltEntrySize;
            std::uint8_t* got_slot_ptr = reinterpret_cast<std::uint8_t*>(&got_base[slot_idx]);
            const auto rip_after = reinterpret_cast<std::intptr_t>(stub + 6);
            const auto got_addr = reinterpret_cast<std::intptr_t>(got_slot_ptr);
            const auto disp = static_cast<std::int32_t>(got_addr - rip_after);

            stub[0] = 0xFF;
            stub[1] = 0x25;
            std::memcpy(stub + 2, &disp, sizeof(disp));
            std::memset(stub + 6, 0x90, kPltEntrySize - 6);
        }

        // 6. Apply relocations
        for (const auto& rel : object.relocations()) {
            if (rel.offset + 4 > text_size) {
                result.error_message = "Relocation offset out of bounds in .text";
#if defined(_WIN32) || defined(_WIN64)
                VirtualFree(mem, 0, MEM_RELEASE);
#else
                munmap(mem, total_alloc);
#endif
                return result;
            }

            std::uint8_t* patch_pos = text_base + rel.offset;
            const auto pc = reinterpret_cast<std::uint64_t>(patch_pos);

            std::uint64_t target_dest = 0;
            auto got_it = got_slot_map.find(rel.symbol_name);
            if (got_it != got_slot_map.end()) {
                // Point external call relocations to the executable PLT trampoline stub
                target_dest = reinterpret_cast<std::uint64_t>(plt_base + got_it->second * kPltEntrySize);
            } else if (const auto* sym = object.find_symbol(rel.symbol_name); sym && sym->is_defined()) {
                if (sym->section_index < object.sections().size()) {
                    const auto& sname = object.sections()[sym->section_index].name;
                    if (sname.starts_with(".rdata") || sname.starts_with(".rodata") || sname == "__const") {
                        target_dest = reinterpret_cast<std::uint64_t>(rdata_base + sym->value);
                    } else if (sname.starts_with(".data") || sname == "__data") {
                        target_dest = reinterpret_cast<std::uint64_t>(data_base + sym->value);
                    } else if (sname.starts_with(".bss") || sname == "__bss") {
                        target_dest = reinterpret_cast<std::uint64_t>(bss_base + sym->value);
                    } else {
                        target_dest = reinterpret_cast<std::uint64_t>(text_base + sym->value);
                    }
                } else if (rdata_sec && (rel.symbol_name.starts_with("__str_") || rel.symbol_name.starts_with("__myzon_str_") || rel.symbol_name.starts_with(".rdata") || rel.symbol_name.starts_with(".rodata") || rel.symbol_name.starts_with("__const"))) {
                    target_dest = reinterpret_cast<std::uint64_t>(rdata_base + sym->value);
                } else if (data_sec && (rel.symbol_name.starts_with(".data") || rel.symbol_name.starts_with("__data"))) {
                    target_dest = reinterpret_cast<std::uint64_t>(data_base + sym->value);
                } else if (bss_sec && (rel.symbol_name.starts_with(".bss") || rel.symbol_name.starts_with("__bss"))) {
                    target_dest = reinterpret_cast<std::uint64_t>(bss_base + sym->value);
                } else {
                    target_dest = reinterpret_cast<std::uint64_t>(text_base + sym->value);
                }
            } else if (rdata_sec && (rel.symbol_name.starts_with("__str_") || rel.symbol_name.starts_with("__myzon_str_") || rel.symbol_name.starts_with(".rdata") || rel.symbol_name.starts_with(".rodata") || rel.symbol_name.starts_with("__const"))) {
                target_dest = reinterpret_cast<std::uint64_t>(rdata_base);
            } else if (data_sec && (rel.symbol_name.starts_with(".data") || rel.symbol_name.starts_with("__data"))) {
                target_dest = reinterpret_cast<std::uint64_t>(data_base);
            } else if (bss_sec && (rel.symbol_name.starts_with(".bss") || rel.symbol_name.starts_with("__bss"))) {
                target_dest = reinterpret_cast<std::uint64_t>(bss_base);
            }

            if (rel.kind == dgtl::object::RelocationKind::PCRel32 ||
                rel.kind == dgtl::object::RelocationKind::PLT32) {
                if (!dgtl::object::RelocationApplier::apply_pc32(patch_pos, pc, target_dest, rel.addend)) {
                    result.error_message = "Relocation overflow for symbol: " + rel.symbol_name;
#if defined(_WIN32) || defined(_WIN64)
                    VirtualFree(mem, 0, MEM_RELEASE);
#else
                    munmap(mem, total_alloc);
#endif
                    return result;
                }
            } else if (rel.kind == dgtl::object::RelocationKind::Absolute64) {
                if (rel.offset + 8 > text_size) {
                    result.error_message = "Relocation offset out of bounds in .text";
#if defined(_WIN32) || defined(_WIN64)
                    VirtualFree(mem, 0, MEM_RELEASE);
#else
                    munmap(mem, total_alloc);
#endif
                    return result;
                }
                const uint64_t val = target_dest + rel.addend;
                for (int i = 0; i < 8; ++i) patch_pos[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
            }
        }

        // 7. Enforce W^X: Switch from PAGE_READWRITE to PAGE_EXECUTE_READ (or RXW if mutable data present)
#if defined(_WIN32) || defined(_WIN64)
        DWORD old_protect;
        const DWORD target_prot = (data_size > 0 || bss_size > 0) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        if (!VirtualProtect(mem, total_alloc, target_prot, &old_protect)) {
            VirtualFree(mem, 0, MEM_RELEASE);
            result.error_message = "VirtualProtect failed to set executable memory protection";
            return result;
        }
        FlushInstructionCache(GetCurrentProcess(), mem, total_alloc);
#else
        const int target_prot = (data_size > 0 || bss_size > 0) ? (PROT_READ | PROT_WRITE | PROT_EXEC) : (PROT_READ | PROT_EXEC);
        if (mprotect(mem, total_alloc, target_prot) != 0) {
            munmap(mem, total_alloc);
            result.error_message = "mprotect failed to set executable memory protection";
            return result;
        }
#endif

        // 8. Resolve Entry Point
        std::uintptr_t entry_addr = reinterpret_cast<std::uintptr_t>(text_base);
        if (const auto* sym = object.find_symbol(entry_name); sym && sym->is_defined()) {
            entry_addr = reinterpret_cast<std::uintptr_t>(text_base + sym->value);
        } else if (const auto* sym_main = object.find_symbol("main"); sym_main && sym_main->is_defined()) {
            entry_addr = reinterpret_cast<std::uintptr_t>(text_base + sym_main->value);
        } else if (const auto* sym_kernel = object.find_symbol("DGTL_kernel_entry"); sym_kernel && sym_kernel->is_defined()) {
            entry_addr = reinterpret_cast<std::uintptr_t>(text_base + sym_kernel->value);
        }

        result.success = true;
        result.module = JITLoadedModule(mem, total_alloc, entry_addr);
        return result;
    }
};

} // namespace dgtl::jit
