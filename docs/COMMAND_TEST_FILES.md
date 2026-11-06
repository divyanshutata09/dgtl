# DGTL Command and Test File Mapping

Generated from actual mappings in `dgtl.ps1` (May 13, 2026).

## Correct Command Order

```powershell
# 1) Static policy/compliance
dgtl header-check

# 2) Fast aggregate tests (quick set)
dgtl test

# 3) Backend conformance against goldens
dgtl run

# 4) Performance suite
dgtl brun

# 5) Optional benchmark mode
dgtl bench x64 fold

# 6) Optional payload compilation sweep
dgtl compile

# 7) Release packaging gate
dgtl package
```

## Non-Overlapping Test Ownership

For test-source ownership, use these groups with no overlap:

- `dgtl test` owns `Quick`.
- `dgtl brun` owns `BrunDefault`.
- `dgtl package` additionally owns `SdkSmoke` (smoke link test source).

## Group: Quick (used by `dgtl test`)

- tests/test_suite_main.cpp
- tests/functional/ir/t01_ir_stream_correctness.cpp
- tests/functional/ir/t02_ir_transaction_rollback.cpp
- tests/functional/ir/t12_ir_cfg.cpp
- tests/functional/optimizer/t03_optimizer_gvn_const_fold.cpp
- tests/functional/optimizer/t04_optimizer_fusion.cpp
- tests/functional/optimizer/t05_optimizer_volatile_guard.cpp
- tests/functional/codegen/t06_codegen_arm64_emission.cpp
- tests/functional/frontend/t07_frontend_pipeline_lowerer.cpp
- tests/sdk/t08_sdk_c_api_roundtrip.cpp
- tests/functional/runtime/t09_runtime_mt_arena_isolation.cpp
- tests/performance/benchmarks/t10_optimizer_strike_throughput.cpp
- tests/performance/benchmarks/t11_pipeline_production_benchmark.cpp
- tests/functional/utils/t13_utils_micro_fuzz.cpp

## Group: BrunDefault (used by `dgtl brun` with no path arg)

- tests/performance/benchmarks/benchmark_full_production_stress.cpp
- tests/performance/benchmarks/benchmark_hardest_e2e_gate.cpp

## Group: SdkSmoke (used by `dgtl package`)

- tests/sdk/sdk_smoke_test.cpp

## Notes

- `dgtl run`/`dgtl update-golden` are backend conformance flows and use `build/dgtl_main*.exe` with golden/log files, not test `.cpp` sources directly.
- `dgtl bench` uses executable runtime benchmark mode and does not directly consume test `.cpp` source lists.
- `dgtl compile` consumes payload files and emits backend artifacts, not test `.cpp` source lists.
