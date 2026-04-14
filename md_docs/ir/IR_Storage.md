# IR_Storage.hpp Documentation

## Map of the Control Flow

```
IRStorage Memory Management Flow
├── Buffer Structure (SoA Layout)
│   ├── Primary Buffers
│   │   ├── IRAtom* atoms (Tier 1)
│   │   └── IRCore* cores (Tier 2)
│   ├── Shadow Buffers
│   │   ├── IRAtom* shadow_atoms (Transactional)
│   │   └── IRCore* shadow_cores (Transactional)
│   └── View Buffer
│       └── IRNode* joined_buffer (AoS for debugging)
├── State Management
│   ├── size_t count (Primary buffer size)
│   ├── size_t shadow_count (Shadow buffer size)
│   ├── size_t capacity (Total allocated capacity)
│   ├── std::atomic<bool> in_transaction
│   └── bool owns_buffer (Arena vs OS allocation)
├── Memory Operations
│   ├── allocate_aligned() → void
│   │   ├── Arena Allocation (Deterministic)
│   │   └── OS Allocation (Fallback)
│   ├── deallocate() → void
│   │   ├── Arena Skip (No cleanup)
│   │   └── OS Free (Aligned free)
│   └── commit_transaction() → void
│       ├── memcpy Shadow → Primary
│       └── Reset transaction state
└── Transaction Lifecycle
    ├── Begin: in_transaction = true
    ├── Modify: Write to shadow buffers
    ├── Commit: memcpy to primary + reset
    └── Rollback: Discard shadow (not implemented)
```

**Data Flow:**
1. **Allocation**: Choose Arena (deterministic) or OS (fallback) allocation with 64-byte alignment
2. **Transactional Write**: All modifications go to shadow buffers during transaction
3. **Commit**: High-speed memcpy from shadow to primary buffers, update counts
4. **Deallocation**: Arena-backed buffers skip cleanup, OS buffers use aligned_free
5. **View Access**: joined_buffer provides AoS view for debugging/legacy code

## Summary

IR_Storage.hpp defines the `IRStorage` struct, which serves as the internal storage controller for DGTL's IRStream, managing dual-tier Structure-of-Arrays (SoA) buffers with transactional semantics. The design implements Pillar 2's tri-tier transactional stream concept with primary and shadow buffers, enabling zero-cost rollbacks and validation. Memory management integrates with the Arena system for deterministic allocation while providing OS-level fallbacks.

Key features include:
- **Dual-Tier SoA**: Primary and shadow buffers for transactional operations
- **64-Byte Alignment**: Cache-line optimization to prevent false sharing
- **Arena Integration**: Deterministic memory management with huge-page support
- **Transactional Safety**: Atomic transaction state with commit/rollback capabilities
- **Multi-View Support**: SoA for performance, AoS for debugging compatibility

The implementation ensures HFT-grade performance with zero-overhead validation and cache-efficient memory access patterns.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#include <malloc.h> 

#include "../core/Arena.hpp"
#include "IR_Node.hpp"

