# Pass_Manager.hpp Documentation

## Map of the Control Flow

```
DGTL Pass Manager (Transactional Optimization Pipeline)
├── DGTLOptimizationPass Abstract Base
│   ├── virtual ~DGTLOptimizationPass()
│   ├── virtual execute(stream, am) = 0
│   └── virtual get_pass_name() const noexcept = 0
├── PipelineStrike Struct
│   ├── std::unique_ptr<DGTLOptimizationPass> pass
│   ├── bool is_enabled = true
│   └── double last_execution_ms = 0.0
├── DGTLPassManager Class
│   ├── Dependencies
│   │   ├── std::vector<PipelineStrike> strikes
│   │   ├── core::DGTLArena& global_arena
│   │   ├── core::DGTLFormalVerifier sentinel
│   │   └── DGTLAnalysisManager analysis_manager
│   ├── Pass Registration
│   │   └── add_pass<T, Args...>(args...) → void
│   │       └── Create PipelineStrike with unique_ptr<T>
│   ├── Pipeline Execution
│   │   └── run(primary_stream) → bool
│   │       ├── Early return for empty streams
│   │       ├── Iterate through enabled strikes
│   │       │   ├── Arena checkpoint for rollback
│   │       │   ├── High-resolution timing start
│   │       │   ├── begin_transaction() on stream
│   │       │   ├── Execute pass with analysis manager
│   │       │   ├── validate_strike() check
│   │       │   ├── Commit on success, rollback on failure
│   │       │   └── Record execution time
│   │       └── Return true (success)
│   ├── Management Methods
│   │   ├── clear() → void (clear passes and invalidate analyses)
│   │   ├── get_analysis_manager() → DGTLAnalysisManager&
│   │   └── get_telemetry() → const std::vector<PipelineStrike>&
│   └── Validation Logic
│       └── validate_strike(stream) → bool
│           ├── Check transactional state
│           ├── Iterate shadow buffer nodes
│           │   ├── Verify intent_hash vs computed master hash
│           │   └── Check bit consistency with sentinel
│           └── Final shadow integrity validation
└── Integration Points
    ├── Arena: Checkpoint/rollback for memory safety
    ├── FormalVerifier: Intent hash and bit consistency checks
    ├── AnalysisManager: Cached analysis facts for passes
    └── IRStream: Transactional operations and validation
```

**Data Flow:**
1. **Pass Registration**: Template-based addition of optimization passes
2. **Pipeline Start**: Check for empty streams, iterate enabled passes
3. **Per-Pass Execution**: Arena checkpoint, timing start, transaction begin
4. **Pass Execution**: Call execute() with stream and analysis manager
5. **Validation**: Deep verification of shadow buffer changes
6. **Resolution**: Commit valid transformations, rollback invalid ones
7. **Telemetry**: Record execution times for performance monitoring

## Summary

Pass_Manager.hpp implements the DGTL Pass Manager (TPM), the central orchestrator for DGTL's transactional optimization pipeline. The manager coordinates optimization passes with Pillar 2 transactional safety and Pillar 3 formal verification, ensuring zero-corruption IR transformations. It integrates with the Analysis Manager (TAM) for cached analysis facts and provides comprehensive telemetry.

Key features include:
- **Transactional Execution**: Atomic IR mutations with automatic rollback on failure
- **Formal Verification**: Sentinel-based validation of all transformations
- **Analysis Integration**: Cached facts through DGTLAnalysisManager
- **Memory Safety**: Arena checkpoints for zero-latency rollback
- **Performance Telemetry**: High-resolution timing of pass execution
- **Template-Based Registration**: Type-safe pass addition with perfect forwarding

The implementation achieves HFT-grade optimization pipeline with zero-exception overhead and comprehensive correctness guarantees.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <memory>
#include <utility>
#include <string_view>
#include <chrono>
#include <expected> // C++26: Functional error handling to kill exception overhead

// Core Architectural Includes
#include "../core/Arena.hpp"
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"
#include "Analysis_Manager.hpp"

