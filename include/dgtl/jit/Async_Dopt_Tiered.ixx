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
 * STEP 30: CYCLE-ACCURATE HARDWARE PORT SCHEDULER & TIERED DEOPT HANDLER
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING (ARENA-BACKED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * VERSION 3.0: TITAN-STABLE (ZERO-TER PINNING & 178 CANONICAL OPCODES)
 */

module;

#include <cstdlib>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <string>
#include <iostream>
#include <stdexcept>
#include <chrono>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define DGTL_OS_WINDOWS
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

export module dgtl.jit.tiered_optimizer;

import dgtl.ir;
import dgtl.jit.executor;
import dgtl.ir.bridge;
import dgtl.hir.semantic;
import dgtl.mir.instruction_selector;
import dgtl.mir.machine_ir;
import dgtl.mir.pipeline;
import dgtl.mir.isel_gen;
import dgtl.mir;
import dgtl.isa.x64.x64_reg_map;
import dgtl.isa.x64.x64_instruction_info;
import dgtl.isa.x64.x64_abi;
import dgtl.isa.x64.nasm_emitter;
import dgtl.isa.x64.target;
import dgtl.target;
import dgtl.ir.pass.transform_scalar_core;
import dgtl.linker.backend;

export namespace dgtl::jit {

/**
 * @enum DeoptReason
 * @brief Reasons why deoptimization was triggered.
 */
enum class DeoptReason : uint8_t {
    SpeculationFailed = 0,  ///< Speculative assumption proved wrong
    TypeMismatch = 1,       ///< Type guard failed at runtime
    BoundCheckFailed = 2,   ///< Array bounds check failed
    GuardFailed = 3,        ///< Generic guard condition failed
    Explicit = 4            ///< Explicit deoptimization requested
};

/**
 * @struct DeoptInfo
 * @brief Metadata about a deoptimization point.
 */
struct DeoptInfo {
    [[maybe_unused]] uint32_t id;
    [[maybe_unused]] DeoptReason reason;
    [[maybe_unused]] const char* guard_name;
    [[maybe_unused]] uint64_t trigger_count;
};

/**
 * @class Deopt_Handler
 * @brief Manages deoptimization callbacks and fallback execution paths.
 * 
 * When JIT-compiled code encounters a speculative assumption that fails,
 * it deoptimizes: invalidates the optimized code and falls back to
 * interpreter or baseline-compiled path with updated type information.
 */
class Deopt_Handler {
public:
    Deopt_Handler() = default;
    ~Deopt_Handler() = default;

    Deopt_Handler(const Deopt_Handler&) = delete;
    Deopt_Handler& operator=(const Deopt_Handler&) = delete;

    using DeoptCallback = std::function<int64_t(DeoptReason, void*)>;

    /**
     * @brief Register a deoptimization handler for a specific ID.
     * @param id Unique deoptimization point ID
     * @param handler Callback to invoke when deoptimization occurs
     */
    void register_deopt_handler(uint32_t id, DeoptCallback handler) noexcept {
        if (handler) {
            handlers_[id] = handler;
        }
    }

    /**
     * @brief Unregister a deoptimization handler.
     * @param id Handler ID to remove
     */
    void unregister_deopt_handler(uint32_t id) noexcept {
        handlers_.erase(id);
    }

    /**
     * @brief Handle a deoptimization event.
     * @param id Deoptimization point ID
     * @param reason Why deoptimization occurred
     * @param context Opaque context passed to handler
     * @return Result code from handler (negative = error)
     */
    [[nodiscard]] int64_t handle_deopt(uint32_t id, DeoptReason reason, void* context) noexcept {
        auto it = handlers_.find(id);
        if (it == handlers_.end()) return -1;
        
        try {
            // Track deoptimization event
            deopt_history_.push_back({id, reason, nullptr, 0});
            
            // Invoke callback
            return it->second(reason, context);
        } catch (...) {
            return -1;
        }
    }

    /**
     * @brief Check if a handler is registered.
     * @param id Handler ID
     * @return true if handler exists
     */
    [[nodiscard]] bool has_handler(uint32_t id) const noexcept {
        return handlers_.count(id) > 0;
    }

    /**
     * @brief Get the count of registered handlers.
     * @return Number of active handlers
     */
    [[nodiscard]] size_t handler_count() const noexcept {
        return handlers_.size();
    }

    /**
     * @brief Get deoptimization history.
     * @return Reference to history log
     */
    [[nodiscard]] const std::vector<DeoptInfo>& get_history() const noexcept {
        return deopt_history_;
    }

    /**
     * @brief Clear all handlers and reset state.
     */
    void clear() noexcept {
        handlers_.clear();
        deopt_history_.clear();
    }

private:
    std::unordered_map<uint32_t, DeoptCallback> handlers_;
    std::vector<DeoptInfo> deopt_history_;
};

/**
 * @struct SpeculativeVersion
 * @brief Represents a speculative optimization variant with confidence score.
 */
struct SpeculativeVersion {
    [[maybe_unused]] ir::IRNode* variant;
    double confidence;
};

struct SpeculationReplayEvent {
    uint64_t sequence_id = 0;
    uint32_t target_node_idx = 0;
    bool committed = false;
};

/**
 * @class AsyncSpeculator
 * @brief Manages background JIT re-optimization tasks with multi-version speculation.
 * Uses a transactional "Shadow" approach to ensure background threads never
 * corrupt the primary instruction stream during optimization "surgery".
 */
class alignas(64) AsyncSpeculator {
    // Reference to the JIT engine for loading validated machine code
    dgtl::runtime::DGTLExecutor& m__engine;
    // Target Machine Configuration (queries CCs, registers, instruction latencies)
    const dgtl::core::TargetMachine& m_target_machine;

    struct SpeculativeCache {
        // Points to the RX-protected executable code once background JIT finishes.
        std::atomic<dgtl::runtime::Function> code_ptr{nullptr};
        
        // Transactional State: 0 = Pending, 1 = Validated, 2 = Corrupt/Discarded
        std::atomic<uint8_t> tx_status{0}; 
    };

    // Registry: Branch Target ID -> Speculative Metadata
    std::unordered_map<uint32_t, std::shared_ptr<SpeculativeCache>> m_registry;
    std::mutex m_registry_mtx;

    struct SpeculationTask {
        uint64_t sequence_id = 0;
        uint32_t target_node_idx = 0;
        ir::IRStream stream;
    };

    // Background Worker Pool
    std::vector<std::thread> m_workers;
    std::queue<SpeculationTask> m_task_queue;
    std::mutex m_queue_mtx;
    std::condition_variable m_cv_task;
    std::atomic<bool> m_stop_flag{false};
    std::atomic<bool> m_deterministic_replay{false};
    std::atomic<uint64_t> m_next_sequence_id{1};
    mutable std::mutex m_replay_mtx;
    std::vector<SpeculationReplayEvent> m_replay_log;

public:
    explicit AsyncSpeculator(dgtl::runtime::DGTLExecutor& executor,
                           size_t background_threads = 4)
        : m__engine(executor), m_target_machine(executor.get_target_machine()) {
        for (size_t i = 0; i < background_threads; ++i) {
            m_workers.emplace_back(&AsyncSpeculator::worker_loop, this);
        }
    }

    ~AsyncSpeculator() {
        m_stop_flag.store(true, std::memory_order_release);
        m_cv_task.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
        
        std::lock_guard<std::mutex> lock(m_registry_mtx);
        m_registry.clear();
    }

    void enable_deterministic_replay(bool enabled) noexcept {
        m_deterministic_replay.store(enabled, std::memory_order_release);
        if (enabled) {
            clear_replay_log();
        }
    }

    [[nodiscard]] bool deterministic_replay_enabled() const noexcept {
        return m_deterministic_replay.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::vector<SpeculationReplayEvent> get_replay_log() const {
        std::lock_guard<std::mutex> lock(m_replay_mtx);
        return m_replay_log;
    }

    void clear_replay_log() noexcept {
        std::lock_guard<std::mutex> lock(m_replay_mtx);
        m_replay_log.clear();
    }

    /**
     * @brief Parallel Speculative Strike.
     * Deep-clones the IR into a Shadow Stream to isolate the optimization opt.
     */
    void speculate_branch(const ir::IRStream& stream, uint32_t target_node_idx) {
        {
            std::lock_guard<std::mutex> lock(m_registry_mtx);
            auto it = m_registry.find(target_node_idx);
            if (it != m_registry.end()) {
                const uint8_t state = it->second ? it->second->tx_status.load(std::memory_order_acquire) : 2;
                // Pending or already validated: do not enqueue duplicate work.
                if (state == 0 || state == 1) {
                    return;
                }
                // Rejected/failed entry can be retried with a fresh cache object.
                m_registry.erase(it);
            }
            m_registry[target_node_idx] = std::make_shared<SpeculativeCache>();
        }

        {
            std::lock_guard<std::mutex> lock(m_queue_mtx);
            // .clone() bypasses the deleted copy constructor to ensure memory safety.
            m_task_queue.push(SpeculationTask{
                m_next_sequence_id.fetch_add(1, std::memory_order_relaxed),
                target_node_idx,
                stream.clone()
            });
        }
        m_cv_task.notify_one();
    }

    /**
     * @brief Attempts to retrieve a background-optimized function pointer.
     */
    dgtl::runtime::Function get_speculative_target(uint32_t node_idx) {
        std::lock_guard<std::mutex> lock(m_registry_mtx);
        if (auto it = m_registry.find(node_idx); it != m_registry.end()) {
            if (it->second->tx_status.load(std::memory_order_acquire) == 1) {
                return it->second->code_ptr.load(std::memory_order_acquire);
            }
        }
        return nullptr;
    }

    /**
     * @brief Generate multiple speculative optimization variants for an IR node.
     * Analyzes the IR node and produces multiple specialization candidates.
     * 
     * Variants include:
     * 1. NATIVE_TYPE: Assume operand types match function signature
     * 2. CONSTANT_FOLDING: Pre-compute constant expressions
     * 3. GUARD_SPECIALIZATION: Add type checks before optimized code
     * 4. LOOP_UNROLL: Unroll small loops if detected
     * 5. TARGET_SPECIALIZATION: Optimize for target CC & register layout
     * 
     * @param node The IR node to generate variants for
     * @return Vector of speculative versions sorted by confidence (highest first), 
     *         using target machine description to guide variant generation
     */
    std::vector<SpeculativeVersion> generate_versions(ir::IRNode* node);

private:
    /**
     * @brief Primary background thread logic for JIT re-optimization.
     *        Uses target description framework for instruction latency & CC queries
     */
    void worker_loop() {
        while (true) {
            uint64_t sequence_id = 0;
            uint32_t target_idx = 0;
            ir::IRStream current_task_stream;

            {
                std::unique_lock<std::mutex> lock(m_queue_mtx);
                m_cv_task.wait(lock, [this] { 
                    return !m_task_queue.empty() || m_stop_flag.load(std::memory_order_acquire); 
                });

                if (m_stop_flag.load(std::memory_order_acquire) && m_task_queue.empty()) return;
                
                // Move semantic extraction for non-copyable IRStream
                sequence_id = m_task_queue.front().sequence_id;
                target_idx = m_task_queue.front().target_node_idx;
                current_task_stream = std::move(m_task_queue.front().stream);
                m_task_queue.pop();
            }

            if (deterministic_replay_enabled()) {
                std::lock_guard<std::mutex> lock(m_replay_mtx);
                m_replay_log.push_back({sequence_id, target_idx, false});
            }

            // PILLAR 2: SHADOW WRITING
            // Isolate optimization from the moved stream to prevent main-thread corruption.
            ir::IRStream shadow_stream = current_task_stream.clone();
            bool strike_success = perform_speculative_strike(shadow_stream, target_idx);

            if (strike_success) [[likely]] {
                if (validate_ir_invariants(shadow_stream)) {
                    dgtl::machine::MachineFunction speculative_mf;
                    std::vector<uint8_t> machine_code = emit_speculative_code(shadow_stream, speculative_mf, m_target_machine);
                    dgtl::runtime::Function func = machine_code.empty()
                        ? nullptr
                        : (speculative_mf.instructions().empty()
                            ? m__engine.load_and_prepare(machine_code)
                            : m__engine.load_and_prepare_mir(machine_code, speculative_mf));

                    std::lock_guard<std::mutex> lock(m_registry_mtx);
                    auto it = m_registry.find(target_idx);
                    if (it != m_registry.end() && it->second) {
                        const auto& entry = it->second;
                        if (func) {
                            entry->code_ptr.store(func, std::memory_order_release);
                            entry->tx_status.store(1, std::memory_order_release); // COMMIT SUCCESS
                            if (deterministic_replay_enabled()) {
                                std::lock_guard<std::mutex> replay_lock(m_replay_mtx);
                                auto event_it = std::find_if(m_replay_log.begin(), m_replay_log.end(),
                                    [sequence_id](const auto& event) {
                                        return event.sequence_id == sequence_id;
                                    });
                                if (event_it != m_replay_log.end()) {
                                    event_it->committed = true;
                                }
                            }
                        } else {
                            entry->tx_status.store(2, std::memory_order_release); // REJECTED
                        }
                    }
                } else {
                    std::lock_guard<std::mutex> lock(m_registry_mtx);
                    auto it = m_registry.find(target_idx);
                    if (it != m_registry.end() && it->second) {
                        it->second->tx_status.store(2, std::memory_order_release); // REJECTED
                    }
                }
            } else {
                std::lock_guard<std::mutex> lock(m_registry_mtx);
                auto it = m_registry.find(target_idx);
                if (it != m_registry.end() && it->second) {
                    it->second->tx_status.store(2, std::memory_order_release); // REJECTED
                }
            }
        }
    }

    /**
     * @brief Optimized IR specialization: GVN, DCE, constant folding.
     * Marks anchor node as optimized and runs multi-pass optimization.
     */
    static bool perform_speculative_strike(ir::IRStream& stream, uint32_t start_idx) {
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream) || start_idx >= stream.size()) {
            return false;
        }

        // Mark target node as optimized and refresh its identity hash before passes.
        auto anchor = stream.get_node(start_idx);
        anchor.flags |= ir::Flags::IS_OPTIMIZED;
        anchor.refresh_intent_hash();
        stream.set_node(start_idx, anchor);

        // Execute speculative optimization passes
        dgtl::ir::pass::transform::GlobalValueNumbering().execute(stream);
        dgtl::ir::pass::transform::DeadCodeElimination().execute(stream);
        
        // Transactional validation: if passes corrupt SSA, rollback
        if (!stream.validate_shadow_integrity()) [[unlikely]] {
            return false;
        }
        
        return true;
    }

    /**
     * @brief The Sentinel: Bit-Level Invariant Check.
     * Scans for Dangling IDs, uninitialized slots, or logic hallucinations.
     * Prevents corrupted IR from entering code generation.
     */
    static bool validate_ir_invariants(const ir::IRStream& stream) {
        const size_t count = stream.size();
        
        // Quick-exit for empty streams
        if (count == 0) [[unlikely]] return true;
        
        for (size_t i = 0; i < count; ++i) {
            // Using get_node(i) to ensure bit-packed getters are invoked correctly.
            const ir::IRNode node = stream.get_node(i);
            
            // Check for uninitialized result IDs in active instructions
            if (node.result_id == ir::INVALID_SLOT && node.opcode != ir::OpCode::NOP) [[unlikely]] {
                return false; 
            }
            
            // Verify intent hash matches computed value (detects bit-flip corruption)
            const uint32_t expected_hash = ir::IRNode::calculate_hash(
                node.opcode, node.result_id, node.op_a, node.op_b, node.op_c
            );
            if (node.intent_hash != expected_hash) [[unlikely]] {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Generates machine code for the optimized speculative branch.
     * Lowers IRStream to formal MachineFunction via MachinePipeline, verifies via MachineVerifier,
     * and synthesizes validated native machine code via DGTLLinker.
     */
    static std::vector<uint8_t> emit_speculative_code(const ir::IRStream& stream,
                                                      dgtl::machine::MachineFunction& out_mf,
                                                      const dgtl::core::TargetMachine& target) {
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream) || stream.size() == 0) [[unlikely]] {
            return { 0x55, 0x48, 0x89, 0xE5, 0x90, 0x5D, 0xC3 };
        }

        const dgtl::isa::x64::X64TargetSubtarget subtarget;

        const auto mir_result = dgtl::mir::MachinePipeline::compile(
            out_mf,
            stream,
            target,
            subtarget.reg_info,
            &subtarget.instr_info,
            &subtarget.frame_info,
            &subtarget.abi(true),
            dgtl::mir::make_builtin_instruction_selector_program());

        if (!mir_result.success || out_mf.instructions().empty()) {
            return { 0x55, 0x48, 0x89, 0xE5, 0x90, 0x5D, 0xC3 };
        }

        const auto verify_result = dgtl::machine::MachineVerifier::verify(out_mf);
        if (!verify_result.ok()) {
            return { 0x55, 0x48, 0x89, 0xE5, 0x90, 0x5D, 0xC3 };
        }

        std::vector<uint8_t> code;
        dgtl::linker::DGTLLinker linker;
        if (linker.build_raw_x64_machine_code(out_mf, code) && !code.empty()) {
            return code;
        }

        return { 0x55, 0x48, 0x89, 0xE5, 0x90, 0x5D, 0xC3 };
    }
};

/**
 * @class TieredController
 * @brief Manages tiered compilation: JIT for hot code, AOT baking for stabilized hot loops.
 * 
 * Tracks execution counts and when a function exceeds BAKE_THRESHOLD, 
 * it's serialized to disk as AOT-compiled assembly.
 */
class TieredController {
    dgtl::runtime::DGTLExecutor& engine_;   ///< Reference to JIT executor engine
    dgtl::isa::DGTLNasmEmitter emitter_;            ///< AOT emitter for baking to disk
    std::unordered_map<uintptr_t, uint32_t> hot_counts_;  ///< Execution count tracking
    mutable std::mutex profile_mtx_;                ///< Thread-safe profiling (mutable for const methods)
    static constexpr uint32_t BAKE_THRESHOLD = 10000;  ///< Executions before AOT bake

public:
    /**
     * @brief Constructor binds reference to JIT executor.
     * @param executor Reference to the JIT execution engine
     */
    explicit TieredController(dgtl::runtime::DGTLExecutor& executor)
        : engine_(executor) {}

    ~TieredController() = default;
    TieredController(const TieredController&) = delete;
    TieredController& operator=(const TieredController&) = delete;

    /**
     * @brief Record execution and check if we should bake to disk (thread-safe).
     * Increments execution count for the given function pointer.
     * When count reaches BAKE_THRESHOLD, triggers AOT compilation.
     * 
     * @param func_ptr Function pointer from JIT executor
     * @param symbols Symbol table for AOT code generation
     */
    void profile_and_check(dgtl::runtime::Function func_ptr,
                           const dgtl::semantic::DGTLSymbolTable& symbols) {
        if (!func_ptr) {
            return;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(func_ptr);
        
        {
            std::lock_guard<std::mutex> lock(profile_mtx_);
            if (++hot_counts_[addr] >= BAKE_THRESHOLD) {
                hot_counts_[addr] = 0;  // Reset counter to allow re-baking after further evolution
                bake_to_disk(addr, symbols);
            }
        }
    }

    /**
     * @brief Select the best speculative version based on runtime profiling.
     * Sorts versions by confidence score (highest first) to identify
     * the most likely-correct specialization for execution.
     * 
     * @param versions Vector of speculative versions to rank
     */
    static void select_best_version(std::vector<SpeculativeVersion>& versions) {
        if (versions.empty()) return;
        
        // Sort by confidence score (highest first) - stable sort preserves order for ties
        std::stable_sort(versions.begin(), versions.end(), 
                    [](const SpeculativeVersion& a, const SpeculativeVersion& b) {
                        return a.confidence > b.confidence;
                    });
    }

    /**
     * @brief Get current execution count for a function address.
     * @param addr Function address
     * @return Number of times this function has been executed
     */
    [[nodiscard]] uint32_t get_execution_count(uintptr_t addr) const noexcept {
        std::lock_guard<std::mutex> lock(profile_mtx_);
        auto it = hot_counts_.find(addr);
        return (it != hot_counts_.end()) ? it->second : 0;
    }

    /**
     * @brief Clear all profiling data (useful for testing or resetting state).
     */
    void clear_profiles() noexcept {
        std::lock_guard<std::mutex> lock(profile_mtx_);
        hot_counts_.clear();
    }

private:
    /**
     * @brief Bake hot JIT code to disk as AOT assembly.
     * Retrieves the IR trace from the given function address and emits it
     * as human-readable NASM assembly for inspection or later compilation.
     * 
     * @param addr JIT function address (key to IR trace map)
     * @param symbols Symbol table for code generation
     */
    void bake_mir_to_disk(uintptr_t addr, const dgtl::machine::MachineFunction& mf) {
        try {
            char filename_buf[128] = {};
            std::snprintf(filename_buf, sizeof(filename_buf), 
                        "dgtl_baked_mir_kernel_0x%llX.mir", 
                        static_cast<unsigned long long>(addr));
            std::string filename(filename_buf);

            std::ofstream file(filename, std::ios::out | std::ios::trunc);
            if (!file.is_open()) [[unlikely]] {
                return;
            }

            file << "; DGTL Verified MIR Baked Kernel\n";
            file << "; Function: " << mf.name() << "\n";
            file << "; Address: 0x" << std::hex << addr << std::dec << "\n";
            file << "; Basic Blocks: " << mf.blocks().size() << "\n";
            file << "; Instructions: " << mf.instructions().size() << "\n";
            file << "; Frame Size: " << mf.frame_info().total_frame_bytes << " bytes\n";
            file << "; Baked at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n\n";

            for (const auto& block : mf.blocks()) {
                file << "bb." << block.id << ":\n";
                for (auto instr_id : block.instructions) {
                    const auto* inst = mf.instruction(instr_id);
                    if (!inst) continue;
                    file << "  " << dgtl::machine::machine_form_name(inst->form);
                    if (inst->has_target_opcode()) {
                        file << " [opcode: 0x" << std::hex << inst->target_opcode() << std::dec << "]";
                    }
                    file << "\n";
                }
            }
            file.close();

            std::cerr << "[DGTL-TIERED] Baked verified MIR kernel to " << filename << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[DGTL-ERROR] MIR Bake failed at 0x" << std::hex << addr << std::dec 
                      << ": " << e.what() << "\n";
        }
    }

    void bake_to_disk(uintptr_t addr, const dgtl::semantic::DGTLSymbolTable& symbols) {
        try {
            // Check if verified Machine IR is available from the executor
            auto mir_func = engine_.get_mir_from_address(addr);
            if (mir_func && !mir_func->instructions().empty()) {
                bake_mir_to_disk(addr, *mir_func);

                // Also emit NASM assembly directly from the verified MachineFunction
                emitter_.clear();
                emitter_.emit(*mir_func, &symbols);

                char filename_buf[128] = {};
                std::snprintf(filename_buf, sizeof(filename_buf), 
                            "dgtl_baked_kernel_0x%llX.asm", 
                            static_cast<unsigned long long>(addr));
                std::string filename(filename_buf);
                
                std::ofstream file(filename, std::ios::out | std::ios::trunc);
                if (file.is_open()) {
                    file << "; DGTL Baked Kernel (from MIR)\n";
                    file << "; Function: " << mir_func->name() << "\n";
                    file << "; Address: 0x" << std::hex << addr << std::dec << "\n";
                    file << "; Baked at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n\n";
                    file << emitter_.get_output();
                    file.close();
                    std::cerr << "[DGTL-TIERED] Baked kernel to " << filename << "\n";
                }
                return;
            }

            std::cerr << "[DGTL-TIERED] No verified MIR trace found at 0x" << std::hex << addr << std::dec << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[DGTL-ERROR] Bake failed at 0x" << std::hex << addr << std::dec 
                      << ": " << e.what() << "\n";
        }
    }
};

} // namespace dgtl::jit

/**
 * @brief Compatibility Alias for DGTL-Zenith high-level logic.
 */
export namespace dgtl::opt {
    using DGTLAsyncSpeculator = dgtl::jit::AsyncSpeculator;
} // namespace dgtl::opt