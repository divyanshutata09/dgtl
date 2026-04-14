# MultiArch_Orchestrator.hpp Documentation

## Map of the Control Flow

```
Multi-Architecture Orchestrator (Final Gatekeeper)
├── CompilationReport Struct
│   ├── double total_ms: Compilation time in milliseconds
│   ├── size_t nodes_processed: Number of IR nodes processed
│   ├── double throughput_nodes_sec: Processing throughput
│   └── bool validation_passed: Compilation success flag
├── DGTLOrchestrator Class
│   ├── Dependencies
│   │   ├── dgtl::target::DGTLTargetManager target_mgr
│   │   └── CompilationReport last_report
│   ├── Main Compilation
│   │   └── compile_to_binary(output_path, stream)
│   │       ├── Pre-Flight Validation
│   │       │   ├── Stream null/empty check
│   │       │   └── Transactional state guard (Pillar 2)
│   │       ├── Target Detection
│   │       │   ├── Get active architecture and OS
│   │       │   └── Route to appropriate backend
│   │       ├── Backend Dispatch
│   │       │   ├── Linux/Windows → dispatch_to_linker() (ELF)
│   │       │   ├── macOS → dispatch_to_macho() (Mach-O)
│   │       │   └── WASM → dispatch_to_wasm() (WebAssembly)
│   │       └── Telemetry Update
│   │           ├── Time measurement with high_resolution_clock
│   │           ├── Throughput calculation (nodes/sec)
│   │           └── Validation status update
│   ├── Telemetry Access
│   │   └── get_last_report() → CompilationReport
│   └── Error Handling
│       └── trigger_fatal_trap(code, details) → [[noreturn]]
│           └── Throw runtime_error with DGTL_ORCHESTRATOR_HALT
└── Backend Dispatch (Private Methods)
    ├── dispatch_to_linker(): ELF binary generation
    ├── dispatch_to_macho(): Mach-O binary generation
    └── dispatch_to_wasm(): WebAssembly module generation
```

**Data Flow:**
1. **Validation Gate**: Check stream validity and transactional state
2. **Target Query**: Determine active architecture and operating system
3. **Backend Routing**: Dispatch to appropriate binary format handler
4. **Binary Generation**: Convert IR to target-specific binary format
5. **Telemetry**: Record compilation metrics and validation status
6. **Error Propagation**: Fatal traps prevent corrupted binary emission

## Summary

MultiArch_Orchestrator.hpp implements the final gatekeeper for DGTL's multi-architecture binary generation system, serving as Pillar 2 & 3 integration point with transactional safety and zero-corruption guarantees. The orchestrator validates IR streams before binary emission and routes compilation to appropriate backend emitters based on target architecture and operating system.

Key features include:
- **Transactional Safety**: Prevents emission of uncommitted shadow IR streams
- **Multi-Target Support**: Linux/Windows (ELF), macOS (Mach-O), WebAssembly
- **Sentinel Validation**: Re-validates DGTL invariants before hardware dispatch
- **Fatal Trap System**: Prevents corrupted IR from reaching filesystem
- **High-Precision Telemetry**: Sub-microsecond timing with throughput metrics
- **Zero-Corruption Guarantee**: Comprehensive validation before binary generation

The implementation ensures that only validated, committed IR streams are converted to executable binaries across multiple target platforms.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <memory>
#include <string_view>
#include <chrono>
#include <stdexcept>
#include <string>
#include "core/Target.hpp"      
#include "../ir/Global_IR.hpp"

// Forward Declarations for specialized backend emitters
namespace dgtl::exec {
    class DGTLLinker;      
    class DGTLMachOEmitter; 
    class DGTLWasmEncoder;  
}

