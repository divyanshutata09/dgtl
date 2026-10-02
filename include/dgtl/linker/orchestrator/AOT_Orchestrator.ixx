/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* 
 * DGTL AOT ORCHESTRATOR
 * MODULE: dgtl.backend.orchestrator.aot
 * PURPOSE: Sovereign, industrial-grade AOT compiler orchestrator. Coordinates
 *          whole-program offline lowering, multi-module IR/MIR unification,
 *          and direct binary synthesis (.obj, .lib, .exe, .o, .a, .wasm, .macho).
 */

module;

#include <cstdlib>
#include <vector>
#include <string_view>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #undef CONST
    #undef IMAGE_FILE_MACHINE_AMD64
    #undef IMAGE_REL_AMD64_REL32
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

export module dgtl.linker.aot_orchestrator;

import dgtl.target;
import dgtl.linker.driver;
import dgtl.runtime.bulletins;
import dgtl.mir;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.writers.pe;
import dgtl.object.writers.elf;
import dgtl.object.writers.macho;
import dgtl.object.writers.wasm;
import dgtl.linker.driver_api;
import dgtl.mir.machine_ir;

export namespace dgtl::linker::aot {

struct AOTCompilationReport {
    double total_ms = 0.0;
    size_t nodes_processed = 0;
    double throughput_nodes_sec = 0.0;
    size_t text_section_bytes = 0;
    size_t rdata_section_bytes = 0;
    size_t symbol_count = 0;
    std::string target_format;
};

class DGTLAOTOrchestrator {
protected:
    dgtl::linker::driver::DGTLAOTLinker linker_;
    AOTCompilationReport last_report_{};

    [[nodiscard]] static bool write_file(std::string_view path, const std::vector<uint8_t>& bytes) {
        if (path.empty()) return false;
        std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        return out.good();
    }

public:
    DGTLAOTOrchestrator() = default;

    [[nodiscard]] const AOTCompilationReport& get_last_report() const noexcept {
        return last_report_;
    }

    [[nodiscard]] const std::vector<std::string>& get_diagnostics() const noexcept {
        return linker_.fallback_diagnostics();
    }

    [[nodiscard]] static std::unordered_map<uint32_t, std::string> merge_default_callees(
        const std::unordered_map<uint32_t, std::string>& user_callees)
    {
        std::unordered_map<uint32_t, std::string> merged = user_callees;
        static const std::pair<uint32_t, const char*> kBuiltinCallees[] = {
            {0x80000000u, "dgtl_gpu_init_pipeline"},
            {0x80000001u, "dgtl_gpu_resize"},
            {0x80000002u, "dgtl_gpu_destroy_pipeline"},
            {0x80000003u, "dgtl_gpu_begin_frame"},
            {0x80000004u, "dgtl_gpu_end_frame"},
            {0x80000005u, "dgtl_gpu_push_clip_rect"},
            {0x80000006u, "dgtl_gpu_pop_clip_rect"},
            {0x80000007u, "dgtl_gpu_render_quad_instances"},
            {0x80000008u, "dgtl_gpu_render_glyph_instances"},
            {0x80000009u, "dgtl_gpu_render_vector_paths"},
            {0x8000000Au, "dgtl_gpu_upload_texture"},
            {0x8000000Bu, "dgtl_gpu_destroy_texture"},
            {0x8000000Cu, "dgtl_platform_poll_events"},
            {0x8000000Du, "dgtl_platform_get_time_us"},
            {0x8000000Eu, "dgtl_platform_set_cursor"}
        };
        for (const auto& [slot, name] : kBuiltinCallees) {
            merged.try_emplace(slot, name);
        }
        return merged;
    }

