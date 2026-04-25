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

#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"     
#include "dgtl/ir/base/IR_Stream.hpp" 
#include "dgtl/core/Arena.hpp" 

#include <iostream>
#include <atomic>
#include <thread>
#include <vector>


namespace dgtl::ir {

namespace {

constexpr size_t kJoinedSyncPrefetchDistance = 32;
constexpr size_t kJoinedSyncUnrollFactor = 8;

inline void sync_joined_buffer_simd(IRNode* __restrict dst,
                               const IRAtom* __restrict atoms,
                               const IRCore* __restrict cores,
                               size_t count) noexcept {
    size_t i = 0;
#ifdef __AVX512F__
    for (; i + kJoinedSyncUnrollFactor <= count; i += kJoinedSyncUnrollFactor) {
#if defined(__GNUC__) || defined(__clang__)
        if (i + kJoinedSyncPrefetchDistance < count) {
            __builtin_prefetch(&atoms[i + kJoinedSyncPrefetchDistance], 0, 1);
            __builtin_prefetch(&cores[i + kJoinedSyncPrefetchDistance], 0, 1);
            __builtin_prefetch(&dst[i + kJoinedSyncPrefetchDistance], 1, 1);
        }
#endif
        #pragma GCC unroll 8
        for(size_t j = 0; j < kJoinedSyncUnrollFactor; ++j) {
            const size_t idx = i + j;
            dst[idx].opcode = atoms[idx].opcode;
            dst[idx].flags = atoms[idx].flags;
            dst[idx].result_id = cores[idx].result_id;
            const uint32_t mask = (atoms[idx].opcode == OpCode::NOP) ? 0u : 0xFFFFFFFFu;
            dst[idx].op_a = cores[idx].op_a & mask;
            dst[idx].op_b = cores[idx].op_b & mask;
            dst[idx].op_c = cores[idx].op_c & mask;
            dst[idx].intent_hash = cores[idx].intent_hash;
        }
    }
#endif
    for (; i < count; ++i) {
#if defined(__GNUC__) || defined(__clang__)
        if (i + kJoinedSyncPrefetchDistance < count) {
            __builtin_prefetch(&atoms[i + kJoinedSyncPrefetchDistance], 0, 1);
            __builtin_prefetch(&cores[i + kJoinedSyncPrefetchDistance], 0, 1);
        }
#endif
        dst[i].opcode = atoms[i].opcode;
        dst[i].flags = atoms[i].flags;
        dst[i].result_id = cores[i].result_id;
        if (atoms[i].opcode != OpCode::NOP) [[likely]] {
            dst[i].op_a = cores[i].op_a;
            dst[i].op_b = cores[i].op_b;
            dst[i].op_c = cores[i].op_c;
        } else {
            dst[i].op_a = 0;
            dst[i].op_b = 0;
            dst[i].op_c = 0;
        }
        dst[i].intent_hash = cores[i].intent_hash;
    }
}

} // namespace

// From File 2: Extern declaration for linking across units
extern dgtl::core::DGTL_Arena local_transaction_arena;

// --- Hot Path Operations (From File 3) ---

void IRStream::push_node(const IRAtom& atom, const IRCore& core) noexcept {
    push_node(atom.opcode, atom.flags, core.result_id, core.op_a, core.op_b, core.op_c, core.intent_hash);
}

void IRStream::push_back(const IRNode& node) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const uint64_t final_hash = (node.intent_hash == 0)
        ? IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c)
        : node.intent_hash;

    if (in_transaction.load(std::memory_order_acquire)) {
        uint32_t idx = reserve_shadow_slot();
        if (idx == 0xFFFFFFFF || !shadow_atoms || !shadow_cores) return;

        shadow_atoms[idx] = {node.opcode, node.flags};
        shadow_cores[idx] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash, 0, 0};
    }
    else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] {
            std::cerr << "[DGTL FATAL] IRStream Capacity Overflow at " << _capacity << "\n";
            return;
        }
        atoms[count].opcode = node.opcode;
        atoms[count].flags = node.flags;
        cores[count] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash, 0, 0};
        count++;
        shadow_count = count;
        joined_valid = false;
    }
}

