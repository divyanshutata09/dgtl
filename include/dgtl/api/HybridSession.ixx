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

export module dgtl.internal.api_hybrid_session;

// Import external monolithic module architectures natively
import dgtl.ir_core;
import dgtl.numa_runtime;
import dgtl.compiler_context;
import dgtl.dgtl_bulletins;
import dgtl.ir_core.pass_manager_orchestrator;
import dgtl.ir_core.jit_orchestrator;
import dgtl.ir_core.runtime;
import dgtl.flat_map; 
import dgtl.sync;     

// Import local base class definition safely via its sibling API partition shortcut
import dgtl.internal.api_execution_engine;

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
    explicit HybridSession(Context& context) noexcept
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
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override {
        std::cout << "[DGTL-HYBRID-CHECKPOINT] H1. Entering HybridSession::compile" << std::endl;
        if (stream.is_null()) return false;

        std::cout << "[DGTL-HYBRID-CHECKPOINT] H2. Calling flusher.merge_concurrent" << std::endl;
        dgtl::runtime::TLSFlushMerge flusher;
        flusher.merge_concurrent(stream);

        std::cout << "[DGTL-HYBRID-CHECKPOINT] H3. Executing hybrid optimization pipeline" << std::endl;
        auto status = pipeline_.execute(stream);
        if (status != dgtl::pipeline::HybridStatus::Success) {
            std::cout << "[DGTL-HYBRID-CHECKPOINT] H3-FAIL. Hybrid pipeline execution failed" << std::endl;
            return false;
        }

        std::cout << "[DGTL-HYBRID-CHECKPOINT] H4. Checking execution tier" << std::endl;
        if (pipeline_.get_current_tier() == dgtl::pipeline::ExecutionTier::JIT_Fast) {
            std::cout << "[DGTL-HYBRID-CHECKPOINT] H5. Tier is JIT_Fast. Compiling stream to address (StrikeOrchestrator)" << std::endl;
            uintptr_t addr = orchestrator_.compile_to_address(stream);
            std::cout << "[DGTL-HYBRID-CHECKPOINT] H5a. Obtained address: " << addr << std::endl;
            if (addr != 0) {
                std::unique_lock<dgtl::utils::sync::FastSharedSpinLock> lock(symbol_mutex_);
                
                uint64_t logic_hash = stream.generate_logic_hash();
                symbol_map_.insert_or_assign(std::to_string(logic_hash), addr);

                std::cout << "[DGTL-HYBRID-CHECKPOINT] H6. Mapping symbols" << std::endl;
                for (size_t i = 0; i < stream.size(); ++i) {
                    const auto node = stream.get_node_readonly(i);
                    if (node.opcode == ir::OpCode::LABEL) {
                        std::string_view name = dgtl::runtime::try_lookup_string(node.result_id);
                        if (!name.empty()) {
                            symbol_map_.insert_or_assign(std::string(name), addr);
                        }
                    }
                }
            }
        } else {
            std::cout << "[DGTL-HYBRID-CHECKPOINT] H4a. Tier is NOT JIT_Fast" << std::endl;
        }
        std::cout << "[DGTL-HYBRID-CHECKPOINT] H6-SUCCESS. Hybrid compile complete" << std::endl;
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

    void register_host_symbol(const std::string& symbol_name, uint64_t address) noexcept override {
        orchestrator_.register_runtime_function(symbol_name, static_cast<uintptr_t>(address));
    }

    void register_host_symbol_slot(uint32_t slot, uint64_t address) noexcept override {
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