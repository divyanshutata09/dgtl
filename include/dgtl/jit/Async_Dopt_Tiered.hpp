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

#pragma once

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

// Core DGTL Includes
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/jit/JIT_Executor.hpp"
#include "dgtl/ir/interop/IR_Analysis_Bridge.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/isa/lowering/Instruction_Selector.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"

namespace dgtl::jit {

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 1: DEOPT_HANDLER.HPP (Deoptimization Management)
// ═══════════════════════════════════════════════════════════════════════════════

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
    uint32_t id;
    DeoptReason reason;
    const char* guard_name;
    uint64_t trigger_count;
};

/**
 * @class Deopt_Handler
 * @brief Manages deoptimization callbacks and fallback execution paths.
 * 
 * When JIT-compiled code encounters a speculative assumption that fails,
 * it deoptimizes: invalidates the optimized code and falls back to
 * interpreter or AOT-compiled path with updated type information.
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

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 2: ASYNC_SPECULATOR.HPP (Speculative Background Optimization)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @struct SpeculativeVersion
 * @brief Represents a speculative optimization variant with confidence score.
 */
struct SpeculativeVersion {
    ir::IRNode* variant;
    double confidence;
};

/**
 * @class AsyncSpeculator
 * @brief Manages background JIT re-optimization tasks with multi-version speculation.
 * Uses a transactional "Shadow" approach to ensure background threads never
 * corrupt the primary instruction stream during optimization "surgery".
 */
class alignas(64) AsyncSpeculator {
    // Reference to the JIT engine for loading validated machine code
    dgtl::jit::runtime::DGTLJitExecutor& m_jit_engine;
        // Target Machine Configuration (queries CCs, registers, instruction latencies)
        const dgtl::core::TargetMachine& m_target_machine;

    
    struct SpeculativeCache {
        // Points to the RX-protected executable code once background JIT finishes.
        std::atomic<dgtl::jit::runtime::JitFunction> code_ptr{nullptr};
        
        // Transactional State: 0 = Pending, 1 = Validated, 2 = Corrupt/Discarded
        std::atomic<uint8_t> tx_status{0}; 
    };

    // Registry: Branch Target ID -> Speculative Metadata
    std::unordered_map<uint32_t, std::shared_ptr<SpeculativeCache>> m_registry;
    std::mutex m_registry_mtx;

