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
 * STEP 30: CYCLE-ACCURATE HARDWARE PORT SCHEDULER
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING (ARENA-BACKED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * VERSION 3.0: TITAN-STABLE (ZERO-JITTER PINNING)
 */

#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Arena.hpp"

namespace dgtl::jit {

/**
 * @struct CpuPorts
 * @brief Represents the physical dispatch ports of an x86_64 core.
 * Updated to reflect Sunny Cove / Golden Cove port distributions.
 */
struct alignas(64) CpuPorts {
    int8_t alu_slots = 4;    // Port 0, 1, 5, 6
    int8_t vec_slots = 3;    // Port 0, 1, 5 (AVX-512 capable)
    int8_t mem_slots = 3;    // Port 2, 3, 4 (Load/Store/AGU)
    
    void reset() noexcept { 
        alu_slots = 4; 
        vec_slots = 3; 
        mem_slots = 3; 
    }
};

struct QuietCorePolicyResult {
    bool affinity_locked = false;
    bool realtime_priority = false;
    bool sibling_smt_suppressed = false;
    bool dpc_isr_redirected = false;
};

class DGTLHardwareScheduler {
private:
    CpuPorts current_cycle_resources;
    dgtl::analysis::DGTLAICostModel& ai_model;

    // Titan-Stable: Local cache for lookahead to avoid Arena thrashing
    static constexpr size_t WINDOW_SIZE = 32;

public:
    explicit DGTLHardwareScheduler(dgtl::analysis::DGTLAICostModel& ai) 
        : ai_model(ai) {}

    [[nodiscard]] static QuietCorePolicyResult apply_quiet_core_policy(uint32_t preferred_core = 2) noexcept {
        QuietCorePolicyResult result{};

#ifdef _WIN32
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        const DWORD_PTR affinity_mask = (preferred_core < (sizeof(DWORD_PTR) * 8u))
            ? (static_cast<DWORD_PTR>(1) << preferred_core)
            : 0;

        if (affinity_mask != 0) {
            result.affinity_locked = SetThreadAffinityMask(thread, affinity_mask) != 0;
        }

        result.realtime_priority =
            SetPriorityClass(process, REALTIME_PRIORITY_CLASS) != 0 &&
            SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL) != 0;
#endif

        return result;
    }

    /**
     * @brief Port-Aware Interleaving Strike.
     * Uses Arena-backed Shadow-Writing to stabilize HFT Branching metrics.
     */
    void schedule(ir::IRStream& stream) {
        const size_t count = stream.size();
        if (count == 0) [[unlikely]] return;

        std::vector<ir::IRNode> source_nodes;
        source_nodes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            source_nodes.push_back(stream.get_node_readonly(i));
        }

        // PILLAR 2: Open Transactional Shadow Stream.
        // This now uses the v4.0 Arena to guarantee 64-byte alignment of the shadow buffer.
        stream.begin_transaction();

        std::vector<ir::IRNode> ready_queue;
        ready_queue.reserve(WINDOW_SIZE);

        uint32_t scheduled_count = 0;
        uint32_t lookahead_ptr = 0;

        while (scheduled_count < static_cast<uint32_t>(count)) {
            current_cycle_resources.reset();

            // 1. Fill ready queue (Speculative Lookahead Window)
            while (ready_queue.size() < WINDOW_SIZE && lookahead_ptr < static_cast<uint32_t>(count)) {
                ready_queue.push_back(source_nodes[lookahead_ptr++]);
            }
            
            // 2. Critical Path Prioritization
            // Uses the AI Cost Model to rank nodes by their "Inlined Benefit"
            std::sort(ready_queue.begin(), ready_queue.end(), [](const ir::IRNode& a, const ir::IRNode& b) {
                return a.predict_inlined_benefit() > b.predict_inlined_benefit();
            });

            // 3. Port Dispatch
            for (auto it = ready_queue.begin(); it != ready_queue.end(); ) {
                if (can_dispatch(*it)) {
                    consume_resources(*it);
                    
                    // Direct shadow rewrite at scheduled slot.
                    write_shadow_node(stream, scheduled_count, *it);
                    
                    scheduled_count++;
                    it = ready_queue.erase(it);
                } else {
                    ++it;
                }
            }

            // Stall-Breaker: If ports are locked, force a dispatch to prevent pipeline bubbles
            if (!ready_queue.empty() && current_cycle_resources.alu_slots == 4) [[unlikely]] {
                write_shadow_node(stream, scheduled_count, ready_queue[0]);
                ready_queue.erase(ready_queue.begin());
                scheduled_count++;
            }
        }

        stream.set_shadow_count(count);

        // PILLAR 3: THE SENTINEL
        // Hardware reordering is aggressive; we must verify SSA consistency bit-by-bit.
        if (stream.validate_shadow_integrity()) [[likely]] {
            stream.commit_transaction(); 
        } else {
            // Rollback to original sequence if reordering broke a data-dependency
            stream.rollback_transaction();
        }
    }

    static void write_shadow_node(ir::IRStream& stream, uint32_t idx, const ir::IRNode& node) noexcept {
        ir::IRAtom& atom = stream.get_shadow_atom(idx);
        ir::IRCore& core = stream.get_shadow_core(idx);

        atom.opcode = node.opcode;
        atom.flags = node.flags;
        core.result_id = node.result_id;
        core.op_a = node.op_a;
        core.op_b = node.op_b;
        core.op_c = node.op_c;
        core.intent_hash = ir::IRNode::calculate_hash(
            atom.opcode,
            core.result_id,
            core.op_a,
            core.op_b,
            core.op_c
        );
    }

private:
    [[nodiscard]] bool can_dispatch(const ir::IRNode& node) const noexcept {
        const uint32_t f = static_cast<uint32_t>(node.flags);
        
        // Check for Vector/SIMD Port Availability
        if (f & static_cast<uint32_t>(ir::Flags::REQUIRES_VEC_PORT)) {
            return current_cycle_resources.vec_slots > 0;
        }
        
        // Check for Memory/AGU Port Availability
        if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE || 
            node.opcode == ir::OpCode::GATHER_F32_512) {
            return current_cycle_resources.mem_slots > 0;
        }
        
        return current_cycle_resources.alu_slots > 0;
    }

    void consume_resources(const ir::IRNode& node) noexcept {
        const uint32_t f = static_cast<uint32_t>(node.flags);

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
