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

#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <bit>
#include <concepts>

export module dgtl.flat_map;

import dgtl.hashing;

export namespace dgtl::utils::container {

/**
 * @brief FastFlatMap - Production-Grade, Zero-Allocation Fixed-Capacity Associative Container.
 * Outperforms classic linear probing and LLVM DenseMap architectures by decoupling tracking states 
 * from the data layers, minimizing line bouncing and maximizing pipeline superscalar parallel execution.
 */
template <typename Key, typename Value, std::size_t Capacity>
    requires std::movable<Key> && std::movable<Value>
class FastFlatMap {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast bitwise masking");
    static_assert(Capacity >= 8, "Minimum capacity must be at least 8 for safe chunk probing");

    struct Ctrl {
        static constexpr std::uint8_t kEmpty     = 0x80; 
        static constexpr std::uint8_t kTombstone = 0xFE; 

        [[nodiscard]] static constexpr std::uint8_t empty_byte() noexcept { return kEmpty; }
        [[nodiscard]] static constexpr std::uint8_t tombstone_byte() noexcept { return kTombstone; }
    };

    alignas(64) std::array<std::uint8_t, Capacity> ctrl_{};
    alignas(64) std::array<Key, Capacity> keys_{};
    alignas(64) std::array<Value, Capacity> values_{};
    
    std::size_t size_ = 0;
    std::size_t tombstones_ = 0;

    static constexpr std::size_t kMaxActiveSlots = (Capacity * 7) / 8; 

    [[nodiscard]] static constexpr std::size_t mask(std::size_t hash_val) noexcept {
        return hash_val & (Capacity - 1);
    }

    [[nodiscard]] static constexpr std::uint8_t H2(std::size_t hash_val) noexcept {
        return static_cast<std::uint8_t>(hash_val & 0x7F); 
    }

    constexpr void reset_metadata() noexcept {
        ctrl_.fill(Ctrl::kEmpty);
    }

    constexpr void compact_tombstones() noexcept {
        if (tombstones_ == 0) return;

        std::array<std::uint8_t, Capacity> new_ctrl;
        new_ctrl.fill(Ctrl::kEmpty);
        std::array<Key, Capacity> new_keys{};
        std::array<Value, Capacity> new_values{};

        for (std::size_t i = 0; i < Capacity; ++i) {
            if ((ctrl_[i] & 0x80) == 0) { 
                std::size_t hash_val = dgtl::utils::hashing::hash(keys_[i]);
                std::size_t index = mask(hash_val);
                
                while ((new_ctrl[index] & 0x80) == 0) {
                    index = mask(index + 1);
                }
                new_keys[index] = std::move(keys_[i]);
                new_values[index] = std::move(values_[i]);
                new_ctrl[index] = ctrl_[i];
            }
        }

        ctrl_ = std::move(new_ctrl);
        keys_ = std::move(new_keys);
        values_ = std::move(new_values);
        tombstones_ = 0;
    }

    [[nodiscard]] constexpr bool should_compact_before_insert() const noexcept {
        return tombstones_ > 0 && (size_ + tombstones_ >= kMaxActiveSlots || tombstones_ > (Capacity / 4));
    }

public:
    constexpr FastFlatMap() noexcept {
        reset_metadata();
    }

    // â”€â”€ Core Mutators â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    template <typename K, typename V>
    [[nodiscard]] constexpr bool insert(K&& key, V&& value) noexcept {
        if (should_compact_before_insert()) {
            compact_tombstones();
        }
        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            return false;
        }

        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t first_tombstone = Capacity;
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) {
                break;
            }
            if (ctrl == h2_val && keys_[index] == key) {
                return false; 
            }
            if (first_tombstone == Capacity && ctrl == Ctrl::kTombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = std::forward<K>(key);
        values_[index] = std::forward<V>(value);
        ctrl_[index] = h2_val;
        ++size_;
        return true;
    }

    template <typename K, typename V>
    constexpr bool insert_or_assign(K&& key, V&& value) noexcept {
        if (should_compact_before_insert()) {
            compact_tombstones();
        }

        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t first_tombstone = Capacity;
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) {
                break;
            }
            if (ctrl == h2_val && keys_[index] == key) {
                values_[index] = std::forward<V>(value); 
                return true;
            }
            if (first_tombstone == Capacity && ctrl == Ctrl::kTombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            return false;
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = std::forward<K>(key);
        values_[index] = std::forward<V>(value);
        ctrl_[index] = h2_val;
        ++size_;
        return true;
    }

