/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 46: STRIKE ORCHESTRATOR (THE COMMAND CENTER)
 * PILLAR 4: HARDWARE-NATIVE STRIKE
 * FINAL UPDATE: Fully Synchronized Namespaces & Type Resolution
 */

#pragma once
#include <vector>
#include <cstdint>

// Core Infrastructure
#include "dgtl/core/Context.hpp"
#include "dgtl/ir/Global_IR.hpp"

// Cross-Module includes
#include "dgtl/isa/lowering/Stack_Spiller.hpp"
#include "dgtl/isa/emitter/X64_Strike_Emitter.hpp"
#include "dgtl/jit/orchestrator/Strike_Runner.hpp"

// Closed-loop learning components
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/jit/runtime/JIT_Executor.hpp"

namespace dgtl::core {

/**
 * @class StrikeOrchestrator
 * @brief Coordinates the transition from IRStream to high-frequency execution.
 * * Orchestrates the four-stage "Fire" Strike:
 * 1. Optimization (Spill Surgery via StackPiller)
 * 2. Emission (BitPacked IR to x64 Machine Code)
 * 3. Injection (Huge-Page RWX Mapping)
 * 4. Execution (The JIT Jump/Kernel Strike)
 */
class StrikeOrchestrator {
    DGTLContext& ctx;
    DGTL_Arena& arena;
    dgtl::isa::StackPiller spiller;
    
    // Emitter resides in the dgtl::isa namespace for backend-specific logic
    dgtl::isa::X64StrikeEmitter emitter;

    // Closed-loop learning components
    dgtl::analysis::DGTLAICostModel cost_model;
    dgtl::isa::DGTLJitExecutor executor;
    dgtl::exec::StrikeRunner strike_runner;

    // PRE-ALLOCATED SCRATCH PAD: 16MB Initial Reserve
    // Used to build the machine code before injecting into the executable Arena.
    std::vector<uint8_t> machine_code_buffer;

public:
    static constexpr uint32_t STRIKE_OK = static_cast<uint32_t>(dgtl::exec::StrikeStatus::Ok);
    static constexpr uint32_t STRIKE_INVALID_STREAM = 0xE001;
    static constexpr uint32_t STRIKE_EMIT_EMPTY = 0xE002;
    static constexpr uint32_t STRIKE_INJECT_FAILED = static_cast<uint32_t>(dgtl::exec::StrikeStatus::InjectFailed);

    StrikeOrchestrator(DGTLContext& c, DGTL_Arena& a) 
        : ctx(c), arena(a), spiller(arena) {
        // Reserve 16MB to handle typical high-performance kernels without reallocation
        machine_code_buffer.reserve(1024 * 1024 * 16);
    }

    /**
     * @brief The "Fire" Strike.
     * Transitions IR from the stream into a live execution result.
     * PILLAR 4: Single-opt pipeline with zero mid-strike heap allocation.
     * * @param stream The BitPacked IR stream to be compiled.
     * @param data_payload Pointer to SVM or context data for the kernel.
     * @return StrikeMetrics containing execution time and result codes.
     */
    dgtl::exec::StrikeMetrics fire_strike(ir::IRStream& stream, void* data_payload = nullptr) {
        // Validation using BitPacked_IR.hpp interface
        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            dgtl::exec::StrikeMetrics metrics{};
            metrics.status_code = STRIKE_INVALID_STREAM;
            return metrics;
        }

        // 1. OPTIMIZATION PHASE: Solve Register Constraints
        // Uses the Transactional Strike (Pillar 2) if spills are needed.
        // spiller.execute(stream, arena);

        // 2. EMISSION PHASE: IR -> Machine Code
        machine_code_buffer.clear(); 
        emitter.process_stream(stream, machine_code_buffer);
        if (machine_code_buffer.empty()) {
            dgtl::exec::StrikeMetrics metrics{};
            metrics.status_code = STRIKE_EMIT_EMPTY;
            return metrics;
        }

        // 3. INJECTION PHASE: Move to Huge-Page Aligned Arena
        // Copies the scratch buffer into the executable memory segment.
        size_t injected_size = strike_runner.inject_code(arena, machine_code_buffer);
        if (injected_size == 0) {
            dgtl::exec::StrikeMetrics metrics{};
            metrics.status_code = STRIKE_INJECT_FAILED;
            return metrics;
        }

        // 4. EXECUTION PHASE: The JIT Jump
        // Flushes Instruction Cache (I-Cache) and executes the generated code.
        auto metrics = strike_runner.execute(arena, injected_size, data_payload);

        // CLOSED-LOOP FEEDBACK: Collect runtime metrics and update cost model
        // This enables the AI cost model to learn from actual execution
        if (!stream.is_null() && stream.size() > 0) {
            // Use the injected code address as function ID.
            uintptr_t function_id = strike_runner.last_injected_address();
            if (function_id != 0) {
                auto feedback = executor.collect_feedback(function_id);

                // Update cost model for each IR node in the stream
                for (size_t i = 0; i < stream.size(); ++i) {
                    const auto node = stream.get_node_readonly(i);
                    cost_model.update_with_feedback(node, feedback);
                }
            }
        }

        if (metrics.status_code == static_cast<uint32_t>(dgtl::exec::StrikeStatus::Ok)) {
            metrics.status_code = STRIKE_OK;
        }

        return metrics;
    }

    /**
     * @brief Hard Reset: Recycles the arena and stabilizes the scratch buffer.
     * Prevents memory fragmentation if a previous strike was unusually large.
     */
    void hard_reset() {
        arena.reset_all();
        // spiller.reset_frame();
        machine_code_buffer.clear();
        
        // If the buffer grew beyond 32MB, shrink it back to baseline to save RAM
        if (machine_code_buffer.capacity() > (1024 * 1024 * 32)) {
            machine_code_buffer.shrink_to_fit();
            machine_code_buffer.reserve(1024 * 1024 * 16);
        }
    }

    // --- Telemetry & Diagnostics ---

    [[nodiscard]] size_t get_last_code_size() const noexcept { 
        return machine_code_buffer.size(); 
    }

    [[nodiscard]] uint32_t get_stack_frame_requirement() const noexcept {
        return 0; // spiller.get_frame_size();
    }
};

} // namespace dgtl::core



