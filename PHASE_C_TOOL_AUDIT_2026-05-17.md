# Phase C Auxiliary Flags Audit: DGTL Tool Files (2026-05-17)

**Scope**: Audit four DGTL tool files for Phase C aux_flags awareness and semantic preservation.
**Date**: 2026-05-17
**Status**: CRITICAL GAPS IDENTIFIED - All files require updates for full Phase C support

---

## Executive Summary

**Finding**: All four tool files have **critical gaps** in Phase C aux_flags support:
- **dgtl_opt_main.cpp**: Does NOT read/write/preserve aux_flags (data loss during parse/serialize)
- **dgtl_llc_main.cpp**: Does NOT access aux_flags or handle address-space semantics
- **dgtl_verify_main.cpp**: Does NOT validate Phase C constraints
- **dgtl_isel_gen_main.cpp**: Not relevant (DSL code generator, no IR manipulation)

**Impact**: IR transformations lose Phase C semantic information; lowering cannot respect address-space constraints; verification cannot validate invariants.

---

## File-by-File Analysis

### 1. D:\TataG\DGTL\tools\cpp\dgtl_isel_gen_main.cpp

**Status**: ✅ NOT AFFECTED (Not relevant to Phase C)

**Analysis**:
- Purpose: Parse ISel DSL rules file (`x64_core.dgtl_isel`) and emit C++ header array
- Scope: Code generation only; does not read, manipulate, or write IR
- Lines 1-115: Simple DSL parser → string array emitter
- No IR nodes, no aux_flags, no address-space handling needed

**Recommendation**: No changes required. This tool is outside the IR processing pipeline.

---

### 2. D:\TataG\DGTL\src\apps\dgtl_opt_main.cpp

**Status**: 🔴 CRITICAL - aux_flags completely missing from parse/serialize

