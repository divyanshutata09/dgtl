# DGTL Real Gaps and Mislinkings Audit
Date: 2026-05-08

## Findings (ordered by severity)

### 1) High: Test suite can report PASS while functional checks fail
- In [tests/functional/pipeline/production_arch_validator.cpp](../../tests/functional/pipeline/production_arch_validator.cpp#L209), failure conditions are printed but not raised.
- In [tests/functional/pipeline/production_arch_validator.cpp](../../tests/functional/pipeline/production_arch_validator.cpp#L278), the verdict is text-only.
- The aggregate harness in [tests/test_suite_main.cpp](../../tests/test_suite_main.cpp#L58) and [tests/test_suite_main.cpp](../../tests/test_suite_main.cpp#L65) marks PASS whenever no exception is thrown.
- Impact: false-green CI risk and masked optimizer regressions.

### 2) High: DGTL internal object contract drift causes MYZON link failures
- DGTL now exposes CFG methods from merged IR implementation in [src/ir/IR_Stream.cpp](../../src/ir/IR_Stream.cpp#L1886) and [src/ir/IR_Stream.cpp](../../src/ir/IR_Stream.cpp#L1892).
- MYZON still links legacy split IR objects in [../MYZON/myzon.ps1](../../../MYZON/myzon.ps1#L474-L476).
- Reproduced symptom: undefined references to IRStream get_basic_blocks and get_cfg_edges during MYZON link.
- Impact: hard integration break between DGTL and MYZON.

### 3) Medium: internal_obj is preserved, but stale artifacts are not pruned
- Internal object build is incremental in [dgtl.ps1](../../dgtl.ps1#L1307).
- Clean explicitly preserves internal_obj in [dgtl.ps1](../../dgtl.ps1#L1567).
- Risk: downstream scripts that hardcode or enumerate historical object names may pick stale artifacts and drift from current source layout.

### 4) Medium: Additional pipeline checks are also print-only failures
- [tests/functional/pipeline/test_pipeline1.cpp](../../tests/functional/pipeline/test_pipeline1.cpp#L159) prints failure text but does not fail the test process.
- Same flow invokes production validator in [tests/functional/pipeline/test_pipeline1.cpp](../../tests/functional/pipeline/test_pipeline1.cpp#L172), inheriting non-failing behavior.
- Impact: more false-green paths in functional coverage.

### 5) Low-Medium: Performance guardrail is informational only
- [tests/performance/stress/test_opt_strike.cpp](../../tests/performance/stress/test_opt_strike.cpp#L183-L184) reports outside expected recovery band as a notice, not a failure.
- Impact: throughput regressions may be logged but not gated.

### 6) Low: New CFG SDK surface lacks explicit behavior tests
- Existing SDK roundtrip test [tests/sdk/test_sdk_api_sync_roundtrip.cpp](../../tests/sdk/test_sdk_api_sync_roundtrip.cpp) validates emit/optimize basics.
- No explicit assertions currently found for CFG C APIs (block/edge/successor/predecessor retrieval).
- Impact: ABI compiles, but CFG behavior contract is not strongly verified.

## Reproduced evidence snapshot
- Command: myzon build
- Result: link failed with unresolved references to IRStream CFG methods (get_basic_blocks, get_cfg_edges).

## Recommended remediation sequence
1. Convert print-only FAIL paths in functional tests into hard failures (throw or explicit non-zero result propagation).
2. Update MYZON link list to match merged DGTL IR object strategy.
3. Add stale-object pruning or manifest-based object selection for internal_obj consumers.
4. Add SDK tests for CFG C API behavior to close the contract gap.
