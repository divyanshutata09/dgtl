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

# DGTL Deep Technical Response Policy

## Goal

Answer expert-level questions credibly while protecting proprietary implementation details.

## Response Principles

1. Explain the algorithm class, not internal source code.
2. Provide rationale and performance intent.
3. Use benchmark evidence where possible.
4. Avoid pass internals, heuristic constants, and implementation-specific code paths.

## Allowed Disclosure

- architecture-level design decisions
- algorithm family (for example, graph-coloring allocator)
- backend-specific constraints at a conceptual level
- measured outcomes and test methodology

## Protected Disclosure

- exact implementation of lowering passes
- proprietary scheduling heuristics
- private tuning tables and thresholds
- internal source snippets and call-graph specifics

## Standard Answer Pattern

Use this structure:

1. General mechanism
2. Why it is used
3. Observable behavior in output or benchmarks
4. Proprietary boundary statement

Example pattern:

"We use a custom allocator strategy in the graph-coloring family with backend-aware pressure heuristics to reduce spill costs in hot loops. In benchmark profile X we observed lower tail latency under register pressure. The exact heuristic implementation and pass internals are proprietary."

## Trade Secret Pivot (When Question Is Too Specific)

Use a direct and professional boundary:

"The implementation details of this lowering/scheduling pass are proprietary, but the design objective is to minimize pipeline stalls and spill pressure under high-pressure loops."

## Public Channel Guidance

For Reddit, Hacker News, conference Q and A, and social media:

- keep answers short, factual, and non-defensive
- avoid source-level detail requests
- redirect to whitepaper sections and benchmark appendix
- provide licensing contact for evaluation requests

Licensing contact: licensing@dgtl.tech
