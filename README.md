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

1. Clone or navigate to the project directory.

2. Create a build directory:
   ```
   mkdir build
   cd build
   ```

3. Configure with CMake:
   ```
   cmake ..
   ```

4. Build the project:
   ```
   cmake --build . --config Release
   ```

This will compile the main executable `AuraCore` and all test executables.

## Running

After building, run the main executable:
```
./AuraCore
```

## Testing

Run the automated test suite:
```
ctest
```

Or run individual tests by executing the test executables in the build directory.

## Project Structure

- `include/`: Header files organized by module (core, ir, analysis, etc.)
- `src/`: Source files
- `tests/`: Test suites (functional and latency)
- `docs/`: Documentation (currently contains code; to be updated)
- `tools/`: Additional tools

## Contributing

Ensure all tests opt before submitting changes. Add new tests for new features.