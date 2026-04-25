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

# ARM64_RegAlloc.hpp

Source Header: include/dgtl/isa/regalloc/ARM64_RegAlloc.hpp
Last Synced: 2026-04-14 19:46:41

## Vision
This component exists to make the compiler/runtime pipeline explicit, safe, and predictable under high-performance constraints.

## Why This File Exists
- Provide reusable infrastructure used by the DGTL compilation and execution pipeline.
- Keep responsibilities localized so surrounding passes and backends can compose without hidden coupling.
- Preserve correctness with explicit guardrails before optimization gains.
- Backend lowering is explicit here to keep architecture behavior auditable and deterministic.
- Sentinel-style integrity checks are intentionally present to detect corruption before commit stages.

## Core Logic
- Inputs are validated early and non-actionable states return fast.
- Main logic operates on typed IR structures and explicit state transitions.
- Traversal is iterative to keep performance linear over stream and region size.
- Structural hashes are refreshed or verified to preserve correctness across rewrites.
- Transactional semantics are present: begin, mutate, validate, then commit or rollback.

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
- #include <array>
- #include <cstdint>
- #include <vector>
- #include <bit>
- #include <algorithm>
- #include "dgtl/ir/base/Global_IR.hpp"
- #include "../../analysis/dataflow/Liveness_Analyzer.hpp"

## Class and Struct Catalog
### Classes
- class DGTLARM64RegAlloc {
### Structs
- struct ARM64Regs {

## Enum Catalog
- none detected

## Function Catalog
- void execute(ir::IRStream& stream, const dgtl::analysis::LivenessAnalyzer& liveness) {
- [[nodiscard]] uint32_t find_and_occupy_reg(uint32_t result_id) noexcept {
- [[nodiscard]] uint32_t find_and_occupy_vec_reg(uint32_t result_id) noexcept {
- [[nodiscard]] uint8_t get_phys_reg(uint32_t virtual_id) const noexcept {
- [[nodiscard]] uint8_t get_phys_vec_reg(uint32_t virtual_id) const noexcept {
- void expire_dead_registers(uint32_t current_idx, const dgtl::analysis::LivenessAnalyzer& liveness) noexcept {

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
