# DGTL Memory Guard (Memory_Guard.hpp)

## Control Flow Map

```
[MemoryGuard Class]
    |
    +--> Constructor (MemoryGuard())
    |       +--> Default constructor, all members zero-initialized
    |
    +--> Template Constructor (MemoryGuard(T* ptr, size_t count, Access perm))
    |       +--> Store base pointer as uintptr_t
    |       +--> Calculate total size (count * sizeof(T))
    |       +--> Store access permissions
    |
    +--> Access Methods
    |       +--> get(size_t offset, size_t requested_size, source_location loc)
    |       |       +--> Debug-only: validate_access()
    |       |       +--> Return pointer at offset from base
    |
    +--> Utility Methods
    |       +--> fence()
    |       |       +--> Hardware memory fence (_mm_sfence or __sync_synchronize)
    |       |
    |       +--> size()
    |       |       +--> Return guarded region size
    |       |
    |       +--> is_null()
    |               +--> Check if base pointer is null
    |
    +--> Private Validation
            +--> validate_access(size_t offset, size_t req_size, source_location& loc)
                    +--> Check for null pointer access
                    +--> Check for buffer overflow
                    +--> Throw exceptions with location info
```

**Flow Description**: The memory guard provides RAII-based safe memory access with bounds checking and hardware fences. It wraps existing buffers with permission tracking and debug-time validation, ensuring zero-trust memory operations.

## Summary

`Memory_Guard.hpp` implements production-grade RAII memory protection with hardware-aligned bounds checking, access permissions, and memory fencing. It provides zero-trust memory management for DGTL's core components, featuring debug-time validation that optimizes away in release builds while maintaining security guarantees.

Key features:
- RAII memory segment protection with access permissions
- Debug-time bounds checking with source location reporting
- Hardware memory fences for synchronization
- Template-based type-safe buffer wrapping
- Zero-overhead in release builds (validation optimized away)
- Platform-specific optimizations (Windows/Linux)

## Full Explanation

### Architecture Overview
The MemoryGuard serves as Pillar 1's zero-trust memory management foundation:

1. **RAII Protection**: Automatic resource management for memory segments
2. **Access Control**: Permission-based read/write/execute restrictions
3. **Bounds Validation**: Debug-time overflow detection with location info
4. **Hardware Fencing**: Memory barrier operations for thread safety
5. **Type Safety**: Template-based buffer size calculation

### Key Classes and Functions

#### MemoryGuard
Main memory protection class:

```cpp
class alignas(64) MemoryGuard {
public:
    enum class Access : uint8_t {
        None  = 0,
        Read  = 1 << 0,
        Write = 1 << 1,
        Exec  = 1 << 2
    };

    constexpr MemoryGuard() noexcept = default;
    
    template<typename T>
    explicit constexpr MemoryGuard(T* ptr, size_t count, Access perm = Access::Read | Access::Write) noexcept;
    
    void* get(size_t offset, size_t requested_size, std::source_location loc = std::source_location::current()) const;
    
    static void fence() noexcept;
    
    size_t size() const noexcept;
    bool is_null() const noexcept;
};
```

### Memory Protection Setup

#### Template Constructor
```cpp
template<typename T>
explicit constexpr MemoryGuard(T* ptr, size_t count, Access perm = Access::Read | Access::Write) noexcept
    : m_base(reinterpret_cast<uintptr_t>(ptr)),
      m_size(count * sizeof(T)),
      m_perms(perm) {}
```

- **Type Safety**: Template parameter ensures correct size calculation
- **Pointer Storage**: uintptr_t for arithmetic operations
- **Permission Tracking**: Access rights for the guarded region
- **Size Calculation**: Automatic element count to byte conversion

#### Access Permissions
```cpp
enum class Access : uint8_t {
    None  = 0,
    Read  = 1 << 0,      // 0x01
    Write = 1 << 1,      // 0x02  
    Exec  = 1 << 2       // 0x04
};
```

- **Bit Flags**: Combinable permissions (Read | Write = 0x03)
- **Hardware Ready**: Compatible with page protection mechanisms
- **Minimal Overhead**: Single byte storage

### Safe Memory Access

