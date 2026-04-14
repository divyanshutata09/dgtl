# IR_StreamCloneTransn.hpp Documentation

## Map of the Control Flow

```
IRStream Transactional Orchestrator
├── RAII Transaction Guard
│   ├── IRTransactionGuard
│   │   ├── Constructor: begin_transaction()
│   │   ├── commit(): commit_transaction()
│   │   └── Destructor: rollback if not committed
├── IRStream Class (64-byte aligned)
│   ├── Construction
│   │   ├── Default Constructor
│   │   ├── External Buffer Constructor
│   │   ├── Arena Constructor (Pillar 1)
│   │   ├── Static Allocators: allocate_transactional/allocate_shadow
│   │   └── Rule of Five: Copy/Move operations
│   ├── Transaction Engine
│   │   ├── begin_transaction() → in_transaction = true
│   │   ├── commit_transaction() → validate + memcpy shadow→primary
│   │   ├── abort_transaction() → discard shadow
│   │   └── rollback_transaction() → reset to start_count
│   ├── Buffer Management
│   │   ├── Primary Buffers: atoms[], cores[]
│   │   ├── Shadow Buffers: shadow_atoms[], shadow_cores[]
│   │   ├── State: count, shadow_count, capacity
│   │   └── Accessors: raw_atoms(), raw_cores(), get_buffer_ptr()
│   ├── Node Operations
│   │   ├── Emission: push_back(), push_node()
│   │   ├── Access: get_node(), get_atom(), get_core()
│   │   ├── Shadow Ops: push_to_shadow(), reserve_shadow_slot()
│   │   └── Iteration: begin()/end() iterators
│   ├── Shadow Synchronization
│   │   ├── commit_from_shadow() → merge external shadow
│   │   ├── prune_shadow_buffer() → clean shadow state
│   │   └── push_full_to_shadow() → copy primary to shadow
│   ├── Performance Passes
│   │   ├── perform_dead_code_elimination()
│   │   ├── perform_gvn() (Global Value Numbering)
│   │   └── validate_shadow_integrity()
│   └── Validation & Hashing
│       ├── intent_crc() → CRC32 of operations
│       ├── generate_logic_hash() → 64-bit validation hash
│       └── get_max_id() → maximum result ID
└── Transaction Lifecycle
    ├── Idle State: in_transaction = false
    ├── Active: begin_transaction() → modify shadow buffers
    ├── Commit: validate_shadow_integrity() → memcpy → count += shadow_count
    ├── Rollback: set_count(start_count) → shadow_count = 0
    └── Guard: Automatic rollback on exception
```

**Data Flow:**
1. **Construction**: Allocate SoA buffers (Arena preferred, OS fallback)
2. **Transaction Begin**: Set in_transaction flag, work on shadow buffers
3. **Emission**: push_node() writes to shadow_atoms/cores, increments shadow_count
4. **Validation**: validate_shadow_integrity() checks consistency
5. **Commit**: memcpy shadow → primary, update counts, reset transaction
6. **Access**: get_node() returns IRNode view (primary or shadow based on transaction state)
7. **Cleanup**: RAII guard ensures rollback on failure

## Summary

IR_StreamCloneTransn.hpp defines the `IRStream` class, the primary orchestrator for DGTL's Bit-Packed Intermediate Representation (BIR), implementing Pillar 2's tri-tier transactional stream with shadow-aware emission. The class provides a high-velocity SoA (Structure of Arrays) interface for IR manipulation with transactional safety, enabling zero-cost rollbacks and validation. It includes an RAII transaction guard for automatic cleanup and integrates with the Arena system for deterministic memory management.

Key features include:
- **Transactional Engine**: Double-buffered shadow validation with atomic state management
- **SoA Optimization**: Cache-efficient structure-of-arrays layout for SIMD operations
- **RAII Safety**: IRTransactionGuard ensures transactional integrity
- **Multi-View Access**: Primary/shadow buffer switching with unified IRNode interface
- **Performance Passes**: Built-in dead code elimination and global value numbering
- **Validation Framework**: CRC and logic hashing for integrity verification

