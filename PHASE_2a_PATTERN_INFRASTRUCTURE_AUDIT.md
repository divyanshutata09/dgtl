# PHASE 2a: Pattern-Matching Infrastructure Audit

**Date**: May 14, 2026  
**Scope**: DGTL codebase pattern-matching and instruction selection infrastructure  
**Goal**: Inventory existing capabilities and identify gaps for Phase 2a pattern DSL integration

---

## PART 1: EXISTING PATTERN INFRASTRUCTURE

### 1.1 Core Pattern Files & Capabilities

#### [Op_Traits.hpp](dgtl/ir/interfaces/Op_Traits.hpp)
**Location**: `include/dgtl/ir/interfaces/Op_Traits.hpp`  
**Purpose**: Operation metadata and trait dispatch system  
**Key Components**:
- `OpTrait` enum: `IsCommutative`, `HasNoSideEffects`, `IsTerminator`, `IsMemoryRead`, `IsMemoryWrite`
- `OpInterface` enum: `Arithmetic`, `Memory`, `ControlFlow`, `Terminator`
- `OpMetadata` struct: opcode, trait_bits, interface_bits, operand_count, has_result
- `get_metadata(OpCode)` → queries generated `DGTL_Gen_Ops.hpp` or external dialect registry
- `has_trait(OpCode, OpTrait)` → bitwise trait query

**Capability**: ✅ Trait-based operation filtering  
**Example Usage**: Passes can select ops via `has_trait(opcode, OpTrait::IsMemoryWrite)` instead of hardcoded checks

---

#### [Pattern_Rewriter.hpp](dgtl/ir/rewrite/Pattern_Rewriter.hpp)
**Location**: `include/dgtl/ir/rewrite/Pattern_Rewriter.hpp`  
**Purpose**: Transactional IR mutation framework (AFTER pattern matching)  
**Key Components**:
- `MutationKind` enum: `Replace`, `Erase`
- `MutationRecord`: before/after node snapshots + index tracking
- `PatternRewriter` class:
  - `begin()` / `commit()` / `rollback()` transactional API
  - `replace_node(idx, replacement)` → shadow write + mutation record
  - `erase_node(idx)` → tombstone + mark as erase
  - `validate_shadow_integrity()` before commit

**Capability**: ✅ Transactional rewriting of matched patterns  
**Limitation**: ⚠️ Does NOT perform pattern matching; only mutates after a match is found

---

#### [SimdGatherScatter_Legal_Vector_Recognizer.hpp](dgtl/pass/simd/SimdGatherScatter_Legal_Vector_Recognizer.hpp)
**Location**: `include/dgtl/pass/simd/SimdGatherScatter_Legal_Vector_Recognizer.hpp`  
**Purpose**: SIMD pattern recognition and legalization  
**Key Components**:
- Pattern Recognition (Section 4):
  - `DGTLAdvancedSIMDRecognizer` class (extends `DGTLOptimizationPass`)
  - `VectorPattern` enum: 8-16 different patterns (mixed-precision, partial loads/stores, etc.)
  - Sliding-window 4-node pattern matcher for O(n) detection
  - Methods: `try_match_f16_f32_blend()`, `try_match_partial_load()`, `try_match_partial_store()`
  
- Pattern data structures:
  - `struct VectorOpportunity`: pattern type + consumer index + cost model
  - Lookahead window scanning (typically 4-8 nodes forward)

**Capability**: ✅ Multi-node SIMD pattern detection with cost modeling  
**Scope**: Limited to SIMD patterns; hardcoded for specific vector operations

---

#### [Transform_Scalar_Core.hpp](dgtl/pass/transform/Transform_Scalar_Core.hpp) — Lines 698+
**Location**: `include/dgtl/pass/transform/Transform_Scalar_Core.hpp`  
**Purpose**: Scalar instruction fusion (FMA, shift+logic)  
**Key Components**:
- `struct FusionMatch`: consumer_idx, fused_op, operand IDs, result_id
- Methods:
  - `try_synthesize_fma(stream, p_idx)` → detects `Mul + Add` → FMA
  - `try_synthesize_shifted_logic(stream, p_idx)` → detects `Shift + Logic` → fused op
  - `apply_fusion(stream, p_idx, match)` → creates fused instruction in shadow
  
