# MYZON → DGTL Public SDK Migration Plan

**Goal:** Migrate MYZON from DGTL internal C++ API (Tier C) to the public C SDK (`dgtl.h` / `dgtl_core.h` / ``).

---

## Current State

MYZON currently includes **8 internal DGTL headers** and compiles **3 DGTL IR source files** directly:

### Internal Headers Used

| File | Internal Include | Used For |
|------|-----------------|----------|
| `main.cpp` | `dgtl/api/Backend_Driver_API.hpp` | `FrontendEmitRequest`, `emit_frontend_module()`, `parse_backend_driver_target()` |
| `Lowering.cpp` | `dgtl/api/Backend_Driver_API.hpp` | Same as above |
| `Lowering.cpp` | `dgtl/api/Function.hpp` | `DGTLFunction` |
| `Lowering.cpp` | `dgtl/api/Frontend_Pipeline.hpp` | `FrontendPipelineLowerer::lower_function()` |
| `Lowering.cpp` | `dgtl/api/ABI.hpp` | `DGTLABIRegistry`, `DGTLABIValueClass` |
| `Lowering.cpp` | `dgtl/api/ControlFlow.hpp` | `DGTLControlFlowGraph` |
| `Lowering.cpp` | `dgtl/api/FrontendHelpers.hpp` | `FrontendLoweringContext` |
| `Lowering.cpp` | `dgtl/api/Frontend_Payload.hpp` | `FrontendPayloadModule`, `FrontendLoweringFunction`, `write_frontend_payload_file()` |
| `Lowering.cpp` | `dgtl/ir/base/IR_Types.hpp` | `FrontendInstructionKind` enum |

### DGTL Sources Compiled Into MYZON

```
DGTL/src/ir/IR_Stream_Core.cpp
DGTL/src/ir/IR_Stream_Operations.cpp
DGTL/src/ir/IR_Stream_Optimizations.cpp
```

---

## Migration Changes

### 1. `main.cpp` — Include Change

**Before:**
```cpp
#ifdef MYZON_HAS_DGTL
#include "dgtl/api/Backend_Driver_API.hpp"
#endif
```

**After:**
```cpp
#ifdef MYZON_HAS_DGTL
#include "dgtl.h"
#include "dgtl_core.h"
#endif
```

### 2. `main.cpp` — Rewrite `emit_with_dgtl_backend_driver()`

The `FrontendEmitRequest` / `emit_frontend_module()` API does not exist in the public SDK.

**Option A: Out-of-process (shell out to `dgtl_release.exe`)**

Replace the entire function with:

```cpp
#ifdef MYZON_HAS_DGTL
bool emit_with_dgtl_backend_driver(const CliOptions& options,
                                   std::string_view source_path,
                                   std::string_view frontend_payload_path) {
    const std::filesystem::path source_fs(source_path);
    const std::string stem = source_fs.stem().string().empty() ? "module" : source_fs.stem().string();

    auto run_backend = [&](const std::string& target_name) -> bool {
        std::string cmd = options.dgtl_exe_path
            + " --frontend-payload " + std::string(frontend_payload_path)
            + " --backend " + target_name
            + " --output-dir " + options.emit_dir
            + " --no-print";
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            std::cerr << "emit failed for " << target_name << " (exit " << rc << ")\n";
            return false;
        }
        std::cout << "emitted " << target_name << "\n";
        return true;
    };

    if (options.emit_all) {
        bool ok = true;
        for (const char* t : {"arm64", "x64", "nasm", "wasm"}) {
            ok &= run_backend(t);
        }
        return ok;
    } else {
        return run_backend(options.emit_target);
    }
}
#endif
```

**Option B: In-process (link `dgtl.lib`)**

Replace with `dgtl_emit_to_file()` calls — requires the stream to already be built (see Lowering changes below).

### 3. `Lowering.hpp` — Remove Internal DGTL Types

**Before:**
```cpp
#ifdef MYZON_HAS_DGTL
// References to FrontendLoweringFunction, FrontendPayloadModule,
// DGTLFunction, DGTLABIValueClass, etc.
#endif
```

**After:**
```cpp
#ifdef MYZON_HAS_DGTL
#include "dgtl.h"
#include "dgtl_core.h"

// Replace internal types with SDK handles
// FrontendPayloadModule   → dgtl_stream_t* per function
// FrontendLoweringFunction → direct dgtl_emit_*() calls
// DGTLABIValueClass       → removed (not in public SDK)
// FrontendTypeHandle       → removed (not in public SDK)
#endif
```

### 4. `Lowering.cpp` — The Major Rewrite

This is the largest change. Replace all 8 internal includes with:

```cpp
#ifdef MYZON_HAS_DGTL
#include "dgtl.h"
#include "dgtl_core.h"
#endif
```

#### 4a. Remove `verify_dgtl_frontend_link_contract()`

No equivalent in public SDK. Replace with a simple version check:

```cpp
bool verify_dgtl_link_contract() {
    const char* ver = dgtl_version_string();
    // Optionally check DGTL_VERSION_MAJOR >= 1
    return ver != nullptr;
}
```

#### 4b. Rewrite `adapt_mir_to_frontend_lowering_function()`

Currently builds a `FrontendLoweringFunction` struct (internal). Replace with direct emission into a `dgtl_stream_t*`:

**Before (conceptual):**
```cpp
void adapt_mir_to_frontend_lowering_function(
    const MirFunction& mir_fn,
    FrontendLoweringFunction& adapted,
    std::string& error);
```

**After:**
```cpp
dgtl_stream_t* adapt_mir_to_stream(
    dgtl_context_t* ctx,
    const MirFunction& mir_fn,
    std::string& error)
{
    dgtl_stream_t* s = dgtl_stream_create(ctx, mir_fn.instructions.size() * 2);
    std::unordered_map<std::string, uint32_t> slot_map;

    // Parameters get initial const slots
    for (size_t i = 0; i < mir_fn.parameters.size(); ++i) {
        uint32_t slot = dgtl_emit_const_i64(s, 0);
        slot_map["%" + std::to_string(i + 1)] = slot;
    }

    auto resolve = [&](const std::string& op) -> uint32_t {
        auto it = slot_map.find(op);
        if (it != slot_map.end()) return it->second;
        error = "unresolved: " + op;
        return 0;
    };

    for (const auto& inst : mir_fn.instructions) {
        const auto& op = inst.op;
        const auto& ops = inst.operands;

        if (op == "const_i64" && ops.size() >= 2) {
            uint64_t imm = std::strtoull(ops[1].c_str(), nullptr, 10);
            slot_map[ops[0]] = dgtl_emit_const_i64(s, imm);
        }
        else if (op == "add_i32" && ops.size() >= 3)
            slot_map[ops[0]] = dgtl_emit_add_i32(s, resolve(ops[1]), resolve(ops[2]));
        else if (op == "sub_i32" && ops.size() >= 3)
            slot_map[ops[0]] = dgtl_emit_sub_i32(s, resolve(ops[1]), resolve(ops[2]));
        else if (op == "mul_i32" && ops.size() >= 3)
            slot_map[ops[0]] = dgtl_emit_mul_i32(s, resolve(ops[1]), resolve(ops[2]));
        else if (op == "div_i32" && ops.size() >= 3)
            slot_map[ops[0]] = dgtl_emit_div_i32(s, resolve(ops[1]), resolve(ops[2]));
        else if (op == "mod_i32" && ops.size() >= 3)
            slot_map[ops[0]] = dgtl_emit_mod_i32(s, resolve(ops[1]), resolve(ops[2]));
        else if ((op == "cmp_eq" || op == "cmp_ne" || op == "cmp_lt" ||
                  op == "cmp_le" || op == "cmp_gt" || op == "cmp_ge") && ops.size() >= 3) {
            static const std::unordered_map<std::string, dgtl_opcode_t> cmp_map = {
                {"cmp_eq", DGTL_OP_CMP_EQ}, {"cmp_ne", DGTL_OP_CMP_NE},
                {"cmp_lt", DGTL_OP_CMP_LT}, {"cmp_le", DGTL_OP_CMP_LE},
                {"cmp_gt", DGTL_OP_CMP_GT}, {"cmp_ge", DGTL_OP_CMP_GE},
            };
            slot_map[ops[0]] = dgtl_emit_op(s, cmp_map.at(op),
                resolve(ops[1]), resolve(ops[2]), 0, DGTL_FLAG_NONE);
        }
        else if (op == "label" && ops.size() >= 1) {
            uint64_t id = std::strtoull(ops[0].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(s, DGTL_OP_LABEL, 0, 0, id, DGTL_FLAG_NONE);
        }
        else if (op == "jmp" && ops.size() >= 1) {
            uint64_t target = std::strtoull(ops[0].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(s, DGTL_OP_JMP, 0, 0, target, DGTL_FLAG_NONE);
        }
        else if ((op == "br_if" || op == "br_if_false") && ops.size() >= 2) {
            uint64_t target = std::strtoull(ops[1].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(s, DGTL_OP_BR_IF, resolve(ops[0]), 0, target, DGTL_FLAG_NONE);
        }
        else if (op == "call" && ops.size() >= 2) {
            slot_map[ops[0]] = dgtl_emit_op(s, DGTL_OP_CALL, 0, 0,
                0, DGTL_FLAG_HAS_SIDE_EFFECT);
        }
        else if (op == "ret" && ops.size() >= 1) {
            dgtl_emit_return(s, resolve(ops[0]));
        }
        else if (op == "ret_void") {
            uint32_t zero = dgtl_emit_const_i64(s, 0);
            dgtl_emit_return(s, zero);
        }
        // drop / drop_heap → skip (DGTL DCE handles it)
    }

    return s;
}
```

#### 4c. Rewrite `write_dgtl_frontend_payload()`

**Before:**
```cpp
bool write_dgtl_frontend_payload(const MirModule& module, const std::string& path) {
    dgtl::core::FrontendPayloadModule payload;
    // ... adapt + push to payload ...
    dgtl::core::write_frontend_payload_file(payload, path, &error);
}
```

**After:**
```cpp
bool write_dgtl_frontend_payload(const MirModule& module, const std::string& path) {
    dgtl_context_t* ctx = dgtl_context_create();
    std::string error;

    for (const auto& fn : module.functions) {
        dgtl_stream_t* s = adapt_mir_to_stream(ctx, fn, error);
        if (!error.empty()) {
            dgtl_stream_destroy(s);
            dgtl_context_destroy(ctx);
            return false;
        }
        dgtl_status_t ws = dgtl_write_payload(s, path.c_str());
        dgtl_stream_destroy(s);
        if (!ws.ok) {
            dgtl_context_destroy(ctx);
            return false;
        }
    }

    dgtl_context_destroy(ctx);
    return true;
}
```

#### 4d. Rewrite `lower_mir_to_dgtl()`

**Before (internal):**
```cpp
FrontendPipelineLowerer::lower_function(adapted, dgtl_fn, abi_registry);
dgtl_fn.stream().perform_gvn();
dgtl_fn.stream().perform_dead_code_elimination();
```

**After (public SDK):**
```cpp
dgtl_context_t* ctx = dgtl_context_create();
for (const auto& fn : mir.functions) {
    dgtl_stream_t* s = adapt_mir_to_stream(ctx, fn, error);
    dgtl_optimize(s);                        // replaces GVN + DCE
    dgtl_strike_stream(s, DGTL_TARGET_X64);  // or desired target
    dgtl_stream_destroy(s);
}
dgtl_context_destroy(ctx);
```

### 5. `myzon.ps1` — Build Script Changes

#### 5a. Remove DGTL IR Source Compilation

**Remove these 3 lines:**
```powershell
$sources += (Join-Path $DGTLRoot 'src\ir\IR_Stream_Core.cpp')
$sources += (Join-Path $DGTLRoot 'src\ir\IR_Stream_Operations.cpp')
$sources += (Join-Path $DGTLRoot 'src\ir\IR_Stream_Optimizations.cpp')
```

#### 5b. Change Include Path

**Before:**
```powershell
$cflags += ('-I' + $DGTLInclude)        # Points to DGTL/include (internal)
```

**After:**
```powershell
$SDKInclude = Join-Path $DGTLRoot 'build\DGTL_SDK\include'
$cflags += ('-I' + $SDKInclude)          # Points to DGTL_SDK/include (public)
```

#### 5c. Add Static Library Linking

**Add to link step:**
```powershell
$SDKLib = Join-Path $DGTLRoot 'build\DGTL_SDK\lib\libdgtl.a'
# Change link command from:
& g++ @objects -o $output
# To:
& g++ @objects $SDKLib -o $output
```

#### 5d. Update Header Dependency Scan

**Before:**
```powershell
$headerDirs = @($IncludeRoot)
if (Test-Path $DGTLInclude) { $headerDirs += $DGTLInclude }
```

**After:**
```powershell
$headerDirs = @($IncludeRoot)
$SDKInclude = Join-Path $DGTLRoot 'build\DGTL_SDK\include'
if (Test-Path $SDKInclude) { $headerDirs += $SDKInclude }
```

---

## What Gets Removed (No Longer Needed)

