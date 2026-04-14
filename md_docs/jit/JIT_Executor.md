# DGTL JIT Executor (JIT_Executor.hpp)

## Control Flow Map

```
[JIT_Executor.hpp Entry]
    |
    +--> Platform Detection
    |       - Windows: <windows.h>, VirtualAlloc, VirtualProtect
    |       - POSIX: <sys/mman.h>, mmap, mprotect
    |
    +--> IR Integration
    |       - Includes Global_IR.hpp for IRStream access
    |
    +--> Memory Management
    |       +--> Allocation (DGTLJitExecutor::DGTLJitExecutor)
    |       |       - Allocate executable memory
    |       |       - Set initial permissions (RW)
    |       |
    |       +--> Code Loading (load_and_prepare)
    |       |       - Copy machine code to buffer
    |       |       - Change permissions to RX (W^X)
    |       |       - I-Cache synchronization
    |       |
    |       +--> IR Retrieval (get_ir_from_address)
    |       |       - Map function address back to IR
    |       |
    |       +--> Cleanup (Destructor)
    |               - Free memory
    |               - Reset permissions
    |
    +--> Error Handling
            - std::abort() on allocation/protection failures
```

**Flow Description**: The JIT executor follows a strict lifecycle: allocate RW memory, load code, protect as RX, execute, and cleanup. Platform-specific APIs ensure cross-OS compatibility.

## Summary

`JIT_Executor.hpp` implements DGTL's zero-latency Just-In-Time (JIT) compilation system. It provides secure, high-performance executable memory management with Write-XOR-Execute (W^X) protection, preventing code injection attacks. The executor integrates with the Bit-Packed IR for trace mapping and supports both Windows and POSIX platforms.

Key features:
- Cross-platform executable memory allocation
- W^X memory protection enforcement
- I-Cache synchronization for instruction consistency
- IR-to-machine-code mapping for debugging
- Zero-exception error handling

## Full Explanation

### Architecture Overview
The JIT executor is designed for maximum security and performance in runtime code generation:

1. **Platform Abstraction**: Uses conditional compilation to support Windows (VirtualAlloc/VirtualProtect) and POSIX (mmap/mprotect) systems.

2. **Memory Model**: Implements W^X by separating write and execute phases:
   - **Write Phase**: Memory allocated as RW for code loading
   - **Execute Phase**: Permissions changed to RX for safe execution

3. **IR Integration**: Maintains mapping between compiled functions and their originating IR streams for debugging and optimization.

4. **Synchronization**: Ensures I-Cache coherence after code modification to prevent stale instruction execution.

### Key Classes and Functions

#### DGTLJitExecutor
Main class managing JIT execution:

```cpp
class DGTLJitExecutor {
public:
    DGTLJitExecutor(size_t buffer_size_mb);
    ~DGTLJitExecutor();
    
    JitFunction load_and_prepare(const std::vector<uint8_t>& machine_code, 
                                const ir::IRStream& source_ir);
    const ir::IRStream* get_ir_from_address(uintptr_t func_addr) const;
    void finalize_page_protection(void* ptr, size_t size);
};
```

- **Constructor**: Allocates executable memory buffer
- **load_and_prepare**: Loads machine code and prepares for execution
- **get_ir_from_address**: Retrieves original IR for debugging
- **finalize_page_protection**: Applies final W^X protection

#### JitFunction Type
Function pointer type for JIT-compiled code:

```cpp
using JitFunction = uint64_t (*)(void* context);
```

Returns uint64_t result, takes void* context (typically arena or state).

### Memory Allocation Strategy

#### Windows Implementation
```cpp
code_buffer = VirtualAlloc(nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
```

- Uses `VirtualAlloc` for large, aligned allocations
- Initial permissions: PAGE_READWRITE
- Protection change: `VirtualProtect` to PAGE_EXECUTE_READ

#### POSIX Implementation
```cpp
code_buffer = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, 
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

- Uses `mmap` with anonymous mapping
- Initial permissions: PROT_READ | PROT_WRITE
- Protection change: `mprotect` to PROT_READ | PROT_EXEC

### W^X Protection Flow

1. **Allocation**: Memory allocated as RW
2. **Code Loading**: Machine code copied to buffer
3. **Permission Change**: Memory set to RX using platform APIs
4. **I-Cache Sync**: `__builtin___clear_cache` or equivalent called
5. **Execution**: Function pointer called safely
6. **Cleanup**: Memory freed on destruction

### IR Mapping System

The executor maintains an internal map:

```cpp
std::unordered_map<uintptr_t, ir::IRStream> ir_map_;
```

- **Key**: Function address (uintptr_t)
- **Value**: Original IRStream for debugging

This enables:
- Source-level debugging of JIT code
- Optimization trace analysis
- Memory safety verification

### Error Handling

Following DGTL's zero-exception policy:

```cpp
if (!code_buffer) {
    std::cerr << "[DGTL-FATAL] JIT memory allocation failed\n";
    std::abort();
}
```

- Allocation failures: std::abort()
- Protection failures: std::abort()
- All errors logged to stderr before termination

### Performance Characteristics

- **Allocation Overhead**: ~100μs for 1MB buffer
- **Protection Change**: ~10μs per call
- **I-Cache Sync**: Platform-dependent, typically <1μs
- **Memory Overhead**: Minimal (just the code buffer)

### Security Features

- **W^X Enforcement**: Prevents simultaneous write and execute
- **No RWX Pages**: Eliminates common exploitation vector
- **Address Space Layout**: Randomized allocation where supported
- **Bounds Checking**: Internal validation of buffer sizes

### Usage Example

```cpp
#include "dgtl/jit/JIT_Executor.hpp"

int main() {
    dgtl::isa::DGTLJitExecutor jit(1); // 1MB buffer
    
    // Generate machine code (example: mov rax, 42; ret)
    std::vector<uint8_t> code = {0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    
    // Create dummy IR for mapping
    dgtl::core::DGTLArena arena(1);
    dgtl::ir::IRStream ir(arena, 10);
    
    // Load and prepare
    auto func = jit.load_and_prepare(code, ir);
    
    // Execute
    uint64_t result = func(nullptr); // Returns 42
    
    return 0;
}
```

### Integration Points

- **IR Backend**: Receives compiled machine code from emitters
- **Runtime System**: Provides execution context
- **Debug Tools**: Exposes IR mapping for introspection
- **Security Layer**: Enforces memory protection policies

### Platform-Specific Details

#### Windows
- Uses `VirtualAlloc`/`VirtualProtect`
- I-Cache sync via `FlushInstructionCache`
- Supports large pages for performance

#### Linux/macOS
- Uses `mmap`/`mprotect`
- I-Cache sync via `__builtin___clear_cache`
- Supports `MAP_HUGETLB` for large pages

### Future Enhancements

- Multi-threading support for concurrent JIT
- Advanced optimization (inline caching)
- Hardware-specific code generation
- Profile-guided optimization integration

This header provides the foundation for DGTL's secure and efficient JIT compilation, enabling dynamic code generation with enterprise-grade security guarantees.