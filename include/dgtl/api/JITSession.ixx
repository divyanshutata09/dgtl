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

// Named module interface declaration
export module dgtl.api.jit_session;

// Import external monolithic module architectures natively
import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.runtime.bulletins;
import dgtl.ir.stream;
import dgtl.ir.pipeline.pass_manager_orchestrator;
import dgtl.jit.jit_orchestrator;
import dgtl.runtime.tls;
import dgtl.execution.cache;
import dgtl.execution.dispatcher;
import dgtl.flat_map; 
import dgtl.sync;     
import dgtl.mir;
import dgtl.mir.machine_ir;

// Import local base class engine definition safely via its sibling API partition shortcut
import dgtl.api.execution_engine;
import dgtl.formal;

export namespace dgtl {
namespace api {

/**
 * @class JITSession
 * @brief Just-In-Time execution engine.
 * Optimized for low-latency on-the-fly compilation of hot paths.
 * Suitable for HFT and latency-critical workloads where compilation
 * must complete in microseconds.
 *
 * ARCHITECTURAL INVARIANT:
 * One Authoritative MIR: dgtl::machine::MachineFunction is the sole source
 * of truth across target lowering and binary emission.
 */
class alignas(64) JITSession final : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    /**
     * @brief Create a JIT session for the given context.
     * @param context The execution context
     */
    explicit JITSession(Context& context)
        : context_(context),
          pipeline_(),
          orchestrator_(*context.get_impl(), context.get_impl()->get_arena()),
          code_cache_(16 * 1024 * 1024) {
        (void)pipeline_.initialize();
    }

    ~JITSession() override = default;

    /**
     * @brief Sovereign MachineFunction JIT Compilation.
     * dgtl::machine::MachineFunction is the authoritative execution and emission contract.
     *
     * @param function The verified MachineFunction to compile and load into executable memory
     * @return true if compilation, verification, and loading succeeded
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
            ? ("dgtl_mf_" + std::to_string(reinterpret_cast<uintptr_t>(&function)))
            : std::string(function.name());

        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            if (symbol_map_.find(name_key) != nullptr) {
                return true;
            }
        }

        uintptr_t addr = orchestrator_.compile_mir_to_address(function);
        if (addr == 0) {
            return false;
        }

        const void* code_ptr = reinterpret_cast<const void*>(addr);
        size_t code_size = orchestrator_.get_last_code_size();
        (void)code_cache_.store(name_key, code_ptr, code_size);

        {
            std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
            symbol_map_.insert_or_assign(name_key, addr);
            symbol_map_.insert_or_assign("DGTL_kernel_entry", addr);
            symbol_map_.insert_or_assign("main", addr);
        }

        return true;
    }

    /**
     * @brief Sovereign MIR MachineFunction compilation override from ExecutionEngine base.
     */
    [[nodiscard]] bool compile_mir(const machine::MachineFunction& fn) override {
        return compile(fn);
    }

    /**
     * @brief Compile IRStream with JIT optimizations by lowering to MachineFunction.
     * Enforces the architectural invariant: IRStream -> MachinePipeline -> MachineFunction -> Binary Emission.
     *
     * @param stream The IR to compile
     * @return true if compilation and execution succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() == 0) return false;

        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        uint64_t logic_hash = stream.generate_logic_hash();
        std::string key = std::to_string(logic_hash);

        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            if (symbol_map_.find(key) != nullptr) {
                return true;
            }
        }

        if (pipeline_.execute(stream) != dgtl::pipeline::JITStatus::Success) {
            return false;
        }

        dgtl::machine::MachineFunction mf;
        auto result = orchestrator_.compile_mir_function(stream, mf);
        if (!result.success) {
            return false;
        }

        if (!compile(mf)) {
            return false;
        }

        uintptr_t addr = 0;
        {
            std::shared_lock<std::shared_mutex> lock(symbol_mutex_);
            auto* p_addr = symbol_map_.find("DGTL_kernel_entry");
            if (p_addr) {
                addr = *p_addr;
            }
        }

        if (addr != 0) {
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
        }

        return true;
    }

    /**
     * @brief Compile an IRStream directly through the Unified MIR Machine Pipeline.
     * Provides target capability legalization, instruction selection, SSA Phi lowering,
     * two-address normalization, graph-coloring register allocation, frame lowering,
     * target ABI lowering, and formal Machine IR structural verification.
     *
     * @param stream The IRStream to lower into formal Machine IR
     * @param options Pipeline pass configuration
     * @return Pair containing the fully lowered/verified MachineFunction and pipeline result
     */
    [[nodiscard]] std::pair<dgtl::machine::MachineFunction, dgtl::mir::MachinePipelineResult> compile_mir(
        const ir::IRStream& stream,
        const dgtl::mir::MachinePipelineOptions& options = {}) {
        dgtl::machine::MachineFunction mf;
        auto result = orchestrator_.compile_mir_function(stream, mf, options);
        return {std::move(mf), result};
    }

