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

export module dgtl.execution.cache;

import dgtl.mir;

export namespace dgtl::execution::cache {

struct StringViewHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct StringViewEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

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

    // Universal byte storage
    [[nodiscard]] bool store(std::string_view key, const void* code, size_t size) {
        if (key.empty() || code == nullptr || size == 0 || size > max_size_) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_.find(key);
        const size_t existing_size = (it != cache_.end()) ? it->second.size : 0;
        size_t projected = current_size_ - existing_size + size;

        // O(1) LRU EVICTION
        while (projected > max_size_ && !lru_list_.empty()) {
            const auto& oldest_key = lru_list_.back();
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

        auto buf = std::make_unique<uint8_t[]>(size);
        std::memcpy(buf.get(), code, size);

        if (it != cache_.end()) {
            lru_list_.erase(it->second.lru_it);
            lru_list_.push_front(it->second.key);
            it->second.buffer = std::move(buf);
            it->second.size = size;
            it->second.lru_it = lru_list_.begin();
        } else {
            lru_list_.push_front(std::string(key));
            auto lru_it = lru_list_.begin();
            CacheNode node{
                *lru_it,
                std::move(buf),
                size,
                lru_it
            };
            std::string_view stable_key(*lru_it);
            cache_.emplace(stable_key, std::move(node));
        }

        current_size_ = projected;
        return true;
    }

    [[nodiscard]] bool store(std::string_view key, std::span<const uint8_t> code) {
        return store(key, code.data(), code.size());
    }

    // Sovereign MIR MachineFunction caching
    [[nodiscard]] bool store(const machine::MachineFunction& fn, const void* code, size_t size) {
        return store(fn.name(), code, size);
    }

    [[nodiscard]] bool store(const machine::MachineFunction& fn, std::span<const uint8_t> code) {
        return store(fn.name(), code.data(), code.size());
    }

    [[nodiscard]] const void* retrieve(std::string_view key) const {
        if (key.empty()) {
            misses_count_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.buffer != nullptr) {
            hits_count_.fetch_add(1, std::memory_order_relaxed);
            return it->second.buffer.get();
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
        if (it != cache_.end() && it->second.buffer != nullptr) {
            hits_count_.fetch_add(1, std::memory_order_relaxed);
            return std::span<const uint8_t>(it->second.buffer.get(), it->second.size);
        }
        misses_count_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] bool retrieve_copy(std::string_view key, std::vector<uint8_t>& out) const {
        if (key.empty()) return false;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end() || it->second.buffer == nullptr || it->second.size == 0) {
            misses_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        hits_count_.fetch_add(1, std::memory_order_relaxed);
        const auto* p = it->second.buffer.get();
        out.assign(p, p + it->second.size);
        return true;
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

private:
    struct CacheNode {
        std::string key{};
        std::unique_ptr<uint8_t[]> buffer{nullptr};
        size_t size{0};
        std::list<std::string>::iterator lru_it{};
    };

    alignas(64) mutable std::shared_mutex mutex_;
    std::list<std::string> lru_list_;
    std::unordered_map<std::string_view, CacheNode, StringViewHash, StringViewEqual> cache_;
    size_t max_size_;
    size_t current_size_;
    mutable std::atomic<size_t> hits_count_{0};
    mutable std::atomic<size_t> misses_count_{0};
    mutable std::atomic<size_t> evictions_count_{0};
};

} // namespace dgtl::execution::cache


