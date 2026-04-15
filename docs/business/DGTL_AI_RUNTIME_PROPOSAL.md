# DGTL Proposal For AI Runtime And Execution Teams

## Proposal Overview

DGTL is a commercial multi-target backend and optimization engine for teams building AI runtimes, graph execution systems, operator lowering pipelines, and specialized execution backends.

This version of the proposal is tailored for teams that need a controllable lowering path from internal execution graphs or typed IR into native or portable backend output.

## Prepared For

- Customer:
- Primary contact:
- Team / business unit:
- Proposal date:
- Proposal version:

## Executive Summary

AI runtime teams often reach a point where they need more control over lowering, optimization, and target emission than generic runtime stacks provide.

Typical needs include:

- controlled lowering from graph or operator IR
- deterministic backend behavior
- portable and native target coverage
- support for backend validation before production deployment

DGTL gives those teams a focused backend layer that can be used to lower internal plans into DGTL IR, run backend passes, and emit validated output across ARM64, x64, NASM, and WASM.

The recommended first step is a tightly scoped pilot on one representative graph or execution workload and one primary target.

## Customer Need

### Typical AI Runtime Situation

- graph or execution planning layer already exists
- operators or typed execution nodes already exist internally
- backend codegen and optimization path is incomplete, rigid, or expensive to extend
- target support needs to expand across native and portable outputs

### Typical Pain Points

- internal lowering path becomes hard to scale
- target-specific output is difficult to control cleanly
- regression detection for code generation is weak
- backend customization takes too much time away from runtime/product work

### Desired Outcome

- lower one representative graph or operator workload into DGTL IR
- emit deterministic output for one target backend
- validate conformance and repeatable backend behavior
- decide whether DGTL is suitable as a backend layer for broader execution workloads

## Why DGTL For AI Runtime Teams

### Controlled lowering path

DGTL gives teams a focused backend target for internal IR or execution-plan lowering.

### Multi-target flexibility

DGTL can support evaluation across native and portable targets, which is valuable when runtime deployment environments vary.

### Deterministic validation

DGTL includes explicit conformance and strict build flows intended to support repeatable backend behavior.

### Commercial support

AI runtime teams can buy integration help, pass tuning, and backend customization support directly from the engine author.

## Recommended Engagement

## Option A: AI Runtime Pilot

Recommended first step.

### Scope

- Duration: 6 to 8 weeks
- One representative graph or execution workload
- One primary backend target, often WASM, x64, or ARM64
- One designated customer engineering team

### Deliverables

- DGTL evaluation access
- architecture and lowering review
- graph/IR-to-DGTL integration review
- backend validation workflow for the chosen target
- conformance runbook for the selected workload
- final findings and rollout recommendation

### Success Criteria

- customer can lower a representative graph or execution workload into DGTL IR
- DGTL emits agreed target output successfully
- conformance workflow runs reproducibly
- customer can assess fit for broader runtime/backend adoption

### Commercials

- Suggested pilot fee: USD 20,000 to USD 30,000
- Suggested duration: 6 to 8 weeks

## Option B: Commercial License

Recommended when the pilot validates internal production use.

### Includes

- internal production-use rights
- source access under commercial terms
- standard updates during contract term
- standard support window

### Commercials

- Suggested annual fee: USD 25,000 to USD 75,000

## Option C: Enterprise Runtime Engagement

Recommended for teams needing backend customization, target bring-up, or strategic support.

### Includes

- production commercial rights
- priority support
- custom engineering block
- backend and pass customization support
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

- confirm graph/workload scope
- confirm target backend
- confirm validation and deployment constraints

### Phase 2: Pilot execution

- review lowering approach
- validate backend emission flow
- run conformance and technical checks

### Phase 3: Production recommendation

- summarize technical findings
- identify scaling risks and next steps
- recommend commercial or enterprise conversion path

## Customer Responsibilities

- provide representative graph or execution workload
- provide target/runtime constraints
- designate technical owner for feedback and checkpoints
- participate in milestone reviews

## DGTL Responsibilities

- provide agreed access scope
- support the target validation plan
- provide findings and recommended rollout path

## Recommendation

For AI runtime teams, the best initial path is a narrow pilot centered on one graph/workload and one target backend, followed by commercial expansion only after fit is demonstrated.

## Acceptance

- Customer representative:
- Title:
- Date:

- DGTL representative:
- Title:
- Date: