/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

# DGTL Public API

This folder defines the stable public integration surface for external language frontends and tools.

Use these headers from consumers:
- dgtl/api/Api_all.hpp
- dgtl/api/ABI.hpp
- dgtl/api/ABI_Lowering.hpp
- dgtl/api/Config.hpp
- dgtl/api/ExecutionEngine.hpp
- dgtl/api/Session.hpp
- dgtl/api/Session.hpp
- dgtl/api/Session.hpp
- dgtl/api/Runtime.hpp
- dgtl/api/IRBuilder.hpp
- dgtl/api/Backend_Driver_Shared_Utils.hpp
- dgtl/api/Backend_Driver_API.hpp

Public API goals:
- Stable include paths for frontend integrations.
- Clear separation from internal/compiler-core implementation layout.
- Safer upgrades without forcing frontend code rewrites.
- 9 backend targets: arm64, x64, nasm, wasm, elfobj, staticlib, coffobj, cofflib, macho.
-  execution via StrikeOrchestrator (`---execute` with `--backend x64`).

Frontend pipeline guidance:
- Build language AST/MIR in your frontend.
- Lower frontend IR into dgtl::ir::IRStream through dgtl/api/IRBuilder.hpp.
- Select execution strategy via dgtl/api/ExecutionEngine.hpp or session APIs.
- Use dgtl/api/Runtime.hpp for scheduler/task integration.
- Let DGTL handle optimization, lowering, / emission, and target specialization.

## IRBuilder Facade Contract
- IRBuilder is the client-facing facade for IR construction.
- Prefer typed facade methods (`emit_*`, typed enums) over direct opcode use.
- Use `emit_generated("OpName", ...)` for full metadata-driven .dgtl_def coverage.
- `emit_generated` supports variadic operand overflow, extra result IDs, and effect sidecars.

## ABI Layering Contract
- dgtl/api/ABI.hpp exposes the ABI contract and validation layer.
- dgtl/api/ABI_Lowering.hpp exposes the target ABI physical placement layer.
- Validation/migration policy must live in the ABI contract layer, not in emitters.
- Register/stack placement must come from ABI lowering outputs, not ad hoc backend rules.

## Staff-Level Architectural Guarantees (v1.1.0+)
- **Cache-Line Coherency**: Core API engines (`Session`, `Session`) are enforced `alignas(64)` to strictly eliminate false sharing on modern CPU L1 caches.
- **Zero-Cost Dispatch**: Abstract boundaries implement `final` sealing to allow the compiler to devirtualize hot-path invocations. 
- **Configuration Parsing**: Execution policies inside `Config.hpp` evaluate completely via `constexpr` guarantees.
- **Thread Safety Constraint**: API objects (e.g., `IRBuilder`, `ExecutionEngine`) are thread-compatible but intentionally **not** thread-safe by default to avoid mutex overhead. Wrap with external locks or `std::shared_mutex` for multi-threaded access.