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

# DGTL Public API Release Policy

## Versioning Model
DGTL public API follows semantic versioning for Tier A:
- MAJOR: Breaking change in Tier A contract.
- MINOR: Backward-compatible feature additions.
- PATCH: Backward-compatible bug fixes and clarifications.

## Compatibility Matrix

### Tier A
- Patch to patch: fully compatible.
- Minor to minor: backward compatible.
- Major bump: may break; migration guide required.

### Tier B
- Patch to patch: expected compatible.
- Minor to minor: may include controlled breaking changes with migration notes.
- Major bump: unrestricted changes allowed with high-level migration guidance.

### Tier C
- No compatibility guarantee.

## Change Control Rules

### Rule 1: Tier Declaration Required
Every public header or symbol must be declared as Tier A, B, or C in docs.

### Rule 2: No Undeclared Breaking Changes
Any Tier A breaking change without a major version bump is prohibited.

### Rule 3: Deprecation Window
Tier A deprecations must follow:
- Introduce deprecation warning in minor N.
- Keep old behavior for at least one subsequent minor release.
- Remove only in next major.

### Rule 4: Migration Artifacts
Any Tier A change must include:
- Changelog entry.
- Migration notes.
- Before/after examples.

### Rule 5: Payload Version Stability
Payload format uses explicit schema version header.
- Reader must support current and at least one previous schema version where feasible.
- Writer emits only current schema by default.

## Release Gates for Tier A
A release is blocked unless all gates pass.

Gate A: Contract tests
- Payload roundtrip tests pass.
- Lowering contract tests pass.
- Backend emission parity tests pass for all targets.

Gate B: Determinism tests
- Same payload + options produce deterministic logic hash.

Gate C: Compatibility tests
- Latest reader can load previous supported payload versions.

Gate D: Documentation completeness
- API docs updated.
- Capability matrix updated.
- Breaking changes clearly flagged.

## Capability Negotiation Rollout
Planned mandatory rollout:
1. Add API capability endpoint and static capability object.
2. Require frontends to check capabilities before emit in strict mode.
3. Promote strict mode to default in a later major release.

## Recommended Repository Artifacts
- docs/api/PUBLIC_API_TIERS_AND_CONTRACT.md
- docs/api/PUBLIC_API_RELEASE_POLICY.md
- docs/api/FRONTEND_CONFORMANCE_KIT.md

## Ownership
- Tier A owner: DGTL platform maintainers.
- Tier B owner: optimization and performance maintainers.
- Tier C owner: subsystem maintainers.
