/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <functional>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

export module dgtl.execution.cache;

import dgtl.mir;

export namespace dgtl::execution::cache {

struct StringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    [[nodiscard]] size_t operator()(const char* s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct StringEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

// Aliases for backwards compatibility
using StringViewHash = StringHash;
using StringViewEqual = StringEqual;

struct CacheStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
    size_t entries = 0;
    size_t current_bytes = 0;
    size_t max_bytes = 0;

    [[nodiscard]] double hit_ratio() const noexcept {
        size_t total = hits + misses;
        return total == 0 ? 0.0 : (static_cast<double>(hits) / static_cast<double>(total));
    }
};

class Code_Cache {
public:
    explicit Code_Cache(size_t max_size = 64 * 1024 * 1024) noexcept
        : max_size_(max_size), current_size_(0) {}
    ~Code_Cache() = default;

    Code_Cache(const Code_Cache&) = delete;
    Code_Cache& operator=(const Code_Cache&) = delete;

    Code_Cache(Code_Cache&&) = delete;
    Code_Cache& operator=(Code_Cache&&) = delete;

    // Universal storage with W^X executable memory option
    [[nodiscard]] bool store(std::string_view key, const void* code, size_t size, bool executable = true) {
        if (key.empty() || code == nullptr || size == 0 || size > max_size_) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_.find(key);
        const size_t existing_size = (it != cache_.end()) ? it->second.size : 0;
        size_t projected = current_size_ - existing_size + size;

        // O(1) LRU EVICTION
        while (projected > max_size_ && !lru_list_.empty()) {
            const std::string& oldest_key = lru_list_.back();
            if (it != cache_.end() && oldest_key == it->second.key) {
                if (lru_list_.size() == 1) break;
            }

            auto erase_it = cache_.find(oldest_key);
            if (erase_it != cache_.end()) {
                current_size_ -= erase_it->second.size;
                cache_.erase(erase_it);
                evictions_count_.fetch_add(1, std::memory_order_relaxed);
            }
            lru_list_.pop_back();

            it = cache_.find(key);
            const size_t cur_existing = (it != cache_.end()) ? it->second.size : 0;
            projected = current_size_ - cur_existing + size;
        }

        if (projected > max_size_) {
            return false;
        }

        void* exec_buf = nullptr;
        size_t exec_cap = 0;
        std::unique_ptr<uint8_t[]> heap_buf = nullptr;

        if (executable) {
            exec_cap = (size + 4095u) & ~static_cast<size_t>(4095u);
#if defined(_WIN32) || defined(_WIN64)
            exec_buf = VirtualAlloc(nullptr, exec_cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!exec_buf) return false;
            std::memcpy(exec_buf, code, size);
            DWORD old_prot = 0;
            if (!VirtualProtect(exec_buf, exec_cap, PAGE_EXECUTE_READ, &old_prot)) {
                VirtualFree(exec_buf, 0, MEM_RELEASE);
                return false;
            }
            FlushInstructionCache(GetCurrentProcess(), exec_buf, exec_cap);
#else
            exec_buf = mmap(nullptr, exec_cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (exec_buf == MAP_FAILED) return false;
            std::memcpy(exec_buf, code, size);
            if (mprotect(exec_buf, exec_cap, PROT_READ | PROT_EXEC) != 0) {
                munmap(exec_buf, exec_cap);
                return false;
            }
#if defined(__GNUC__) || defined(__clang__)
            __builtin___clear_cache(static_cast<char*>(exec_buf), static_cast<char*>(exec_buf) + exec_cap);
#endif
#endif
        } else {
            heap_buf = std::make_unique<uint8_t[]>(size);
            std::memcpy(heap_buf.get(), code, size);
        }

        if (it != cache_.end()) {
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
            it->second.cleanup();
            it->second.heap_buffer = std::move(heap_buf);
            it->second.exec_buffer = exec_buf;
            it->second.exec_capacity = exec_cap;
            it->second.size = size;
            it->second.is_executable = executable;
        } else {
            lru_list_.push_front(std::string(key));
            auto lru_it = lru_list_.begin();

            CacheNode node;
            node.key = *lru_it;
            node.heap_buffer = std::move(heap_buf);
            node.exec_buffer = exec_buf;
            node.exec_capacity = exec_cap;
            node.size = size;
            node.is_executable = executable;
            node.lru_it = lru_it;

            cache_.emplace(*lru_it, std::move(node));
        }

        current_size_ = projected;
        return true;
    }

    [[nodiscard]] bool store(std::string_view key, std::span<const uint8_t> code, bool executable = true) {
        return store(key, code.data(), code.size(), executable);
    }

    // Sovereign MIR MachineFunction caching
    [[nodiscard]] bool store(const machine::MachineFunction& fn, const void* code, size_t size, bool executable = true) {
        return store(fn.name(), code, size, executable);
    }

    [[nodiscard]] bool store(const machine::MachineFunction& fn, std::span<const uint8_t> code, bool executable = true) {
        return store(fn.name(), code.data(), code.size(), executable);
    }

    [[nodiscard]] const void* retrieve(std::string_view key) const {
        if (key.empty()) {
            misses_count_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.get_buffer() != nullptr) {
            hits_count_.fetch_add(1, std::memory_order_relaxed);
            return it->second.get_buffer();
        }
        misses_count_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    [[nodiscard]] const void* retrieve(const machine::MachineFunction& fn) const {
        return retrieve(fn.name());
    }

    [[nodiscard]] size_t retrieve_size(std::string_view key) const {
        if (key.empty()) return 0;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.size : 0;
    }

    [[nodiscard]] size_t retrieve_size(const machine::MachineFunction& fn) const {
        return retrieve_size(fn.name());
    }

    [[nodiscard]] std::span<const uint8_t> retrieve_span(std::string_view key) const {
        if (key.empty()) return {};
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.get_buffer() != nullptr) {
            hits_count_.fetch_add(1, std::memory_order_relaxed);
            return std::span<const uint8_t>(static_cast<const uint8_t*>(it->second.get_buffer()), it->second.size);
        }
        misses_count_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] bool retrieve_copy(std::string_view key, std::vector<uint8_t>& out) const {
        if (key.empty()) return false;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end() || it->second.get_buffer() == nullptr || it->second.size == 0) {
            misses_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        hits_count_.fetch_add(1, std::memory_order_relaxed);
        const auto* p = static_cast<const uint8_t*>(it->second.get_buffer());
        out.assign(p, p + it->second.size);
        return true;
    }

    [[nodiscard]] bool is_executable(std::string_view key) const {
        if (key.empty()) return false;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.is_executable : false;
    }

    [[nodiscard]] bool exists(std::string_view key) const {
        if (key.empty()) return false;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.find(key) != cache_.end();
    }

    [[nodiscard]] bool exists(const machine::MachineFunction& fn) const {
        return exists(fn.name());
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
        lru_list_.clear();
        current_size_ = 0;
    }

    [[nodiscard]] size_t get_usage() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return current_size_;
    }

