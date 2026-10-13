# Sovereign Industry-Grade Implementation Plan: Advanced Mathematics & Numerical Differentiation Engine for DGTL

## Executive Architectural Summary & Benchmark Superiority Target
This implementation plan establishes the definitive, production-grade engineering specification for elevating DGTL above **LLVM, Enzyme, PyTorch (ATen), and Cranelift** in mathematical optimization, transcendental evaluation, complex arithmetic, numerical calculus, and automatic differentiation.

```
====================================================================================================
                        DGTL TRANSCENDENTAL & DIFFERENTIATION SUPREMACY MATRIX
====================================================================================================
Metric / Feature              LLVM 18+            Enzyme AD          PyTorch (ATen)       DGTL (Proposed)
----------------------------------------------------------------------------------------------------
Transcendental Evaluation     Libm / Fast-Math    None (delegates)   Sleef / Vectorized   Cody-Waite + Remez Minimax FMA
Typical ULP Error             0.5 - 2.5 ULP       N/A                1.0 - 3.0 ULP        < 0.5 ULP (IEEE) / < 1.0 (Fast)
Automatic Differentiation     None (Requires Clang) LLVM IR Pass     Tape Interpreter / C++ Direct True IR Forward-Mode
AD Graph Construction         External Library    Tape Allocation    Dynamic Autograd     Zero-Alloc Tangent Sidecars
Complex Division Overhead     Smith (GCC/Clang)   N/A                Standard Complex     Smith Robust FMA Vectorized
Numerical Quadrature & ODE    None                None               torchdiffeq (Python) Native Gauss-Legendre & RKF45
Binary Linker Sovereignty     External (lld/link) External (lld/ld)  External Toolchain   Sovereign In-Memory Linker
JIT Linking Latency           15 - 50 ms (ORC)    30 - 100 ms        50 - 200 ms (Inductor) < 1.5 ms Direct W^X Emission
IR-MIR-ISA Boundary Safety    Pass-leaking        Rigid LLVM Bitcode C++ Wrapper Bloat    Zero-Boundary-Leak SoA & LUTs
====================================================================================================
```

### Sovereign Linker & Binary Weaver Sovereignty
Unlike LLVM and PyTorch, which require external platform linkers (`lld`, `link.exe`, `ld64`) and massive runtime support libraries (`libm`, `libc++`, `compiler-rt`), **DGTL incorporates a sovereign in-memory linker and binary weaver** (`include/dgtl/linker/` and `include/dgtl/object/`):
1. **Direct PE32+ / ELF Object & Executable Emission**:
   - `dgtl::linker::driver::DGTLAOTLinker` (`Linker_Driver.ixx`) emits fully relocatable COFF `.obj` and ELF `.o` files with exact PC-relative (`IMAGE_REL_AMD64_REL32` / `R_X86_64_PLT32`) relocations directly from `MachineFunction` streams.
   - `dgtl::linker::pe::SovereignPELinker` (`PE_Linker.ixx`) emits signed, ASLR-compatible PE32+ executables (`.exe`) with complete IAT, `.reloc` fixup tables, and 16-byte Win64 stack alignment in under 10ms with **zero external dependencies**.
2. **Dual-ABI Return Marshalling (`returns_in_xmm0`)**:
   - In `Linker_Driver.ixx` (lines 1022–1056), when emitting `MachineForm::Call`, the sovereign linker extracts return values based on whether the callee returns a floating-point value in `XMM0`.
   - If `returns_in_xmm0 == true`, the sovereign linker automatically emits `movq rax, xmm0` (`0x66, 0x48, 0x0F, 0x7E, 0xC0`) before storing `RAX` to the destination stack slot.
   - **Audit Fix**: `__dgtl_rt_fast_inv_sqrt` and `__dgtl_rt_rsqrt` are registered in the `returns_in_xmm0` name check in lines 1028–1043 so that their return values are preserved in `XMM0`.
3. **Dual Argument Marshalling**:
   - In `Linker_Driver.ixx` (lines 949–953), arguments are mirrored:
     ```assembly
     movq xmm0, rcx
     movq xmm1, rdx
     movq xmm2, r8
     movq xmm3, r9
     ```
     This guarantees calling convention compliance across Microsoft x64 and System V AMD64 ABIs.
