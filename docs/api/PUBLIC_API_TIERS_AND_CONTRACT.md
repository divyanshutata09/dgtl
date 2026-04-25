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

# DGTL Public API Tiers and Contract

## Purpose
This document defines a stable API architecture for DGTL as a language-agnostic compiler platform.

Goals:
- Preserve universality for all frontends (MYZON and future languages).
- Prevent accidental coupling to DGTL internals.
- Allow DGTL internals to evolve without breaking frontend integrations.

## Tier Model

### Tier A: Stable Frontend Platform API
Audience:
- Language frontend authors.
- Tooling that compiles language-specific IR into DGTL.

Guarantees:
- Semantic versioning compatibility promises.
- Backward compatibility across minor and patch releases.
- Major release required for breaking changes.

Scope:
- Frontend payload schema and loader API.
- Frontend lowering contract data model.
- Frontend-to-driver orchestration helpers.
- Backend target selection and emission policy controls.
- Capability discovery API.
- Diagnostics and structured error results.

Current anchors in repository:
- include/dgtl/api/Frontend_Pipeline.hpp
- include/dgtl/api/Frontend_Payload.hpp
- include/dgtl/api/Backend_Driver_API.hpp
- include/dgtl/api/Frontend.hpp
- include/dgtl/api/Api_all.hpp

### Tier B: Advanced Power API (Opt-in)
Audience:
- Performance teams and advanced compiler integrators.

Guarantees:
- Best-effort compatibility.
- Changes allowed in minor releases with migration notes.
- Feature flags and capability checks required.

Scope:
- Direct pass/profile policy objects.
- Explicit optimization tuning knobs.
- Extended target and scheduling controls.

Rules:
- Must be clearly namespaced and documented as Tier B.
- Must not be transitively required by Tier A consumers.

### Tier C: Internal API
Audience:
- DGTL core maintainers only.

Guarantees:
- No stability guarantee.
- May change at any time.

Scope:
- Internal pass graph shape.
- Internal data-structure layouts.
- Experimental APIs and implementation details.

## Tier A Contract Surface

Tier A contract is centered on language-agnostic payload ingestion and standardized backend execution.

### A1. Frontend Lowering Contract
Type families:
- FrontendLoweringFunction
- FrontendLoweringInstruction
- FrontendLoweringParameter
- FrontendInstructionKind

Contract requirements:
- Value IDs are frontend-owned SSA-like identifiers.
- Type handles are opaque frontend-provided IDs.
- Call instructions carry callee name plus argument value IDs.
- Function must produce a terminating return instruction.

### A2. Universal Payload Serialization
API:
- write_frontend_payload_file(...)
- read_frontend_payload_file(...)

Contract requirements:
- Versioned payload header.
- Deterministic serialization for identical input model.
- Strict parser diagnostics on malformed payloads.
- Escape/unescape rules stable across versions.

### A3. Driver Ingestion and Emission
API:
- FrontendEmitRequest
- emit_frontend_module(...)
- make_backend_driver_request(...)
- validate_backend_driver_request(...)

Driver flags:
- --frontend-payload <path>
- --backend <arm64|x64|nasm|wasm|elfobj|staticlib|coffobj|cofflib|macho>
- --jit-execute (x64 only, requires DGTL_STRIKE_EXECUTE=1)
- --emit-output <path>

Contract requirements:
- Same optimization/emission pipeline for payload and non-payload mode.
- No frontend-specific grammar logic inside DGTL driver.
- Backend determinism remains invariant for identical payload and options.

### A4. Diagnostics Contract
Requirements:
- User-visible message for all hard failures.
- Machine-consumable status object for programmatic integrations.
- Failure class must distinguish parse/load/lower/emit failures.

### A5. Capability Negotiation Contract
DGTL must expose a queryable capability model (next implementation phase):
- supported_frontend_payload_version
- supported_instruction_kinds
- max_call_arity
- backend_targets
- optional_optimization_profiles

Frontends must:
- Query capabilities before payload generation.
- Downgrade or reject unsupported constructs with explicit diagnostics.

## Non-Goals for Tier A
- Exposing all pass internals.
- Exposing all internal headers as stable API.
- Guaranteeing stable binary ABI for all C++ symbols across compilers.

## Design Principle
Tier A is the platform contract.
Tier B is expert mode.
Tier C is implementation territory.
