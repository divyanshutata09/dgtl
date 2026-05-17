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

#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>   // Required for std::forward
#include <stdexcept> // Required for capacity exceptions

#include "dgtl/utils/hashing/Hashing.hpp"

namespace dgtl::utils::container {

template <typename Key, typename Value, size_t Capacity>
class FastFlatMap {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 for fast modulo");
    static_assert(Capacity >= 4,
                  "Capacity must be >= 4 for bounded probe windows and load-factor safety");

    enum class State : uint8_t { Empty, Filled, Tombstone };

    // HFT Optimization: Align to CPU cache lines (64 bytes) to prevent false 
    // sharing and maximize hardware prefetching efficiency.
    alignas(64) std::array<State, Capacity> states_{};
    alignas(64) std::array<Key, Capacity> keys_{};
    alignas(64) std::array<Value, Capacity> values_{};
    size_t size_ = 0;
    size_t tombstones_ = 0;

    static constexpr size_t kMaxActiveSlots = (Capacity * 3) / 4;

    [[nodiscard]] static constexpr size_t mask(size_t hash_val) noexcept {
        return hash_val & (Capacity - 1);
    }

    constexpr void compact_tombstones() noexcept {
        if (tombstones_ == 0) {
            return;
        }

        std::array<State, Capacity> new_states{};
        std::array<Key, Capacity> new_keys{};
        std::array<Value, Capacity> new_values{};
        new_states.fill(State::Empty);

        for (size_t i = 0; i < Capacity; ++i) {
            if (states_[i] != State::Filled) {
                continue;
            }

            size_t index = mask(dgtl::utils::hashing::hash(keys_[i]));
            while (new_states[index] == State::Filled) {
                index = mask(index + 1);
            }

            new_keys[index] = std::move(keys_[i]);
            new_values[index] = std::move(values_[i]);
            new_states[index] = State::Filled;
        }

        states_ = std::move(new_states);
        keys_ = std::move(new_keys);
        values_ = std::move(new_values);
        tombstones_ = 0;
    }

    [[nodiscard]] constexpr bool should_compact_before_insert() const noexcept {
        return tombstones_ > 0 && (size_ + tombstones_ >= kMaxActiveSlots || tombstones_ > (Capacity / 8));
    }

public:
    constexpr FastFlatMap() {
        states_.fill(State::Empty);
    }

    // ── Core Mutators ────────────────────────────────────────────────────────

    // Standard Insert: Returns false if the map is full OR if the key already exists.
    // HFT Optimization: Perfect forwarding (K&&, V&&) allows zero-copy insertion.
    template <typename K, typename V>
    [[nodiscard]] constexpr bool insert(K&& key, V&& value) noexcept {
        if (should_compact_before_insert()) {
            compact_tombstones();
        }
        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            return false;
        }

        size_t index = mask(dgtl::utils::hashing::hash(key));
        size_t first_tombstone = Capacity;
        size_t probes = 0;

