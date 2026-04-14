/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 30: CYCLE-ACCURATE HARDWARE PORT SCHEDULER
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 */

#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::jit {

struct CpuPorts {
    int8_t alu_slots = 4;    // Port 0, 1, 5, 6 on x86_64
    int8_t vec_slots = 2;    // Port 0, 1
    int8_t mem_slots = 2;    // Port 2, 3 (Load/Store)
    
    void reset() noexcept { alu_slots = 4; vec_slots = 2; mem_slots = 2; }
};

class DGTLHardwareScheduler {
    CpuPorts current_cycle_resources;
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    explicit DGTLHardwareScheduler(dgtl::analysis::DGTLAICostModel& ai) : ai_model(ai) {}

    /**
     * @brief Port-Aware Interleaving Strike.
     * Uses Shadow-Writing to ensure zero-corruption during reordering.
     */
    void schedule(ir::IRStream& stream) {
        const size_t count = stream.size();
        if (count == 0) [[unlikely]] return;

        // PILLAR 2: Open Transactional Shadow Stream via Strike
        stream.begin_transaction();

        std::vector<ir::IRNode> ready_queue;
        uint32_t scheduled_count = 0;
        uint32_t lookahead_ptr = 0;

        while (scheduled_count < static_cast<uint32_t>(count)) {
            current_cycle_resources.reset();

            // 1. Fill ready queue (Lookahead Window)
            // Using get_node as it is the standard accessor for the stream
            while (ready_queue.size() < 16 && lookahead_ptr < static_cast<uint32_t>(count)) {
                ready_queue.push_back(stream.get_node(lookahead_ptr++));
            }
            
            // 2. Sort by AI-Predicted Latency/Benefit to prioritize critical path
            std::sort(ready_queue.begin(), ready_queue.end(), [](const ir::IRNode& a, const ir::IRNode& b) {
                return a.predict_inlined_benefit() > b.predict_inlined_benefit();
            });

            // 3. Dispatch to Shadow Buffer
            for (auto it = ready_queue.begin(); it != ready_queue.end(); ) {
                if (can_dispatch(*it)) {
                    consume_resources(*it);
                    
                    // push_back automatically targets the shadow buffer during a strike
                    stream.push_back(*it);
                    
                    scheduled_count++;
                    it = ready_queue.erase(it);
                } else {
                    ++it;
                }
            }

            // Stall-Breaker: Force-advance if ports are saturated to prevent deadlocks
            if (!ready_queue.empty() && current_cycle_resources.alu_slots == 4) {
                stream.push_back(ready_queue[0]);
                ready_queue.erase(ready_queue.begin());
                scheduled_count++;
            }
        }

        // PILLAR 3: THE SENTINEL
        // Verify that the new hardware-optimized order hasn't broken SSA invariants
        // Updated to use the formal bit-consistency verification logic across the shadow stream
        bool consistent = true;
        for (uint32_t j = 0; j < stream.size(); ++j) {
            // Re-routing internal verification to the shadow proxy method
            if (!stream.validate_shadow_integrity()) {
                consistent = false;
                break;
            }
        }

        if (consistent) {
            stream.commit_transaction(); // Swap Shadow to Primary
        } else {
            // Transaction Failed: Discard Shadow, keep Original sequence
            stream.rollback_transaction();
        }
    }

private:
    [[nodiscard]] bool can_dispatch(const ir::IRNode& node) const noexcept {
        uint32_t f = static_cast<uint32_t>(node.flags);
        
        if (f & static_cast<uint32_t>(ir::Flags::REQUIRES_VEC_PORT)) {
            return current_cycle_resources.vec_slots > 0;
        }
        
        if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE || 
            node.opcode == ir::OpCode::GATHER_F32_512 || node.opcode == ir::OpCode::STORE_V512) {
            return current_cycle_resources.mem_slots > 0;
        }
        
        return current_cycle_resources.alu_slots > 0;
    }

    void consume_resources(const ir::IRNode& node) noexcept {
        uint32_t f = static_cast<uint32_t>(node.flags);

        if (f & static_cast<uint32_t>(ir::Flags::REQUIRES_VEC_PORT)) {
            current_cycle_resources.vec_slots--;
        } else if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE) {
            current_cycle_resources.mem_slots--;
        } else {
            current_cycle_resources.alu_slots--;
        }
    }
};

} // namespace dgtl::jit


