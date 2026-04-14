# DGTL Arena (Arena.hpp)

## Control Flow Map

```
[DGTLArena Class]
    |
    +--> Constructor (DGTLArena(size_t size_in_mb))
    |       +--> Calculate page-aligned size
    |       +--> Allocate memory (Windows: VirtualAlloc, POSIX: mmap)
    |       +--> Lock memory pages (VirtualLock/mlock)
    |       +--> Initialize offset to 0
    |
    +--> Allocation Methods
    |       +--> allocate(size_t size, size_t alignment)
    |       |       +--> Align offset to alignment
    |       |       +--> Check bounds
    |       |       +--> Increment offset
    |       |       +--> Return pointer
    |       |
    |       +--> allocate_zeroed(size_t size, size_t alignment)
    |       |       +--> Allocate and memset to 0
    |       |
    |       +--> allocate_aligned(size_t size, size_t alignment)
    |               +--> Force specific alignment
    |
    +--> Transactional Methods
    |       +--> checkpoint() -> size_t
    |       |       +--> Return current offset
    |       |
    |       +--> rollback(size_t checkpoint)
    |               +--> Reset offset to checkpoint
    |
    +--> Access Methods
    |       +--> raw_data() -> uint8_t*
    |       +--> size() -> size_t
    |       +--> remaining() -> size_t
    |       +--> is_empty() -> bool
    |
    +--> Destructor
            +--> Unlock memory (VirtualUnlock/munlock)
            +--> Free memory (VirtualFree/munmap)
```

**Flow Description**: The arena manages a contiguous memory block with transactional allocation. Memory is allocated upfront, then subdivided with alignment guarantees. Transactions enable rollback for speculative operations.

## Summary

`Arena.hpp` implements DGTL's transactional memory arena, providing deterministic, high-performance memory management optimized for compiler IR storage. It uses OS huge-pages (2MB) for efficient virtual memory handling and supports zero-latency rollback through offset-based checkpointing. The arena guarantees 64-byte alignment for SIMD operations and includes memory locking for real-time performance.

Key features:
- Huge-page allocation for reduced TLB misses
- Transactional allocation with rollback support
- Cross-platform memory locking
- SIMD-friendly alignment (64-byte)
- Bounds checking and overflow protection

## Full Explanation

### Architecture Overview
The DGTLArena is designed as Pillar 1 of DGTL's memory architecture, providing the foundation for all IR storage and manipulation:

1. **Huge-Page Allocation**: Uses 2MB pages to minimize TLB overhead
2. **Memory Locking**: Prevents page faults in real-time systems
3. **Transactional Model**: Checkpoint/rollback for speculative compilation
4. **Alignment Guarantees**: 64-byte alignment for AVX-512 operations
5. **Cross-Platform**: Supports Windows and POSIX systems

### Key Classes and Functions

#### DGTLArena
Main arena class:

```cpp
class DGTLArena {
public:
    DGTLArena(); // Default 256MB
    explicit DGTLArena(size_t size_in_mb);
    ~DGTLArena();
    
    void* allocate(size_t size, size_t alignment = 64);
    void* allocate_zeroed(size_t size, size_t alignment = 64);
    void* allocate_aligned(size_t size, size_t alignment);
    
    size_t checkpoint() const;
    void rollback(size_t checkpoint);
    
    uint8_t* raw_data();
    size_t size() const;
    size_t remaining() const;
    bool is_empty() const;
};
```

#### Constructor Details
- **Default**: Allocates 256MB for standard use
- **Parameterized**: Takes size in MB, rounds to 2MB page boundary
- **Allocation**: Uses platform-specific APIs for huge pages

#### Memory Allocation Strategy

##### Windows Implementation
```cpp
base_ptr = static_cast<uint8_t*>(
    VirtualAlloc(nullptr, total_size, 
                 MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, 
                 PAGE_READWRITE)
);
VirtualLock(base_ptr, total_size);
```

##### POSIX Implementation
```cpp
base_ptr = static_cast<uint8_t*>(
    mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0)
);
mlock(base_ptr, total_size);
```

### Transactional Operations

The arena supports speculative allocation with rollback:

```cpp
size_t checkpoint = arena.checkpoint();
// Perform speculative allocations
// ...
arena.rollback(checkpoint); // Undo all allocations since checkpoint
```

- **Checkpoint**: Returns current offset
- **Rollback**: Resets offset, effectively freeing memory
- **Zero Overhead**: No additional data structures needed

### Alignment Handling

All allocations are aligned to 64 bytes by default:

```cpp
size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
if (aligned_offset + size > capacity) {
    // Handle out of memory
}
offset = aligned_offset + size;
```

- **Power-of-2 Alignment**: Efficient bitwise operations
- **Overflow Protection**: Bounds checking before allocation
- **SIMD Ready**: 64-byte alignment enables AVX-512

### Error Handling

Following zero-exception policy:

```cpp
if (!base_ptr) {
    std::cerr << "[DGTL-FATAL] Arena allocation failed\n";
    std::abort();
}
```

- Allocation failures: std::abort()
- Bounds violations: Internal checks (no exceptions)

### Performance Characteristics

- **Allocation Speed**: O(1) - simple offset increment
- **Memory Overhead**: Minimal (just base pointer and counters)
- **TLB Efficiency**: 2MB pages reduce translation overhead
- **Cache Performance**: Contiguous allocation improves locality

### Usage Example

```cpp
#include "dgtl/core/Arena.hpp"

int main() {
    dgtl::core::DGTLArena arena(64); // 64MB arena
    
    // Allocate aligned memory
    int* data = static_cast<int*>(arena.allocate(sizeof(int) * 100, 64));
    
    // Transactional allocation
    size_t cp = arena.checkpoint();
    float* temp = static_cast<float*>(arena.allocate(sizeof(float) * 50));
    // Use temp...
    arena.rollback(cp); // Free temp allocation
    
    // data still valid
    return 0;
}
```

### Integration with IR System

- **IRStream**: Uses arena for node storage
- **Passes**: Allocate temporary data structures
- **Code Generation**: Buffer for emitted machine code
- **JIT**: Executable memory allocation

### Platform-Specific Details

#### Windows
- Requires `SeLockMemoryPrivilege` for large pages
- Uses `VirtualAlloc` with `MEM_LARGE_PAGES`
- `VirtualLock` prevents paging

#### Linux
- Requires `CAP_IPC_LOCK` for mlock
- Uses `mmap` with `MAP_HUGETLB`
- `mlock` locks pages in RAM

### Future Enhancements

- NUMA-aware allocation
- Memory defragmentation
- Compression for large arenas
- Hardware-specific alignment

This header provides the memory foundation for DGTL's deterministic, high-performance compilation pipeline.