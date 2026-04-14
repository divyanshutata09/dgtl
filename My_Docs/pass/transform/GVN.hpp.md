# GVN.hpp

Source Header: include/dgtl/pass/transform/GVN.hpp
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
- #include <vector>
- #include <memory>
- #include <cstdint>
- #include <cstring>
- #include <algorithm>
- #include <iostream>
- #include <unordered_map>
- #include <immintrin.h>
- #include <mutex>
- #include "dgtl/ir/base/Global_IR.hpp"

## Class and Struct Catalog
### Classes
- class DGTLGVN {
### Structs
- struct alignas(16) ValueKey {
- struct Entry {

## Enum Catalog
- none detected

## Function Catalog
- explicit DGTLGVN(size_t max_ids = 1048576) {
- void execute(ir::IRStream& stream) {
- inline void map_id(uint32_t from, uint32_t to) {
- inline uint32_t resolve(uint32_t id) {
- static bool is_zero(ir::IRStream& s, uint32_t id, size_t upto) {
- static ir::IRNode make_mov(uint32_t dst, uint32_t src) {
- static ir::IRNode make_nop(uint32_t id) {
- static bool is_commutative(ir::OpCode op) noexcept {
- uint32_t lookup_or_insert(const ValueKey& key, uint32_t id) {
- static uint64_t mix_h1(const ValueKey& k) noexcept {
- static uint64_t mix_h2(const ValueKey& k) noexcept {

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