- Sliding-window lookahead: typically 8 instructions ahead
- Barrier awareness: skips side-effecting ops

**Capability**: ✅ Two-node fusion pattern detection  
**Limitation**: 🔴 Hardcoded patterns; no DSL; manual lookahead logic

---

#### [Transform_Control_Predication.hpp](dgtl/pass/transform/Transform_Control_Predication.hpp)
**Location**: `include/dgtl/pass/transform/Transform_Control_Predication.hpp`  
**Purpose**: Control flow predication (branch elimination)  
**Key Components**:
- "PATTERN RECOGNITION" section (lines 38-80)
  - Scans for branch + arithmetic payload patterns
  - Trait-based filtering: `has_trait(OpCode, OpTrait::IsTerminator)`
  - Two-phase approach:
    1. Scan for branch patterns with arithmetic payloads
    2. Pattern matching and transformation

**Capability**: ✅ Control-flow pattern detection with trait dispatch  
**Limitation**: ⚠️ Limited to branch patterns; manual tree walking

---

### 1.2 Pattern-Related Utilities

#### [LUT_FSM_Matcher.hpp](dgtl/utils/automata/LUT_FSM_Matcher.hpp)
**Location**: `include/dgtl/utils/automata/LUT_FSM_Matcher.hpp`  
**Purpose**: Finite-state machine for text/opcode tokenization  
**Key Components**:
- `LUTFsmMatcher` class: character classification via lookup table
- Built-in matchers: digits, hex, whitespace, operators
- Compile-time character class LUT (`kMatcherLut`)

**Capability**: ✅ Low-level string/token pattern matching  
**Scope**: For lexing/parsing; NOT for IR patterns

---

#### [Instruction_Fusion_Synth.hpp](dgtl/build/DGTL_SDK/include/dgtl/pass/transform/Instruction_Fusion_Synth.hpp)
**Location**: `include/dgtl/pass/transform/Instruction_Fusion_Synth.hpp`  
**Purpose**: Centralized instruction fusion orchestrator  
**Key Components**:
- `DGTLInstructionFusionSynth` class: combines FMA + shift+logic patterns
- "PATTERN 1: FMA" & "PATTERN 2: SHIFT+LOGIC" sections
- Transactional shadow-state mutations (via `PatternRewriter`)
- Sentinel integrity check before commit

**Capability**: ✅ Multi-pattern fusion synthesis  
**Implementation**: 🔴 Hardcoded pattern dispatch; no registry

---

## PART 2: PATTERN INFRASTRUCTURE GAP ANALYSIS

### 2.1 Missing Components for Phase 2a

| Feature | Exists? | Location | Gap |
|---------|---------|----------|-----|
| **Trait System** | ✅ Yes | `Op_Traits.hpp` | Mature; minimal additions needed |
| **Pattern DSL (.dgtl_isel)** | ❌ No | - | **Critical Gap**: No declarative syntax for rules |
| **Pattern Registry** | ❌ No | - | **Critical Gap**: Patterns hardcoded; no central registry |
| **ISLE-like Rewriter** | ❌ No | - | **Critical Gap**: No rule-based IR transformation engine |
| **Pattern Matcher** | 🟡 Partial | Various passes | Fragmented: no unified API |
| **Legalization Framework** | 🟡 Partial | SIMD recognizer | Limited to SIMD; no type lowering |
| **Cost Model Integration** | ✅ Yes | `AI_Cost_Model.hpp` | Used by SIMD; reusable |
| **Transactional IR Mutations** | ✅ Yes | `Pattern_Rewriter.hpp` | Mature; ready to integrate |
| **Pass Integration** | ✅ Yes | `Pass_Manager.hpp` | Mature; supports custom passes |

---

### 2.2 Critical Missing Pieces (Blocking Phase 2a)

#### **Gap 1: No Pattern DSL (.dgtl_isel)**
**Impact**: Phase 2a cannot replace X64_Strike_Emitter with declarative rules  
**Current State**: Patterns written directly in C++:
```cpp
// CURRENT: Manual C++
std::optional<FusionMatch> try_synthesize_fma(const ir::IRStream& s, uint32_t p_idx) {
    // ... 50+ lines of explicit pattern matching
    if (is_mul(producer.opcode) && is_add(consumer.opcode) && ...) {
        return FusionMatch{ /* ... */ };
    }
}
```