| Internal API | Purpose | SDK Replacement |
|-------------|---------|-----------------|
| `FrontendLoweringFunction` | Structured IR intake | Direct `dgtl_emit_*()` calls |
| `FrontendPayloadModule` | Multi-function payload container | `dgtl_write_payload()` per stream |
| `FrontendPipelineLowerer` | Internal lowering pipeline | `dgtl_optimize()` + `dgtl_strike_stream()` |
| `DGTLFunction` | Function container with arena | `dgtl_stream_t*` |
| `DGTLABIRegistry` | Call signature validation | Removed (enforce in your frontend) |
| `DGTLABIValueClass` | ABI classification | Removed |
| `FrontendTypeHandle` | Type metadata tag (`0x1001`) | Removed (or encode in custom flag bits 8-15) |
| `DGTLControlFlowGraph` | CFG with PHI insertion | `dgtl_emit_op()` with `DGTL_OP_LABEL` / `DGTL_OP_JMP` / `DGTL_OP_BR_IF` |
| `FrontendEmitRequest` | Backend driver request struct | `std::system()` call to `dgtl_release.exe` |
| `verify_dgtl_frontend_link_contract()` | ABI compatibility check | `dgtl_version_string()` check |
| IR source files (3 `.cpp`) | IRStream internals | `libdgtl.a` static library |

---

## What You Gain

- **Decoupled builds** — MYZON no longer compiles DGTL source files (saves ~3 objects from the 22)
- **Stable ABI** — Tier A API with semver guarantees; internal changes won't break MYZON
- **Cleaner separation** — MYZON can be built and distributed independently of DGTL source tree
- **Simpler dependency** — Only 3 header files + 1 static library vs. 84+ internal headers

## What You Lose

- Direct `FrontendPipelineLowerer` access (the structured lowering pipeline)
- `DGTLABIRegistry` call arity validation (must enforce in MYZON frontend instead)
- Granular per-pass control (only `dgtl_optimize()` / `dgtl_optimize_with_profile()` instead of individual GVN/DCE calls)
- Type metadata propagation (`FrontendTypeHandle`, `DGTLABIValueClass`)
- PHI node insertion via `DGTLControlFlowGraph` (must emit PHI via `dgtl_emit_op()` with `DGTL_OP_PHI`)

---

## File Change Summary

| File | Change | Effort |
|------|--------|--------|
| `src/main.cpp` | Replace include + rewrite `emit_with_dgtl_backend_driver()` | Small |
| `include/myzon/frontend/Lowering.hpp` | Remove internal DGTL types, add SDK handles | Small |
| `src/frontend/Lowering.cpp` | **Major rewrite**: adapt, payload write, lower functions | Large |
| `myzon.ps1` | Include path, remove IR sources, add library link | Small |

---

## Pros & Cons

### Staying on Internal API (Current State)

**Pros:**
- Full access to `FrontendPipelineLowerer` — the most optimized lowering path
- `DGTLABIRegistry` validates call signatures automatically (catches bugs early)
- Granular pass control — can call GVN, DCE, SIMD legalization individually
- Type metadata (`FrontendTypeHandle`, `DGTLABIValueClass`) propagates through the entire pipeline
- PHI insertion via `DGTLControlFlowGraph` — proper SSA construction
- `FrontendLoweringFunction` is a structured, validated contract — harder to emit malformed IR
- Maximum performance — no abstraction overhead

**Cons:**
- **No stability guarantee** — Tier C API can break on any DGTL update without notice
- Tightly coupled — MYZON compiles 3 DGTL `.cpp` files directly into its binary
- 84+ internal headers scanned on every build (slower incremental builds)
- Can't distribute MYZON independently — needs DGTL source tree present
- Any DGTL refactor forces MYZON recompilation

### Migrating to Public SDK

**Pros:**
- **Semver stability** — Tier A API guaranteed backward-compatible across minor versions
- Decoupled build — only 3 headers + 1 `.lib`, no DGTL source files compiled
- Faster incremental builds (3 headers vs 84+)
- MYZON can be built and shipped without DGTL source tree
- Clean separation for open-sourcing MYZON on GitHub independently
- Same binary works across DGTL updates (as long as major version matches)

