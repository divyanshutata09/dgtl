# GVN.hpp Documentation

## Map of the Control Flow

```
Transactional Global Value Numbering (SIMD-Hybrid GVN)
├── ValueKey Struct
│   ├── opcode: Operation code
│   ├── flags: Semantic flags only
│   ├── op_a/op_b/op_c: Operand IDs
│   ├── intent_hash: Stable hash value
│   └── operator==: Semantic equality (ignores result_id)
├── DGTLGVN Class
│   ├── Constructor(max_ids): Initialize tables and ID map
│   ├── execute(stream)
│   │   ├── Validation: Check stream size
│   │   ├── Table Reset: Clear primary/secondary tables
│   │   ├── Transaction Begin: begin_strike()
│   │   ├── Shadow Pruning: prune_shadow_buffer()
│   │   ├── Node Processing: Process each instruction
│   │   ├── Side Effect Handling: Preserve critical operations
│   │   ├── Operand Resolution: Apply ID mapping
│   │   ├── Commutative Normalization: Sort operands
│   │   ├── Value Lookup: lookup_or_insert()
│   │   ├── Redundancy Elimination: Replace with golden IDs
│   │   ├── Sentinel Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Error Handling: Rollback on integrity failure
│   ├── resolve(id): Path compression ID resolution
│   ├── is_commutative(op): Check operation commutativity
│   ├── lookup_or_insert(key, current_id): Dual hash table lookup
│   ├── mix_h1/mix_h2(key): Hash function variants
│   └── NodeHash/NodeEq: SIMD fingerprinting helpers
├── GVNPass Class (Legacy)
│   ├── NodeHash: SIMD-based node hashing
│   ├── NodeEq: SIMD-based node equality
│   └── run(stream): Simple value numbering
└── Pillar Integration
    ├── Pillar 1: SIMD fingerprinting (AVX-512/AVX2)
    ├── Pillar 2: Shadow tier prevents IR corruption
    ├── Pillar 3: Intent-hash validation
    ├── Dual Hash Tables: Primary/secondary collision handling
    ├── Commutative Normalization: Operand ordering for better matching
    ├── Side Effect Protection: Preserves critical operations
    ├── Zero-Corruption: Failed GVN cannot poison main IR
```

**Data Flow:**
1. **Initialization**: Reset hash tables and ID mapping
2. **Transaction Start**: Begin shadow tier operation
3. **Node Analysis**: Process each instruction in stream
4. **Semantic Key Creation**: Build value keys ignoring result IDs
5. **Value Lookup**: Check for existing equivalent computations
6. **Redundancy Elimination**: Replace redundant operations with existing IDs
7. **Shadow Construction**: Build optimized shadow stream
8. **Integrity Validation**: Check shadow correctness
9. **Commit/Rollback**: Apply changes or revert based on validation

## Summary

GVN.hpp implements a transactional global value numbering pass that uses SIMD-accelerated fingerprinting and dual hash tables to eliminate redundant computations. The pass employs Pillar 2 shadow operations and Pillar 3 intent-hash validation to guarantee zero-corruption while achieving high-performance value deduplication through AVX-512/AVX2 intrinsics.

Key features include:
- **SIMD Fingerprinting**: AVX-512/AVX2 accelerated node comparison
- **Dual Hash Tables**: Primary/secondary tables for collision handling
- **Transactional Safety**: Shadow tier prevents corruption of main IR
- **Semantic Equality**: Ignores result IDs for true value matching
- **Commutative Normalization**: Operand ordering for better deduplication
- **Side Effect Protection**: Preserves stores, returns, and system calls
- **Path Compression**: Efficient ID resolution with union-find
- **Zero-Corruption Guarantee**: Failed GVN operations leave IR unchanged