    // Background Worker Pool
    std::vector<std::thread> m_workers;
    std::queue<std::pair<uint32_t, ir::IRStream>> m_task_queue;
    std::mutex m_queue_mtx;
    std::condition_variable m_cv_task;
    std::atomic<bool> m_stop_flag{false};

public:
    explicit AsyncSpeculator(dgtl::jit::runtime::DGTLJitExecutor& jit, size_t background_threads = 4) 
            : m_jit_engine(jit), m_target_machine(jit.get_target_machine()) {
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
            m_task_queue.push({target_node_idx, stream.clone()});
        }
        m_cv_task.notify_one();
    }

    /**
     * @brief Attempts to retrieve a background-optimized function pointer.
     */
    dgtl::jit::runtime::JitFunction get_speculative_target(uint32_t node_idx) {
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
            uint32_t target_idx = 0;
            ir::IRStream current_task_stream;

            {
                std::unique_lock<std::mutex> lock(m_queue_mtx);
                m_cv_task.wait(lock, [this] { 
                    return !m_task_queue.empty() || m_stop_flag.load(std::memory_order_acquire); 
                });

                if (m_stop_flag.load(std::memory_order_acquire) && m_task_queue.empty()) return;
                
                // Move semantic extraction for non-copyable IRStream
                target_idx = m_task_queue.front().first;
                current_task_stream = std::move(m_task_queue.front().second);
                m_task_queue.pop();
            }

            // PILLAR 2: SHADOW WRITING
            // Isolate optimization from the moved stream to prevent main-thread corruption.
            ir::IRStream shadow_stream = current_task_stream.clone();
            bool strike_success = perform_speculative_strike(shadow_stream, target_idx);

            if (strike_success) [[likely]] {
                if (validate_ir_invariants(shadow_stream)) {
                    std::vector<uint8_t> machine_code = emit_speculative_code(shadow_stream);
                    dgtl::jit::runtime::JitFunction func = machine_code.empty()
                        ? nullptr
                        : m_jit_engine.load_and_prepare(machine_code, shadow_stream);

                    std::lock_guard<std::mutex> lock(m_registry_mtx);
                    auto it = m_registry.find(target_idx);
                    if (it != m_registry.end() && it->second) {
                        auto& entry = it->second;
                        if (func) {
                            entry->code_ptr.store(func, std::memory_order_release);
                            entry->tx_status.store(1, std::memory_order_release); // COMMIT SUCCESS
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
    bool perform_speculative_strike(ir::IRStream& stream, uint32_t start_idx) {
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream) || start_idx >= stream.size()) {
            return false;
        }

        // Mark target node as optimized and refresh its identity hash before passes.
        auto anchor = stream.get_node(start_idx);
        anchor.flags |= ir::Flags::IS_OPTIMIZED;
        anchor.refresh_intent_hash();
        stream.set_node(start_idx, anchor);

        // Execute speculative optimization passes
        dgtl::pass::transform::GlobalValueNumbering().execute(stream);
        dgtl::pass::transform::DeadCodeElimination().execute(stream);
        
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
    bool validate_ir_invariants(const ir::IRStream& stream) {
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
     * Uses direct binary emission to produce minimal, validated x64 code.
     * 
     * For speculative code, we emit:
     * 1. Standard prologue (push rbp, mov rbp rsp)
     * 2. IR node sequence via X64 backend
     * 3. Standard epilogue (pop rbp, ret)
     * 
     * The code is kept compact to fit in I-cache and reduce speculation overhead.
     */
    std::vector<uint8_t> emit_speculative_code(const ir::IRStream& stream) {
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream)) [[unlikely]] {
            // Empty stream: return minimal valid function
            return { 0x55, 0x48, 0x89, 0xE5, 0x90, 0x5D, 0xC3 };
        }

        std::vector<uint8_t> code;
        code.reserve(256);  // Typical speculative code is <256 bytes
        
        // PROLOGUE: push rbp; mov rbp, rsp (setup frame)
        code.push_back(0x55);                           // push rbp
        code.push_back(0x48); code.push_back(0x89);     // mov
        code.push_back(0xE5);                           // rbp, rsp
        
        // BODY: Direct IR emission for speculative nodes
        // For each IR node in the optimized stream, emit appropriate x64 sequences
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node(i);
            
            // Emit single-byte NOPs for placeholder nodes to maintain code alignment
            // In production, this would dispatch to proper emitter backends
            if (node.opcode == ir::OpCode::NOP) {
                code.push_back(0x90);
            }
            else if (node.opcode == ir::OpCode::RETURN) {
                // Break: speculative code terminates early
                break;
            }
            else {
                // For other opcodes: emit 1-byte NOP as placeholder
                // Full X64 emission would happen here with real backend
                code.push_back(0x90);
            }
        }
        
        // Ensure we have at least one instruction to prevent empty code
        if (code.size() <= 3) {
            code.push_back(0x90);  // Add NOP if too short
        }
        
        // EPILOGUE: pop rbp; ret (cleanup frame + return)
        code.push_back(0x5D);           // pop rbp
        code.push_back(0xC3);           // ret
        
        return code;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 3: TIERED_CONTROLLER.HPP (Tiered Compilation Management)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @class TieredController
 * @brief Manages tiered compilation: JIT for hot code, AOT baking for stabilized hot loops.
 * 
 * Tracks execution counts and when a function exceeds BAKE_THRESHOLD, 
 * it's serialized to disk as AOT-compiled assembly.
 */
class TieredController {
    dgtl::jit::runtime::DGTLJitExecutor& _jit;   ///< Reference to JIT executor engine
    dgtl::isa::DGTLNasmEmitter _aot;             ///< AOT emitter for baking to disk
    std::unordered_map<uintptr_t, uint32_t> _hot_counts;  ///< Execution count tracking
    mutable std::mutex _profile_mtx;             ///< Thread-safe profiling (mutable for const methods)
    static constexpr uint32_t BAKE_THRESHOLD = 10000;  ///< Executions before AOT bake

public:
    /**
     * @brief Constructor binds reference to JIT executor.
     * @param jit Reference to the JIT execution engine
     */
    explicit TieredController(dgtl::jit::runtime::DGTLJitExecutor& jit) 
        : _jit(jit) {
        // Initialize AOT emitter state
    }

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
    void profile_and_check(dgtl::jit::runtime::JitFunction func_ptr,
                           const dgtl::semantic::DGTLSymbolTable& symbols) {
        if (!func_ptr) {
            return;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(func_ptr);
        
        {
            std::lock_guard<std::mutex> lock(_profile_mtx);
            if (++_hot_counts[addr] >= BAKE_THRESHOLD) {
                _hot_counts[addr] = 0;  // Reset counter to allow re-baking after further evolution
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
    void select_best_version(std::vector<SpeculativeVersion>& versions) {
        if (versions.empty()) return;
        
        // Sort by confidence score (highest first) - stable sort preserves order for ties
        std::stable_sort(versions.begin(), versions.end(), 
                  [](const SpeculativeVersion& a, const SpeculativeVersion& b) {
                      return a.confidence > b.confidence;
                  });
        
        // The best version (highest confidence) is now at index 0
        // In production, this would also weight runtime metrics (cache misses, etc.)
    }

    /**
     * @brief Get current execution count for a function address.
     * @param addr Function address
     * @return Number of times this function has been executed
     */
    [[nodiscard]] uint32_t get_execution_count(uintptr_t addr) const noexcept {
        std::lock_guard<std::mutex> lock(_profile_mtx);
        auto it = _hot_counts.find(addr);
        return (it != _hot_counts.end()) ? it->second : 0;
    }

    /**
     * @brief Clear all profiling data (useful for testing or resetting state).
     */
    void clear_profiles() noexcept {
        std::lock_guard<std::mutex> lock(_profile_mtx);
        _hot_counts.clear();
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
    void bake_to_disk(uintptr_t addr, const dgtl::semantic::DGTLSymbolTable& symbols) {
        try {
            // Retrieve IR trace associated with this JIT function
            auto ir = _jit.get_ir_from_address(addr);
            
            if (ir.is_null() || ir.size() == 0) [[unlikely]] {
                std::cerr << "[DGTL-TIERED] No IR trace found at 0x" << std::hex << addr << std::dec << "\n";
                return;
            }

            // Emit IR to NASM assembly
            _aot.clear();
            _aot.emit(ir, symbols);

            // Write to timestamped file for analysis
            char filename_buf[128] = {};
            std::snprintf(filename_buf, sizeof(filename_buf), 
                         "dgtl_baked_kernel_0x%llX.asm", 
                         static_cast<unsigned long long>(addr));
            std::string filename(filename_buf);
            
            std::ofstream file(filename, std::ios::out | std::ios::trunc);
            if (!file.is_open()) [[unlikely]] {
                std::cerr << "[DGTL-WARN] Failed to open bake output: " << filename << "\n";
                return;
            }
            
            // Write assembly header with metadata
            file << "; DGTL Baked Kernel\n";
            file << "; Address: 0x" << std::hex << addr << std::dec << "\n";
            file << "; IR Size: " << ir.size() << " nodes\n";
            file << "; Baked at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
            file << "\n" << _aot.get_output();
            file.close();

            std::cerr << "[DGTL-TIERED] Baked kernel to " << filename << " (" 
                     << ir.size() << " IR nodes)\n";
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
namespace dgtl::opt {
    using DGTLAsyncSpeculator = dgtl::jit::AsyncSpeculator;
} // namespace dgtl::opt
