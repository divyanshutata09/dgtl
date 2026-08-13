# DGTL Phase D: IR Expressiveness & Safety
## Variable-Arity Operations, Multi-Result Semantics, Mandatory Verifier
## Architecture Specification (2026-05-17)

---

## Problem Statement

**LLVM/MLIR advantages DGTL currently lacks**:
1. **Multi-result operations**: Some ops produce multiple values (e.g., tuple ops, multi-output builtins)
2. **Variable-arity operands**: Ops like PHI, SELECT, CALL need N operands, not fixed 3
3. **First-class variadic semantics**: Proper modeling of control joins, data multiplexing, etc.
4. **Mandatory effect tracking**: Memory effects, concurrency constraints at verifier level

**DGTL's strength to build on**:
- 32-byte IRNode ABI (keep it!)
- SoA stream architecture (extensible via sidecar tables)
- Transactional mutation with rollback
- Flat + SSA verifier stack

---

## Phase D Design: Sidecar Architecture

### Principle: Keep 32-byte Node, Add Overflow Tables

```
┌─────────────────────────────────────────────────────────┐
│ Fast Path (32-byte IRNode)                              │
│ ✓ Unary/binary/ternary ops                              │
│ ✓ 90% of real IR (no sidecar access)                    │
└─────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
    ┌────▼────┐  ┌──────▼──────┐  ┌────▼────┐
    │ Overflow│  │ Result Map  │  │ Type &  │
    │ Operands│  │ (multi-rslt)│  │ Effects │
    │ Table   │  │             │  │ Sidecar │
    └─────────┘  └─────────────┘  └─────────┘
         (IRStream internal maps)
```

### Key Invariant
- **IRNode.op_c encodes overflow hint** (VARIADIC flag or index)
- **IRStream maintains consistency** via sidecar tables
- **Non-variadic ops ignore sidecars** (zero overhead)
- **Verifier validates all tables** at stream boundaries

---

## Component 1: Opcode Categories

### New Flags in IR_Enums.hpp (OpCode Bits)

```cpp
enum class OpCode : uint16_t {
    // ... existing opcodes ...
    
    // Phase D: Multi-result & Variadic Markers (0x0001-0x0010 range in flags-ish encoding)
    // These are encoded in high bits or separate field if room permits
    
    PHI_MULTI = 0x0050,         // phi with variadic predecessors
    TUPLE_PACK = 0x0051,        // (v0, v1, ..., vN) → tuple_id
    TUPLE_UNPACK = 0x0052,      // tuple_id → (v0, v1, ...)
    SELECT_MULTI = 0x0053,      // select(pred_mask, values[], fallback)
    CALL_VARIADIC = 0x0054,     // call(func, args[], ...)
    
    // Control flow
    SWITCH_DISPATCH = 0x0055,   // switch(value, cases[])
    
    // Memory effects (explicit)
    VOLATILE_LOAD = 0x0056,     // load with volatile semantics
    VOLATILE_STORE = 0x0057,    // store with volatile semantics
};

// In Flags namespace:
namespace Flags {
    constexpr uint16_t IS_VARIADIC = 0x1000;      // op uses overflow operands
    constexpr uint16_t HAS_MULTI_RESULT = 0x2000; // op produces multiple results
    constexpr uint16_t EFFECT_MASK = 0x4000;      // op has tracked effects
};
```

---

## Component 2: IRStream Sidecar Tables

### Extension to IR_Stream.hpp

```cpp
class IRStream {
private:
    // Phase D: Overflow and multi-result tables
    std::unordered_map<size_t, std::vector<uint32_t>> operand_overflow_;
    std::unordered_map<size_t, std::vector<uint32_t>> result_ids_;        // node_idx → [rid0, rid1, ...]
    std::unordered_map<uint32_t, TypeHandle> result_types_;               // result_id → type
    std::unordered_map<size_t, EffectSet> node_effects_;                  // node_idx → effects
    
public:
    // Accessors
    std::vector<uint32_t> get_operands(size_t node_idx) const;
    void set_operands(size_t node_idx, std::span<const uint32_t> operands);
    
    std::vector<uint32_t> get_result_ids(size_t node_idx) const;
    void set_result_ids(size_t node_idx, std::span<const uint32_t> result_ids);
    
    TypeHandle get_result_type(uint32_t result_id) const;
    void set_result_type(uint32_t result_id, TypeHandle ty);
    
    EffectSet get_effects(size_t node_idx) const;
    void set_effects(size_t node_idx, EffectSet effects);
    
    // Transaction support for sidecars
    void sync_overflow_tables_on_commit();
    void invalidate_sidecars_on_abort();
};
```

---

## Component 3: Canonical Phi Semantics

### Old Phi (Fixed 2 operands)
```
PHI result_id pred0 pred1 block_label
```

### New Phi (Variadic, N predecessors)
```
Node Layout:
  opcode: PHI_MULTI
  op_a: predecessor_count (or 0xFFFFFFFF if overflow)
  op_b: block_label
  op_c: overflow_idx (if op_a == INVALID_SLOT)

Sidecar (operand_overflow_):
  operand_overflow_[node_idx] = [phi_value_0, phi_value_1, ..., phi_value_N]
  (values correspond to predecessors in reverse CFG order)
```

**Advantage**: Unlimited predecessors, canonical representation, verifier can check all operands are reachable.

