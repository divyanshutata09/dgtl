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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 (DGTL Edition)
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * IR Storage: Arena-backed SoA & Shadow-Buffer Logic
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#include <cstdlib>   // aligned_alloc / free / _aligned_malloc

#include "dgtl/core/Arena.hpp"
#include "IR_Types.hpp"
#include "IR_Nodes.hpp"
#include "IR_Traits.hpp"

namespace dgtl::ir {

namespace detail {

#if defined(__GNUC__) || defined(__clang__)
    #define DGTL_IR_STORAGE_RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define DGTL_IR_STORAGE_RESTRICT __restrict
#else
    #define DGTL_IR_STORAGE_RESTRICT
#endif

constexpr size_t kIRStorageCopyPrefetchDistance = 32;
constexpr size_t kIRStorageCopyUnrollFactor = 8;

inline void copy_atom_core_stream(
    IRAtom* DGTL_IR_STORAGE_RESTRICT dst_atoms,
    IRCore* DGTL_IR_STORAGE_RESTRICT dst_cores,
    const IRAtom* DGTL_IR_STORAGE_RESTRICT src_atoms,
    const IRCore* DGTL_IR_STORAGE_RESTRICT src_cores,
    size_t count) noexcept
{
    if (!dst_atoms || !dst_cores || !src_atoms || !src_cores || count == 0) {
        return;
    }

    size_t i = 0;
    for (; i + kIRStorageCopyUnrollFactor <= count; i += kIRStorageCopyUnrollFactor) {
#if defined(__GNUC__) || defined(__clang__)
        if (i + kIRStorageCopyPrefetchDistance < count) {
            __builtin_prefetch(&src_atoms[i + kIRStorageCopyPrefetchDistance], 0, 1);
            __builtin_prefetch(&src_cores[i + kIRStorageCopyPrefetchDistance], 0, 1);
            __builtin_prefetch(&dst_atoms[i + kIRStorageCopyPrefetchDistance], 1, 1);
            __builtin_prefetch(&dst_cores[i + kIRStorageCopyPrefetchDistance], 1, 1);
        }
#endif
        dst_atoms[i + 0] = src_atoms[i + 0];
        dst_atoms[i + 1] = src_atoms[i + 1];
        dst_atoms[i + 2] = src_atoms[i + 2];
        dst_atoms[i + 3] = src_atoms[i + 3];
        dst_atoms[i + 4] = src_atoms[i + 4];
        dst_atoms[i + 5] = src_atoms[i + 5];
        dst_atoms[i + 6] = src_atoms[i + 6];
        dst_atoms[i + 7] = src_atoms[i + 7];

        dst_cores[i + 0] = src_cores[i + 0];
        dst_cores[i + 1] = src_cores[i + 1];
        dst_cores[i + 2] = src_cores[i + 2];
        dst_cores[i + 3] = src_cores[i + 3];
        dst_cores[i + 4] = src_cores[i + 4];
        dst_cores[i + 5] = src_cores[i + 5];
        dst_cores[i + 6] = src_cores[i + 6];
        dst_cores[i + 7] = src_cores[i + 7];
    }

    for (; i < count; ++i) {
        dst_atoms[i] = src_atoms[i];
        dst_cores[i] = src_cores[i];
    }
}

} // namespace detail

/**
 * @struct IRStorage
 * @brief High-performance storage backend using a hybrid SoA (Structure of Arrays) 
 * and Transactional Shadow Buffer. Optimized for AVX-512 cache-line strides.
 */
struct IRStorage {
    // ================================
    // Ã°Å¸â€Â¹ PRIMARY (SoA View)
    // ================================
    IRAtom* atoms = nullptr; // Tier 1: Opcodes + Flags
    IRCore* cores = nullptr; // Tier 2: Operands + Hashes

    // ================================
    // Ã°Å¸â€Â¹ SHADOW (Transactional Buffer)
    // ================================
    IRAtom* shadow_atoms = nullptr;
    IRCore* shadow_cores = nullptr;

    // ================================
    // Ã°Å¸â€Â¹ DEBUG / AoS VIEW (Joined)
    // ================================
    mutable IRNode* joined_buffer = nullptr; // Unified 24-byte view

    size_t count = 0;
    size_t shadow_count = 0;
    size_t capacity = 0;

    std::atomic<bool> in_transaction{false};
    bool arena_backed = false;

