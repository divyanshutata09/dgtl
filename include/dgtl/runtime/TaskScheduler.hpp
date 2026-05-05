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

#include "dgtl/core/Arena.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dgtl::runtime {

using TaskEntryFn = void (*)(void* context);
using ParallelLoopFn = void (*)(void* context, size_t iteration);

// Task states
enum class TaskState : uint8_t {
    NotStarted = 0,
    Running = 1,
    Complete = 2
};

// Priority levels for task scheduling
enum class TaskPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2
};

// Runtime statistics for monitoring
struct RuntimeStats {
    size_t total_tasks_executed = 0;
    size_t total_tasks_spawned = 0;
    size_t tasks_stolen = 0;
    size_t work_stealing_attempts = 0;
    uint64_t total_task_duration_us = 0;  // Microseconds
};

// Barrier for phase synchronization
class TaskBarrier {
public:
    explicit TaskBarrier(size_t count) noexcept;
    ~TaskBarrier() noexcept;
    
    TaskBarrier(const TaskBarrier&) = delete;
    TaskBarrier& operator=(const TaskBarrier&) = delete;
    
    void wait() noexcept;

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t count_;
    size_t waiting_ = 0;
    uint64_t generation_ = 0;
};

class TaskScheduler {
public:
    explicit TaskScheduler(size_t worker_threads = 0, size_t arena_mb_per_worker = 32) noexcept;
    
    ~TaskScheduler() noexcept;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Core task operations
    uintptr_t spawn(TaskEntryFn fn, void* ctx) noexcept;
    
    void join(uint64_t task_handle) noexcept;
    
    void yield() noexcept;

    // Task batching - submit multiple tasks atomically
    void spawn_batch(const TaskEntryFn* fns, void* const* ctxs, size_t count) noexcept;

    // Parallel for loop - language creators frequently need this
    void parallel_for(size_t start, size_t end, ParallelLoopFn fn, void* ctx) noexcept;
    
    // Task groups for bulk synchronization
    uintptr_t create_task_group() noexcept;
    void spawn_in_group(uintptr_t group_handle, TaskEntryFn fn, void* ctx) noexcept;
    void join_group(uintptr_t group_handle) noexcept;
    
    // Runtime statistics and monitoring
    RuntimeStats get_statistics() const noexcept;
    size_t get_worker_count() const noexcept;
    bool is_worker_thread() const noexcept;
    
    // Synchronization primitives
    uintptr_t create_barrier(size_t count) noexcept;
    void barrier_wait(uintptr_t barrier_handle) noexcept;
    void destroy_barrier(uintptr_t barrier_handle) noexcept;
    
    // Thread-local context for language runtimes
    void* get_thread_local_data() const noexcept;
    void set_thread_local_data(void* data) noexcept;

private:
    struct Task {
        TaskEntryFn fn;
        void* ctx;
        std::atomic<uint8_t> state;
        Task* next;

        explicit Task(TaskEntryFn fn_, void* ctx_) noexcept;
        
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        
        ~Task();

        void execute() noexcept;
        
        [[nodiscard]] bool is_complete() const noexcept;
        
        void wait_complete() noexcept;

        void reset(TaskEntryFn fn_, void* ctx_) noexcept;
    };

    struct TaskGroup {
        std::vector<uintptr_t> tasks;
        std::mutex mutex;
    };

public:
    struct WorkerState {
        std::deque<Task*> queue;
        std::mutex mutex;
        size_t id = 0;
        void* thread_local_data = nullptr;
        
        // Per-worker statistics
        size_t tasks_executed = 0;
        uint64_t steal_attempts = 0;
        uint64_t steals_succeeded = 0;
    };

private:
    [[nodiscard]] bool steal_task(WorkerState& self, Task*& out_task) noexcept;
    
    [[nodiscard]] bool fetch_next_task(WorkerState& worker, Task*& out_task) noexcept;
    
    void worker_loop(WorkerState& worker) noexcept;
    
    [[nodiscard]] Task* allocate_task(TaskEntryFn fn, void* ctx) noexcept;
    
    void free_task(Task* task) noexcept;
    
    void destroy_all_tasks() noexcept;

    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkerState>> worker_states_;

    std::deque<Task*> global_queue_;
    std::mutex global_mutex_;
    std::condition_variable global_cv_;

    std::atomic<bool> shutdown_;
    std::atomic<Task*> task_pool_head_;
    
    // Task groups storage
    std::map<uintptr_t, std::unique_ptr<TaskGroup>> groups_;
    std::mutex groups_mutex_;
    std::atomic<uintptr_t> next_group_id_{1};
    
    // Barriers storage
    std::map<uintptr_t, std::unique_ptr<TaskBarrier>> barriers_;
    std::mutex barriers_mutex_;
    std::atomic<uintptr_t> next_barrier_id_{1};
    
    // Global statistics
    std::atomic<size_t> total_tasks_spawned_{0};
    std::atomic<size_t> total_steals_{0};
    std::atomic<size_t> total_steal_attempts_{0};
};

} // namespace dgtl::runtime