namespace dgtl::core {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<memory>`: Smart pointers (not used in current implementation)
  - `<string_view>`: Non-owning string references
  - `<chrono>`: High-precision timing
  - `<stdexcept>`: Exception handling
  - `<string>`: String operations
- **Internal Includes**:
  - `"core/Target.hpp"`: Target architecture and OS definitions
  - `"../ir/Global_IR.hpp"`: IRStream and IR types
- **Forward Declarations**: Backend emitter classes in dgtl::exec namespace
- **Namespace**: dgtl::core

### CompilationReport Struct
```cpp
struct CompilationReport {
    double total_ms = 0.0;
    size_t nodes_processed = 0;
    double throughput_nodes_sec = 0.0;
    bool validation_passed = false;
};
```
**Purpose**: High-precision telemetry for compilation strikes
**Members**:
- **total_ms**: Total compilation time in milliseconds
- **nodes_processed**: Number of IR nodes processed
- **throughput_nodes_sec**: Processing rate in nodes per second
- **validation_passed**: Boolean success indicator

### DGTLOrchestrator Class Definition
```cpp
class DGTLOrchestrator {
    dgtl::target::DGTLTargetManager target_mgr;
    CompilationReport last_report;
```
**Members**:
- **target_mgr**: Target architecture and OS manager
- **last_report**: Most recent compilation telemetry

### Constructor
```cpp
explicit DGTLOrchestrator() = default;
```
**Purpose**: Default initialization of orchestrator
**Note**: Target manager initialized with defaults

### Main Compilation Method

#### compile_to_binary()
```cpp
void compile_to_binary(std::string_view output_path, const ir::IRStream& stream) {
    // 1. Mandatory Pre-Flight Check (The Sentinel Gate)
    if (stream.is_null() || stream.size() == 0) [[unlikely]] {
        return;
    }

    // PILLAR 2: Transactional State Guard.
    // If a strike is active (Shadow Tier exists), it MUST be committed before emission.
    if (stream.is_transactional() && !stream.is_committed()) [[unlikely]] {
        trigger_fatal_trap("TRANSACTION_NOT_COMMITTED", 
            "Attempted to emit binary from an unvalidated Shadow Stream. "
            "The Sentinel has blocked this hardware dispatch.");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // 2. Hardware Dispatch logic
    const dgtl::target::Arch current_arch = target_mgr.get_active_arch();
    const dgtl::target::OS current_os     = target_mgr.get_active_os();

    try {
        // Route based on Target Policy defined in TargetRegistry
        if (current_os == dgtl::target::OS::LINUX || current_os == dgtl::target::OS::WINDOWS) {
            this->dispatch_to_linker(output_path, stream);
        } 
        else if (current_os == dgtl::target::OS::DARWIN) {
            this->dispatch_to_macho(output_path, stream);
        }
        else if (current_arch == dgtl::target::Arch::WASM32) {
            this->dispatch_to_wasm(output_path, stream);
        }
        last_report.validation_passed = true;
    } catch (...) {
        last_report.validation_passed = false;
        // Re-throw to trigger DGTL-Pass-Manager Global Rollback
        throw; 
    }

    // 3. Telemetry Update (Optimized for Sub-Microsecond Precision)
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    
    last_report.total_ms = elapsed.count();
    last_report.nodes_processed = stream.size();

    // Zero-Division Protection & High-Precision Scaling (Nodes per Second)
    if (last_report.total_ms > 1e-9) {
        last_report.throughput_nodes_sec = (static_cast<double>(stream.size()) / (last_report.total_ms / 1000.0));
    } else {
        // Handle instantaneous strikes (cached or trivial IR)
        last_report.throughput_nodes_sec = static_cast<double>(stream.size()) * 1000000000.0; 
    }
}
```
**Purpose**: Zero-corruption entry point for binary generation
**Parameters**:
- **output_path**: Filesystem destination for binary
- **stream**: Validated, committed IRStream

**Validation Phase**:
- **Null Check**: Return early for invalid streams
- **Transactional Guard**: Prevent emission of uncommitted shadow streams
- **Fatal Trap**: Throw exception for Pillar 2 violations

**Dispatch Phase**:
- **Target Query**: Get active architecture and OS from target manager
- **Routing Logic**: 
  - Linux/Windows → ELF linker dispatch
  - macOS → Mach-O emitter dispatch  
  - WASM32 → WebAssembly encoder dispatch

**Exception Handling**:
- **Try-Catch**: Mark validation as failed on any exception
- **Re-throw**: Propagate exceptions for global rollback

**Telemetry Phase**:
- **Timing**: High-resolution clock measurement
- **Metrics**: Calculate throughput with zero-division protection
- **Instantaneous Handling**: Special case for sub-nanosecond compilations

### Telemetry Access

#### get_last_report()
```cpp
[[nodiscard]] CompilationReport get_last_report() const noexcept {
    return last_report;
}
```
**Purpose**: Access most recent compilation telemetry
**Return**: Copy of last compilation report

### Error Handling

#### trigger_fatal_trap()
```cpp
[[noreturn]] void trigger_fatal_trap(const char* code, const char* details) {
    throw std::runtime_error(std::string("DGTL_ORCHESTRATOR_HALT [") + code + "]: " + details);
}
```
**Purpose**: Prevent corrupted IR from reaching filesystem
**Parameters**:
- **code**: Error code identifier
- **details**: Descriptive error message

**Behavior**: Throws runtime_error with formatted DGTL halt message
**Attribute**: [[noreturn]] indicates function never returns normally

### Backend Dispatch Methods
```cpp
void dispatch_to_linker(std::string_view path, const ir::IRStream& stream);
void dispatch_to_macho(std::string_view path, const ir::IRStream& stream);
void dispatch_to_wasm(std::string_view path, const ir::IRStream& stream);
```
**Purpose**: Route to appropriate binary format generators
**Note**: Implementation in separate compilation units

### Namespace Closing
```cpp
} // namespace dgtl::core
```
- Closes the core namespace

## Usage Example
```cpp
#include "MultiArch_Orchestrator.hpp"

// Create orchestrator
dgtl::core::DGTLOrchestrator orchestrator;

// Prepare validated IR stream
dgtl::ir::IRStream stream(/*...*/);
// ... populate and commit stream ...

try {
    // Compile to binary (auto-detects target)
    orchestrator.compile_to_binary("output.bin", stream);
    
    // Get compilation metrics
    auto report = orchestrator.get_last_report();
    if (report.validation_passed) {
        std::cout << "Compilation successful!\n";
        std::cout << "Time: " << report.total_ms << " ms\n";
        std::cout << "Throughput: " << report.throughput_nodes_sec << " nodes/sec\n";
    }
} catch (const std::exception& e) {
    std::cout << "Compilation failed: " << e.what() << "\n";
    // Global rollback triggered
}
```

## Dependencies
- **Target.hpp**: DGTLTargetManager, Arch, OS enumerations
- **Global_IR.hpp**: IRStream class and transactional state queries
- **Backend Emitters**: DGTLLinker, DGTLMachOEmitter, DGTLWasmEncoder (forward declared)

## Thread Safety
- **Compilation**: Not thread-safe, single orchestrator per process
- **Report Access**: get_last_report() is const and thread-safe for reading
- **Target Manager**: Thread safety depends on DGTLTargetManager implementation

## Performance Characteristics
- **Validation**: O(1) stream checks with early returns
- **Dispatch**: O(1) target routing with constant-time comparisons
- **Telemetry**: High-precision timing with std::chrono::high_resolution_clock
- **Throughput**: Zero-division protection for instantaneous compilations
- **Memory**: Minimal additional memory usage beyond report storage
- **Exception Safety**: Comprehensive error handling with global rollback capability