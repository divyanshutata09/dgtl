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
- dgtl/api/Frontend.hpp
- dgtl/api/Backend_Driver_API.hpp
- dgtl/api/Frontend_Pipeline.hpp
- dgtl/api/Frontend_Payload.hpp

Public API goals:
- Stable include paths for frontend integrations.
- Clear separation from internal/compiler-core implementation layout.
- Safer upgrades without forcing frontend code rewrites.
- 9 backend targets: arm64, x64, nasm, wasm, elfobj, staticlib, coffobj, cofflib, macho.
- JIT execution via StrikeOrchestrator (`--jit-execute` with `--backend x64`).

Frontend pipeline guidance:
- Build language AST/MIR in your frontend.
- Translate to FrontendLoweringFunction via dgtl/api/Frontend_Pipeline.hpp.
- Serialize FrontendPayloadModule via dgtl/api/Frontend_Payload.hpp.
- Let DGTL handle backend optimization and multi-target emission.

Notes:
- Existing dgtl/core headers remain available for compatibility and internal use.
- New integrations should prefer dgtl/api/* includes.
