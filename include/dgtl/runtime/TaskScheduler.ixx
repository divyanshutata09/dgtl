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
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

export module dgtl.task_scheduler;

import dgtl.arena;

export namespace dgtl::runtime {

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

enum class SchedulingMode : uint8_t {
    AdaptiveWorkStealing = 0,
    DeterministicReplay = 1,
};

// Runtime statistics for monitoring
struct RuntimeStats {
    size_t total_tasks_executed = 0;
    size_t total_tasks_spawned = 0;
    size_t tasks_stolen = 0;
    size_t work_stealing_attempts = 0;
    uint64_t total_task_duration_us = 0;  // Microseconds
    bool deterministic_replay_enabled = false;
    size_t replay_tasks_enqueued = 0;
    size_t replay_tasks_executed = 0;
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

public:
    [[nodiscard]] size_t target_count() const noexcept { return count_; }
};

class TaskScheduler {
public:
    explicit TaskScheduler(size_t worker_threads = 0, size_t arena_mb_per_worker = 32) noexcept;
    
    ~TaskScheduler() noexcept;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Lifecycle Orchestration Methods (Expected by Runtime_Context)
    void initialize(size_t worker_threads) noexcept;
    void start() noexcept;
    void stop() noexcept;
    void reconfigure(size_t worker_threads) noexcept;
    void reset_statistics() noexcept;

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
    void set_scheduling_mode(SchedulingMode mode) noexcept;
    [[nodiscard]] SchedulingMode get_scheduling_mode() const noexcept;
    [[nodiscard]] bool deterministic_replay_enabled() const noexcept;
    [[nodiscard]] std::vector<uint64_t> get_replay_log() const;
    void clear_replay_log() noexcept;
    
    // Synchronization primitives
    uintptr_t create_barrier(size_t count) noexcept;
    void barrier_wait(uintptr_t barrier_handle) noexcept;
    void destroy_barrier(uintptr_t barrier_handle) noexcept;
    
    // Thread-local context for language runtimes
    void* get_thread_local_data() const noexcept;
    void set_thread_local_data(void* data) noexcept;

private:
    struct Task;

    struct WorkQueue {
        static constexpr size_t kCapacity = 4096;
        static_assert((kCapacity & (kCapacity - 1)) == 0, "WorkQueue capacity must be a power of two");

        WorkQueue() noexcept;

        [[nodiscard]] bool try_push(Task* task) noexcept;
        [[nodiscard]] bool try_pop(Task*& task) noexcept;
        [[nodiscard]] bool empty() const noexcept;

        struct Slot {
            std::atomic<size_t> sequence{0};
            std::atomic<Task*> task{nullptr};
        };

        alignas(64) std::array<Slot, kCapacity> slots_{};
        alignas(64) std::atomic<size_t> head_{0};
        alignas(64) std::atomic<size_t> tail_{0};

        [[nodiscard]] const std::array<Slot, kCapacity>& get_slots() const noexcept { return slots_; }
    };

    struct Task {
        std::atomic<TaskEntryFn> fn;
        std::atomic<void*> ctx;
        std::atomic<uint8_t> state;
        uint64_t sequence_id;
        Task* next;

        explicit Task(TaskEntryFn fn_, void* ctx_) noexcept;
        
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        
        ~Task();

        void execute() noexcept;
        
        [[nodiscard]] bool is_complete() const noexcept;
        
        void wait_complete() noexcept;

        void reset(TaskEntryFn fn_, void* ctx_) noexcept;

        [[nodiscard]] uint64_t get_sequence_id() const noexcept { return sequence_id; }
        [[nodiscard]] Task* get_next() const noexcept { return next; }
    };

    struct TaskGroup {
        std::vector<uintptr_t> tasks;
        std::mutex mutex;

        [[nodiscard]] size_t task_count() const noexcept { return tasks.size(); }
    };

public:
    struct WorkerState {
        WorkQueue queue;
        size_t id = 0;
        void* thread_local_data = nullptr;
        
        // Per-worker statistics
        size_t tasks_executed = 0;
        uint64_t steal_attempts = 0;
        uint64_t steals_succeeded = 0;
    };

    [[nodiscard]] const std::vector<std::unique_ptr<WorkerState>>& get_worker_states() const noexcept {
        return worker_states_;
    }

private:
    void record_replay_event(uint64_t sequence_id) noexcept;
    [[nodiscard]] bool steal_task(WorkerState& self, Task*& out_task) noexcept;
    
    [[nodiscard]] bool fetch_next_task(WorkerState& worker, Task*& out_task) noexcept;
    void enqueue_task(Task* task, bool prefer_local) noexcept;
    [[nodiscard]] bool has_pending_work() const noexcept;
    
    void worker_loop(WorkerState& worker) noexcept;
    
    [[nodiscard]] Task* allocate_task(TaskEntryFn fn, void* ctx) noexcept;
    
    void free_task(Task* task) noexcept;
    
    void destroy_all_tasks() noexcept;

    [[nodiscard]] size_t configured_worker_threads() const noexcept { return configured_worker_threads_; }
    [[nodiscard]] size_t arena_mb_per_worker() const noexcept { return arena_mb_per_worker_; }
    [[nodiscard]] size_t active_worker_count() const noexcept { return workers_.size(); }
    [[nodiscard]] size_t active_group_count() const noexcept { return groups_.size(); }
    [[nodiscard]] size_t active_barrier_count() const noexcept { return barriers_.size(); }
    [[nodiscard]] const std::vector<uint64_t>& get_replay_log_vector() const noexcept { return replay_log_; }

    size_t configured_worker_threads_{0};
    size_t arena_mb_per_worker_{32};
    std::atomic<bool> is_initialized_{false};
    std::atomic<bool> is_running_{false};

    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkerState>> worker_states_;

    WorkQueue global_queue_;
    std::condition_variable global_cv_;
    mutable std::mutex global_wait_mutex_;

    std::atomic<bool> shutdown_{false};
    std::atomic<Task*> task_pool_head_{nullptr};
    
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
    std::atomic<uint8_t> scheduling_mode_{static_cast<uint8_t>(SchedulingMode::AdaptiveWorkStealing)};
    std::atomic<uint64_t> next_sequence_id_{1};
    std::atomic<size_t> replay_tasks_enqueued_{0};
    std::atomic<size_t> replay_tasks_executed_{0};
    mutable std::mutex replay_mutex_;
    std::vector<uint64_t> replay_log_;
};

} // namespace dgtl::runtime