    /**
     * @brief Sovereign MIR MachineFunction direct compilation and emission.
     */
    bool compile_and_emit(
        std::string_view output_path,
        const machine::MachineFunction& fn,
        std::string_view entry_symbol = "DGTL_kernel_entry")
    {
        if (output_path.empty()) return false;
        const auto start_time = std::chrono::high_resolution_clock::now();
        last_report_ = AOTCompilationReport{};

        std::string path_str(output_path);
        std::string ext;
        if (const size_t dot = path_str.find_last_of('.'); dot != std::string::npos) {
            ext = path_str.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }

        std::vector<uint8_t> bytes;
        bool success = false;

        if (ext == ".obj") {
            last_report_.target_format = "COFF_OBJ";
            dgtl::object::writers::PEObjectWriter writer;
            success = writer.write_machine_function(fn, bytes, entry_symbol) && write_file(output_path, bytes);
        } else if (ext == ".lib") {
            last_report_.target_format = "COFF_LIB";
            dgtl::object::writers::PEObjectWriter writer;
            std::vector<uint8_t> obj_bytes;
            success = writer.write_machine_function(fn, obj_bytes, entry_symbol) &&
                      writer.build_archive(obj_bytes, bytes, "module.obj") &&
                      write_file(output_path, bytes);
        } else if (ext == ".exe") {
            last_report_.target_format = "PE_EXE";
            dgtl::object::writers::PEObjectWriter writer;
            success = writer.write_standalone_executable(fn, bytes, entry_symbol) && write_file(output_path, bytes);
        } else if (ext == ".o") {
            last_report_.target_format = "ELF_OBJ";
            dgtl::object::writers::ELFObjectWriter writer;
            success = writer.write_machine_function(fn, bytes, entry_symbol) && write_file(output_path, bytes);
        } else if (ext == ".a") {
            last_report_.target_format = "ELF_ARCHIVE";
            dgtl::object::writers::ELFObjectWriter writer;
            std::vector<uint8_t> obj_bytes;
            success = writer.write_machine_function(fn, obj_bytes, entry_symbol) &&
                      writer.build_archive(obj_bytes, bytes, "module.o") &&
                      write_file(output_path, bytes);
        } else if (ext == ".wasm") {
            last_report_.target_format = "WASM_BINARY";
            dgtl::object::writers::WASMBinaryWriter writer;
            success = writer.write_machine_function(fn, bytes) && write_file(output_path, bytes);
        } else if (ext == ".macho") {
            last_report_.target_format = "MACHO_OBJ";
            dgtl::object::writers::MachOObjectWriter writer;
            success = writer.write_machine_function(fn, bytes, entry_symbol) && write_file(output_path, bytes);
        } else {
            // Default to COFF relocatable object
            last_report_.target_format = "COFF_OBJ_DEFAULT";
            dgtl::object::writers::PEObjectWriter writer;
            success = writer.write_machine_function(fn, bytes, entry_symbol) && write_file(output_path, bytes);
        }

        const auto end_time = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        last_report_.total_ms = elapsed.count();
        last_report_.text_section_bytes = bytes.size();
        return success;
    }

    /**
     * @brief Universal ObjectFile direct emission.
     */
    bool compile_and_emit(
        std::string_view output_path,
        const dgtl::object::ObjectFile& obj)
    {
        if (output_path.empty()) return false;
        std::vector<uint8_t> bytes;
        bool success = false;

        switch (obj.format()) {
            case dgtl::object::ObjectFormat::COFF: {
                dgtl::object::writers::PEObjectWriter writer;
                success = writer.write(obj, bytes);
                break;
            }
            case dgtl::object::ObjectFormat::ELF: {
                dgtl::object::writers::ELFObjectWriter writer;
                success = writer.write(obj, bytes);
                break;
            }
            case dgtl::object::ObjectFormat::MachO: {
                dgtl::object::writers::MachOObjectWriter writer;
                success = writer.write(obj, bytes);
                break;
            }
            case dgtl::object::ObjectFormat::WASM: {
                dgtl::object::writers::WASMBinaryWriter writer;
                success = writer.write(obj, bytes);
                break;
            }
        }
        if (success) {
            return write_file(output_path, bytes);
        }
        return false;
    }

    /**
     * @brief Compile multiple MachineFunctions into a unified object file.
     */
    bool compile_module(
        std::string_view output_path,
        const std::vector<const machine::MachineFunction*>& functions)
    {
        if (output_path.empty() || functions.empty()) return false;
        dgtl::object::ObjectFile unified_obj(dgtl::object::ObjectFormat::COFF);
        auto& text_sec = unified_obj.get_text_section();

        for (const auto* fn_ptr : functions) {
            if (!fn_ptr) continue;
            const auto& fn = *fn_ptr;
            uint64_t fn_offset = text_sec.bytes.size();
            if (fn_offset % 16 != 0) {
                size_t pad = 16 - (fn_offset % 16);
                text_sec.bytes.insert(text_sec.bytes.end(), pad, 0x90);
                fn_offset = text_sec.bytes.size();
            }
            unified_obj.add_function_symbol(std::string(fn.name()), fn_offset, 0, true);
        }

        return compile_and_emit(output_path, unified_obj);
    }

