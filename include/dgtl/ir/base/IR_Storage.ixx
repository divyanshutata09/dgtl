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

module;

#include <cstdlib>

// Global Module Fragment: Capture core memory mapping arrays
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>

export module dgtl.ir_storage;


// Dependency Ordered Named Module Imports
import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;

// ============================================================================
// SYSTEM RUNTIME TYPE PROXIES (NUMA Integration Layers)
// ============================================================================
namespace dgtl::runtime::numa {
    // Stub boundaries to let module link successfully before numa_runtime compilation
    [[nodiscard]] inline std::uint32_t get_current_node() noexcept { return 0u; }
    [[nodiscard]] inline void* allocate_on_node(std::size_t size, std::uint32_t, std::size_t) noexcept {
        return std::malloc(size);
    }
    inline void free_allocation(void* ptr) noexcept { std::free(ptr); }
}

// ============================================================================
// INTERNAL STORAGE ENGINE PIPELINES
// ============================================================================
//
// IRStorage intentionally does NOT define its own IRAtom/IRCore/IRNode types.
// The canonical representations live in dgtl.ir_node. Keeping a second
// fallback representation here creates an ABI/layout split between storage,
// SSA, rewriting, verification, and downstream consumers.
//
// Canonical ownership:
//   dgtl.ir_node -> IRAtom / IRCore / IRNode
//   dgtl.ir_storage -> storage of those canonical types
//
// This follows the compiler-layering principle used by mature IR systems:
// storage owns lifetime/layout management, while the IR node module owns the
// semantic data model.
//
// ============================================================================

export namespace dgtl::ir {

namespace detail {

#if defined(__GNUC__) || defined(__clang__)
    #define DGTL_IR_STORAGE_RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define DGTL_IR_STORAGE_RESTRICT __restrict
#else
    #define DGTL_IR_STORAGE_RESTRICT
#endif

constexpr std::size_t kIRStorageCopyPrefetchDistance = 32u;
constexpr std::size_t kIRStorageCopyUnrollFactor = 8u;

inline void copy_atom_core_stream(
    IRAtom* DGTL_IR_STORAGE_RESTRICT dst_atoms,
    IRCore* DGTL_IR_STORAGE_RESTRICT dst_cores,
    const IRAtom* DGTL_IR_STORAGE_RESTRICT src_atoms,
    const IRCore* DGTL_IR_STORAGE_RESTRICT src_cores,
    std::size_t count) noexcept
{
    if (!dst_atoms || !dst_cores || !src_atoms || !src_cores || count == 0u) return;

    std::size_t i = 0u;
    for (; i + kIRStorageCopyUnrollFactor <= count; i += kIRStorageCopyUnrollFactor) {
#if defined(__GNUC__) || defined(__clang__)
        if (i + kIRStorageCopyPrefetchDistance < count) {
            __builtin_prefetch(&src_atoms[i + kIRStorageCopyPrefetchDistance], 0, 1);
            __builtin_prefetch(&src_cores[i + kIRStorageCopyPrefetchDistance], 0, 1);
            __builtin_prefetch(&dst_atoms[i + kIRStorageCopyPrefetchDistance], 1, 1);
            __builtin_prefetch(&dst_cores[i + kIRStorageCopyPrefetchDistance], 1, 1);
        }
#endif
        dst_atoms[i+0u] = src_atoms[i+0u]; dst_atoms[i+1u] = src_atoms[i+1u];
        dst_atoms[i+2u] = src_atoms[i+2u]; dst_atoms[i+3u] = src_atoms[i+3u];
        dst_atoms[i+4u] = src_atoms[i+4u]; dst_atoms[i+5u] = src_atoms[i+5u];
        dst_atoms[i+6u] = src_atoms[i+6u]; dst_atoms[i+7u] = src_atoms[i+7u];

        dst_cores[i+0u] = src_cores[i+0u]; dst_cores[i+1u] = src_cores[i+1u];
        dst_cores[i+2u] = src_cores[i+2u]; dst_cores[i+3u] = src_cores[i+3u];
        dst_cores[i+4u] = src_cores[i+4u]; dst_cores[i+5u] = src_cores[i+5u];
        dst_cores[i+6u] = src_cores[i+6u]; dst_cores[i+7u] = src_cores[i+7u];
    }

    for (; i < count; ++i) {
        dst_atoms[i] = src_atoms[i];
        dst_cores[i] = src_cores[i];
    }
}

#undef DGTL_IR_STORAGE_RESTRICT

} // namespace detail

static_assert(sizeof(IRAtom) == sizeof(dgtl::ir::IRAtom),
              "IRStorage must use canonical IRAtom");
static_assert(sizeof(IRCore) == sizeof(dgtl::ir::IRCore),
              "IRStorage must use canonical IRCore");
static_assert(sizeof(IRNode) == sizeof(dgtl::ir::IRNode),
              "IRStorage must use canonical IRNode");

struct IRStorage {
    IRAtom* atoms         = nullptr;
    IRCore* cores         = nullptr;

