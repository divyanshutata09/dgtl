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

export module dgtl.api.hybrid_session;

// Import external monolithic module architectures natively
import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.runtime.bulletins;
import dgtl.ir.pipeline.pass_manager_orchestrator;
import dgtl.jit.jit_orchestrator;
import dgtl.runtime.tls;
import dgtl.flat_map; 
import dgtl.sync;     
import dgtl.mir;
import dgtl.mir.machine_ir;

// Import local base class definition safely via its sibling API partition shortcut
import dgtl.api.execution_engine;
import dgtl.formal;

export namespace dgtl {
namespace api {

/**
 * @class HybridSession
 * @brief Adaptive tiered execution engine.
 * Dynamically selects between JIT and AOT based on runtime characteristics
 * of code being compiled. JIT handles cold paths with minimal latency,
 * while AOT handles hot loops with maximum optimization intensity.
 * Intelligent tier switching optimizes for the best of both worlds.
 */
class alignas(64) HybridSession final : public ExecutionEngine {
public:
    using ExecutionEngine::get_symbol_address;

    /**
     * @brief Create a Hybrid session for the given context.
     * @param context The execution context
     */
    explicit HybridSession(Context& context)
        : context_(context),
          pipeline_(),
          orchestrator_(*context.get_impl(), context.get_impl()->get_arena()) {
        (void)pipeline_.initialize();
    }

    ~HybridSession() override = default;

    /**
     * @brief Compile IRStream with adaptive tier selection.
     * @param stream The IR to compile
     * @return true if compilation succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) override {

        if (stream.is_null()) return false;

        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        auto status = pipeline_.execute(stream);
        if (status != dgtl::pipeline::HybridStatus::Success) {

            return false;
        }

        if (pipeline_.get_current_tier() == dgtl::pipeline::ExecutionTier::JIT_Fast) {

            uintptr_t addr = orchestrator_.compile_to_address(stream);

            if (addr != 0) {
                std::unique_lock<dgtl::utils::sync::FastSharedSpinLock> lock(symbol_mutex_);
                
                uint64_t logic_hash = stream.generate_logic_hash();
                symbol_map_.insert_or_assign(std::to_string(logic_hash), addr);

                for (size_t i = 0; i < stream.size(); ++i) {
                    const auto node = stream.get_node_readonly(i);
                    if (node.opcode == ir::OpCode::LABEL) {
                        std::string_view name = dgtl::runtime::try_lookup_string(node.result_id);
                        if (!name.empty()) {
                            const uint32_t label_id = node.result_id != ir::INVALID_SLOT ? node.result_id : node.op_a;
                            const size_t offset = orchestrator_.get_label_offset(label_id);
                            symbol_map_.insert_or_assign(std::string(name), addr + offset);
                        }
                    }
                }
            }
            if (symbol_map_.find("DGTL_kernel_entry") == nullptr) {
                symbol_map_.insert_or_assign("DGTL_kernel_entry", addr);
                symbol_map_.insert_or_assign("main", addr);
            }
        } else {

        }

        return true;
    }

    /**
     * @brief Sovereign MIR MachineFunction compilation.
     */
    [[nodiscard]] bool compile_mir(const machine::MachineFunction& fn) override {
        uintptr_t addr = orchestrator_.compile_mir_to_address(fn);
        if (addr == 0) return false;
        std::unique_lock<dgtl::utils::sync::FastSharedSpinLock> lock(symbol_mutex_);
        const std::string name_key = fn.name().empty()
            ? ("dgtl_mf_" + std::to_string(reinterpret_cast<uintptr_t>(&fn)))
            : std::string(fn.name());
        symbol_map_.insert_or_assign(name_key, addr);
        symbol_map_.insert_or_assign("DGTL_kernel_entry", addr);
        symbol_map_.insert_or_assign("main", addr);
        return true;
    }

    /**
     * @brief Resolve a symbol by name from compiled code.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override {
        if (!symbol_name) return 0;
        
        std::shared_lock<dgtl::utils::sync::FastSharedSpinLock> lock(symbol_mutex_);
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
     * @brief Toggle execution tier preference.
     * @param prefer_jit If true, prefer JIT for next compilations. If false, prefer AOT.
     */
    void toggle_mode(bool prefer_jit) noexcept {
        pipeline_.set_tier(prefer_jit ? dgtl::pipeline::ExecutionTier::JIT_Fast : dgtl::pipeline::ExecutionTier::AOT_Intensive);
    }

    /**
     * @brief Get current execution tier.
     * @return The active tier (JIT_Fast, AOT_Intensive, or Adaptive)
     */
    [[nodiscard]] dgtl::pipeline::ExecutionTier get_current_tier() const noexcept {
        return pipeline_.get_current_tier();
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

    [[nodiscard]] dgtl::pipeline::Hybrid_Pipeline& get_pipeline() noexcept { return pipeline_; }
    [[nodiscard]] const dgtl::pipeline::Hybrid_Pipeline& get_pipeline() const noexcept { return pipeline_; }

private:
    Context& context_;
    dgtl::pipeline::Hybrid_Pipeline pipeline_;
    dgtl::jit::StrikeOrchestrator orchestrator_;

    mutable dgtl::utils::sync::FastSharedSpinLock symbol_mutex_;
    dgtl::utils::FastFlatMap<std::string, uint64_t, 512> symbol_map_;

    HybridSession(const HybridSession&) = delete;
    HybridSession& operator=(const HybridSession&) = delete;
};

} // namespace api
} // namespace dgtl
