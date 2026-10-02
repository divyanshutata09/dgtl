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

/*
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 4: IR STREAM — FULL IMPLEMENTATION
 *
 * Merged from three translation units:
 * IR_Stream_Core.cpp        — lifecycle, allocation, move semantics
 * IR_Stream_Operations.cpp  — emit, access, sync/hash helpers
 * IR_Stream_Optimizations.cpp — passes (DCE, GVN, speculative, PGO, ...)
 *
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

module;

// ---------------------------------------------------------------------------
// Includes - Isolated inside the global module fragment
// ---------------------------------------------------------------------------
#include <immintrin.h>   // __builtin_prefetch / AVX-512 guards
#include <cstring>       // std::memset, std::memcpy
#include <algorithm>     // std::min, std::max, std::swap, std::fill
#include <atomic>        // std::atomic<bool> in parallel validation
#include <mutex>         // std::lock_guard
#include <thread>        // std::thread, hardware_concurrency
#include <vector>        // vector<thread>, vector<pair<>>, vector<int32_t>
#include <unordered_map> // profile/analysis maps
#include <iostream>      // std::cerr for fatal diagnostic paths
#include <cassert>
#include <limits>
#include <cassert>

#ifdef _WIN32
#  include <malloc.h>    // _aligned_malloc / _aligned_free
#else
#  include <stdlib.h>    // posix_memalign / free
#endif

// Binds this translation unit to the primary module definition interface
module dgtl.ir.stream;

import dgtl.ir_node;
import dgtl.ir_traits;
import dgtl.arena;
import dgtl.runtime.numa;
import dgtl.assertions;
import dgtl.flat_map;
import dgtl.hashing;

#ifndef DGTL_ASSERT_BOUNDS
#define DGTL_ASSERT_BOUNDS(index, size) assert((index) < (size))
#endif

#ifndef DGTL_ASSERT_NOT_NULL
#define DGTL_ASSERT_NOT_NULL(ptr) assert((ptr) != nullptr)
#endif

namespace dgtl::ir {

namespace detail {
inline void copy_atom_core_stream(IRAtom* dst_atoms,
                                  IRCore* dst_cores,
                                  const IRAtom* src_atoms,
                                  const IRCore* src_cores,
                                  size_t count) noexcept {
    if (!dst_atoms || !dst_cores || !src_atoms || !src_cores || count == 0) {
        return;
    }
    std::memcpy(dst_atoms, src_atoms, count * sizeof(IRAtom));
    std::memcpy(dst_cores, src_cores, count * sizeof(IRCore));
}
} // namespace detail

// ===========================================================================
// FILE-LOCAL HELPERS (anonymous namespace — zero external linkage cost)
// ===========================================================================
namespace {

// ---------------------------------------------------------------------------
// Platform-portable 64-byte-aligned allocator
// 64-byte boundaries prevent cache-line splits for AVX-512 / Neon strikes.
// ---------------------------------------------------------------------------
void* DGTL_aligned_alloc(size_t size, size_t alignment) noexcept {
    if (size == 0) size = alignment;
    const uint32_t node = dgtl::runtime::numa::get_current_node();
    return dgtl::runtime::numa::allocate_on_node(size, node, alignment);
}

void DGTL_aligned_free(void* ptr) noexcept {
    if (!ptr) return;
    dgtl::runtime::numa::free_allocation(ptr);
}

// ---------------------------------------------------------------------------
// Arena-first allocator with aligned fallback.
// Tries arena allocation; if the returned pointer is not 64-byte aligned
// (some arena implementations may not guarantee this), falls back to
// DGTL_aligned_alloc so the "strict aligned invariant" is always honoured.
// ---------------------------------------------------------------------------
template <typename T>
T* safe_allocate(dgtl::core::DGTL_Arena& arena, size_t count) noexcept {
    T* ptr = arena.allocate<T>(count);
    if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 64u == 0u)) {
        return ptr;
    }
    ptr = static_cast<T*>(DGTL_aligned_alloc(count * sizeof(T), 64u));
    if (!ptr) {
        std::cerr << "[DGTL FATAL] Aligned allocation failed for count: "
                  << count << "\n";
    }
    return ptr;
}

// ---------------------------------------------------------------------------
// SIMD-accelerated SoA → joined-buffer sync
// (IRAtom + IRCore) → IRNode, with unrolled prefetch under AVX-512.
// ---------------------------------------------------------------------------
constexpr size_t kJoinedSyncPrefetchDistance = 32u;
constexpr size_t kJoinedSyncUnrollFactor     = 8u;

void sync_joined_buffer_simd(IRNode* __restrict dst,
                             const IRAtom* __restrict atoms,
                             const IRCore* __restrict cores,
                             size_t count) noexcept
{
    size_t i = 0;

#ifdef __AVX512F__
    for (; i + kJoinedSyncUnrollFactor <= count; i += kJoinedSyncUnrollFactor) {
#if defined(__GNUC__) || defined(__clang__)
        if (i + kJoinedSyncPrefetchDistance < count) {
            __builtin_prefetch(&atoms[i + kJoinedSyncPrefetchDistance], 0, 1);
            __builtin_prefetch(&cores[i + kJoinedSyncPrefetchDistance], 0, 1);
            __builtin_prefetch(&dst  [i + kJoinedSyncPrefetchDistance], 1, 1);
        }
#endif
#pragma GCC unroll 8
        for (size_t j = 0; j < kJoinedSyncUnrollFactor; ++j) {
            const size_t idx  = i + j;
            const uint32_t mask = (atoms[idx].opcode == OpCode::NOP) ? 0u : 0xFFFFFFFFu;
            dst[idx].opcode      = atoms[idx].opcode;
            dst[idx].flags       = atoms[idx].flags;
            dst[idx].result_id   = cores[idx].result_id;
            dst[idx].op_a        = cores[idx].op_a  & mask;
            dst[idx].op_b        = cores[idx].op_b  & mask;
            dst[idx].op_c        = cores[idx].op_c  & mask;
            dst[idx].intent_hash = cores[idx].intent_hash;
            dst[idx].logic_flags = cores[idx].logic_flags;
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
        dst[i].opcode      = atoms[i].opcode;
        dst[i].flags       = atoms[i].flags;
        dst[i].result_id   = cores[i].result_id;
        if (atoms[i].opcode != OpCode::NOP) [[likely]] {
            dst[i].op_a = cores[i].op_a;
            dst[i].op_b = cores[i].op_b;
            dst[i].op_c = cores[i].op_c;
        } else {
            dst[i].op_a = 0u;
            dst[i].op_b = 0u;
            dst[i].op_c = 0u;
        }
        dst[i].intent_hash = cores[i].intent_hash;
        dst[i].logic_flags = cores[i].logic_flags;
    }
}

// ---------------------------------------------------------------------------
// Commutativity predicate for GVN canonicalization.
// Ensures (a + b) and (b + a) produce identical semantic hashes.
// ---------------------------------------------------------------------------
[[maybe_unused]] constexpr bool is_commutative_opcode(const OpCode op) noexcept {
    return op == OpCode::ADD_I32 || op == OpCode::MUL_I32 ||
           op == OpCode::AND_I32 || op == OpCode::OR_I32  ||
           op == OpCode::XOR_I32 ||
           op == OpCode::ADD_F32 || op == OpCode::MUL_F32;
}

[[maybe_unused]] constexpr bool is_cfg_terminator(const OpCode op) noexcept {
    if (op == OpCode::BRANCH || op == OpCode::BRANCH_COND ||
        op == OpCode::BRANCH_COMPARE_FUSED ||
        op == OpCode::BRANCH_LIKELY || op == OpCode::BRANCH_UNLIKELY ||
        op == OpCode::SWITCH_DISPATCH ||
        op == OpCode::RESUME_CTX ||
        op == OpCode::RET || op == OpCode::RETURN) {
        return true;
    }
    return IRTraits::is_terminator(op);
}

// ---------------------------------------------------------------------------
// Phase C: Semantic promotion from IRNode to IRCore
// Encodes address-space and predicate contract into aux_flags during stream insertion.
// This ensures all Phase C pointer/predicate invariants are enforced at the stream level.
// ---------------------------------------------------------------------------
[[nodiscard]] inline uint32_t compute_phase_c_aux_flags(OpCode op, uint16_t flags, uint32_t op_c) noexcept {
    // Address-space pointer ops: encode op_c (address-space id) into aux_flags
    switch (op) {
        case OpCode::ATOMIC_LOAD:
        case OpCode::ATOMIC_STORE:
        case OpCode::PTR_CAST:
        case OpCode::PTR_DIFF: {
            // op_c carries address-space id (0=generic, 1=global, 2=shared, 3=local, 4=constant)
            // Map to AuxFlags bits; enforce that op_c must be valid (not INVALID_SLOT)
            if (op_c != dgtl::ir::INVALID_SLOT) {
                // Store address-space id in lower 8 bits of aux_flags
                // Mark as valid address-space via PTR_ADDRSPACE_GENERIC (0x00000001 means "explicit address-space present")
                return (AuxFlags::PTR_ADDRSPACE_GENERIC | ((op_c & 0x0F) << 4));
            }
            break;
        }
        case OpCode::BOOL_SELECT:
            // Predicate ops: enforce IS_PREDICATED flag contract
            if ((flags & Flags::IS_PREDICATED) != 0u) {
                return AuxFlags::PREDICATE_CANONICAL;
            }
            break;
        default:
            break;
    }
    return 0u;
}

} // anonymous namespace

// ===========================================================================
// TU-SCOPE STORAGE
// ===========================================================================

/// Thread-local arena for shadow buffers — prevents mutex contention across
/// microservices that each operate on their own IR streams.
dgtl::core::DGTL_Arena local_transaction_arena(1);

// ===========================================================================
// SECTION 1: LIFECYCLE — Constructors, Destructor, Move
// ===========================================================================

IRStream::IRStream([[maybe_unused]] dgtl::core::DGTL_Arena& arena,
                   size_t node_count)
    : count(0)
    , _capacity((node_count > 0) ? node_count : 1024)
    , atoms(nullptr)
    , cores(nullptr)
    , shadow_atoms(nullptr)
    , shadow_cores(nullptr)
    , shadow_count(0)
    , in_transaction(false)
    , joined_buffer_in_transaction(false)
    , joined_buffer(nullptr)
    , joined_valid(false)
    , owns_aligned_alloc_storage(true)
    , owns_joined_buffer(true)
{
    in_transaction.store(false, std::memory_order_release);

    // Use DGTL_aligned_alloc directly so the destructor's _aligned_free is safe.
    // Every buffer is 64-byte aligned for AVX-512 / Neon strikes.
    atoms        = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64u));
    cores        = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64u));
    shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64u));
    shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64u));
    joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64u));

    if (atoms)        std::memset(static_cast<void*>(atoms),        0, _capacity * sizeof(IRAtom));
    if (cores)        std::fill  (cores,        cores        + _capacity, IRCore{});
    if (shadow_atoms) std::memset(static_cast<void*>(shadow_atoms), 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::fill  (shadow_cores, shadow_cores + _capacity, IRCore{});
    if (joined_buffer) std::memset(static_cast<void*>(joined_buffer), 0, _capacity * sizeof(IRNode));
}

/// Construct from an external joined buffer (ownership NOT taken for the
/// external buffer itself; SoA buffers are still privately allocated).
/// When external_buffer == nullptr this acts as a capacity-reservation ctor;
/// the second argument is then treated as desired capacity, not node count.
IRStream::IRStream(IRNode* external_buffer, size_t node_count)
    : count(external_buffer ? node_count : 0)
    , _capacity((node_count > 0) ? node_count : 1024)
    , atoms(nullptr)
    , cores(nullptr)
    , shadow_atoms(nullptr)
    , shadow_cores(nullptr)
    , shadow_count(external_buffer ? node_count : 0)
    , in_transaction(false)
    , joined_buffer_in_transaction(false)
    , joined_buffer(external_buffer)
    , joined_valid(false)
    , owns_aligned_alloc_storage(true)
    , owns_joined_buffer(!external_buffer) // owns only when we allocate it
{
    in_transaction.store(false, std::memory_order_release);

    atoms        = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64u));
    cores        = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64u));
    shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64u));
    shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64u));

    if (!external_buffer) {
        joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64u));
    }

    if (atoms)        std::memset(static_cast<void*>(atoms),        0, _capacity * sizeof(IRAtom));
    if (cores)        std::fill  (cores,        cores        + _capacity, IRCore{});
    if (shadow_atoms) std::memset(static_cast<void*>(shadow_atoms), 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::fill  (shadow_cores, shadow_cores + _capacity, IRCore{});
    if (!external_buffer && joined_buffer)
        std::memset(static_cast<void*>(joined_buffer), 0, _capacity * sizeof(IRNode));

    // Decompose provided joined nodes into SoA
    if (external_buffer && atoms && cores) {
        for (size_t i = 0; i < count; ++i) {
            atoms[i].opcode      = external_buffer[i].opcode;
            atoms[i].flags       = external_buffer[i].flags;
            cores[i].result_id   = external_buffer[i].result_id;
            cores[i].op_a        = external_buffer[i].op_a;
            cores[i].op_b        = external_buffer[i].op_b;
            cores[i].op_c        = external_buffer[i].op_c;
            cores[i].intent_hash = external_buffer[i].intent_hash;
            cores[i].logic_flags = external_buffer[i].logic_flags;
        }
    }
}

IRStream::~IRStream() {
    if (owns_aligned_alloc_storage) {
        DGTL_aligned_free(atoms);
        DGTL_aligned_free(cores);
        DGTL_aligned_free(shadow_atoms);
        DGTL_aligned_free(shadow_cores);
    }
    if (owns_joined_buffer) {
        DGTL_aligned_free(joined_buffer);
    }
}

IRStream::IRStream(IRStream&& other) noexcept
    : count(other.count)
    , _capacity(other._capacity)
    , atoms(other.atoms)
    , cores(other.cores)
    , shadow_atoms(other.shadow_atoms)
    , shadow_cores(other.shadow_cores)
    , shadow_count(other.shadow_count)
    , in_transaction(other.in_transaction.load(std::memory_order_acquire))
    , joined_buffer_in_transaction(other.joined_buffer_in_transaction)
    , joined_buffer(other.joined_buffer)
    , joined_valid(other.joined_valid)
    , owns_aligned_alloc_storage(other.owns_aligned_alloc_storage)
    , owns_joined_buffer(other.owns_joined_buffer)
    , frontend_lang(other.frontend_lang)
    , default_type_ctx(std::move(other.default_type_ctx))
    , ownership_tracking(std::move(other.ownership_tracking))
    , concurrency_map(std::move(other.concurrency_map))
    , dsa_map(std::move(other.dsa_map))
{
    other.count     = 0;
    other._capacity = 0;
    other.atoms     = nullptr;
    other.cores     = nullptr;
    other.shadow_atoms = nullptr;
    other.shadow_cores = nullptr;
    other.shadow_count = 0;
    other.in_transaction.store(false, std::memory_order_release);
    other.joined_buffer_in_transaction = false;
    other.joined_buffer = nullptr;
    other.joined_valid  = false;
    other.owns_aligned_alloc_storage = false;
    other.owns_joined_buffer         = false;
}

IRStream& IRStream::operator=(IRStream&& other) noexcept {
    if (this == &other) return *this;

    if (owns_aligned_alloc_storage) {
        DGTL_aligned_free(atoms);
        DGTL_aligned_free(cores);
        DGTL_aligned_free(shadow_atoms);
        DGTL_aligned_free(shadow_cores);
    }
    if (owns_joined_buffer) {
        DGTL_aligned_free(joined_buffer);
    }

    count     = other.count;
    _capacity = other._capacity;
    atoms     = other.atoms;
    cores     = other.cores;
    shadow_atoms = other.shadow_atoms;
    shadow_cores = other.shadow_cores;
    shadow_count = other.shadow_count;
    in_transaction.store(other.in_transaction.load(std::memory_order_acquire),
                         std::memory_order_release);
    joined_buffer_in_transaction = other.joined_buffer_in_transaction;
    joined_buffer = other.joined_buffer;
    joined_valid  = other.joined_valid;
    owns_aligned_alloc_storage = other.owns_aligned_alloc_storage;
    owns_joined_buffer         = other.owns_joined_buffer;
    frontend_lang              = other.frontend_lang;
    default_type_ctx           = std::move(other.default_type_ctx);
    ownership_tracking         = std::move(other.ownership_tracking);
    concurrency_map            = std::move(other.concurrency_map);
    dsa_map                    = std::move(other.dsa_map);

    other.count     = 0;
    other._capacity = 0;
    other.atoms     = nullptr;
    other.cores     = nullptr;
    other.shadow_atoms = nullptr;
    other.shadow_cores = nullptr;
    other.shadow_count = 0;
    other.in_transaction.store(false, std::memory_order_release);
    other.joined_buffer_in_transaction = false;
    other.joined_buffer = nullptr;
    other.joined_valid  = false;
    other.owns_aligned_alloc_storage = false;
    other.owns_joined_buffer         = false;

    return *this;
}

// ===========================================================================
// SECTION 2: ALLOCATION & COPYING
// ===========================================================================

IRStream IRStream::allocate_transactional(size_t size) {
    return IRStream(local_transaction_arena, size);
}

IRStream IRStream::allocate_shadow(size_t size) {
    return IRStream(local_transaction_arena, size);
}

void IRStream::invalidate_cfg_cache_locked() noexcept {
    cfg_cache_valid = false;
}

void IRStream::invalidate_cfg_cache() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    invalidate_cfg_cache_locked();
}

void IRStream::ensure_cfg_cache_locked() const noexcept {
    if (cfg_cache_valid) return;
    rebuild_cfg_locked();
}

void IRStream::rebuild_cfg_locked() const noexcept {
    cfg_blocks_cache.clear();
    cfg_edges_cache.clear();
    cfg_succ_cache.clear();
    cfg_pred_cache.clear();
    cfg_node_to_block_cache.clear();
    cfg_label_to_block_cache.clear();

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const IRAtom* a_ptr = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr = trans ? shadow_cores : cores;
    const size_t cur_count = trans ? shadow_count : count;

    if (!a_ptr || !c_ptr || cur_count == 0) {
        cfg_cache_valid = true;
        return;
    }

    std::vector<size_t> leaders;
    leaders.reserve(cur_count + 1u);
    leaders.push_back(0u);

    for (size_t i = 0; i < cur_count; ++i) {
        if (a_ptr[i].opcode == OpCode::LABEL) {
            leaders.push_back(i);
        }
        if (is_cfg_terminator(a_ptr[i].opcode) && (i + 1u) < cur_count) {
            leaders.push_back(i + 1u);
        }
    }

    std::sort(leaders.begin(), leaders.end());
    leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());

    cfg_blocks_cache.reserve(leaders.size());
    cfg_node_to_block_cache.assign(cur_count, INVALID_SLOT);

    for (size_t bi = 0; bi < leaders.size(); ++bi) {
        const size_t begin = leaders[bi];
        const size_t end = (bi + 1u < leaders.size()) ? leaders[bi + 1u] : cur_count;

        IRBasicBlock block;
        block.id = static_cast<uint32_t>(bi);
        block.begin = begin;
        block.end = end;
        block.is_entry = (bi == 0u);

        for (size_t i = begin; i < end; ++i) {
            cfg_node_to_block_cache[i] = block.id;
        }

        for (size_t i = begin; i < end && a_ptr[i].opcode == OpCode::LABEL; ++i) {
            const uint32_t label_id = c_ptr[i].result_id;
            if (label_id == INVALID_SLOT) continue;
            if (block.primary_label == INVALID_SLOT) {
                block.primary_label = label_id;
            }
            cfg_label_to_block_cache[label_id] = block.id;
        }

        cfg_blocks_cache.push_back(block);
    }

    cfg_succ_cache.assign(cfg_blocks_cache.size(), {});
    cfg_pred_cache.assign(cfg_blocks_cache.size(), {});

    const auto add_edge = [&](uint32_t from, uint32_t to, CFGEdgeKind kind) {
        if (from == INVALID_SLOT || to == INVALID_SLOT) return;
        if (from >= cfg_succ_cache.size() || to >= cfg_pred_cache.size()) return;

        auto& succ = cfg_succ_cache[from];
        if (std::find(succ.begin(), succ.end(), to) != succ.end()) return;

        succ.push_back(to);
        cfg_pred_cache[to].push_back(from);
        cfg_edges_cache.push_back({ from, to, kind });
    };

    for (size_t bi = 0; bi < cfg_blocks_cache.size(); ++bi) {
        const IRBasicBlock& block = cfg_blocks_cache[bi];
        if (block.end <= block.begin) continue;

        const uint32_t from = static_cast<uint32_t>(bi);
        const uint32_t next = (bi + 1u < cfg_blocks_cache.size())
            ? static_cast<uint32_t>(bi + 1u)
            : INVALID_SLOT;

        const size_t term_idx = block.end - 1u;
        const OpCode op = a_ptr[term_idx].opcode;

        if (op == OpCode::BRANCH) {
            const uint32_t target_label = c_ptr[term_idx].op_a;
            const auto* it = cfg_label_to_block_cache.find(target_label);
            if (it) {
                add_edge(from, *it, CFGEdgeKind::UnconditionalBranch);
            }
            continue;
        }

        if (op == OpCode::BRANCH_COND || op == OpCode::BRANCH_COMPARE_FUSED) {
            const uint32_t target_label = (op == OpCode::BRANCH_COND) ? c_ptr[term_idx].op_b : c_ptr[term_idx].op_a;
            const auto* it = cfg_label_to_block_cache.find(target_label);
            if (it) {
                add_edge(from, *it, CFGEdgeKind::ConditionalTrue);
            }
            if (next != INVALID_SLOT) {
                add_edge(from, next, CFGEdgeKind::ConditionalFalse);
            }
            continue;
        }

        if (op == OpCode::RET || op == OpCode::RETURN || IRTraits::is_terminator(op)) {
            continue;
        }

        if (next != INVALID_SLOT) {
            add_edge(from, next, CFGEdgeKind::Fallthrough);
        }
    }

    cfg_cache_valid = true;
}

void IRStream::copy_from(const IRStream& other) {
    if (!atoms || !cores || !other.atoms || !other.cores) return;

    const bool src_trans     = other.in_transaction.load(std::memory_order_acquire);
    const IRAtom* src_atoms  = src_trans ? other.shadow_atoms : other.atoms;
    const IRCore* src_cores  = src_trans ? other.shadow_cores : other.cores;
    const size_t  src_count  = src_trans ? other.shadow_count : other.count;

    if (!src_atoms || !src_cores) {
        count = shadow_count = 0;
        joined_valid = false;
        return;
    }

    count = shadow_count = std::min(_capacity, src_count);
    detail::copy_atom_core_stream(atoms, cores, src_atoms, src_cores, count);

    if (shadow_atoms && shadow_cores) {
        detail::copy_atom_core_stream(shadow_atoms, shadow_cores, atoms, cores, count);
    }
    
    /* CRITICAL FIX P0: Preserve all sidecars during copy_from.
     * Sidecars contain essential metadata (call parameters, result IDs, ABI info).
     * Failure to copy them breaks subsequent HIR transformations and downstream lowering.
     */
    call_overflow_args_ = other.call_overflow_args_;
    call_abi_layout_words_ = other.call_abi_layout_words_;
    operand_overflow_ = other.operand_overflow_;
    result_ids_ = other.result_ids_;
    result_types_ = other.result_types_;
    node_effects_ = other.node_effects_;
    
    joined_valid = false;
    invalidate_cfg_cache();
}