**Required for Phase 2a**:
```
# DESIRED: .dgtl_isel rule (declarative)
rule "fma_synthesis" {
  pattern: Add(Mul(?a, ?b), ?c)
  action: FusedOp(FMA, $a, $b, $c)
  cost: 1
  constraints: no_multiple_uses($a), $b is_commutative
}
```

---

#### **Gap 2: No Pattern Registry**
**Impact**: Cannot load/unload patterns at compile-time; scales poorly  
**Current State**: Each pass hardcodes its patterns:
- `Transform_Scalar_Core.hpp` → FMA + shift+logic (2 patterns)
- `SimdGatherScatter_Legal_Vector_Recognizer.hpp` → 8-16 patterns
- `Transform_Control_Predication.hpp` → branch patterns
- `Instruction_Fusion_Synth.hpp` → combines above

**Problem**: Adding new pattern requires editing C++ header; no runtime loading

**Required for Phase 2a**: Centralized registry:
```cpp
// Desired: Unified pattern registry
class PatternRegistry {
    void register_rule(const PatternRule& rule);
    std::vector<PatternMatch> match_all(const ir::IRStream&, uint32_t idx);
};
```

---

#### **Gap 3: No ISLE-like Rewriter Engine**
**Impact**: Cannot compose patterns or prioritize matches; brittle  
**Current State**: Each pattern matcher returns `std::optional<FusionMatch>`; no composition  
**Desired for Phase 2a**:
- Rule priority system (higher precedence rules tried first)
- Pattern combinators: `Or(pattern1, pattern2)`, `And(pattern1, pattern2)`
- Rewrite action language (not just IR replacement)

---

#### **Gap 4: Legalization Framework Incomplete**
**Impact**: Cannot lower complex types (128-bit → two 64-bit) declaratively  
**Current State**: SIMD recognizer has partial type checking; no systematic lowering pipeline  
**Desired**: Type legalization rules (e.g., `I128 → I64 + I64`)

---

### 2.3 Existing Patterns Not Yet Centralized

| Pattern | Location | Type | Complexity |
|---------|----------|------|-------------|
| **FMA (Mul + Add)** | `Transform_Scalar_Core.hpp` | 2-node fusion | Low |
| **Shift + Logic** | `Transform_Scalar_Core.hpp` | 2-node fusion | Low |
| **SIMD Gather/Scatter** | `SimdGatherScatter_Legal_Vector_Recognizer.hpp` | 4+ node | Medium |
| **Mixed-precision blending** | `SimdGatherScatter_Legal_Vector_Recognizer.hpp` | Type conversion | Medium |
| **Branch predication** | `Transform_Control_Predication.hpp` | Control flow | High |
| **Type upconversion chain** | `SimdGatherScatter_Legal_Vector_Recognizer.hpp` | Multi-step | Medium |
| **Tensor fusion** | (docs reference) | Domain-specific | High |

---

## PART 3: RECOMMENDATIONS FOR PHASE 2a

### 3.1 Incremental Integration Path

**Step 1: Build Pattern Registry** (Week 1)
- Create `dgtl/ir/pattern/Pattern_Registry.hpp`
- Migrate existing patterns → registry entries (no breaking changes)
- Keep C++ implementations; add registration layer

**Step 2: Create Pattern DSL Parser** (Weeks 2-3)
- Implement `.dgtl_isel` tokenizer + parser
- Generate pattern rule classes from DSL
- Integrate with registry

**Step 3: Implement Pattern Matcher** (Week 4)
- Centralized pattern matching algorithm (DAG matching, tree matching)
- Cost model integration
- Caching for repeated patterns

**Step 4: Compose into ISel** (Week 5)
- Implement instruction selector using registry
- Target-specific dispatch (X64, ARM, etc.)
- Replace X64_Strike_Emitter gradually

---

### 3.2 Concrete Next Steps (Phase 2a Deliverables)

**Deliverable 1: Pattern Registry & Centralized API**
```cpp
// dgtl/ir/pattern/Pattern_Registry.hpp (NEW)
class PatternRule {
    std::string name;
    std::function<std::optional<PatternMatch>(const ir::IRStream&, uint32_t)> matcher;
    uint32_t priority;
    std::string description;
};

class PatternRegistry {
public:
    void register_rule(const PatternRule& rule);
    std::vector<PatternMatch> match_all(const ir::IRStream&, uint32_t idx);
    const PatternRule* lookup_rule(std::string_view name) const;
};
```

