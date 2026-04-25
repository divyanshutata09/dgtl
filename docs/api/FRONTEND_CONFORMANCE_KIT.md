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

# DGTL Frontend Conformance Kit

## Purpose
Define a standard test kit that every frontend (MYZON and future languages) must pass to claim DGTL compatibility.

## Test Categories

### 1. Payload Schema Compliance
Checks:
- Payload header version is valid.
- Required records are present and ordered correctly.
- Escaping/unescaping roundtrip works.
- Malformed payloads are rejected with clear diagnostics.

### 2. Lowering Contract Compliance
Checks:
- Every function terminates with return.
- Referenced value IDs are defined before use.
- Instruction kinds are supported by current DGTL capabilities.
- Call arity and signature constraints are validated.

### 3. Backend Emission Coverage
Checks per target:
- arm64 emission succeeds.
- x64 emission succeeds.
- nasm emission succeeds.
- wasm emission succeeds.
- elfobj emission succeeds (ELF .o with 6 sections).
- staticlib emission succeeds (GNU .a archive).
- coffobj emission succeeds (COFF .obj, IMAGE_FILE_MACHINE_AMD64).
- cofflib emission succeeds (COFF .lib with first linker member).
- macho emission succeeds (Mach-O ARM64 with LC_SEGMENT_64 + LC_SYMTAB).

### 4. Determinism
Checks:
- Same payload/options produce stable logic hash.
- Emitted textual backends remain byte-for-byte stable where expected.

### 5. Error Classification
Checks:
- Parse/load errors are distinct from lowering errors.
- Lowering errors are distinct from backend emit errors.
- Failure output is actionable and frontends can map it to user diagnostics.

## Mandatory Corpus
Each frontend should include fixtures for:
- constants and returns
- arithmetic chains
- function calls (including signature mismatch fixture)
- nested expression lowering
- unsupported construct fallback diagnostics

## Optional Performance Guardrails
- compile-time budget per fixture
- no-regression budget for hot-path payload ingestion
- no-regression budget for backend emission latency

## Suggested CI Flow
1. Frontend generates payload fixtures.
2. DGTL payload reader roundtrip validates fixtures.
3. DGTL driver emits all configured targets from fixtures.
4. Determinism and parity checks run.
5. Publish compatibility badge/report.

## Reporting Template
Required report fields:
- frontend_name
- frontend_version
- dgtl_version
- payload_version
- passed_targets
- failed_targets
- determinism_status
- known_limitations