IRStream IRStream::clone() const {
    const bool src_trans      = in_transaction.load(std::memory_order_acquire);
    const size_t target_count = src_trans ? shadow_count : count;
    const size_t new_capacity = std::max(_capacity, target_count > 0 ? target_count : size_t{1024});

    IRStream cloned(nullptr, new_capacity);
    cloned.count        = target_count;
    cloned.shadow_count = target_count;
    cloned.in_transaction.store(false, std::memory_order_release);

    const IRAtom* src_atoms = src_trans ? shadow_atoms : atoms;
    const IRCore* src_cores = src_trans ? shadow_cores : cores;

    if (src_atoms && src_cores && target_count > 0 && cloned.atoms && cloned.cores) {
        const size_t safe_n = std::min(target_count, cloned._capacity);
        std::memcpy(cloned.atoms, src_atoms, safe_n * sizeof(IRAtom));
        std::memcpy(cloned.cores, src_cores, safe_n * sizeof(IRCore));

        if (cloned.joined_buffer) {
            for (size_t i = 0; i < safe_n; ++i) {
                IRNode& dst       = cloned.joined_buffer[i];
                const IRAtom& a   = cloned.atoms[i];
                const IRCore& c   = cloned.cores[i];
                dst.opcode      = a.opcode;
                dst.flags       = a.flags;
                dst.result_id   = c.result_id;
                dst.op_a        = c.op_a;
                dst.op_b        = c.op_b;
                dst.op_c        = c.op_c;
                dst.intent_hash = c.intent_hash;
                dst.logic_flags = c.logic_flags;
            }
        }
    }
    
    /* CRITICAL FIX P0: Clone all sidecars to preserve metadata.
     * Sidecars are essential for ABI lowering, call parameter passing, and type tracking.
     */
    cloned.call_overflow_args_ = call_overflow_args_;
    cloned.call_abi_layout_words_ = call_abi_layout_words_;
    cloned.operand_overflow_ = operand_overflow_;
    cloned.result_ids_ = result_ids_;
    cloned.result_types_ = result_types_;
    cloned.node_effects_ = node_effects_;
    
    return cloned;
}