**Deliverable 2: Formalize DSL Syntax**
- `.dgtl_isel` file format (ABNF grammar)
- Pattern syntax: `OpA(OpB(?x), ?y) where constraints`
- Action syntax: `FusedOp(...) : cost_model`

**Deliverable 3: Integrate with Pass Infrastructure**
- New pass type: `PatternMatcherPass` (extends `DGTLOptimizationPass`)
- Automatic pattern discovery from `.dgtl_isel` files
- Registry injection into pass context

---

## PART 4: FILE INVENTORY & STRUCTURE

### Directory Map (Pattern-Related Files)

```
DGTL/include/dgtl/
├── ir/
│   ├── interfaces/
│   │   └── Op_Traits.hpp                           ✅ Trait system
│   ├── rewrite/
│   │   └── Pattern_Rewriter.hpp                    ✅ Transactional mutations
│   ├── pattern/                                     ❌ MISSING (Phase 2a adds)
│   │   ├── Pattern_Registry.hpp                    (TO CREATE)
│   │   ├── Pattern_Matcher.hpp                     (TO CREATE)
│   │   └── Pattern_DSL.hpp                         (TO CREATE)
│   └── ...
├── pass/
│   ├── transform/
│   │   ├── Transform_Scalar_Core.hpp               ✅ FMA/shift patterns
│   │   ├── Instruction_Fusion_Synth.hpp            ✅ Fusion orchestration
│   │   └── Transform_Control_Predication.hpp       ✅ Branch patterns
│   ├── simd/
│   │   └── SimdGatherScatter_Legal_Vector_Recognizer.hpp  ✅ SIMD patterns
│   └── ...
├── utils/
│   └── automata/
│       └── LUT_FSM_Matcher.hpp                     ✅ (for tokenizing DSL)
└── ...
```

---

## PART 5: SUMMARY TABLE

### Pattern Infrastructure Status

| Component | Status | Maturity | Notes |
|-----------|--------|----------|-------|
| **Trait System** | ✅ Complete | Production | `Op_Traits.hpp`; ready to extend |
| **IR Mutation Layer** | ✅ Complete | Production | `Pattern_Rewriter.hpp`; transactional |
| **SIMD Pattern Recognizer** | ✅ Complete | Partial | Works for SIMD; not generalized |
| **Scalar Fusion Patterns** | ✅ Complete | Ad-hoc | `Transform_Scalar_Core.hpp`; manual C++ |
| **Control Flow Patterns** | ✅ Complete | Ad-hoc | `Transform_Control_Predication.hpp` |
| **Pattern DSL** | ❌ Missing | - | **PHASE 2a CRITICAL** |
| **Pattern Registry** | ❌ Missing | - | **PHASE 2a CRITICAL** |
| **ISLE-like Rewriter** | ❌ Missing | - | **PHASE 2a MILESTONE** |
| **Unified Matcher API** | ❌ Missing | - | **PHASE 2a FOUNDATION** |
| **Legalization Framework** | 🟡 Partial | Incomplete | SIMD-specific; needs generalization |

---

## PART 6: INTEGRATION CHECKLIST

- [ ] Inventory all existing patterns (DONE)
- [ ] Design pattern DSL syntax (PENDING)
- [ ] Create pattern registry interface (PENDING)
- [ ] Migrate FMA pattern → registry (PENDING)
- [ ] Migrate SIMD patterns → registry (PENDING)
- [ ] Implement `.dgtl_isel` parser (PENDING)
- [ ] Add pattern matching tests (PENDING)
- [ ] Update documentation (PENDING)
- [ ] Benchmark pattern matching overhead (PENDING)

---

## CONCLUSION

**Current State**: DGTL has **excellent building blocks** (traits, transactional mutations, partial pattern recognizers) but **lacks centralized pattern infrastructure**.

**Phase 2a Scope**: Build pattern registry + DSL → enable declarative instruction selection  
**Quick Win**: Centralize existing 15+ patterns into registry (2 weeks)  
**Full Payoff**: Replace X64_Strike_Emitter with ISLE-like rules (Phase 2b/2c)

**Key Success Metric**: "Add new instruction pattern without editing C++ header files"
