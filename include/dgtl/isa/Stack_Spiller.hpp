/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 20: STACK SPILLER & MEMORY PROMOTER (ZERO-CORRUPTION)
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED SPILL SURGERY)
 * PILLAR 3: THE SENTINEL (INVARIANT GUARD)
 * DESIGN: Handles register exhaustion by promoting Virtual IDs to stack slots.
 * STANDARDS: C++26 / UCRT64 / AVX-512 Validation
 */

#pragma once
#include <vector>
#include <iostream>
#include <cstdint>

// Corrected relative paths based on your project structure
#include "../ir/Global_IR.hpp"
#include "../core/Arena.hpp"

namespace dgtl::opt {

/**
 * @class StackSpiller
 * @brief Manages "Virtual Register" overflow by mapping ResultIDs to the physical stack.
 * Operates as a transformation pass on the IRStream.
 */
class alignas(64) StackSpiller {
    struct SpillSlot {
        uint32_t virtual_id;
        int32_t stack_offset;
    };

    std::vector<SpillSlot> m_spill_map;
    int32_t m_current_stack_depth = 0;

public:
    /**
     * @brief Executes stack spilling using Transactional IR.
     * PILLAR 2: Prevents "In-Place Surgery" corruption by using the Shadow Tier.
     * Logic: If -fno-exceptions is used, we use return-based error handling.
     */
    void execute(ir::IRStream& stream, [[maybe_unused]] dgtl::core::DGTLArena& arena) {
        // Validation: Ensure stream is valid and not empty
        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            return;
        }

        // PILLAR 2: Begin Transaction. Clones Primary -> Shadow Tier.
        stream.begin_strike();

        // Perform the "Surgery" directly on the Shadow Tier of the stream.
        process_shadow(stream);

        // PILLAR 3: THE SENTINEL (Bit-Level Invariant Check)
        // Validates that the shadow transformation didn't break opcode-operand logic.
        if (stream.validate_shadow_integrity()) [[likely]] {
            stream.commit_strike(); 
        } else {
            // Halt! Invariant broken. Original IR remains untouched.
            stream.rollback_strike();
            handle_spill_failure();
        }
    }

    /**
     * @brief Returns the total stack frame size required for the JIT function.
     * Ensures 16-byte alignment for modern ABI compliance (x86_64/AArch64).
     */
    [[nodiscard]] uint32_t get_frame_size() const noexcept {
        return (static_cast<uint32_t>(m_current_stack_depth) + 15) & ~15;
    }

    /**
     * @brief Resets the spiller state for a new compilation session.
     */
    void reset() noexcept {
        m_spill_map.clear();
        m_current_stack_depth = 0;
    }

private:
    /**
     * @brief High-speed surgery on the Shadow Tier.
     * Uses IRAtom and IRCore split accessors to avoid expensive IRNode reconstruction.
     */
    void process_shadow(ir::IRStream& s) {
        const size_t limit = s.size();
        
        for (size_t i = 0; i < limit; ++i) {
            ir::IRAtom& atom = s.get_shadow_atom(i);
            ir::IRCore& core = s.get_shadow_core(i);

            // 1. ALLOCATION: If marked as spill candidate (from RegAlloc)
            if (atom.flags & ir::Flags::IS_SPILL_CANDIDATE) {
                int32_t offset = allocate_spill_slot(core.result_id);
                
                // Hijack op_c for stack offset. Emitter uses SVE_ACTIVE to interpret.
                core.op_c = static_cast<uint32_t>(offset);
                atom.flags |= ir::Flags::SVE_ACTIVE; // Mark as hardware-mapped (to stack)
                
                // PILLAR 3: Refresh Hash to lock the new 'Stack-Resident' intent
                uint32_t op_val = static_cast<uint32_t>(static_cast<uint16_t>(atom.opcode));
                core.intent_hash = (op_val * 31u) ^ (core.op_a * 17u) ^ 
                                   (core.op_b * 7u) ^ (core.op_c * 3u);
            }

            // 2. OPERAND RESOLUTION: Check if inputs (op_a/op_b) reside on the stack
            check_and_reload(core.op_a, atom.flags);
            check_and_reload(core.op_b, atom.flags);
        }
    }

    /**
     * @brief Allocates or retrieves a stack slot for a Virtual ID.
     */
    int32_t allocate_spill_slot(uint32_t vid) {
        for (const auto& slot : m_spill_map) {
            if (slot.virtual_id == vid) [[unlikely]] return slot.stack_offset;
        }
        
        int32_t offset = m_current_stack_depth;
        m_spill_map.push_back({vid, offset});
        m_current_stack_depth += 8; // 64-bit slots for high-performance memory alignment
        return offset;
    }

    /**
     * @brief Checks if an operand is residing on the stack and updates flags for Emitter.
     */
    void check_and_reload(uint32_t operand, uint16_t& flags) noexcept {
        if (operand == ir::INVALID_SLOT) return; 

        for (const auto& slot : m_spill_map) {
            if (slot.virtual_id == operand) {
                // Operand is on stack; mark node for stack-relative LDR during emission
                flags |= ir::Flags::IS_SPILL_CANDIDATE; 
                break;
            }
        }
    }

    void handle_spill_failure() {
        std::cerr << "\033[1;31m[DGTL CRITICAL]\033[0m SPILL SURGERY FAILURE: Invariant check failed.\n";
        std::cerr << "[DGTL] Rollback triggered. Primary IR tier remains intact.\n";
    }
};

} // namespace dgtl::opt

/**
 * @namespace dgtl::opt
 * @brief Compatibility Alias for DGTL-Zenith high-level logic.
 */
namespace dgtl::opt {
    using StackSpiller = dgtl::opt::StackSpiller;
}