// ===========================================================================
// SECTION 3: TRANSACTION MANAGEMENT
// ===========================================================================

void IRStream::begin_transaction() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    joined_valid = false;
    invalidate_cfg_cache_locked();
    if (in_transaction.exchange(true, std::memory_order_acq_rel)) {
        // Already in a transaction — just re-sync shadow_count.
        shadow_count = std::min(count, _capacity);
        return;
    }
    shadow_count = std::min(count, _capacity);
    if (shadow_atoms && shadow_cores && atoms && cores && count > 0) {
        detail::copy_atom_core_stream(shadow_atoms, shadow_cores, atoms, cores, count);
    }
}

void IRStream::commit_transaction() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (!in_transaction.load(std::memory_order_acquire)) return;
    count = shadow_count;
    if (count > 0 && atoms && shadow_atoms && cores && shadow_cores) {
        detail::copy_atom_core_stream(atoms, cores, shadow_atoms, shadow_cores, count);
    }
    in_transaction.store(false, std::memory_order_release);
    joined_valid = false;
    invalidate_cfg_cache_locked();
}

void IRStream::abort_transaction() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    in_transaction.store(false, std::memory_order_release);
    shadow_count = count;
    joined_valid = false;
    invalidate_cfg_cache_locked();
}

void IRStream::rollback_transaction() noexcept {
    abort_transaction();
}

void IRStream::set_count(size_t val) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (val <= _capacity) [[likely]] {
        count = shadow_count = val;
        joined_valid = false;
        invalidate_cfg_cache_locked();
    }
}

bool IRStream::reserve_capacity(size_t requested_capacity) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (requested_capacity <= _capacity) return true;

    const size_t max_size = std::numeric_limits<size_t>::max();
    size_t new_capacity = requested_capacity;
    if (_capacity > 0 && _capacity <= max_size / 2u) {
        new_capacity = std::max(requested_capacity, _capacity * 2u);
    }

    const auto allocation_size_valid = [max_size](size_t element_count, size_t element_size) noexcept {
        return element_count <= max_size / element_size;
    };
    if (!allocation_size_valid(new_capacity, sizeof(IRAtom)) ||
        !allocation_size_valid(new_capacity, sizeof(IRCore)) ||
        !allocation_size_valid(new_capacity, sizeof(IRNode))) {
        return false;
    }

    const size_t atom_bytes = new_capacity * sizeof(IRAtom);
    const size_t core_bytes = new_capacity * sizeof(IRCore);
    const size_t node_bytes = new_capacity * sizeof(IRNode);
    IRAtom* new_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(atom_bytes, 64u));
    IRCore* new_cores = static_cast<IRCore*>(DGTL_aligned_alloc(core_bytes, 64u));
    IRAtom* new_shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(atom_bytes, 64u));
    IRCore* new_shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(core_bytes, 64u));
    IRNode* new_joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(node_bytes, 64u));

    if (!new_atoms || !new_cores || !new_shadow_atoms || !new_shadow_cores || !new_joined_buffer) {
        DGTL_aligned_free(new_atoms);
        DGTL_aligned_free(new_cores);
        DGTL_aligned_free(new_shadow_atoms);
        DGTL_aligned_free(new_shadow_cores);
        DGTL_aligned_free(new_joined_buffer);
        return false;
    }

    std::fill_n(new_atoms, new_capacity, IRAtom{});
    std::fill(new_cores, new_cores + new_capacity, IRCore{});
    std::fill_n(new_shadow_atoms, new_capacity, IRAtom{});
    std::fill(new_shadow_cores, new_shadow_cores + new_capacity, IRCore{});
    std::fill_n(new_joined_buffer, new_capacity, IRNode{});

    const size_t committed_count = std::min(count, _capacity);
    const size_t transactional_count = std::min(shadow_count, _capacity);
    detail::copy_atom_core_stream(new_atoms, new_cores, atoms, cores, committed_count);
    detail::copy_atom_core_stream(new_shadow_atoms, new_shadow_cores,
                                  shadow_atoms, shadow_cores, transactional_count);

    if (owns_aligned_alloc_storage) {
        DGTL_aligned_free(atoms);
        DGTL_aligned_free(cores);
        DGTL_aligned_free(shadow_atoms);
        DGTL_aligned_free(shadow_cores);
    }
    if (owns_joined_buffer) DGTL_aligned_free(joined_buffer);

    atoms = new_atoms;
    cores = new_cores;
    shadow_atoms = new_shadow_atoms;
    shadow_cores = new_shadow_cores;
    joined_buffer = new_joined_buffer;
    _capacity = new_capacity;
    owns_aligned_alloc_storage = true;
    owns_joined_buffer = true;
    joined_valid = false;
    return true;
}

