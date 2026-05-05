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

#include "dgtl/pipeline/modes/AOT_Pipeline.hpp"
#include "dgtl/pipeline/modes/JIT_Pipeline.hpp"
#include "dgtl/runtime/TaskScheduler.hpp"
#include <memory>
#include <cstdint>
#include <chrono>
#include <stdexcept>

namespace dgtl {
namespace pipeline {

enum class HybridStatus : int32_t {
    Success = 0,
    NotInitialized = -1,
    ExecutionFailed = -2,
    NullStream = -3,
    ModeUnavailable = -4
};

enum class ExecutionTier {
    JIT_Fast = 0,
    AOT_Intensive = 1,
    Adaptive = 2
};

struct HybridStats {
    uint64_t jit_executions = 0;
    uint64_t aot_executions = 0;
    uint64_t tier_switches = 0;
    uint64_t total_execution_time_us = 0;
};

/**
 * @class Hybrid_Pipeline
 * @brief Dynamic tiered compilation pipeline with adaptive mode selection.
 * Routes cold execution branches to JIT for low latency, and hot loops to intensive AOT tiers.
 * Monitors performance metrics to intelligently switch execution modes.
 */
class Hybrid_Pipeline {
public:
    Hybrid_Pipeline() 
        : aot_pipeline_(std::make_unique<AOT_Pipeline>()),
          jit_pipeline_(std::make_unique<JIT_Pipeline>()),
          initialized_(false),
          current_tier_(ExecutionTier::JIT_Fast),
          stats_({}) {}
    
    ~Hybrid_Pipeline() = default;
    
    Hybrid_Pipeline(const Hybrid_Pipeline&) = delete;
    Hybrid_Pipeline& operator=(const Hybrid_Pipeline&) = delete;
    Hybrid_Pipeline(Hybrid_Pipeline&&) = default;
    Hybrid_Pipeline& operator=(Hybrid_Pipeline&&) = default;

