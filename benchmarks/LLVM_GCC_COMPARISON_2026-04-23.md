# DGTL Benchmark Comparison (GCC Proxy for LLVM)

Date: 2026-04-23

## Context

- Source benchmark: `tests/performance/llvm_compare.cpp`
- LLVM/Clang was not available on this machine (`clang++` not found), so direct LLVM timings could not be collected.
- A GCC native-tuned proxy was used for comparison.

## Single-Run Results

### Baseline GCC

Command:

```powershell
g++ -std=c++20 -O3 -mavx2 "tests/performance/llvm_compare.cpp" -o "$env:TEMP\llvm_compare.exe"; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & "$env:TEMP\llvm_compare.exe"
```

Results:

- TEST 1 Dependency Chain: 0.0586963 sec
- TEST 2 ECS Update: 0.0121456 sec
- TEST 3 SIMD Kernel: 0.0228276 sec
- TEST 4 HFT Branching: 0.32941 sec

### GCC Native + LTO Proxy

Command:

```powershell
g++ -std=c++20 -O3 -march=native -mtune=native -flto "tests/performance/llvm_compare.cpp" -o "$env:TEMP\llvm_compare_gcc_native.exe"; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; & "$env:TEMP\llvm_compare_gcc_native.exe"
```

Results:

- TEST 1 Dependency Chain: 0.0570069 sec
- TEST 2 ECS Update: 0.0130037 sec
- TEST 3 SIMD Kernel: 0.0173537 sec
- TEST 4 HFT Branching: 0.334921 sec

## 10-Run Median Comparison

Each profile was run 10 times, then per-test medians were computed.

| Profile | Test 1 Dependency | Test 2 ECS | Test 3 SIMD | Test 4 HFT |
|---|---:|---:|---:|---:|
| c20_avx2 | 0.056951 | 0.009606 | 0.014976 | 0.333005 |
| c20_native | 0.057494 | 0.009596 | 0.015319 | 0.331271 |
| c26_avx2 | 0.057432 | 0.009910 | 0.015430 | 0.332939 |
| c26_native | 0.055450 | 0.008791 | 0.014363 | 0.323106 |

## Summary

- Best measured profile on this machine: `c26_native`.
- `c26_native` produced the fastest median in all four tests among tested GCC configurations.
- For a real GCC vs LLVM comparison, I need to install LLVM and rerun with `clang++`.
