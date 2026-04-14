# DGTL Project

DGTL (DGTL-MLIR-X / DGTL-TITAN ENGINE) is a high-performance, retargetable compiler infrastructure optimized for AI, High-Frequency Trading (HFT), Cloud computing, and full-stack applications. It features an innovative bit-packed intermediate representation (BIR 2.1) with advanced optimization optes and just-in-time execution capabilities.

## Features

- Bit-packed IR (BIR 2.1) for memory efficiency and SIMD-friendliness
- Multi-target code generation (ARM64, x86-64, WebAssembly, macOS)
- AI-powered cost modeling for optimization decisions
- Transactional memory with speculative execution and rollback
- JIT execution with W^X memory protection
- Extensive optimization optes (GVN, DCE, SIMD fusion, etc.)

## Requirements

- CMake 3.28+
- C++26 compatible compiler (GCC with UCRT64 or similar)
- Windows/Linux/macOS

## Building

### Quick Start (Recommended)
```powershell
dgtl build
```
Builds the full project with guard protection (prevents hangs).

**Single target:**
```powershell
dgtl build test_pipeline1.cpp
```

### Manual Build
1. Clone or navigate to the project directory.

2. Create a build directory:
   ```
   mkdir build-gcc
   cd build-gcc
   ```

3. Configure with CMake:
   ```
   cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
   ```

4. Build the project:
   ```
   cmake --build . -j 4
   ```

This will compile all test executables into `build-gcc/bin/`.

## Running

### Quick Start (Recommended)
```powershell
dgtl run
```
Runs all tests with guard protection.

**Single executable:**
```powershell
dgtl run test_pipeline1.cpp
```

### Manual Test Execution
```powershell
ctest --test-dir build-gcc --output-on-failure
```

Or run individual executables directly:
```powershell
./build-gcc/bin/test_pipeline1.exe
```

## Testing

### Full Workflow
```powershell
dgtl crun
```
Clean + build + run all (each step guarded).

**Single target:**
```powershell
dgtl crun test_pipeline1.cpp
```

### Strict Mode (CI/Final Validation)
```powershell
dgtl strict
dgtl srun
```

### Manual CTest
```powershell
ctest --test-dir build-gcc --output-on-failure
ctest --test-dir build-gcc-strict --output-on-failure
```

## Comparison and Release Readiness

Use measured local results only. Avoid claiming superiority over LLVM/MLIR without same-machine, same-workload A/B runs.

- Comparison harness: `test_comparison_suite.ps1`
- Detailed methodology: [docs/COMPARISON_TEST_SUITE.md](docs/COMPARISON_TEST_SUITE.md)
- Independent rerun guide: [docs/VERIFICATION_GUIDE.md](docs/VERIFICATION_GUIDE.md)
- Current release gate status: [docs/RELEASE_READINESS_REPORT.md](docs/RELEASE_READINESS_REPORT.md)

## Command Reference

See [docs/commands/build_guard_commands.md](docs/commands/build_guard_commands.md) for full command reference.

| Action | Usage | Purpose |
|--------|-------|---------|
| `dgtl build` | Build all or single target | Compile with guard protection |
| `dgtl run` | Run all or single executable | Execute tests with guard |
| `dgtl clean` | Remove binaries | Delete all/single exe from `build-gcc/bin/` |
| `dgtl crun` | Clean + build + run | Full workflow (each step guarded) |
| `dgtl strict` | Build with strict warnings | Catch all warnings before merge |
| `dgtl srun` | Run strict-built tests | Execute tests from strict build |

## Project Structure

- `include/`: Header files organized by module (core, ir, analysis, etc.)
- `src/`: Source files
- `tests/`: Test suites (functional and latency)
- `docs/`: Project documentation, release notes, and validation reports
- `tools/`: Additional tools

## Contributing

Ensure all tests pass before submitting changes. Add new tests for new features.