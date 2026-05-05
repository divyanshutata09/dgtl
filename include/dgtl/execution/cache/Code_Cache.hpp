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

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>

namespace dgtl {
namespace execution {

class Code_Cache {
public:
    Code_Cache(size_t max_size = 16 * 1024 * 1024) noexcept 
        : max_size_(max_size), current_size_(0) {}
    ~Code_Cache() = default;

    [[nodiscard]] bool store(const char* key, const void* code, size_t size) noexcept {
        if (!key || !code || size == 0) return false;
        if (size > max_size_) return false;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string key_str(key);
        auto existing = cache_.find(key_str);
        if (existing != cache_.end()) {
            current_size_ -= existing->second.second;
        }
        
        if (size > max_size_ - current_size_) {
            if (existing != cache_.end()) {
                current_size_ += existing->second.second;
            }
            return false;
        }
        
        auto buf = std::make_unique<uint8_t[]>(size);
        std::memcpy(buf.get(), code, size);
        
        cache_[key_str] = {std::move(buf), size};
        current_size_ += size;
        return true;
    }

    [[nodiscard]] const void* retrieve(const char* key) const noexcept {
        if (!key) return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.first.get() : nullptr;
    }

    [[nodiscard]] bool exists(const char* key) const noexcept {
        if (!key) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.find(key) != cache_.end();
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        current_size_ = 0;
    }

    [[nodiscard]] size_t get_usage() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_size_;
    }

    [[nodiscard]] bool is_full() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_size_ >= max_size_;
    }

private:
    std::unordered_map<std::string, std::pair<std::unique_ptr<uint8_t[]>, size_t>> cache_;
    size_t max_size_;
    size_t current_size_;
    mutable std::mutex mutex_;
};

} // namespace execution
} // namespace dgtl