    [[nodiscard]] HybridStatus initialize() noexcept {
        if (initialized_) return HybridStatus::Success;
        
        try {
            AOTStatus aot_status = aot_pipeline_->initialize();
            if (aot_status != AOTStatus::Success) {
                return HybridStatus::ExecutionFailed;
            }
            
            JITStatus jit_status = jit_pipeline_->initialize();
            if (jit_status != JITStatus::Success) {
                return HybridStatus::ExecutionFailed;
            }
            
            initialized_ = true;
            return HybridStatus::Success;
        } catch (...) {
            initialized_ = false;
            return HybridStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] HybridStatus execute(ir::IRStream& stream, ExecutionTier preferred_tier = ExecutionTier::Adaptive) noexcept {
        if (!initialized_) return HybridStatus::NotInitialized;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            HybridStatus status = HybridStatus::ExecutionFailed;
            ExecutionTier selected_tier = preferred_tier;
            
            if (preferred_tier == ExecutionTier::Adaptive) {
                selected_tier = select_adaptive_tier(stream);
            }
            
            if (selected_tier != current_tier_) {
                current_tier_ = selected_tier;
                stats_.tier_switches++;
            }
            
            if (current_tier_ == ExecutionTier::JIT_Fast) {
                JITStatus jit_status = jit_pipeline_->execute(stream);
                status = (jit_status == JITStatus::Success) ? HybridStatus::Success : HybridStatus::ExecutionFailed;
                if (status == HybridStatus::Success) stats_.jit_executions++;
            } else if (current_tier_ == ExecutionTier::AOT_Intensive) {
                AOTStatus aot_status = aot_pipeline_->execute(stream);
                status = (aot_status == AOTStatus::Success) ? HybridStatus::Success : HybridStatus::ExecutionFailed;
                if (status == HybridStatus::Success) stats_.aot_executions++;
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            stats_.total_execution_time_us += 
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            
            return status;
        } catch (...) {
            return HybridStatus::ExecutionFailed;
        }
    }

    [[nodiscard]] AOT_Pipeline& get_aot() noexcept { return *aot_pipeline_; }
    [[nodiscard]] JIT_Pipeline& get_jit() noexcept { return *jit_pipeline_; }
    [[nodiscard]] ExecutionTier get_current_tier() const noexcept { return current_tier_; }
    [[nodiscard]] const HybridStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    /**
     * @brief Get access to internal TaskScheduler for orchestrating hybrid execution.
     */
    [[nodiscard]] dgtl::runtime::TaskScheduler& get_scheduler() noexcept {
        return scheduler_;
    }

    /**
     * @brief Execute streams with concurrent tier selection.
     * Some streams route to JIT (low latency), others to AOT (high optimization).
     * All execute concurrently with automatic tier balancing.
     * Returns failure if ANY stream fails (all-or-nothing semantics).
     */
    [[nodiscard]] HybridStatus execute_multi_tier(ir::IRStream* streams, 
                                                  const ExecutionTier* tiers, 
                                                  size_t count) noexcept {
        if (!initialized_ || !streams || !tiers || count == 0) return HybridStatus::NotInitialized;
        
        struct TierTask {
            Hybrid_Pipeline* self;
            ir::IRStream* stream;
            ExecutionTier tier;
            HybridStatus* result_ptr;  // Output parameter for status
        };
        
        std::vector<HybridStatus> results(count, HybridStatus::ExecutionFailed);
        
        auto tier_execute_fn = [](void* raw) {
            auto* task = static_cast<TierTask*>(raw);
            if (task->stream && task->result_ptr) {
                if (task->tier == ExecutionTier::JIT_Fast) {
                    JITStatus jit_status = task->self->get_jit().execute(*task->stream);
                    *task->result_ptr = (jit_status == JITStatus::Success) 
                        ? HybridStatus::Success : HybridStatus::ExecutionFailed;
                } else if (task->tier == ExecutionTier::AOT_Intensive) {
                    AOTStatus aot_status = task->self->get_aot().execute(*task->stream);
                    *task->result_ptr = (aot_status == AOTStatus::Success) 
                        ? HybridStatus::Success : HybridStatus::ExecutionFailed;
                } else {
                    *task->result_ptr = HybridStatus::ModeUnavailable;
                }
            }
            delete task;
        };
        
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<uintptr_t> handles;
        
        for (size_t i = 0; i < count; ++i) {
            auto* task = new TierTask{this, &streams[i], tiers[i], &results[i]};
            handles.push_back(scheduler_.spawn(tier_execute_fn, task));
        }
        
        for (auto handle : handles) {
            scheduler_.join(handle);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        stats_.total_execution_time_us += 
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // Aggregate results: fail if ANY stream failed or execution tier is unavailable
        HybridStatus first_error = HybridStatus::Success;
        for (size_t i = 0; i < count; ++i) {
            if (results[i] != HybridStatus::Success) {
                first_error = results[i];
                break;
            }
        }
        
        return first_error;
    }

    /**
     * @brief Get barrier for multi-tier phase synchronization.
     * Language creators can coordinate phase transitions across tiers.
     */
    [[nodiscard]] uintptr_t create_tier_barrier(size_t tier_count) noexcept {
        return scheduler_.create_barrier(tier_count);
    }

    /**
     * @brief Wait at tier barrier for all competing tiers.
     */
    void tier_barrier_wait(uintptr_t barrier) noexcept {
        scheduler_.barrier_wait(barrier);
    }

    void set_tier(ExecutionTier tier) noexcept {
        if (tier != ExecutionTier::Adaptive) {
            current_tier_ = tier;
        }
    }

    void reset_stats() noexcept {
        stats_ = HybridStats{};
    }

private:
    [[nodiscard]] ExecutionTier select_adaptive_tier(const ir::IRStream& stream) const noexcept {
        // Adaptive heuristic: choose based on stream properties
        // For now, default to JIT for low latency critical paths
        // In production, this would analyze stream size, complexity, etc.
        if (stream.size() < 1000) {
            return ExecutionTier::JIT_Fast;
        }
        return ExecutionTier::AOT_Intensive;
    }

    std::unique_ptr<AOT_Pipeline> aot_pipeline_;
    std::unique_ptr<JIT_Pipeline> jit_pipeline_;
    dgtl::runtime::TaskScheduler scheduler_{12, 48};  // 12 worker threads for hybrid, 48MB per worker
    bool initialized_;
    ExecutionTier current_tier_;
    HybridStats stats_;
};

} // namespace pipeline
} // namespace dgtl
