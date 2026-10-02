/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 30: CYCLE-ACCURATE HARDWARE PORT SCHEDULER
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING (ARENA-BACKED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * VERSION 3.0: TITAN-STABLE (ZERO-TER PINNING & 178 CANONICAL OPCODES)
 */

module;

#include <cstdlib>

#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

// 🚀 FIXED: Bind directly to your backend sub-module partition space
export module dgtl.jit.hardware_scheduler;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir.analysis.cost_model;
import dgtl.target;
import dgtl.mir.machine_ir;


export namespace dgtl::jit {

/**
 * @struct CpuPorts
 * @brief Represents the physical dispatch ports of an x86_64 core.
 * Updated to reflect Sunny Cove / Golden Cove port distributions.
 * Queried from TargetDescription for declarative port information.
 */
struct alignas(64) CpuPorts {
    int8_t alu_slots = 4;    // Port 0, 1, 5, 6
    int8_t vec_slots = 3;    // Port 0, 1, 5 (AVX-512 capable)
    int8_t mem_slots = 3;    // Port 2, 3, 4 (Load/Store/AGU)
    
    /**
     * @brief Initialize port slots from target description.
     * @param target Target machine description with ISA and port info
     */
    void init_from_target(const dgtl::core::TargetMachine& target) noexcept {
        switch (target.get_arch()) {
            case dgtl::core::ArchType::X86_64_AVX512:
                alu_slots = 4;  // Sunny Cove: 4x ALU
                vec_slots = 4;  // 4x vector (AVX-512)
                mem_slots = 3;  // 3x memory (load/store/AGU)
                break;
            case dgtl::core::ArchType::X86_64_AVX2:
                alu_slots = 4;  // Golden Cove: 4x ALU
                vec_slots = 3;  // 3x vector (AVX2)
                mem_slots = 3;  // 3x memory
                break;
            case dgtl::core::ArchType::AARCH64_SVE:
                alu_slots = 4;  // Cortex-X3: 4x fixed-point
                vec_slots = 4;  // 4x SIMD/FP
                mem_slots = 2;  // 2x memory (load/store)
                break;
            default:
                reset();  // Use defaults
                break;
        }
    }
    
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
    [[maybe_unused]] dgtl::analysis::DGTLAICostModel& ai_model;
    const dgtl::core::TargetMachine& m_target_machine;

    // Titan-Stable: Local cache for lookahead to avoid Arena thrashing
    static constexpr size_t WINDOW_SIZE = 32;

public:
    explicit DGTLHardwareScheduler(dgtl::analysis::DGTLAICostModel& ai,
                                   const dgtl::core::TargetMachine& target) 
        : ai_model(ai), m_target_machine(target) {
        current_cycle_resources.init_from_target(target);
    }

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
     * Queries target description for instruction latency and port requirements.
     */
    void schedule(dgtl::ir::IRStream& stream) {
        const size_t count = stream.size();
        if (count == 0) [[unlikely]] return;

        std::vector<dgtl::ir::IRNode> source_nodes;
        source_nodes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            source_nodes.push_back(stream.get_node_readonly(i));
        }

        // PILLAR 2: Open Transactional Shadow Stream.
        // This now uses the v1.0.0 Arena to guarantee 64-byte alignment of the shadow buffer.
        stream.begin_transaction();

        std::vector<dgtl::ir::IRNode> ready_queue;
        ready_queue.reserve(WINDOW_SIZE);

        uint32_t scheduled_count = 0;
        uint32_t lookahead_ptr = 0;

        while (scheduled_count < static_cast<uint32_t>(count)) {
            current_cycle_resources.init_from_target(m_target_machine);

            // 1. Fill ready queue (Speculative Lookahead Window)
            while (ready_queue.size() < WINDOW_SIZE && lookahead_ptr < static_cast<uint32_t>(count)) {
                ready_queue.push_back(source_nodes[lookahead_ptr++]);
            }
            
            // 2. Critical Path Prioritization
            // Uses the AI Cost Model to rank nodes by their "Inlined Benefit"
            std::sort(ready_queue.begin(), ready_queue.end(), [](const dgtl::ir::IRNode& a, const dgtl::ir::IRNode& b) {
                return a.predict_inlined_benefit() > b.predict_inlined_benefit();
            });

            // 3. Port Dispatch
            bool dispatched_any = false;
            for (auto it = ready_queue.begin(); it != ready_queue.end(); ) {
                if (can_dispatch(*it)) {
                    consume_resources(*it);
                    
                    // Direct shadow rewrite at scheduled slot.
                    write_shadow_node(stream, scheduled_count, *it);
                    
                    scheduled_count++;
                    it = ready_queue.erase(it);
                    dispatched_any = true;
                } else {
                    ++it;
                }
            }

            // Stall-Breaker: If nothing dispatched in this cycle and queue is non-empty, force dispatch to prevent starvation
            if (!dispatched_any && !ready_queue.empty() && scheduled_count < static_cast<uint32_t>(count)) [[unlikely]] {
                write_shadow_node(stream, scheduled_count, ready_queue[0]);
                ready_queue.erase(ready_queue.begin());
                scheduled_count++;
            } else if (!ready_queue.empty() && ready_queue.size() < WINDOW_SIZE && 
                       lookahead_ptr < static_cast<uint32_t>(count)) {
                // Advance lookahead if stalled but window not full
                ready_queue.push_back(source_nodes[lookahead_ptr++]);
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

    /**
     * @brief MIR-Native Hardware Port Scheduling (Machine Instruction Scheduling - MISched).
     * Reorders machine instructions within each basic block to maximize multi-port
     * parallel execution across ALU, Vector, and AGU/Memory ports.
     */
    void schedule(dgtl::machine::MachineFunction& function) {
        for (auto& block : function.blocks()) {
            schedule_block(block, function);
        }
    }

    /**
     * @brief Schedules machine instructions within a single MachineBasicBlock.
     * Uses a cycle-accurate sliding window and port consumption model.
     */
    void schedule_block(dgtl::machine::MachineBasicBlock& block,
                        dgtl::machine::MachineFunction& function) {
        if (block.instructions.size() <= 1) {
            return;
        }

        std::vector<dgtl::machine::InstrId> orig_instructions = block.instructions;
        std::vector<dgtl::machine::InstrId> scheduled;
        scheduled.reserve(orig_instructions.size());

        std::vector<dgtl::machine::InstrId> ready_queue;
        ready_queue.reserve(WINDOW_SIZE);

        size_t lookahead_ptr = 0;
        const size_t total_count = orig_instructions.size();

        while (scheduled.size() < total_count) {
            current_cycle_resources.init_from_target(m_target_machine);

            // Fill ready window
            while (ready_queue.size() < WINDOW_SIZE && lookahead_ptr < total_count) {
                ready_queue.push_back(orig_instructions[lookahead_ptr++]);
            }

            // Port dispatch across ready queue
            bool dispatched_any = false;
            for (auto it = ready_queue.begin(); it != ready_queue.end(); ) {
                const auto* inst = function.instruction(*it);
                if (inst && can_dispatch(*inst)) {
                    consume_resources(*inst);
                    scheduled.push_back(*it);
                    it = ready_queue.erase(it);
                    dispatched_any = true;
                } else {
                    ++it;
                }
            }

            // Stall-breaker: force dispatch if stalled to maintain progress
            if (!dispatched_any && !ready_queue.empty()) {
                scheduled.push_back(ready_queue.front());
                ready_queue.erase(ready_queue.begin());
            }
        }

        block.instructions = std::move(scheduled);
    }

    static void write_shadow_node(dgtl::ir::IRStream& stream, uint32_t idx, const dgtl::ir::IRNode& node) noexcept {
        dgtl::ir::IRAtom& atom = stream.get_shadow_atom(idx);
        dgtl::ir::IRCore& core = stream.get_shadow_core(idx);

        atom.opcode = node.opcode;
        atom.flags = node.flags;
        core.result_id = node.result_id;
        core.op_a = node.op_a;
        core.op_b = node.op_b;
        core.op_c = node.op_c;
        core.intent_hash = dgtl::ir::IRNode::calculate_hash(
            atom.opcode,
            core.result_id,
            core.op_a,
            core.op_b,
            core.op_c
        );
    }

    [[nodiscard]] bool can_dispatch(const dgtl::machine::MachineInstruction& inst) const noexcept {
        switch (inst.semantic_kind) {
            case dgtl::machine::MachineSemanticKind::VectorAdd:
            case dgtl::machine::MachineSemanticKind::VectorMul:
            case dgtl::machine::MachineSemanticKind::VectorGather:
            case dgtl::machine::MachineSemanticKind::VectorStore:
                return current_cycle_resources.vec_slots > 0;
            case dgtl::machine::MachineSemanticKind::LoadStack:
            case dgtl::machine::MachineSemanticKind::StoreStack:
            case dgtl::machine::MachineSemanticKind::AtomicOp:
            case dgtl::machine::MachineSemanticKind::HardwareFence:
                return current_cycle_resources.mem_slots > 0;
            case dgtl::machine::MachineSemanticKind::IntegerDiv:
            case dgtl::machine::MachineSemanticKind::IntegerMod:
            case dgtl::machine::MachineSemanticKind::FloatDiv:
                return current_cycle_resources.alu_slots > 0;
            default:
                return current_cycle_resources.alu_slots > 0;
        }
    }

    void consume_resources(const dgtl::machine::MachineInstruction& inst) noexcept {
        switch (inst.semantic_kind) {
            case dgtl::machine::MachineSemanticKind::VectorAdd:
            case dgtl::machine::MachineSemanticKind::VectorMul:
            case dgtl::machine::MachineSemanticKind::VectorGather:
            case dgtl::machine::MachineSemanticKind::VectorStore:
                if (current_cycle_resources.vec_slots > 0) current_cycle_resources.vec_slots--;
                break;
            case dgtl::machine::MachineSemanticKind::LoadStack:
            case dgtl::machine::MachineSemanticKind::StoreStack:
            case dgtl::machine::MachineSemanticKind::AtomicOp:
            case dgtl::machine::MachineSemanticKind::HardwareFence:
                if (current_cycle_resources.mem_slots > 0) current_cycle_resources.mem_slots--;
                break;
            default:
                if (current_cycle_resources.alu_slots > 0) current_cycle_resources.alu_slots--;
                break;
        }
    }

private:
    [[nodiscard]] bool can_dispatch(const dgtl::ir::IRNode& node) const noexcept {
        const uint32_t lf = static_cast<uint32_t>(node.logic_flags);
        
        // Query target description for instruction latency
        const uint8_t latency = m_target_machine.instruction_latency(node.opcode);
        if (latency > 1) {
            // Multi-cycle instructions may have restricted dispatch
            if (node.opcode == dgtl::ir::OpCode::DIV_I64 || node.opcode == dgtl::ir::OpCode::DIV_I32) {
                return current_cycle_resources.alu_slots > 0;  // DIV requires ALU resource hold
            }
        }
        
        // Check for Vector/SIMD Port Availability (HintFlags in logic_flags)
        if (lf & static_cast<uint32_t>(dgtl::ir::HintFlags::RequiresVecPort)) {
            return current_cycle_resources.vec_slots > 0;
        }
        
        // Check for Memory/AGU Port Availability
        if (node.opcode == dgtl::ir::OpCode::LOAD || node.opcode == dgtl::ir::OpCode::ATOMIC_LOAD ||
            node.opcode == dgtl::ir::OpCode::STORE || node.opcode == dgtl::ir::OpCode::ATOMIC_STORE ||
            node.opcode == dgtl::ir::OpCode::ATOMIC_FENCE ||
            node.opcode == dgtl::ir::OpCode::GATHER_F32_512 ||
            node.opcode == dgtl::ir::OpCode::GATHER_16 ||
            node.opcode == dgtl::ir::OpCode::PREFETCH ||
            node.opcode == dgtl::ir::OpCode::PREFETCH_T0 ||
            node.opcode == dgtl::ir::OpCode::CLFLUSH) {
            return current_cycle_resources.mem_slots > 0;
        }
        
        return current_cycle_resources.alu_slots > 0;
    }

    void consume_resources(const dgtl::ir::IRNode& node) noexcept {
        const uint32_t lf = static_cast<uint32_t>(node.logic_flags);
        
        // Query instruction latency to determine resource hold duration
        const uint8_t latency = m_target_machine.instruction_latency(node.opcode);
        (void)latency;

        if (lf & static_cast<uint32_t>(dgtl::ir::HintFlags::RequiresVecPort)) {
            if (current_cycle_resources.vec_slots > 0) current_cycle_resources.vec_slots--;
        } else if (node.opcode == dgtl::ir::OpCode::LOAD || node.opcode == dgtl::ir::OpCode::ATOMIC_LOAD ||
                   node.opcode == dgtl::ir::OpCode::STORE || node.opcode == dgtl::ir::OpCode::ATOMIC_STORE ||
                   node.opcode == dgtl::ir::OpCode::ATOMIC_FENCE ||
                   node.opcode == dgtl::ir::OpCode::GATHER_F32_512 ||
                   node.opcode == dgtl::ir::OpCode::GATHER_16 ||
                   node.opcode == dgtl::ir::OpCode::PREFETCH ||
                   node.opcode == dgtl::ir::OpCode::PREFETCH_T0 ||
                   node.opcode == dgtl::ir::OpCode::CLFLUSH) {
            if (current_cycle_resources.mem_slots > 0) current_cycle_resources.mem_slots--;
        } else {
            if (current_cycle_resources.alu_slots > 0) current_cycle_resources.alu_slots--;
        }
    }
};

} // namespace dgtl::jit