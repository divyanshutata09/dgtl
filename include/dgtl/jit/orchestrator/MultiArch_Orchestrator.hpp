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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.1.0
 * STEP 21: MULTI-ARCH ORCHESTRATOR (V6 - Hardware Aware)
 * PILLAR 2 & 3 INTEGRATION: TRANSACTIONAL GATEKEEPER
 * UNIFIED TARGETING: Integrated with dgtl::core::TargetMachine
 */

#pragma once
#include <memory>
#include <string_view>
#include <chrono>
#include <stdexcept>
#include <string>
#include <cctype>
#include "dgtl/core/Target.hpp"
#include "dgtl/ir/Global_IR.hpp"

// Forward Declarations for specialized backend emitters
namespace dgtl::exec {
    class DGTLLinker;      
    class DGTLMachOEmitter; 
    class DGTLWasmEncoder;  
}

namespace dgtl::core {

/**
 * @struct CompilationReport
 * @brief High-precision telemetry for the compilation strike.
 */
struct CompilationReport {
    double total_ms = 0.0;
    size_t nodes_processed = 0;
    double throughput_nodes_sec = 0.0;
    bool validation_opted = false;
    std::string target_arch_used; 
};

/**
 * @class DGTLOrchestrator
 * @brief The final gatekeeper before hardware-native binary generation.
 */
class DGTLOrchestrator {
    CompilationReport last_report;
    TargetMachine target; 

public:
    /**
     * @brief Constructor initializes hardware discovery.
     * Uses preprocessor detection to satisfy TargetMachine(ArchType).
     */
    explicit DGTLOrchestrator() 
        : target([]() {
            #if defined(__x86_64__) || defined(_M_X64)
                return select_preferred_x86_arch();
            #elif defined(__aarch64__) || defined(_M_ARM64)
                return ArchType::AARCH64_SVE;
            #else
                return ArchType::WASM_SIMD;
            #endif
          }()) 
    {}

    /**
     * @brief Zero-Corruption Entry Point for Binary Generation.
     * @param output_path The filesystem destination for the binary.
     * @param stream The validated, committed IRStream.
     */
    void compile_to_binary(std::string_view output_path, const ir::IRStream& stream) {
        if (output_path.empty()) [[unlikely]] {
            trigger_fatal_trap("INVALID_OUTPUT_PATH", "Output path for binary emission cannot be empty.");
        }

        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            return;
        }

        if (!stream.validate_shadow_integrity()) [[unlikely]] {
            trigger_fatal_trap("TRANSACTION_NOT_COMMITTED", 
                "Attempted to emit binary from an unvalidated Shadow Stream.");
        }
        
        last_report.validation_opted = true;
        
        // Manual string representation since get_triple() is missing in Target.hpp
        switch (target.get_arch()) {
            case ArchType::X86_64_AVX512:
            case ArchType::X86_64_AVX2:
                last_report.target_arch_used = "x86_64-pc-linux-gnu";
                break;
            case ArchType::AARCH64_SVE:
                last_report.target_arch_used = "aarch64-unknown-linux-gnu";
                break;
            case ArchType::WASM_SIMD:
                last_report.target_arch_used = "wasm32-unknown-unknown";
                break;
            case ArchType::FPGA_VERILOG:
            default:
                last_report.target_arch_used = "generic-unknown-unknown";
                break;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        std::string out_path(output_path);
        for (char& ch : out_path) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        if (out_path.ends_with(".wasm")) {
            this->dispatch_to_wasm(output_path, stream);
        }
        else if (out_path.ends_with(".o")) {
            this->dispatch_to_relocatable(output_path, stream);
        }
        else if (out_path.ends_with(".obj")) {
            this->dispatch_to_coff_relocatable(output_path, stream);
        }
        else if (out_path.ends_with(".a")) {
            this->dispatch_to_archive(output_path, stream);
        }
        else if (out_path.ends_with(".lib")) {
            this->dispatch_to_coff_archive(output_path, stream);
        }
        else if (out_path.ends_with(".macho")) {
            this->dispatch_to_macho(output_path, stream);
        }
        else {
            this->dispatch_to_linker(output_path, stream);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        
        last_report.total_ms = elapsed.count();
        last_report.nodes_processed = stream.size();

        if (last_report.total_ms > 1e-9) {
            last_report.throughput_nodes_sec = (static_cast<double>(stream.size()) / (last_report.total_ms / 1000.0));
        } else {
            last_report.throughput_nodes_sec = static_cast<double>(stream.size()) * 1000000.0; 
        }
    }

    [[nodiscard]] CompilationReport get_last_report() const noexcept {
        return last_report;
    }

    const TargetMachine& get_target() const noexcept {
        return target;
    }

private:
    [[noreturn]] void trigger_fatal_trap(const char* code, const char* details) {
        throw std::runtime_error(std::string(code) + ": " + details);
    }

    void dispatch_to_linker(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_relocatable(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_archive(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_coff_relocatable(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_coff_archive(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_macho(std::string_view path, const ir::IRStream& stream);
    void dispatch_to_wasm(std::string_view path, const ir::IRStream& stream);
};

} // namespace dgtl::core