# DGTL Proposal For HFT And Low-Latency Infrastructure Teams

## Proposal Overview

DGTL is a commercial multi-target backend and optimization engine for teams that need deterministic output, explicit backend behavior, and commercially supported integration for performance-critical systems.

This version of the proposal is tailored for HFT and low-latency infrastructure teams evaluating DGTL as a backend and optimization layer for internal DSLs, strategy engines, execution logic, or latency-sensitive runtime components.

## Prepared For

- Customer:
- Primary contact:
- Team / business unit:
- Proposal date:
- Proposal version:

## Executive Summary

HFT and low-latency teams typically care less about generic compiler breadth and more about:

- deterministic emitted output
- tight control over backend behavior
- repeatable validation
- shorter path from internal strategy/runtime logic to target-specific output

DGTL is designed to support that operating model.

DGTL helps teams lower internal representations into DGTL IR, apply backend and optimization passes, and emit validated output for targets such as ARM64, x64, NASM, and WASM.

For HFT use cases, DGTL should be evaluated first through a narrowly scoped paid pilot focused on one representative workload and one primary target.

## Customer Need

### Typical HFT Situation

- internal execution or strategy logic already exists
- frontend or DSL representation exists or is in progress
- backend code generation is either manual, fragile, or expensive to extend
- deterministic output and regression control matter more than ecosystem size

### Typical Pain Points

- backend engineering slows strategy/tooling delivery
- validation of emitted behavior is difficult to keep reproducible
- teams need direct control over optimization and emission decisions
- extending to additional targets increases cost and complexity

### Desired Outcome

- lower an internal representation into DGTL IR
- emit deterministic target output for one agreed backend
- validate conformance and smoke checks reproducibly
- decide whether DGTL is suitable for production deployment or custom extension

## Why DGTL For HFT Teams

### Deterministic validation flow

DGTL includes explicit conformance and strict build flows intended to make backend regression detection easier.

### Backend control

DGTL is positioned as a focused backend engine rather than a broad language platform, which is a better fit for teams that need target-specific control.

### Faster backend iteration

Instead of growing an internal backend stack from zero, teams can adopt DGTL and focus on the logic that differentiates their trading or execution systems.

### Commercial support

HFT teams can buy direct integration support, backend tuning, and validation help from the engine author.

## Recommended Engagement

## Option A: HFT Pilot

Recommended first step.

### Scope

- Duration: 6 weeks
- One representative low-latency or execution-related workload
- One primary backend target, typically x64 or NASM
- One designated customer engineering team

### Deliverables

- DGTL evaluation access
- onboarding and architecture review
- frontend-to-DGTL lowering review
- backend validation flow for the chosen target
- conformance runbook for the target use case
- final findings, risk notes, and production recommendation

### Success Criteria

- customer can lower a representative workload into DGTL IR
- DGTL emits agreed target output successfully
- conformance and smoke flow run reproducibly
- customer can judge operational fit for production use

### Commercials

- Suggested pilot fee: USD 20,000 to USD 30,000
- Suggested duration: 6 weeks

## Option B: Commercial License

Recommended if pilot results are positive and the customer wants internal production use.

### Includes

- internal production-use rights
- source access under commercial terms
- standard updates during contract term
- standard support window

### Commercials

- Suggested annual fee: USD 25,000 to USD 75,000

## Option C: Enterprise / Strategic Infrastructure Engagement

Recommended for teams requiring custom backend work, dedicated support, or deeper deployment collaboration.

### Includes

- production commercial rights
- priority support
- custom engineering block
- target/backend customization support
- release and validation collaboration

### Commercials

- Suggested annual fee: USD 100,000+
- Additional services fee as scoped

## Scope Boundaries

Unless explicitly licensed, this proposal does not include:

- redistribution rights
- sublicensing rights
- OEM rights
- unlimited engineering support
- public benchmark publication rights

## Delivery Plan

### Phase 1: Technical scoping

- confirm workload shape
- confirm target backend
- confirm validation constraints and success criteria

### Phase 2: Pilot execution

- review lowering approach
- run backend validation and conformance flow
- identify integration or optimization issues

### Phase 3: Production recommendation

- summarize technical findings
- document production risks and next steps
- recommend commercial or enterprise conversion path

## Customer Responsibilities

- provide representative workload or internal IR shape
- provide target/runtime and performance constraints
- designate a technical owner for review and feedback
- participate in milestone checkpoints

## DGTL Responsibilities

- provide agreed access scope
- support the target validation plan
- provide findings and recommended rollout path

## Recommendation

For HFT and low-latency teams, the recommended starting point is a narrow paid pilot on one representative workload and one primary target backend. That approach creates fast technical clarity without forcing a broad platform commitment too early.

## Acceptance

- Customer representative:
- Title:
- Date:

- DGTL representative:
- Title:
- Date: