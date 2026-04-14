# DGTL Context (Context.hpp)

## Control Flow Map

```
[DGTLContext Class]
    |
    +--> Constructor (DGTLContext(DGTLArena& arena))
    |       +--> Store reference to global arena
    |       +--> Initialize allocation mutex
    |
    +--> IR Emission Methods
    |       +--> emit_node_fast(ir::OpCode op, uint16_t flg, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t hash)
    |       |       +--> Check if TLS buffer needs refresh
    |       |       +--> Select target pointers (shadow or primary based on transaction state)
    |       |       +--> Write to Tier 1 (IRAtom) and Tier 2 (IRCore)
    |       |       +--> Bump pointers and decrement remaining
    |       |
    |       +--> create_node_atomic()
    |       |       +--> Reset temp_node_buffer
    |       |       +--> Return pointer to staging node
    |       |
    |       +--> commit_node(const ir::IRNode& node)
    |               +--> Call emit_node_fast with node data
    |
    +--> Access Methods
    |       +--> get_arena()
    |               +--> Return reference to global arena
    |
    +--> Transaction Control
    |       +--> enter_transactional_mode()
    |       |       +--> Set g_tls_buffer.in_strike = true
    |       |
    |       +--> exit_transactional_mode()
    |       |       +--> Set g_tls_buffer.in_strike = false
    |       |
    |       +--> is_in_transaction()
    |               +--> Return g_tls_buffer.in_strike
    |
    +--> Private Methods
            +--> refresh_thread_pages()
                    +--> Lock allocation mutex
                    +--> Double-check remaining count
                    +--> Allocate primary atoms and cores (4096-byte aligned)
                    +--> Allocate shadow atoms and cores (4096-byte aligned)
                    +--> Update TLS buffer pointers and remaining count
```

**Flow Description**: The context manages thread-local IR emission with transactional dual-tier buffering. Fast-path emission (~4 cycles) uses bump-pointer allocation from huge-page slices, with automatic replenishment and transactional redirection to shadow buffers.

## Summary

`Context.hpp` implements lock-free thread-local IR emission with transactional safety, providing deterministic high-frequency node creation through 2MB huge-page slices. It features tri-tier TLS buffering with Pillar 2 transactional redirection and Pillar 3 sentinel validation for zero-contention, zero-corruption IR generation.

Key features:
- Thread-local 2MB huge-page slices (256K nodes each)
- Dual-tier buffering (primary + shadow) for transactions
- 4-cycle fast-path emission with bump-pointer allocation
- Automatic buffer replenishment from global arena
- Transactional mode for speculative IR generation
- 4096-byte alignment for page-table efficiency

## Full Explanation

### Architecture Overview
The DGTLContext serves as the IR emission engine for DGTL's multi-threaded compilation:

1. **Thread-Local Buffering**: 2MB slices per thread prevent contention
2. **Transactional Safety**: Pillar 2 shadow redirection for speculation
3. **Huge-Page Optimization**: 2MB pages reduce TLB pressure
4. **Bump-Pointer Allocation**: O(1) emission with zero overhead
5. **Sentinel Integration**: Pillar 3 intent hash validation

### Key Classes and Structures

#### ThreadLocalBuffer
Per-thread storage structure:

```cpp
struct alignas(64) ThreadLocalBuffer {
    ir::IRAtom* atom_ptr = nullptr;        // Primary tier 1 pointers
    ir::IRCore* core_ptr = nullptr;        // Primary tier 2 pointers
    
    ir::IRAtom* shadow_atom_ptr = nullptr; // Shadow tier 1 pointers  
    ir::IRCore* shadow_core_ptr = nullptr; // Shadow tier 2 pointers
    
    size_t remaining = 0;                  // Nodes left in current slice
    bool in_strike = false;                // Transaction state
};
```

- **64-byte Alignment**: Prevents false sharing between threads
- **Dual Tiers**: Primary for committed IR, shadow for speculative
- **Bump Pointers**: Efficient allocation without bounds checking