The implementation follows zero-overhead principles with 64-byte alignment and atomic operations for HFT-grade performance.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <atomic>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <algorithm>

// Internal Modularized Components
#include "IR_Types.hpp"
#include "IR_Traits.hpp"
#include "IR_Node.hpp"
#include "IR_Storage.hpp"

// Arena dependency (Core Logic)
#include "../core/Arena.hpp"

namespace dgtl::ir {
```
- **#pragma once**: Header guard
- **Standard Includes**: Atomic operations, containers, algorithms for IR manipulation
- **Internal Includes**: All IR component headers for unified interface
- **Arena Include**: Deterministic memory management
- **Namespace**: dgtl::ir for all declarations

### Forward Declaration and RAII Guard
```cpp
// --- FORWARD DECLARATION FOR RAII COMPLIANCE ---
class IRStream;

/**
 * @struct IRTransactionGuard
 * @brief RAII Guard for transactional safety. 
 * Automatically rolls back the stream if commit() is not called.
 */
struct IRTransactionGuard {
    IRStream& stream;
    size_t start_count;
    bool committed = false;

    explicit IRTransactionGuard(IRStream& s);
    void commit();
    ~IRTransactionGuard();
};
```
**Purpose**: Provides exception-safe transaction management
**Members**:
- stream: Reference to guarded IRStream
- start_count: Snapshot of count at transaction start
- committed: Flag to prevent double commit

**RAII Pattern**: Constructor begins transaction, destructor rolls back if not committed

### IRStream Class Definition
```cpp
/**
 * @class IRStream
 * @brief High-Velocity SoA Stream with Double-Buffered Shadow Validation.
 * This class serves as the primary interface for BIR manipulation. It leverages
 * the SoA (Structure of Arrays) layout for cache-optimal throughput.
 */
class alignas(64) IRStream {
```
**Alignment**: 64-byte alignment prevents false sharing in multi-threaded scenarios
**Purpose**: Main orchestrator for BIR operations with transactional semantics

### Copy Operations (Rule of Five)
```cpp
IRStream(const IRStream& other) noexcept {
    // Deep copy of all SoA buffers with atomic state sync
}

IRStream& operator=(const IRStream& other) noexcept {
    // Deep copy assignment with self-check
}
```
**Logic**: Performs deep copy of all buffers and atomic state
**Thread Safety**: Uses acquire/release memory ordering for atomic variables

### Iterator Support
```cpp
struct Iterator {
    const IRStream* stream;
    size_t index;
    bool operator!=(const Iterator& other) const { return index != other.index; }
    void operator++() { ++index; }
    IRNode operator*() const;
};

Iterator begin() const { return {this, 0}; }
Iterator end() const { return {this, count}; }
```
**Purpose**: Enables range-based for loops over IR nodes
**Implementation**: Lightweight iterator returning IRNode views

### Private Buffer Members
```cpp
private:
    // Core SoA Buffers
    IRAtom* atoms         = nullptr;
    IRCore* cores         = nullptr;
    IRAtom* shadow_atoms  = nullptr;
    IRCore* shadow_cores  = nullptr;

    size_t shadow_count   = 0; 
    size_t _capacity      = 0; 
    std::atomic<bool> in_transaction{false};