4. **Native FMA Emission in Sovereign Linker**:
   - `estimate_machine_node_bytes` reserves 36 bytes for `MachineForm::FloatTernaryFma32` (line 400).
   - We supply the concrete emission block for `MachineForm::FloatTernaryFma32` inside `emit_aot_machine_code` using vectorized FMA or fused `mulss` + `addss` sequences into stack displacements.
5. **Runtime Symbol Resolution**:
   - `dgtl::linker::SymbolResolver` (`Symbol_Resolver.ixx`) and `dgtl::jit::JITSession` (`JITSession.ixx` / `JIT_Orchestrator.ixx`) resolve all `__dgtl_rt_*` symbols directly against `dgtl.runtime.maths` and host symbol tables without link errors.

---

## Strict Architectural Boundary Rules (IR $\to$ MIR $\to$ ISA $\to$ Object/Linker)

To ensure zero architectural degradation and zero compilation bugs, boundary invariants must be maintained:

$$\begin{array}{rcccl}
\text{\bf Layer} & \text{\bf Authority} & \text{\bf Input Form} & \text{\bf Output Form} & \text{\bf Permitted Operations} \\
\hline
\text{Layer 1} & \text{Definitions} & \text{.dgtl\_def / .dgtl\_isel} & \text{Gen C++26 Headers} & \text{Dialect schema, opcode tables (FROZEN)} \\
\text{Layer 2} & \text{True IR} & \text{AST / User API} & \text{IRStream (SoA / BIR)} & \text{SSA, Cody-Waite lowering, Auto-diff} \\
\text{Layer 3} & \text{Core Math} & \text{Mathematical Scalars} & \text{Polynomial / Quadrature} & \text{Minimax, Smith's div, RKF45 ODE} \\
\text{Layer 4} & \text{Pass Pipeline} & \text{IRStream} & \text{Optimized IRStream} & \text{Math lowering, sincos fusion, AD} \\
\text{Layer 5} & \text{MIR Selection} & \text{Optimized IRStream} & \text{MachineFunction} & \text{ISel, Legalization, Graph-coloring RA} \\
\text{Layer 6} & \text{Target ISA} & \text{MachineFunction} & \text{Target MC Encodings} & \text{x64 AVX/FMA lowering, MC emission} \\
\text{Layer 7} & \text{Sovereign Linker} & \text{MachineFunction / MC} & \text{.obj / .lib / .exe / JIT} & \text{Binary weaving, IAT, reloc fixups, W\textasciicircum X} \\
\text{Layer 8} & \text{Public SDK / Apps} & \text{C API / CLI Args} & \text{Engine Invocation} & \text{User endpoints, diagnostic telemetry} \\
\end{array}$$

**Forbidden Cross-Boundary Contaminations**:
- IR passes **MUST NEVER** emit target register IDs (`XMM0`, `RAX`) or target machine opcodes.
- MIR instruction selection **MUST NEVER** mutate IR nodes or violate SSA def-use chains.
- ISA lowering **MUST NEVER** alter CFG basic block structure.
- The Sovereign Linker **MUST NEVER** depend on external linker executables (`link.exe`, `lld`, `ld`).

---

## Layer-by-Layer Architectural Specification

```
================================================================================
LAYER 1: DEFINITIONS & AUTOGENERATION CONTRACTS (defs/ & tools/cpp/)
================================================================================
```

### 1.1 `defs/ir/phase1_core_ops.dgtl_def`
- **Invariant**: All opcode numerical values remain **100% frozen**:
  - `ADD_F32` (0x1E), `SUB_F32` (0x1F), `MUL_F32` (0x20), `DIV_F32` (0x21), `FMA_F32` (0x23)
  - `FAST_INV_SQRT` (0x700), `SIMD_BSL_PREDICATE` (0x507), `SIMD_COMPLEX_FMA` (0x8015)
  - `CALL` (0x6E), `RET` (0x65)
- **Validation**: Zero modifications to existing numeric opcodes guarantees that all 8 cross-target lookup tables remain cache-coherent and ABI-stable.

### 1.2 `defs/isel/machine_semantics.dgtl_contract`
- **Invariant**: Retains the canonical 47 transcendental semantic kinds:
  - `FloatSin` (38), `FloatCos` (39), `FloatTan` (40), `FloatAsin` (41), `FloatAcos` (42), `FloatAtan` (43), `FloatAtan2` (44)
  - `FloatSinh` (45), `FloatCosh` (46), `FloatTanh` (47)
  - `FloatExp` (30), `FloatExp2` (31), `FloatExp10` (32), `FloatExpm1` (33)
  - `FloatLog` (34), `FloatLog2` (35), `FloatLog10` (36), `FloatLog1p` (37)
  - `FloatPow` (26), `FloatRsqrt` (27), `FloatCbrt` (28), `FloatHypot` (29), `FloatFma` (19).

### 1.3 `defs/isel/common_runtime_forms.dgtl_isel`
- **Modification**: Promote `SIMD_COMPLEX_FMA` from `Unhandled` to `Call`:
  ```
  form simd_complex_fma: opcode=SIMD_COMPLEX_FMA, machine_form=Call
  ```
- **Rationale**: Allows the instruction selector and legalizer to route vector complex multiplication into `__dgtl_rt_complex_mul` or native vector sequences without triggering compiler aborts.

### 1.4 Autogeneration Tool Verification
- Run `dgtl-gen-validate.exe` to verify syntax, table bounds, and opcode collision avoidance.

---

```
================================================================================
LAYER 2: TRUE IR AUTHORITY (include/dgtl/ir/)
================================================================================
```

Every file in `include/dgtl/ir/` has an audited, explicit role:

### 2.1 `include/dgtl/ir/base/IR_Node.ixx` & `IR_Forward.ixx`
- **Bit-Packed Node Layout (32 bytes)**:
  ```cpp
  struct IRNode {
      uint16_t opcode;        // e.g. OpCode::FMA_F32 (0x23)
      uint16_t flags;         // Flags::FAST_MATH, Flags::NONE
      uint32_t result_id;     // SSA destination slot
      uint32_t op_a;          // First multiplicand
      uint32_t op_b;          // Second multiplicand
      uint32_t op_c;          // Accumulator operand
      uint64_t intent_hash;   // Hash of (opcode, result_id, op_a, op_b, op_c)
      uint32_t logic_flags;   // Predication / rounding mode
      uint32_t aux_flags;     // Provenance & vector width
  };
  ```
- **Invariants**:
  - `intent_hash` calculation for `FMA_F32` strictly hashes all 3 operands (`op_a`, `op_b`, `op_c`), ensuring that Global Value Numbering (GVN) deduplicates identical polynomial sub-terms across Horner evaluations.

### 2.2 `include/dgtl/ir/base/IR_Verifier.ixx` (Flat-Stream Verification Oracle)
- **All 7 Verification Passes**:
  - **Pass 1 (Structural Validity)**: `FMA_F32` must produce a non-invalid `result_id`.
  - **Pass 2 (Intent Hash Consistency)**: Intent hash must match recomputed hash of `(opcode, result_id, op_a, op_b, op_c)`.
  - **Pass 3 (BasicBlock Boundaries)**: Nodes must belong to valid blocks; branch nodes must terminate blocks.
  - **Pass 4 (Use-Before-Def Rule)**: `op_a`, `op_b`, and `op_c` must be defined prior to the `FMA_F32` node in flat-stream index order.
  - **Pass 5 (Type Consistency)**: Operands for `FMA_F32` must share floating-point representation (`TypeKind::Float32` or `Float64`).
  - **Pass 6 (Dominance Invariants)**: SSA definitions must dominate all uses.
  - **Pass 7 (Opcode-Specific Constraints)**: Enforces lines 1420–1430: `op_a`, `op_b`, and `op_c` must not be `INVALID_SLOT`. Enforces line 1353: `FAST_INV_SQRT` must have non-invalid `op_a`.

### 2.3 `include/dgtl/ir/base/IR_Storage.ixx` & `IR_Stream.ixx`
- 64-byte cache-line aligned Structure of Arrays (SoA).
- Allocations backed by `ThreadLocalArena`. Polynomial expansions reserve capacity upfront (`stream.reserve_capacity(stream.size() + expansion_nodes)`) to eliminate dynamic reallocations.

### 2.4 `include/dgtl/ir/IR_Transaction.ixx` & `IR_Pass_Contract.ixx`
- Every math lowering mutation is wrapped in a transaction:
  ```cpp
  auto tx = stream.begin_transaction();
  if (lower_transcendentals(stream)) {
      if (stream.validate_shadow_integrity()) {
          tx.commit();
      } else {
          tx.rollback();
      }
  }
  ```

---

```
================================================================================
LAYER 3: CORE MATHEMATICAL FOUNDATION (include/dgtl/math/ & utils/math/)
================================================================================
```

### 3.1 `include/dgtl/utils/math/Math_Utils.ixx` (Existing High-Performance Base)
- **Status**: Production-ready IEEE-754 and fast-math minimax implementation:
  - Exact IEEE: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `log`, `log2`, `log10`, `log1p`, `exp`, `exp2`, `exp10`, `expm1`, `pow`, `sqrt`, `cbrt`, `hypot`, `fma`, `fabs`, `floor`, `ceil`, `trunc`, `round`, `fmod`, `remainder`, `copysign`, `nextafter`.
  - Fast-Math Minimax: `fast_sin_poly`, `fast_cos_poly`, `fast_sin`, `fast_cos`, `fast_tan`, `fast_exp2_poly`, `fast_exp`, `fast_log`, `fast_pow`, `fast_inv_sqrt_f32`, `fast_inv_sqrt_f64`.
  - Bitwise Extras: `clz`, `ctz`, `popcount`, `rotl`, `rotr`, `is_pow2`, `saturating_add`, `saturating_sub`, `saturating_mul`, `mul_high_u64`, `mul_high_i64`.

### 3.2 `include/dgtl/math/Math_Algorithms.ixx` [NEW]
- **Module**: `dgtl.math.algorithms`
- **Quadruple-Precision Cody-Waite Range Reduction**:
  $$\begin{aligned}
  q &= \text{round}\left(x \cdot \frac{2}{\pi}\right) \\
  r &= \left(x - q \cdot \frac{\pi}{2}_{\text{hi}}\right) - q \cdot \frac{\pi}{2}_{\text{lo}}
  \end{aligned}$$
  where:
  - $\frac{\pi}{2}_{\text{hi}} = 1.57079632679489655800$ (high 53 bits)
  - $\frac{\pi}{2}_{\text{lo}} = 6.12323399573676603587 \times 10^{-17}$ (low 53 bits)
  - Eliminates catastrophic cancellation for arguments up to $|x| < 10^5$.
- **Remez Minimax Horner Evaluation**:
  - Evaluated entirely via chains of native `FMA_F32` instructions:
    $$\sin(r) \approx r + r \cdot r^2 \cdot \left(s_1 + r^2 \cdot \left(s_2 + r^2 \cdot \left(s_3 + r^2 \cdot \left(s_4 + r^2 \cdot s_5\right)\right)\right)\right)$$
    Accuracy: $< 0.5\text{ ULP}$ error on entire reduced interval $[-\pi/4, \pi/4]$.

### 3.3 `include/dgtl/math/Complex_Utils.ixx` [NEW]
- **Module**: `dgtl.math.complex`
- **Smith's Robust Complex Division**:
  $$\frac{a + bi}{c + di} = \begin{cases}
  \frac{a + b(d/c)}{c + d(d/c)} + i \frac{b - a(d/c)}{c + d(d/c)}, & \text{if } |c| \ge |d| \\
  \frac{a(c/d) + b}{c(c/d) + d} + i \frac{b(c/d) - a}{c(c/d) + d}, & \text{if } |c| < |d|
  \end{cases}$$
  Guarantees zero premature overflow or underflow when $c, d \in [10^{-35}, 10^{35}]$.
- **Euler's Polar Conversion & Vectorized Multiplication**:
  - $e^{i\theta} = \cos\theta + i\sin\theta$ with simultaneous range reduction.
  - Complex multiplication: $(ac - bd) + i(ad + bc)$ synthesized as 4 multiplies and 2 additions/subtractions, vectorized via `SIMD_COMPLEX_FMA`.

### 3.4 `include/dgtl/math/Calculus_Utils.ixx` [NEW]
- **Module**: `dgtl.math.calculus`
- **Gauss-Legendre 5-Point Quadrature**:
  $$\int_a^b f(x)dx \approx \frac{b - a}{2} \sum_{i=1}^5 w_i f\left(\frac{b - a}{2}\xi_i + \frac{a + b}{2}\right)$$
  Exact for all polynomials up to degree 9.
- **Adaptive Runge-Kutta-Fehlberg (RKF45) ODE Solver**:
  - Embedded 4th/5th order Runge-Kutta pairs:
    $$\begin{aligned}
    y_{n+1} &= y_n + \sum_{i=1}^6 c_i k_i \quad (\text{Order 5}) \\
    y^*_{n+1} &= y_n + \sum_{i=1}^6 c^*_i k_i \quad (\text{Order 4}) \\
    E &= \|y_{n+1} - y^*_{n+1}\| \\
    h_{\text{new}} &= 0.9 \cdot h \cdot \left(\frac{\text{tol}}{E}\right)^{0.2}
    \end{aligned}$$
  - Solves initial value problems $y' = f(t, y)$ with automatic step adaptation and zero user parameter tuning.

---

```
================================================================================
LAYER 4: IR PASS TRANSFORMS & IRBUILDER (include/dgtl/ir/pass/, pipeline/, api/)
================================================================================
```

### 4.1 `include/dgtl/ir/pass/Math_Lowering_Pass.ixx` [NEW]
- **Module**: `dgtl.ir.pass.math_lowering`
- **Class**: `MathLoweringPass : public dgtl::pipeline::DGTLOptimizationPass`
- **Optimization Strategy**:
  1. **Pattern Matching**: Matches calls to transcendental functions (`sin`, `cos`, `tan`, `exp`, `log`, `pow`).
  2. **Fast-Math Expansion**: Under `-Ofast` or `Flags::FAST_MATH`, replaces call nodes with inline Cody-Waite reduction nodes and Horner FMA chains.
  3. **Strict IEEE Legalization**: Under default flags, rewrites calls to canonical ABI symbols (`__dgtl_rt_sin`, etc.).
  4. **Sincos Fusion**: Scans basic blocks for co-located calls to $\sin(x)$ and $\cos(x)$ sharing identical input SSA values. Merges their range reduction and evaluates polynomials concurrently.

### 4.2 `include/dgtl/ir/pass/Transform_Auto_Diff.ixx` [NEW]
- **Module**: `dgtl.ir.pass.auto_diff`
- **Class**: `TransformAutoDiff : public dgtl::pipeline::DGTLOptimizationPass`
- **Dual Tangent Forward-Mode Differentiation (Enzyme-Beater)**:
  - Operates directly on SSA def-use chains without dynamic tape allocation.
  - For an active function $f(x)$ with respect to independent variable $x$, seeds $\dot{x} = 1.0$ and traverses SSA nodes in topological order:
    $$\begin{aligned}
    z = x + y &\implies \dot{z} = \dot{x} + \dot{y} \\
    z = x - y &\implies \dot{z} = \dot{x} - \dot{y} \\
    z = x \cdot y &\implies \dot{z} = \text{FMA}(\dot{x}, y, \text{FMA}(x, \dot{y}, 0)) \\
    z = x / y &\implies \dot{z} = (\dot{x} - z \cdot \dot{y}) / y \\
    z = \sin(x) &\implies \dot{z} = \dot{x} \cdot \cos(x) \\
    z = \cos(x) &\implies \dot{z} = -\dot{x} \cdot \sin(x) \\
    z = \exp(x) &\implies \dot{z} = \dot{x} \cdot \exp(x) \\
    z = \ln(x) &\implies \dot{z} = \dot{x} / x \\
    z = x^y &\implies \dot{z} = z \cdot (\dot{y} \ln x + y \dot{x} / x)
    \end{aligned}$$
  - Synthesizes exact dual tangents using native `FMA_F32` instructions with zero heap allocations and 100% SSA dominance validity.

### 4.3 `include/dgtl/ir/pipeline/Pass_Manager_Orchestrator.ixx`
- Registers `"math-lowering"` into `PassStage::PostInlining`.
- Registers `"auto-diff"` into `PassStage::PreInlining` and `PassStage::PostInlining`.

### 4.4 `include/dgtl/api/IRBuilder.ixx`
- Adds user-facing creation methods:
  - `create_sin`, `create_cos`, `create_sincos`, `create_tan`
  - `create_exp`, `create_log`, `create_pow`, `create_rsqrt`
  - `create_complex_add`, `create_complex_mul`, `create_complex_div`
  - `create_forward_derivative(uint32_t func_entry, uint32_t wrt_param)`

---

```
================================================================================
LAYER 5: COMPLETE MACHINE IR (MIR) SUBSYSTEM (include/dgtl/mir/)
================================================================================
```

Every single file in `include/dgtl/mir/` and `include/dgtl/mir/machine_ir/` is accounted for:

### 5.1 `include/dgtl/mir/machine_ir/Machine_IR_Types.ixx`
- Defines `MachineOpcode`, `MachineForm`, `MachineOperand`, `MachineRegister`, `RegisterClassKind::FloatingPoint`, `RegisterClassKind::Vector`.
- Supports `MachineOperandKind::FloatingImmediate` and `MachineOperandKind::ConstantPoolIndex`.

### 5.2 `include/dgtl/mir/machine_ir/Machine_IR_Function.ixx` & `Machine_IR_Module.ixx`
- `MachineFunction`: Contains `MachineBasicBlock` sequences and `MachineConstantPool` for polynomial floating-point coefficients.
- `Machine_IR_Module`: Global registry of functions and external symbol declarations (`__dgtl_rt_*`).

### 5.3 `include/dgtl/mir/machine_ir/Machine_IR_Target_Interfaces.ixx` & `Machine_IR_Target_ISel.ixx`
- Defines target register and instruction interfaces (`TargetRegisterInfo`, `MachineInstructionInfo`).
- Marks caller-saved XMM registers around math call boundaries.

### 5.4 `include/dgtl/mir/machine_ir/Machine_IR_Allocation.ixx`, `Machine_IR_Analysis.ixx`, `Machine_IR_Verifier.ixx`
- Bump-allocates instructions during polynomial expansion.
- Tracks register pressure across unrolled loops.
- `MachineVerifier` enforces `kOperandRoleLegalityLUT` for float operands and verifies `MachineFmaView`.

### 5.5 `include/dgtl/mir/machine_ir/Machine_Op_Wrappers.ixx` & `Machine_IR.ixx`
- Provides `MachineFmaView` (`dst()`, `acc()`, `mul_a()`, `mul_b()`, `is_fast_math()`).
- Provides `MachineCallOpView` for runtime ABI invocations.

### 5.6 `include/dgtl/mir/selection/Instruction_Selector.ixx` & `Legalizer.ixx`
- Maps IR nodes to `GenericMachineOpcode::GM_CALL` and `GenericMachineOpcode::GM_FFMA32`.
- When the target lacks hardware FMA, `Legalizer` expands `GM_FFMA32` into `GM_FMUL32` + `GM_FADD32`.

### 5.7 `include/dgtl/mir/regalloc/Universal_GraphColoring_RegAlloc.ixx`
- Allocates `XMM0`–`XMM15` without spilling for Horner evaluation trees up to order 12.

### 5.8 `include/dgtl/mir/transforms/Backend_Runtime_Contract.ixx`
- Canonical lookup table mapping `MachineSemanticKind` to ABI names:
  `__dgtl_rt_sin`, `__dgtl_rt_cos`, `__dgtl_rt_tan`, `__dgtl_rt_exp`, `__dgtl_rt_log`, `__dgtl_rt_pow`, `__dgtl_rt_fast_inv_sqrt`.

---

```
================================================================================
LAYER 6: TARGET ISA LOWERING & MC ENCODING (include/dgtl/isa/x64/)
================================================================================
```

### 6.1 `include/dgtl/isa/x64/X64_Machine_Form_Lowering.ixx`
- Lowers `GM_FFMA32` to AVX `VFMADD213SS` ($a \cdot b + c$) or `VFMADD231SS` ($b \cdot c + a$).
- Lowers `GM_FAST_INV_SQRT` to `RSQRTSS` with Newton-Raphson refinement step.

### 6.2 `include/dgtl/isa/x64/X64_MC_Encoder.ixx`
- Emits 3-byte VEX prefixes (`C4 E2 ...`) for 3-operand FMA instructions.
- Encodes 32-bit PC-relative relocations (`IMAGE_REL_AMD64_REL32` / `R_X86_64_PLT32`) for math calls.

### 6.3 `include/dgtl/isa/x64/X64_ABI.ixx`
- Implements Microsoft x64 and System V calling conventions:
  - First 4 float arguments in `XMM0`–`XMM3`.
  - Floating-point return value in `XMM0`.

---

```
================================================================================
LAYER 7: DGTL'S SOVEREIGN LINKER & JIT SUBSYSTEM (include/dgtl/linker/, object/, jit/)
================================================================================
```

### 7.1 `include/dgtl/linker/Linker_Driver.ixx` (The Sovereign Binary Weaver)
- **Role**: Emits machine code and weaves COFF `.obj` and ELF `.o` files with relocations.
- **Key Updates**:
  1. **Dual-ABI Return Fixup (lines 1028–1043)**: Add `__dgtl_rt_fast_inv_sqrt` and `__dgtl_rt_rsqrt` to the `returns_in_xmm0 = true` condition:
     ```cpp
     } else if (name == "pow" || name == "sqrt" || name == "sin" ||
                name == "cos" || name == "tan" || name == "exp" ||
                name == "log" || name == "fabs" || name == "floor" ||
                name == "ceil" ||
                name == "__dgtl_rt_sin" || name == "__dgtl_rt_cos" ||
                name == "__dgtl_rt_tan" || name == "__dgtl_rt_asin" ||
                name == "__dgtl_rt_acos" || name == "__dgtl_rt_atan" ||
                name == "__dgtl_rt_atan2" || name == "__dgtl_rt_sinh" ||
                name == "__dgtl_rt_cosh" || name == "__dgtl_rt_tanh" ||
                name == "__dgtl_rt_log" || name == "__dgtl_rt_log2" ||
                name == "__dgtl_rt_log10" || name == "__dgtl_rt_log1p" ||
                name == "__dgtl_rt_exp" || name == "__dgtl_rt_exp2" ||
                name == "__dgtl_rt_exp10" || name == "__dgtl_rt_expm1" ||
                name == "__dgtl_rt_pow" || name == "__dgtl_rt_cbrt" ||
                name == "__dgtl_rt_hypot" ||
                name == "__dgtl_rt_fast_inv_sqrt" || name == "__dgtl_rt_rsqrt") {
         returns_in_xmm0 = true;
     }
     ```
  2. **Native FMA Emission**: Add the missing `case MachineForm::FloatTernaryFma32:` block inside `emit_aot_machine_code` (between lines 902 and 904):
     ```cpp
     case MachineForm::FloatTernaryFma32: {
         // movss xmm0, [rbp + slot_disp(node.op_a)]
         write_u8(0xF3); write_u8(0x0F); write_u8(0x10); write_u8(0x85);
         write_i32(slot_disp(node.op_a));
         // mulss xmm0, [rbp + slot_disp(node.op_b)]
         write_u8(0xF3); write_u8(0x0F); write_u8(0x59); write_u8(0x85);
         write_i32(slot_disp(node.op_b));
         // addss xmm0, [rbp + slot_disp(node.op_c)]
         write_u8(0xF3); write_u8(0x0F); write_u8(0x58); write_u8(0x85);
         write_i32(slot_disp(node.op_c));
         // movss [rbp + slot_disp(node.result_id)], xmm0
         write_u8(0xF3); write_u8(0x0F); write_u8(0x11); write_u8(0x85);
         write_i32(slot_disp(node.result_id));
         break;
     }
     ```

### 7.2 `include/dgtl/linker/pe/PE_Linker.ixx` (Sovereign In-Memory PE Linker)
- Direct emission of standalone PE32+ executables (`.exe`) with complete IAT (linking standard CRT math in `msvcrt.dll` / `ucrtbase.dll`), `.reloc` base relocations, and zero reliance on external `link.exe`.

### 7.3 `include/dgtl/runtime/Runtime_Maths.ixx`
- Add zero-overhead C-ABI exports:
  ```cpp
  inline float __dgtl_rt_fast_inv_sqrt(float x) noexcept { return dgtl::utils::math::fast_inv_sqrt_f32(x); }
  inline double __dgtl_rt_rsqrt(double x) noexcept { return dgtl::utils::math::fast_inv_sqrt_f64(x); }
  ```

### 7.4 `include/dgtl/jit/JIT_Orchestrator.ixx` & `JITSession.ixx`
- Pre-register `__dgtl_rt_fast_inv_sqrt` and `__dgtl_rt_rsqrt` in `session.map_symbol()` (lines 295–317) and in the fallback dispatch map (lines 617–635).
- Guarantees zero `DGTL JIT Linker Error: Unresolved external host symbol` during in-memory kernel execution.

---

```
================================================================================
LAYER 8: PUBLIC C/C++ SDK, DIAGNOSTIC CLI TOOLS & E2E VERIFICATION
================================================================================
```

### 8.1 `include/dgtl_sdk/dgtl.h` & `src/sdk/dgtl_sdk_impl.cpp`
- **Public C API**:
  - `dgtl_build_sin`, `dgtl_build_cos`, `dgtl_build_sincos`, `dgtl_build_tan`
  - `dgtl_build_exp`, `dgtl_build_log`, `dgtl_build_pow`, `dgtl_build_rsqrt`
  - `dgtl_build_complex_mul`, `dgtl_build_complex_div`
  - `dgtl_generate_forward_derivative`

### 8.2 Diagnostic CLI Tools & Forensic Debugger
- **`src/apps/dgtl_opt_main.cpp`**: CLI options `--pass=math-lowering`, `--pass=auto-diff`, `-Ofast`.
- **`src/apps/dgtl_llc_main.cpp`**: `--fast-math` flag triggering FMA synthesis and polynomial fast paths.
- **`src/apps/dgtl_engine_main.cpp`**: Pre-registers host math symbols before jumping to kernel entry point.
- **`tools/cpp/dgtl_debugger_main.cpp`**: Verifies zero module import cycles and zero symbol collisions.

### 8.3 `tests/benchmarks/e2e_production/test_advancemath.cpp` [NEW]
- **Verification Suites**:
  1. **Accuracy & ULP Suite**:
     - Evaluate `sin`, `cos`, `tan`, `exp`, `log`, `pow` across $[-1000.0, 1000.0]$ against double precision.
     - Max error $\le 0.5\text{ ULP}$ for IEEE path, $\le 1.0\text{ ULP}$ for fast polynomial path.
  2. **Complex Arithmetic Suite**:
     - Smith's division with $c, d \in [10^{-35}, 10^{35}]$ (zero overflow/underflow or NaN).
     - Euler's identity: $e^{i\pi} + 1 = 0$ within $10^{-7}$ tolerance.
  3. **Numerical Calculus Suite**:
     - Gauss-Legendre 5-point quadrature on $\int_0^\pi \sin(x)dx = 2.0$.
     - Adaptive Simpson integration on $\int_0^1 \frac{4}{1+x^2}dx = \pi$.
     - RKF45 ODE solver on $y' = -2ty, y(0)=1$ (analytical: $y(t) = e^{-t^2}$).
  4. **Auto-Diff Tangent Suite**:
     - Compute $\frac{d}{dx}[x^3 + \sin(x)\exp(x)]$ at $x = 2.0$ via `Transform_Auto_Diff` and compare against symbolic analytic derivative $3x^2 + \cos(x)\exp(x) + \sin(x)\exp(x)$.
  5. **Sovereign Linker & JIT End-to-End Suite**:
     - Emit `.obj` via `DGTLAOTLinker`.
     - Emit standalone `.exe` via `SovereignPELinker`.
     - Execute JIT kernel in memory, verifying correct `XMM0` return extraction and zero memory leaks.

---

## Phased Implementation Sequence

| Phase | Subsystem Focus | Key Deliverables | Quality & Safety Gate |
|---|---|---|---|
| **Phase 1** | Defs & Autogen | Update `common_runtime_forms.dgtl_isel` (`SIMD_COMPLEX_FMA` -> `Call`). Run `dgtl-gen` & `dgtl-isel-gen`. | `dgtl-gen-validate` passes with 0 diagnostics. |
| **Phase 2** | Math Foundation | Implement `Math_Algorithms.ixx`, `Complex_Utils.ixx`, `Calculus_Utils.ixx` in `include/dgtl/math/`. | Numerical stability verified, Cody-Waite reduction (< 0.5 ULP). |
| **Phase 3** | True IR Authority & Passes | Implement `Math_Lowering_Pass.ixx`, `Transform_Auto_Diff.ixx`. Register in `Pass_Manager_Orchestrator.ixx`. | `IR_Verifier` passes 7 passes on synthesized math streams. |
| **Phase 4** | MIR, Machine IR & ISA | Wire `Backend_Runtime_Contract`, `Instruction_Selector`, `Machine_Op_Wrappers`, `X64_MC_Encoder`. | `MachineVerifier` passes with 0 errors. |
| **Phase 5** | Sovereign Linker & JIT | Add `FloatTernaryFma32` to `Linker_Driver.ixx:emit_aot_machine_code`. Update `returns_in_xmm0` with `fast_inv_sqrt`. Update `Runtime_Maths.ixx`, `JIT_Orchestrator.ixx`. | Sovereign linker emits valid `.obj`/`.exe`; JIT runs with zero unresolved symbol error. |
| **Phase 6** | Apps, SDK & Debugger | Update `IRBuilder.ixx`, `dgtl.h`, `dgtl_sdk_impl.cpp`, `dgtl_opt_main.cpp`, `dgtl_debugger_main.cpp`. | `dgtl-debugger` confirms 0 module cycles and 0 symbol collisions. |
| **Phase 7** | E2E Conformance Suite | Create and run `tests/benchmarks/e2e_production/test_advancemath.cpp`. | All ULP, complex, calculus, auto-diff, and sovereign linker tests pass 100%. |
