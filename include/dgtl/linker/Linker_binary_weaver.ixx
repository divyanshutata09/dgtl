/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 15: DIRECT-TO-BINARY WEAVER (ELF64 / MACH-O / WASM / COFF PE-OBJ / STATIC LIB)
 * FULL SYNCHRONIZATION: Aligned with 178 IR OpCodes, ISel DSL Bootstrap Rules & Precise mmap alignment.
 * PERFORMANCE: Zero-copy executable generation via memory-mapped I/O.
 * ARCHITECTURE: Staff-Level Upgraded with AVX-512 / FMA32 / Vector512 / AI / Spatial Math Native Emission.
 */

module;

#include <cstdlib>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include <string_view>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream> 
#include <limits>
#include <unordered_map>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <stdlib.h> // For _byteswap_ulong
    #undef CONST    // Prevents Windows.h CONST macro from corrupting dgtl::linker::mir::MachineLinkOpcode::CONST
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <elf.h>
#endif

export module dgtl.linker.backend;

import dgtl.object.file;
import dgtl.mir.machine_ir;
import dgtl.mir.instruction_selector;
import dgtl.mir.isel_gen;
import dgtl.isa.x64.isel_gen;
import dgtl.isa.isel_dispatch;
import dgtl.isa.wasm.wasm_machine_form_lowering;
import dgtl.isa.wasm.wasm_abi;
import dgtl.isa.wasm.wasm_instruction_info;
import dgtl.isa.wasm.reg_map;
import dgtl.execution.loader;
import dgtl.target;
import dgtl.execution.cache;
import dgtl.execution.dispatcher;
import dgtl.isa.wasm.encoder;
import dgtl.mir.runtime_contract;
import dgtl.linker.driver;
import dgtl.linker.elf.sections;
import dgtl.linker.pe.sections;
import dgtl.object.readers.elf;

namespace runtime_contract = dgtl::mir::runtime_contract;

using dgtl::mir::MachineForm;
using dgtl::mir::MachineSemanticKind;

namespace generated = dgtl::isa::x64::generated;

#if defined(_WIN32) || defined(_WIN64)
    #define DGTL_MAP_FAILED nullptr
#else
    #define DGTL_MAP_FAILED MAP_FAILED
#endif

#ifndef ELF64_ST_INFO
#define ELF64_ST_INFO(bind, type) (dgtl::linker::elf::elf64_st_info((bind), (type)))
#endif
#ifndef ELF64_R_INFO
#define ELF64_R_INFO(sym, type) (dgtl::linker::elf::elf64_r_info((sym), (type)))
#endif

using namespace dgtl::linker::elf;
using namespace dgtl::linker::pe;

namespace elf {
    using namespace dgtl::linker::elf;
}

namespace coff {
    using namespace dgtl::linker::pe;
}



export namespace dgtl::linker {

namespace generated = dgtl::isa::x64::generated;

/**
 * @class DGTLLinker
 * @brief Weaves IR directly into executable binary formats without .obj overhead.
 */
class DGTLLinker {
protected:
    intptr_t fd_handle = -1; 
    uint8_t* output_ptr = nullptr;
    size_t file_size = 0;
    std::vector<std::string> fallback_diagnostics_;

    inline void clear_fallback_diagnostics() noexcept {
        fallback_diagnostics_.clear();
    }

    inline void record_fallback_opcode(size_t node_index, uint16_t opcode) {
        std::string msg;
        msg.reserve(48);
        msg.append("node#").append(std::to_string(node_index))
           .append(" opcode=#").append(std::to_string(opcode));
        fallback_diagnostics_.push_back(std::move(msg));
    }

    void reset_mapping_state() noexcept {
        output_ptr = nullptr;
        fd_handle = -1;
        file_size = 0;
    }

    void release_mapping() noexcept {
        if (!output_ptr || output_ptr == DGTL_MAP_FAILED) {
            output_ptr = nullptr;
            if (fd_handle != -1) {
#if defined(_WIN32)
                CloseHandle(reinterpret_cast<HANDLE>(fd_handle));
#else
                close(static_cast<int>(fd_handle));
#endif
                fd_handle = -1;
            }
            return;
        }

#if defined(_WIN32)
        UnmapViewOfFile(output_ptr);
        if (fd_handle != -1) CloseHandle(reinterpret_cast<HANDLE>(fd_handle));
#else
        munmap(output_ptr, file_size);
        if (fd_handle != -1) close(static_cast<int>(fd_handle));
#endif

        output_ptr = nullptr;
        fd_handle = -1;
        file_size = 0;
    }