    // ================================
    // Ã°Å¸â€Â¥ ALLOCATION (V4 COMPATIBLE)
    // ================================
    void allocate_aligned(size_t cap,
                          bool use_arena,
                          dgtl::core::DGTL_Arena* arena = nullptr)
    {
        capacity = (cap > 0) ? cap : 1;
        count = 0;
        shadow_count = 0;
        in_transaction.store(false, std::memory_order_release);
        arena_backed = use_arena;

        if (use_arena && arena) {
            // Ã¢Å“â€¦ DGTL_Arena V4 API: High-speed arena dispatch
            atoms         = arena->allocate<IRAtom>(capacity);
            cores         = arena->allocate<IRCore>(capacity);
            shadow_atoms  = arena->allocate<IRAtom>(capacity);
            shadow_cores  = arena->allocate<IRCore>(capacity);
            joined_buffer = arena->allocate<IRNode>(capacity);
        }
        else {
            // Ã¢Å“â€¦ Standard Aligned Allocation (64-byte for Cache-Line Alignment)
            auto round_up = [](size_t size, size_t align) {
                return (size + align - 1) & ~(align - 1);
            };

            size_t sz_atom = round_up(capacity * sizeof(IRAtom), 64);
            size_t sz_core = round_up(capacity * sizeof(IRCore), 64);
            size_t sz_node = round_up(capacity * sizeof(IRNode), 64);

#ifdef _WIN32
            atoms         = static_cast<IRAtom*>(_aligned_malloc(sz_atom, 64));
            cores         = static_cast<IRCore*>(_aligned_malloc(sz_core, 64));
            shadow_atoms  = static_cast<IRAtom*>(_aligned_malloc(sz_atom, 64));
            shadow_cores  = static_cast<IRCore*>(_aligned_malloc(sz_core, 64));
            joined_buffer = static_cast<IRNode*>(_aligned_malloc(sz_node, 64));
#else
            atoms         = static_cast<IRAtom*>(std::aligned_alloc(64, sz_atom));
            cores         = static_cast<IRCore*>(std::aligned_alloc(64, sz_core));
            shadow_atoms  = static_cast<IRAtom*>(std::aligned_alloc(64, sz_atom));
            shadow_cores  = static_cast<IRCore*>(std::aligned_alloc(64, sz_core));
            joined_buffer = static_cast<IRNode*>(std::aligned_alloc(64, sz_node));
#endif
        }

        // Ã¢Å“â€¦ SAFE ZERO INIT: Prevents junk opcodes in the stream
        if (atoms)         std::memset(atoms, 0, capacity * sizeof(IRAtom));
        if (cores)         std::fill(cores, cores + capacity, IRCore{});
        if (shadow_atoms) std::memset(shadow_atoms, 0, capacity * sizeof(IRAtom));
        if (shadow_cores) std::fill(shadow_cores, shadow_cores + capacity, IRCore{});
        if (joined_buffer) std::memset(joined_buffer, 0, capacity * sizeof(IRNode));
    }

    // ================================
    // Ã°Å¸â€Â¥ DEALLOC
    // ================================
    void deallocate() {
        if (arena_backed) return; // Arena lifetime is managed externally

#ifdef _WIN32
        if (atoms)         _aligned_free(atoms);
        if (cores)         _aligned_free(cores);
        if (shadow_atoms)  _aligned_free(shadow_atoms);
        if (shadow_cores)  _aligned_free(shadow_cores);
        if (joined_buffer) _aligned_free(joined_buffer);
#else
        if (atoms)         std::free(atoms);
        if (cores)         std::free(cores);
        if (shadow_atoms)  std::free(shadow_atoms);
        if (shadow_cores)  std::free(shadow_cores);
        if (joined_buffer) std::free(joined_buffer);
#endif

        // Reset pointers to prevent use-after-free
        atoms = nullptr;
        cores = nullptr;
        shadow_atoms = nullptr;
        shadow_cores = nullptr;
        joined_buffer = nullptr;

        count = 0;
        shadow_count = 0;
        capacity = 0;
        in_transaction.store(false, std::memory_order_release);
    }

    // ================================
    // Ã°Å¸â€Â¥ TRANSACTION COMMIT (OVERWRITE MODE)
    // ================================
    inline void commit_transaction() noexcept {
        if (shadow_count == 0) {
            in_transaction.store(false, std::memory_order_release);
            return;
        }

        if (!atoms || !cores || !shadow_atoms || !shadow_cores) {
            in_transaction.store(false, std::memory_order_release);
            return;
        }

        if (shadow_count <= capacity) {
            // Pillar 2: stream the shadow SoA back into primary SoA with aligned linear copies.
            detail::copy_atom_core_stream(atoms, cores, shadow_atoms, shadow_cores, shadow_count);

            count = shadow_count;
            shadow_count = 0;
        }

        in_transaction.store(false, std::memory_order_release);
    }
};

} // namespace dgtl::ir