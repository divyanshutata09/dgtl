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

module;

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <atomic>
#include <thread>
#include <algorithm>

export module dgtl.runtime_context;

import dgtl.arena;
import dgtl.task_scheduler;

export namespace dgtl::runtime {

/**
 * @enum RuntimeState
 * @brief State machine for runtime execution lifecycle.
 */
enum class RuntimeState : uint8_t {
    Created = 0,    ///< Initial state after construction
    Running = 1,    ///< Actively executing workloads
    Paused = 2,     ///< Temporarily suspended
    Stopped = 3,    ///< Shut down cleanly
    Error = 4       ///< Encountered fatal error
};

/**
 * @class Runtime_Context
 * @brief Central runtime state manager for DGTL execution.
 * 
 * Manages execution lifecycle, thread coordination, memory arena,
 * and aggregated statistics.
 */
class Runtime_Context {
public:
    /**
     * @brief Create runtime context with given thread count.
     * @param thread_count Number of execution threads (0 = use hardware concurrency)
     * @param arena Optional external arena (uses internal if nullptr)
     */
    explicit Runtime_Context(uint32_t thread_count = 0, 
                             ::dgtl::core::DGTL_Arena* arena = nullptr)
        : state_(RuntimeState::Created),
          thread_count_(0),
          internal_arena_(nullptr),
          arena_(arena) {
        
        uint32_t hw = std::thread::hardware_concurrency();
        uint32_t chosen_threads = (thread_count == 0) ? (hw == 0 ? 1 : hw) : thread_count;
        thread_count_ = std::clamp(chosen_threads, 1u, 256u);

        if (arena_ == nullptr) {
            internal_arena_ = std::make_unique<::dgtl::core::DGTL_Arena>(64 * 1024 * 1024);
            arena_ = internal_arena_.get();
        }

        // Initialize embedded TaskScheduler with configured thread count
        scheduler_.initialize(thread_count_);
    }
    
    ~Runtime_Context() {
        shutdown();
    }

    Runtime_Context(const Runtime_Context&) = delete;
    Runtime_Context& operator=(const Runtime_Context&) = delete;

    /**
     * @brief Get current runtime state.
     */
    [[nodiscard]] RuntimeState get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /**
     * @brief Start the runtime and underlying task scheduler.
     */
    bool start() noexcept {
        RuntimeState expected = RuntimeState::Created;
        if (state_.compare_exchange_strong(expected, RuntimeState::Running, 
                                           std::memory_order_acq_rel)) {
            scheduler_.start();
            return true;
        }
        if (state_.load(std::memory_order_acquire) == RuntimeState::Running) {
            return true;
        }
        return false;
    }

    /**
     * @brief Gracefully shut down the runtime and worker thread pool.
     */
    void shutdown() noexcept {
        RuntimeState current = get_state();
        if (current != RuntimeState::Stopped) {
            state_.store(RuntimeState::Stopped, std::memory_order_release);
            scheduler_.stop();
        }
    }

    /**
     * @brief Transition runtime to new state explicitly.
     * @param state New state to enter
     */
    void set_state(RuntimeState state) noexcept {
        if (state == RuntimeState::Running) {
            start();
        } else if (state == RuntimeState::Stopped) {
            shutdown();
        } else {
            state_.store(state, std::memory_order_release);
        }
    }

    /**
     * @brief Get configured thread count.
     */
    [[nodiscard]] uint32_t get_thread_count() const noexcept {
        return thread_count_;
    }

    /**
     * @brief Configure thread count (must be done before entering Running state).
     * @param count New thread count (clamped to 1-256)
     */
    void set_thread_count(uint32_t count) noexcept {
        if (get_state() != RuntimeState::Running && count > 0) {
            thread_count_ = std::clamp(count, 1u, 256u);
            scheduler_.reconfigure(thread_count_);
        }
    }

    /**
     * @brief Get snapshot of current execution statistics.
     */
    [[nodiscard]] RuntimeStats get_stats() const noexcept {
        RuntimeStats s = scheduler_.get_statistics();
        s.total_tasks_spawned = total_tasks_spawned_.load(std::memory_order_relaxed);
        s.total_tasks_executed = total_tasks_executed_.load(std::memory_order_relaxed);
        s.total_task_duration_us = total_task_duration_us_.load(std::memory_order_relaxed);
        return s;
    }

    /**
     * @brief Reset all statistics counters to zero.
     */
    void reset_stats() noexcept {
        total_tasks_spawned_.store(0, std::memory_order_relaxed);
        total_tasks_executed_.store(0, std::memory_order_relaxed);
        total_task_duration_us_.store(0, std::memory_order_relaxed);
        scheduler_.reset_statistics();
    }

    /**
     * @brief Increment total task count safely across threads.
     */
    void increment_task_count() noexcept {
        total_tasks_spawned_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Increment completed task counter safely across threads.
     */
    void increment_completed_tasks() noexcept {
        total_tasks_executed_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Add execution time sample (in microseconds).
     */
    void add_execution_time_us(uint64_t time_us) noexcept {
        total_task_duration_us_.fetch_add(time_us, std::memory_order_relaxed);
    }

    /**
     * @brief Get underlying memory arena.
     */
    [[nodiscard]] ::dgtl::core::DGTL_Arena& get_arena() noexcept {
        return *arena_;
    }

    /**
     * @brief Check if runtime is actively executing.
     */
    [[nodiscard]] bool is_running() const noexcept {
        return get_state() == RuntimeState::Running;
    }

    /**
     * @brief Get access to embedded TaskScheduler.
     */
    [[nodiscard]] TaskScheduler& get_scheduler() noexcept {
        return scheduler_;
    }

    /**
     * @brief Get const access to embedded TaskScheduler.
     */
    [[nodiscard]] const TaskScheduler& get_scheduler() const noexcept {
        return scheduler_;
    }

    /**
     * @brief Spawn a task on the runtime's TaskScheduler.
     */
    [[nodiscard]] uintptr_t spawn_task(TaskEntryFn fn, void* ctx) noexcept {
        if (!is_running()) {
            // Auto-start runtime if work is spawned in Created state
            if (!start()) return 0;
        }
        increment_task_count();
        return scheduler_.spawn(fn, ctx);
    }

    /**
     * @brief Join a previously spawned task.
     * Blocks until task completion with active work stealing.
     */
    void join_task(uintptr_t task_handle) noexcept {
        if (task_handle == 0) return;
        scheduler_.join(task_handle);
        increment_completed_tasks();
    }

private:
    std::atomic<RuntimeState> state_;
    uint32_t thread_count_;

    // Prevent False Sharing across CPU cores via 64-byte alignment
    alignas(64) std::atomic<uint64_t> total_tasks_spawned_{0};
    alignas(64) std::atomic<uint64_t> total_tasks_executed_{0};
    alignas(64) std::atomic<uint64_t> total_task_duration_us_{0};

    std::unique_ptr<::dgtl::core::DGTL_Arena> internal_arena_;
    ::dgtl::core::DGTL_Arena* arena_;
    TaskScheduler scheduler_;
};

} // namespace dgtl::runtime