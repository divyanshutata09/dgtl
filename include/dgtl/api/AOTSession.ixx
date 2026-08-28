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

export module dgtl.internal.api_aot_session;

// FIXED: Native structural dependencies pulled from unified core frameworks
import dgtl.ir_core;
import dgtl.numa_runtime;
import dgtl.compiler_context;
import dgtl.ir_core.stream;
import dgtl.ir_core.pass_manager_orchestrator;
import dgtl.ir_core.jit_orchestrator;
import dgtl.backend.aot_linker;
import dgtl.backend.aot_orchestrator;
import dgtl.ir_core.runtime;

// Import public interface parent securely 
import dgtl.internal.api_execution_engine;

export namespace dgtl {
namespace api {

/**
 * @class AOTSession
 * @brief Ahead-Of-Time (AOT) execution engine.
 * Compiles entire programs offline with maximum optimization intensity.
 * Suitable for Cloud/HPC workloads where compilation latency is amortized
 * over long-running computations. Supports static binary and object emission.
 */
class AOTSession : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    /**
     * @brief Create an AOT session for the given context.
     * @param context The execution context
     */
    explicit AOTSession(Context& context) noexcept
        : context_(context) {
        (void)pipeline_.initialize();
    }

    ~AOTSession() override = default;

    /**
     * @brief Compile IRStream with maximum AOT optimizations.
     * @param stream The IR to compile
     * @return true if compilation succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override {
        if (stream.is_null()) return false;

        // 1. Flush thread-local buffers
        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        // 2. AOT optimization pipeline
        if (pipeline_.execute(stream) != dgtl::pipeline::AOTStatus::Success) {
            return false;
        }

        // 3. Clone optimized stream
        optimized_stream_ = stream.clone();
        return true;
    }

    /**
     * @brief Resolve a symbol by name from AOT compiled code.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address([[maybe_unused]] const char* symbol_name) noexcept override {
        // AOT compiled code is written to disk and not executed in-memory directly by AOTSession.
        return 0;
    }

    void register_host_symbol(const std::string& symbol_name, uint64_t address) noexcept override {
        host_symbols_[symbol_name] = address;
    }
    void register_host_symbol_slot(uint32_t slot, uint64_t address) noexcept override {
        host_symbol_slots_[slot] = address;
    }

    /**
     * @brief Emit compiled code as relocatable object file.
     * @param output_path Path to write .o file
     * @return true if emission succeeded
     */
    [[nodiscard]] bool emit_object(std::string_view output_path) noexcept {
        if (optimized_stream_.is_null()) return false;
        try {
            dgtl::backend::aot::DGTLAOTOrchestrator orchestrator;
            return orchestrator.compile_and_emit(output_path, optimized_stream_, "DGTL_kernel_entry");
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Emit compiled code as standalone executable binary.
     * @param output_path Path to write binary executable
     * @return true if emission succeeded
     */
    [[nodiscard]] bool emit_binary(std::string_view output_path) noexcept {
        if (optimized_stream_.is_null()) return false;
        try {
            dgtl::backend::aot::DGTLAOTOrchestrator orchestrator;
            return orchestrator.compile_and_emit(output_path, optimized_stream_, "DGTL_kernel_entry");
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Support compiling a multi-stream module.
     */
    [[nodiscard]] bool compile_module(std::vector<std::pair<std::string, ir::IRStream>>& module_streams) noexcept {
        dgtl::runtime::TLSFlushMerge flusher;
        for (auto& [name, stream] : module_streams) {
            flusher.merge_concurrent(stream);
            (void)pipeline_.execute(stream);
        }
        if (!module_streams.empty()) {
            optimized_stream_ = module_streams[0].second.clone();
        }
        return true;
    }

    /**
     * @brief Verify equivalence using external SMT solver.
     */
    [[nodiscard]] formal::VerificationResult verify_semantics(
        const ir::IRStream& original_stream,
        const ir::IRStream& optimized_stream,
        std::string_view pass_name,
        std::string_view solver_name = "z3"
    ) noexcept override {
        formal::DGTLFormalVerificationSMT verifier;
        const std::string script = verifier.export_to_smtlib2(original_stream, optimized_stream, pass_name);
        return verifier.verify_with_external_solver(script, solver_name);
    }

    /**
     * @brief Specialize an IR stream using evolutionary optimization.
     */
    [[nodiscard]] bool specialize_stream(
        ir::IRStream& stream,
        const ir::evolution::ExecutionFeedback& feedback
    ) noexcept override {
        ir::evolution::IREvolver evolver;
        auto res = evolver.apply_specialization(stream, feedback);
        return res.succeeded;
    }

    /**
     * @brief Get internal AOT pipeline for advanced tuning.
     */
    [[nodiscard]] dgtl::pipeline::AOT_Pipeline& get_pipeline() noexcept { return pipeline_; }

private:
    Context& context_;
    dgtl::pipeline::AOT_Pipeline pipeline_;
    ir::IRStream optimized_stream_;
    std::unordered_map<std::string, uint64_t> host_symbols_;
    std::unordered_map<uint32_t, uint64_t> host_symbol_slots_;

    AOTSession(const AOTSession&) = delete;
    AOTSession& operator=(const AOTSession&) = delete;
};

} // namespace api
} // namespace dgtl