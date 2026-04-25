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

# DGTL Frontend Integration Spec

This note defines the intended boundary between an external language frontend and DGTL after the selective integration updates.

## Goal

Keep the frontend separate while making DGTL easier to target as a backend.

Frontend owns:

- lexing
- parsing
- AST
- semantic analysis
- language diagnostics
- source-language rules

DGTL owns:

- IR construction target
- optimization
- IR validation
- target emission
- JIT and binary orchestration

## New Boundary APIs

Use these headers for frontend integration:

- `include/dgtl/core/Function.hpp`
- `include/dgtl/core/ControlFlow.hpp`
- `include/dgtl/core/ABI.hpp`
- `include/dgtl/core/FrontendHelpers.hpp`

These are additive helpers. They do not replace `IRStream`, emitters, passes, or the existing validation model.

## Core Flow

1. Frontend creates one `DGTLFunction` per lowered function.
2. Frontend registers parameters and local slots through `DGTLFunction`.
3. Frontend lowers expressions through `FrontendLoweringContext` into the contained `IRStream`.
4. Frontend builds block structure with `DGTLControlFlowGraph` when lowering branches and merges.
5. Frontend records source metadata with `DGTLDebugStore`.
6. Frontend optionally registers call signatures in `DGTLABIRegistry` for internal and foreign calls.
7. DGTL optimization and emission proceed on the produced `IRStream`.

## Metadata Contract

`DGTLDebugStore` now carries two parallel metadata channels:

- `SourceLoc`: line, column, file id
- `TypeLoc`: type tag, ownership flags, nullability flags, auxiliary flags

This metadata is side-channel only. It does not alter the existing IR node layout.

Recommended usage:

- `type_tag`: frontend-defined primitive or semantic type id
- `ownership_flags`: frontend-defined ownership or borrowing flags
- `nullability_flags`: frontend-defined nullability state
- `aux_flags`: reserved for qualifiers like generic specialization or effect markers

## ABI Contract

`DGTLABIRegistry` and `DGTLCallSignature` provide a stable place to describe calls without forcing that information into the raw IR layout.

Use ABI metadata for:

- internal function signatures
- foreign bindings
- calling convention tags
- argument count validation
- return-value classification

The current IR call emission remains intentionally small. ABI metadata exists so the frontend and backend can agree on call meaning without weakening DGTL core semantics.

## Control-Flow Contract

`DGTLControlFlowGraph` is a lowering helper, not a replacement CFG pass framework.

Use it to:

- create and label logical blocks
- connect predecessor and successor edges
- seal blocks at finalized node indices
- insert simple phi nodes at merge points

This keeps structured lowering outside the optimizer while avoiding fully ad hoc branch construction in each frontend.

## Recommended Frontend Strategy For MYZON

For MYZON or a similar language:

1. Keep lexer, parser, AST, and typing fully outside DGTL.
2. Lower one source function at a time into `DGTLFunction`.
3. Carry ownership and nullability through `TypeLoc` side metadata.
4. Lower advanced features like async, channels, and policy semantics initially as runtime calls plus metadata.
5. Let DGTL stay backend-first.

## Non-Goals

This integration layer does not attempt to make DGTL into:

- a parser generator
- an AST framework
- a language-specific semantic analyzer
- a frontend diagnostics engine

That separation is intentional.