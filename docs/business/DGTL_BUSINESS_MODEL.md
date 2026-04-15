# DGTL Business Model

This document defines a practical commercial model for DGTL as a paid compiler/backend product.

## Goal

Monetize DGTL as high-value infrastructure IP rather than as a generic open-source project.

## Core Commercial Thesis

DGTL is most valuable when it reduces backend engineering time, improves performance, and gives teams deterministic multi-target code generation without requiring them to build a compiler backend from scratch.

The product is not just "a compiler framework." The product is:

- a multi-target backend engine
- a performance and optimization layer
- a validation and conformance workflow
- expert integration support

## Primary Customer Segments

### 1. Internal DSL teams

Organizations building internal domain-specific languages for rules engines, data pipelines, quant workflows, simulation, or AI orchestration.

Why they pay:

- they need code generation but do not want to build a backend team
- they need faster time to production
- they need multiple output targets later

### 2. HFT and low-latency infrastructure teams

Teams that care about determinism, latency, and backend control.

Why they pay:

- they value predictable generated output
- they care about custom optimization passes and backend behavior
- they are willing to pay for performance tuning and support

### 3. AI runtime and inference toolchain teams

Teams lowering graphs or DSL-like execution plans into native or portable targets.

Why they pay:

- they need a controllable lowering path
- they need WASM / x64 / ARM64 coverage
- they may need custom passes and backend adaptation

### 4. Embedded, edge, and specialized runtime vendors

Teams shipping software to ARM64, x64, or WASM targets with strong runtime constraints.

Why they pay:

- they want control over emitted code
- they need portability and predictability
- they may need vendor-specific backend extensions

## Revenue Model

## 1. Paid evaluation / pilot

Short-term commercial evaluation with limited scope.

Deliverables:

- evaluation access
- onboarding support
- one integration workshop
- one target/backend validation run

Why this matters:

- fastest path to first revenue
- validates willingness to pay
- qualifies serious buyers

## 2. Annual commercial license

Production license for internal commercial use.

Typical inclusions:

- production-use rights
- version updates for the contract term
- standard support window
- defined number of support hours or tickets

## 3. Enterprise license + support

Higher-value package for teams that need deeper partnership.

Typical inclusions:

- priority support
- custom backend or pass work
- roadmap input
- release validation help
- performance tuning and integration guidance

## 4. Custom engineering and optimization services

Professional services sold alongside licenses.

High-value service areas:

- frontend-to-DGTL lowering help
- backend customization
- target bring-up
- latency/performance tuning
- benchmarking and release validation

## Packaging Strategy

### Evaluation Tier

- internal evaluation only
- no redistribution
- limited support
- fixed evaluation term

### Commercial Tier

- production internal use
- standard updates
- standard support
- no sublicensing or redistribution unless explicitly licensed

### Enterprise Tier

- negotiated production rights
- tailored support and SLAs
- custom engineering access
- release and validation collaboration

## Pricing Direction

Exact pricing should be deal-based early, not self-serve.

Recommended early pricing structure:

- pilot fee
- annual license fee
- optional support / customization fee

Do not anchor the launch around low-cost public pricing. Early value comes from solving expensive backend problems for teams with real budgets.

## Go-To-Market Plan

### Phase 1: Prove paid demand

Focus on 2-3 pilot customers.

Goals:

- validate problem/solution fit
- learn which targets and passes matter most
- collect outcome-based proof points

### Phase 2: Productize the offer

Once pilots repeat, formalize:

- evaluation package
- commercial package
- enterprise package
- onboarding and support process

### Phase 3: Scale messaging and outbound

Target narrow buyer groups first:

- DSL teams
- HFT / quant infrastructure
- AI backend/runtime teams

## Sales Positioning

The customer is buying:

- reduced backend engineering time
- deterministic multi-target output
- optimization expertise
- integration confidence
- support from the engine author

They are not buying abstract compiler theory.

## Launch Readiness For Commercialization

Before paid outreach, DGTL should have:

- a stable maintainer workflow
- reproducible build and conformance runs
- one business-facing positioning page
- one commercial/evaluation terms draft
- one concise integration guide for backend consumers
- one or two benchmark or proof-of-value examples

## Recommended Initial License Direction

If the primary goal is monetization of DGTL itself, start with a proprietary commercial model.

This preserves pricing leverage while the market and product fit are still being discovered.