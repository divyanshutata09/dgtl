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

enum class AOTStatus : int32_t {
    Success = 0,
    NotInitialized = -1,
    ExecutionFailed = -2,
    NullStream = -3,
    TimeoutExceeded = -4
};

struct AOTStats {
    uint64_t passes_executed = 0;
    uint64_t passes_skipped = 0;
    uint64_t total_nodes_optimized = 0;
    uint64_t execution_time_us = 0;
};

/**
 * @class AOT_Pipeline
 * @brief Ahead-Of-Time optimization pipeline enforcing strict deterministic ordering
 * and maximal optimization depths for static binary emission.
 * Optimized for Cloud/HPC workloads with maximum optimization intensity.
 */
class AOT_Pipeline {
public:
    AOT_Pipeline() 
        : manager_(std::make_unique<DGTLoptManager>(TargetPrecision::CLOUD)),
          initialized_(false),
          stats_({}) {}
    
    ~AOT_Pipeline() = default;
    
    AOT_Pipeline(const AOT_Pipeline&) = delete;
    AOT_Pipeline& operator=(const AOT_Pipeline&) = delete;
    AOT_Pipeline(AOT_Pipeline&&) = default;
    AOT_Pipeline& operator=(AOT_Pipeline&&) = default;

    [[nodiscard]] AOTStatus initialize() noexcept {
        if (initialized_) return AOTStatus::Success;
        
        try {
            // In AOT mode, we fully populate the manager with intensive transformations.
            // AOT implies Cloud/HPC precision with maximum speculation layers.
            // Passes are added by specialized pipeline builders or direct calls to add_opt().
            initialized_ = true;
            return AOTStatus::Success;
        } catch (...) {
            initialized_ = false;
            return AOTStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] AOTStatus execute(ir::IRStream& stream) noexcept {
        if (!initialized_) return AOTStatus::NotInitialized;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            bool success = manager_->execute(stream, TargetPrecision::CLOUD);
            
            auto end = std::chrono::high_resolution_clock::now();
            stats_.execution_time_us = 
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            
            return success ? AOTStatus::Success : AOTStatus::ExecutionFailed;
        } catch (...) {
            return AOTStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] DGTLoptManager& get_manager() noexcept { return *manager_; }
    [[nodiscard]] const AOTStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    /**
     * @brief Get access to internal TaskScheduler for concurrent compilation.
     * Language creators can spawn multiple compilation tasks.
     */
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept {
        return scheduler_;
    }

    /**
     * @brief Execute multiple IR streams concurrently using work stealing scheduler.
     * Ideal for batch compilation of multiple modules.
     * Returns failure if ANY compilation fails (all-or-nothing semantics).
     */
    [[nodiscard]] AOTStatus execute_batch(ir::IRStream* streams, size_t count) noexcept {
        if (!initialized_ || !streams || count == 0) return AOTStatus::NotInitialized;
        
        struct CompileTask {
            AOT_Pipeline* self;
            ir::IRStream* stream;
            AOTStatus* result_ptr;  // Output parameter for status
        };
        
        std::vector<AOTStatus> results(count, AOTStatus::ExecutionFailed);
        
        auto compile_fn = [](void* raw) {
            auto* task = static_cast<CompileTask*>(raw);
            if (task->stream) {
                *task->result_ptr = task->self->execute(*task->stream);
            }
            delete task;
        };
        
        std::vector<uintptr_t> handles;
        for (size_t i = 0; i < count; ++i) {
            auto* task = new CompileTask{this, &streams[i], &results[i]};
            handles.push_back(scheduler_.spawn(compile_fn, task));
        }
        
        for (auto handle : handles) {
            scheduler_.join(handle);
        }
        
        // Aggregate results: fail if ANY compilation failed
        for (size_t i = 0; i < count; ++i) {
            if (results[i] != AOTStatus::Success) {
                return results[i];  // Return first error encountered
            }
        }
        
        return AOTStatus::Success;
    }

    void reset_stats() noexcept {
        stats_ = AOTStats{};
    }

private:
    std::unique_ptr<DGTLoptManager> manager_;
    dgtl::runtime::TaskScheduler scheduler_{4, 64};  // 4 worker threads, 64MB per worker
    bool initialized_;
    AOTStats stats_;
};

} // namespace pipeline
} // namespace dgtl
