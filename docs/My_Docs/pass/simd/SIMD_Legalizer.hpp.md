# SIMD_Legalizer.hpp

Source Header: include/dgtl/pass/simd/SIMD_Legalizer.hpp
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
- Dispatch logic uses explicit switching to keep opcode or policy behavior transparent.
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
C --> S[Opcode or Policy Dispatch Switch]
S --> D
B --> X[Guard and Early Exit]
```

## Control Flow Narrative
- Entry points validate stream and state before any mutation or expensive work.
- Processing is mainly loop-driven to walk nodes, regions, or dependency structures.
- Dispatch branches by opcode or strategy to keep behavior explicit and testable.
- Mutation paths are transactional to ensure safe rollback on validation failure.

## Include Dependencies
- #include <immintrin.h>
- #include <algorithm>
- #include <cstdio>
- #include "dgtl/ir/base/Global_IR.hpp"

## Class and Struct Catalog
### Classes
- class DGTLSIMDLegalizer {

## Enum Catalog
- enum class TargetArch : uint8_t { X86_AVX512, ARM_NEON, WASM_SIMD128, GENERIC };

## Function Catalog
- void legalize(ir::IRStream& stream) {
- void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) {
- [[nodiscard]] bool is_vectorizable_quad(const ir::IRStream& stream, uint32_t start) const noexcept {
- void fuse_to_simd_v4_transactional(ir::IRStream& stream, uint32_t start) {
- bool fuse_across_nodes(std::vector<ir::IRNode*>& nodes) {

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
