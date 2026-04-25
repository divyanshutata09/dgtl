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

# DGTL Opt-Strike Performance Recovery

## Scope

This note records how the `performance.opt_strike` path was improved from the original `26.274 M-Nodes/sec` baseline to a best verified run of `211.015 M-Nodes/sec` on the current host, while preserving feature behavior and keeping the final logic hash fixed at:

- `0x13a623aac2e93e70`

The work was intentionally limited to hot-path performance barriers. No feature was removed. The optimization passes, transactional semantics, and sentinel integrity model all remained active.

## Starting Point

The first reference run in this recovery cycle was:

- Throughput: `26.274 M-Nodes/sec`
- Total strike time: `38.061 ms`
- HFT stride: `8 bytes (mode=auto)`
- Final stream size: `1000000 nodes`
- Final logic hash: `0x13a623aac2e93e70`

The target was to recover lost throughput without compromising:

- deterministic output
- transactional safety
- sentinel validation
- benchmark semantics
- final logic hash

## What Was Slowing The Benchmark Down

The benchmark was spending significant time in overhead that was not changing the optimized result.

The main hurdles were:

1. Hash-table churn inside large optimization sweeps.
2. Dead-code-elimination liveness tracking using a hash set even when register IDs were dense and predictable.
3. The no-model `SpeculativeDCE` pipeline path doing transactional work even when it did not mutate the stream.
4. `validate_shadow_integrity()` not validating committed buffers at all, which both hid real cost and forced validation to happen through an unnecessary shadow-buffer path.
5. Once the no-op transactional work was removed, sentinel validation itself became the dominant cost on the 1M-node stream.

## Change Sequence

### 1. Stabilize GVN hash containers

File:

- `src/ir/IR_Stream_Optimizations.cpp`

Problem:

- `perform_gvn()` created three unordered maps and let them grow dynamically during a 1M-node walk.
- That caused repeated rehashing and allocator churn in a path that should be close to linear and cache-stable.

Change:

- Added `reserve()` calls for:
  - `expression_map`
  - `register_remap`
  - `const_values`

Why it helped:

- Reduced repeated growth and rehash overhead during the largest benchmark workload.
- Kept the GVN sweep closer to a stable linear pass.

Correctness impact:

- None. Capacity planning changed, not semantics.

## 2. Replace DCE hash-based liveness with a dense fast path

File:

- `src/ir/IR_Stream_Optimizations.cpp`

Problem:

- `perform_dead_code_elimination()` tracked live registers with `std::unordered_set<uint32_t>`.
- In the strike benchmark, register IDs are large in count but compact in shape, so hashing every live/erase/query was unnecessary overhead.

Change:

- Added a pre-scan to find the maximum register ID referenced by the shadow stream.
- Introduced a dense liveness table using `std::vector<uint8_t>` when the ID space is compact.
- Kept a sparse `std::unordered_set<uint32_t>` fallback for large or sparse spaces.
- Replaced direct set operations with `is_live`, `set_live`, and `clear_live` lambdas.

Why it helped:

- Turned the common benchmark case from hash lookups into direct indexed loads and stores.
- Improved locality and reduced allocator and hash overhead.

Correctness impact:

- None. The same liveness decisions were preserved, only the storage strategy changed.

## 3. Remove no-op transactional work in the no-model SpeculativeDCE path

File:

- `include/dgtl/pass/speculative/Speculative_DCE.hpp`

Problem:

- The strike benchmark registers `DGTLSpeculativeDCE()` without an AI model.
- In that mode, the pass was still:
  - entering a transaction
  - cloning the active stream into shadow storage
  - validating
  - committing an unchanged stream
- On a 1M-node stream, that was pure overhead.

Change:

- Added a fast path:
  - if no AI model is attached, call `stream.validate_shadow_integrity()` and return immediately.
- The transactional path remains intact when a model is present and real shadow mutation happens.

Why it helped:

- Removed a full shadow-copy and commit cycle from the benchmark path.
- This was the single biggest structural win because it deleted work rather than merely making that work cheaper.

Correctness impact:

- None for the benchmark path, because the pass was not mutating anything in the no-model case.
- The AI-driven path still uses the full transactional protocol.

## 4. Fix `validate_shadow_integrity()` so it validates the active buffer

File:

- `src/ir/IR_Stream_Operations.cpp`

Problem:

- Before this fix, `validate_shadow_integrity()` returned `true` immediately if no transaction was active.
- That meant callers on the committed stream were not actually validating anything.
- It also forced real validation to happen via transaction entry, which created avoidable copy overhead.

Change:

- Updated validation to inspect whichever buffer is active:
  - `shadow_atoms/shadow_cores` when in transaction
  - `atoms/cores` when not in transaction