    static inline void write_leb128(std::vector<uint8_t>& buf, uint64_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            buf.push_back(byte);
        } while (value != 0);
    }

    [[nodiscard]] static constexpr size_t align_up(size_t value, size_t alignment) noexcept {
        if (alignment <= 1) [[unlikely]] {
            return value;
        }
        const size_t rem = value % alignment;
        return rem == 0 ? value : (value + (alignment - rem));
    }

    [[nodiscard]] static bool write_binary_file(std::string_view path, const std::vector<uint8_t>& bytes) {
        if (path.empty()) [[unlikely]] return false;
        std::ofstream out(path.data(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) [[unlikely]] return false;
        
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        
        out.close();
        return out.good();
    }

    [[nodiscard]] static constexpr uint32_t to_big_endian_u32(uint32_t val) noexcept {
#if defined(_MSC_VER)
        return _byteswap_ulong(val);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap32(val);
#else
        return ((val & 0xFF000000u) >> 24) |
               ((val & 0x00FF0000u) >> 8)  |
               ((val & 0x0000FF00u) << 8)  |
               ((val & 0x000000FFu) << 24);
#endif
    }

    [[nodiscard]] static inline size_t x64_prologue_size(const machine::MachineFunction& function) noexcept {
        return dgtl::linker::driver::DGTLAOTLinker::x64_prologue_size(function);
    }

    [[nodiscard]] static constexpr size_t x64_prologue_size() noexcept {
        return 25;
    }

public:
    [[nodiscard]] static bool build_wasm_binary(
        const dgtl::machine::MachineFunction& function,
        std::vector<uint8_t>& out,
        std::string& diagnostic)
    {
        out.clear();
        diagnostic.clear();

        // Validate the canonical MIR directly.
        const auto verification =
            dgtl::machine::MachineVerifier::verify(function);

        if (!verification.ok()) {
            diagnostic = "WASM linker rejected invalid MachineFunction";
            return false;
        }

        if (function.instructions().empty()) {
            diagnostic = "WASM linker received an empty MachineFunction";
            return false;
        }

        // The linker must not reconstruct MIR from IR or run ISel again.
        dgtl::exec::DGTLWasmEncoder encoder;
        encoder.encode(function);

        out = encoder.get_binary();

        if (out.empty()) {
            diagnostic = "WASM binary emission produced no output";
            return false;
        }

        return true;
    }

    [[nodiscard]] static bool build_wasm_binary(
        const dgtl::machine::MachineFunction& function,
        std::vector<uint8_t>& out)
    {
        std::string diagnostic;
        return build_wasm_binary(function, out, diagnostic);
    }

    [[nodiscard]] static bool inspect_elf_object_from_memory(
        const void* data,
        size_t size,
        dgtl::execution::loader::Object_Format& out_object) {
        if (data == nullptr || size == 0) {
            return false;
        }
        try {
            dgtl::object::readers::ELFObjectReader reader;
            return reader.read(data, size, out_object);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] static bool inspect_binary_format_from_memory(
        const void* data,
        size_t size,
        dgtl::execution::loader::Binary_Format& out_binary) {
        if (data == nullptr || size == 0) return false;
        try {
            dgtl::execution::loader::Binary_Loader loader;
            auto bin = loader.load_from_memory(data, size);
            if (!bin) return false;
            out_binary = *bin;
            return true;
        } catch (...) {
            return false;
        }
    }

        [[nodiscard]] static bool build_cached_machine_code(
        dgtl::execution::cache::Code_Cache& cache,
        std::string_view key,
        const machine::MachineFunction& function,
        std::vector<uint8_t>& out_bytes) {
        if (!key.empty() && cache.retrieve_copy(key, out_bytes)) {
            return true;
        }
        DGTLLinker linker;
        if (!linker.build_raw_x64_machine_code(function, out_bytes)) {
            return false;
        }
        if (!key.empty()) {
            (void)cache.store(key, out_bytes.data(), out_bytes.size());
        }
        return true;
    }

    using CoffCallReloc = dgtl::linker::driver::CoffCallReloc;
    using CoffReloc     = dgtl::linker::driver::CoffReloc;
    using MagicMultiplierS64 = dgtl::linker::driver::MagicMultiplierS64;
    using DGTLAOTLinker = dgtl::linker::driver::DGTLAOTLinker;

    bool build_elf_relocatable_binary(const machine::MachineFunction& function,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_elf_relocatable_binary(function, out, entry_symbol);
    }

    bool build_static_archive_binary(const machine::MachineFunction& function,
                                     std::vector<uint8_t>& out,
                                     std::string_view member_name = "module.o") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_static_archive_binary(function, out, member_name);
    }

    bool build_coff_relocatable_binary_with_relocs(
        const machine::MachineFunction& function,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol,
        const std::vector<CoffCallReloc>& call_relocs,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary_with_relocs(
            function, out, entry_symbol, call_relocs, slot_to_callee, rdata_payload);
    }

    bool build_module_coff_with_relocs(
        const std::vector<const machine::MachineFunction*>& module_functions,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_module_coff_with_relocs(module_functions, out, slot_to_callee, rdata_payload);
    }

    bool build_module_coff_with_relocs(
        const std::vector<std::pair<std::string, const machine::MachineFunction*>>& module_streams,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee,
        const std::vector<uint8_t>& rdata_payload = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_module_coff_with_relocs(module_streams, out, slot_to_callee, rdata_payload);
    }

    bool build_coff_relocatable_binary(const machine::MachineFunction& function,
                                       std::vector<uint8_t>& out,
                                       std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_relocatable_binary(function, out, entry_symbol);
    }

    bool build_coff_archive_binary(const machine::MachineFunction& function,
                                   std::vector<uint8_t>& out,
                                   std::string_view member_name = "module.obj",
                                   std::string_view entry_symbol = "DGTL_kernel_entry") {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_coff_archive_binary(function, out, member_name, entry_symbol);
    }

    bool build_pe_executable_buffer(const machine::MachineFunction& function,
                                    std::vector<uint8_t>& pe_image) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable_buffer(function, pe_image);
    }