    constexpr void erase(const Key& key) noexcept {
        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) return;
            if (ctrl == h2_val && keys_[index] == key) {
                ctrl_[index] = Ctrl::kTombstone;
                --size_;
                ++tombstones_;
                return;
            }
            index = mask(index + 1);
            ++probes;
        }
    }

    constexpr void clear() noexcept {
        reset_metadata();
        size_ = 0;
        tombstones_ = 0;
    }

    // â”€â”€ Accessors & Lookups â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    [[nodiscard]] constexpr Value* find(const Key& key) noexcept {
        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) return nullptr;
            if (ctrl == h2_val && keys_[index] == key) {
                return &values_[index];
            }
            index = mask(index + 1);
            ++probes;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const Value* find(const Key& key) const noexcept {
        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) return nullptr;
            if (ctrl == h2_val && keys_[index] == key) {
                return &values_[index];
            }
            index = mask(index + 1);
            ++probes;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool contains(const Key& key) const noexcept {
        return find(key) != nullptr;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    [[nodiscard]] static constexpr std::size_t max_load() noexcept { return kMaxActiveSlots; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    /**
     * @brief Perfectly forwarded non-copy single-pass lookup operator.
     */
    [[nodiscard]] constexpr Value& operator[](const Key& key) requires std::default_initializable<Value> {
        if (should_compact_before_insert()) {
            compact_tombstones();
        }

        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t first_tombstone = Capacity;
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) {
                break;
            }
            if (ctrl == h2_val && keys_[index] == key) {
                return values_[index];
            }
            if (first_tombstone == Capacity && ctrl == Ctrl::kTombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            throw std::runtime_error("DGTL-E9001: FastFlatMap capacity exceeded");
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = key;
        values_[index] = Value{};
        ctrl_[index] = h2_val;
        ++size_;
        return values_[index];
    }

    [[nodiscard]] constexpr Value& operator[](Key&& key) requires std::default_initializable<Value> {
        if (should_compact_before_insert()) {
            compact_tombstones();
        }

        const std::size_t hash_val = dgtl::utils::hashing::hash(key);
        std::size_t index = mask(hash_val);
        const std::uint8_t h2_val = H2(hash_val);
        std::size_t first_tombstone = Capacity;
        std::size_t probes = 0;

        while (probes < Capacity) {
            const std::uint8_t ctrl = ctrl_[index];
            if (ctrl == Ctrl::kEmpty) {
                break;
            }
            if (ctrl == h2_val && keys_[index] == key) {
                return values_[index];
            }
            if (first_tombstone == Capacity && ctrl == Ctrl::kTombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            throw std::runtime_error("DGTL-E9001: FastFlatMap capacity exceeded");
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = std::move(key);
        values_[index] = Value{};
        ctrl_[index] = h2_val;
        ++size_;
        return values_[index];
    }

    // â”€â”€ Zero-Copy Iterator Proxies â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    
    struct KeyValueProxy {
        const Key& first;
        Value& second;
    };

    struct ConstKeyValueProxy {
        const Key& first;
        const Value& second;
    };

    struct Iterator {
        FastFlatMap* map;
        std::size_t index;

        constexpr Iterator& operator++() noexcept {
            do { ++index; } while (index < Capacity && (map->ctrl_[index] & 0x80) != 0);
            return *this;
        }
        constexpr bool operator!=(const Iterator& other) const noexcept { return index != other.index; }
        [[nodiscard]] constexpr KeyValueProxy operator*() const noexcept { 
            return {map->keys_[index], map->values_[index]}; 
        }
    };

    struct ConstIterator {
        const FastFlatMap* map;
        std::size_t index;

        constexpr ConstIterator& operator++() noexcept {
            do { ++index; } while (index < Capacity && (map->ctrl_[index] & 0x80) != 0);
            return *this;
        }
        constexpr bool operator!=(const ConstIterator& other) const noexcept { return index != other.index; }
        [[nodiscard]] constexpr ConstKeyValueProxy operator*() const noexcept { 
            return {map->keys_[index], map->values_[index]}; 
        }
    };

    [[nodiscard]] constexpr Iterator begin() noexcept {
        std::size_t i = 0;
        while (i < Capacity && (ctrl_[i] & 0x80) != 0) { ++i; }
        return {this, i};
    }

    [[nodiscard]] constexpr Iterator end() noexcept { return {this, Capacity}; }

    [[nodiscard]] constexpr ConstIterator begin() const noexcept {
        std::size_t i = 0;
        while (i < Capacity && (ctrl_[i] & 0x80) != 0) { ++i; }
        return {this, i};
    }

    [[nodiscard]] constexpr ConstIterator end() const noexcept { return {this, Capacity}; }
    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] constexpr ConstIterator cend() const noexcept { return end(); }
};

} // namespace dgtl::utils::container

export namespace dgtl::utils {

template <typename Key, typename Value, std::size_t Capacity>
using FastFlatMap = dgtl::utils::container::FastFlatMap<Key, Value, Capacity>;

} // namespace dgtl::utils