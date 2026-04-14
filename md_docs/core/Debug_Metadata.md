# DGTL Debug Metadata (Debug_Matadata.hpp)

## Control Flow Map

```
[DGTLDebugStore Class]
    |
    +--> Constructor (DGTLDebugStore(DGTLArena& arena, size_t max_nodes))
    |       +--> Allocate primary and shadow SourceLoc arrays (64-byte aligned)
    |       +--> Initialize capacity and count
    |       +--> Memset arrays to zero
    |
    +--> Metadata Recording
    |       +--> record_loc(size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id, bool is_shadow)
    |       |       +--> Check bounds
    |       |       +--> Select target array (shadow or primary)
    |       |       +--> Write SourceLoc struct
    |       |       +--> Atomically update count if necessary
    |
    +--> Metadata Retrieval
    |       +--> get_loc(size_t node_idx, bool prefer_shadow)
    |       |       +--> Check bounds against current count
    |       |       +--> Return shadow location if available and preferred
    |       |       +--> Otherwise return primary location
    |
    +--> Transactional Operations
    |       +--> commit_shadow_slice(size_t start, size_t end)
    |       |       +--> Copy validated shadow entries to primary
    |       |       +--> Zero out shadow entries after commit
    |       |
    |       +--> rollback_shadow_slice(size_t start, size_t end)
    |               +--> Zero out shadow entries on failure
    |
    +--> DWARF Emission
    |       +--> emit_dwarf_line_table()
    |       |       +--> Reserve buffer space
    |       |       +--> Iterate through primary locations
    |       |       +--> Calculate line deltas
    |       |       +--> Encode DWARF line table opcodes
    |       |       +--> Return compressed buffer
    |
    +--> Utility Methods
    |       +--> write_sleb128(uint8_t* dest, int64_t value)
    |       |       +--> Encode signed LEB128 value
    |       |       +--> Handle variable-length encoding
    |       |       +--> Return bytes written
    |       |
    |       +--> reset()
    |               +--> Reset count to 0
    |               +--> Clear scopes vector
    |               +--> Memset location arrays to zero
```

**Flow Description**: The debug store manages source location metadata with dual buffering for transactional safety. It records line/column/file mappings during IR emission, supports shadow validation during optimization strikes, and emits compressed DWARF line tables for debugging.

## Summary

`Debug_Matadata.hpp` implements transactional debug metadata storage with DWARF-compatible line table generation, providing source location mapping for JIT and AOT debugging. It features dual-buffered storage (primary + shadow) for Pillar 2 transactional validation and compressed LEB128 encoding for efficient debug information.

Key features:
- Dual-buffered metadata (primary + shadow) for transactions
- 64-byte aligned storage for cache efficiency
- Atomic count updates for thread safety
- DWARF .debug_line table generation
- Signed LEB128 compression for compact encoding
- Shadow-aware retrieval during optimization strikes

## Full Explanation

### Architecture Overview
The DGTLDebugStore serves as the debug information management system for DGTL's compilation pipeline:

1. **Transactional Storage**: Dual buffers for safe speculative transformations
2. **Source Mapping**: Line/column/file location tracking per IR node
3. **DWARF Compatibility**: Standard debug format generation
4. **Compression**: LEB128 encoding for space efficiency
5. **Thread Safety**: Atomic operations for concurrent access

### Key Classes and Structures

#### SourceLoc
Compact source location structure:

```cpp
struct alignas(8) SourceLoc {
    uint32_t line;     // Source line number
    uint16_t column;   // Source column number  
    uint16_t file_id;  // File identifier index
};
```

- **8-byte Alignment**: Single CPU cycle load/store operations
- **Compact Layout**: 8 bytes total for efficient storage
- **Standard Fields**: DWARF-compatible location information

#### DGTLDebugStore
Main debug metadata class:

```cpp
class DGTLDebugStore {
    SourceLoc* locations = nullptr;      // Primary stable mappings
    SourceLoc* shadow_locations = nullptr; // Shadow speculative mappings
    
    size_t capacity = 0;
    std::atomic<size_t> count{0};        // Current number of recorded locations
    
    struct ScopeEntry { /* ... */ };
    std::vector<ScopeEntry> scopes;      // Scope information
    
public:
    explicit DGTLDebugStore(core::DGTLArena& arena, size_t max_nodes);
    // ... recording and retrieval methods
};
```

### Metadata Recording

#### Location Recording
```cpp
void record_loc(size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id, bool is_shadow = false) noexcept {
    if (node_idx < capacity) {
        SourceLoc* target = is_shadow ? shadow_locations : locations;
        target[node_idx] = {line, col, file_id};
        
        // Atomic count update for thread safety
        size_t current = count.load(std::memory_order_relaxed);
        while (node_idx + 1 > current && !count.compare_exchange_weak(current, node_idx + 1));
    }
}
```

- **Bounds Checking**: Prevent out-of-bounds writes
- **Tier Selection**: Shadow or primary based on transaction state
- **Atomic Update**: Thread-safe count maintenance
- **Relaxed Ordering**: Performance optimization for frequent updates

#### Location Retrieval
```cpp
SourceLoc get_loc(size_t node_idx, bool prefer_shadow = false) const noexcept {
    if (node_idx >= count.load(std::memory_order_relaxed)) return {0, 0, 0};
    
    if (prefer_shadow && shadow_locations[node_idx].line != 0) {
        return shadow_locations[node_idx];
    }
    return locations[node_idx];
}
```

- **Bounds Validation**: Check against current recorded count
- **Shadow Preference**: Use speculative mappings when available
- **Fallback**: Primary mappings for stable locations
- **Zero Return**: Invalid locations return zeroed struct

### Transactional Operations

#### Shadow Commit
```cpp
void commit_shadow_slice(size_t start, size_t end) noexcept {
    const size_t limit = std::min(end, capacity);
    for (size_t i = start; i < limit; ++i) {
        if (shadow_locations[i].line != 0) {
            locations[i] = shadow_locations[i];
            // Clean shadow after commit
            shadow_locations[i] = {0, 0, 0};
        }
    }
}
```

- **Range Commit**: Transfer validated range to primary storage
- **Conditional Copy**: Only non-zero locations are committed
- **Cleanup**: Zero shadow entries to prevent stale data
- **Bounds Safety**: Respect capacity limits

#### Shadow Rollback
```cpp
void rollback_shadow_slice(size_t start, size_t end) noexcept {
    const size_t limit = std::min(end, capacity);
    for (size_t i = start; i < limit; ++i) {
        shadow_locations[i] = {0, 0, 0};
    }
}
```

- **Range Cleanup**: Zero out failed speculative transformations
- **No Data Loss**: Primary storage remains untouched
- **Reset State**: Prepare shadow for next transaction

### DWARF Generation

#### Line Table Emission
```cpp
std::vector<uint8_t> emit_dwarf_line_table() const {
    std::vector<uint8_t> buffer;
    const size_t current_count = count.load(std::memory_order_acquire);
    buffer.reserve(current_count * 2);
    
    uint32_t last_line = 1;
    uint8_t temp_leb[10];
    
    for (size_t i = 0; i < current_count; ++i) {
        const auto& loc = locations[i];
        if (loc.line == 0) continue;
        
        int32_t line_delta = static_cast<int32_t>(loc.line) - static_cast<int32_t>(last_line);
        
        if (line_delta != 0) {
            // DW_LNS_advance_line
            buffer.push_back(0x03);
            size_t n = write_sleb128(temp_leb, line_delta);
            buffer.insert(buffer.end(), temp_leb, temp_leb + n);
            last_line = loc.line;
        }
        // DW_LNS_copy
        buffer.push_back(0x01);
    }
    return buffer;
}
```

- **Buffer Reservation**: Pre-allocate for efficiency
- **Delta Encoding**: Only encode line changes
- **LEB128 Compression**: Variable-length signed encoding
- **DWARF Opcodes**: Standard line table operations