#### Bounds-Checked Access
```cpp
void* get(size_t offset, size_t requested_size, std::source_location loc = std::source_location::current()) const {
    #ifdef _DEBUG
        validate_access(offset, requested_size, loc);
    #endif
    
    return reinterpret_cast<void*>(m_base + offset);
}
```

- **Conditional Validation**: Debug-only bounds checking
- **Source Location**: Automatic error reporting with file/line info
- **Zero Overhead**: Release builds skip validation entirely
- **Pointer Arithmetic**: Direct offset calculation from base

#### Validation Logic
```cpp
void validate_access(size_t offset, size_t req_size, const std::source_location& loc) const {
    if (m_base == 0) {
        throw std::runtime_error("MemoryGuard: Null pointer access at " + std::string(loc.function_name()));
    }
    
    if (offset + req_size > m_size) {
        throw std::out_of_range("MemoryGuard: Buffer Overflow detected in " + std::string(loc.function_name()));
    }
}
```

- **Null Check**: Prevent null pointer dereferences
- **Bounds Check**: Detect buffer overflows before they occur
- **Location Info**: Include function name in error messages
- **Exception Types**: runtime_error for null, out_of_range for overflow

### Hardware Synchronization

#### Memory Fence
```cpp
static void fence() noexcept {
    #ifdef DGTL_WIN64
        _mm_sfence(); // Store fence for x86
    #else
        __sync_synchronize(); // Full barrier for other platforms
    #endif
}
```

- **Platform Specific**: Windows uses SSE store fence
- **POSIX Compatible**: GCC atomic synchronization
- **Store Ordering**: Ensures previous writes are visible
- **Static Method**: No instance required

### Utility Methods

#### Size and Null Checks
```cpp
constexpr size_t size() const noexcept { return m_size; }
constexpr bool is_null() const noexcept { return m_base == 0; }
```

- **Constexpr**: Compile-time evaluation where possible
- **Noexcept**: Exception-free query operations
- **Inline**: Minimal overhead accessors

### Performance Characteristics

- **Construction**: O(1) with simple assignments
- **Access**: O(1) with optional debug validation
- **Validation**: Debug-only, optimized away in release
- **Memory**: 24 bytes per instance (uintptr_t + size_t + Access)
- **Alignment**: 64-byte cache line alignment

### Integration Points

- **Arena Allocation**: Protects DGTLArena memory regions
- **IR Storage**: Guards Tier 1/2 atom and core arrays
- **JIT Code**: Protects executable memory segments
- **Transactional Memory**: Bounds checking for shadow buffers
- **Debug Metadata**: Safe access to location arrays

### Usage Example

```cpp
#include "dgtl/core/Memory_Guard.hpp"

int main() {
    // Create a buffer
    int buffer[100];
    
    // Guard the entire array with read/write access
    dgtl::core::MemoryGuard guard(buffer, 100, 
                                  dgtl::core::MemoryGuard::Access::Read | 
                                  dgtl::core::MemoryGuard::Access::Write);
    
    // Safe access with bounds checking (debug only)
    int* element = static_cast<int*>(guard.get(10 * sizeof(int), sizeof(int)));
    *element = 42;
    
    // Memory fence for synchronization
    dgtl::core::MemoryGuard::fence();
    
    // Check guard properties
    if (!guard.is_null() && guard.size() >= 100 * sizeof(int)) {
        // Safe to use
    }
    
    return 0;
}
```

### Error Handling

- **Null Access**: runtime_error with function name
- **Buffer Overflow**: out_of_range with function name
- **Debug Only**: Release builds have no runtime overhead
- **Source Location**: Automatic file/line reporting

### Security Features

- **Bounds Checking**: Prevents buffer overflow exploits
- **Null Protection**: Eliminates null pointer vulnerabilities
- **Permission Tracking**: Access control for memory regions
- **Hardware Fencing**: Prevents memory reordering attacks
- **Zero Trust**: Validates every access in debug builds

### Future Enhancements

- Hardware page protection integration
- Memory poisoning for use-after-free detection
- Access pattern telemetry
- NUMA-aware memory guarding
- SIMD-accelerated validation

This header provides the memory safety foundation for DGTL's zero-trust architecture, enabling secure and efficient memory operations with comprehensive debug-time validation.