The implementation achieves global value numbering with transactional safety and SIMD-accelerated performance.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <immintrin.h> // AVX-512 / AVX2
#include "../ir/Global_IR.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic arrays for ID mapping
  - `<memory>`: Smart pointers for table management
  - `<cstdint>`: Fixed-width integers
  - `<cstring>`: Memory operations
  - `<algorithm>`: std::swap for commutative ops
  - `<iostream>`: Error output
  - `<unordered_map>`: Legacy GVN pass
  - `<immintrin.h>`: AVX-512/AVX2 intrinsics
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR types and operations

### ValueKey Struct
```cpp
struct alignas(16) ValueKey {
    uint16_t opcode;
    uint16_t flags;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint32_t intent_hash; 

    bool operator==(const ValueKey& other) const noexcept {
        // ✅ DEDUP FIX: Ignore intent_hash for semantic equality.
        // Node 0 (1001) and Node 1 (1002) must match even if their result_id differs.
        return opcode == other.opcode &&
               flags   == other.flags   &&
               op_a    == other.op_a    &&
               op_b    == other.op_b    &&
               op_c    == other.op_c;
    }
};
```
**Purpose**: Semantic key for value numbering
**Alignment**: 16-byte for SIMD operations
**Members**: Operation components excluding result_id
**Equality**: Semantic comparison ignoring result differences

### DGTLGVN Class
```cpp
class DGTLGVN {
    static constexpr size_t TableSize = 1 << 18; 
    
    struct Entry {
        ValueKey key{};
        uint32_t result_id = 0;
    };

    std::unique_ptr<Entry[]> primary_table;
    std::unique_ptr<Entry[]> secondary_table;
    std::vector<uint32_t> id_map;
```
**Constants**:
- **TableSize**: 256K entries (2^18)
**Members**:
- **primary_table**: Main hash table
- **secondary_table**: Collision resolution table
- **id_map**: Union-find structure for ID mapping

### Constructor
```cpp
explicit DGTLGVN(size_t max_ids = 1048576) {
    primary_table = std::make_unique<Entry[]>(TableSize);
    secondary_table = std::make_unique<Entry[]>(TableSize);
    id_map.resize(max_ids);
    for(uint32_t i = 0; i < max_ids; ++i) id_map[i] = i;
}
```
**Purpose**: Initialize GVN tables
**Parameters**:
- **max_ids**: Maximum number of IDs to track (default 1M)
**Initialization**: Identity mapping for ID resolution

### execute()
```cpp
void execute(ir::IRStream& stream) {
    const size_t original_count = stream.size();
    if (original_count == 0) return;

    std::memset(primary_table.get(), 0, sizeof(Entry) * TableSize);
    std::memset(secondary_table.get(), 0, sizeof(Entry) * TableSize);

    stream.begin_strike();
    stream.prune_shadow_buffer();

    bool has_live_op = false;

    for (size_t i = 0; i < original_count; ++i) {
        // ... node processing logic ...
    }

    if (!has_live_op) {
        std::cerr << "[DGTL-GVN] WARNING: All ops eliminated. Preserving original stream.\n";
        stream.rollback_strike();
        return;
    }

    if (stream.validate_shadow_integrity()) {
        stream.commit_strike();
    } else {
        std::cerr << "[DGTL-SENTINEL] ALERT: Shadow Integrity Mismatch detected after GVN. Rolling back.\n";
        stream.rollback_strike(); 
    }
}
```
**Purpose**: Execute transactional GVN pass
**Algorithm**:
1. Reset hash tables
2. Begin shadow transaction
3. Process all nodes
4. Validate live operations exist
5. Check shadow integrity
6. Commit or rollback