    bool build_pe_executable(std::string_view path, const machine::MachineFunction& function) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, function);
    }

    bool emit_pe_executable(std::string_view path, const machine::MachineFunction& function) {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.build_pe_executable(path, function);
    }

    bool emit_aot_machine_code(
        uint8_t* dest,
        size_t dest_capacity,
        const machine::MachineFunction& function,
        size_t* emitted_size = nullptr,
        const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
        std::vector<CoffCallReloc>* out_relocs = nullptr,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.emit_aot_machine_code(dest, dest_capacity, function, emitted_size, slot_map, out_relocs, slot_to_callee);
    }

    [[nodiscard]] const std::vector<std::string>& fallback_diagnostics() const noexcept {
        return fallback_diagnostics_;
    }

    std::vector<CoffCallReloc> collect_call_relocs(
        const machine::MachineFunction& function,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.collect_call_relocs(function, slot_to_callee);
    }

    bool build_raw_x64_machine_code(const machine::MachineFunction& function,
                                    std::vector<uint8_t>& out_bytes,
                                    const std::unordered_map<uint32_t, uintptr_t>& slot_map = {}) {
        out_bytes.resize(function.instructions().size() * 64 + 1024);
        size_t emitted_size = 0;
        if (!emit_machine_code(out_bytes.data(), out_bytes.size(), function, &emitted_size, slot_map)) {
            return false;
        }
        out_bytes.resize(emitted_size);
        return true;
    }

    bool emit_machine_code(uint8_t* dest,
                           size_t dest_capacity,
                           const machine::MachineFunction& function,
                           size_t* emitted_size = nullptr,
                           const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
                           std::vector<CoffCallReloc>* out_relocs = nullptr,
                           const std::unordered_map<uint32_t, std::string>& slot_to_callee = {}) noexcept {
        dgtl::linker::driver::DGTLAOTLinker aot;
        return aot.emit_aot_machine_code(dest, dest_capacity, function, emitted_size, slot_map, out_relocs, slot_to_callee);
    }
}; // class DGTLLinker

} // namespace dgtl::linker

// ========================================================================================
// BACKWARD COMPATIBILITY ALIASES (dgtl::exec)
// ========================================================================================
export namespace dgtl::exec {
    using DGTLLinker = dgtl::linker::DGTLLinker;
} // namespace dgtl::exec

export namespace dgtl::backend::linker {
    using DGTLLinker = dgtl::linker::DGTLLinker;
}

