# Loop_Unswitcher.hpp

Source Header: include/dgtl/pass/transform/Loop_Unswitcher.hpp
Last Synced: 2026-04-14 19:46:42

## Vision
This component exists to make the compiler/runtime pipeline explicit, safe, and predictable under high-performance constraints.

## Why This File Exists
- Apply correctness-preserving transformations to improve cost, latency, and code quality.
- Keep responsibilities localized so surrounding passes and backends can compose without hidden coupling.
- Preserve correctness with explicit guardrails before optimization gains.
- Transformation safety is bounded here through checks that preserve IR validity and intent.
- Sentinel-style integrity checks are intentionally present to detect corruption before commit stages.

## Core Logic
- Inputs are validated early and non-actionable states return fast.
- Main logic operates on typed IR structures and explicit state transitions.
- Traversal is iterative to keep performance linear over stream and region size.
- Structural hashes are refreshed or verified to preserve correctness across rewrites.
- Transactional semantics are present: begin, mutate, validate, then commit or rollback.
- Defensive error paths are explicit to prevent silent corruption propagation.

## Control Flow Graph and Map
```mermaid
flowchart TD
A[Inputs] --> B[Validate]
B --> C[Begin Transaction]
C --> D[Mutate Shadow/Core]
D --> E[Commit or Rollback]
C --> L[Iterative Walk Over IR or Regions]
L --> D
B --> X[Guard and Early Exit]
```

## Control Flow Narrative
- Entry points validate stream and state before any mutation or expensive work.
- Processing is mainly loop-driven to walk nodes, regions, or dependency structures.
- Mutation paths are transactional to ensure safe rollback on validation failure.

## Include Dependencies
- #include <algorithm>
- #include "dgtl/ir/base/Global_IR.hpp"
- #include "dgtl/analysis/cost/AI_Cost_Model.hpp"
- #include "dgtl/pass/speculative/Speculative_DCE.hpp"
- #include "dgtl/core/Verified_Formal.hpp"

## Class and Struct Catalog
### Classes
- class DGTLLoopUnswitcher {

## Enum Catalog
- none detected

## Function Catalog
- void unswitch_loop(ir::IRStream& stream) {
- bool is_loop_invariant(uint32_t cond_reg, const ir::IRStream& s) const noexcept {
- bool perform_shadow_versioning(ir::IRStream& stream, uint32_t branch_idx) {

## Design Decisions and Whys
- Explicit typing across IR nodes and flags reduces hidden semantic drift.
- Guard checks prevent invalid states from propagating into emitters and runtime paths.
- Layered modules allow policy evolution in pass and pipeline without backend rewrites.

## Critical Invariants
- Shadow integrity must validate before state is committed.
- Transaction boundaries must remain balanced across success and failure paths.
- Intent hash values must be consistent with opcode and operands after mutation.

## Practical Reading Order
- Start with type and enum declarations.
- Follow entry functions for control-flow intent.
- Then inspect helper methods for safety and transformation details.