void IRStream::push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint64_t h) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const uint64_t sync_hash = (h == 0)
        ? IRNode::calculate_hash(op, res, a, b, c)
        : h;
    bool trans = in_transaction.load(std::memory_order_acquire);

    if (trans) {
        if (shadow_count >= _capacity || !shadow_atoms || !shadow_cores) [[unlikely]] return;
        shadow_atoms[shadow_count] = { op, fl };
        shadow_cores[shadow_count] = { res, a, b, c, sync_hash, fl, 0 };
        shadow_count++;
    } else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] return;
        atoms[count] = { op, fl };
        cores[count] = { res, a, b, c, sync_hash, fl, 0 };
        count++;
        shadow_count = count;
        joined_valid = false;
    }
}

uint32_t IRStream::reserve_shadow_slot() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (shadow_count >= _capacity || !shadow_atoms) [[unlikely]] {
        std::cerr << "[DGTL FATAL] IRStream Shadow Overflow at " << _capacity << "\n";
        return 0xFFFFFFFF;
    }
    uint32_t idx = static_cast<uint32_t>(shadow_count);
    shadow_count++;
    return idx;
}

void IRStream::push_to_shadow(const IRNode& node) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (!in_transaction.load(std::memory_order_acquire)) {
        begin_transaction();
    }
    push_back(node);
}

// --- Accessors (From File 3, 6) ---

IRNode IRStream::get_node(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a = trans ? shadow_atoms : atoms;
    const auto* c = trans ? shadow_cores : cores;
    const size_t limit = trans ? shadow_count : count;

    if (idx >= limit || !a || !c) return {};
    return { a[idx].opcode, a[idx].flags, c[idx].result_id, c[idx].op_a, c[idx].op_b, c[idx].op_c, c[idx].intent_hash, 0u };
}

const IRNode& IRStream::get_node_readonly(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    fallback_node = get_node(idx);
    return fallback_node;
}

IRNode& IRStream::get_node_ref(size_t idx) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans = in_transaction.load(std::memory_order_acquire);
    const size_t current_count = trans ? shadow_count : count;
    if (idx >= current_count || !joined_buffer) {
        fallback_node = IRNode{};
        return fallback_node;
    }

    refresh_joined_buffer_locked(trans);
    return joined_buffer[idx];
}

void IRStream::set_node(size_t idx, const IRNode& node) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans = in_transaction.load(std::memory_order_acquire);
    IRAtom* atom_ptr = trans ? shadow_atoms : atoms;
    IRCore* core_ptr = trans ? shadow_cores : cores;
    const size_t limit = trans ? shadow_count : count;

    if (!atom_ptr || !core_ptr || idx >= limit) {
        return;
    }

    const uint64_t final_hash = (node.intent_hash == 0)
        ? IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c)
        : node.intent_hash;

    atom_ptr[idx] = {node.opcode, node.flags};
    core_ptr[idx] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash, node.flags, 0};

    if (joined_buffer && idx < _capacity) {
        joined_buffer[idx] = node;
        joined_buffer[idx].intent_hash = final_hash;
        joined_buffer_in_transaction = trans;
    }
    joined_valid = false;
}

const IRCore& IRStream::get_core_readonly(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    bool trans = in_transaction.load(std::memory_order_acquire);
    if (idx >= (trans ? shadow_count : count)) {
        static const IRCore null_core = {0u, 0u, 0u, 0u, 0ull, 0u, 0u};
        return null_core;
    }
    return trans ? shadow_cores[idx] : cores[idx];
}

const IRAtom& IRStream::get_atom_readonly(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    static const IRAtom null_atom{OpCode::NOP, Flags::NONE};

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const size_t limit = trans ? shadow_count : count;
    if (idx >= limit) {
        return null_atom;
    }

    return trans ? shadow_atoms[idx] : atoms[idx];
}