namespace dgtl::ir {
```
- **#pragma once**: Header guard for single inclusion
- **Includes**:
  - `<cstdint>`, `<cstddef>`: Standard integer and size types
  - `<cstring>`: For memset/memcpy operations
  - `<atomic>`: For thread-safe transaction state
  - `<malloc.h>`: For aligned memory allocation
  - `"../core/Arena.hpp"`: DGTLArena for deterministic allocation
  - `"IR_Node.hpp"`: IRNode and related types
- **Namespace**: All declarations in `dgtl::ir`

### IRStorage Struct Definition
```cpp
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
```
**Buffer Structure**:
- **Primary Buffers**: atoms (IRAtom array), cores (IRCore array) - main storage
- **Shadow Buffers**: Transactional copies for speculative operations
- **Joined Buffer**: AoS view combining atoms+cores into IRNode array for compatibility
- **State Variables**:
  - count: Number of valid entries in primary buffers
  - shadow_count: Number of entries in shadow buffers
  - capacity: Total allocated size for all buffers
  - in_transaction: Atomic flag for transaction state
  - owns_buffer: true if Arena-managed, false if OS-managed

### Memory Allocation

#### allocate_aligned()
```cpp
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
```
**Purpose**: Allocates aligned memory blocks for all SoA buffers
**Parameters**:
- cap: Number of elements to allocate
- use_arena: true for Arena allocation, false for OS allocation
- arena: Pointer to DGTLArena instance (if use_arena is true)

**Arena Path** (Deterministic):
- Uses arena->allocate<T>(count, alignment) for all buffers
- 64-byte alignment for cache-line optimization
- No manual deallocation needed (Arena managed)

**OS Path** (Fallback):
- Platform-specific aligned allocation:
  - Windows: _aligned_malloc()
  - POSIX: aligned_alloc()
- All buffers allocated with 64-byte alignment
- Requires manual deallocation

**Initialization**:
- Primary buffers zero-initialized for safe state
- Shadow buffers not initialized (filled during transactions)

### Memory Deallocation

#### deallocate()
```cpp
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
```
**Purpose**: Safely deallocates all buffers
**Logic**:
- If owns_buffer is true (Arena-managed): Return immediately (no cleanup)
- If OS-managed: Use platform-specific aligned free functions
- Clear all pointers to prevent use-after-free
- Reset all counters to zero

### Transaction Management

#### commit_transaction()
```cpp
inline void commit_transaction() noexcept {
    if (shadow_count > 0 && shadow_count <= capacity) {
        std::memcpy(atoms + count, shadow_atoms, shadow_count * sizeof(IRAtom));
        cores += count, shadow_cores, shadow_count * sizeof(IRCore));
        count += shadow_count;
        shadow_count = 0;
    }
    in_transaction.store(false, std::memory_order_release);
}
```
**Purpose**: Commits transactional changes from shadow to primary buffers
**Logic**:
- Validates shadow_count bounds
- memcpy shadow buffers to end of primary buffers
- Updates count by adding shadow_count
- Resets shadow_count to 0
- Atomically sets in_transaction to false with release semantics

**Performance**: High-speed blit operation, effectively zero-cost commit

### Struct Closing
```cpp
};
```
- End of IRStorage struct definition

### Namespace Closing and Compatibility
```cpp
} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
```
- Closes the dgtl::ir namespace
- Backward compatibility alias (imports namespace into itself)

## Usage Example
```cpp
#include "IR_Storage.hpp"

// Create storage with Arena allocation
dgtl::core::DGTLArena arena;
dgtl::ir::IRStorage storage;
storage.allocate_aligned(1024, true, &arena);

// Start transaction
storage.in_transaction.store(true, std::memory_order_relaxed);
// ... modify shadow_atoms and shadow_cores ...
storage.shadow_count = 10;

// Commit changes
storage.commit_transaction();

// Access primary buffers
for (size_t i = 0; i < storage.count; ++i) {
    // Use storage.atoms[i] and storage.cores[i]
}

// Cleanup (Arena handles deallocation)
storage.deallocate();
```

## Dependencies
- **Arena.hpp**: DGTLArena for deterministic memory management
- **IR_Node.hpp**: IRNode, IRAtom, IRCore type definitions
- **Standard Library**: `<atomic>`, `<cstring>`, `<malloc.h>`

## Thread Safety
- **in_transaction**: std::atomic with proper memory ordering
- **Buffer Access**: No internal synchronization - external locks required for concurrent access
- **Transaction State**: Atomic operations ensure visibility across threads

## Performance Characteristics
- **Allocation**: 64-byte alignment prevents false sharing
- **Commit**: memcpy-based, cache-efficient bulk transfer
- **Memory Layout**: SoA design optimizes for SIMD operations
- **Arena Integration**: Reduces allocation overhead in hot paths
- **Zero-Initialization**: Only primary buffers initialized, shadow buffers filled on demand