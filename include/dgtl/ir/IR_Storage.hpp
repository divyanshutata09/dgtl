/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * STORAGE ENGINE: SoA Logic, Transactional Buffers, & Arena Integration
 * STANDARDS: C++26 / UCRT64 (Cache-Aligned Transactional Storage)
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#include <malloc.h> 

#include "../core/Arena.hpp"
#include "IR_Node.hpp"

namespace dgtl::ir {

/**
 * @struct IRStorage
 * @brief Internal storage controller for IRStream. 
 * Manages dual-tier (Primary/Shadow) SoA buffers and memory lifecycle.
 * Optimized for 64-byte cache-line alignment to prevent false sharing during 
 * transactional emission.
 */
struct IRStorage {
    // Primary SoA Buffers (Tier 1 & 2)
    IRAtom* atoms         = nullptr;
    IRCore* cores         = nullptr;
    
    // Shadow SoA Buffers (Pillar 2 & 3 Validation)
    IRAtom* shadow_atoms  = nullptr;
    IRCore* shadow_cores  = nullptr;

    // View-Sync Buffer (AoS view for debugging/legacy emitters)
    mutable IRNode* joined_buffer = nullptr;

    size_t count          = 0;
    size_t shadow_count   = 0; 
    size_t capacity       = 0; 
    
    std::atomic<bool> in_transaction{false};
    bool owns_buffer      = false; // Logic: Managed by Arena vs Managed by OS

    /**
     * @brief Allocates aligned memory blocks for SoA processing.
     * ✅ BIR 2.1: Correctly accounts for 20-byte IRCore structures and 24-byte IRNodes.
     */
    void allocate_aligned(size_t cap, bool use_arena, dgtl::core::DGTLArena* arena = nullptr) {
        capacity = cap;
        owns_buffer = use_arena;

        if (use_arena && arena) {
            // PILLAR 1: Deterministic Arena Allocation
            atoms         = arena->allocate<IRAtom>(capacity, 64);
            cores         = arena->allocate<IRCore>(capacity, 64);
            shadow_atoms  = arena->allocate<IRAtom>(capacity, 64);
            shadow_cores  = arena->allocate<IRCore>(capacity, 64);
            joined_buffer = arena->allocate<IRNode>(capacity, 64);
        } else {
            // Fallback: OS-level Aligned Allocation (64-byte for Cache Line Optimization)
            size_t sz_atom   = capacity * sizeof(IRAtom);
            size_t sz_core   = capacity * sizeof(IRCore);
            size_t sz_node   = capacity * sizeof(IRNode);

#ifdef _WIN32
            atoms         = static_cast<IRAtom*>(_aligned_malloc(sz_atom, 64));
            cores         = static_cast<IRCore*>(_aligned_malloc(sz_core, 64));
            shadow_atoms  = static_cast<IRAtom*>(_aligned_malloc(sz_atom, 64));
            shadow_cores  = static_cast<IRCore*>(_aligned_malloc(sz_core, 64));
            joined_buffer = static_cast<IRNode*>(_aligned_malloc(sz_node, 64));
#else
            atoms         = static_cast<IRAtom*>(aligned_alloc(64, sz_atom));
            cores         = static_cast<IRCore*>(aligned_alloc(64, sz_core));
            shadow_atoms  = static_cast<IRAtom*>(aligned_alloc(64, sz_atom));
            shadow_cores  = static_cast<IRCore*>(aligned_alloc(64, sz_core));
            joined_buffer = static_cast<IRNode*>(aligned_alloc(64, sz_node));
#endif
        }
        
        // Zero-initialize primary buffers for safe initial state
        if (atoms) std::memset(atoms, 0, capacity * sizeof(IRAtom));
        if (cores) std::memset(cores, 0, capacity * sizeof(IRCore));
        if (shadow_atoms) std::memset(shadow_atoms, 0, capacity * sizeof(IRAtom));
        if (shadow_cores) std::memset(shadow_cores, 0, capacity * sizeof(IRCore));
    }

    /**
     * @brief High-Velocity buffer deallocation.
     * Skips cleanup if Arena-backed (Tier 1 logic).
     */
    void deallocate() {
        if (owns_buffer) return; 

#ifdef _WIN32
        if (atoms)         _aligned_free(atoms);
        if (cores)         _aligned_free(cores);
        if (shadow_atoms)  _aligned_free(shadow_atoms);
        if (shadow_cores)  _aligned_free(shadow_cores);
        if (joined_buffer) _aligned_free(joined_buffer);
#else
        if (atoms)         free(atoms);
        if (cores)         free(cores);
        if (shadow_atoms)  free(shadow_atoms);
        if (shadow_cores)  free(shadow_cores);
        if (joined_buffer) free(joined_buffer);
#endif
        // Clear pointers to prevent use-after-free
        atoms = shadow_atoms = nullptr;
        cores = shadow_cores = nullptr;
        joined_buffer = nullptr;
        count = shadow_count = capacity = 0;
    }

    /**
     * @brief Commit Shadow buffer to Primary.
     * Zero-cost pointer swap is not feasible due to SoA fixed arrays, 
     * so we perform a high-speed blit.
     */
    inline void commit_transaction() noexcept {
        if (shadow_count > 0 && shadow_count <= capacity) {
            std::memcpy(atoms + count, shadow_atoms, shadow_count * sizeof(IRAtom));
            std::memcpy(cores + count, shadow_cores, shadow_count * sizeof(IRCore));
            count += shadow_count;
            shadow_count = 0;
        }
        in_transaction.store(false, std::memory_order_release);
    }
};

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
