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
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>
#include <mutex>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include "dgtl/jit/runtime/JIT_Executor.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/jit/speculation/Async_Speculator.hpp"
#include "dgtl/core/Symbol_Table.hpp"

namespace dgtl::jit {

/**
 * @class TieredController
 * @brief Manages tiered compilation: JIT for hot code, AOT baking for stabilized hot loops.
 * 
 * Tracks execution counts and when a function exceeds BAKE_THRESHOLD, 
 * it's serialized to disk as AOT-compiled assembly.
 */
class TieredController {
    dgtl::exec::DGTLJitExecutor& _jit;           ///< Reference to JIT executor engine
    dgtl::isa::DGTLNasmEmitter _aot;             ///< AOT emitter for baking to disk
    std::unordered_map<uintptr_t, uint32_t> _hot_counts;  ///< Execution count tracking
    mutable std::mutex _profile_mtx;             ///< Thread-safe profiling (mutable for const methods)
    static constexpr uint32_t BAKE_THRESHOLD = 10000;  ///< Executions before AOT bake

public:
    /**
     * @brief Constructor binds reference to JIT executor.
     * @param jit Reference to the JIT execution engine
     */
    explicit TieredController(dgtl::exec::DGTLJitExecutor& jit) 
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
    void profile_and_check(dgtl::isa::JitFunction func_ptr, const dgtl::core::DGTLSymbolTable& symbols) {
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
    void bake_to_disk(uintptr_t addr, const dgtl::core::DGTLSymbolTable& symbols) {
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