#### LEB128 Encoding
```cpp
static size_t write_sleb128(uint8_t* dest, int64_t value) noexcept {
    uint8_t* p = dest;
    bool more = true;
    while (more) {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        *p++ = byte;
    }
    return static_cast<size_t>(p - dest);
}
```

- **Variable Length**: 7 bits per byte, MSB continuation flag
- **Sign Extension**: Proper handling of negative values
- **Compact**: Small values use fewer bytes
- **Standard**: DWARF-compatible encoding

### Memory Management

#### Constructor Allocation
```cpp
explicit DGTLDebugStore(core::DGTLArena& arena, size_t max_nodes) {
    capacity = max_nodes;
    // 64-byte alignment for cache line optimization
    locations = arena.allocate<SourceLoc>(max_nodes, 64);
    shadow_locations = arena.allocate<SourceLoc>(max_nodes, 64);
    
    if (!locations || !shadow_locations) {
        capacity = 0;
    } else {
        std::memset(locations, 0, sizeof(SourceLoc) * max_nodes);
        std::memset(shadow_locations, 0, sizeof(SourceLoc) * max_nodes);
    }
}
```

- **Arena Allocation**: Uses DGTL's transactional memory system
- **Paired Buffers**: Primary and shadow arrays
- **Cache Alignment**: 64-byte alignment for performance
- **Zero Initialization**: Clean state for all locations

#### Reset Operation
```cpp
void reset() noexcept {
    count.store(0, std::memory_order_release);
    scopes.clear();
    if (locations && capacity > 0) {
        std::memset(locations, 0, sizeof(SourceLoc) * capacity);
        std::memset(shadow_locations, 0, sizeof(SourceLoc) * capacity);
    }
}
```

- **Atomic Reset**: Thread-safe count reset
- **Scope Cleanup**: Clear scope information
- **Memory Wipe**: Zero all location data

### Performance Characteristics

- **Recording**: O(1) with atomic count updates
- **Retrieval**: O(1) array access
- **Commit/Rollback**: O(range_size) for affected nodes
- **DWARF Emission**: O(node_count) with LEB128 compression
- **Memory**: 8 bytes per node (16 bytes with shadow)

### Integration Points

- **IR Emission**: Location recording during code generation
- **Transactional IR**: Shadow buffer management
- **Sentinel System**: Validation before commit
- **JIT Executor**: Debug information for runtime debugging
- **DWARF Generator**: Line table emission for debuggers

### Usage Example

```cpp
#include "dgtl/core/Debug_Matadata.hpp"

int main() {
    dgtl::core::DGTLArena arena(256); // 256MB
    dgtl::debug::DGTLDebugStore debug_store(arena, 1000000); // 1M nodes
    
    // Record source locations during IR emission
    debug_store.record_loc(0, 42, 10, 1, false); // Primary buffer
    debug_store.record_loc(1, 43, 5, 1, true);   // Shadow buffer (transaction)
    
    // Retrieve location information
    auto loc = debug_store.get_loc(0, true); // Prefer shadow if available
    std::cout << "Line: " << loc.line << ", Col: " << loc.column << std::endl;
    
    // Commit validated shadow changes
    debug_store.commit_shadow_slice(0, 100);
    
    // Rollback failed transformations
    debug_store.rollback_shadow_slice(100, 200);
    
    // Generate DWARF debug information
    auto dwarf_data = debug_store.emit_dwarf_line_table();
    
    // Reset for new compilation
    debug_store.reset();
    
    return 0;
}
```

### DWARF Integration

The debug store generates standard DWARF line table opcodes:

- **DW_LNS_advance_line (0x03)**: Advance line number by signed delta
- **DW_LNS_copy (0x01)**: Copy current state to line table matrix

This enables full debugger integration for both JIT and AOT compilation modes.

### Future Enhancements

- Column number tracking for precise breakpoints
- Inline function debug information
- Variable location tracking
- Scope information emission
- Compressed storage formats
- Parallel debug info generation

This header provides the debug metadata foundation for DGTL's compilation pipeline, enabling accurate source-level debugging with transactional safety and standard DWARF compatibility.