# Speculative_Alias_Hoister.hpp

Source Header: include/dgtl/pass/speculative/Speculative_Alias_Hoister.hpp
Last Synced: 2026-04-14 19:46:41

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
- #include <vector>
- #include <stdexcept>
- #include "dgtl/ir/base/Global_IR.hpp"
- #include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
- #include "dgtl/analysis/cost/AI_Cost_Model.hpp"

## Class and Struct Catalog
### Classes
- class DGTLSpeculativeAliasHoister {

## Enum Catalog
- none detected

## Function Catalog
- void execute(ir::IRStream& stream) {
- void hoist_speculative_invariants(ir::IRStream& loop_body, ir::IRStream& pre_header) {
- void sync_shadow_node(ir::IRStream& s, uint32_t idx, const ir::IRNode& node) {
- void inject_runtime_alias_guard_transactional(ir::IRStream& pre, const ir::IRNode& load_node) {
- void move_to_preheader_transactional(ir::IRStream& body, uint32_t body_idx, ir::IRStream& pre) {

## Design Decisions and Whys
- Explicit typing across IR nodes and flags reduces hidden semantic drift.
- Guard checks prevent invalid states from propagating into emitters and runtime paths.
- Layered modules allow policy evolution in pass and pipeline without backend rewrites.

## Critical Invariants
- Shadow integrity must validate before state is committed.
- Transaction boundaries must remain balanced across success and failure paths.
- Intent hash values must be consistent with opcode and operands after mutation.
- Null or invalid sentinel states must be short-circuited early.

## Practical Reading Order
- Start with type and enum declarations.
- Follow entry functions for control-flow intent.
- Then inspect helper methods for safety and transformation details.
