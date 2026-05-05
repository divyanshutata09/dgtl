/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/runtime/TaskScheduler.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <map>

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

// Thread-Local pointer to allow spawn() to push to local queues lock-free(ish)
static thread_local TaskScheduler::WorkerState* tls_worker_state = nullptr;

// ============================================================================
// Task Implementation
// ============================================================================

TaskScheduler::Task::Task(TaskEntryFn fn_, void* ctx_) noexcept
    : fn(fn_), ctx(ctx_), state(static_cast<uint8_t>(TaskState::NotStarted)), 
      next(nullptr) {
}

TaskScheduler::Task::~Task() = default;

void TaskScheduler::Task::execute() noexcept {
    if (fn && ctx) {
        state.store(static_cast<uint8_t>(TaskState::Running), std::memory_order_release);
        fn(ctx);
        
        // C++20 atomic notification: Zero-overhead when uncontended
        state.store(static_cast<uint8_t>(TaskState::Complete), std::memory_order_release);
        state.notify_all(); 
    }
}

bool TaskScheduler::Task::is_complete() const noexcept {
    return state.load(std::memory_order_acquire) == static_cast<uint8_t>(TaskState::Complete);
}

void TaskScheduler::Task::wait_complete() noexcept {
    // C++20 atomic wait: Blocks efficiently without a heavy std::mutex/cv per task
    uint8_t current_state = state.load(std::memory_order_acquire);
    while (current_state != static_cast<uint8_t>(TaskState::Complete)) {
        state.wait(current_state, std::memory_order_acquire);
        current_state = state.load(std::memory_order_acquire);
    }
}

void TaskScheduler::Task::reset(TaskEntryFn fn_, void* ctx_) noexcept {
    fn = fn_;
    ctx = ctx_;
    state.store(static_cast<uint8_t>(TaskState::NotStarted), std::memory_order_relaxed);
    next = nullptr;
}

// ============================================================================
// TaskScheduler Implementation
// ============================================================================

TaskScheduler::TaskScheduler(size_t worker_threads, size_t) noexcept
    : shutdown_(false), task_pool_head_(nullptr) {
    
    size_t num_workers = worker_threads > 0 ? worker_threads : std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 1;
    
    worker_states_.reserve(num_workers);
    for (size_t i = 0; i < num_workers; ++i) {
        worker_states_.push_back(std::make_unique<WorkerState>());
        worker_states_.back()->id = i;
    }
    
    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back([this, i]() { worker_loop(*worker_states_[i]); });
    }
}

TaskScheduler::~TaskScheduler() noexcept {
    shutdown_.store(true, std::memory_order_release);
    global_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    destroy_all_tasks();
}

uintptr_t TaskScheduler::spawn(TaskEntryFn fn, void* ctx) noexcept {
    if (!fn) return 0;
    
    Task* task = allocate_task(fn, ctx);
    if (!task) return 0;

    total_tasks_spawned_.fetch_add(1, std::memory_order_relaxed);

    // Fast Path: If called from a worker thread, push to local queue
    if (tls_worker_state) {
        std::lock_guard<std::mutex> lock(tls_worker_state->mutex);
        tls_worker_state->queue.push_back(task);
        global_cv_.notify_one(); 
    } 
    // Slow Path: External thread submitting work
    else {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push_back(task);
        global_cv_.notify_one();
    }
    
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
    
    if (tls_worker_state) {
        std::lock_guard<std::mutex> lock(tls_worker_state->mutex);
        for (auto* task : tasks) {
            tls_worker_state->queue.push_back(task);
        }
        global_cv_.notify_one();
    } else {
        std::lock_guard<std::mutex> lock(global_mutex_);
        for (auto* task : tasks) {
            global_queue_.push_back(task);
        }
        global_cv_.notify_one();
    }
}

void TaskScheduler::parallel_for(size_t start, size_t end, ParallelLoopFn fn, void* ctx) noexcept {
    if (!fn || start >= end) return;
    
    struct LoopTask {
        ParallelLoopFn fn;
        void* ctx;
        size_t iteration;
    };
    
    // For small ranges, execute serially to avoid overhead
    if (end - start <= 128) {
        for (size_t i = start; i < end; ++i) {
            fn(ctx, i);
        }
        return;
    }
    
    // For large ranges, spawn tasks
    size_t num_tasks = std::min(end - start, static_cast<size_t>(worker_states_.size() * 4));
    size_t chunk_size = (end - start) / num_tasks;
    if (chunk_size == 0) chunk_size = 1;
    
    auto loop_task_fn = [](void* raw) {
        auto* task = static_cast<LoopTask*>(raw);
        task->fn(task->ctx, task->iteration);
        delete task;
    };
    
    std::vector<uintptr_t> handles;
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
    
    for (const auto& worker : worker_states_) {
        stats.total_tasks_executed += worker->tasks_executed;
    }
    
    return stats;
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
    
    // If called from a worker thread, actively execute other tasks while waiting
    if (tls_worker_state) {
        while (!task->is_complete()) {
            Task* work = nullptr;
            if (fetch_next_task(*tls_worker_state, work) && work) {
                work->execute();
                tls_worker_state->tasks_executed++;
            } else {
                std::this_thread::yield();
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
    size_t num_workers = worker_states_.size();
    total_steal_attempts_.fetch_add(1, std::memory_order_relaxed);
    
    for (size_t i = 1; i < num_workers; ++i) {
        size_t target_idx = (self.id + i) % num_workers;
        auto& target_state = worker_states_[target_idx];
        
        if (std::unique_lock<std::mutex> lock(target_state->mutex, std::try_to_lock); lock.owns_lock()) {
            if (!target_state->queue.empty()) {
                out_task = target_state->queue.front();
                target_state->queue.pop_front();
                total_steals_.fetch_add(1, std::memory_order_relaxed);
                self.steals_succeeded++;
                return true;
            }
        }
        self.steal_attempts++;
    }
    return false;
}

bool TaskScheduler::fetch_next_task(WorkerState& worker, Task*& out_task) noexcept {
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        if (!worker.queue.empty()) {
            out_task = worker.queue.back();
            worker.queue.pop_back();
            return true;
        }
    }
    
    {
        std::unique_lock<std::mutex> lock(global_mutex_, std::try_to_lock);
        if (lock.owns_lock() && !global_queue_.empty()) {
            out_task = global_queue_.front();
            global_queue_.pop_front();
            return true;
        }
    }
    
    return steal_task(worker, out_task);
}

void TaskScheduler::worker_loop(WorkerState& worker) noexcept {
    tls_worker_state = &worker;
    
    while (!shutdown_.load(std::memory_order_acquire)) {
        Task* task = nullptr;
        
        if (fetch_next_task(worker, task) && task) {
            task->execute();
            worker.tasks_executed++;
        } else {
            std::unique_lock<std::mutex> lock(global_mutex_);
            global_cv_.wait(lock, [this]() { 
                return !global_queue_.empty() || shutdown_.load(std::memory_order_acquire); 
            });
        }
    }
}

} // namespace dgtl::runtime
