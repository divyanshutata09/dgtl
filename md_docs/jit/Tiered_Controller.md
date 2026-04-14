# Tiered_Controller.hpp Documentation

## Map of the Control Flow

```
Tiered Execution Controller
├── TieredController Class
│   ├── Dependencies
│   │   ├── DGTLJitExecutor& _jit: JIT execution engine
│   │   └── DGTLNasmEmitter _aot: AOT assembly emitter
│   ├── Profiling State
│   │   ├── std::unordered_map<uintptr_t, uint32_t> _hot_counts
│   │   └── const uint32_t BAKE_THRESHOLD = 10000
│   ├── Public Interface
│   │   └── profile_and_check(func_ptr, symbols)
│   │       ├── Convert function pointer to uintptr_t address
│   │       ├── Increment execution count in _hot_counts
│   │       └── Check threshold: call bake_to_disk if reached
│   └── Private Implementation
│       └── bake_to_disk(addr, symbols)
│           ├── Retrieve IR from JIT executor
│           ├── Emit assembly using NASM emitter
│           ├── Generate filename: "kernel_0x{addr:X}.asm"
│           └── Write assembly to disk file
└── Integration Points
    ├── JIT_Executor: get_ir_from_address() for IR retrieval
    ├── NASM_Emitter: emit() and get_output() for assembly generation
    └── SymbolTable: Symbol resolution for assembly emission
```

**Data Flow:**
1. **Profiling**: Each function call increments execution counter
2. **Threshold Check**: When counter reaches BAKE_THRESHOLD (10,000)
3. **IR Retrieval**: Get original IR from JIT executor using function address
4. **Assembly Emission**: Use NASM emitter to convert IR to x86-64 assembly
5. **File Output**: Write assembly to disk with address-based filename
6. **Future Extension**: Automatic NASM compilation and linking

## Summary

Tiered_Controller.hpp implements a profiling-driven tiered execution system for DGTL's JIT compiler infrastructure. The controller monitors function execution frequencies and automatically "bakes" hot functions to disk as assembly files when they exceed a execution threshold. This enables ahead-of-time (AOT) compilation of performance-critical code paths while maintaining JIT flexibility for cold code.

Key features include:
- **Hot Function Detection**: Execution count profiling with configurable threshold
- **Automatic Baking**: Seamless transition from JIT to AOT for hot functions
- **Assembly Persistence**: Disk-based storage of optimized assembly code
- **Address-Based Naming**: Function address encoded in output filenames
- **Emitter Integration**: Uses NASM emitter for x86-64 assembly generation
- **Symbol Resolution**: Full symbol table support for complex functions

The implementation provides a bridge between DGTL's high-performance JIT execution and traditional AOT compilation workflows.

## Full Explanation of Code

### Includes and Namespace
```cpp
#pragma once
#include <filesystem>
#include <fstream>
#include "JIT_Executor.hpp"
#include "../isa/NASM_Emitter.hpp"

namespace dgtl::exec {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<filesystem>`: Path operations (not used in current implementation)
  - `<fstream>`: File I/O operations
- **Internal Includes**:
  - `"JIT_Executor.hpp"`: JIT execution engine interface
  - `"../isa/NASM_Emitter.hpp"`: x86-64 assembly emitter
- **Namespace**: dgtl::exec (execution management)

### TieredController Class Definition
```cpp
class TieredController {
    DGTLJitExecutor& _jit;
    DGTLNasmEmitter _aot;
    std::unordered_map<uintptr_t, uint32_t> _hot_counts;
    const uint32_t BAKE_THRESHOLD = 10000;
```
**Members**:
- **_jit**: Reference to JIT executor for IR retrieval
- **_aot**: NASM emitter instance for assembly generation
- **_hot_counts**: Map from function address to execution count
- **BAKE_THRESHOLD**: Constant threshold (10,000 executions) for baking

