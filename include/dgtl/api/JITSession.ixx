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
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

// Named module interface declaration
export module dgtl.api.jit_session;

import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.runtime.bulletins;
import dgtl.ir.stream;
import dgtl.runtime.tls;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.mir.pipeline;
import dgtl.object.file;
import dgtl.target;
import dgtl.codegen.driver;
import dgtl.jit.object_loader;

import dgtl.api.execution_engine;
import dgtl.formal;

export namespace dgtl {
namespace api {

/**
 * @class JITSession
 * @brief Just-In-Time execution engine.
 * Powered by DGTL's sovereign compilation pipeline:
 * IRStream -> CodegenDriver -> ObjectFile -> JITObjectLoader -> W^X Memory.
 */
class alignas(64) JITSession final : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    explicit JITSession(Context& context)
        : context_(context),
          target_machine_(),
          pipeline_options_() {
    }

    ~JITSession() override = default;

    /**
     * @brief Sovereign MachineFunction JIT Compilation.
     */
    [[nodiscard]] bool compile(const dgtl::machine::MachineFunction& function) {
        if (function.instructions().empty()) {
            return false;
        }

        const auto verify_result = dgtl::machine::MachineVerifier::verify(function);
        if (!verify_result.ok()) {
            return false;
        }

        const std::string name_key = function.name().empty()
            ? (std::string("dgtl_mf_") + std::to_string(reinterpret_cast<uintptr_t>(&function)))
            : std::string(function.name());

        std::unordered_map<std::string, uintptr_t> host_syms_copy;
        std::unordered_map<uint32_t, std::string> slot_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            host_syms_copy = host_symbols_;
            slot_syms_copy = slot_to_symbol_;
        }

        auto codegen_res = dgtl::codegen::CodegenDriver::compile(
            function,
            target_machine_,
            slot_syms_copy,
            name_key
        );

        if (!codegen_res.success) {
            return false;
        }

        return compile_object_with_name(codegen_res.object, name_key);
    }

    [[nodiscard]] bool compile_mir(const machine::MachineFunction& fn) override {
        return compile(fn);
    }

    [[nodiscard]] bool compile_object(const dgtl::object::ObjectFile& obj) override {
        return compile_object_with_name(obj, "DGTL_kernel_entry");
    }

    [[nodiscard]] bool compile_object_with_name(const dgtl::object::ObjectFile& obj, const std::string& name_key) {
        std::unordered_map<std::string, uintptr_t> host_syms_copy;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            host_syms_copy = host_symbols_;
        }

        auto load_res = dgtl::jit::JITObjectLoader::load(obj, host_syms_copy, name_key);
        if (!load_res.success) {
            return false;
        }

        uintptr_t entry_addr = load_res.module.entry_point();

        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            loaded_modules_.push_back(std::move(load_res.module));
            const auto* base = static_cast<const uint8_t*>(loaded_modules_.back().memory());
            if (base) {
                for (const auto& sym : obj.symbols()) {
                    if (sym.is_defined() && !sym.name.empty()) {
                        symbol_map_[sym.name] = reinterpret_cast<uint64_t>(base + sym.value);
                    }
                }
            }
            symbol_map_[name_key] = entry_addr;
            symbol_map_["DGTL_kernel_entry"] = entry_addr;
            symbol_map_["main"] = entry_addr;
        }

        return true;
    }

    /**
     * @brief Compile IRStream with MIR optimizations through sovereign CodegenDriver.
     */
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

    std::vector<dgtl::jit::JITLoadedModule> loaded_modules_;
    std::unordered_map<std::string, uintptr_t> host_symbols_;
    std::unordered_map<uint32_t, std::string> slot_to_symbol_;
    std::unordered_map<std::string, uint64_t> symbol_map_;
    mutable std::shared_mutex symbol_mutex_;

    JITSession(const JITSession&) = delete;
    JITSession& operator=(const JITSession&) = delete;
};

} // namespace api
} // namespace dgtl
