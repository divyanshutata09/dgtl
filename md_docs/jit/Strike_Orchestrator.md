# Strike_Orchestrator.hpp Documentation

## Map of the Control Flow

```
Strike Orchestrator (JIT Compilation Pipeline)
├── StrikeOrchestrator Class
│   ├── Dependencies
│   │   ├── DGTLContext& ctx: IR emission context
│   │   ├── DGTLArena& arena: Memory allocation arena
│   │   ├── opt::StackSpiller spiller: Register allocation/spilling
│   │   └── dgtl::exec::X64StrikeEmitter emitter: x64 machine code emitter
│   ├── Scratch Buffer
│   │   └── std::vector<uint8_t> machine_code_buffer (16MB reserve)
│   ├── Main Pipeline
│   │   └── fire_strike(stream, data_payload)
│   │       ├── Validation: Check stream validity
│   │       ├── Phase 1 - Optimization: spiller.execute() for register allocation
│   │       ├── Phase 2 - Emission: emitter.process_stream() to machine code
│   │       ├── Phase 3 - Injection: StrikeRunner::inject_code() to executable memory
│   │       └── Phase 4 - Execution: StrikeRunner::execute() with I-cache flush
│   ├── Memory Management
│   │   └── hard_reset()
│   │       ├── arena.reset(): Clear arena allocations
│   │       ├── spiller.reset(): Reset spiller state
│   │       ├── machine_code_buffer.clear(): Clear scratch buffer
│   │       └── Shrink buffer if >32MB to prevent memory bloat
│   └── Telemetry
│       ├── get_last_code_size(): Return emitted code size
│       └── get_stack_frame_requirement(): Get required stack frame size
└── Integration Points
    ├── Stack_Spiller: Register allocation and spilling
    ├── X64_Strike_Emitter: Machine code generation
    ├── Strike_Runner: Code injection and execution
    └── DGTLArena: Executable memory management
```

**Data Flow:**
1. **Strike Initiation**: fire_strike() called with IRStream and optional data payload
2. **Validation**: Check stream is not null/empty, return error metrics if invalid
3. **Optimization**: StackSpiller resolves register constraints and handles spills
4. **Emission**: X64StrikeEmitter converts IR to x86-64 machine code in scratch buffer
5. **Injection**: StrikeRunner copies code to huge-page aligned executable arena
6. **Execution**: StrikeRunner executes code with I-cache synchronization
7. **Result**: Return StrikeMetrics with execution time and status codes

## Summary

Strike_Orchestrator.hpp implements the central command center for DGTL's JIT compilation system, orchestrating the four-stage "Fire Strike" pipeline that transforms bit-packed intermediate representation into executable machine code. The orchestrator coordinates optimization, emission, injection, and execution phases while managing memory resources and providing telemetry.

Key features include:
- **Four-Stage Pipeline**: Optimization → Emission → Injection → Execution
- **Transactional Safety**: Zero mid-strike heap allocation with scratch buffers
- **Huge-Page Support**: Executable memory allocation for performance
- **I-Cache Management**: Instruction cache flushing for correct execution
- **Memory Efficiency**: Automatic buffer shrinking to prevent memory bloat
- **Telemetry Integration**: Code size and stack frame requirement reporting

The implementation follows DGTL's Pillar 4 "Hardware-Native Strike" with single-pass compilation and hardware-aware memory management.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <cstdint>

// Core Infrastructure
#include "core/Context.hpp"
#include "../ir/Global_IR.hpp" // No path prefix needed if both are in core/

// Cross-Module includes (using relative paths from include/core/)
#include "../isa/Stack_Spiller.hpp"
#include "../isa/X64_Strike_Emitter.hpp" 
#include "exec/Strike_Runner.hpp"