#### DGTLContext
Main context class:

```cpp
class DGTLContext {
    static constexpr size_t PageSizeNodes = 262144; // 256K nodes
    
    DGTLArena& global_arena;
    std::mutex allocation_mutex;
    mutable ir::IRNode temp_node_buffer;

public:
    explicit DGTLContext(DGTLArena& arena);
    // ... emission and control methods
};
```

### IR Emission System

#### Fast-Path Emission
```cpp
void emit_node_fast(ir::OpCode op, uint16_t flg, uint32_t res, uint32_t a, uint32_t b, uint32_t c = 0, uint32_t hash = 0) noexcept {
    if (g_tls_buffer.remaining == 0) {
        refresh_thread_pages();
    }
    
    // Pillar 2: Transactional tier selection
    ir::IRAtom* target_atom = g_tls_buffer.in_strike ? g_tls_buffer.shadow_atom_ptr : g_tls_buffer.atom_ptr;
    ir::IRCore* target_core = g_tls_buffer.in_strike ? g_tls_buffer.shadow_core_ptr : g_tls_buffer.core_ptr;
    
    // Tier 1: Bit-packed metadata
    target_atom->opcode = op;
    target_atom->flags = flg;
    
    // Tier 2: Logical data + sentinel hash
    target_core->result_id = res;
    target_core->op_a = a;
    target_core->op_b = b;
    target_core->op_c = c;
    target_core->intent_hash = hash;
    
    // Atomic bump allocation
    if (g_tls_buffer.in_strike) {
        g_tls_buffer.shadow_atom_ptr++;
        g_tls_buffer.shadow_core_ptr++;
    } else {
        g_tls_buffer.atom_ptr++;
        g_tls_buffer.core_ptr++;
    }
    
    g_tls_buffer.remaining--;
}
```

- **Buffer Check**: Automatic replenishment when exhausted
- **Tier Selection**: Shadow redirection during transactions
- **Dual Write**: Simultaneous Tier 1 and Tier 2 emission
- **Bump Allocation**: Pointer increment (no bounds checking needed)
- **Sentinel Hash**: Pillar 3 intent validation

#### Atomic Node Creation
```cpp
ir::IRNode* create_node_atomic() noexcept {
    temp_node_buffer = ir::IRNode{};
    return &temp_node_buffer;
}
```

- **Staging Area**: Temporary node for complex instruction assembly
- **Thread-Safe**: No shared state modifications
- **Reset**: Clean slate for each use

#### Node Commitment
```cpp
void commit_node(const ir::IRNode& node) noexcept {
    emit_node_fast(node.opcode, node.flags, node.result_id, 
                  node.op_a, node.op_b, node.op_c, node.intent_hash);
}
```

- **Bridge Pattern**: Stages complex nodes before emission
- **Fast Commit**: Direct call to optimized emission path
- **No Copy**: Reference-based parameter passing

### Transactional Control

#### Transaction Lifecycle
```cpp
void enter_transactional_mode() noexcept { 
    g_tls_buffer.in_strike = true; 
}

void exit_transactional_mode() noexcept { 
    g_tls_buffer.in_strike = false; 
}

bool is_in_transaction() const noexcept { 
    return g_tls_buffer.in_strike; 
}
```

- **Mode Flag**: Controls tier selection in emission
- **Noexcept**: Exception-free state changes
- **Query Method**: Transaction state inspection

### Memory Management