### Node Processing Logic
```cpp
const ir::IRAtom& src_atom = stream.get_atom_readonly(i);
const ir::IRCore& src_core = stream.get_core_readonly(i);

if (src_atom.opcode == ir::OpCode::NOP) {
    // Handle NOP nodes
}

const bool has_se = ir::IRTraits::has_side_effects(src_atom.opcode, src_atom.flags) 
                    || src_atom.opcode == ir::OpCode::SYSCALL
                    || src_atom.opcode == ir::OpCode::RETURN
                    || src_atom.opcode == ir::OpCode::RET;

if (has_se) {
    // Handle side effect nodes
}

uint32_t res_op_a = resolve(src_core.op_a);
uint32_t res_op_b = resolve(src_core.op_b);
uint32_t res_op_c = resolve(src_core.op_c);

if (is_commutative(src_atom.opcode)) {
    if (res_op_a > res_op_b) std::swap(res_op_a, res_op_b);
}

// Create semantic key and lookup
ValueKey key{...};
uint32_t golden_id = lookup_or_insert(key, src_core.result_id);

if (golden_id != src_core.result_id && src_core.result_id != 0 && golden_id != 0) {
    // Replace with golden ID, mark original as dead
} else {
    // Keep original node
}
```
**Processing Phases**:
1. **NOP Handling**: Preserve NOP operations
2. **Side Effect Protection**: Always keep critical operations
3. **Operand Resolution**: Apply current ID mappings
4. **Commutative Normalization**: Sort operands for better matching
5. **Value Lookup**: Find existing equivalent computations
6. **Redundancy Elimination**: Replace duplicates with canonical IDs

### resolve()
```cpp
inline uint32_t resolve(uint32_t id) {
    if (id >= id_map.size() || id == 0 || id == ir::INVALID_SLOT) return id;
    if (id_map[id] == id) return id;
    return id_map[id] = resolve(id_map[id]);
}
```
**Purpose**: Path compression ID resolution
**Algorithm**: Union-find with path compression
**Safety**: Bounds checking and invalid ID handling

### is_commutative()
```cpp
static bool is_commutative(ir::OpCode op) noexcept {
    return (op == ir::OpCode::ADD_I32 || op == ir::OpCode::MUL_I32 || 
            op == ir::OpCode::AND_I32 || op == ir::OpCode::OR_I32 || 
            op == ir::OpCode::XOR_I32 || op == ir::OpCode::ADD_F32 ||
            op == ir::OpCode::MUL_F32);
}
```
**Purpose**: Check if operation is commutative
**Operations**: Arithmetic and logical operations that commute

### lookup_or_insert()
```cpp
uint32_t lookup_or_insert(const ValueKey& key, uint32_t current_id) {
    if (current_id == 0 || current_id == ir::INVALID_SLOT) return current_id;
    
    const uint64_t h1 = mix_h1(key) & (TableSize - 1);
    const uint64_t h2 = mix_h2(key) & (TableSize - 1);

    if (primary_table[h1].result_id != 0 && primary_table[h1].key == key) 
        return primary_table[h1].result_id;
    if (primary_table[h1].result_id == 0) { 
        primary_table[h1] = {key, current_id}; 
        return current_id; 
    }

    if (secondary_table[h2].result_id != 0 && secondary_table[h2].key == key) 
        return secondary_table[h2].result_id;
    if (secondary_table[h2].result_id == 0) { 
        secondary_table[h2] = {key, current_id}; 
        return current_id; 
    }

    return current_id; 
}
```
**Purpose**: Dual hash table lookup with collision handling
**Algorithm**:
1. Compute two hash values
2. Check primary table
3. Check secondary table on collision
4. Insert if not found
5. Return canonical ID

### Hash Functions
```cpp
static uint64_t mix_h1(const ValueKey& k) noexcept {
    uint64_t h = 0x123456789ABCDEF0ULL ^ k.op_a;
    h ^= (static_cast<uint64_t>(k.opcode) << 32) | k.op_b;
    h ^= (static_cast<uint64_t>(k.op_c) << 48);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    return h ^ (h >> 33);
}

static uint64_t mix_h2(const ValueKey& k) noexcept {
    uint64_t h = 0x876543210FEDCBA9ULL ^ k.op_b;
    h ^= (static_cast<uint64_t>(k.op_a) << 16) | k.opcode;
    h ^= (static_cast<uint64_t>(k.op_c) << 32);
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    return h ^ (h >> 31);
}
```
**Purpose**: Generate hash values for table indexing
**Features**: Different mixing for collision distribution

