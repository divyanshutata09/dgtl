# Stack_spillar.hpp Documentation

## Map of the Control Flow

```
Stack Management & Lexical Scoping System
├── StackSlot Structure (16-byte aligned)
│   ├── int32_t offset (FP-relative offset)
│   ├── uint32_t size (bytes)
│   ├── OpCode type_hint (alignment validation)
│   ├── uint32_t version (shadowing detection)
│   └── bool is_spill (register spill flag)
├── StackPiller Class (64-byte aligned)
│   ├── Construction
│   │   ├── Arena-backed initialization
│   │   └── Global scope creation
│   ├── Scope Management
│   │   ├── push_scope() → add new scope layer
│   │   └── pop_scope() → remove innermost scope
│   ├── Variable Allocation
│   │   ├── allocate(name, bytes, type) → int32_t offset
│   │   │   ├── 16-byte alignment calculation
│   │   │   ├── Negative offset assignment
│   │   │   ├── FNV-1a name hashing
│   │   │   └── Slot creation and storage
│   ├── Variable Resolution
│   │   └── resolve(name) → optional<StackSlot>
│   │       ├── FNV-1a name hashing
│   │       └── Lexical scope search (inner→outer)
│   ├── Metrics & Control
│   │   ├── current_frame_size() → uint32_t
│   │   ├── nesting_depth() → size_t
│   │   └── reset_frame() → clear for new function
└── Hashing Utility
    └── hash_name(string_view) → uint64_t (FNV-1a)
```

**Data Flow:**
1. **Initialization**: StackPiller created with Arena reference, global scope initialized
2. **Scope Entry**: push_scope() adds new unordered_map layer to scope stack
3. **Allocation**: allocate() computes aligned offset, creates StackSlot, stores in current scope
4. **Resolution**: resolve() searches from innermost to outermost scope using hashed names
5. **Scope Exit**: pop_scope() removes top scope layer (except global scope)
6. **Frame Reset**: reset_frame() clears all scopes and resets frame size for new function

## Summary

Stack_spillar.hpp implements DGTL's stack management system for lexical scoping and variable allocation within the Bit-Packed Intermediate Representation (BIR). This component handles Pillar 2's "Shadowing" concept through nested scope layers, providing zero-latency symbol resolution and hardware-efficient stack frame layout. The design ensures 16-byte alignment for SIMD operations while supporting lexical variable shadowing.

Key features include:
- **Lexical Scoping**: Nested unordered_map scopes for variable shadowing support
- **16-Byte Alignment**: Hardware-efficient alignment for AVX-512/NEON operations
- **FNV-1a Hashing**: Ultra-fast symbol lookup with minimal collisions
- **Arena Integration**: Deterministic memory management for scope layers
- **Stack Frame Management**: Automatic offset calculation and frame size tracking
- **Type-Aware Allocation**: OpCode hints for alignment validation

The implementation follows DGTL's zero-overhead principle with compile-time hashing and cache-aligned data structures.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

#include "Global_IR.hpp"
#include "../core/Arena.hpp"

