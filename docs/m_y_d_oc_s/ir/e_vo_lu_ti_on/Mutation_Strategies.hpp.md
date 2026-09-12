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

# Mutation_Strategies.hpp

Source Header: include/dgtl/ir/evolution/Mutation_Strategies.hpp
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
- #include <functional>
- #include "dgtl/ir/base/IR_Core.hpp"
- #include "dgtl/ir/base/IR_Core.hpp"
- #include "IR_Evolver.hpp"

## Class and Struct Catalog
### Classes
- class MutationStrategy {
- class ConstantFoldingStrategy : public MutationStrategy {
- class VectorizationStrategy : public MutationStrategy {
- class MemoryOptimizationStrategy : public MutationStrategy {
- class MutationStrategyRegistry {

## Enum Catalog
- none detected

## Function Catalog
- virtual bool applies_to(const dgtl::ir::IRNode* node) const = 0;
- virtual void apply(dgtl::ir::IRNode* node) = 0;
- virtual MutationType get_type() const = 0;
- bool applies_to(const dgtl::ir::IRNode* node) const override;
- void apply(dgtl::ir::IRNode* node) override;
- static MutationStrategyRegistry& get_instance();
- void register_strategy(std::unique_ptr<MutationStrategy> strategy);
- std::vector<MutationStrategy*> get_applicable_strategies(const dgtl::ir::IRNode* node) const;

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
