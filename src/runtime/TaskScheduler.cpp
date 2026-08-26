/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <array>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <vector>

module dgtl.task_scheduler;

import dgtl.numa_runtime;
import dgtl.arena;

namespace dgtl::runtime {

// ============================================================================
// TaskBarrier Implementation
// ============================================================================

TaskBarrier::TaskBarrier(size_t count) noexcept : count_(count), waiting_(0), generation_(0) {}

TaskBarrier::~TaskBarrier() = default;

void TaskBarrier::wait() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    uint64_t gen = generation_;
    ++waiting_;
    
    if (waiting_ == count_) {
        waiting_ = 0;
        ++generation_;
        cv_.notify_all();
    } else {
        cv_.wait(lock, [this, gen]() { return generation_ != gen; });
    }
}

// Thread-local worker identity lets spawn() prefer the local queue without locks.
static thread_local TaskScheduler::WorkerState* tls_worker_state = nullptr;

// ============================================================================
// WorkQueue Implementation
// ============================================================================

TaskScheduler::WorkQueue::WorkQueue() noexcept {
    for (size_t i = 0; i < kCapacity; ++i) {
        slots_[i].sequence.store(i, std::memory_order_relaxed);
        slots_[i].task.store(nullptr, std::memory_order_relaxed);
    }
}

bool TaskScheduler::WorkQueue::try_push(Task* task) noexcept {
    if (!task) {
        return false;
    }

    size_t tail = tail_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[tail & (kCapacity - 1)];
        const size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const intptr_t delta = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(tail);

        if (delta == 0) {
            if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                slot.task.store(task, std::memory_order_relaxed);
                slot.sequence.store(tail + 1, std::memory_order_release);
                return true;
            }
        } else if (delta < 0) {
            return false;
        } else {
            tail = tail_.load(std::memory_order_relaxed);
        }
    }
}

bool TaskScheduler::WorkQueue::try_pop(Task*& task) noexcept {
    size_t head = head_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[head & (kCapacity - 1)];
        const size_t sequence = slot.sequence.load(std::memory_order_acquire);
        const intptr_t delta = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(head + 1);

        if (delta == 0) {
            if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                task = slot.task.load(std::memory_order_relaxed);
                slot.task.store(nullptr, std::memory_order_relaxed);
                slot.sequence.store(head + kCapacity, std::memory_order_release);
                return task != nullptr;
            }
        } else if (delta < 0) {
            return false;
        } else {
            head = head_.load(std::memory_order_relaxed);
        }
    }
}

bool TaskScheduler::WorkQueue::empty() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

// ============================================================================
// Task Implementation
// ============================================================================

TaskScheduler::Task::Task(TaskEntryFn fn_, void* ctx_) noexcept
    : fn(fn_), ctx(ctx_), state(static_cast<uint8_t>(TaskState::NotStarted)), 
      sequence_id(0), next(nullptr) {
}

TaskScheduler::Task::~Task() = default;

void TaskScheduler::Task::execute() noexcept {
    uint8_t expected = static_cast<uint8_t>(TaskState::NotStarted);
    if (!state.compare_exchange_strong(expected,
                                       static_cast<uint8_t>(TaskState::Running),
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return;
    }

    const TaskEntryFn entry = fn.load(std::memory_order_acquire);
    void* context = ctx.load(std::memory_order_acquire);
    if (entry) {
        entry(context);
    }

    state.store(static_cast<uint8_t>(TaskState::Complete), std::memory_order_release);
    state.notify_all(); 
}

bool TaskScheduler::Task::is_complete() const noexcept {
    return state.load(std::memory_order_acquire) == static_cast<uint8_t>(TaskState::Complete);
}

void TaskScheduler::Task::wait_complete() noexcept {
    uint8_t current_state = state.load(std::memory_order_acquire);
    while (current_state != static_cast<uint8_t>(TaskState::Complete)) {
        state.wait(current_state, std::memory_order_acquire);
        current_state = state.load(std::memory_order_acquire);
    }
}

void TaskScheduler::Task::reset(TaskEntryFn fn_, void* ctx_) noexcept {
    fn.store(fn_, std::memory_order_relaxed);
    ctx.store(ctx_, std::memory_order_relaxed);
    state.store(static_cast<uint8_t>(TaskState::NotStarted), std::memory_order_release);
    sequence_id = 0;
    next = nullptr;
}

// ============================================================================
// TaskScheduler Implementation
// ============================================================================

TaskScheduler::TaskScheduler(size_t worker_threads, size_t arena_mb_per_worker) noexcept
    : configured_worker_threads_(worker_threads),
      arena_mb_per_worker_(arena_mb_per_worker),
      shutdown_(false),
      task_pool_head_(nullptr) {
    
    initialize(configured_worker_threads_);
}

TaskScheduler::~TaskScheduler() noexcept {
    stop();
    destroy_all_tasks();
}

void TaskScheduler::initialize(size_t worker_threads) noexcept {
    if (is_running_.load(std::memory_order_acquire)) {
        stop();
    }

    size_t num_workers = worker_threads > 0 ? worker_threads : std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    configured_worker_threads_ = num_workers;

    worker_states_.clear();
    worker_states_.reserve(num_workers);
    for (size_t i = 0; i < num_workers; ++i) {
        worker_states_.push_back(std::make_unique<WorkerState>());
        worker_states_.back()->id = i;
    }

    is_initialized_.store(true, std::memory_order_release);
}

void TaskScheduler::start() noexcept {
    if (!is_initialized_.load(std::memory_order_acquire)) {
        initialize(configured_worker_threads_);
    }

    bool expected = false;
    if (!is_running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // Already running
    }

    shutdown_.store(false, std::memory_order_release);
    size_t num_workers = worker_states_.size();
    workers_.clear();
    workers_.reserve(num_workers);

    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back([this, i]() {
            const uint32_t worker_idx = static_cast<uint32_t>(i);
            (void)dgtl::runtime::numa::pin_thread_to_core(dgtl::runtime::numa::recommend_worker_core(worker_idx));
            (void)dgtl::runtime::numa::pin_current_thread_to_node(dgtl::runtime::numa::recommend_worker_node(worker_idx));
            worker_loop(*worker_states_[i]);
        });
    }
}

