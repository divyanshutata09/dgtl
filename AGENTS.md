Set-Location "D:\TataG\dgtl"

@'
# AGENTS.md — DGTL Sovereign Backend Runtime Governance & Token Optimization Contract
# Workspace: D:\TataG\dgtl
# File Extensions: .ixx (C++ Modules/FileCheck), .cpp (Impl), .h (Headers), .inl (Inlines)

================================================================================
SECTION 0: EXECUTION CEILINGS & CIRCUIT BREAKERS (TOKEN PRESERVATION)
================================================================================
1. TOOL CALL BUDGET: An agent MUST NOT execute more than 3 tool calls in a single turn.
   If unresolved after 3 calls, the agent MUST pause and ask the operator for direction.
2. READ WINDOW CEILING: Never issue a whole-file read on files exceeding 100 lines.
   Reads on `.ixx`, `.cpp`, `.h`, or `.inl` files MUST be line-ranged (target line ±30 lines).
   Massive bridges (`dgtl_adapter.cpp`, `hir_function_lowerer.cpp`, `dgtl_main.cpp`) MUST NEVER be read in full.
3. WORKSPACE LOCALITY: All operations MUST execute strictly inside `D:\TataG\dgtl`.
   Probing `C:\`, user directories, or parent folders is STRICTLY FORBIDDEN.
4. ZERO AUTONOMOUS TERMINAL WRITES: The agent SHALL NOT execute build commands
   (`cmake`, `ninja`, `ctest`, benchmark runs) or Git operations (`reset`, `commit`, `stash`).
   The human operator retains sole execution authority.

================================================================================
SECTION 1: CONTEXT INGESTION & SEARCH BOUNDARIES
================================================================================
1. TURN CACHE FIRST: The agent MUST draw definitions from current conversation context
   before inspecting disk. Rereading already loaded context is PROHIBITED.
2. REPOSITORY EXCAVATION FORBIDDEN:
   - Glob/recursive directory traversals are PROHIBITED.
   - Codebase-wide regex/grep queries are PROHIBITED.
   - Preemptive loading of entire `.h`, `.inl`, or `.ixx` files is PROHIBITED.
3. SURGICAL SYMBOL RESOLUTION:
   When an IR node, pass definition, or bridge signature is missing:
   - Tier 1: Infer strictly from the immediate call site or pipeline invariant.
   - Tier 2: Read ONLY the exact declaration block in the owning `.h` or `.inl` file.
   - Tier 3: If resolution requires >2 file hops, HALT and request definition from operator.

================================================================================
SECTION 2: DGTL ARCHITECTURAL & COMPILER INVARIANTS
================================================================================
1. SOVEREIGN & BACKWARD-COMPATIBLE:
   - DGTL is 100% language-agnostic. No language-specific hacks in core IR passes.
   - NO DOWNGRADES POLICY: Existing backend functionality, optimizations, and C ABI
     contracts (`dgtl.h`) must never be degraded or broken. Only additive enhancements allowed.
2. FIVE-TIER CANONICAL TEST TAXONOMY:
   Test artifacts belong strictly in one canonical category:
   - `tests/unit/` (In-memory verification across ir/, semantic/, codegen/)
   - `tests/lit_filecheck/` (Declarative pass validation via FileCheck.ixx for dce/, gvn/, fusion/)
   - `tests/functional/` (Multi-pass lowering & stages 00–36 JIT runs)
   - `tests/conformance_sdk/` (Public C ABI round-trip contracts & CLI entry points)
   - `tests/benchmarks/` (Throughput/latency audits—NEVER run by agent)
3. MEMORY & DATA-ORIENTED DESIGN (DOD):
   - Zero-indirection subsystems: `Arena`, `VirtualAlloc`/`mmap`, explicit sequential `IR_Stream`.
   - Polymorphic inheritance, virtual methods, and heap allocations (`new`, `malloc`,
     `std::unique_ptr`, `std::shared_ptr`) in the IR transformation pipeline are FORBIDDEN.
4. TARGET CODEGEN & .rodata:
   - Direct hardware emitters for `x64`, `arm64`, and explicit SIMD lanes.
   - Binary literals in `.rodata` MUST be 64-byte hardware cache line aligned with RIP-relative relocations.

================================================================================
SECTION 3: ERROR DIAGNOSTIC TRIAGE
================================================================================
When compiler or test errors are supplied:
1. Isolate the PRIMARY diagnostic line (file, line number, error code).
2. NEVER ingest secondary template traces, cascading compiler warnings, or full terminal logs.
3. Do not attempt to fix multiple unconfirmed compiler errors simultaneously.

================================================================================
SECTION 4: TOKEN-OPTIMIZED OUTPUT SPECIFICATION
================================================================================
1. NO CONVERSATIONAL PROSE: Eliminate intros, pleasantries, recaps, and filler.
2. NO WHOLE-FILE GENERATION: Emitting complete files is STRICTLY PROHIBITED.
3. OUTPUT FORMAT: All code modifications MUST follow this dense schema:

### Root Cause
<1 sentence explaining the broken invariant>

### Patch
```diff
--- a/path/to/file.cpp (or .ixx, .h, .inl)
+++ b/path/to/file.cpp (or .ixx, .h, .inl)
@@ -line,count +line,count @@
- [removed code]
+ [added code]