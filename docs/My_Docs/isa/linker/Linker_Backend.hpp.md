# Linker_Backend.hpp

Source Header: include/dgtl/isa/linker/Linker_Backend.hpp
Last Synced: 2026-04-14 19:46:41

## Vision
This component exists to make the compiler/runtime pipeline explicit, safe, and predictable under high-performance constraints.

## Why This File Exists
- Lower IR into target-specific representation while preserving semantic correctness and backend constraints.
- Keep responsibilities localized so surrounding passes and backends can compose without hidden coupling.
- Preserve correctness with explicit guardrails before optimization gains.
- This file captures a focused subsystem contract so complexity does not leak across module boundaries.

## Core Logic
- Inputs are validated early and non-actionable states return fast.
- Main logic operates on typed IR structures and explicit state transitions.
- Traversal is iterative to keep performance linear over stream and region size.
- Dispatch logic uses explicit switching to keep opcode or policy behavior transparent.
- The design favors deterministic transformations and bounded side effects.

## Control Flow Graph and Map
```mermaid
flowchart TD
A[Inputs] --> B[Validate]
B --> C[Analyze]
C --> D[Transform]
D --> E[Finalize]
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

## Include Dependencies
- #include <fcntl.h>
- #include <sys/types.h>
- #include <sys/stat.h>
- #include <vector>
- #include <string_view>
- #include <cstring>
- #include <cstdint>
- #include <iostream>
- #include <windows.h>
- #include <io.h>
- #include <sys/mman.h>
- #include <unistd.h>
- #include <elf.h>
- #include "dgtl/ir/base/Global_IR.hpp"

## Class and Struct Catalog
### Classes
- class DGTLLinker {
- class DGTLUniversalLinker : public DGTLLinker {
### Structs
- struct Elf64_Ehdr {
- struct Elf64_Phdr {

## Enum Catalog
- none detected

## Function Catalog
- inline void write_leb128(std::vector<uint8_t>& buf, uint64_t value) {
- void emit_elf64(std::string_view path, const ir::IRStream& ir_stream) {
- void emit_machine_code(uint8_t* dest, const ir::IRStream& stream) {
- void emit_wasm(std::string_view path, const ir::IRStream& stream) {

## Design Decisions and Whys
- Explicit typing across IR nodes and flags reduces hidden semantic drift.
- Guard checks prevent invalid states from propagating into emitters and runtime paths.
- Layered modules allow policy evolution in pass and pipeline without backend rewrites.

## Critical Invariants
- Null or invalid sentinel states must be short-circuited early.

## Practical Reading Order
- Start with type and enum declarations.
- Follow entry functions for control-flow intent.
- Then inspect helper methods for safety and transformation details.