void TaskScheduler::stop() noexcept {
    bool expected = true;
    if (!is_running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return; // Not running
    }

    {
        std::lock_guard<std::mutex> lock(global_wait_mutex_);
        shutdown_.store(true, std::memory_order_release);
    }
    global_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void TaskScheduler::reconfigure(size_t worker_threads) noexcept {
    stop();
    initialize(worker_threads);
}

void TaskScheduler::reset_statistics() noexcept {
    total_tasks_spawned_.store(0, std::memory_order_relaxed);
    total_steals_.store(0, std::memory_order_relaxed);
    total_steal_attempts_.store(0, std::memory_order_relaxed);
    replay_tasks_enqueued_.store(0, std::memory_order_relaxed);
    replay_tasks_executed_.store(0, std::memory_order_relaxed);

    for (const auto& worker : worker_states_) {
        if (worker) {
            worker->tasks_executed = 0;
            worker->steal_attempts = 0;
            worker->steals_succeeded = 0;
        }
    }
}

uintptr_t TaskScheduler::spawn(TaskEntryFn fn, void* ctx) noexcept {
    if (!fn) return 0;
    
    Task* task = allocate_task(fn, ctx);
    if (!task) return 0;
    task->sequence_id = next_sequence_id_.fetch_add(1, std::memory_order_relaxed);

    total_tasks_spawned_.fetch_add(1, std::memory_order_relaxed);

    const bool deterministic = deterministic_replay_enabled();
    if (deterministic) {
        replay_tasks_enqueued_.fetch_add(1, std::memory_order_relaxed);
    }

    enqueue_task(task, tls_worker_state != nullptr && !deterministic);
    
    return reinterpret_cast<uintptr_t>(task);
}

void TaskScheduler::spawn_batch(const TaskEntryFn* fns, void* const* ctxs, size_t count) noexcept {
    if (!fns || !ctxs || count == 0) return;
    
    std::vector<Task*> tasks;
    tasks.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        if (fns[i]) {
            tasks.push_back(allocate_task(fns[i], ctxs[i]));
        }
    }
    
    total_tasks_spawned_.fetch_add(tasks.size(), std::memory_order_relaxed);
    const bool deterministic = deterministic_replay_enabled();
    if (deterministic) {
        replay_tasks_enqueued_.fetch_add(tasks.size(), std::memory_order_relaxed);
    }

    for (Task* task : tasks) {
        if (task) {
            task->sequence_id = next_sequence_id_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    for (auto* task : tasks) {
        enqueue_task(task, tls_worker_state != nullptr && !deterministic);
    }
}

void TaskScheduler::parallel_for(size_t start, size_t end, ParallelLoopFn fn, void* ctx) noexcept {
    if (!fn || start >= end) return;
    
    struct LoopTask {
        ParallelLoopFn fn;
        void* ctx{nullptr};
        size_t iteration{0};
    };
    
    if (end - start <= 128) {
        for (size_t i = start; i < end; ++i) {
            fn(ctx, i);
        }
        return;
    }
    
    size_t num_tasks = std::min(end - start, static_cast<size_t>(worker_states_.size() * 4));
    num_tasks = (num_tasks > 0) ? num_tasks : 1;
    
    auto loop_task_fn = [](void* raw) {
        auto* task = static_cast<LoopTask*>(raw);
        task->fn(task->ctx, task->iteration);
        delete task;
    };
    
    std::vector<uintptr_t> handles;
    handles.reserve(num_tasks);
    for (size_t i = start; i < end; ++i) {
        auto* loop_task = new LoopTask{fn, ctx, i};
        handles.push_back(spawn(loop_task_fn, loop_task));
    }
    
    for (auto handle : handles) {
        join(handle);
    }
}

uintptr_t TaskScheduler::create_task_group() noexcept {
    uintptr_t group_id = next_group_id_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(groups_mutex_);
    groups_[group_id] = std::make_unique<TaskGroup>();
    return group_id;
}

void TaskScheduler::spawn_in_group(uintptr_t group_handle, TaskEntryFn fn, void* ctx) noexcept {
    if (!fn) return;
    
    uintptr_t task_handle = spawn(fn, ctx);
    
    std::lock_guard<std::mutex> lock(groups_mutex_);
    auto it = groups_.find(group_handle);
    if (it != groups_.end()) {
        std::lock_guard<std::mutex> group_lock(it->second->mutex);
        it->second->tasks.push_back(task_handle);
    }
}

void TaskScheduler::join_group(uintptr_t group_handle) noexcept {
    std::vector<uintptr_t> tasks_to_join;
    
    {
        std::lock_guard<std::mutex> lock(groups_mutex_);
        auto it = groups_.find(group_handle);
        if (it != groups_.end()) {
            std::lock_guard<std::mutex> group_lock(it->second->mutex);
            tasks_to_join = it->second->tasks;
            groups_.erase(it);
        }
    }
    
    for (auto task_handle : tasks_to_join) {
        join(task_handle);
    }
}

RuntimeStats TaskScheduler::get_statistics() const noexcept {
    RuntimeStats stats;
    stats.total_tasks_spawned = total_tasks_spawned_.load(std::memory_order_relaxed);
    stats.tasks_stolen = total_steals_.load(std::memory_order_relaxed);
    stats.work_stealing_attempts = total_steal_attempts_.load(std::memory_order_relaxed);
    stats.deterministic_replay_enabled = deterministic_replay_enabled();
    stats.replay_tasks_enqueued = replay_tasks_enqueued_.load(std::memory_order_relaxed);
    stats.replay_tasks_executed = replay_tasks_executed_.load(std::memory_order_relaxed);
    
    for (const auto& worker : worker_states_) {
        if (worker) {
            stats.total_tasks_executed += worker->tasks_executed;
        }
    }
    
    return stats;
}

void TaskScheduler::set_scheduling_mode(SchedulingMode mode) noexcept {
    scheduling_mode_.store(static_cast<uint8_t>(mode), std::memory_order_release);
    if (mode == SchedulingMode::DeterministicReplay) {
        clear_replay_log();
        replay_tasks_enqueued_.store(0, std::memory_order_relaxed);
        replay_tasks_executed_.store(0, std::memory_order_relaxed);
    }
}

SchedulingMode TaskScheduler::get_scheduling_mode() const noexcept {
    return static_cast<SchedulingMode>(scheduling_mode_.load(std::memory_order_acquire));
}

bool TaskScheduler::deterministic_replay_enabled() const noexcept {
    return get_scheduling_mode() == SchedulingMode::DeterministicReplay;
}

std::vector<uint64_t> TaskScheduler::get_replay_log() const {
    std::lock_guard<std::mutex> lock(replay_mutex_);
    return replay_log_;
}

void TaskScheduler::clear_replay_log() noexcept {
    std::lock_guard<std::mutex> lock(replay_mutex_);
    replay_log_.clear();
}

void TaskScheduler::record_replay_event(uint64_t sequence_id) noexcept {
    if (!deterministic_replay_enabled()) {
        return;
    }
    std::lock_guard<std::mutex> lock(replay_mutex_);
    replay_log_.push_back(sequence_id);
    replay_tasks_executed_.fetch_add(1, std::memory_order_relaxed);
}

size_t TaskScheduler::get_worker_count() const noexcept {
    return worker_states_.size();
}

bool TaskScheduler::is_worker_thread() const noexcept {
    return tls_worker_state != nullptr;
}

uintptr_t TaskScheduler::create_barrier(size_t count) noexcept {
    uintptr_t barrier_id = next_barrier_id_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(barriers_mutex_);
    barriers_[barrier_id] = std::make_unique<TaskBarrier>(count);
    return barrier_id;
}

void TaskScheduler::barrier_wait(uintptr_t barrier_handle) noexcept {
    std::lock_guard<std::mutex> lock(barriers_mutex_);
    auto it = barriers_.find(barrier_handle);
    if (it != barriers_.end()) {
        it->second->wait();
    }
}

void TaskScheduler::destroy_barrier(uintptr_t barrier_handle) noexcept {
    std::lock_guard<std::mutex> lock(barriers_mutex_);
    barriers_.erase(barrier_handle);
}

void* TaskScheduler::get_thread_local_data() const noexcept {
    if (tls_worker_state) {
        return tls_worker_state->thread_local_data;
    }
    return nullptr;
}

void TaskScheduler::set_thread_local_data(void* data) noexcept {
    if (tls_worker_state) {
        tls_worker_state->thread_local_data = data;
    }
}

void TaskScheduler::join(uint64_t task_handle) noexcept {
    Task* task = reinterpret_cast<Task*>(static_cast<uintptr_t>(task_handle));
    if (!task) return;
    
    if (tls_worker_state) {
        while (!task->is_complete()) {
            Task* work = nullptr;
            if (fetch_next_task(*tls_worker_state, work) && work) {
                record_replay_event(work->sequence_id);
                work->execute();
                tls_worker_state->tasks_executed++;
            } else {
                task->wait_complete();
                break;
            }
        }
    } else {
        task->wait_complete();
    }
    
    free_task(task);
}

void TaskScheduler::yield() noexcept {
    std::this_thread::yield();
}

TaskScheduler::Task* TaskScheduler::allocate_task(TaskEntryFn fn, void* ctx) noexcept {
    Task* task = task_pool_head_.load(std::memory_order_acquire);
    while (task && !task_pool_head_.compare_exchange_weak(task, task->next,
                                                          std::memory_order_release,
                                                          std::memory_order_relaxed));
    if (task) {
        task->reset(fn, ctx);
        return task;
    }
    return new Task(fn, ctx);
}

void TaskScheduler::free_task(Task* task) noexcept {
    if (!task) return;
    Task* head = task_pool_head_.load(std::memory_order_relaxed);
    do {
        task->next = head;
    } while (!task_pool_head_.compare_exchange_weak(head, task,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed));
}

void TaskScheduler::destroy_all_tasks() noexcept {
    Task* current = task_pool_head_.load(std::memory_order_acquire);
    while (current) {
        Task* next = current->next;
        delete current;
        current = next;
    }
    task_pool_head_.store(nullptr, std::memory_order_release);
}

bool TaskScheduler::steal_task(WorkerState& self, Task*& out_task) noexcept {
    if (deterministic_replay_enabled()) {
        return false;
    }
    size_t num_workers = worker_states_.size();
    total_steal_attempts_.fetch_add(1, std::memory_order_relaxed);
    
    for (size_t i = 1; i < num_workers; ++i) {
        size_t target_idx = (self.id + i) % num_workers;
        const auto& target_state = worker_states_[target_idx];

        if (target_state->queue.try_pop(out_task)) {
            total_steals_.fetch_add(1, std::memory_order_relaxed);
            self.steals_succeeded++;
            return true;
        }
        self.steal_attempts++;
    }
    return false;
}

bool TaskScheduler::fetch_next_task(WorkerState& worker, Task*& out_task) noexcept {
    if (deterministic_replay_enabled()) {
        return global_queue_.try_pop(out_task);
    }

    if (worker.queue.try_pop(out_task)) {
        return true;
    }
    
    if (global_queue_.try_pop(out_task)) {
        return true;
    }

    return steal_task(worker, out_task);
}

void TaskScheduler::worker_loop(WorkerState& worker) noexcept {
    tls_worker_state = &worker;
    
    while (!shutdown_.load(std::memory_order_acquire)) {
        Task* task = nullptr;
        
        if (fetch_next_task(worker, task) && task) {
            record_replay_event(task->sequence_id);
            task->execute();
            worker.tasks_executed++;
        } else {
            std::unique_lock<std::mutex> lock(global_wait_mutex_);
            global_cv_.wait(lock, [this]() {
                return shutdown_.load(std::memory_order_acquire) || has_pending_work();
            });
        }
    }
}

void TaskScheduler::enqueue_task(Task* task, bool prefer_local) noexcept {
    if (!task) {
        return;
    }

    if (prefer_local && tls_worker_state) {
        if (tls_worker_state->queue.try_push(task)) {
            global_cv_.notify_one();
            return;
        }
    }

    while (!global_queue_.try_push(task)) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return;
        }
        std::this_thread::yield();
    }

    global_cv_.notify_one();
}

bool TaskScheduler::has_pending_work() const noexcept {
    if (!global_queue_.empty()) {
        return true;
    }

    for (const auto& worker : worker_states_) {
        if (worker && !worker->queue.empty()) {
            return true;
        }
    }

    return false;
}

} // namespace dgtl::runtime