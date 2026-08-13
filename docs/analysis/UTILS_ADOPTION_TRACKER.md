# DGTL Utils Adoption Tracker

## Goal
Migrate low-level DGTL codepaths to mandatory utility primitives for robustness, determinism, and throughput predictability.

## Status Summary
- Completed waves: 15
- In progress: implementation-only migration sweep (tests intentionally deferred)
- Next priority: map/matcher adoption in hot bounded domains

## Completed
- [x] `src/sdk/dgtl_sdk_impl.cpp`
  - Replaced ad-hoc split/merge bit ops with centralized BitPack-based helpers.
  - Replaced manual label hashing with `Hashing::fnv1a`.
  - Replaced ad-hoc binary constant identifier with `Hashing::xxhash64`.
- [x] `include/dgtl/pipeline/Pass_Manager.hpp`
  - Replaced pointer-address-derived trace hashing with content-derived deterministic tokens (`Hashing::fnv1a`).
  - Replaced local custom hash-step logic with `Hashing::combine` for unified hash composition semantics.
- [x] `tests/t08_sdk_c_api_roundtrip.cpp`
  - Added deterministic-hash parity assertion across two independently emitted identical streams.
- [x] `dgtl.cmd`
  - Removed UTF-8 BOM launcher instability by re-encoding ASCII.
- [x] `src/ir/IR_Stream.cpp`
  - Migrated stream logic hash mixer to utility-based hash composition.
  - Added assertionized bounds guard path in block hotness recording.
  - Migrated speculative alias-hoister address-access map to `FastFlatMap` primary path with deterministic overflow fallback.
  - Replaced memory-access optimizer overflow map with FlatMap overflow tier.
- [x] `include/dgtl/ir/base/IR_Verifier.hpp`
  - Added bounded `FastFlatMap` fast path for duplicate-def and use-before-def passes.
  - Preserved correctness on large streams with automatic `unordered_*` fallback.
  - Added bounded `FastFlatMap` fast path for ownership/lock lifecycle passes with fallback for large streams.
  - Removed all remaining `unordered_map` / `unordered_set` usage from verifier implementation paths.
  - Reworked overflow handling to FlatMap-only tiers to satisfy strict `Flat_Map.hpp`-only map requirement.
- [x] `src/main.cpp`
  - Migrated bounded helper-address  cache from `std::unordered_map` to `FastFlatMap<...,1024>`.
  - Added explicit capacity-failure handling for deterministic failure behavior under pathological payloads.
  - Migrated helper-function call-slot map to `FastFlatMap<...,2048>` and retained linker-only materialization map where API requires `std::unordered_map`.
  - Added entry-function global call-slot bounded facade (`FastFlatMap<...,2048>`) while preserving linker-required `std::unordered_map`.
- [x] `include/dgtl/core/FrontendHelpers.hpp`
  - Replaced ad-hoc 64-bit immediate split/join operations with `BitPack`-based helpers.
  - Routed constant-fold immediate reconstruction through utility helper for deterministic encoding consistency.
  - Migrated control-flow label position lookup to `FastFlatMap` primary path with overflow fallback.
  - Removed remaining `unordered_map` usage by migrating value/slot and extern-slot tables to `FastFlatMap` + deterministic overflow vectors.
  - Reworked overflow handling to FlatMap-only tiers for value/slot/label maps.

## Remaining Priority Queue
1. `src/ir/IR_Stream.cpp`
  - Add collision/occupancy telemetry around transient analysis maps.
2. `include/dgtl/ir/base/IR_Verifier.hpp`
  - Completed unordered container removal; keep monitoring diagnostics parity in next validation phase.
3. `include/dgtl/core/FrontendHelpers.hpp`
  - Continue targeted string ownership simplification where safe (favor `string_view` on non-owning paths).
5. `include/dgtl/analysis/*` and `include/dgtl/pass/*`
   - Map-by-map audit: convert only bounded hot-path maps; keep unbounded maps on unordered_map with documented rationale.

## Adoption Rules (Enforced During Review)
- New low-level hashing must use `dgtl::utils::Hashing`.
- New bitfield composition must use `dgtl::utils::BitPack`.
- New high-risk indexing must be guarded with assertions/bounds checks.
- `FastFlatMap` is mandatory for bounded hot maps unless a file-level waiver is documented.