void IRStream::reset() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    count = shadow_count = 0;
    call_overflow_args_.clear();
    call_abi_layout_words_.clear();
    in_transaction.store(false, std::memory_order_release);
    joined_valid = false;
    invalidate_cfg_cache_locked();
}

bool IRStream::is_null() const noexcept {
    return atoms == nullptr || cores == nullptr;
}

void IRStream::prune_shadow_buffer() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    shadow_count = count;
    if (shadow_atoms && shadow_cores) [[likely]] {
        const size_t tail = _capacity - count;
        if (tail > 0) {
            std::memset(static_cast<void*>(shadow_atoms + count), 0, tail * sizeof(IRAtom));
            std::fill  (shadow_cores + count, shadow_cores + count + tail, IRCore{});
        }
    }
    invalidate_cfg_cache_locked();
}

void IRStream::push_full_to_shadow() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (!shadow_atoms || !shadow_cores || !atoms || !cores) return;
    detail::copy_atom_core_stream(shadow_atoms, shadow_cores, atoms, cores, count);
    shadow_count = count;
    invalidate_cfg_cache_locked();
}

/// Commit an external shadow stream into this stream.
void IRStream::commit_from_shadow(IRStream& shadow_stream) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (shadow_stream.is_null()) [[unlikely]] return;
    shadow_stream.commit_transaction();

    if (!atoms || !cores || !shadow_stream.atoms || !shadow_stream.cores) return;

    const size_t n = std::min(_capacity, shadow_stream.count);
    if (n > 0) {
        detail::copy_atom_core_stream(atoms, cores,
                                      shadow_stream.atoms, shadow_stream.cores, n);
        if (shadow_atoms && shadow_cores) {
            detail::copy_atom_core_stream(shadow_atoms, shadow_cores, atoms, cores, n);
        }
    }

    count = shadow_count = n;
    in_transaction.store(false, std::memory_order_release);
    joined_valid = false;
    invalidate_cfg_cache_locked();
    shadow_stream.abort_transaction();
}

/// Commit the internal shadow into the live stream.
void IRStream::commit_from_shadow() noexcept {
    commit_transaction();
}

// ===========================================================================
// SECTION 4: HOT-PATH EMIT
// ===========================================================================

void IRStream::push_node(const IRAtom& atom, const IRCore& core) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const uint64_t sync_hash = (core.intent_hash == 0)
        ? IRNode::calculate_hash(atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c)
        : core.intent_hash;
    
    // Phase C: Apply semantic promotion to ensure aux_flags carries address-space/predicate info
    uint32_t final_aux_flags = core.aux_flags;
    if (final_aux_flags == 0u) {
        final_aux_flags = compute_phase_c_aux_flags(atom.opcode, atom.flags, core.op_c);
    }

    if (in_transaction.load(std::memory_order_acquire)) {
        if (shadow_count >= _capacity && !reserve_capacity(shadow_count + 1u)) [[unlikely]] return;
        if (!shadow_atoms || !shadow_cores) [[unlikely]] return;
        shadow_atoms[shadow_count] = atom;
        shadow_cores[shadow_count] = { core.result_id, core.op_a, core.op_b, core.op_c,
                                        sync_hash, core.logic_flags, final_aux_flags, core.debug_id, core.epoch };
        ++shadow_count;
        invalidate_cfg_cache_locked();
    } else {
        if (count >= _capacity && !reserve_capacity(count + 1u)) [[unlikely]] return;
        if (!atoms || !cores) [[unlikely]] return;
        atoms[count] = atom;
        cores[count] = { core.result_id, core.op_a, core.op_b, core.op_c,
                         sync_hash, core.logic_flags, final_aux_flags, core.debug_id, core.epoch };
        ++count;
        shadow_count = count;
        joined_valid = false;
        invalidate_cfg_cache_locked();
    }
}

void IRStream::push_node(OpCode op,
                         uint16_t fl,
                         uint32_t res,
                         uint32_t a,
                         uint32_t b,
                         uint32_t c,
                         uint64_t h) noexcept
{
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const uint64_t sync_hash = (h == 0)
        ? IRNode::calculate_hash(op, res, a, b, c)
        : h;
    
    // Phase C: Compute semantic aux_flags from op code and operands
    const uint32_t phase_c_aux_flags = compute_phase_c_aux_flags(op, fl, c);

    if (in_transaction.load(std::memory_order_acquire)) {
        if (shadow_count >= _capacity && !reserve_capacity(shadow_count + 1u)) [[unlikely]] return;
        if (!shadow_atoms || !shadow_cores) [[unlikely]] return;
        shadow_atoms[shadow_count] = { op, fl };
        shadow_cores[shadow_count] = { res, a, b, c, sync_hash, 0u, phase_c_aux_flags, 0u, 0u };
        ++shadow_count;
        invalidate_cfg_cache_locked();
    } else {
        if (count >= _capacity && !reserve_capacity(count + 1u)) [[unlikely]] return;
        if (!atoms || !cores) [[unlikely]] return;
        atoms[count] = { op, fl };
        cores[count] = { res, a, b, c, sync_hash, 0u, phase_c_aux_flags, 0u, 0u };
        ++count;
        shadow_count = count;
        joined_valid = false;
        invalidate_cfg_cache_locked();
    }
}

void IRStream::push_back(const IRNode& node) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const uint64_t final_hash = (node.intent_hash == 0)
        ? IRNode::calculate_hash(node.opcode, node.result_id,
                                 node.op_a, node.op_b, node.op_c)
        : node.intent_hash;
    
    // Phase C: Compute semantic aux_flags for pointer/predicate ops
    const uint32_t phase_c_aux_flags = compute_phase_c_aux_flags(node.opcode, node.flags, node.op_c);

    if (in_transaction.load(std::memory_order_acquire)) {
        const uint32_t idx = reserve_shadow_slot();
        if (idx == INVALID_SLOT || !shadow_atoms || !shadow_cores) return;
        shadow_atoms[idx] = { node.opcode, node.flags };
        shadow_cores[idx] = { node.result_id, node.op_a, node.op_b, node.op_c,
                      final_hash, node.logic_flags, phase_c_aux_flags, 0u, 0u };
        invalidate_cfg_cache_locked();
    } else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] {
            std::cerr << "[DGTL FATAL] IRStream capacity overflow at "
                      << _capacity << "\n";
            return;
        }
        atoms[count].opcode = node.opcode;
        atoms[count].flags  = node.flags;
        cores[count] = { node.result_id, node.op_a, node.op_b, node.op_c,
                 final_hash, node.logic_flags, phase_c_aux_flags, 0u, 0u };
        ++count;
        shadow_count = count;
        joined_valid = false;
        invalidate_cfg_cache_locked();
    }
}

uint32_t IRStream::reserve_shadow_slot() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (shadow_count >= _capacity && !reserve_capacity(shadow_count + 1u)) [[unlikely]] return INVALID_SLOT;
    if (!shadow_atoms) [[unlikely]] return INVALID_SLOT;
    return static_cast<uint32_t>(shadow_count++);
}

void IRStream::push_to_shadow(const IRNode& node) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (!in_transaction.load(std::memory_order_acquire)) {
        begin_transaction();
    }
    push_back(node);
}

// ===========================================================================
// SECTION 5: ACCESSORS
// ===========================================================================

IRNode IRStream::get_node(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans       = in_transaction.load(std::memory_order_acquire);
    const IRAtom* a_ptr    = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr    = trans ? shadow_cores : cores;
    const size_t  limit    = trans ? shadow_count  : count;

    if (idx >= limit || !a_ptr || !c_ptr) return {};
    return { a_ptr[idx].opcode, a_ptr[idx].flags,
             c_ptr[idx].result_id, c_ptr[idx].op_a, c_ptr[idx].op_b,
             c_ptr[idx].op_c,
             c_ptr[idx].intent_hash, c_ptr[idx].logic_flags };
}

const IRNode& IRStream::get_node_readonly(size_t idx) const noexcept {
    // fallback_node is non-mutable in the source header; use thread_local storage
    // so this const method can legally return a stable reference.
    // (The build SDK version marks it mutable — either path is ABI-identical.)
    thread_local IRNode tl_fallback{};
    tl_fallback = get_node(idx);
    return tl_fallback;
}

