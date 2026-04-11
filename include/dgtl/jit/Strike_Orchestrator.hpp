/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 46: STRIKE ORCHESTRATOR (THE COMMAND CENTER)
 * PILLAR 4: HARDWARE-NATIVE STRIKE
 * FINAL UPDATE: Fully Synchronized Namespaces & Type Resolution
 */

#pragma once
#include <vector>
#include <cstdint>

// Core Infrastructure
#include "core/Context.hpp"
#include "../ir/Global_IR.hpp" // No path prefix needed if both are in core/

// Cross-Module includes (using relative paths from include/core/)
#include "../isa/Stack_Spiller.hpp"
#include "../isa/X64_Strike_Emitter.hpp" 
#include "exec/Strike_Runner.hpp"

namespace dgtl::core {

/**
 * @class StrikeOrchestrator
 * @brief Coordinates the transition from IRStream to high-frequency execution.
 * * Orchestrates the four-stage "Fire" Strike:
 * 1. Optimization (Spill Surgery via StackSpiller)
 * 2. Emission (BitPacked IR to x64 Machine Code)
 * 3. Injection (Huge-Page RWX Mapping)
 * 4. Execution (The JIT Jump/Kernel Strike)
 */
class StrikeOrchestrator {
    DGTLContext& ctx;
    DGTLArena& arena;
    opt::StackSpiller spiller;
    
    // Emitter resides in the dgtl::exec namespace for backend-specific logic
    dgtl::exec::X64StrikeEmitter emitter;

    // PRE-ALLOCATED SCRATCH PAD: 16MB Initial Reserve
    // Used to build the machine code before injecting into the executable Arena.
    std::vector<uint8_t> machine_code_buffer;

public:
    StrikeOrchestrator(DGTLContext& c, DGTLArena& a) 
        : ctx(c), arena(a) {
        // Reserve 16MB to handle typical high-performance kernels without reallocation
        machine_code_buffer.reserve(1024 * 1024 * 16);
    }

    /**
     * @brief The "Fire" Strike.
     * Transitions IR from the stream into a live execution result.
     * PILLAR 4: Single-pass pipeline with zero mid-strike heap allocation.
     * * @param stream The BitPacked IR stream to be compiled.
     * @param data_payload Pointer to SVM or context data for the kernel.
     * @return StrikeMetrics containing execution time and result codes.
     */
    dgtl::exec::StrikeMetrics fire_strike(ir::IRStream& stream, void* data_payload = nullptr) {
        // Validation using BitPacked_IR.hpp interface
        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            return dgtl::exec::StrikeMetrics{0, 0xFFFFFFFF}; 
        }

        // 1. OPTIMIZATION PHASE: Solve Register Constraints
        // Uses the Transactional Strike (Pillar 2) if spills are needed.
        spiller.execute(stream, arena);

        // 2. EMISSION PHASE: IR -> Machine Code
        machine_code_buffer.clear(); 
        emitter.process_stream(stream, machine_code_buffer);

        // 3. INJECTION PHASE: Move to Huge-Page Aligned Arena
        // Copies the scratch buffer into the executable memory segment.
        size_t injected_size = dgtl::exec::StrikeRunner::inject_code(arena, machine_code_buffer);

        // 4. EXECUTION PHASE: The JIT Jump
        // Flushes Instruction Cache (I-Cache) and executes the generated code.
        return dgtl::exec::StrikeRunner::execute(arena, injected_size, data_payload);
    }

    /**
     * @brief Hard Reset: Recycles the arena and stabilizes the scratch buffer.
     * Prevents memory fragmentation if a previous strike was unusually large.
     */
    void hard_reset() {
        arena.reset();
        spiller.reset();
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
        return spiller.get_frame_size();
    }
};

} // namespace dgtl::core
