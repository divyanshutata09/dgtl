/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.ir_core.code_cache;

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

class Code_Cache {
public:
    explicit Code_Cache(size_t max_size = 16 * 1024 * 1024) noexcept
        : max_size_(max_size), current_size_(0) {}
    ~Code_Cache() = default;

    Code_Cache(const Code_Cache&) = delete;
    Code_Cache& operator=(const Code_Cache&) = delete;

    [[nodiscard]] bool store(std::string_view key, const void* code, size_t size) {
        if (key.empty() || code == nullptr || size == 0 || size > max_size_) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_.find(key);
        const size_t existing_size = (it != cache_.end()) ? it->second.size : 0;
        size_t projected = current_size_ - existing_size + size;

        // 🚀 O(1) LRU EVICTION: Evict oldest cached entries if max_size_ threshold is exceeded
        while (projected > max_size_ && !lru_list_.empty()) {
            const auto& oldest_key = lru_list_.back();
            if (it != cache_.end() && oldest_key == it->second.key) {
                // If the only item left is the one we are replacing, break to update
                if (lru_list_.size() == 1) break;
            }
            
            auto erase_it = cache_.find(oldest_key);
            if (erase_it != cache_.end()) {
                current_size_ -= erase_it->second.size;
                cache_.erase(erase_it);
            }
            lru_list_.pop_back();
            projected = current_size_ - existing_size + size;
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
            std::string owned_key(key);
            lru_list_.push_front(owned_key);
            CacheNode node{
                owned_key,
                std::move(buf),
                size,
                lru_list_.begin()
            };
            cache_.emplace(std::string_view(node.key), std::move(node));
        }

        current_size_ = projected;
        return true;
    }

    [[nodiscard]] const void* retrieve(std::string_view key) const {
        if (key.empty()) {
            return nullptr;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.buffer.get() : nullptr;
    }

    [[nodiscard]] size_t retrieve_size(std::string_view key) const {
        if (key.empty()) {
            return 0;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return (it != cache_.end()) ? it->second.size : 0;
    }

    [[nodiscard]] bool retrieve_copy(std::string_view key, std::vector<uint8_t>& out) const {
        if (key.empty()) {
            return false;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end() || it->second.buffer == nullptr || it->second.size == 0) {
            return false;
        }
        const auto* p = it->second.buffer.get();
        out.assign(p, p + it->second.size);
        return true;
    }

    [[nodiscard]] bool exists(std::string_view key) const {
        if (key.empty()) {
            return false;
        }
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.find(key) != cache_.end();
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
};

} // namespace dgtl::execution::cache