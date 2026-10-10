# DGTL Sovereign API Architecture

All C++ module API session classes (`dgtl.api.*`) are fully wired to the sovereign compilation backend:

$$\text{IRStream} \xrightarrow{\text{CodegenDriver}} \text{ObjectFile} \xrightarrow{\text{JITObjectLoader}} \text{Executable Memory ($W \oplus X$)}$$
$$\text{IRStream} \xrightarrow{\text{CodegenDriver}} \text{ObjectFile} \xrightarrow{\text{DGTLAOTLinker}} \text{Standalone Executable}$$

### Modernized Session Implementations:
- **`JITSession.ixx` (`dgtl.api.jit_session`)**: Directly compiles `IRStream`, `MachineFunction`, or `ObjectFile` via `CodegenDriver` and loads into executable memory via `JITObjectLoader`. Zero legacy orchestrator dependencies.
- **`AOTSession.ixx` (`dgtl.api.aot_session`)**: Compiles to `ObjectFile` via `CodegenDriver`, directly serializes `.obj`/`.o` via `ObjectWriter`, and synthesizes standalone `.exe` executables via `DGTLAOTLinker`.
- **`HybridSession.ixx` (`dgtl.api.hybrid_session`)**: Adaptive compilation dynamically selecting fast JIT loading or optimized AOT emission via `CodegenDriver`.
- **`ExecutionEngine.ixx` (`dgtl.api.execution_engine`)**: Sovereign polymorphic base interface supporting `IRStream`, `MachineFunction`, and `ObjectFile` execution and file emission.
- **`Api_all.ixx` (`dgtl.api`)**: Re-exports all public API sessions and modern backend interfaces (`dgtl.codegen.driver`, `dgtl.jit.object_loader`, `dgtl.linker.driver`).
