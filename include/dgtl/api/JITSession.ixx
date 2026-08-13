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
#include <cstdio>

// FIXED: Root name corrected to attach to your primary API compilation graph
export module dgtl.internal.api_jit_session;

// Import external monolithic module architectures natively
import dgtl.ir_core;
import dgtl.numa_runtime;
import dgtl.compiler_context;
import dgtl.dgtl_bulletins;
import dgtl.ir_core.stream;
import dgtl.ir_core.pass_manager_orchestrator;
import dgtl.ir_core.jit_orchestrator;
import dgtl.ir_core.runtime;
import dgtl.ir_core.code_cache;
// import dgtl.ir_core.execution;
import dgtl.flat_map; 
import dgtl.sync;     

// Import local base class engine definition safely via its sibling API partition shortcut
import dgtl.internal.api_execution_engine;

export namespace dgtl {
namespace api {

/**
 * @class JITSession
 * @brief Just-In-Time execution engine.
 * Optimized for low-latency on-the-fly compilation of hot paths.
 * Suitable for HFT and latency-critical workloads where compilation
 * must complete in microseconds.
 */
class alignas(64) JITSession final : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    /**
     * @brief Create a JIT session for the given context.
     * @param context The execution context
     */
    explicit JITSession(Context& context) noexcept
        : context_(context),
          pipeline_(),
          orchestrator_(*context.get_impl(), context.get_impl()->get_arena()),
          code_cache_(16 * 1024 * 1024) {
        (void)pipeline_.initialize();
    }

    ~JITSession() override = default;

    /**
     * @brief Compile IRStream with JIT optimizations and execute entry point.
     * @param stream The IR to compile
     * @return true if compilation and execution succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override {
        std::cout << "[DGTL-JIT-CHECKPOINT] J1. Entering JITSession::compile" << std::endl;
        if (stream.is_null() || stream.size() == 0) return false;

        std::cout << "[DGTL-JIT-CHECKPOINT] J2. Calling flusher.merge_concurrent" << std::endl;
        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        std::cout << "[DGTL-JIT-CHECKPOINT] J3. Checking cache logic hash" << std::endl;
        uint64_t logic_hash = stream.generate_logic_hash();
        std::string key = std::to_string(logic_hash);

        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            if (symbol_map_.find(key) != nullptr) {
                std::cout << "[DGTL-JIT-CHECKPOINT] J3a. Logic hash found in cache" << std::endl;
                return true;
            }
        }

        std::cout << "[DGTL-JIT-CHECKPOINT] J4. Execating optimization pipeline" << std::endl;
        if (pipeline_.execute(stream) != dgtl::pipeline::JITStatus::Success) {
            std::cout << "[DGTL-JIT-CHECKPOINT] J4-FAIL. Pipeline execution failed" << std::endl;
            return false;
        }

        std::cout << "[DGTL-JIT-CHECKPOINT] J5. Compiling stream to address (StrikeOrchestrator)" << std::endl;
        uintptr_t addr = orchestrator_.compile_to_address(stream);
        std::cout << "[DGTL-JIT-CHECKPOINT] J5a. Obtained address: " << addr << std::endl;
        if (addr == 0) return false;

        std::cout << "[DGTL-JIT-CHECKPOINT] J6. Caching raw machine code" << std::endl;
        const void* code_ptr = reinterpret_cast<const void*>(addr);
        size_t code_size = orchestrator_.get_last_code_size();
        (void)code_cache_.store(key, code_ptr, code_size);

        std::cout << "[DGTL-JIT-CHECKPOINT] J7. Mapping symbols" << std::endl;
        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            symbol_map_.insert_or_assign(key, addr);

            for (size_t i = 0; i < stream.size(); ++i) {
                const auto node = stream.get_node_readonly(i);
                if (node.opcode == ir::OpCode::LABEL) {
                    uint32_t label_id = node.result_id != ir::INVALID_SLOT ? node.result_id : node.op_a;
                    size_t offset = orchestrator_.get_label_offset(label_id);
                    uintptr_t label_addr = addr + offset;

                    std::string_view name = dgtl::runtime::try_lookup_string(label_id);
                    if (!name.empty()) {
                        symbol_map_.insert_or_assign(std::string(name), label_addr);
                        if (name == "DGTL_kernel_entry" || name == "main") {
                            symbol_map_.insert_or_assign("DGTL_kernel_entry", label_addr);
                            symbol_map_.insert_or_assign("main", label_addr);
                        }
                    }
                }
            }

            if (symbol_map_.find("DGTL_kernel_entry") == nullptr) {
                symbol_map_.insert_or_assign("DGTL_kernel_entry", addr);
                symbol_map_.insert_or_assign("main", addr);
            }
        }

        std::cout << "[DGTL-JIT-CHECKPOINT] J7-SUCCESS. JIT compile complete" << std::endl;
        std::cout.flush();

        return true;
    }

    /**
     * @brief Resolve a symbol by name from JIT code cache.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override {
        if (!symbol_name) return 0;
        std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
        auto* addr = symbol_map_.find(symbol_name);
        if (addr) {
            return *addr;
        }
        return orchestrator_.lookup_global_symbol(symbol_name);
    }

    void register_host_symbol(const std::string& symbol_name, uint64_t address) noexcept override {
        orchestrator_.register_runtime_function(symbol_name, static_cast<uintptr_t>(address));
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) noexcept override {
        orchestrator_.register_runtime_function_slot(slot, static_cast<uintptr_t>(address));
    }

    /**
     * @brief Multi-Module JIT Linking to resolve cross-function references.
     */
    [[nodiscard]] bool compile_module(std::vector<std::pair<std::string, ir::IRStream>>& module_streams) noexcept {
        dgtl::runtime::TLSFlushMerge flusher;
        for (auto& [name, stream] : module_streams) {
            flusher.merge_concurrent(stream);
            (void)pipeline_.execute(stream);
        }

        auto metrics = orchestrator_.fire_module_strike(module_streams);
        if (metrics.status_code != dgtl::jit::StrikeOrchestrator::STRIKE_OK) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        for (const auto& [name, stream] : module_streams) {
            uintptr_t addr = orchestrator_.lookup_global_symbol(name);
            if (addr != 0) {
                symbol_map_.insert_or_assign(name, addr);
            }
            for (size_t i = 0; i < stream.size(); ++i) {
                const auto node = stream.get_node_readonly(i);
                if (node.opcode == ir::OpCode::LABEL) {
                    std::string_view label_name = dgtl::runtime::try_lookup_string(node.result_id);
                    if (!label_name.empty()) {
                        uintptr_t label_addr = orchestrator_.lookup_global_symbol(std::string(label_name));
                        if (label_addr != 0) {
                            symbol_map_.insert_or_assign(std::string(label_name), label_addr);
                        }
                    }
                }
            }
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

    [[nodiscard]] dgtl::pipeline::JIT_Pipeline& get_pipeline() noexcept { return pipeline_; }
    [[nodiscard]] const dgtl::pipeline::JIT_Pipeline& get_pipeline() const noexcept { return pipeline_; }

private:
    Context& context_;
    dgtl::pipeline::JIT_Pipeline pipeline_;
    dgtl::jit::StrikeOrchestrator orchestrator_;
    dgtl::execution::cache::Code_Cache code_cache_;

    mutable std::shared_mutex symbol_mutex_;
    dgtl::utils::FastFlatMap<std::string, uint64_t, 512> symbol_map_;

    JITSession(const JITSession&) = delete;
    JITSession& operator=(const JITSession&) = delete;
};

} // namespace api
} // namespace dgtl