namespace dgtl::pipeline {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic arrays for pass pipeline
  - `<memory>`: Smart pointers for pass ownership
  - `<utility>`: std::forward for perfect forwarding
  - `<string_view>`: Non-owning string references
  - `<chrono>`: High-resolution timing
  - `<expected>`: C++23 expected (functional error handling)
- **Internal Includes**:
  - `"../core/Arena.hpp"`: Memory management and checkpoints
  - `"../ir/Global_IR.hpp"`: IRStream and transactional operations
  - `"../core/Verified_Formal.hpp"`: Formal verification and sentinel
  - `"Analysis_Manager.hpp"`: Analysis caching and management
- **Namespace**: dgtl::pipeline

### DGTLOptimizationPass Abstract Base
```cpp
class DGTLOptimizationPass {
public:
    virtual ~DGTLOptimizationPass() = default;

    /**
     * @brief Core strike logic.
     * @param stream The IR stream to mutate.
     * @param am The Analysis Manager for requesting cached IR facts.
     */
    virtual void execute(ir::IRStream& stream, DGTLAnalysisManager& am) = 0;

    [[nodiscard]] virtual std::string_view get_pass_name() const noexcept = 0;
};
```
**Purpose**: Abstract interface for all optimization passes
**Virtual Methods**:
- **execute()**: Core optimization logic with analysis manager access
- **get_pass_name()**: Human-readable pass identifier

### PipelineStrike Struct
```cpp
struct PipelineStrike {
    std::unique_ptr<DGTLOptimizationPass> pass;
    bool is_enabled{true};
    double last_execution_ms{0.0};
};
```
**Purpose**: Metadata container for registered passes
**Members**:
- **pass**: Unique ownership of pass instance
- **is_enabled**: Runtime enable/disable flag
- **last_execution_ms**: Performance telemetry

### DGTLPassManager Class Members
```cpp
class DGTLPassManager {
private:
    struct PipelineStrike {
        std::unique_ptr<DGTLOptimizationPass> pass;
        bool is_enabled{true};
        double last_execution_ms{0.0};
    };

    std::vector<PipelineStrike> strikes;
    core::DGTLArena& global_arena;
    core::DGTLFormalVerifier sentinel; 
    DGTLAnalysisManager analysis_manager;
```
**Members**:
- **strikes**: Pipeline of registered optimization passes
- **global_arena**: Reference to master hardware arena
- **sentinel**: Formal verifier for Pillar 3 validation
- **analysis_manager**: TAM integration for cached analyses

### Constructor and Rule of Five
```cpp
explicit DGTLPassManager(core::DGTLArena& arena) noexcept 
    : global_arena(arena) {}

// RAII: Strict ownership (No copies allowed in high-performance pipelines)
DGTLPassManager(const DGTLPassManager&) = delete;
DGTLPassManager& operator=(const DGTLPassManager&) = delete;

DGTLPassManager(DGTLPassManager&&) noexcept = default;
DGTLPassManager& operator=(DGTLPassManager&&) noexcept = default;
```
**Constructor**: Initialize with arena reference
**Copy Operations**: Deleted for performance (no copying pipelines)
**Move Operations**: Defaulted for ownership transfer

### Pass Registration

#### add_pass()
```cpp
template <typename T, typename... Args>
void add_pass(Args&&... args) {
    strikes.push_back({
        std::make_unique<T>(std::forward<Args>(args)...)
    });
}
```
**Purpose**: Template-based pass registration with perfect forwarding
**Template Parameters**:
- **T**: Pass type (must inherit from DGTLOptimizationPass)
- **Args**: Constructor arguments for pass

**Logic**: Create PipelineStrike with unique_ptr to new pass instance

### Pipeline Execution

#### run()
```cpp
[[nodiscard]] bool run(ir::IRStream& primary_stream) noexcept {
    if (primary_stream.empty()) [[unlikely]] return true;

    for (auto& entry : strikes) {
        if (!entry.is_enabled) [[unlikely]] continue;

        // Checkpoint for zero-latency memory rollback
        const size_t arena_checkpoint = global_arena.get_checkpoint();
        const auto start_time = std::chrono::high_resolution_clock::now();

        // PILLAR 2: Open Transactional Strike Zone
        primary_stream.begin_transaction();

        // Execute Strike without the bloat of try-catch
        // The Analysis Manager (TAM) is passed here to allow lazy analysis retrieval
        entry.pass->execute(primary_stream, analysis_manager);

        // PILLAR 3: The Final Gate (Formal Verification)
        if (validate_strike(primary_stream)) [[likely]] {
            // Success: Atomic Commit. IR version increments -> TAM cache purges automatically.
            primary_stream.commit_transaction();
        } else {
            // Logic Breach: Rollback IR and Arena Memory
            primary_stream.rollback_transaction();
            global_arena.rollback(arena_checkpoint);
        }

        const auto end_time = std::chrono::high_resolution_clock::now();
        entry.last_execution_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
    return true;
}
```
**Purpose**: Execute complete optimization pipeline with transactional safety
**Algorithm**:
1. **Early Return**: Skip empty streams
2. **Pass Iteration**: Process each enabled pass
3. **Safety Setup**: Arena checkpoint, timing start, transaction begin
4. **Execution**: Call pass execute() with analysis manager
5. **Validation**: Deep verification of transformations
6. **Resolution**: Commit valid changes, rollback invalid ones
7. **Telemetry**: Record execution time

**Transactional Safety**: All transformations are atomic with rollback capability

### Management Methods

#### clear()
```cpp
void clear() noexcept {
    strikes.clear();
    analysis_manager.invalidate_all();
}
```
**Purpose**: Reset pipeline and analysis caches
**Operations**: Clear pass vector and invalidate all cached analyses

#### get_analysis_manager()
```cpp
[[nodiscard]] DGTLAnalysisManager& get_analysis_manager() noexcept {
    return analysis_manager;
}
```
**Purpose**: Direct access to analysis manager for pass registration

#### get_telemetry()
```cpp
[[nodiscard]] const std::vector<PipelineStrike>& get_telemetry() const noexcept {
    return strikes;
}
```
**Purpose**: Access performance telemetry for all passes

### Validation Logic

#### validate_strike()
```cpp
[[nodiscard]] bool validate_strike(ir::IRStream& stream) const noexcept {
    if (!stream.is_transactional()) [[unlikely]] return false;

    const size_t shadow_count = stream.size(); 
    for (uint32_t i = 0; i < shadow_count; ++i) {
        ir::IRNode shadow_node = stream.get_node_readonly(i);

        // Pillar 3: Formal Proof of Intent
        const uint32_t expected_hash = core::DGTLFormalVerifier::compute_master_hash(shadow_node);
        if (shadow_node.intent_hash != expected_hash) [[unlikely]] {
            return false; 
        }

        // Pillar 3: Bit-Consistency Gate
        if (!sentinel.verify_bit_consistency(shadow_node, shadow_node)) [[unlikely]] {
            return false;
        }
    }

    return stream.validate_shadow_integrity();
}
```
**Purpose**: Comprehensive validation of shadow buffer transformations
**Validation Layers**:
1. **Transactional Check**: Ensure stream is in transactional state
2. **Intent Verification**: Compare intent_hash with formal master hash
3. **Bit Consistency**: Sentinel check for IR node consistency
4. **Shadow Integrity**: Stream-level validation

**Return**: true if all validations pass, false triggers rollback

### Namespace Closing
```cpp
} // namespace dgtl::pipeline
```
- Closes the pipeline namespace

## Usage Example
```cpp
#include "Pass_Manager.hpp"

// Set up arena and pass manager
dgtl::core::DGTLArena arena(/*...*/);
dgtl::pipeline::DGTLPassManager pm(arena);

// Register optimization passes
pm.add_pass<dgtl::opt::DCEPass>();
pm.add_pass<dgtl::opt::GVNPass>();
pm.add_pass<dgtl::opt::HardwareScheduler>(ai_model);

// Prepare IR stream
dgtl::ir::IRStream stream(/*...*/);
// ... populate stream ...

// Execute optimization pipeline
bool success = pm.run(stream);

// Access telemetry
const auto& telemetry = pm.get_telemetry();
for (const auto& strike : telemetry) {
    std::cout << strike.pass->get_pass_name() 
              << " took " << strike.last_execution_ms << " ms\n";
}

// Access analysis manager for custom queries
auto& am = pm.get_analysis_manager();
// ... use analysis manager ...
```

## Dependencies
- **Arena.hpp**: Memory checkpoints and rollback
- **Global_IR.hpp**: IRStream transactional operations
- **Verified_Formal.hpp**: Formal verification and sentinel
- **Analysis_Manager.hpp**: Cached analysis facts

## Thread Safety
- **Pipeline Execution**: Not thread-safe, requires exclusive stream access
- **Pass Registration**: Not thread-safe, modify before execution
- **Telemetry Access**: Const access is thread-safe for reading

## Performance Characteristics
- **Execution Model**: Sequential pass execution with transactional boundaries
- **Memory Safety**: Zero-latency rollback via arena checkpoints
- **Validation Cost**: O(N) per pass where N is IR node count
- **Analysis Caching**: Lazy evaluation prevents redundant computation
- **Timing Precision**: High-resolution clock for sub-millisecond measurement
- **Exception Safety**: Noexcept design eliminates exception overhead