    /**
     * @brief Compile an authoritative MachineFunction directly into executable memory with verification.
     */
    [[nodiscard]] bool compile_with_mir(const dgtl::machine::MachineFunction& function) {
        return compile(function);
    }

    /**
     * @brief Compile an IRStream directly through the Unified MIR Machine Pipeline
     * and load into executable memory with full verification.
     */
    [[nodiscard]] bool compile_with_mir(ir::IRStream& stream) {
        return compile(stream);
    }

    /**
     * @brief Compile a MachineFunction using the JIT Tiered Compilation subsystem.
     *
     * @param function The MachineFunction to compile
     * @param queue_tier2 Whether to queue background Tier 2 optimization
     * @return Function ID for tiered execution dispatch
     */
    [[nodiscard]] uint64_t compile_tiered(const dgtl::machine::MachineFunction& function, bool queue_tier2 = true) {
        return orchestrator_.compile_tiered_function(function, queue_tier2);
    }

    /**
     * @brief Compile an IRStream using the JIT Tiered Compilation subsystem.
     * Starts with ultra-low latency Tier 1 baseline compilation, followed by asynchronous
     * background Tier 2 optimization and MIR verification.
     *
     * @param stream The IRStream to compile
     * @param queue_tier2 Whether to queue background Tier 2 optimization
     * @return Function ID for tiered execution dispatch
     */
    [[nodiscard]] uint64_t compile_tiered(const ir::IRStream& stream, bool queue_tier2 = true) {
        return orchestrator_.compile_tiered_function(stream, queue_tier2);
    }

    /**
     * @brief Retrieve the verified MachineFunction for an optimized tiered function if available.
     * @param function_id The tiered function identifier
     * @return Shared pointer to const MachineFunction, or nullptr if not yet ready
     */
    [[nodiscard]] std::shared_ptr<const dgtl::machine::MachineFunction> get_mir_function(uint64_t function_id) const {
        return orchestrator_.get_tiered_mir_function(function_id);
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

    void register_host_symbol(const std::string& symbol_name, uint64_t address) override {
        orchestrator_.register_runtime_function(symbol_name, static_cast<uintptr_t>(address));
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) override {
        orchestrator_.register_runtime_function_slot(slot, static_cast<uintptr_t>(address));
    }

    /**
     * @brief Multi-Module JIT Linking directly from authoritative MachineFunctions.
     */
    [[nodiscard]] bool compile_module(
        const std::vector<std::pair<std::string, const dgtl::machine::MachineFunction*>>& module_functions) noexcept {
        auto metrics = orchestrator_.fire_module_strike(module_functions);
        if (metrics.status_code != dgtl::jit::StrikeOrchestrator::STRIKE_OK) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(symbol_mutex_);
        for (const auto& [name, fn_ptr] : module_functions) {
            uintptr_t addr = orchestrator_.lookup_global_symbol(name);
            if (addr != 0) {
                symbol_map_.insert_or_assign(name, addr);
            }
        }
        return true;
    }

    /**
     * @brief Multi-Module JIT Linking to resolve cross-function references from IRStreams.
     * Lowers each stream to MachineFunction before module strike.
     */
    [[nodiscard]] bool compile_module(std::vector<std::pair<std::string, ir::IRStream>>& module_streams) noexcept {
        dgtl::runtime::TLSFlushMerge flusher;
        std::vector<dgtl::machine::MachineFunction> lowered_functions;
        lowered_functions.reserve(module_streams.size());

        for (auto& [name, stream] : module_streams) {
            flusher.merge_concurrent(stream);
            if (pipeline_.execute(stream) != dgtl::pipeline::JITStatus::Success) return false;

            dgtl::machine::MachineFunction mf;
            auto result = orchestrator_.compile_mir_function(stream, mf);
            if (!result.success) return false;
            lowered_functions.push_back(std::move(mf));
        }

        std::vector<std::pair<std::string, const dgtl::machine::MachineFunction*>> mir_modules;
        mir_modules.reserve(module_streams.size());
        for (size_t i = 0; i < module_streams.size(); ++i) {
            mir_modules.emplace_back(module_streams[i].first, &lowered_functions[i]);
        }

        return compile_module(mir_modules);
    }

    /**
     * @brief Verify equivalence using external SMT solver.
     */
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

    /**
     * @brief Specialize an IR stream using evolutionary optimization.
     */
    [[nodiscard]] bool specialize_stream(
        ir::IRStream& stream,
        const ir::evolution::ExecutionFeedback& feedback
    ) override {
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