IRNode& IRStream::get_node_ref(size_t idx) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool   trans         = in_transaction.load(std::memory_order_acquire);
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
    const bool trans       = in_transaction.load(std::memory_order_acquire);
    IRAtom* atom_ptr       = trans ? shadow_atoms : atoms;
    IRCore* core_ptr       = trans ? shadow_cores : cores;
    const size_t limit     = trans ? shadow_count  : count;

    if (!atom_ptr || !core_ptr || idx >= limit) return;

    const uint64_t final_hash = (node.intent_hash == 0)
        ? IRNode::calculate_hash(node.opcode, node.result_id,
                                 node.op_a, node.op_b, node.op_c)
        : node.intent_hash;
    
    // Phase C: Compute semantic aux_flags for the new node
    const uint32_t phase_c_aux_flags = compute_phase_c_aux_flags(node.opcode, node.flags, node.op_c);

    atom_ptr[idx] = { node.opcode, node.flags };
    core_ptr[idx] = { node.result_id, node.op_a, node.op_b, node.op_c,
                      final_hash, node.logic_flags, phase_c_aux_flags, 0u, 0u };

    if (joined_buffer && idx < _capacity) {
        joined_buffer[idx]             = node;
        joined_buffer[idx].intent_hash = final_hash;
        joined_buffer_in_transaction   = trans;
    }

    if (node.opcode != OpCode::CALL) {
        call_overflow_args_.erase(idx);
        call_abi_layout_words_.erase(idx);
    }

    joined_valid = false;
    invalidate_cfg_cache_locked();
}

void IRStream::set_call_overflow_args(size_t node_idx, std::span<const uint32_t> extra_args) {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (extra_args.empty()) {
        call_overflow_args_.erase(node_idx);
        return;
    }
    call_overflow_args_[node_idx] = std::vector<uint32_t>(extra_args.begin(), extra_args.end());
}

std::vector<uint32_t> IRStream::get_call_overflow_args(size_t node_idx) const {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = call_overflow_args_.find(node_idx);
    if (!it) {
        return {};
    }
    return *it;
}

void IRStream::clear_call_overflow_args(size_t node_idx) {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    call_overflow_args_.erase(node_idx);
}

void IRStream::set_call_abi_layout_words(size_t node_idx, std::span<const uint32_t> words) {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (words.empty()) {
        call_abi_layout_words_.erase(node_idx);
        return;
    }
    call_abi_layout_words_[node_idx] = std::vector<uint32_t>(words.begin(), words.end());
}

std::vector<uint32_t> IRStream::get_call_abi_layout_words(size_t node_idx) const {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = call_abi_layout_words_.find(node_idx);
    if (!it) {
        return {};
    }
    return *it;
}

void IRStream::clear_call_abi_layout_words(size_t node_idx) {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    call_abi_layout_words_.erase(node_idx);
}

// ============================================================================
// Phase D: Variadic & Multi-Result Sidecar Accessors
// ============================================================================

void IRStream::set_operand_overflow(size_t node_idx, std::span<const uint32_t> overflow_operands) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (overflow_operands.empty()) {
        operand_overflow_.erase(node_idx);
        joined_valid = false;
        return;
    }
    operand_overflow_[node_idx] = std::vector<uint32_t>(overflow_operands.begin(), overflow_operands.end());
    joined_valid = false;
}

std::vector<uint32_t> IRStream::get_operand_overflow(size_t node_idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = operand_overflow_.find(node_idx);
    if (!it) {
        return {};
    }
    return *it;
}

void IRStream::clear_operand_overflow(size_t node_idx) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    operand_overflow_.erase(node_idx);
    joined_valid = false;
}

void IRStream::set_result_ids(size_t node_idx, std::span<const uint32_t> result_ids) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (result_ids.empty()) {
        result_ids_.erase(node_idx);
        joined_valid = false;
        return;
    }
    result_ids_[node_idx] = std::vector<uint32_t>(result_ids.begin(), result_ids.end());
    joined_valid = false;
}

std::vector<uint32_t> IRStream::get_result_ids(size_t node_idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = result_ids_.find(node_idx);
    if (!it) {
        return {};
    }
    return *it;
}

void IRStream::clear_result_ids(size_t node_idx) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    result_ids_.erase(node_idx);
    joined_valid = false;
}

void IRStream::set_result_type(uint32_t result_id, uint32_t type_handle) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (result_id == 0u || result_id == INVALID_SLOT || type_handle == 0u) {
        result_types_.erase(result_id);
        return;
    }
    if (!is_valid_type_handle(type_handle)) {
        return;
    }
    result_types_[result_id] = type_handle;
}

void IRStream::set_result_type(uint32_t result_id, TypeCode type_code) noexcept {
    set_result_type(result_id, static_cast<uint32_t>(type_code));
}

uint32_t IRStream::get_result_type(uint32_t result_id) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = result_types_.find(result_id);
    if (!it) {
        return 0u;  // Unknown type
    }
    return *it;
}

TypeCode IRStream::get_result_type_code(uint32_t result_id) const noexcept {
    const uint32_t type_handle = get_result_type(result_id);
    if (!is_valid_type_handle(type_handle)) {
        return TypeCode::VOID_T;
    }
    return static_cast<TypeCode>(type_handle);
}

void IRStream::clear_result_type(uint32_t result_id) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    result_types_.erase(result_id);
}

void IRStream::set_node_effects(size_t node_idx, uint32_t effect_set) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (effect_set == 0u) {
        node_effects_.erase(node_idx);
        return;
    }
    node_effects_[node_idx] = effect_set;
}

uint32_t IRStream::get_node_effects(size_t node_idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const auto* it = node_effects_.find(node_idx);
    if (!it) {
        return 0u;  // No tracked effects
    }
    return *it;
}

void IRStream::clear_node_effects(size_t node_idx) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    node_effects_.erase(node_idx);
}

const IRCore& IRStream::get_core_readonly(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans = in_transaction.load(std::memory_order_acquire);
    if (idx >= (trans ? shadow_count : count)) {
        static const IRCore null_core{};
        return null_core;
    }
    return trans ? shadow_cores[idx] : cores[idx];
}

const IRAtom& IRStream::get_atom_readonly(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    static const IRAtom null_atom{ OpCode::NOP, Flags::NONE };
    const bool   trans = in_transaction.load(std::memory_order_acquire);
    const size_t limit = trans ? shadow_count : count;
    if (idx >= limit) return null_atom;
    return trans ? shadow_atoms[idx] : atoms[idx];
}

IRNode IRStream::get_shadow_node_proxy(size_t idx) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (idx >= _capacity || !shadow_atoms || !shadow_cores) return {};
    return { shadow_atoms[idx].opcode, shadow_atoms[idx].flags,
             shadow_cores[idx].result_id, shadow_cores[idx].op_a,
             shadow_cores[idx].op_b,     shadow_cores[idx].op_c,
             shadow_cores[idx].intent_hash, shadow_cores[idx].logic_flags };
}

// ===========================================================================
// SECTION 6: SYNC & HASH LOGIC
// ===========================================================================

void IRStream::refresh_joined_buffer_locked(bool trans) const noexcept {
    const IRAtom* a_ptr       = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr       = trans ? shadow_cores : cores;
    const size_t  cur_count   = trans ? shadow_count  : count;

    if (!joined_buffer || !a_ptr || !c_ptr || cur_count == 0) return;
    if (joined_valid && joined_buffer_in_transaction == trans)  return;

    sync_joined_buffer_simd(joined_buffer, a_ptr, c_ptr,
                            std::min(cur_count, _capacity));
    joined_valid                 = true;
    joined_buffer_in_transaction = trans;
}

IRNode* IRStream::get_buffer_ptr() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool   trans     = in_transaction.load(std::memory_order_acquire);
    const size_t cur_count = trans ? shadow_count : count;
    if (!joined_buffer || cur_count == 0) return joined_buffer;
    refresh_joined_buffer_locked(trans);
    return joined_buffer;
}

bool IRStream::validate_shadow_integrity() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool   trans        = in_transaction.load(std::memory_order_acquire);
    const IRAtom* atom_ptr    = trans ? shadow_atoms : atoms;
    const IRCore* core_ptr    = trans ? shadow_cores : cores;
    const size_t  cur_count   = trans ? shadow_count  : count;

    if (!atom_ptr || !core_ptr) return (cur_count == 0);

    const auto validate_range = [&](size_t begin, size_t end) -> bool {
        for (size_t i = begin; i < end; ++i) {
            const uint64_t expected = IRNode::calculate_hash(
                atom_ptr[i].opcode,
                core_ptr[i].result_id,
                core_ptr[i].op_a,
                core_ptr[i].op_b,
                core_ptr[i].op_c);
            if (core_ptr[i].intent_hash != expected) return false;
        }
        return true;
    };

    constexpr size_t kParallelThreshold = 262144u;
    const uint32_t hw_threads  = std::thread::hardware_concurrency();
    const size_t   max_workers = cur_count / 65536u;
    const size_t   worker_n    = std::min<size_t>(
        hw_threads  == 0 ? 1u : hw_threads,
        max_workers == 0 ? 1u : max_workers);

    if (cur_count < kParallelThreshold || worker_n <= 1u) {
        return validate_range(0, cur_count);
    }

    std::atomic<bool> valid{ true };
    std::vector<std::thread> workers;
    workers.reserve(worker_n - 1u);
    const size_t chunk = (cur_count + worker_n - 1u) / worker_n;

    for (size_t w = 0; w + 1u < worker_n; ++w) {
        const size_t beg = w * chunk;
        const size_t end = std::min(cur_count, beg + chunk);
        workers.emplace_back([&, beg, end]() {
            if (!validate_range(beg, end))
                valid.store(false, std::memory_order_release);
        });
    }

    const size_t tail_begin = (worker_n - 1u) * chunk;
    if (!validate_range(tail_begin, cur_count))
        valid.store(false, std::memory_order_release);

    for (auto& w : workers) w.join();
    return valid.load(std::memory_order_acquire);
}