    [[nodiscard]] bool is_full() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return current_size_ >= max_size_;
    }

    [[nodiscard]] CacheStats stats() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return CacheStats{
            hits_count_.load(std::memory_order_relaxed),
            misses_count_.load(std::memory_order_relaxed),
            evictions_count_.load(std::memory_order_relaxed),
            cache_.size(),
            current_size_,
            max_size_
        };
    }

    void reset_stats() noexcept {
        hits_count_.store(0, std::memory_order_relaxed);
        misses_count_.store(0, std::memory_order_relaxed);
        evictions_count_.store(0, std::memory_order_relaxed);
    }

private:
    struct CacheNode {
        std::string key{};
        std::unique_ptr<uint8_t[]> heap_buffer{nullptr};
        void* exec_buffer{nullptr};
        size_t exec_capacity{0};
        size_t size{0};
        bool is_executable{false};
        std::list<std::string>::iterator lru_it{};

        CacheNode() = default;
        ~CacheNode() {
            cleanup();
        }

        CacheNode(const CacheNode&) = delete;
        CacheNode& operator=(const CacheNode&) = delete;

        CacheNode(CacheNode&& other) noexcept
            : key(std::move(other.key)),
              heap_buffer(std::move(other.heap_buffer)),
              exec_buffer(other.exec_buffer),
              exec_capacity(other.exec_capacity),
              size(other.size),
              is_executable(other.is_executable),
              lru_it(other.lru_it) {
            other.exec_buffer = nullptr;
            other.exec_capacity = 0;
            other.size = 0;
            other.is_executable = false;
        }

        CacheNode& operator=(CacheNode&& other) noexcept {
            if (this != &other) {
                cleanup();
                key = std::move(other.key);
                heap_buffer = std::move(other.heap_buffer);
                exec_buffer = other.exec_buffer;
                exec_capacity = other.exec_capacity;
                size = other.size;
                is_executable = other.is_executable;
                lru_it = other.lru_it;

                other.exec_buffer = nullptr;
                other.exec_capacity = 0;
                other.size = 0;
                other.is_executable = false;
            }
            return *this;
        }

        void cleanup() noexcept {
            if (exec_buffer != nullptr) {
#if defined(_WIN32) || defined(_WIN64)
                VirtualFree(exec_buffer, 0, MEM_RELEASE);
#else
                munmap(exec_buffer, exec_capacity);
#endif
                exec_buffer = nullptr;
                exec_capacity = 0;
            }
        }

        [[nodiscard]] const void* get_buffer() const noexcept {
            return is_executable ? exec_buffer : heap_buffer.get();
        }

        [[nodiscard]] void* get_buffer() noexcept {
            return is_executable ? exec_buffer : heap_buffer.get();
        }
    };

    alignas(64) mutable std::shared_mutex mutex_;
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, CacheNode, StringHash, StringEqual> cache_;
    size_t max_size_;
    size_t current_size_;
    mutable std::atomic<size_t> hits_count_{0};
    mutable std::atomic<size_t> misses_count_{0};
    mutable std::atomic<size_t> evictions_count_{0};
};

} // namespace dgtl::execution::cache