### Constructor
```cpp
TieredController(DGTLJitExecutor& jit) : _jit(jit) {}
```
**Purpose**: Initialize controller with JIT executor reference
**Initialization**: _jit reference stored, _aot default-constructed

### Profiling and Threshold Checking

#### profile_and_check()
```cpp
void profile_and_check(JitFunction func_ptr, const core::DGTLSymbolTable& symbols) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(func_ptr);
    
    if (++_hot_counts[addr] == BAKE_THRESHOLD) {
        bake_to_disk(addr, symbols);
    }
}
```
**Purpose**: Record function execution and check for baking threshold
**Parameters**:
- **func_ptr**: JIT-compiled function pointer
- **symbols**: Symbol table for assembly emission

**Logic**:
1. Convert function pointer to uintptr_t address
2. Increment execution count in _hot_counts map
3. If count reaches BAKE_THRESHOLD, trigger baking

**Threshold**: 10,000 executions before considering function "hot"

### Baking Implementation

#### bake_to_disk()
```cpp
void bake_to_disk(uintptr_t addr, const core::DGTLSymbolTable& symbols) {
    auto ir = _jit.get_ir_from_address(addr);
    if (ir.is_null()) return;

    _aot.clear();
    _aot.emit(ir, symbols);

    std::string filename = std::format("kernel_0x{:X}.asm", addr);
    std::ofstream file(filename);
    file << _aot.get_output();
    file.close();
    
    // Potential Step: Trigger NASM assembly automatically here
}
```
**Purpose**: Convert hot JIT function to AOT assembly file
**Steps**:
1. **IR Retrieval**: Get original IR from JIT executor using function address
2. **Null Check**: Return early if IR not found (shouldn't happen)
3. **Emitter Reset**: Clear any previous assembly output
4. **Assembly Generation**: Emit x86-64 assembly from IR
5. **Filename Generation**: Create address-based filename "kernel_0x{addr:X}.asm"
6. **File Writing**: Write assembly to disk file
7. **Future Extension**: Comment indicates potential automatic NASM compilation

**Filename Format**: Hexadecimal address encoding for unique identification

### Namespace Closing
```cpp
} // namespace dgtl::exec
```
- Closes the execution namespace

## Usage Example
```cpp
#include "Tiered_Controller.hpp"

// Set up JIT executor and symbol table
dgtl::jit::DGTLJitExecutor jit(/*...*/);
dgtl::core::DGTLSymbolTable symbols(/*...*/);

// Create tiered controller
dgtl::exec::TieredController controller(jit);

// During execution loop
while (running) {
    // Execute JIT function
    auto result = jit_function(args...);
    
    // Profile and potentially bake hot functions
    controller.profile_and_check(jit_function, symbols);
    
    // After 10,000 executions, "kernel_0x{address}.asm" will be created
}
```

## Dependencies
- **JIT_Executor.hpp**: DGTLJitExecutor class and JitFunction type
- **NASM_Emitter.hpp**: DGTLNasmEmitter class for assembly generation
- **Symbol_Table.hpp**: DGTLSymbolTable for symbol resolution
- **C++20**: std::format for filename generation

## Thread Safety
- **Profiling**: Not thread-safe, requires external synchronization
- **Baking**: File I/O operations are not thread-safe
- **JIT Access**: get_ir_from_address() thread safety depends on JIT implementation

## Performance Characteristics
- **Profiling Overhead**: O(1) map lookup and increment per function call
- **Threshold Check**: Constant-time comparison
- **Baking Cost**: O(N) where N is IR size, plus file I/O
- **Memory Usage**: O(H) where H is number of hot functions tracked
- **Storage**: Unbounded growth of _hot_counts map
- **I/O Pattern**: Infrequent large writes (assembly files)

## Future Extensions
The comment in bake_to_disk() indicates planned automatic NASM compilation:
- **NASM Integration**: Automatic invocation of NASM assembler
- **Object File Generation**: .o file creation from .asm
- **Linking**: Integration with linker for executable generation
- **Optimization**: Profile-guided optimization during AOT compilation