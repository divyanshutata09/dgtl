# IR_Traits.hpp

Source Header: include/dgtl/ir/base/IR_Traits.hpp
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
- Dispatch logic uses explicit switching to keep opcode or policy behavior transparent.
- The design favors deterministic transformations and bounded side effects.

## Control Flow Graph and Map
```mermaid
flowchart TD
A[Inputs] --> B[Validate]
B --> C[Analyze]
C --> D[Transform]
D --> E[Finalize]
C --> S[Opcode or Policy Dispatch Switch]
S --> D
```

## Control Flow Narrative
- Entry points validate stream and state before any mutation or expensive work.
- Dispatch branches by opcode or strategy to keep behavior explicit and testable.

## Include Dependencies
- #include <cstdint>
- #include <bit>
- #include <type_traits>
- #include "IR_Types.hpp"
- #include "IR_Nodes.hpp"

## Class and Struct Catalog
### Structs
- struct IRTraits {

## Enum Catalog
- none detected

## Function Catalog
- [[nodiscard]] static constexpr bool is_branch(OpCode op) noexcept {
- [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
- [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
- [[nodiscard]] static constexpr bool has_side_effects(OpCode op, uint16_t flags) noexcept {
- [[nodiscard]] static constexpr bool is_simd(OpCode op) noexcept {
- [[nodiscard]] static constexpr bool is_valid_slot(uint32_t slot_id) noexcept {
- [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {

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
