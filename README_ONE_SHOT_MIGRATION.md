# DGTL one-shot generator/MIR migration

This bundle is the coordinated migration produced from the supplied generator,
canonical IR DSL, all supplied MIR boundary files, and all supplied target ISel
files.

## Architectural result

```text
canonical IR DSL ───────┐
                        ├─> dgtl-isel-gen ─> neutral MachineForm contract
MachineForm contract ───┘                         │
                                                  ├─> target ISel modules
                                                  └─> target dispatch (outside MIR)

Machine_IR ──> neutral dgtl.mir.isel_gen only
Instruction_Selector / runtime policy ──> target dispatch boundary
Target lowering ──> (MachineForm, MachineSemanticKind) -> MachineOpcode
```

## Important changes

1. Removed the hard-coded canonical opcode registry from `dgtl_isel_gen_main.cpp`.
2. Canonical opcodes and aliases are read from `defs/ir/phase1_core_ops.dgtl_def`.
3. Added `defs/isel/machine_forms.dgtl_contract` as the sole MachineForm shape ABI.
4. Added strict 183/183 opcode coverage; unhandled operations must be explicitly declared.
5. Removed generator-side opcode-name heuristics for Tensor/Atomic/Concurrency forms.
6. Made those previously inferred forms explicit in `common_runtime_forms.dgtl_isel`.
7. Alias spellings are canonicalized before C++ emission, so aliases do not require enum members.
8. Fingerprints now include the canonical IR schema, MachineForm contract, target DSL, target aliases/capabilities, and generator contract version.
9. `dgtl.mir.isel_gen` is now neutral and contains no target ISA imports.
10. Target dispatch moved to `dgtl.isa.isel_dispatch`.
11. Common MIR generated bridges no longer import all eight ISA modules.
12. Legacy `dgtl.mir.abi_lowering` is no longer exported by `dgtl.mir`; target ABI interfaces are the active backend ABI.
13. Generation is deterministic; two full generations produced byte-identical output in validation.
14. The validator now checks canonical coverage and the MIR/ISA dependency boundary.

## Verification performed

- `dgtl_isel_gen_main.cpp`: C++20 syntax check passed.
- `dgtl_main.cpp`: C++20 syntax check passed.
- `validate_dgtl_gen_main.cpp`: C++20 syntax check passed.
- Full generator run: passed.
- Canonical opcode coverage: 183/183.
- MachineForm contract entries: 25.
- Generated HIR metadata rows: 183.
- Generated HIR wrappers: 183.
- Deterministic second generation: 0 file differences.
- Common MIR direct imports of target ISA modules: 0.

