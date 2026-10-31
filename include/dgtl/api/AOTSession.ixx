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

module;

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>
#include <utility>
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

export module dgtl.api.aot_session;

import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.ir.stream;
import dgtl.ir.evolution;
import dgtl.runtime.tls;
import dgtl.mir.machine_ir;
import dgtl.mir.pipeline;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.target;
import dgtl.codegen.driver;
import dgtl.linker.driver;
import dgtl.linker.driver_api;
import dgtl.object.writers.pe;
import dgtl.object.writers.elf;
import dgtl.formal;

import dgtl.api.config;
import dgtl.api.execution_engine;

export namespace dgtl {
namespace api {

/**
 * @class AOTSession
 * @brief Ahead-Of-Time (AOT) execution and binary synthesis engine.
 * Powered by DGTL's sovereign compilation pipeline:
 * IRStream -> CodegenDriver -> ObjectFile -> DGTLAOTLinker -> Standalone Binary.
 */
class AOTSession : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    explicit AOTSession(Context& context)
        : context_(context),
          target_machine_(),
          pipeline_options_() {
        switch (context.get_config().target_arch) {
            case TargetArch::X86_64:  target_machine_ = core::TargetMachine(core::ArchType::X86_64_AVX2); break;
            case TargetArch::ARM64:   target_machine_ = core::TargetMachine(core::ArchType::AARCH64_SVE); break;
            case TargetArch::ARM32:   target_machine_ = core::TargetMachine(core::ArchType::ARM32_NEON); break;
            case TargetArch::RISCV64: target_machine_ = core::TargetMachine(core::ArchType::RISCV64_RVV); break;
            case TargetArch::WASM:    target_machine_ = core::TargetMachine(core::ArchType::WASM_SIMD); break;
            case TargetArch::CUDA:
            case TargetArch::PTX:     target_machine_ = core::TargetMachine(core::ArchType::CUDA_PTX); break;
            case TargetArch::ROCm:
            case TargetArch::AMDGPU:  target_machine_ = core::TargetMachine(core::ArchType::ROCM_AMDGCN); break;
            case TargetArch::SPIRV:   target_machine_ = core::TargetMachine(core::ArchType::SPIRV_LOGICAL); break;
            case TargetArch::Host:
            default: break;
        }
    }

    ~AOTSession() override = default;

    [[nodiscard]] bool compile(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() == 0) return false;

        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        std::unordered_map<uint32_t, std::string> slot_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            slot_syms_copy = slot_to_symbol_;
        }

        auto codegen_res = dgtl::codegen::CodegenDriver::compile(
            stream,
            target_machine_,
            slot_syms_copy,
            "DGTL_kernel_entry",
            pipeline_options_
        );