**Cons:**
- Lose `DGTLABIRegistry` call validation — must enforce arity/types in MYZON's own frontend
- Lose granular per-pass control — only `dgtl_optimize()` or 6 profiles, can't call GVN/DCE individually
- Lose type metadata propagation — no `FrontendTypeHandle` / `DGTLABIValueClass`
- `Lowering.cpp` needs a major rewrite (~500+ lines changed)
- Slightly more error-prone IR emission — raw `dgtl_emit_*()` calls vs structured `FrontendLoweringFunction`
- No `DGTLControlFlowGraph` PHI insertion — must emit `DGTL_OP_PHI` manually

**Recommendation:** Stay on internal API during active development. Migrate to public SDK only when ready to ship MYZON as a separate repo/package.

---

## Optimization Availability After Migration

**All optimizations still run.** They live inside `libdgtl.a` — nothing is lost. Only the **control granularity** changes.

| Optimization | Internal API (Current) | Public SDK |
|---|---|---|
| **GVN** | `stream.perform_gvn()` — call directly | Included in `dgtl_optimize()` |
| **DCE** | `stream.perform_dead_code_elimination()` — call directly | Included in `dgtl_optimize()` |
| **Constant Folding** | `stream.perform_gvn()` includes it | Included in `dgtl_optimize()` |
| **FMA Fusion** | Call individually via pass manager | `dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_FUSION)` |
| **SIMD Legalization** | Call individually via pass manager | `dgtl_legalize_simd(s, arch)` — **directly available in SDK** |
| **SIMD Vectorization** | Call individually via pass manager | `dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_SIMD)` |
| **Register Pressure** | Call individually via pass manager | `dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_PRESSURE)` |
| **Predication** | Call individually via pass manager | `dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_PREDICATION)` |
| **Polyhedral/SCoP** | Direct access to `PolyhedralConfig`, region tiling | Via CLI: `--polyhedral-mode safe\|aggressive` |
| **Execution Graph** | `build_from_ir()`, `schedule()`, `find_fusion_candidates()` | Runs internally during `dgtl_strike_stream()` |
| **Speculation** | `AsyncSpeculator` — 4 variant generation | Runs internally during strike |
| **IR Evolution** | `IR_Evolver::mutate()` with AI cost model | Runs internally during strike |

**What changes:** You pick a **profile** (default/fold/fusion/simd/pressure/predication) instead of calling individual passes in a custom order.

**What does NOT change:**
- Same optimization code runs inside the library
- Same machine code output
- Same LogicHash determinism
- Same throughput (250M+ nodes/sec)

The only real loss is **fine-grained sequencing** — e.g., you cannot do "run GVN → inspect → skip DCE → run SIMD" through the public SDK. But the same optimizations fire internally.

---

## How to Achieve Fine-Grained Sequencing on the Public SDK

### The Problem

On the internal API you can do this:

```cpp
// Internal API — full control
stream.perform_gvn();                        // Step 1: GVN
// inspect IR here, decide next step...
size_t count = stream.node_count();          // Step 2: Inspect
if (count > 100)
    stream.perform_dead_code_elimination();  // Step 3: Conditional DCE
// skip SIMD if not needed
stream.perform_simd_legalization(arch);      // Step 4: SIMD (optional)
```

On the public SDK, `dgtl_optimize()` runs **all default passes at once** — you can't stop midway or skip a pass.

### Solution: Chain Multiple Profile Calls

The SDK **does** let you call `dgtl_optimize_with_profile()` multiple times on the same stream. Each call runs only that profile's passes. Combined with `dgtl_stream_get_node()` for inspection, you get sequencing:

```c
dgtl_context_t* ctx = dgtl_context_create();
dgtl_stream_t*  s   = dgtl_stream_create(ctx, 4096);

// ... emit IR ...

// ── Step 1: Run constant folding only ──
dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_FOLD);

// ── Step 2: Inspect the IR ──
size_t count = dgtl_stream_node_count(s);
uint64_t hash_after_fold = dgtl_stream_logic_hash(s);
printf("After fold: %zu nodes, hash=0x%016llx\n", count, hash_after_fold);

// Walk nodes to check what happened:
for (size_t i = 0; i < count; ++i) {
    dgtl_node_info_t info;
    dgtl_stream_get_node(s, i, &info);
    if (info.flags & DGTL_FLAG_DEAD) {
        printf("  Node %zu is dead after folding\n", i);
    }
    if (info.flags & DGTL_FLAG_FMA_FUSED) {
        printf("  Node %zu was fused\n", i);
    }
}

// ── Step 3: Conditionally run fusion ──
// Only fuse if we have enough MUL+ADD pairs to justify it
bool has_mul = false;
for (size_t i = 0; i < count; ++i) {
    dgtl_node_info_t info;
    dgtl_stream_get_node(s, i, &info);
    if (info.opcode == DGTL_OP_MUL || info.opcode == DGTL_OP_FMUL) {
        has_mul = true;
        break;
    }
}
if (has_mul) {
    dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_FUSION);
}

// ── Step 4: Conditionally run SIMD legalization ──
if (count > 16) {  // only worth vectorizing if enough nodes
    dgtl_legalize_simd(s, DGTL_SIMD_X86_AVX512);
    dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_SIMD);
}

// ── Step 5: Final pressure pass ──
dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_PRESSURE);

// ── Step 6: Inspect final state ──
uint64_t final_hash = dgtl_stream_logic_hash(s);
printf("Final: %zu nodes, hash=0x%016llx\n",
       dgtl_stream_node_count(s), final_hash);

// ── Step 7: Strike ──
dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);

dgtl_stream_destroy(s);
dgtl_context_destroy(ctx);
```