IRNode IRStream::get_shadow_node_proxy(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (idx >= _capacity || !shadow_atoms || !shadow_cores) return {};
    return { shadow_atoms[idx].opcode, shadow_atoms[idx].flags, shadow_cores[idx].result_id,
             shadow_cores[idx].op_a, shadow_cores[idx].op_b, shadow_cores[idx].op_c, shadow_cores[idx].intent_hash, 0u };
}

// --- Sync & Hash Logic (From File 4) ---

void IRStream::refresh_joined_buffer_locked(bool trans) const noexcept {
    const auto* active_atoms = trans ? shadow_atoms : atoms;
    const auto* active_cores = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!joined_buffer || !active_atoms || !active_cores || current_count == 0) {
        return;
    }
    if (joined_valid && joined_buffer_in_transaction == trans) {
        return;
    }

    sync_joined_buffer_simd(joined_buffer, active_atoms, active_cores, std::min(current_count, _capacity));
    joined_valid = true;
    joined_buffer_in_transaction = trans;
}

IRNode* IRStream::get_buffer_ptr() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    bool trans = in_transaction.load(std::memory_order_acquire);
    const size_t current_count = trans ? shadow_count : count;

    if (!joined_buffer || current_count == 0) return joined_buffer;
    refresh_joined_buffer_locked(trans);
    return joined_buffer;
}

bool IRStream::validate_shadow_integrity() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans = in_transaction.load(std::memory_order_acquire);
    const IRAtom* atom_ptr = trans ? shadow_atoms : atoms;
    const IRCore* core_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!atom_ptr || !core_ptr) {
        return current_count == 0;
    }

    const auto validate_range = [&](size_t begin, size_t end) -> bool {
        for (size_t i = begin; i < end; ++i) {
            const uint64_t expected = IRNode::calculate_hash(
                atom_ptr[i].opcode,
                core_ptr[i].result_id,
                core_ptr[i].op_a,
                core_ptr[i].op_b,
                core_ptr[i].op_c
            );
            if (core_ptr[i].intent_hash != expected) {
                return false;
            }
        }
        return true;
    };

    constexpr size_t kParallelValidationThreshold = 262144;
    const uint32_t hardware_threads = std::thread::hardware_concurrency();
    const size_t max_workers = current_count / 65536;
    const size_t worker_count = std::min<size_t>(
        hardware_threads == 0 ? 1u : hardware_threads,
        max_workers == 0 ? 1u : max_workers
    );

    if (current_count < kParallelValidationThreshold || worker_count <= 1) {
        return validate_range(0, current_count);
    }

    std::atomic<bool> valid{true};
    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    const size_t chunk_size = (current_count + worker_count - 1) / worker_count;

    for (size_t worker = 0; worker + 1 < worker_count; ++worker) {
        const size_t begin = worker * chunk_size;
        const size_t end = std::min(current_count, begin + chunk_size);
        workers.emplace_back([&, begin, end]() {
            if (!validate_range(begin, end)) {
                valid.store(false, std::memory_order_release);
            }
        });
    }

    const size_t tail_begin = (worker_count - 1) * chunk_size;
    if (!validate_range(tail_begin, current_count)) {
        valid.store(false, std::memory_order_release);
    }

    for (auto& worker : workers) {
        worker.join();
    }

    return valid.load(std::memory_order_acquire);
}

uint64_t IRStream::generate_logic_hash() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    constexpr uint64_t seed = 0x243F6A8885A308D3ULL;
    uint64_t h = seed;

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a_ptr = trans ? shadow_atoms : atoms;
    const auto* c_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!a_ptr || !c_ptr || current_count == 0) {
        return h;
    }

    for (size_t i = 0; i < current_count; ++i) {
        const uint64_t node_hash = IRNode::calculate_hash(
            a_ptr[i].opcode,
            c_ptr[i].result_id,
            c_ptr[i].op_a,
            c_ptr[i].op_b,
            c_ptr[i].op_c
        ) ^ (static_cast<uint64_t>(a_ptr[i].flags) << 48)
          ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL);

        h ^= node_hash + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    }
    return h;
}
} // namespace dgtl::ir