    /**
     * @brief Emit a single MachineFunction to a target file, dispatching automatically by file extension.
     */
    bool compile_and_emit(
        std::string_view output_path,
        const machine::MachineFunction& stream,
        std::string_view entry_symbol,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        if (output_path.empty() || stream.instructions().empty()) return false;

        const auto start_time = std::chrono::high_resolution_clock::now();
        last_report_ = AOTCompilationReport{};
        last_report_.nodes_processed = stream.instructions().size();

        std::string path_str(output_path);
        std::string ext;
        if (const size_t dot = path_str.find_last_of('.'); dot != std::string::npos) {
            ext = path_str.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }

        bool success = false;
        const std::vector<uint8_t> rdata_payload = dgtl::runtime::build_registered_strings_rdata();
        const auto effective_callees = merge_default_callees(slot_to_callee);

        if (ext == ".obj") {
            last_report_.target_format = "COFF_OBJ";
            std::vector<uint8_t> bytes;
            success = linker_.build_coff_relocatable_binary_with_relocs(
                stream, bytes, entry_symbol, {}, effective_callees, rdata_payload);
            if (success) success = write_file(output_path, bytes);
        }
        else if (ext == ".lib") {
            last_report_.target_format = "COFF_LIB";
            std::vector<uint8_t> bytes;
            success = linker_.build_coff_archive_binary(stream, bytes, "module.obj", entry_symbol);
            if (success) success = write_file(output_path, bytes);
        }
        else if (ext == ".exe") {
            last_report_.target_format = "PE_EXE";
            success = linker_.build_pe_executable(output_path, stream);
        }
        else if (ext == ".o") {
            last_report_.target_format = "ELF_OBJ";
            std::vector<uint8_t> bytes;
            success = linker_.build_elf_relocatable_binary(stream, bytes, entry_symbol);
            if (success) success = write_file(output_path, bytes);
        }
        else if (ext == ".a") {
            last_report_.target_format = "ELF_ARCHIVE";
            std::vector<uint8_t> bytes;
            success = linker_.build_static_archive_binary(stream, bytes, "module.o");
            if (success) success = write_file(output_path, bytes);
        }
        else {
            // Default to COFF relocatable object
            last_report_.target_format = "COFF_OBJ_DEFAULT";
            std::vector<uint8_t> bytes;
            success = linker_.build_coff_relocatable_binary_with_relocs(
                stream, bytes, entry_symbol, {}, effective_callees, rdata_payload);
            if (success) success = write_file(output_path, bytes);
        }

        const auto end_time = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        last_report_.total_ms = elapsed.count();
        if (last_report_.total_ms > 1e-9) {
            last_report_.throughput_nodes_sec = (static_cast<double>(stream.instructions().size()) / (last_report_.total_ms / 1000.0));
        }

        return success;
    }

    /**
     * @brief Compile and emit a multi-function module to a dual-section COFF object.
     */
    bool compile_module_to_coff(
        std::string_view output_path,
        const std::vector<const machine::MachineFunction*>& module_streams,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        if (output_path.empty() || module_streams.empty()) return false;

        const auto start_time = std::chrono::high_resolution_clock::now();
        last_report_ = AOTCompilationReport{};
        last_report_.target_format = "COFF_MODULE_OBJ";

        size_t total_nodes = 0;
        for (const auto* fn_ptr : module_streams) {
            if (fn_ptr && !fn_ptr->instructions().empty()) {
                total_nodes += fn_ptr->instructions().size();
            }
        }
        last_report_.nodes_processed = total_nodes;

        const std::vector<uint8_t> rdata_payload = dgtl::runtime::build_registered_strings_rdata();
        const auto effective_callees = merge_default_callees(slot_to_callee);
        std::vector<uint8_t> bytes;

        const bool success = linker_.build_module_coff_with_relocs(
            module_streams, bytes, effective_callees, rdata_payload);

        if (!success) return false;
        if (!write_file(output_path, bytes)) return false;

        const auto end_time = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        last_report_.total_ms = elapsed.count();
        if (last_report_.total_ms > 1e-9) {
            last_report_.throughput_nodes_sec = (static_cast<double>(total_nodes) / (last_report_.total_ms / 1000.0));
        }

        return true;
    }

    /**
     * @brief Compile and emit MachineFunction to a static library archive (.lib / .a).
     */
    bool compile_to_archive(
        std::string_view output_path,
        const machine::MachineFunction& stream,
        std::string_view member_name = "module.obj",
        std::string_view entry_symbol = "DGTL_kernel_entry")
    {
        if (output_path.empty() || stream.instructions().empty()) return false;
        std::vector<uint8_t> bytes;
        bool ok = false;
        std::string path_str(output_path);
        if (path_str.size() >= 2 && path_str.substr(path_str.size() - 2) == ".a") {
            ok = linker_.build_static_archive_binary(stream, bytes, member_name.empty() ? "module.o" : member_name);
        } else {
            ok = linker_.build_coff_archive_binary(stream, bytes, member_name, entry_symbol);
        }
        if (!ok) return false;
        return write_file(output_path, bytes);
    }

    /**
     * @brief Direct-to-binary compilation for standalone executable (.exe).
     */
    bool compile_to_executable(
        std::string_view output_path,
        const machine::MachineFunction& stream)
    {
        if (output_path.empty() || stream.instructions().empty()) return false;
        return linker_.build_pe_executable(output_path, stream);
    }
};

} // namespace dgtl::linker::aot

export namespace dgtl::backend::aot {
    using DGTLAOTOrchestrator = dgtl::linker::aot::DGTLAOTOrchestrator;
    using AOTCompilationReport = dgtl::linker::aot::AOTCompilationReport;
}

export namespace dgtl::backend::orchestrator {
    using DGTLAOTOrchestrator = dgtl::linker::aot::DGTLAOTOrchestrator;
    using AOTCompilationReport = dgtl::linker::aot::AOTCompilationReport;
}