    mutable IRNode* joined_buffer = nullptr;
    bool owns_buffer = false;
```
**Buffers**: SoA layout for cache efficiency
**State**: Atomic transaction flag, separate counts for primary/shadow
**Ownership**: owns_buffer indicates Arena vs OS management

### Constructors and Destructors
```cpp
IRStream() = default;
IRStream(IRNode* external_buffer, size_t node_count);
~IRStream();
IRStream(IRStream&& other) noexcept;
IRStream& operator=(IRStream&& other) noexcept;
IRStream(dgtl::core::DGTLArena& arena, size_t max_nodes);
```
**Variants**:
- Default: Empty stream
- External Buffer: Wrap existing IRNode array
- Arena: Deterministic allocation with specified capacity
- Move operations: Transfer ownership efficiently

### Static Allocators
```cpp
static IRStream allocate_transactional(size_t size);
static IRStream allocate_shadow(size_t size);
```
**Purpose**: Factory methods for different allocation strategies
**Transactional**: Prepares for transaction-heavy workloads
**Shadow**: Optimizes for shadow buffer operations

### Transaction Engine

#### Transaction State Queries
```cpp
[[nodiscard]] bool is_transactional() const noexcept { return in_transaction.load(std::memory_order_acquire); }
bool is_active_transaction() const noexcept { return is_transactional(); }
bool is_committed() const noexcept { return !is_transactional(); }
```
**Purpose**: Query current transaction state
**Thread Safety**: Atomic load with acquire semantics

#### Transaction Control
```cpp
void begin_transaction() noexcept;
void begin_strike() noexcept { begin_transaction(); }
void commit_transaction() noexcept;
void commit_strike() noexcept { commit_transaction(); }
void abort_transaction() noexcept;
void release_transaction() noexcept { in_transaction.store(false, std::memory_order_release); }
void rollback_transaction() noexcept;
void rollback_strike() noexcept { rollback_transaction(); }
```
**Begin**: Sets in_transaction flag, switches to shadow buffers
**Commit**: Validates shadow, copies to primary, resets flag
**Abort/Rollback**: Discards shadow changes, restores original state
**Aliases**: strike_ variants for optimization terminology

### Shadow Buffer Management
```cpp
void prune_shadow_buffer() noexcept;
void push_full_to_shadow() noexcept;
void commit_from_shadow(IRStream& shadow_stream) noexcept;
void commit_from_shadow() noexcept;
void copy_from(const IRStream& other);
```
**Prune**: Clean up shadow buffer state
**Push Full**: Copy entire primary to shadow
**Commit From Shadow**: Merge external shadow stream
**Copy From**: Deep copy from another stream

### Buffer Access Methods
```cpp
[[nodiscard]] IRAtom* raw_atoms() const noexcept { return in_transaction.load(std::memory_order_acquire) ? shadow_atoms : atoms; }
[[nodiscard]] IRCore* raw_cores() const noexcept { return in_transaction.load(std::memory_order_acquire) ? shadow_cores : cores; }
[[nodiscard]] IRNode* get_buffer_ptr() const noexcept;
```
**Logic**: Returns appropriate buffer based on transaction state
**Thread Safety**: Atomic check for transaction status

### Node Accessors
```cpp
[[nodiscard]] IRNode get_node(size_t idx) const noexcept;
[[nodiscard]] IRNode get_node_readonly(size_t idx) const noexcept { return get_node(idx); }
[[nodiscard]] const IRCore& get_core_readonly(size_t idx) const noexcept;
[[nodiscard]] const IRAtom& get_atom_readonly(size_t idx) const noexcept;

[[nodiscard]] inline IRAtom& get_atom(size_t idx) noexcept {
    return in_transaction.load(std::memory_order_acquire) ? shadow_atoms[idx] : atoms[idx];
}

[[nodiscard]] inline IRCore& get_core(size_t idx) noexcept { 
    return in_transaction.load(std::memory_order_acquire) ? shadow_cores[idx] : cores[idx]; 
}
```
**Read-Only**: Const access to primary buffers
**Mutable**: Returns reference to appropriate buffer (primary or shadow)
**Thread Safety**: Atomic transaction state checks

### Emission Methods
```cpp
void push_back(const IRNode& node) noexcept;
void push_node(const IRNode& node) noexcept { push_back(node); }
void push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h = 0) noexcept;
```
**push_back**: Append IRNode to current transaction buffer
**push_node**: Alias for push_back, plus parameterized version
**Logic**: Writes to shadow buffers during transactions

### Shadow-Specific Operations
```cpp
[[nodiscard]] uint32_t reserve_shadow_slot() noexcept;
void push_to_shadow(const IRNode& node) noexcept;
void push_shadow_fold(const IRNode& node) noexcept;
[[nodiscard]] inline IRAtom& get_shadow_atom(size_t idx) noexcept { return shadow_atoms[idx]; }
[[nodiscard]] inline IRCore& get_shadow_core(size_t idx) noexcept { return shadow_cores[idx]; }
void set_shadow_count(size_t val) noexcept;
void update_shadow_size(size_t new_size) noexcept;
```
**Reserve**: Allocate slot in shadow buffer
**Push Operations**: Direct shadow buffer manipulation
**Accessors**: Direct shadow buffer access (bypasses transaction checks)

### Performance Passes
```cpp
void perform_dead_code_elimination() noexcept;
void perform_gvn() noexcept;
```
**DCE**: Removes unreachable instructions
**GVN**: Global Value Numbering for redundancy elimination
**Purpose**: Built-in optimization passes

### Validation and Hashing
```cpp
[[nodiscard]] uint32_t intent_crc() const noexcept;
[[nodiscard]] uint64_t generate_logic_hash() const noexcept;
[[nodiscard]] bool validate_shadow_integrity() const noexcept;
[[nodiscard]] uint32_t get_max_id() const noexcept;
[[nodiscard]] IRStream clone() const;
```
**CRC**: 32-bit checksum of operation intents
**Logic Hash**: 64-bit validation hash
**Integrity**: Validates shadow buffer consistency
**Max ID**: Finds highest result identifier
**Clone**: Creates identical copy

### RAII Guard Implementation
```cpp
inline IRTransactionGuard::IRTransactionGuard(IRStream& s) : stream(s), start_count(s.count) {
    stream.begin_transaction();
}

inline void IRTransactionGuard::commit() {
    stream.commit_transaction();
    committed = true;
}

inline IRTransactionGuard::~IRTransactionGuard() {
    if (!committed) {
        stream.rollback_transaction();
        stream.set_count(start_count);
    }
}
```
**Constructor**: Begins transaction, saves start count
**Commit**: Finalizes transaction, marks as committed
**Destructor**: Rolls back if not committed, restores count

### Namespace Closing
```cpp
} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
```
- Closes namespace
- Backward compatibility alias

## Usage Example
```cpp
#include "IR_StreamCloneTransn.hpp"

