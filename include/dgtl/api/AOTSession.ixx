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
#include <memory>
#include <string_view>
#include <vector>
#include <utility>
#include <string>
#include <unordered_map>

export module dgtl.api.aot_session;

import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.ir.stream;
import dgtl.ir.pipeline.pass_manager_orchestrator;
import dgtl.jit.jit_orchestrator;
import dgtl.linker.aot_orchestrator;
import dgtl.object.writers.elf;
import dgtl.object.writers.pe;
import dgtl.object.writers.macho;
import dgtl.object.writers.wasm;
import dgtl.runtime.tls;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.object.file;
import dgtl.formal.smt;

import dgtl.api.execution_engine;

export namespace dgtl {
namespace api {

/**
 * @class AOTSession
 * @brief Ahead-Of-Time (AOT) execution and binary synthesis engine.
 * Coordinates multi-target compilation, MIR lowering, and direct file synthesis.
 */
class AOTSession : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    explicit AOTSession(Context& context)
        : context_(context) {
        (void)pipeline_.initialize();
    }

    ~AOTSession() override = default;

    [[nodiscard]] bool compile(ir::IRStream& stream) override {
        if (stream.is_null()) return false;
        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        if (pipeline_.execute(stream) != dgtl::pipeline::AOTStatus::Success) {
            return false;
        }

        optimized_stream_ = stream.clone();

        dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
        dgtl::jit::StrikeOrchestrator orch(*context_.get_impl(), context_.get_impl()->get_arena());
        auto result = orch.compile_mir_function(optimized_stream_, mf);
        if (result.success) {
            cached_mir_function_ = std::make_unique<machine::MachineFunction>(std::move(mf));
        }
        return true;
    }

    [[nodiscard]] bool compile_mir(const machine::MachineFunction& fn) override {
        cached_mir_function_ = std::make_unique<machine::MachineFunction>(fn);
        return true;
    }

    [[nodiscard]] bool compile_object(const dgtl::object::ObjectFile& obj) override {
        cached_object_file_ = std::make_unique<dgtl::object::ObjectFile>(obj);
        return true;
    }

    [[nodiscard]] uint64_t get_symbol_address([[maybe_unused]] const char* symbol_name) noexcept override {
        auto it = host_symbols_.find(symbol_name ? symbol_name : "");
        return it != host_symbols_.end() ? it->second : 0;
    }

    void register_host_symbol(const std::string& symbol_name, uint64_t address) override {
        host_symbols_.insert_or_assign(symbol_name, address);
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) override {
        host_symbol_slots_[slot] = address;
    }

    [[nodiscard]] bool emit_object(std::string_view output_path) noexcept {
        if (cached_mir_function_) {
            return emit_mir_object(output_path, *cached_mir_function_);
        }
        if (cached_object_file_) {
            return emit_object_file(output_path, *cached_object_file_);
        }
        if (optimized_stream_.is_null() || optimized_stream_.size() == 0) return false;
        try {
            dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
            dgtl::jit::StrikeOrchestrator orch(*context_.get_impl(), context_.get_impl()->get_arena());
            auto result = orch.compile_mir_function(optimized_stream_, mf);
            if (!result.success) return false;
            return emit_mir_object(output_path, mf);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool emit_binary(std::string_view output_path) noexcept {
        if (cached_mir_function_) {
            return emit_mir_binary(output_path, *cached_mir_function_);
        }
        if (cached_object_file_) {
            return emit_object_file(output_path, *cached_object_file_);
        }
        if (optimized_stream_.is_null() || optimized_stream_.size() == 0) return false;
        try {
            dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
            dgtl::jit::StrikeOrchestrator orch(*context_.get_impl(), context_.get_impl()->get_arena());
            auto result = orch.compile_mir_function(optimized_stream_, mf);
            if (!result.success) return false;
            return emit_mir_binary(output_path, mf);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool emit_mir_object(std::string_view output_path, const machine::MachineFunction& fn) noexcept {
        try {
            dgtl::backend::aot::DGTLAOTOrchestrator orchestrator;
            return orchestrator.compile_and_emit(output_path, fn, fn.name());
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool emit_mir_binary(std::string_view output_path, const machine::MachineFunction& fn) noexcept {
        return emit_mir_object(output_path, fn);
    }

    [[nodiscard]] bool emit_object_file(std::string_view output_path, const dgtl::object::ObjectFile& obj) noexcept {
        try {
            dgtl::backend::aot::DGTLAOTOrchestrator orchestrator;
            return orchestrator.compile_and_emit(output_path, obj);
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool compile_module(std::vector<std::pair<std::string, ir::IRStream>>& module_streams) noexcept {
        dgtl::runtime::TLSFlushMerge flusher;
        for (auto& [name, stream] : module_streams) {
            flusher.merge_concurrent(stream);
            if (pipeline_.execute(stream) != dgtl::pipeline::AOTStatus::Success) return false;
        }
        if (!module_streams.empty()) {
            return compile(module_streams[0].second);
        }
        return true;
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

    [[nodiscard]] dgtl::pipeline::AOT_Pipeline& get_pipeline() noexcept { return pipeline_; }

private:
    Context& context_;
    dgtl::pipeline::AOT_Pipeline pipeline_;
    ir::IRStream optimized_stream_;
    std::unique_ptr<machine::MachineFunction> cached_mir_function_;
    std::unique_ptr<dgtl::object::ObjectFile> cached_object_file_;
    std::unordered_map<std::string, uint64_t> host_symbols_;
    std::unordered_map<uint32_t, uint64_t> host_symbol_slots_;

    AOTSession(const AOTSession&) = delete;
    AOTSession& operator=(const AOTSession&) = delete;
};

} // namespace api
} // namespace dgtl