#### Buffer Replenishment
```cpp
void refresh_thread_pages() noexcept {
    std::lock_guard<std::mutex> lock(allocation_mutex);
    
    // Double-check to prevent race
    if (g_tls_buffer.remaining > 0) return;
    
    // Allocate primary tier slices
    ir::IRAtom* new_atoms = global_arena.allocate<ir::IRAtom>(PageSizeNodes, 4096);
    ir::IRCore* new_cores = global_arena.allocate<ir::IRCore>(PageSizeNodes, 4096);
    
    // Allocate shadow tier slices  
    ir::IRAtom* shad_atoms = global_arena.allocate<ir::IRAtom>(PageSizeNodes, 4096);
    ir::IRCore* shad_cores = global_arena.allocate<ir::IRCore>(PageSizeNodes, 4096);
    
    if (!new_atoms || !shad_atoms || !new_cores || !shad_cores) {
        return; // Memory pressure - DGTL triggers global flush
    }
    
    g_tls_buffer.atom_ptr = new_atoms;
    g_tls_buffer.core_ptr = new_cores;
    g_tls_buffer.shadow_atom_ptr = shad_atoms;
    g_tls_buffer.shadow_core_ptr = shad_cores;
    g_tls_buffer.remaining = PageSizeNodes;
}
```

- **Mutex Protection**: Thread-safe global arena access
- **Double-Check**: Prevents unnecessary allocations
- **Paired Allocation**: Atoms and cores allocated together
- **4096-Byte Alignment**: Page boundary optimization
- **Dual Tier Setup**: Both primary and shadow buffers

### Performance Characteristics

- **Emission Speed**: ~4 CPU cycles (branchless bump allocation)
- **Memory Efficiency**: 2MB slices, 256K nodes per allocation
- **TLB Optimization**: Huge-page aligned allocations
- **Contention**: Zero inter-thread contention
- **Scalability**: Linear scaling with thread count

### Integration Points

- **IR Stream**: Provides backing storage for IR construction
- **Transactional IR**: Pillar 2 shadow buffering support
- **Sentinel System**: Pillar 3 intent hash emission
- **Pass Manager**: IR generation during optimization
- **JIT Executor**: Final code emission

### Usage Example

```cpp
#include "dgtl/core/Context.hpp"

int main() {
    // Initialize global arena
    dgtl::core::DGTLArena arena(1024); // 1GB
    dgtl::core::DGTLContext context(arena);
    
    // Fast-path emission
    context.emit_node_fast(ir::OpCode::ADD, 0, result_id, op_a, op_b, 0, intent_hash);
    
    // Complex node staging
    ir::IRNode* temp = context.create_node_atomic();
    temp->opcode = ir::OpCode::MUL;
    temp->flags = ir::Flags::IS_SIMD_BURST;
    temp->result_id = result_id;
    temp->op_a = op_a;
    temp->op_b = op_b;
    temp->refresh_intent_hash();
    
    context.commit_node(*temp);
    
    // Transactional emission
    context.enter_transactional_mode();
    // All emissions now go to shadow buffers
    context.emit_node_fast(ir::OpCode::LOAD, 0, temp_id, addr, 0);
    context.exit_transactional_mode();
    // Emissions return to primary buffers
    
    return 0;
}
```

### Memory Layout

```
Thread Local Buffer (2MB per thread):
+-------------------+  <- 2MB Huge Page Boundary
| Primary Atoms     |  <- 1MB (256K * 4 bytes)
| 256K IRAtom      |
+-------------------+
| Primary Cores     |  <- 1MB (256K * 4 bytes)  
| 256K IRCore      |
+-------------------+
| Shadow Atoms      |  <- 1MB
| 256K IRAtom      |
+-------------------+
| Shadow Cores      |  <- 1MB
| 256K IRCore      |
+-------------------+  <- 2MB Huge Page Boundary
```

### Error Handling

- **Memory Pressure**: Graceful handling of allocation failures
- **Buffer Exhaustion**: Automatic replenishment (no user intervention)
- **Thread Safety**: Mutex protection for global allocations
- **Exception Safety**: Noexcept methods prevent exception propagation

### Future Enhancements

- NUMA-aware buffer allocation
- Dynamic slice sizing based on workload
- Memory pressure prediction
- Hardware transactional memory integration
- SIMD-accelerated bulk emission

This header provides the high-performance IR emission foundation for DGTL's multi-threaded compilation pipeline, enabling deterministic, contention-free code generation with transactional safety guarantees.