// Create transactional stream
dgtl::core::DGTLArena arena;
dgtl::ir::IRStream stream(arena, 1024);

// RAII transaction
{
    dgtl::ir::IRTransactionGuard guard(stream);
    
    // Emit nodes to shadow buffer
    stream.push_node(OpCode::ADD_I32, 0, 1, 10, 20, 0);
    stream.push_node(OpCode::STORE, 0, 0, 1, 100, 0);
    
    // Validate and commit
    if (stream.validate_shadow_integrity()) {
        guard.commit();  // Changes become permanent
    }
    // Automatic rollback if commit() not called
}

// Access committed nodes
for (const auto& node : stream) {
    // Process node...
}
```

## Dependencies
- **IR_Types.hpp**: OpCode, IRAtom, IRCore definitions
- **IR_Traits.hpp**: Semantic analysis functions
- **IR_Node.hpp**: IRNode unified view
- **IR_Storage.hpp**: Storage management utilities
- **Arena.hpp**: Deterministic memory allocation

## Thread Safety
- **Transaction State**: std::atomic with proper memory ordering
- **Buffer Access**: Transaction-aware automatic primary/shadow switching
- **Concurrent Access**: External synchronization required for multi-threaded use
- **RAII Guard**: Exception-safe transaction management

## Performance Characteristics
- **Alignment**: 64-byte cache-line alignment
- **SoA Layout**: SIMD-friendly memory access patterns
- **Transactional Overhead**: Minimal atomic operations
- **Commit Speed**: memcpy-based bulk transfer
- **Iterator Performance**: Zero-overhead range access
- **Memory Efficiency**: Separate primary/shadow buffers minimize copying