        if (!codegen_res.success) {
            return false;
        }

        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            if (!cached_object_file_) {
                cached_object_file_ = std::make_unique<dgtl::object::ObjectFile>();
            }
            for (const auto& sym : codegen_res.object.symbols()) {
                if (sym.is_defined() && !sym.name.empty()) {
                    defined_symbols_[sym.name] = sym.value;
                }
            }
            *cached_object_file_ = std::move(codegen_res.object);
        }

        return true;
    }

    [[nodiscard]] bool compile_mir(const machine::MachineFunction& fn) override {
        std::unordered_map<uint32_t, std::string> slot_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            slot_syms_copy = slot_to_symbol_;
        }
        auto codegen_res = dgtl::codegen::CodegenDriver::compile(
            fn,
            target_machine_,
            slot_syms_copy,
            fn.name().empty() ? "DGTL_kernel_entry" : fn.name()
        );
        if (!codegen_res.success) {
            return false;
        }
        return compile_object(codegen_res.object);
    }

    [[nodiscard]] bool compile_object(const dgtl::object::ObjectFile& obj) override {
        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) {
            cached_object_file_ = std::make_unique<dgtl::object::ObjectFile>();
        }
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined() && !sym.name.empty()) {
                defined_symbols_[sym.name] = sym.value;
            }
        }
        *cached_object_file_ = obj;
        return true;
    }

    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override {
        if (!symbol_name) return 0;
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        auto it = host_symbols_.find(symbol_name);
        if (it != host_symbols_.end()) return it->second;
        auto d_it = defined_symbols_.find(symbol_name);
        if (d_it != defined_symbols_.end()) return d_it->second;
        return 0;
    }

    void register_host_symbol(const std::string& symbol_name, uint64_t address) override {
        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        host_symbols_[symbol_name] = address;
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) override {
        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        const std::string name = std::string("host_sym_") + std::to_string(slot);
        host_symbols_[name] = address;
        slot_to_symbol_[slot] = name;
    }

    [[nodiscard]] bool emit_object_to_memory(std::vector<uint8_t>& out_bytes) noexcept override {
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) return false;

        dgtl::linker::LinkerDriver driver;
        dgtl::linker::OutputFormat fmt = dgtl::linker::OutputFormat::COFFObject;
        if (cached_object_file_->format() == dgtl::object::ObjectFormat::ELF) {
            fmt = dgtl::linker::OutputFormat::ELFObject;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::MachO) {
            fmt = dgtl::linker::OutputFormat::MachOObject;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::WASM) {
            fmt = dgtl::linker::OutputFormat::WASMBinary;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::COFF ||
                   cached_object_file_->format() == dgtl::object::ObjectFormat::PE) {
            fmt = dgtl::linker::OutputFormat::COFFObject;
        } else {
#if defined(_WIN32) || defined(_WIN64)
            fmt = dgtl::linker::OutputFormat::COFFObject;
#else
            fmt = dgtl::linker::OutputFormat::ELFObject;
#endif
        }

        return driver.emit(fmt, *cached_object_file_, out_bytes) && !out_bytes.empty();
    }

    [[nodiscard]] bool emit_binary_to_memory(std::vector<uint8_t>& out_bytes) noexcept override {
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) return false;

        dgtl::linker::LinkerDriver driver;
        dgtl::linker::OutputFormat fmt = dgtl::linker::OutputFormat::PEExecutable;
        if (target_machine_.get_arch() == core::ArchType::WASM_SIMD ||
            cached_object_file_->format() == dgtl::object::ObjectFormat::WASM) {
            fmt = dgtl::linker::OutputFormat::WASMBinary;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::MachO) {
            fmt = dgtl::linker::OutputFormat::MachOExecutable;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::ELF) {
            fmt = dgtl::linker::OutputFormat::ELFExecutable;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::COFF ||
                   cached_object_file_->format() == dgtl::object::ObjectFormat::PE) {
            fmt = dgtl::linker::OutputFormat::PEExecutable;
        } else {
#if defined(_WIN32) || defined(_WIN64)
            fmt = dgtl::linker::OutputFormat::PEExecutable;
#elif defined(__APPLE__)
            fmt = dgtl::linker::OutputFormat::MachOExecutable;
#else
            fmt = dgtl::linker::OutputFormat::ELFExecutable;
#endif
        }

        if (!driver.emit(fmt, *cached_object_file_, out_bytes) || out_bytes.empty()) {
            if (fmt == dgtl::linker::OutputFormat::PEExecutable) {
                dgtl::backend::aot::DGTLAOTLinker linker;
                return linker.build_pe_executable_from_object(*cached_object_file_, out_bytes) && !out_bytes.empty();
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] bool emit_wasm_binary_to_memory(std::vector<uint8_t>& out_bytes) noexcept override {
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) return false;

        dgtl::linker::LinkerDriver driver;
        return driver.emit(dgtl::linker::OutputFormat::WASMBinary, *cached_object_file_, out_bytes) && !out_bytes.empty();
    }

    [[nodiscard]] bool emit_object(std::string_view output_path) noexcept override {
        std::vector<uint8_t> bytes;
        if (!emit_object_to_memory(bytes)) return false;

        std::ofstream out(std::string(output_path), std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return out.good();
    }

    [[nodiscard]] bool emit_binary(std::string_view output_path) noexcept override {
        std::vector<uint8_t> binary_image;
        if (!emit_binary_to_memory(binary_image)) return false;

        std::ofstream out(std::string(output_path), std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(binary_image.data()), static_cast<std::streamsize>(binary_image.size()));
        return out.good();
    }

    [[nodiscard]] formal::VerificationResult verify_semantics(
        const ir::IRStream& original_stream,
        const ir::IRStream& optimized_stream,
        std::string_view pass_name,
        std::string_view solver_name = "z3"
    ) override {
        formal::DGTLFormalVerificationSMT verifier;
        const std::string script = verifier.export_to_smtlib2(original_stream, optimized_stream, pass_name);
        return verifier.verify_with_external_solver(script, solver_name);
    }

    [[nodiscard]] bool specialize_stream(
        ir::IRStream& stream,
        const ir::evolution::ExecutionFeedback& feedback
    ) override {
        ir::evolution::IREvolver evolver;
        auto res = evolver.apply_specialization(stream, feedback);
        return res.succeeded;
    }

    [[nodiscard]] dgtl::mir::MachinePipelineOptions& get_pipeline() noexcept { return pipeline_options_; }
    [[nodiscard]] const dgtl::mir::MachinePipelineOptions& get_pipeline() const noexcept { return pipeline_options_; }
    [[nodiscard]] dgtl::mir::MachinePipelineOptions& get_pipeline_options() noexcept { return pipeline_options_; }
    [[nodiscard]] const dgtl::mir::MachinePipelineOptions& get_pipeline_options() const noexcept { return pipeline_options_; }

    [[nodiscard]] core::TargetMachine& get_target_machine() noexcept { return target_machine_; }
    [[nodiscard]] const core::TargetMachine& get_target_machine() const noexcept { return target_machine_; }
    void set_target_machine(const core::TargetMachine& tm) noexcept { target_machine_ = tm; }
    void set_target_arch(core::ArchType arch) noexcept { target_machine_ = core::TargetMachine(arch); }

private:
    Context& context_;
    core::TargetMachine target_machine_;
    dgtl::mir::MachinePipelineOptions pipeline_options_{};

    std::unique_ptr<dgtl::object::ObjectFile> cached_object_file_;
    std::unordered_map<std::string, uint64_t> host_symbols_;
    std::unordered_map<uint32_t, std::string> slot_to_symbol_;
    std::unordered_map<std::string, uint64_t> defined_symbols_;
    mutable std::shared_mutex symbol_mutex_;

    AOTSession(const AOTSession&) = delete;
    AOTSession& operator=(const AOTSession&) = delete;
};

} // namespace api
} // namespace dgtl // sovereign AOT session