---

## Component 4: Mandatory Verifier Passes

### New Verifiers (Called at Stream Boundaries)

#### a. Dominance Verifier
```cpp
class DominanceVerifier {
    // Build dominator tree
    // For each use: verify defining op dominates use
    // Catch: use-before-def in loops, unreachable code paths
};
```

#### b. Type Verifier
```cpp
class TypeVerifier {
    // For each op: check operand types match signature
    // Check result types are declared and consistent
    // Catch: type mismatches, missing type annotations
};
```

#### c. Effect Verifier
```cpp
class EffectVerifier {
    // Memory effects: no data races, valid ordering
    // Concurrency: proper synchronization before use
    // Catch: UB (use-after-free, data races, etc.)
};
```

#### d. SSA Verifier (Enhanced)
```cpp
class SSAVerifier {
    // Extended to handle multi-result ops
    // Check result_ids map is consistent
    // Catch: result ID conflicts, orphaned results
};
```

**Mandatory Gate**: All four must pass before IR is considered valid.

---

## Component 5: DSL Extensions

### New Attributes in phase1_core_ops.dgtl_def

```dsl
// Multi-result operations
op TuplePack: Expr
  = operands = variadic<Value>
  , results = [Type, Type, ...]   // declare tuple element types
  , traits = [IsTerminator]        // example
  , attributes = Pure

// Variadic operand op
op SelectMulti: Expr
  = operands = [Value, variadic<Value>]  // predicate + choices
  , result = Type
  , traits = [IsCommutative]  // if pred mask order doesn't matter

// Phi with N predecessors
op PhiMulti: Value
  = operands = variadic<Value>   // one per predecessor
  , results = [Type]
  , controlDependency = true
  , attributes = [KeepOrder]     // order must match CFG pred order
```

---

## Implementation Roadmap

### Phase D.1: Opcode & Sidecar Infrastructure
- [ ] Add Phase D opcodes to IR_Enums.hpp
- [ ] Add sidecar tables to IRStream (operand_overflow_, result_ids_, result_types_, node_effects_)
- [ ] Implement accessors (get_operands, set_operands, etc.)
- [ ] Update transaction management to sync sidecars on commit

### Phase D.2: Canonical Phi Semantics
- [ ] Retire old binary PHI representation
- [ ] Implement PhiMulti opcode with variadic overflow
- [ ] Update phi builders in generated wrappers
- [ ] Verify CFG construction uses new phi semantics

### Phase D.3: Mandatory Verifier Passes
- [ ] Implement DominanceVerifier (extend SSA verifier)
- [ ] Implement TypeVerifier (new)
- [ ] Implement EffectVerifier (new)
- [ ] Integrate all four as mandatory gate at stream boundaries
- [ ] Add detailed error messages for each verifier failure

### Phase D.4: DSL & Generator Support
- [ ] Extend DSL parser to recognize variadic operands, multi-result declarations
- [ ] Update dgtl_gen_main.cpp to emit wrappers for Phase D ops
- [ ] Generate verifier metadata from DSL declarations
- [ ] Generate trait inference logic (Pure, Commutative, etc.)

### Phase D.5: Build & Validation
- [ ] Full build of Phase D-aware compiler
- [ ] Conversion tests: old IR → new IR (backward compat)
- [ ] New IR generation tests for each Phase D opcode
- [ ] Verifier failure detection tests
- [ ] CFG + dominance correctness tests

---

## Comparison: LLVM/MLIR vs DGTL Phase D

| Dimension | LLVM | MLIR | DGTL Phase D |
|-----------|------|------|-------------|
| **Variadic Ops** | Tied to Region (heavyweight) | Variadic operands, but no sidecar | ✅ Sidecar-based (lightweight) |
| **Multi-result** | Manual result tracking | First-class (elegant) | ✅ Sidecar + canonical (explicit + fast) |
| **Type System** | Dynamic (runtime) | Parameterized (static) | ✅ Verifier-enforced () |
| **Effect Tracking** | Scattered (Attributes) | Structured (Effect ops) | ✅ Mandatory verifier pass |
| **Dominance** | Lazily computed (slow) | Not standard | ✅ Eager verification (safe) |
| **32-byte node** | N/A | N/A | ✅ Unique advantage |

---

## Performance & Safety Claims

### Performance
- **Fast path unchanged** for 90% of ops (no sidecar access)
- **Sidecar lookup O(1)** via unordered_map (hash table, not array search)
- **Lazy sidecar population** (only create entries when variadic)
- **Eager verification** catches bugs early (prevents downstream crashes)

### Safety
- **Dominance checks** prevent use-before-def
- **Type checks** prevent type confusion
- **Effect checks** prevent data races and UB
- **Verifier mandatory** (no opt-out, like LLVM's verify-each-pass)

---

## References

- Current IR Stream: d:\TataG\DGTL\include\dgtl\ir\base\IR_Stream.hpp
- IR Node: d:\TataG\DGTL\include\dgtl\ir\base\IR_Node.hpp
- Enums: d:\TataG\DGTL\include\dgtl\ir\base\IR_Enums.hpp
- SSA Verifier: d:\TataG\DGTL\include\dgtl\ir\base\IR_SSA.hpp
- DSL: d:\TataG\DGTL\include\dgtl\ir\gen\defs\phase1_core_ops.dgtl_def