namespace dgtl::core {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic byte buffer for machine code
  - `<cstdint>`: Fixed-width integers
- **Internal Includes**:
  - `"core/Context.hpp"`: DGTLContext for IR emission
  - `"../ir/Global_IR.hpp"`: IR type definitions
  - `"../isa/Stack_Spiller.hpp"`: Register allocation
  - `"../isa/X64_Strike_Emitter.hpp"`: x64 code emission
  - `"exec/Strike_Runner.hpp"`: Code injection and execution
- **Namespace**: dgtl::core

### StrikeOrchestrator Class Definition
```cpp
class StrikeOrchestrator {
    DGTLContext& ctx;
    DGTLArena& arena;
    opt::StackSpiller spiller;
    
    // Emitter resides in the dgtl::exec namespace for backend-specific logic
    dgtl::exec::X64StrikeEmitter emitter;

    // PRE-ALLOCATED SCRATCH PAD: 16MB Initial Reserve
    // Used to build the machine code before injecting into the executable Arena.
    std::vector<uint8_t> machine_code_buffer;
```
**Members**:
- **ctx**: Reference to IR emission context
- **arena**: Reference to memory allocation arena
- **spiller**: Stack spiller for register allocation
- **emitter**: x64 machine code emitter
- **machine_code_buffer**: 16MB pre-allocated scratch buffer for code generation

### Constructor
```cpp
StrikeOrchestrator(DGTLContext& c, DGTLArena& a) 
    : ctx(c), arena(a) {
    // Reserve 16MB to handle typical high-performance kernels without reallocation
    machine_code_buffer.reserve(1024 * 1024 * 16);
}
```
**Purpose**: Initialize orchestrator with context and arena
**Buffer Allocation**: Pre-reserve 16MB to avoid reallocations during strikes

### Main Pipeline Method

#### fire_strike()
```cpp
dgtl::exec::StrikeMetrics fire_strike(ir::IRStream& stream, void* data_payload = nullptr) {
    // Validation using BitPacked_IR.hpp interface
    if (stream.is_null() || stream.size() == 0) [[unlikely]] {
        return dgtl::exec::StrikeMetrics{0, 0xFFFFFFFF}; 
    }

    // 1. OPTIMIZATION PHASE: Solve Register Constraints
    // Uses the Transactional Strike (Pillar 2) if spills are needed.
    spiller.execute(stream, arena);

    // 2. EMISSION PHASE: IR -> Machine Code
    machine_code_buffer.clear(); 
    emitter.process_stream(stream, machine_code_buffer);

    // 3. INJECTION PHASE: Move to Huge-Page Aligned Arena
    // Copies the scratch buffer into the executable memory segment.
    size_t injected_size = dgtl::exec::StrikeRunner::inject_code(arena, machine_code_buffer);

    // 4. EXECUTION PHASE: The JIT Jump
    // Flushes Instruction Cache (I-Cache) and executes the generated code.
    return dgtl::exec::StrikeRunner::execute(arena, injected_size, data_payload);
}
```
**Purpose**: Execute the complete four-stage JIT compilation and execution pipeline
**Parameters**:
- **stream**: Bit-packed IR to compile and execute
- **data_payload**: Optional data pointer for kernel execution

**Phases**:
1. **Validation**: Check stream validity, return error metrics if invalid
2. **Optimization**: StackSpiller handles register allocation and spilling
3. **Emission**: X64StrikeEmitter converts IR to machine code in scratch buffer
4. **Injection**: StrikeRunner copies code to executable arena memory
5. **Execution**: StrikeRunner executes code with proper cache management

**Return**: StrikeMetrics containing execution time and status codes

### Memory Management

#### hard_reset()
```cpp
void hard_reset() {
    arena.reset();
    spiller.reset();
    machine_code_buffer.clear();
    
    // If the buffer grew beyond 32MB, shrink it back to baseline to save RAM
    if (machine_code_buffer.capacity() > (1024 * 1024 * 32)) {
        machine_code_buffer.shrink_to_fit();
        machine_code_buffer.reserve(1024 * 1024 * 16);
    }
}
```
**Purpose**: Reset all components and prevent memory fragmentation
**Operations**:
- **arena.reset()**: Clear arena allocations
- **spiller.reset()**: Reset spiller internal state
- **machine_code_buffer.clear()**: Clear scratch buffer contents
- **Shrink Logic**: If capacity > 32MB, shrink to fit and re-reserve 16MB

### Telemetry Methods

#### get_last_code_size()
```cpp
[[nodiscard]] size_t get_last_code_size() const noexcept { 
    return machine_code_buffer.size();
}
```
**Purpose**: Get size of last emitted machine code
**Usage**: Performance monitoring and debugging

#### get_stack_frame_requirement()
```cpp
[[nodiscard]] uint32_t get_stack_frame_requirement() const noexcept {
    return spiller.get_frame_size();
}
```
**Purpose**: Get required stack frame size for generated code
**Usage**: Stack allocation planning and validation

### Namespace Closing
```cpp
} // namespace dgtl::core
```
- Closes the core namespace

## Usage Example
```cpp
#include "Strike_Orchestrator.hpp"

// Set up context and arena
dgtl::core::DGTLContext context(/*...*/);
dgtl::core::DGTLArena arena(/*...*/);

// Create orchestrator
dgtl::core::StrikeOrchestrator orchestrator(context, arena);

// Prepare IR stream
dgtl::ir::IRStream stream(/*...*/);
// ... populate stream with IR ...

// Execute strike
void* data = /* kernel data */;
auto metrics = orchestrator.fire_strike(stream, data);

// Check results
if (metrics.error_code == 0) {
    std::cout << "Execution time: " << metrics.execution_time_ns << " ns\n";
    std::cout << "Code size: " << orchestrator.get_last_code_size() << " bytes\n";
} else {
    std::cout << "Strike failed with error: " << metrics.error_code << "\n";
}

// Reset for next strike
orchestrator.hard_reset();
```

## Dependencies
- **Context.hpp**: DGTLContext for IR emission state
- **Global_IR.hpp**: IRStream and IR type definitions
- **Stack_Spiller.hpp**: Register allocation and spilling
- **X64_Strike_Emitter.hpp**: x64 machine code generation
- **Strike_Runner.hpp**: Code injection and execution utilities

## Thread Safety
- **Per-Instance**: Not thread-safe, single orchestrator per compilation context
- **Arena Access**: External synchronization required for shared arenas
- **Execution**: StrikeRunner execution may have thread safety considerations

## Performance Characteristics
- **Pipeline**: Single-pass compilation with zero mid-strike allocation
- **Memory**: Pre-allocated 16MB scratch buffer, automatic shrinking
- **Cache Management**: I-cache flushing for correct JIT execution
- **Huge Pages**: Executable memory allocation for performance
- **Telemetry**: O(1) access to code size and stack requirements
- **Reset**: Bounded memory growth with shrink-to-fit protection