namespace dgtl::ir {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<cstdint>`: Fixed-width integers
  - `<vector>`: Dynamic arrays for scope stack
  - `<string_view>`: Non-owning string references
  - `<optional>`: For nullable return values
  - `<unordered_map>`: Hash maps for symbol tables
- **Internal Includes**:
  - `"Global_IR.hpp"`: IR definitions and OpCode
  - `"../core/Arena.hpp"`: Deterministic memory allocation
- **Namespace**: dgtl::ir

### StackSlot Structure
```cpp
/**
 * @struct StackSlot
 * @brief Represents a single variable's allocation within a stack frame.
 * Aligned to 16 bytes for SIMD-safe spills.
 */
struct alignas(16) StackSlot {
    int32_t offset;       // Offset relative to Frame Pointer (FP)
    uint32_t size;        // Size in bytes
    OpCode type_hint;     // Type for alignment validation
    uint32_t version;     // For shadowing detection
    bool is_spill;        // Flagged if this is a register-allocator spill
};
```
**Alignment**: 16-byte alignment ensures SIMD-safe memory access
**Members**:
- **offset**: Negative offset from frame pointer (grows downward)
- **size**: Variable size in bytes
- **type_hint**: OpCode for type-specific alignment rules
- **version**: Incrementing counter for shadowing detection
- **is_spill**: Flag for register allocator spills vs. user variables

### StackPiller Class Definition
```cpp
/**
 * @class StackPiller
 * @brief Manages the lexical lifecycle of symbols and their physical stack offsets.
 * Implements Pillar 2 "Shadowing" through nested hash-map scopes.
 */
class alignas(64) StackPiller {
public:
    using ScopeLayer = std::unordered_map<uint64_t, StackSlot>;
```
**Alignment**: 64-byte alignment prevents false sharing in multi-threaded scenarios
**ScopeLayer**: Type alias for symbol table (hash → slot mapping)

### Constructor
```cpp
explicit StackPiller(dgtl::core::DGTLArena& arena) 
    : m_arena(arena), m_current_frame_size(0) {
    // Initialize Global Scope
    m_scope_stack.emplace_back();
}
```
**Parameters**: Reference to Arena for potential future allocations
**Initialization**: 
- Store arena reference
- Reset frame size to 0
- Create initial global scope layer

### Scope Lifecycle Methods

#### push_scope()
```cpp
void push_scope() {
    m_scope_stack.emplace_back();
}
```
**Purpose**: Enter new lexical scope (function, block, etc.)
**Logic**: Add new empty unordered_map to scope stack

#### pop_scope()
```cpp
void pop_scope() {
    if (m_scope_stack.size() > 1) {
        m_scope_stack.pop_back();
    }
}
```
**Purpose**: Exit current lexical scope
**Logic**: Remove top scope layer, but preserve global scope (size > 1 check)

### Allocation Logic

#### allocate()
```cpp
int32_t allocate(std::string_view name, uint32_t bytes, OpCode type = OpCode::NOP) {
    // Maintain 16-byte alignment for hardware efficiency (AVX-512/NEON)
    m_current_frame_size = (m_current_frame_size + 15) & ~15;
    
    int32_t offset = -static_cast<int32_t>(m_current_frame_size + bytes);
    m_current_frame_size += bytes;

    uint64_t name_hash = hash_name(name);
    StackSlot slot{
        .offset = offset,
        .size = bytes,
        .type_hint = type,
        .version = 1,
        .is_spill = false
    };

    m_scope_stack.back()[name_hash] = slot;
    return offset;
}
```
**Purpose**: Allocate stack space for a variable
**Parameters**:
- name: Variable identifier
- bytes: Size in bytes
- type: OpCode hint for alignment (default NOP)

**Alignment Logic**:
- Round up current_frame_size to 16-byte boundary
- Formula: `(size + 15) & ~15` (clear lower 4 bits)

**Offset Calculation**:
- Negative offset (stack grows downward)
- offset = -(aligned_frame_size + bytes)

**Storage**:
- Hash variable name using FNV-1a
- Create StackSlot with computed values
- Store in current (innermost) scope
- Return computed offset

### Variable Resolution

#### resolve()
```cpp
[[nodiscard]] std::optional<StackSlot> resolve(std::string_view name) const noexcept {
    uint64_t name_hash = hash_name(name);

    // Search from innermost scope to global scope
    for (auto it = m_scope_stack.rbegin(); it != m_scope_stack.rend(); ++it) {
        if (auto found = it->find(name_hash); found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}
```
**Purpose**: Find variable's stack slot respecting lexical scoping
**Logic**:
- Hash variable name
- Search from innermost scope (rbegin) to outermost (rend)
- Return first match found
- Return nullopt if not found

**Lexical Shadowing**: Inner scopes can shadow outer scope variables

### Metrics and Control Methods

#### current_frame_size()
```cpp
[[nodiscard]] uint32_t current_frame_size() const noexcept {
    return m_current_frame_size;
}
```
**Purpose**: Get current stack frame size in bytes

#### nesting_depth()
```cpp
[[nodiscard]] size_t nesting_depth() const noexcept {
    return m_scope_stack.size();
}
```
**Purpose**: Get current lexical nesting depth

#### reset_frame()
```cpp
void reset_frame() {
    m_current_frame_size = 0;
    m_scope_stack.clear();
    m_scope_stack.emplace_back();
}
```
**Purpose**: Reset for new function compilation
**Logic**: Clear frame size and all scopes, reinitialize global scope

### Private Members
```cpp
private:
    dgtl::core::DGTLArena& m_arena;
    std::vector<ScopeLayer> m_scope_stack;
    uint32_t m_current_frame_size;
```
**m_arena**: Reference for future allocations (currently unused)
**m_scope_stack**: Vector of scope layers (innermost at back)
**m_current_frame_size**: Running total of allocated stack space

### Hash Function

#### hash_name()
```cpp
[[nodiscard]] static constexpr uint64_t hash_name(std::string_view name) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : name) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}
```
**Algorithm**: FNV-1a (Fowler-Noll-Vo hash variant 1a)
**Constants**:
- Initial hash: 0xcbf29ce484222325ULL
- Multiplication constant: 0x100000001b3ULL
**Properties**: Fast, good distribution, constexpr for compile-time hashing

### Namespace Closing and Compatibility
```cpp
} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for DGTL-Zenith high-level logic.
 */
namespace dgtl::ir {
    using StackSlot   = dgtl::ir::StackSlot;
    using StackPiller = dgtl::ir::StackPiller;
}
```
- Closes dgtl::ir namespace
- Provides backward compatibility aliases

## Usage Example
```cpp
#include "Stack_spillar.hpp"

dgtl::core::DGTLArena arena;
dgtl::ir::StackPiller stack_piller(arena);

// Global scope allocation
int32_t global_var_offset = stack_piller.allocate("global_var", 8, dgtl::ir::OpCode::IMMEDIATE);

// Enter function scope
stack_piller.push_scope();

// Function parameters and locals
int32_t param_offset = stack_piller.allocate("param", 4, dgtl::ir::OpCode::LOAD);
int32_t local_offset = stack_piller.allocate("local", 16, dgtl::ir::OpCode::STORE);

// Shadow global variable
int32_t shadowed_offset = stack_piller.allocate("global_var", 4, dgtl::ir::OpCode::LOAD);

// Resolve variables (respects shadowing)
auto resolved_local = stack_piller.resolve("local");        // Returns local variable
auto resolved_global = stack_piller.resolve("global_var");  // Returns shadowed version
auto not_found = stack_piller.resolve("undefined");         // Returns nullopt

// Exit function scope
stack_piller.pop_scope();

// Global variable accessible again
auto global_again = stack_piller.resolve("global_var");  // Returns original global
```

## Dependencies
- **Global_IR.hpp**: OpCode enum definitions
- **Arena.hpp**: DGTLArena for memory management
- **Standard Library**: `<unordered_map>`, `<vector>`, `<string_view>`, `<optional>`

## Thread Safety
- **Instance Methods**: Not thread-safe, requires external synchronization
- **hash_name()**: Static constexpr, inherently thread-safe
- **Arena Reference**: Shared arena may require synchronization for concurrent allocations

## Performance Characteristics
- **Hashing**: O(n) for string length, but constexpr for compile-time computation
- **Lookup**: O(1) average case for unordered_map, O(depth) for scope search
- **Allocation**: O(1) with fixed overhead for alignment calculation
- **Memory**: Minimal - only scope layers and frame size tracking
- **Cache**: 64-byte aligned class, 16-byte aligned slots for SIMD efficiency
- **Scalability**: Scope depth limited by practical nesting levels