        while (states_[index] != State::Empty && probes < Capacity) {
            if (states_[index] == State::Filled && keys_[index] == key) {
                return false; // Strict map semantics: do NOT overwrite existing key
            }
            if (first_tombstone == Capacity && states_[index] == State::Tombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (probes >= Capacity) [[unlikely]] {
            return false;
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = std::forward<K>(key);
        values_[index] = std::forward<V>(value);
        states_[index] = State::Filled;
        ++size_;
        return true;
    }

    // Insert or Assign: Explicitly overwrites the value if the key already exists.
    template <typename K, typename V>
    constexpr bool insert_or_assign(K&& key, V&& value) noexcept {
        size_t index = mask(dgtl::utils::hashing::hash(key));
        size_t first_tombstone = Capacity;
        size_t probes = 0;

        while (states_[index] != State::Empty && probes < Capacity) {
            if (states_[index] == State::Filled && keys_[index] == key) {
                values_[index] = std::forward<V>(value); // Overwrite existing
                return true;
            }
            if (first_tombstone == Capacity && states_[index] == State::Tombstone) {
                first_tombstone = index;
            }
            index = mask(index + 1);
            ++probes;
        }

        if (size_ >= kMaxActiveSlots) [[unlikely]] {
            if (should_compact_before_insert()) {
                compact_tombstones();
                return insert_or_assign(std::forward<K>(key), std::forward<V>(value));
            }
            return false;
        }

        if (probes >= Capacity) [[unlikely]] {
            if (should_compact_before_insert()) {
                compact_tombstones();
                return insert_or_assign(std::forward<K>(key), std::forward<V>(value));
            }
            return false;
        }

        if (first_tombstone != Capacity) {
            index = first_tombstone;
            --tombstones_;
        }

        keys_[index] = std::forward<K>(key);
        values_[index] = std::forward<V>(value);
        states_[index] = State::Filled;
        ++size_;
        return true;
    }

    constexpr void erase(const Key& key) noexcept {
        size_t index = mask(dgtl::utils::hashing::hash(key));
        size_t probes = 0;
        while (states_[index] != State::Empty && probes < Capacity) {
            if (states_[index] == State::Filled && keys_[index] == key) {
                states_[index] = State::Tombstone;
                --size_;
                ++tombstones_;
                return;
            }
            index = mask(index + 1);
            ++probes;
        }
    }

    constexpr void clear() noexcept {
        states_.fill(State::Empty);
        size_ = 0;
        tombstones_ = 0;
    }

    // ── Accessors & Lookups ──────────────────────────────────────────────────

    [[nodiscard]] constexpr Value* find(const Key& key) noexcept {
        size_t index = mask(dgtl::utils::hashing::hash(key));
        size_t probes = 0;
        while (states_[index] != State::Empty && probes < Capacity) {
            if (states_[index] == State::Filled && keys_[index] == key) {
                return &values_[index];
            }
            index = mask(index + 1);
            ++probes;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const Value* find(const Key& key) const noexcept {
        size_t index = mask(dgtl::utils::hashing::hash(key));
        size_t probes = 0;
        while (states_[index] != State::Empty && probes < Capacity) {
            if (states_[index] == State::Filled && keys_[index] == key) {
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

    [[nodiscard]] constexpr size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

    [[nodiscard]] constexpr size_t max_load() const noexcept {
        return kMaxActiveSlots;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] constexpr Value& operator[](const Key& key) {
        if (Value* val = find(key)) {
            return *val;
        }
        if (!insert(key, Value{})) {
            throw std::runtime_error("DGTL-E9001: FastFlatMap capacity exceeded");
        }
        return *find(key);
    }

    // ── Iterators ────────────────────────────────────────────────────────────
    
    struct Iterator {
        FastFlatMap* map;
        size_t index;

        constexpr Iterator& operator++() noexcept {
            do { ++index; } while (index < Capacity && map->states_[index] != State::Filled);
            return *this;
        }
        constexpr bool operator!=(const Iterator& other) const noexcept { return index != other.index; }
        constexpr std::pair<const Key&, Value&> operator*() noexcept { 
            return {map->keys_[index], map->values_[index]}; 
        }
    };

    struct ConstIterator {
        const FastFlatMap* map;
        size_t index;

        constexpr ConstIterator& operator++() noexcept {
            do { ++index; } while (index < Capacity && map->states_[index] != State::Filled);
            return *this;
        }
        constexpr bool operator!=(const ConstIterator& other) const noexcept { return index != other.index; }
        constexpr std::pair<const Key&, const Value&> operator*() const noexcept { 
            return {map->keys_[index], map->values_[index]}; 
        }
    };

    [[nodiscard]] constexpr Iterator begin() noexcept {
        size_t i = 0;
        while (i < Capacity && states_[i] != State::Filled) { ++i; }
        return {this, i};
    }

    [[nodiscard]] constexpr Iterator end() noexcept {
        return {this, Capacity};
    }

    [[nodiscard]] constexpr ConstIterator begin() const noexcept {
        size_t i = 0;
        while (i < Capacity && states_[i] != State::Filled) { ++i; }
        return {this, i};
    }

    [[nodiscard]] constexpr ConstIterator end() const noexcept {
        return {this, Capacity};
    }

    [[nodiscard]] constexpr ConstIterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr ConstIterator cend() const noexcept {
        return end();
    }
};

} // namespace dgtl::utils::container

namespace dgtl::utils {

template <typename Key, typename Value, size_t Capacity>
using FastFlatMap = dgtl::utils::container::FastFlatMap<Key, Value, Capacity>;

} // namespace dgtl::utils