uint64_t IRStream::generate_logic_hash() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    constexpr uint64_t kSeed = 0x243F6A8885A308D3ULL;
    uint64_t h = kSeed;

    const bool   trans     = in_transaction.load(std::memory_order_acquire);
    const IRAtom* a_ptr    = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr    = trans ? shadow_cores : cores;
    const size_t  cur_count = trans ? shadow_count : count;

    if (!a_ptr || !c_ptr || cur_count == 0) return h;

    for (size_t i = 0; i < cur_count; ++i) {
        const uint64_t base_hash = IRNode::calculate_hash(a_ptr[i].opcode,
                                                          c_ptr[i].result_id,
                                                          c_ptr[i].op_a,
                                                          c_ptr[i].op_b,
                                                          c_ptr[i].op_c);
        const uint64_t meta_hash = dgtl::utils::Hashing::hash_pair32(
            static_cast<uint32_t>(a_ptr[i].flags),
            static_cast<uint32_t>(c_ptr[i].logic_flags));
        const uint64_t index_hash = dgtl::utils::Hashing::hash64(static_cast<uint64_t>(i));

        uint64_t node_hash = dgtl::utils::Hashing::combine(base_hash, meta_hash);
        node_hash = dgtl::utils::Hashing::combine(node_hash, index_hash);
        h = dgtl::utils::Hashing::combine(h, node_hash);
    }
    return h;
}

// ===========================================================================
// SECTION 7: TARGET-NEUTRAL COMPILATION PROFILE
// ===========================================================================

static std::unordered_map<uint32_t, dgtl::ir::CompilationProfile> g_compilation_profiles;
static std::mutex g_compilation_profiles_mutex;

CompilationProfile IRStream::get_compilation_profile(uint32_t stream_id) const noexcept {
    std::lock_guard<std::mutex> lock(g_compilation_profiles_mutex);
    if (const auto it = g_compilation_profiles.find(stream_id); it != g_compilation_profiles.end()) {
        return it->second;
    }
    return CompilationProfile{};
}

void IRStream::record_function_call(uint32_t function_id) noexcept {
    std::lock_guard<std::mutex> lock(g_compilation_profiles_mutex);
    ++g_compilation_profiles[function_id].calls_count;
}

void IRStream::record_block_hotness(uint32_t block_id) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    if (!atoms || block_id >= static_cast<uint32_t>(size())) return;
    DGTL_ASSERT_BOUNDS(block_id, size());
    atoms[block_id].flags |= Flags::IS_OPTIMIZED;
}

void IRStream::update_compilation_stats(uint32_t stream_id, uint64_t compilation_time_us) noexcept {
    std::lock_guard<std::mutex> lock(g_compilation_profiles_mutex);
    auto& profile = g_compilation_profiles[stream_id];
    if (profile.avg_compilation_latency_us == 0.0f) {
        profile.avg_compilation_latency_us = static_cast<float>(compilation_time_us);
    } else {
        constexpr float kAlpha = 0.3f;
        profile.avg_compilation_latency_us =
            kAlpha * static_cast<float>(compilation_time_us) +
            (1.0f - kAlpha) * profile.avg_compilation_latency_us;
    }

    using Tier = CompilationProfile::CompilationTier;
    if (compilation_time_us > 5000u && profile.current_tier == Tier::Fast) {
        profile.current_tier = Tier::Intensive;
        ++profile.tier_switch_count;
    } else if (compilation_time_us < 1000u && profile.current_tier == Tier::Intensive) {
        profile.current_tier = Tier::Fast;
        ++profile.tier_switch_count;
    }
}

void IRStream::record_compilation_cache_access(uint32_t stream_id, bool hit) noexcept {
    std::lock_guard<std::mutex> lock(g_compilation_profiles_mutex);
    auto& profile = g_compilation_profiles[stream_id];
    if (hit) ++profile.cache_hits; else ++profile.cache_misses;
}

void IRStream::ingest_compilation_feedback(uint32_t stream_id,
                                           double actual_latency,
                                           double cache_miss_rate,
                                           double branch_mispredict_rate,
                                           uint64_t execution_count) noexcept {
    std::lock_guard<std::mutex> lock(g_compilation_profiles_mutex);
    if (execution_count == 0) return;
    auto& profile = g_compilation_profiles[stream_id];
    const double miss_rate = std::clamp(cache_miss_rate, 0.0, 1.0);
    const uint64_t misses = static_cast<uint64_t>(miss_rate * static_cast<double>(execution_count));
    const uint64_t hits = execution_count - misses;
    profile.cache_misses += static_cast<uint32_t>(std::min<uint64_t>(misses, UINT32_MAX));
    profile.cache_hits += static_cast<uint32_t>(std::min<uint64_t>(hits, UINT32_MAX));
    profile.calls_count += execution_count;
    profile.execution_time_us += static_cast<uint64_t>(std::max(0.0, actual_latency));
    if (branch_mispredict_rate > 0.20) ++profile.speculation.deopt_triggers;
}

CompilationProfile::CompilationTier
IRStream::compute_adaptive_tier(uint32_t function_id) const noexcept {
    using Tier = CompilationProfile::CompilationTier;
    const CompilationProfile profile = get_compilation_profile(function_id);
    const uint32_t complexity = estimate_complexity_score();
    const uint64_t total_accesses = static_cast<uint64_t>(profile.cache_hits) + profile.cache_misses;
    const float cache_hit_rate = total_accesses == 0 ? 0.0f
        : static_cast<float>(profile.cache_hits) / static_cast<float>(total_accesses);
    if (profile.avg_compilation_latency_us > 8000.0f) return Tier::Intensive;
    if (complexity < 50 && cache_hit_rate > 0.8f && profile.calls_count < 80) return Tier::Fast;
    if (complexity > 140 || cache_hit_rate < 0.45f) return Tier::Intensive;
    return Tier::Adaptive;
}

// ===========================================================================
// SECTION 8: OPTIMIZATION PASSES
// ===========================================================================

// ---------------------------------------------------------------------------
// 8.3  Speculative Optimization
// ---------------------------------------------------------------------------
void IRStream::perform_speculative_optimization() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) { rollback_transaction(); return; }

    for (size_t i = 0; i < shadow_count; ++i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];
        (void)core;

        if (atom.opcode == OpCode::GUARD_VALUE && !(atom.flags & Flags::IS_DEAD)) {
            // Mark arithmetic operations in a guard-dominated window as MAY_DEOPT.
            const size_t window_end = std::min(i + 32u, shadow_count);
            for (size_t j = i + 1u; j < window_end; ++j) {
                IRAtom& dep = shadow_atoms[j];
                if ((dep.opcode >= OpCode::ADD_I32 && dep.opcode <= OpCode::DIV_I32) ||
                    (dep.opcode >= OpCode::ADD_F32 && dep.opcode <= OpCode::DIV_F32)) {
                    dep.flags |= Flags::MAY_DEOPT;
                }
            }
        }

        // Bias likely back-edges for speculation fusion.
        if (atom.opcode == OpCode::BRANCH_COND && (core.logic_flags & HintFlags::BranchLikely)) {
            atom.flags |= Flags::IS_FUSED;
        }
    }

    validate_shadow_integrity() ? commit_transaction() : rollback_transaction();
}

// ---------------------------------------------------------------------------
// 8.4  Loop Unrolling (profile-guided bounded loop unrolling)
// ---------------------------------------------------------------------------
void IRStream::perform_loop_unrolling(uint8_t unroll_factor) noexcept {
    if (unroll_factor <= 1 || count == 0) return;
    unroll_factor = std::clamp<uint8_t>(unroll_factor, 2u, 8u);

    begin_transaction();
    if (!shadow_atoms || !shadow_cores) { rollback_transaction(); return; }

    // Detect simple natural loops: LABEL ... BRANCH_COND(back-to-label)
    std::vector<std::pair<size_t, size_t>> loops;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (shadow_atoms[i].opcode != OpCode::LABEL) continue;
        const uint32_t label_id = shadow_cores[i].result_id;
        for (size_t j = i + 1u; j < shadow_count; ++j) {
            if (shadow_atoms[j].opcode == OpCode::BRANCH_COND &&
                (shadow_cores[j].op_b == label_id || shadow_cores[j].op_a == label_id)) {
                loops.push_back({ i, j });
                break;
            }
        }
    }

    if (loops.empty()) {
        commit_transaction();
        return;
    }

    // Build newly expanded stream in a temporary buffer to avoid clobbering downstream nodes
    std::vector<IRAtom> new_atoms;
    std::vector<IRCore> new_cores;
    new_atoms.reserve(shadow_count * unroll_factor);
    new_cores.reserve(shadow_count * unroll_factor);

    const uint32_t vreg_stride = std::max(get_max_id() + 1u, 1000u);
    size_t cur_src = 0;

    for (const auto& [header_idx, end_idx] : loops) {
        if (header_idx < cur_src) continue; // overlapping loop guard
        const size_t body_sz = end_idx - header_idx - 1u;
        if (body_sz < 3u || body_sz > 100u) {
            // Copy loop verbatim without unrolling
            for (size_t i = cur_src; i <= end_idx; ++i) {
                new_atoms.push_back(shadow_atoms[i]);
                new_cores.push_back(shadow_cores[i]);
            }
            cur_src = end_idx + 1;
            continue;
        }

        // 1. Copy instructions before the loop
        for (size_t i = cur_src; i <= header_idx; ++i) {
            new_atoms.push_back(shadow_atoms[i]);
            new_cores.push_back(shadow_cores[i]);
        }

        // 2. Unroll the loop body (iterations 0 .. unroll_factor - 1)
        for (uint8_t iter = 0u; iter < unroll_factor; ++iter) {
            for (size_t bi = header_idx + 1u; bi < end_idx; ++bi) {
                IRAtom a = shadow_atoms[bi];
                IRCore c = shadow_cores[bi];
                if (iter > 0 && c.result_id != INVALID_SLOT) {
                    c.result_id += static_cast<uint32_t>(iter) * vreg_stride;
                }
                c.intent_hash = IRNode::calculate_hash(
                    a.opcode, c.result_id, c.op_a, c.op_b, c.op_c);
                new_atoms.push_back(a);
                new_cores.push_back(c);
            }
        }

        // 3. Copy the loop terminator (BRANCH_COND)
        new_atoms.push_back(shadow_atoms[end_idx]);
        new_cores.push_back(shadow_cores[end_idx]);

        cur_src = end_idx + 1;
    }

    // 4. Copy remaining tail instructions
    for (size_t i = cur_src; i < shadow_count; ++i) {
        new_atoms.push_back(shadow_atoms[i]);
        new_cores.push_back(shadow_cores[i]);
    }

    if (new_atoms.size() > _capacity) {
        rollback_transaction();
        return;
    }

    std::memcpy(shadow_atoms, new_atoms.data(), new_atoms.size() * sizeof(IRAtom));
    std::memcpy(shadow_cores, new_cores.data(), new_cores.size() * sizeof(IRCore));
    shadow_count = new_atoms.size();

    validate_shadow_integrity() ? commit_transaction() : rollback_transaction();
}

