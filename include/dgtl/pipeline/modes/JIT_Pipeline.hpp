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

#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/runtime/TaskScheduler.hpp"
#include <memory>
#include <cstdint>
#include <chrono>
#include <stdexcept>

namespace dgtl {
namespace pipeline {

enum class JITStatus : int32_t {
    Success = 0,
    NotInitialized = -1,
    ExecutionFailed = -2,
    NullStream = -3,
    TimeoutExceeded = -4,
    HotPathNotFound = -5
};

struct JITStats {
    uint64_t compilations = 0;
    uint64_t hot_paths_detected = 0;
    uint64_t speculative_optimizations = 0;
    uint64_t total_compilation_time_us = 0;
    double avg_compilation_time_us = 0.0;
};

/**
 * @class JIT_Pipeline
 * @brief Just-In-Time optimization pipeline tailored for High-Frequency Trading (HFT)
 * and latency-critical on-the-fly codegen.
 * Optimized for microsecond-scale execution with direct hardware dispatch.
 */
class JIT_Pipeline {
public:
    JIT_Pipeline() 
        : manager_(std::make_unique<DGTLoptManager>(TargetPrecision::HFT)),
          initialized_(false),
          stats_({}) {}
    
    ~JIT_Pipeline() = default;
    
    JIT_Pipeline(const JIT_Pipeline&) = delete;
    JIT_Pipeline& operator=(const JIT_Pipeline&) = delete;
    JIT_Pipeline(JIT_Pipeline&&) = default;
    JIT_Pipeline& operator=(JIT_Pipeline&&) = default;

    [[nodiscard]] JITStatus initialize() noexcept {
        if (initialized_) return JITStatus::Success;
        
        try {
            // JIT relies heavily on functional probes, fast paths, and hardware schedulers.
            // Drops deterministic checks in favor of straight-through execution.
            // Minimal pass set for sub-microsecond turnaround.
            initialized_ = true;
            return JITStatus::Success;
        } catch (...) {
            initialized_ = false;
            return JITStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] JITStatus execute(ir::IRStream& stream, uint64_t timeout_us = 1000000) noexcept {
        if (!initialized_) return JITStatus::NotInitialized;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            bool success = manager_->execute(stream, TargetPrecision::HFT);
            
            auto end = std::chrono::high_resolution_clock::now();
            uint64_t elapsed_us = 
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            
            if (elapsed_us > timeout_us) {
                return JITStatus::TimeoutExceeded;
            }
            
            stats_.compilations++;
            stats_.total_compilation_time_us += elapsed_us;
            stats_.avg_compilation_time_us = 
                static_cast<double>(stats_.total_compilation_time_us) / stats_.compilations;
            
            return success ? JITStatus::Success : JITStatus::ExecutionFailed;
        } catch (...) {
            return JITStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] DGTLoptManager& get_manager() noexcept { return *manager_; }
    [[nodiscard]] const JITStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    /**
     * @brief Get access to internal TaskScheduler for concurrent JIT compilation.
     * Language creators can spawn parallel JIT compilations for hot code paths.
     */
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept {
        return scheduler_;
    }

    /**
     * @brief Execute multiple IR streams concurrently with per-stream timeout enforcement.
     * Perfect for HFT scenarios where multiple code paths compile in parallel.
     * Returns failure if ANY stream times out or fails (all-or-nothing semantics).
     */
    [[nodiscard]] JITStatus execute_concurrent(ir::IRStream* streams, size_t count, 
                                               uint64_t timeout_us = 10000) noexcept {
        if (!initialized_ || !streams || count == 0) return JITStatus::NotInitialized;
        
        struct CompileTask {
            JIT_Pipeline* self;
            ir::IRStream* stream;
            uint64_t timeout_us;
            JITStatus* result_ptr;  // Output parameter for status
        };
        
        std::vector<JITStatus> results(count, JITStatus::ExecutionFailed);
        
        auto compile_fn = [](void* raw) {
            auto* task = static_cast<CompileTask*>(raw);
            if (task->stream && task->result_ptr) {
                *task->result_ptr = task->self->execute(*task->stream, task->timeout_us);
            }
            delete task;
        };
        
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<uintptr_t> handles;
        
        for (size_t i = 0; i < count; ++i) {
            auto* task = new CompileTask{this, &streams[i], timeout_us, &results[i]};
            handles.push_back(scheduler_.spawn(compile_fn, task));
        }
        
        for (auto handle : handles) {
            scheduler_.join(handle);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // Aggregate results: fail if ANY stream timed out or failed
        JITStatus first_error = JITStatus::Success;
        bool has_timeout = false;
        
        for (size_t i = 0; i < count; ++i) {
            if (results[i] == JITStatus::TimeoutExceeded) {
                has_timeout = true;
            } else if (results[i] != JITStatus::Success) {
                first_error = results[i];
            }
        }
        
        if (has_timeout) return JITStatus::TimeoutExceeded;
        if (first_error != JITStatus::Success) return first_error;
        if (total_elapsed_us > static_cast<int64_t>(timeout_us) * static_cast<int64_t>(count)) {
            return JITStatus::TimeoutExceeded;
        }
        return JITStatus::Success;
    }

    void reset_stats() noexcept {
        stats_ = JITStats{};
    }

private:
    std::unique_ptr<DGTLoptManager> manager_;
    dgtl::runtime::TaskScheduler scheduler_{8, 32};  // 8 worker threads for HFT, 32MB per worker
    bool initialized_;
    JITStats stats_;
};

} // namespace pipeline
} // namespace dgtl
