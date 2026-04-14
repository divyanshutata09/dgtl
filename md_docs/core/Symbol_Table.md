# DGTL Symbol Table (Symbol_Table.hpp)

## Control Flow Map

```
[SymbolTable Class]
    |
    +--> Constructor (SymbolTable())
    |       +--> Create internal fallback arena
    |       +--> Initialize with global scope
    |
    +--> Constructor (SymbolTable(DGTLArena& arena))
    |       +--> Use provided arena
    |       +--> Initialize with global scope
    |
    +--> Scope Management
    |       +--> push_scope()
    |       |       +--> Add new scope to stack
    |       |
    |       +--> pop_scope()
    |               +--> Remove current scope (keep global)
    |
    +--> Symbol Operations
    |       +--> define(std::string_view name, uint32_t slot_id, OpCode type)
    |       |       +--> Compute hash
    |       |       +--> Check for redefinition in current scope
    |       |       +--> Create SymbolEntry
    |       |       +--> Insert into current scope
    |       |       +--> Return success
    |       |
    |       +--> resolve(std::string_view name)
    |       |       +--> Compute hash
    |       |       +--> Search from inner to outer scope
    |       |       +--> Return optional SymbolEntry
    |       |
    |       +--> update(std::string_view name, uint32_t new_slot)
    |               +--> Compute hash
    |               +--> Find symbol in scope stack
    |               +--> Update slot_id and increment version
    |               +--> Return success
    |
    +--> Utility Methods
    |       +--> compute_hash(std::string_view name)
    |       |       +--> FNV-1a hash computation
    |       |
    |       +--> depth()
    |       |       +--> Return scope stack size
    |       |
    |       +--> reset()
    |               +--> Clear all scopes except global
    |               +--> Reinitialize global scope
```

**Flow Description**: The symbol table manages scoped symbol resolution with a stack of unordered_maps. Symbols are defined in the current scope, resolved by searching from inner to outer scopes, and can be updated across scope boundaries. Uses FNV-1a hashing for fast lookups.

## Summary

`Symbol_Table.hpp` implements a scoped symbol table with arena-backed allocation and transactional scope management, providing high-velocity identity resolution for DGTL's BIR ecosystem. It features tiered scope stacking for shadowing support, FNV-1a hashing for collision-resistant lookups, and version tracking for symbol updates.

Key features:
- Scoped symbol resolution with lexical scoping rules
- Arena-backed memory allocation for efficiency
- FNV-1a hashing for fast, collision-resistant lookups
- Version tracking for symbol updates and invalidation
- Transactional scope management with push/pop operations
- Type hints and metadata support for symbol attributes

## Full Explanation

### Architecture Overview
The SymbolTable serves as the identity resolution system for DGTL's compilation pipeline:

1. **Scoped Resolution**: Lexical scoping with symbol shadowing
2. **Hash-Based Lookup**: FNV-1a for O(1) average-case performance
3. **Arena Integration**: Memory-efficient allocation from DGTLArena
4. **Transactional Scopes**: Stack-based scope management
5. **Version Control**: Symbol update tracking and invalidation

### Key Classes and Structures

#### SymbolEntry
Symbol representation structure:

```cpp
struct alignas(16) SymbolEntry {
    uint32_t slot_id = ir::INVALID_SLOT;    // IR slot identifier
    uint32_t flags = 0;                    // Symbol flags
    ir::OpCode type_hint = ir::OpCode::NOP; // Type information
    uint32_t version = 0;                  // Update version
    uint64_t name_hash = 0;                // Cached hash
    void* metadata = nullptr;              // Type metadata pointer
};
```

- **16-byte Alignment**: Optimal for cache line usage
- **Slot ID**: Links to IR storage locations
- **Version Tracking**: Detects symbol modifications
- **Type Hints**: Opcode-based type information
- **Metadata**: Extensible attribute storage

#### SymbolTable
Main symbol management class:

```cpp
class alignas(64) SymbolTable {
    using ScopeMap = std::unordered_map<uint64_t, SymbolEntry>;
    
    std::unique_ptr<DGTLArena> m_fallback_arena; // Standalone support
    DGTLArena& m_arena;                         // Memory source
    std::vector<ScopeMap> m_scopes;             // Scope stack
    
public:
    SymbolTable();                              // With internal arena
    explicit SymbolTable(DGTLArena& arena);     // With external arena
    
    void push_scope();
    void pop_scope();
    
    bool define(std::string_view name, uint32_t slot_id, ir::OpCode type = ir::OpCode::NOP);
    std::optional<SymbolEntry> resolve(std::string_view name) const noexcept;
    bool update(std::string_view name, uint32_t new_slot);
    
    static uint64_t compute_hash(std::string_view name) noexcept;
    size_t depth() const noexcept;
    void reset();
};
```

### Scope Management

#### Scope Stack Operations
```cpp
void push_scope() {
    m_scopes.emplace_back();
}

void pop_scope() {
    if (m_scopes.size() > 1) {
        m_scopes.pop_back();
    }
}
```

- **Stack Discipline**: Vector-based scope stack
- **Global Protection**: Prevents popping the global scope
- **RAII Ready**: Supports scope-based resource management

### Symbol Operations

