# IR_Evolver.hpp

Source Header: include/dgtl/ir/evolution/IR_Evolver.hpp
Last Synced: 2026-04-14 19:46:40

## Vision
This component exists to make the compiler/runtime pipeline explicit, safe, and predictable under high-performance constraints.

## Why This File Exists
- Represent program semantics in compact, verifiable IR forms suitable for transformation and emission.
- Keep responsibilities localized so surrounding passes and backends can compose without hidden coupling.
- Preserve correctness with explicit guardrails before optimization gains.
- This file captures a focused subsystem contract so complexity does not leak across module boundaries.

## Core Logic
- Inputs are validated early and non-actionable states return fast.
- Main logic operates on typed IR structures and explicit state transitions.
- The design favors deterministic transformations and bounded side effects.

## Control Flow Graph and Map
```mermaid
flowchart TD
A[Inputs] --> B[Validate]
B --> C[Analyze]
C --> D[Transform]
D --> E[Finalize]
```

## Control Flow Narrative
- Entry points validate stream and state before any mutation or expensive work.

## Include Dependencies
- #include <vector>
- #include <memory>
- #include "dgtl/ir/base/IR_Types.hpp"
- #include "dgtl/ir/base/IR_Nodes.hpp"
- #include "dgtl/analysis/cost/AI_Cost_Model.hpp"

## Class and Struct Catalog
### Classes
- class IREvolver {

## Enum Catalog
- enum class MutationType {

## Function Catalog
- void mutate(dgtl::ir::IRNode* node);
- void specialize(dgtl::ir::IRNode* node, const dgtl::analysis::RuntimeFeedback& feedback);
- void apply_constant_folding(dgtl::ir::IRNode* node);
- void apply_instruction_fusion(dgtl::ir::IRNode* node);
- void apply_register_reassignment(dgtl::ir::IRNode* node);
- void apply_memory_layout_opt(dgtl::ir::IRNode* node);

## Design Decisions and Whys
- Explicit typing across IR nodes and flags reduces hidden semantic drift.
- Guard checks prevent invalid states from propagating into emitters and runtime paths.
- Layered modules allow policy evolution in pass and pipeline without backend rewrites.

## Critical Invariants
- Public operations should preserve deterministic behavior for equivalent inputs.

## Practical Reading Order
- Start with type and enum declarations.
- Follow entry functions for control-flow intent.
- Then inspect helper methods for safety and transformation details.
