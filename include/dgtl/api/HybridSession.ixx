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
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

export module dgtl.api.hybrid_session;

import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.runtime.bulletins;
import dgtl.runtime.tls;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.mir.pipeline;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.target;
import dgtl.codegen.driver;
import dgtl.jit.object_loader;
import dgtl.linker.driver;
import dgtl.linker.driver_api;

import dgtl.api.execution_engine;
import dgtl.formal;

export namespace dgtl {
namespace api {

/**
 * @class HybridSession
 * @brief Adaptive tiered execution engine.
 * Dynamically selects between JIT and AOT based on execution tier.
 * Powered by DGTL's sovereign compilation pipeline.
 */
class alignas(64) HybridSession final : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    explicit HybridSession(Context& context)
        : context_(context),
          target_machine_(),
          pipeline_options_() {
    }

    ~HybridSession() override = default;

    [[nodiscard]] bool compile(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() == 0) return false;

        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        std::unordered_map<uint32_t, std::string> slot_syms_copy;
        std::unordered_map<std::string, uintptr_t> host_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            slot_syms_copy = slot_to_symbol_;
            host_syms_copy = host_symbols_;
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

        auto load_res = dgtl::jit::JITObjectLoader::load(
            codegen_res.object,
            host_syms_copy,
            "DGTL_kernel_entry"
        );

        if (!load_res.success) {
            return false;
        }

        uintptr_t entry_addr = load_res.module.entry_point();

        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            if (!cached_object_file_) {
                cached_object_file_ = std::make_unique<dgtl::object::ObjectFile>();
            }
            *cached_object_file_ = codegen_res.object;

            loaded_modules_.push_back(std::move(load_res.module));
            const auto* base = static_cast<const uint8_t*>(loaded_modules_.back().memory());
            if (base) {
                for (const auto& sym : codegen_res.object.symbols()) {
                    if (sym.is_defined() && !sym.name.empty()) {
                        symbol_map_[sym.name] = reinterpret_cast<uint64_t>(base + sym.value);
                    }
                }
            }
            symbol_map_["DGTL_kernel_entry"] = entry_addr;
            symbol_map_["main"] = entry_addr;
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
        std::unordered_map<std::string, uintptr_t> host_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            host_syms_copy = host_symbols_;
        }

        auto load_res = dgtl::jit::JITObjectLoader::load(obj, host_syms_copy, "DGTL_kernel_entry");
        if (!load_res.success) {
            return false;
        }

        uintptr_t entry_addr = load_res.module.entry_point();

        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            if (!cached_object_file_) {
                cached_object_file_ = std::make_unique<dgtl::object::ObjectFile>();
            }
            *cached_object_file_ = obj;

            loaded_modules_.push_back(std::move(load_res.module));
            const auto* base = static_cast<const uint8_t*>(loaded_modules_.back().memory());
            if (base) {
                for (const auto& sym : obj.symbols()) {
                    if (sym.is_defined() && !sym.name.empty()) {
                        symbol_map_[sym.name] = reinterpret_cast<uint64_t>(base + sym.value);
                    }
                }
            }
            symbol_map_["DGTL_kernel_entry"] = entry_addr;
            symbol_map_["main"] = entry_addr;
        }

        return true;
    }

    [[nodiscard]] bool emit_object(std::string_view output_path) noexcept override {
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) return false;

        std::vector<uint8_t> bytes;
        dgtl::linker::LinkerDriver driver;
        dgtl::linker::OutputFormat fmt = dgtl::linker::OutputFormat::COFFObject;
        if (cached_object_file_->format() == dgtl::object::ObjectFormat::ELF) {
            fmt = dgtl::linker::OutputFormat::ELFObject;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::MachO) {
            fmt = dgtl::linker::OutputFormat::MachOObject;
        } else if (cached_object_file_->format() == dgtl::object::ObjectFormat::WASM) {
            fmt = dgtl::linker::OutputFormat::WASMBinary;
        } else {
#if defined(_WIN32) || defined(_WIN64)
            fmt = dgtl::linker::OutputFormat::COFFObject;
#else
            fmt = dgtl::linker::OutputFormat::ELFObject;
#endif
        }

        if (!driver.emit(fmt, *cached_object_file_, bytes) || bytes.empty()) {
            return false;
        }

        std::ofstream out(std::string(output_path), std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return out.good();
    }

    [[nodiscard]] bool emit_binary(std::string_view output_path) noexcept override {
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        if (!cached_object_file_) return false;

        dgtl::linker::LinkerDriver driver;
        std::vector<uint8_t> binary_image;
        dgtl::linker::OutputFormat fmt = dgtl::linker::OutputFormat::PEExecutable;
        if (target_machine_.get_arch() == core::ArchType::WASM_SIMD ||
            cached_object_file_->format() == dgtl::object::ObjectFormat::WASM) {
            fmt = dgtl::linker::OutputFormat::WASMBinary;
        } else {
#if defined(_WIN32) || defined(_WIN64)
            fmt = dgtl::linker::OutputFormat::PEExecutable;
#else
            fmt = dgtl::linker::OutputFormat::ELFExecutable;
#endif
        }

        if (!driver.emit(fmt, *cached_object_file_, binary_image) || binary_image.empty()) {
            dgtl::backend::aot::DGTLAOTLinker linker;
            if (!linker.build_pe_executable_from_object(*cached_object_file_, binary_image) || binary_image.empty()) {
                return false;
            }
        }

        std::ofstream out(std::string(output_path), std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(binary_image.data()), static_cast<std::streamsize>(binary_image.size()));
        return out.good();
    }

    [[nodiscard]] int64_t execute(const machine::MachineFunction& fn, void* ctx = nullptr) override {
        void* ptr = get_function_pointer(fn.name());
        if (!ptr) {
            if (!compile_mir(fn)) return -1;
            ptr = get_function_pointer(fn.name());
            if (!ptr) ptr = get_function_pointer("DGTL_kernel_entry");
            if (!ptr) return -1;
        }
        using NativeFn = int64_t (*)(void*);
        return reinterpret_cast<NativeFn>(ptr)(ctx);
    }

    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override {
        if (!symbol_name) return 0;
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        auto it = symbol_map_.find(symbol_name);
        if (it != symbol_map_.end()) {
            return it->second;
        }
        auto h_it = host_symbols_.find(symbol_name);
        if (h_it != host_symbols_.end()) {
            return h_it->second;
        }
        return 0;
    }

    void register_host_symbol(const std::string& symbol_name, uint64_t address) override {
        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        host_symbols_[symbol_name] = static_cast<uintptr_t>(address);
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) override {
        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        const std::string name = std::string("host_sym_") + std::to_string(slot);
        host_symbols_[name] = static_cast<uintptr_t>(address);
        slot_to_symbol_[slot] = name;
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

private:
    Context& context_;
    core::TargetMachine target_machine_;
    dgtl::mir::MachinePipelineOptions pipeline_options_{};

    std::unique_ptr<dgtl::object::ObjectFile> cached_object_file_;
    std::vector<dgtl::jit::JITLoadedModule> loaded_modules_;
    std::unordered_map<std::string, uintptr_t> host_symbols_;
    std::unordered_map<uint32_t, std::string> slot_to_symbol_;
    std::unordered_map<std::string, uint64_t> symbol_map_;
    mutable std::shared_mutex symbol_mutex_;

    HybridSession(const HybridSession&) = delete;
    HybridSession& operator=(const HybridSession&) = delete;
};

} // namespace api
} // namespace dgtl // sovereign hybrid session