// ---------------------------------------------------------------------------
// 8.5  Branch Prediction Optimization
// ---------------------------------------------------------------------------
void IRStream::optimize_branch_prediction() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) { rollback_transaction(); return; }

    for (size_t i = 0; i < shadow_count; ++i) {
        const IRAtom& atom = shadow_atoms[i];
        if (atom.opcode != OpCode::BRANCH_COND) continue;

        const int32_t target_slot = static_cast<int32_t>(shadow_cores[i].op_b);
        const int32_t offset = target_slot - static_cast<int32_t>(i);
        if (offset < 0) {
            // Backward branch → likely loop back-edge.
            shadow_cores[i].logic_flags |= HintFlags::BranchLikely;
            shadow_cores[i].logic_flags &= ~HintFlags::BranchUnlikely;
        } else if (offset > 100) {
            // Far forward branch → likely cold error path.
            shadow_cores[i].logic_flags |= HintFlags::BranchUnlikely;
            shadow_cores[i].logic_flags &= ~HintFlags::BranchLikely;
        }
    }

    validate_shadow_integrity() ? commit_transaction() : rollback_transaction();
}

// ---------------------------------------------------------------------------
// 8.7  Profile-Guided Optimization (PGO)
// ---------------------------------------------------------------------------
void IRStream::perform_profile_guided_optimization(
        const ProfilingData& profile_data) noexcept
{
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) { rollback_transaction(); return; }

    for (size_t i = 0; i < shadow_count; ++i) {
        const float hotness = profile_data.compute_hotness_score(i);
        if (hotness > 0.7f) {
            shadow_atoms[i].flags |= Flags::IS_LOOP_INVARIANT; // hot-path marker
        }
    }

    perform_speculative_optimization();

    validate_shadow_integrity() ? commit_transaction() : rollback_transaction();
}

// ---------------------------------------------------------------------------
// 8.8  Memory Access Pattern Optimization
// ---------------------------------------------------------------------------
void IRStream::optimize_memory_access_patterns() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) { rollback_transaction(); return; }

    // Group load/store instructions by address register.
    std::unordered_map<uint32_t, std::vector<size_t>> addr_accesses;
    addr_accesses.reserve(std::min(shadow_count, size_t{1024}));

    for (size_t i = 0; i < shadow_count; ++i) {
        const IRAtom& atom = shadow_atoms[i];
        const IRCore& core = shadow_cores[i];
        if ((atom.opcode == OpCode::LOAD || atom.opcode == OpCode::ATOMIC_LOAD ||
             atom.opcode == OpCode::STORE || atom.opcode == OpCode::ATOMIC_STORE)
                && core.op_a != INVALID_SLOT) {
            addr_accesses[core.op_a].push_back(i);
        }
    }

    // Detect uniform stride patterns and insert prefetch hints.
    for (const auto& [addr_reg, indices] : addr_accesses) {
        (void)addr_reg;
        if (indices.size() < 2u) continue;
        std::vector<int32_t> strides;
        strides.reserve(indices.size() - 1u);
        for (size_t k = 1u; k < indices.size(); ++k)
            strides.push_back(static_cast<int32_t>(indices[k] - indices[k - 1u]));

        const int32_t first_stride = strides[0];
        const bool regular = std::all_of(strides.begin(), strides.end(),
            [first_stride](int32_t s) { return s == first_stride; });

        if (regular && first_stride > 1) {
            for (size_t idx : indices) {
                if (shadow_atoms[idx].opcode == OpCode::LOAD)
                    shadow_atoms[idx].flags |= Flags::IS_LOOP_INVARIANT;
            }
        }
    }

    validate_shadow_integrity() ? commit_transaction() : rollback_transaction();
}

// ---------------------------------------------------------------------------
// 8.12  SIMD Vectorization Analysis (diagnostic / metrics pass)
// ---------------------------------------------------------------------------
void IRStream::analyze_vectorization_patterns() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans      = in_transaction.load(std::memory_order_acquire);
    // Use local pointers to avoid shadowing class members 'atoms' and 'cores'.
    IRAtom* a_ptr         = trans ? shadow_atoms : atoms;
    IRCore* c_ptr         = trans ? shadow_cores : cores;
    const size_t cur_cnt  = trans ? shadow_count  : count;
    if (!a_ptr || !c_ptr) return;

    for (size_t i = 0; i + 2u < cur_cnt; ++i) {
        if (a_ptr[i].opcode     == OpCode::LOAD        &&
            a_ptr[i + 1u].opcode == OpCode::VEC_ADD_F32_512 &&
            a_ptr[i + 2u].opcode == OpCode::STORE) {
            a_ptr[i    ].flags |= Flags::IS_VECTOR_LANE;
            a_ptr[i + 1u].flags |= Flags::IS_VECTOR_LANE;
            a_ptr[i + 2u].flags |= Flags::IS_VECTOR_LANE;
        }
    }
    (void)c_ptr; // retained for future use
}

// ---------------------------------------------------------------------------
// 8.13  SIMD Opportunity Analysis (diagnostic / metrics pass)
// ---------------------------------------------------------------------------
void IRStream::analyze_simd_opportunities() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans      = in_transaction.load(std::memory_order_acquire);
    const IRAtom* a_ptr   = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr   = trans ? shadow_cores : cores;
    const size_t  cur_cnt = trans ? shadow_count  : count;
    if (!a_ptr || !c_ptr) return;

    [[maybe_unused]] uint32_t vectorizable_count = 0u;
    [[maybe_unused]] uint32_t gather_patterns    = 0u;
    [[maybe_unused]] uint32_t scatter_patterns   = 0u;

    for (size_t i = 0; i < cur_cnt; ++i) {
        const OpCode   op  = a_ptr[i].opcode;
        const uint16_t fl  = a_ptr[i].flags;

        if ((op >= OpCode::ADD_F32 && op <= OpCode::DIV_F32) ||
            (op >= OpCode::ADD_F64 && op <= OpCode::DIV_F64) ||
            (op >= OpCode::ADD_I32 && op <= OpCode::DIV_I32) ||
            (op >= OpCode::ADD_I64 && op <= OpCode::DIV_I64)) {
            if (!(fl & Flags::IS_VOLATILE)) ++vectorizable_count;
        }
        if ((op == OpCode::LOAD || op == OpCode::ATOMIC_LOAD || op == OpCode::GATHER_F32_512 ||
             op == OpCode::VEC_LOAD) && (fl & Flags::IS_VECTOR_LANE))
            ++gather_patterns;
        if ((op == OpCode::STORE || op == OpCode::ATOMIC_STORE || op == OpCode::VEC_STORE) &&
            (fl & Flags::IS_VECTOR_LANE))
            ++scatter_patterns;
    }
    // Metrics would be forwarded to ProfilingData in a full implementation.
}

// ---------------------------------------------------------------------------
// 8.14  Speculation Opportunity Analysis (diagnostic / metrics pass)
// ---------------------------------------------------------------------------
void IRStream::analyze_speculation_opportunities() noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    const bool trans      = in_transaction.load(std::memory_order_acquire);
    const IRAtom* a_ptr   = trans ? shadow_atoms : atoms;
    const IRCore* c_ptr   = trans ? shadow_cores : cores;
    const size_t  cur_cnt = trans ? shadow_count  : count;
    if (!a_ptr || !c_ptr) return;

    [[maybe_unused]] uint32_t guard_ops              = 0u;
    [[maybe_unused]] uint32_t speculatable_after_guard = 0u;

    for (size_t i = 0; i < cur_cnt; ++i) {
        if (!(a_ptr[i].flags & Flags::IS_GUARD) ||
             a_ptr[i].opcode != OpCode::GUARD_VALUE) continue;
        ++guard_ops;

        const size_t window_end = std::min(i + 32u, cur_cnt);
        for (size_t j = i + 1u; j < window_end; ++j) {
            const OpCode op = a_ptr[j].opcode;
            if ((op >= OpCode::ADD_I32 && op <= OpCode::DIV_I64) ||
                (op >= OpCode::ADD_F32 && op <= OpCode::DIV_F64)) {
                if (op != OpCode::LOAD && op != OpCode::STORE)
                    ++speculatable_after_guard;
            }
        }
    }
    // Metrics would be forwarded to ProfilingData in a full implementation.
}

// ===========================================================================
// SECTION 9: METADATA ACCESSORS & MISC
// ===========================================================================

void     IRStream::update_shadow_size(size_t s)  noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    shadow_count = std::min(s, _capacity);
    invalidate_cfg_cache_locked();
}
void     IRStream::set_shadow_count(size_t val) noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    shadow_count = std::min(val, _capacity);
    invalidate_cfg_cache_locked();
}
uint32_t IRStream::intent_crc()                  const noexcept {
    return static_cast<uint32_t>(generate_logic_hash());
}

const std::vector<IRBasicBlock>& IRStream::get_basic_blocks() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    return cfg_blocks_cache;
}

const std::vector<CFGEdge>& IRStream::get_cfg_edges() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    return cfg_edges_cache;
}

const std::vector<uint32_t>& IRStream::get_successors(uint32_t block_id) const noexcept {
    static const std::vector<uint32_t> kEmpty;
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    if (block_id >= cfg_succ_cache.size()) return kEmpty;
    return cfg_succ_cache[block_id];
}

