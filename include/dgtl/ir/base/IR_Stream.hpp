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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 3.0 (TITAN-STABLE)
 * PILLAR 4: IR STREAM CORE IMPLEMENTATION WITH ARENA-BACKED SOA
 * DESIGN: 64-Byte Aligned Transactions for HFT-Grade Optimization Stability
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once

#include "IR_Types.hpp"
#include "IR_Nodes.hpp"
#include "IR_Traits.hpp"
#include "IR_Storage.hpp"
#include "dgtl/core/Arena.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>

namespace dgtl::ir {

// ======================================================
// 🔹 IR STREAM
// ======================================================
class alignas(64) IRStream {
private:
    size_t count = 0;
    size_t _capacity = 0;

    void refresh_joined_buffer_locked(bool trans) const noexcept;

    // Guaranteed 64-byte aligned pointers for SoA (Structure of Arrays)
    // Backed by dgtl::core::ThreadLocalArena for deterministic performance
    IRAtom* atoms = nullptr;
    IRCore* cores = nullptr;

    // Shadow buffers for transactional mutation (Pillar 2)
    IRAtom* shadow_atoms = nullptr;
    IRCore* shadow_cores = nullptr;

    size_t shadow_count = 0;

    // Atomic transaction state with release/acquire semantics
    std::atomic<bool> in_transaction{false};

    // FIX: track transaction state for joined buffer proxy
    mutable bool joined_buffer_in_transaction = false;

    // AoS fallback/proxy buffer
    mutable IRNode* joined_buffer = nullptr;
    mutable bool joined_valid = false;
    mutable IRNode fallback_node{};
    mutable std::recursive_mutex stream_mutex;

    // Memory Ownership Flags
    bool owns_aligned_alloc_storage = false;
    bool owns_joined_buffer = false;

public:
    IRStream() = default;

    IRStream([[maybe_unused]] dgtl::core::DGTL_Arena& arena, size_t node_count);
    IRStream(IRNode* external_buffer, size_t node_count);
    ~IRStream();

    IRStream(IRStream&& other) noexcept;
    IRStream& operator=(IRStream&& other) noexcept;

    [[nodiscard]] static IRStream allocate_transactional(size_t size);
    [[nodiscard]] static IRStream allocate_shadow(size_t size);

    // ======================================================
    // 🔹 BASICS
    // ======================================================
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] size_t capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool is_null() const noexcept;
    
    /**
     * @brief Get logical version based on structural hash
     * Critical for Analysis Manager invalidation.
     */
    [[nodiscard]] uint64_t get_version() const noexcept { return generate_logic_hash(); }

    void set_count(size_t v) noexcept;
    void reset() noexcept;
    void update_shadow_size(size_t s) noexcept;
    void set_shadow_count(size_t v) noexcept;

    // ======================================================
    // 🔹 ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] IRNode get_node(size_t idx) const noexcept;
    [[nodiscard]] const IRNode& get_node_readonly(size_t idx) const noexcept;

    [[nodiscard]] IRNode& get_node_ref(size_t idx) noexcept;

    [[nodiscard]] const IRAtom& get_atom_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core(size_t idx) const noexcept { return get_core_readonly(idx); }

    // ======================================================
    // 🔹 SHADOW ACCESS (FOR PASSES)
    // ======================================================
    IRAtom& get_shadow_atom(size_t idx) noexcept { return shadow_atoms[idx]; }
    IRCore& get_shadow_core(size_t idx) noexcept { return shadow_cores[idx]; }

    // ======================================================
    // 🔹 EMIT
    // ======================================================
    void push_node(const IRAtom& atom, const IRCore& core) noexcept;
    void push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint64_t h = 0) noexcept;
    void push_back(const IRNode& node) noexcept;
    void set_node(size_t idx, const IRNode& node) noexcept;

    // ======================================================
    // 🔹 TRANSACTION CORE
    // ======================================================
    void begin_transaction() noexcept;
    void commit_transaction() noexcept;
    void rollback_transaction() noexcept;
    void abort_transaction() noexcept;

    // ======================================================
    // 🔹 SHADOW MANAGEMENT
    // ======================================================
    void prune_shadow_buffer() noexcept;
    void push_full_to_shadow() noexcept;
    void commit_from_shadow(IRStream& shadow_stream) noexcept;
    void commit_from_shadow() noexcept;
    uint32_t reserve_shadow_slot() noexcept;
    void push_to_shadow(const IRNode& node) noexcept;
    [[nodiscard]] IRNode get_shadow_node_proxy(size_t idx) const noexcept;

    // ======================================================
    // 🔹 BUFFER ACCESS (STRICT ALIGNMENT HINTS)
    // ======================================================
    [[nodiscard]] IRNode* get_buffer_ptr() const noexcept;

    [[nodiscard]] bool hint_aligned_strike() const noexcept {
        if (!atoms || !cores) {
            return false;
        }

        return (reinterpret_cast<uintptr_t>(atoms) & 63) == 0 &&
               (reinterpret_cast<uintptr_t>(cores) & 63) == 0;
    }

    // ======================================================
    // 🔹 CLONING & VALIDATION
    // ======================================================
    void copy_from(const IRStream& other);
    [[nodiscard]] IRStream clone() const;
    [[nodiscard]] bool validate_shadow_integrity() const noexcept;
    [[nodiscard]] uint64_t generate_logic_hash() const noexcept;
    [[nodiscard]] uint32_t intent_crc() const noexcept;
    [[nodiscard]] uint32_t get_max_id() const noexcept;

    void push_shadow_fold(const IRNode& node) noexcept;
    void perform_gvn() noexcept;
    void perform_dead_code_elimination() noexcept;
    void evolve_node(IRNode* node);

    // ======================================================
    // 🔹 RAW ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] const IRAtom* raw_atoms() const noexcept { return atoms; }
    [[nodiscard]] const IRCore* raw_cores() const noexcept { return cores; }
    
    [[nodiscard]] IRAtom* raw_atoms() noexcept { 
        return in_transaction.load(std::memory_order_relaxed) ? shadow_atoms : atoms; 
    }
    [[nodiscard]] IRCore* raw_cores() noexcept { 
        return in_transaction.load(std::memory_order_relaxed) ? shadow_cores : cores; 
    }

    [[nodiscard]] bool is_active_transaction() const noexcept { 
        return in_transaction.load(std::memory_order_acquire); 
    }

    void prefetch_node(size_t idx) const noexcept {
        if (idx >= _capacity || !atoms || !cores) return;
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&atoms[idx], 0, 1);
        __builtin_prefetch(&cores[idx], 0, 1);
#else
        (void)idx;
#endif
    }

    // ======================================================
    // 🔹 ITERATOR
    // ======================================================
    struct Iterator {
        const IRStream* stream;
        size_t index;

        bool operator!=(const Iterator& o) const noexcept { return index != o.index; }
        void operator++() noexcept { ++index; }
        IRNode operator*() const noexcept { return stream->get_node(index); }
    };

    Iterator begin() const noexcept { return {this, 0}; }
    Iterator end() const noexcept { return {this, count}; }
};

// ======================================================
// 🔹 TRANSACTION GUARD (RAII SAFE)
// ======================================================
struct IRTransactionGuard {
    IRStream& stream;
    bool committed = false;

    explicit IRTransactionGuard(IRStream& s) : stream(s) {
        stream.begin_transaction();
    }

    void commit() {
        stream.commit_transaction();
        committed = true;
    }

    ~IRTransactionGuard() {
        if (!committed) {
            stream.rollback_transaction();
        }
    }
};

} // namespace dgtl::ir