    IRAtom* shadow_atoms  = nullptr;
    IRCore* shadow_cores  = nullptr;

    IRNode* joined_buffer = nullptr;

    std::size_t count        = 0u;
    std::size_t shadow_count = 0u;
    std::size_t capacity     = 0u;

    std::atomic<bool> in_transaction{false};
    bool arena_backed = false;

    [[nodiscard]] static constexpr bool is_valid_result_type_handle(std::uint32_t type_handle) noexcept {
        return type_handle == 0u || is_valid_type_handle(type_handle);
    }

    [[nodiscard]] bool has_allocated_buffers() const noexcept {
        return atoms && cores && shadow_atoms && shadow_cores && joined_buffer;
    }

    void allocate_aligned(std::size_t cap, bool use_arena,
                          dgtl::core::DGTL_Arena* arena = nullptr)
    {
        capacity     = (cap > 0u) ? cap : 1u;
        count        = 0u;
        shadow_count = 0u;
        in_transaction.store(false, std::memory_order_release);
        arena_backed = use_arena;

        if (use_arena && arena) {
            atoms         = arena->allocate<IRAtom>(capacity);
            cores         = arena->allocate<IRCore>(capacity);
            shadow_atoms  = arena->allocate<IRAtom>(capacity);
            shadow_cores  = arena->allocate<IRCore>(capacity);
            joined_buffer = arena->allocate<IRNode>(capacity);
        }
        else {
            auto round_up = [](std::size_t size, std::size_t align) noexcept {
                return (size + align - 1u) & ~(align - 1u);
            };
            std::size_t sz_atom = round_up(capacity * sizeof(IRAtom), 64u);
            std::size_t sz_core = round_up(capacity * sizeof(IRCore), 64u);
            std::size_t sz_node = round_up(capacity * sizeof(IRNode), 64u);
            const std::uint32_t node = dgtl::runtime::numa::get_current_node();
            
            atoms         = static_cast<IRAtom*>(dgtl::runtime::numa::allocate_on_node(sz_atom, node, 64u));
            cores         = static_cast<IRCore*>(dgtl::runtime::numa::allocate_on_node(sz_core, node, 64u));
            shadow_atoms  = static_cast<IRAtom*>(dgtl::runtime::numa::allocate_on_node(sz_atom, node, 64u));
            shadow_cores  = static_cast<IRCore*>(dgtl::runtime::numa::allocate_on_node(sz_core, node, 64u));
            joined_buffer = static_cast<IRNode*>(dgtl::runtime::numa::allocate_on_node(sz_node, node, 64u));
            
            if (!atoms || !cores || !shadow_atoms || !shadow_cores || !joined_buffer) [[unlikely]] {
                deallocate();
                return;
            }
        }

        if (atoms)         std::memset(atoms,         0, capacity * sizeof(IRAtom));
        if (shadow_atoms)  std::memset(shadow_atoms,  0, capacity * sizeof(IRAtom));
        if (joined_buffer) std::memset(joined_buffer, 0, capacity * sizeof(IRNode));
        
        // Type-safe loops targeting non-trivial array types
        if (cores) {
            for (std::size_t i = 0u; i < capacity; ++i) {
                cores[i] = IRCore{};
            }
        }
        if (shadow_cores) {
            for (std::size_t i = 0u; i < capacity; ++i) {
                shadow_cores[i] = IRCore{};
            }
        }
    }

    void deallocate() {
        if (arena_backed) return;
        if (atoms)         dgtl::runtime::numa::free_allocation(atoms);
        if (cores)         dgtl::runtime::numa::free_allocation(cores);
        if (shadow_atoms)  dgtl::runtime::numa::free_allocation(shadow_atoms);
        if (shadow_cores)  dgtl::runtime::numa::free_allocation(shadow_cores);
        if (joined_buffer) dgtl::runtime::numa::free_allocation(joined_buffer);
        atoms         = nullptr;
        cores         = nullptr;
        shadow_atoms = nullptr;
        shadow_cores = nullptr;
        joined_buffer = nullptr;
        count = shadow_count = capacity = 0u;
        in_transaction.store(false, std::memory_order_release);
    }

    inline void commit_transaction() noexcept {
        if (shadow_count == 0u) {
            in_transaction.store(false, std::memory_order_release);
            return;
        }
        if (!atoms || !cores || !shadow_atoms || !shadow_cores) [[unlikely]] {
            in_transaction.store(false, std::memory_order_release);
            return;
        }
        if (shadow_count <= capacity) {
            detail::copy_atom_core_stream(atoms, cores, shadow_atoms, shadow_cores, shadow_count);
            count = shadow_count;
            shadow_count = 0u;
        }
        in_transaction.store(false, std::memory_order_release);
    }
};

} // namespace dgtl::ir




