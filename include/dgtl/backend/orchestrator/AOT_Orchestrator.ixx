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
 * MODULE: dgtl.backend.aot_orchestrator
 * PURPOSE: Sovereign, industrial-grade AOT compiler orchestrator. Coordinates
 *          whole-program offline lowering, multi-module IR stream unification,
 *          and direct binary synthesis (.obj, .lib, .exe, .o, .a, .wasm).
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

export module dgtl.backend.aot_orchestrator;

import dgtl.ir_core;
import dgtl.target;
import dgtl.ir_core.stream;
import dgtl.backend.aot_linker;
import dgtl.dgtl_bulletins;

export namespace dgtl::backend::aot {

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
    DGTLAOTLinker linker_;
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
     * @brief Emit a single IRStream to a target file, dispatching automatically by file extension.
     */
    bool compile_and_emit(
        std::string_view output_path,
        const ir::IRStream& stream,
        std::string_view entry_symbol = "DGTL_kernel_entry",
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        if (output_path.empty() || stream.is_null() || stream.size() == 0) return false;

        const auto start_time = std::chrono::high_resolution_clock::now();
        last_report_ = AOTCompilationReport{};
        last_report_.nodes_processed = stream.size();

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
            last_report_.throughput_nodes_sec = (static_cast<double>(stream.size()) / (last_report_.total_ms / 1000.0));
        }

        return success;
    }

    /**
     * @brief Compile and emit a multi-function module to a dual-section COFF object.
     */
    bool compile_module_to_coff(
        std::string_view output_path,
        const std::vector<std::pair<std::string, ir::IRStream>>& module_streams,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        if (output_path.empty() || module_streams.empty()) return false;

        const auto start_time = std::chrono::high_resolution_clock::now();
        last_report_ = AOTCompilationReport{};
        last_report_.target_format = "COFF_MODULE_OBJ";

        size_t total_nodes = 0;
        for (const auto& [name, st] : module_streams) {
            if (!st.is_null()) total_nodes += st.size();
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
     * @brief Compile and emit IRStream to a static library archive (.lib / .a).
     */
    bool compile_to_archive(
        std::string_view output_path,
        const ir::IRStream& stream,
        std::string_view member_name = "module.obj",
        std::string_view entry_symbol = "DGTL_kernel_entry")
    {
        if (output_path.empty() || stream.is_null() || stream.size() == 0) return false;
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
        const ir::IRStream& stream)
    {
        if (output_path.empty() || stream.is_null() || stream.size() == 0) return false;
        return linker_.build_pe_executable(output_path, stream);
    }
};

} // namespace dgtl::backend::aot

export namespace dgtl::backend::orchestrator {
    using DGTLAOTOrchestrator = dgtl::backend::aot::DGTLAOTOrchestrator;
    using AOTCompilationReport = dgtl::backend::aot::AOTCompilationReport;
}