const std::vector<uint32_t>& IRStream::get_predecessors(uint32_t block_id) const noexcept {
    static const std::vector<uint32_t> kEmpty;
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    if (block_id >= cfg_pred_cache.size()) return kEmpty;
    return cfg_pred_cache[block_id];
}

uint32_t IRStream::get_entry_block_id() const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    return cfg_blocks_cache.empty() ? INVALID_SLOT : cfg_blocks_cache.front().id;
}

uint32_t IRStream::get_block_id_for_node(size_t node_index) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();
    if (node_index >= cfg_node_to_block_cache.size()) return INVALID_SLOT;
    return cfg_node_to_block_cache[node_index];
}

std::vector<uint32_t> IRStream::traversal_preorder(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<uint32_t> order;
    if (n == 0u) return order;

    std::vector<uint8_t> seen(n, 0u);
    std::vector<uint32_t> stack;
    stack.reserve(n);
    order.reserve(n);

    const auto visit = [&](uint32_t root) {
        if (root == INVALID_SLOT || root >= n) return;
        stack.push_back(root);
        while (!stack.empty()) {
            const uint32_t b = stack.back();
            stack.pop_back();
            if (seen[b]) continue;
            seen[b] = 1u;
            order.push_back(b);

            const auto& succ = cfg_succ_cache[b];
            for (size_t i = succ.size(); i > 0u; --i) {
                const uint32_t s = succ[i - 1u];
                if (s < n && !seen[s]) stack.push_back(s);
            }
        }
    };

    const uint32_t root = (entry_block == INVALID_SLOT)
        ? cfg_blocks_cache.front().id
        : entry_block;
    visit(root);
    for (uint32_t b = 0; b < n; ++b) {
        if (!seen[b]) visit(b);
    }
    return order;
}

std::vector<uint32_t> IRStream::traversal_postorder(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<uint32_t> order;
    if (n == 0u) return order;

    std::vector<uint8_t> seen(n, 0u);
    std::vector<std::pair<uint32_t, bool>> stack;
    stack.reserve(n * 2u);
    order.reserve(n);

    const auto visit = [&](uint32_t root) {
        if (root == INVALID_SLOT || root >= n) return;
        stack.push_back({ root, false });
        while (!stack.empty()) {
            const auto [b, expanded] = stack.back();
            stack.pop_back();

            if (expanded) {
                order.push_back(b);
                continue;
            }
            if (seen[b]) continue;
            seen[b] = 1u;

            stack.push_back({ b, true });
            const auto& succ = cfg_succ_cache[b];
            for (size_t i = succ.size(); i > 0u; --i) {
                const uint32_t s = succ[i - 1u];
                if (s < n && !seen[s]) {
                    stack.push_back({ s, false });
                }
            }
        }
    };

    const uint32_t root = (entry_block == INVALID_SLOT)
        ? cfg_blocks_cache.front().id
        : entry_block;
    visit(root);
    for (uint32_t b = 0; b < n; ++b) {
        if (!seen[b]) visit(b);
    }
    return order;
}

std::vector<uint32_t> IRStream::traversal_reverse_postorder(uint32_t entry_block) const noexcept {
    auto po = traversal_postorder(entry_block);
    std::reverse(po.begin(), po.end());
    return po;
}

std::vector<uint32_t> IRStream::traversal_breadth_first(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<uint32_t> order;
    if (n == 0u) return order;

    std::vector<uint8_t> seen(n, 0u);
    std::vector<uint32_t> queue;
    queue.reserve(n);
    order.reserve(n);

    const auto bfs_from = [&](uint32_t root) {
        if (root == INVALID_SLOT || root >= n || seen[root]) return;
        size_t head = 0u;
        queue.clear();
        queue.push_back(root);
        seen[root] = 1u;

        while (head < queue.size()) {
            const uint32_t b = queue[head++];
            order.push_back(b);
            for (const uint32_t s : cfg_succ_cache[b]) {
                if (s < n && !seen[s]) {
                    seen[s] = 1u;
                    queue.push_back(s);
                }
            }
        }
    };

    const uint32_t root = (entry_block == INVALID_SLOT)
        ? cfg_blocks_cache.front().id
        : entry_block;
    bfs_from(root);
    for (uint32_t b = 0; b < n; ++b) {
        bfs_from(b);
    }
    return order;
}

std::vector<uint32_t> IRStream::compute_immediate_dominators(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<uint32_t> idom(n, INVALID_SLOT);
    if (n == 0u) return idom;

    const uint32_t root = (entry_block == INVALID_SLOT)
        ? cfg_blocks_cache.front().id
        : entry_block;
    if (root >= n) return idom;

    std::vector<uint32_t> rpo;
    rpo.reserve(n);
    std::vector<uint8_t> seen(n, 0u);
    std::vector<std::pair<uint32_t, bool>> stack;
    stack.reserve(n * 2u);

    const auto visit = [&](uint32_t start) {
        if (start >= n || seen[start]) return;
        stack.push_back({ start, false });
        while (!stack.empty()) {
            const auto [b, expanded] = stack.back();
            stack.pop_back();

            if (expanded) {
                rpo.push_back(b);
                continue;
            }
            if (seen[b]) continue;
            seen[b] = 1u;

            stack.push_back({ b, true });
            const auto& succ = cfg_succ_cache[b];
            for (size_t i = succ.size(); i > 0u; --i) {
                const uint32_t s = succ[i - 1u];
                if (s < n && !seen[s]) stack.push_back({ s, false });
            }
        }
    };

    visit(root);
    for (uint32_t b = 0; b < n; ++b) visit(b);
    std::reverse(rpo.begin(), rpo.end());

    std::vector<uint32_t> rpo_index(n, INVALID_SLOT);
    for (size_t i = 0; i < rpo.size(); ++i) rpo_index[rpo[i]] = static_cast<uint32_t>(i);

    const auto intersect = [&](uint32_t a, uint32_t b) -> uint32_t {
        uint32_t finger1 = a;
        uint32_t finger2 = b;
        while (finger1 != finger2) {
            while (rpo_index[finger1] > rpo_index[finger2]) {
                finger1 = idom[finger1];
                if (finger1 == INVALID_SLOT) return INVALID_SLOT;
            }
            while (rpo_index[finger2] > rpo_index[finger1]) {
                finger2 = idom[finger2];
                if (finger2 == INVALID_SLOT) return INVALID_SLOT;
            }
        }
        return finger1;
    };

    idom[root] = root;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < rpo.size(); ++i) {
            const uint32_t b = rpo[i];
            if (b == root) continue;

            uint32_t new_idom = INVALID_SLOT;
            const auto& preds = cfg_pred_cache[b];
            auto it = std::find_if(preds.begin(), preds.end(), [n, &idom](uint32_t p) noexcept {
                return p < n && idom[p] != INVALID_SLOT;
            });
            if (it != preds.end()) {
                new_idom = *it;
            }
            if (new_idom == INVALID_SLOT) continue;

            for (const uint32_t p : cfg_pred_cache[b]) {
                if (p >= n || p == new_idom || idom[p] == INVALID_SLOT) continue;
                new_idom = intersect(p, new_idom);
                if (new_idom == INVALID_SLOT) break;
            }

            if (new_idom != INVALID_SLOT && idom[b] != new_idom) {
                idom[b] = new_idom;
                changed = true;
            }
        }
    }

    idom[root] = INVALID_SLOT;
    return idom;
}

std::vector<std::vector<uint32_t>> IRStream::compute_dominance_frontier(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<std::vector<uint32_t>> df(n);
    if (n == 0u) return df;

    const std::vector<uint32_t> idom = compute_immediate_dominators(entry_block);

    for (uint32_t b = 0; b < n; ++b) {
        const auto& preds = cfg_pred_cache[b];
        if (preds.size() < 2u) continue;

        for (uint32_t p : preds) {
            if (p >= n) continue;
            uint32_t runner = p;
            while (runner != INVALID_SLOT && runner != idom[b]) {
                auto& frontier = df[runner];
                if (std::find(frontier.begin(), frontier.end(), b) == frontier.end()) {
                    frontier.push_back(b);
                }
                if (idom[runner] == runner) break;
                runner = idom[runner];
            }
        }
    }

    return df;
}

std::vector<uint32_t> IRStream::find_natural_loop_headers(uint32_t entry_block) const noexcept {
    std::lock_guard<std::recursive_mutex> guard(stream_mutex);
    ensure_cfg_cache_locked();

    const size_t n = cfg_blocks_cache.size();
    std::vector<uint32_t> headers;
    if (n == 0u) return headers;

    const std::vector<uint32_t> idom = compute_immediate_dominators(entry_block);

    const auto dominates = [&](uint32_t dom, uint32_t node) -> bool {
        if (dom >= n || node >= n) return false;
        if (dom == node) return true;

        uint32_t cur = node;
        while (cur != INVALID_SLOT) {
            cur = idom[cur];
            if (cur == dom) return true;
        }
        return false;
    };

    for (const CFGEdge& e : cfg_edges_cache) {
        if (e.from >= n || e.to >= n) continue;
        if (dominates(e.to, e.from)) {
            if (std::find(headers.begin(), headers.end(), e.to) == headers.end()) {
                headers.push_back(e.to);
            }
        }
    }

    return headers;
}

uint32_t IRStream::get_max_id() const noexcept {
    const bool   trans     = in_transaction.load(std::memory_order_acquire);
    const IRCore* c_ptr    = trans ? shadow_cores : cores;
    const size_t  cur_cnt  = trans ? shadow_count  : count;

    if (!c_ptr || cur_cnt == 0) return 0u;

    uint32_t max_id = 0u;
    for (size_t i = 0; i < cur_cnt; ++i) {
        const uint32_t rid = c_ptr[i].result_id;
        if (rid != INVALID_SLOT && rid < 0x80000000u && rid > max_id)
            max_id = rid;
    }
    return max_id;
}


} // namespace dgtl::ir