### The Sequencing Rules

| Rule | Detail |
|------|--------|
| **Profiles are composable** | Call `dgtl_optimize_with_profile()` multiple times with different profiles. Each runs only its passes. |
| **Order matters** | Run `FOLD` before `FUSION` (fusion needs folded constants). Run `SIMD` after `FUSION` (SIMD legalizer expects fused ops). Run `PRESSURE` last (register allocation is final). |
| **Inspect between passes** | Use `dgtl_stream_node_count()`, `dgtl_stream_logic_hash()`, and `dgtl_stream_get_node()` between profile calls to decide the next step. |
| **Skip passes freely** | Unlike `dgtl_optimize()` which runs all defaults, individual profile calls let you skip any pass. |
| **Don't call `dgtl_optimize()` AND profiles** | `dgtl_optimize()` runs the default pipeline. If you want manual control, use only `dgtl_optimize_with_profile()`. Mixing them may run passes twice. |
| **SIMD legalization is separate** | `dgtl_legalize_simd()` is its own call, not part of any profile. Call it explicitly when you want vectorization. |

### Recommended Sequencing Order

```
1. DGTL_OPT_PROFILE_FOLD        → Constant folding + propagation
2. DGTL_OPT_PROFILE_FUSION      → MUL+ADD→FMA, temporal fusion
3. dgtl_legalize_simd(s, arch)   → SIMD vectorization (target-aware)
4. DGTL_OPT_PROFILE_SIMD        → SIMD-specific optimizations
5. DGTL_OPT_PROFILE_PREDICATION → Branch→predication (HFT paths)
6. DGTL_OPT_PROFILE_PRESSURE    → Register allocation (always last)
```

You can skip any step. You can inspect after any step. You can conditionally branch based on node count, hash, or flag inspection.

### Comparison: Internal API vs SDK Chaining

| Capability | Internal API | SDK Profile Chaining |
|---|---|---|
| Run individual passes | ✅ Direct call | ✅ Via `dgtl_optimize_with_profile()` |
| Skip specific passes | ✅ Just don't call them | ✅ Just don't call that profile |
| Inspect IR between passes | ✅ Direct stream access | ✅ Via `dgtl_stream_get_node()` |
| Conditional pass execution | ✅ Any logic | ✅ Any logic |
| Sub-pass granularity (e.g., GVN without CSE) | ✅ Internal pass API | ❌ Profile runs all sub-passes in that group |
| Custom pass ordering | ✅ Arbitrary | ⚠️ Must follow recommended order for correctness |
| Transaction rollback between passes | ✅ Shadow buffer | ❌ Not exposed in SDK |

**Bottom line:** Profile chaining gives you ~90% of the internal API's sequencing power. The only gaps are sub-pass granularity (e.g., you can't run GVN without also running CSE within the `FOLD` profile) and transaction rollback between passes.

---

## New Files Required

**Zero.** All changes are edits to existing files:

| Existing File | Change Type |
|---|---|
| `src/main.cpp` | Edit — rewrite `emit_with_dgtl_backend_driver()` |
| `src/frontend/Lowering.cpp` | Edit — rewrite adaptation + payload + lowering logic |
| `include/myzon/frontend/Lowering.hpp` | Edit — swap internal types for SDK handles |
| `myzon.ps1` | Edit — include path, remove IR sources, add library link |

All other files — Lexer, Parser, AST, MIR, Token, all 13 semantic checkers — **do not touch DGTL** and stay exactly as-is. The DGTL boundary exists only in `Lowering.cpp` and `main.cpp`.