#### Symbol Definition
```cpp
bool define(std::string_view name, uint32_t slot_id, ir::OpCode type = ir::OpCode::NOP) {
    uint64_t hash = compute_hash(name);
    auto& current_scope = m_scopes.back();
    
    if (current_scope.contains(hash)) {
        return false; // Redefinition in same scope
    }
    
    SymbolEntry entry{
        .slot_id = slot_id,
        .flags = 0,
        .type_hint = type,
        .version = 1,
        .name_hash = hash,
        .metadata = nullptr
    };
    
    current_scope[hash] = entry;
    return true;
}
```

- **Hash Computation**: FNV-1a for collision resistance
- **Redefinition Check**: Same-scope conflicts detected
- **Version Initialization**: Starts at version 1
- **Type Hints**: Optional opcode-based typing

#### Symbol Resolution
```cpp
std::optional<SymbolEntry> resolve(std::string_view name) const noexcept {
    uint64_t hash = compute_hash(name);
    
    // Search from inner to outer scope
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (auto entry_it = it->find(hash); entry_it != it->end()) {
            return entry_it->second;
        }
    }
    
    return std::nullopt;
}
```

- **Inner-First Search**: Lexical scoping rules
- **Hash Lookup**: O(1) average case in unordered_map
- **Optional Return**: No exceptions for missing symbols
- **Noexcept**: Exception-free resolution

#### Symbol Update
```cpp
bool update(std::string_view name, uint32_t new_slot) {
    uint64_t hash = compute_hash(name);
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (auto entry_it = it->find(hash); entry_it != it->end()) {
            entry_it->second.slot_id = new_slot;
            entry_it->second.version++;
            return true;
        }
    }
    return false;
}
```

- **Cross-Scope Update**: Finds symbol anywhere in scope stack
- **Slot Modification**: Updates IR slot identifier
- **Version Increment**: Tracks modification for invalidation
- **Failure Return**: False if symbol not found

### Hash Function

#### FNV-1a Implementation
```cpp
static constexpr uint64_t compute_hash(std::string_view name) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;  // FNV offset basis
    for (char c : name) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 0x100000001b3ULL;           // FNV prime
    }
    return hash;
}
```

- **Compile-Time**: constexpr for static initialization
- **Standard FNV-1a**: Well-tested hash function
- **64-bit**: Full 64-bit hash space
- **Noexcept**: Exception-free computation

### Utility Operations

#### Scope Depth Query
```cpp
size_t depth() const noexcept {
    return m_scopes.size();
}
```

- **Stack Size**: Current nesting level
- **Noexcept**: Simple query operation

#### Table Reset
```cpp
void reset() {
    m_scopes.clear();
    m_scopes.emplace_back();
}
```

- **Complete Clear**: Remove all local scopes
- **Global Reset**: Reinitialize global scope
- **Memory Cleanup**: unordered_map cleanup

### Memory Management

#### Arena Integration
```cpp
SymbolTable() 
    : m_fallback_arena(std::make_unique<DGTLArena>()), 
      m_arena(*m_fallback_arena) {
    m_scopes.emplace_back();
}

explicit SymbolTable(DGTLArena& arena) 
    : m_arena(arena) {
    m_scopes.emplace_back();
}
```

- **Fallback Arena**: Standalone operation support
- **External Arena**: Integration with DGTL memory system
- **Scope Initialization**: Global scope always present

### Performance Characteristics

- **Definition**: O(1) average case hash + map insert
- **Resolution**: O(depth) worst case, O(1) typical
- **Update**: O(depth) search + O(1) modification
- **Memory**: 32 bytes per SymbolEntry + hash map overhead
- **Hash Quality**: FNV-1a provides good distribution

### Integration Points

- **IR Emission**: Slot ID assignment and resolution
- **Parser**: Symbol definition during syntax analysis
- **Optimizer**: Symbol updates during transformations
- **Code Generation**: Final symbol-to-slot mapping
- **Debug Info**: Symbol metadata for debugging

### Usage Example

```cpp
#include "dgtl/core/Symbol_Table.hpp"

int main() {
    dgtl::core::DGTLArena arena(64); // 64MB
    dgtl::core::SymbolTable symtab(arena);
    
    // Define symbols in global scope
    symtab.define("x", 0, dgtl::ir::OpCode::LOAD);
    symtab.define("y", 1, dgtl::ir::OpCode::LOAD);
    
    // Push function scope
    symtab.push_scope();
    symtab.define("x", 2, dgtl::ir::OpCode::LOAD); // Shadows global x
    
    // Resolve symbols (finds local x first)
    auto entry = symtab.resolve("x");
    if (entry) {
        std::cout << "Slot: " << entry->slot_id << std::endl; // Prints 2
    }
    
    // Update symbol
    symtab.update("y", 5); // Updates global y
    
    // Pop scope
    symtab.pop_scope();
    
    // Now resolves to global x
    entry = symtab.resolve("x");
    if (entry) {
        std::cout << "Slot: " << entry->slot_id << std::endl; // Prints 0
    }
    
    return 0;
}
```

### Legacy Compatibility

```cpp
// Aliases for backward compatibility
using DGTLSymbolTable = dgtl::core::SymbolTable;

// Cross-namespace exposure
namespace dgtl::ir {
    using SymbolEntry = dgtl::core::SymbolEntry;
    using SymbolTable = dgtl::core::SymbolTable;
}
```

### Future Enhancements

- Incremental hashing for symbol prefixes
- Perfect hashing for known symbol sets
- Memory-efficient small string optimization
- Concurrent symbol resolution
- Symbol lifetime analysis

This header provides the symbol resolution foundation for DGTL's compilation pipeline, enabling efficient scoped name lookup with transactional scope management and version tracking.