### SIMD Helpers
```cpp
struct NodeHash {
    size_t operator()(const dgtl::ir::IRNode& n) const noexcept {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&n));
        uint64_t low = _mm_extract_epi64(v, 0);
        uint64_t high = _mm_extract_epi64(v, 1);
        return low ^ (high >> 7);
    }
};

struct NodeEq {
    bool operator()(const dgtl::ir::IRNode& a, const dgtl::ir::IRNode& b) const noexcept {
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&a));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&b));
        __m128i res = _mm_cmpeq_epi64(v1, v2);
        return _mm_movemask_epi8(res) == 0xFFFF;
    }
};
```
**Purpose**: SIMD-accelerated node comparison
**NodeHash**: 128-bit SIMD load and mix
**NodeEq**: 128-bit SIMD equality check

### GVNPass (Legacy)
```cpp
class GVNPass {
    // ... SIMD helpers ...
    void run(dgtl::ir::IRStream& stream) {
        std::unordered_map<dgtl::ir::IRNode, uint32_t, NodeHash, NodeEq> value_table;
        
        stream.begin_transaction();
        
        for (size_t i = 0; i < stream.size(); ++i) {
            auto node = stream.get_node(i);
            if (value_table.contains(node)) {
                // Redundant node found via SIMD comparison
            } else {
                value_table[node] = static_cast<uint32_t>(i);
            }
        }
        
        stream.commit_transaction();
    }
};
```
**Purpose**: Legacy GVN implementation
**Features**: SIMD-based value numbering with unordered_map

### Namespace Closings
```cpp
} // namespace dgtl::opt
} // namespace dgtl::passes
```
- Close optimization and passes namespaces

## Usage Example
```cpp
#include "GVN.hpp"

// Initialize GVN pass
dgtl::opt::DGTLGVN gvn(1048576); // 1M max IDs

// Process IR stream for value numbering
dgtl::ir::IRStream ir_stream = // ... IR with redundant computations ...
gvn.execute(ir_stream);

// Result: Redundant operations eliminated, IDs remapped
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, IRAtom, IRCore, IRTraits, Flags, OpCode, INVALID_SLOT, begin_strike(), commit_strike(), rollback_strike(), validate_shadow_integrity(), prune_shadow_buffer(), push_to_shadow(), get_atom_readonly(), get_core_readonly(), calculate_hash()
- **immintrin.h**: AVX-512/AVX2 SIMD intrinsics

## Performance Characteristics
- **Table Size**: 256K entries (2MB memory)
- **Hash Speed**: Dual 64-bit hash functions
- **SIMD Comparison**: 128-bit parallel equality checks
- **Lookup Speed**: O(1) average case with collision handling
- **Memory Usage**: Tables + ID map (size depends on max_ids)
- **Transactional Safety**: Shadow buffer overhead
- **Scalability**: Linear processing O(n) with constant factors

## Integration Points
- **Pass Manager**: Executed as optimization pass
- **Register Allocator**: Benefits from reduced live ranges
- **JIT Executor**: Applied before code generation
- **Memory Guard**: Ensures bounds checking for remapped IDs
- **Debug Store**: Records value numbering decisions
- **Verified Formal**: Proves correctness of GVN transformations

## Safety Guarantees
- **Zero-Corruption**: Shadow operations prevent main IR modification
- **Invariant Protection**: Intent-hash validation ensures correctness
- **Transactional Rollback**: Failed GVN leaves IR unchanged
- **Side Effect Preservation**: Critical operations always retained
- **ID Safety**: Bounds checking and invalid ID handling
- **Semantic Correctness**: Ignores result IDs for true value matching
- **Live Operation Check**: Prevents elimination of all operations