- The function now recalculates and checks every active node hash in both modes.

Why it helped:

- It fixed a real correctness gap.
- It also enabled the no-model `SpeculativeDCE` fast path to perform true validation without creating a shadow transaction first.

Correctness impact:

- Positive. The API now does what its name implies for both transactional and committed streams.

## 5. Parallelize large-buffer sentinel validation

File:

- `src/ir/IR_Stream_Operations.cpp`

Problem:

- After removing the no-op transaction path, the benchmark became dominated by hash validation over a 1M-node active stream.
- That work is embarrassingly parallel because each node hash check is independent.

Change:

- Added a size-aware parallel validation path:
  - serial validation remains in place for small streams
  - large streams are split into chunks
  - worker threads validate independent ranges
  - an atomic `valid` flag collects failure state
- The threshold was set high enough to avoid hurting common small-stream paths.

Why it helped:

- Spread the sentinel hash verification cost across available hardware threads.
- This removed the last major bottleneck in the 1M-node strike benchmark.

Correctness impact:

- None. The same hash check is performed, only in parallel for large active buffers.

## Measured Progression

The key verified runs during the recovery were:

| Stage | Throughput | Strike Time | Notes |
|---|---:|---:|---|
| Baseline | `26.274 M-Nodes/sec` | `38.061 ms` | Original starting point |
| After removing redundant no-op validation and commit overhead | `54.268 M-Nodes/sec` | `18.427 ms` | No-model `SpeculativeDCE` no longer paid the extra transactional tail |
| After active-buffer validation fix | `83.801 M-Nodes/sec` | `11.933 ms` | Real committed-stream validation enabled the fast path cleanly |
| After parallel active-buffer validation | `175.500 M-Nodes/sec` | `5.698 ms` | First large jump beyond the original target band |
| Repeat sample | `200.844 M-Nodes/sec` | `4.979 ms` | Verified follow-up run |
| Repeat sample | `190.295 M-Nodes/sec` | `5.255 ms` | Verified follow-up run |
| Best verified sample | `211.015 M-Nodes/sec` | `4.739 ms` | Current best verified full-suite run on this host |

In other words, the benchmark moved from about `27 M/s` to about `212 M/s` on the same logical workload and with the same final hash.

## Other Parameter Improvements

The strike benchmark was the primary target, but other signals improved as collateral benefit because the same hot infrastructure was being exercised more efficiently.

Observed `performance.mt_codegen` samples during this phase included:

- `729517 ops/sec`
- `875213 ops/sec`
- `901937 ops/sec`
- `855948 ops/sec`

This metric still varies by run more than the strike benchmark, but it did not regress structurally and often improved relative to earlier lower readings seen during the recovery.

## Why The Hash Stayed Intact

The logic hash remained `0x13a623aac2e93e70` in every validated strike result because the work focused on execution overhead, not program meaning.

The changes preserved semantics in the following way:

1. Container reservation changed capacity behavior, not transformation decisions.
2. Dense DCE liveness tracking kept the same live/dead outcome, only with faster lookup.
3. The no-model `SpeculativeDCE` path skipped transaction setup only when no mutation would occur.
4. Active-buffer validation fixed missing checking; it did not rewrite nodes.
5. Parallel validation changed scheduling, not the hash formula or the checked data.

The invariant that mattered most was preserved throughout:

- input workload unchanged
- final stream size unchanged at `1000000 nodes`
- final logic hash unchanged at `0x13a623aac2e93e70`

## Validation Performed

After each substantive code change, the following validation was used:

```powershell
.\dgtl.ps1 build-strict
.\dgtl.ps1 test full
```

This ensured that:

- feature coverage stayed intact
- transactional behavior remained valid
- frontend bridge and polyhedral tests stayed green
- robustness tests stayed green
- the strike benchmark improvement was measured in the normal aggregate suite

## Files That Matter Most To This Recovery

- `src/ir/IR_Stream_Optimizations.cpp`
- `src/ir/IR_Stream_Operations.cpp`
- `include/dgtl/pass/speculative/Speculative_DCE.hpp`
- `tests/performance/stress/test_opt_strike.cpp`

## Engineering Summary

The recovery did not come from one exotic pass rewrite. It came from removing layered overhead that had accumulated around the benchmark path:

1. Avoid hash-container instability when a sweep is predictably large.
2. Use dense data structures when the ID space is dense.
3. Do not start transactions for passes that will not mutate anything.
4. Make validation APIs actually validate the active buffer.
5. Parallelize the remaining independent sentinel work once it becomes the new bottleneck.

That sequence is what took the strike path from roughly `26 M-Nodes/sec` to a verified best of `211.015 M-Nodes/sec`, with the logic hash unchanged.