#### Issue #1: `parse_ir_text()` does NOT read aux_flags
**Location**: [lines 180-268](D:\TataG\DGTL\src\apps\dgtl_opt_main.cpp#L180-L268)

**Current behavior**:
```cpp
// Line 182: Format documentation says "op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>"
// Line 218: uint32_t opcode_val = 0, result_id = 0, op_a = 0, op_b = 0, op_c = 0, flags = 0;
// Line 220: std::string opcode_str, result_str, op_a_str, op_b_str, op_c_str, flags_str;
// Line 221: Parse reads: opcode, result, op_a, op_b, op_c, flags (flags = logic_flags)
node.flags = static_cast<uint16_t>(flags & 0xFFFF);  // Line 257
stream.push_back(node);  // Line 259
```

**Problem**:
- Reads only `flags` (logic_flags) from text
- IRNode has no aux_flags field (only IRCore has aux_flags)
- After `stream.push_back(node)`, aux_flags SHOULD be computed by Phase C promotion
- **But**: The text format has no field for aux_flags, so if re-serialized and re-parsed, aux_flags are LOST

**Changes needed**:
1. Add aux_flags field to IR text format (7th column)
2. Update parse_ir_text() to read aux_flags from text
3. Update serialize_ir_text() to output aux_flags from IRCore
4. Add Phase C constraint validation during parse

#### Issue #2: `serialize_ir_text()` does NOT output aux_flags
**Location**: [lines 280-330](D:\TataG\DGTL\src\apps\dgtl_opt_main.cpp#L280-L330)

**Current behavior**:
```cpp
// Line 281: Output format comment: "op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>"
// Line 309: oss << "op ";
//         oss << opcode << result_id << op_a << op_b << op_c << flags;
// Line 320: Does NOT output aux_flags
```

**Problem**:
- Outputs only node fields and logic_flags
- Ignores IRCore.aux_flags which contains Phase C semantic information
- Round-trip (parse → passes → serialize) LOSES Phase C information

**Changes needed**:
1. Access IRCore via `stream.get_core_readonly(i)` to get aux_flags
2. Output aux_flags as 8th column in IR text format
3. Add format comment documenting aux_flags semantics

#### Issue #3: No Phase C constraint validation during parsing
**Location**: [lines 180-268](D:\TataG\DGTL\src\apps\dgtl_opt_main.cpp#L180-L268)

**Problem**:
- Parser accepts any op_c value without validation
- Does not check Phase C contracts (e.g., ATOMIC_LOAD must have op_c != INVALID_SLOT)
- Does not check IS_PREDICATED flag for BOOL_SELECT operations
- Silent corruption possible if manually created test files violate invariants

**Changes needed**:
1. After creating IRNode, validate Phase C contracts before push_back()
2. Check: if opcode is ATOMIC_LOAD/STORE/PTR_CAST/PTR_DIFF, then op_c must not be INVALID_SLOT
3. Check: if opcode is BOOL_SELECT and flags & IS_PREDICATED, then aux_flags should have PREDICATE_CANONICAL

### 3. D:\TataG\DGTL\src\apps\dgtl_llc_main.cpp

**Status**: 🔴 CRITICAL - No aux_flags access; address-space semantics ignored

#### Issue #1: `lower_ir_to_asm()` does NOT access aux_flags or address-space info
**Location**: [lines 197-336](D:\TataG\DGTL\src\apps\dgtl_llc_main.cpp#L197-L336)

**Current behavior**:
```cpp
// Line 248-249: 
for (size_t i = 0; i < stream.size(); ++i) {
    const auto node = stream.get_node(i);  // Gets IRNode, NOT IRCore
    oss << "  ; [" << i << "] IR node: opcode=0x" << ... << node.opcode << "\n";
    oss << "  ; result=" << node.result_id << " op_a=" << node.op_a << " op_b=" 
        << node.op_b << "\n";  // Does NOT access op_c or aux_flags
}
```

**Problem**:
- Uses `stream.get_node(i)` (returns IRNode) instead of `stream.get_core_readonly(i)` (returns IRCore)
- Cannot access aux_flags, which contains address-space and predicate semantics
- ATOMIC_LOAD/STORE operations use generic address-space by default (op_c=0)
- Cannot generate correct memory barriers or address-space-specific load/store instructions
- Pointer operations lose semantic information needed for correct ISA emission

**Changes needed**:
1. Use `stream.get_core_readonly(i)` to access IRCore with aux_flags
2. For ATOMIC_LOAD/STORE/PTR_CAST/PTR_DIFF: extract address-space from aux_flags
3. Emit address-space-specific memory barriers (seq_cst for generic, rel-acq for other spaces)
4. For BOOL_SELECT with PREDICATE_CANONICAL: use predicate-guarded emissions

#### Issue #2: Legalization ignores address-space constraints
**Location**: [lines 222-237](D:\TataG\DGTL\src\apps\dgtl_llc_main.cpp#L222-L237)

**Current behavior**:
```cpp
// MachineLegalizer doesn't receive address-space information
dgtl::isa::lowering::MachineLegalizer legalizer;
auto legalized = legalizer.legalize(stream, target);
stream = std::move(legalized.stream);
```

**Problem**:
- MachineLegalizer receives only IRStream (no aux_flags context available upstream if not propagated)
- If address-space semantics aren't explicitly preserved during legalization, they are LOST
- Non-generic address spaces may have different legal forms (e.g., local memory may prohibit atomics)
- Verification would fail if verifier later checks for matching aux_flags

**Changes needed**:
1. Verify MachineLegalizer preserves aux_flags in legalized output
2. Add validation after legalization to confirm aux_flags match input

#### Issue #3: No predicate semantic handling
**Location**: [lines 248-336](D:\TataG\DGTL\src\apps\dgtl_llc_main.cpp#L248-L336)

**Problem**:
- Code emission treats all operations uniformly
- BOOL_SELECT with PREDICATE_CANONICAL should emit as predicated select (masking)
- BOOL_SELECT without PREDICATE_CANONICAL should emit as conditional branch sequence
- Current code has no PREDICATE_CANONICAL detection

**Changes needed**:
1. Check aux_flags & PREDICATE_CANONICAL for BOOL_SELECT operations
2. Emit predicated select vs. branch based on predicate bit
3. Validate that all BOOL_SELECT operations have PREDICATE_CANONICAL set (or raise error)

---

### 4. D:\TataG\DGTL\src\apps\dgtl_verify_main.cpp

**Status**: 🔴 CRITICAL - No Phase C constraint validation

#### Issue #1: No aux_flags equivalence checking
**Location**: [main() at lines 278-340+](D:\TataG\DGTL\src\apps\dgtl_verify_main.cpp#L278)

**Current behavior**:
```cpp
// Creates two empty IRStreams
dgtl::ir::IRStream original_ir(arena, 256);
dgtl::ir::IRStream optimized_ir(arena, 256);
// Calls verification without checking aux_flags preservation
auto result = verifier.verify_pass_equivalence(original_ir, optimized_ir, 
                                               opts.pass_name, map_level(opts.level));
```

**Problem**:
- IRStreams created but not populated (placeholder implementation)
- No verification that aux_flags are IDENTICAL between original and optimized IR
- Optimization passes could lose Phase C semantic information silently
- Formal verifier would NOT catch aux_flags drift

**Changes needed**:
1. After pass equivalence verification succeeds, perform aux_flags check
2. For each node index: `original_ir.get_core(i).aux_flags == optimized_ir.get_core(i).aux_flags`
3. If aux_flags diverge, report "Semantic drift: aux_flags modified by pass" and fail verification
4. Document that aux_flags are invariant across valid transformations

#### Issue #2: No Phase C constraint validation in loaded IR
**Location**: [main() at lines 278-340+](D:\TataG\DGTL\src\apps\dgtl_verify_main.cpp#L278)

**Problem**:
- IR files are loaded/parsed (via read_file) but NOT validated against Phase C contracts
- Verifier could accept invalid IR (e.g., ATOMIC_LOAD with op_c=INVALID_SLOT)
- Silent incorrectness: verification report would be meaningless for invalid input

**Changes needed**:
1. After parsing both IR streams, call Phase C validation
2. Check all ATOMIC_LOAD/STORE/PTR_CAST/PTR_DIFF for valid op_c
3. Check all BOOL_SELECT for IS_PREDICATED flag
4. Report validation failures before attempting equivalence check

#### Issue #3: Incomplete implementation (placeholder)
**Location**: [main() at lines 278-340+](D:\TataG\DGTL\src\apps\dgtl_verify_main.cpp#L278-L340)

**Problem**:
- IR stream creation shown but no actual parsing from file (input_text and opt_text are read but not used)
- Formal verification framework called but without loaded IR data
- Result interpretation present but verification loop incomplete

**Changes needed**:
1. Implement proper IR parsing from text format (use same parse_ir_text as dgtl_opt)
2. Load both original and optimized IR from provided files
3. Validate Phase C contracts before formal verification

---

## Summary of Changes Required

### Priority 1: CRITICAL (Break Phase C guarantees)

| File | Issue | Fix |
|------|-------|-----|
| dgtl_opt_main.cpp | aux_flags not in IR text format | Add 8th column for aux_flags in parse/serialize |
| dgtl_opt_main.cpp | No Phase C validation on parse | Validate op_c and IS_PREDICATED during parse |
| dgtl_llc_main.cpp | Legalization/codegen ignores aux_flags | Use get_core() to access aux_flags; emit address-space semantics |
| dgtl_verify_main.cpp | No aux_flags equivalence check | Verify aux_flags identical between original/optimized |

### Priority 2: IMPORTANT (Incomplete verification)

| File | Issue | Fix |
|------|-------|-----|
| dgtl_llc_main.cpp | BOOL_SELECT predicate semantics ignored | Check PREDICATE_CANONICAL; emit predicates vs. branches |
| dgtl_verify_main.cpp | Phase C constraints not validated in input | Add Phase C validation pass after IR parse |
| dgtl_verify_main.cpp | Placeholder implementation | Implement actual IR loading and verification |

### Priority 3: INFORMATIONAL

| File | Note |
|------|------|
| dgtl_isel_gen_main.cpp | No changes required (DSL code generator) |

---

## Implementation Checklist

### dgtl_opt_main.cpp

- [ ] Update IR text format comments (line 182, 281): Document aux_flags as column 8
- [ ] Modify parse_ir_text():
  - [ ] Add aux_flags parameter read (line 218-234)
  - [ ] Add Phase C validation: verify op_c for pointer ops, IS_PREDICATED for BOOL_SELECT (after line 250)
  - [ ] Store aux_flags separately (pre-compute or expect from text)
- [ ] Modify serialize_ir_text():
  - [ ] Access IRCore via get_core_readonly() (line 308)
  - [ ] Output aux_flags field (after op_c output, line 320)

### dgtl_llc_main.cpp

- [ ] Update lower_ir_to_asm():
  - [ ] Change `stream.get_node(i)` to store both node and core (line 248-249)
  - [ ] For ATOMIC_LOAD/STORE/PTR_CAST/PTR_DIFF: extract op_c from core (bit calculation from aux_flags)
  - [ ] Emit memory barriers based on address-space (seq_cst vs. rel-acq)
  - [ ] For BOOL_SELECT: check PREDICATE_CANONICAL in aux_flags; emit predicate vs. branch
- [ ] Verify MachineLegalizer preserves aux_flags (after line 237)

### dgtl_verify_main.cpp

- [ ] Implement IR parsing:
  - [ ] Call parse_ir_text() for both original_ir and optimized_ir (replace placeholder lines 281-282)
  - [ ] Add Phase C validation after parse
- [ ] Add aux_flags equivalence check:
  - [ ] After verify_pass_equivalence() succeeds (after line 307)
  - [ ] Loop through all nodes: compare get_core(i).aux_flags
  - [ ] Report divergence as "Semantic drift"
- [ ] Add Phase C constraint validation (new function):
  - [ ] For all ATOMIC_LOAD/STORE/PTR_CAST/PTR_DIFF: verify op_c != INVALID_SLOT
  - [ ] For all BOOL_SELECT with IS_PREDICATED: verify PREDICATE_CANONICAL

---

## References

- **Phase C Promotion System**: `/memories/repo/phase-c-aux-flags-promotion-2026-05-17.md`
- **IRNode Constraint**: `/memories/repo/phasec-irnode-auxflags-constraint-2026-05-17.md`
- **IR_Node.hpp**: aux_flags field in IRCore struct (line 33)
- **IR_Stream.hpp**: get_core() method (line 262-263)
- **IR_Verifier.hpp**: Phase C constraint checks
- **IR_Enums.hpp**: AuxFlags enum definitions (lines 352-378)

---

## Risk Assessment

**If NOT fixed**:
1. ❌ IR text files lose Phase C information on round-trip (opt parse → serialize)
2. ❌ Lowering ignores address-space constraints → wrong memory barriers
3. ❌ Verification cannot detect Phase C semantic drift
4. ❌ Formal verification reports misleading results (testing invalid IR)

**If fixed**:
1. ✅ Full aux_flags propagation through IR text format
2. ✅ Address-space semantics preserved through lowering pipeline
3. ✅ Phase C invariants validated at all tool boundaries
4. ✅ Verification robust to aux_flags modifications

