# DGTL Incident Report

- Root: D:\TataG\DGTL
- Last command: `dgtl build-strict`
- Last exit code: `1`
- Compiler fingerprint: `881fc869c935224c`
- Command fingerprint: `df6776abfe1a3700`
- Logs scanned: 1
- Declaration cache: 357 hits, 0 misses, 0 rescanned files
- Likely failing command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`

## Error Clusters
- (x13) ##: error: 'dgtl::backend::driver::BackendDriverTarget' has not
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (13 hits)
- (x1) ##: error: 'ARM##Emitter' is not a member of 'dgtl::isa'
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'BackendDriverTarget' is not a member of
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'DGTLGVN' is not a member of
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'MachOEmitter' is not a member of 'dgtl::isa'
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'backend_driver_target_name' is not a member of
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'bytes_to_hex' is not a member of
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)
- (x1) ##: error: 'dgtl::isa::ARM##Emitter' has not been declared
  Command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"` (1 hits)

## Failing Command Context
- `##: error: 'dgtl::backend::driver::BackendDriverTarget' has not`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 13
- `##: error: 'ARM##Emitter' is not a member of 'dgtl::isa'`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'BackendDriverTarget' is not a member of`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'DGTLGVN' is not a member of`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'MachOEmitter' is not a member of 'dgtl::isa'`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'backend_driver_target_name' is not a member of`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'bytes_to_hex' is not a member of`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1
- `##: error: 'dgtl::isa::ARM##Emitter' has not been declared`
  Compile step: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -Wall -Wextra -Wpedantic -Werror -c "D:\TataG\DGTL\src\apps\smoke_main.cpp" -o "D:\TataG\DGTL\build\obj\dgtl_release\apps_smoke_main.o"`
  Evidence hits: 1

## Ownership Recommendations
- Symbol `find_generated_op_def`
  Winner: include/dgtl/ir/gen/DGTL_Gen_Ops.ixx:623 (score 110)
  Evidence:

```cpp
621: }();
622: 
623: [[nodiscard]] inline const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept {
624:     const size_t index = static_cast<size_t>(opcode);
625:     return index < kGeneratedOpDefLookupTable.size() ? kGeneratedOpDefLookupTable[index] : nullptr;
```
- Symbol `TargetMachine`
  Winner: tools/cpp/dgtl_isel_gen_main.cpp:1189 (score 50)
  Evidence:

```cpp
1187: 
1188:     out << "[[nodiscard]] constexpr MachineForm classify_machine_form(\n"
1189:         << "    const core::TargetMachine& target, ir::OpCode opcode) noexcept {\n"
1190:         << "    switch (target.get_arch()) {\n";
1191:     for (const auto& p : parsed_targets) {
```
- Symbol `IRStream`
  Winner: include/dgtl/formal/Formal_Verification_SMT.ixx:235 (score 40)
  Evidence:

```cpp
233:     [[nodiscard]] static std::string generate_gvn_proof_obligations(const ir::IRStream& original, const ir::IRStream& optimized) noexcept;
234:     [[nodiscard]] static std::string generate_dce_proof_obligations(const ir::IRStream& original, const ir::IRStream& optimized) noexcept;
235:     [[nodiscard]] static std::string generate_inlining_proof_obligations(const ir::IRStream& caller, const ir::IRStream& callee, const ir::IRStream& inlined) noexcept;
236: 
237:     [[nodiscard]] static std::string generate_verification_report(const VerificationResult& result, const ir::IRStream& original, const ir::IRStream& optimized) noexcept;
```
- Symbol `BackendDriverTarget`
  Winner: include/dgtl/linker/driver/Backend_Driver_API.ixx:234 (score 40)
  Evidence:

```cpp
232: }
233: 
234: inline const char* backend_driver_target_name(BackendDriverTarget target) {
235:     switch (target) {
236:     case BackendDriverTarget::arm64: return "arm64";
```
- Symbol `driver_target`
  Winner: include/dgtl/linker/driver/Backend_Driver_API.ixx:234 (score 40)
  Evidence:

```cpp
232: }
233: 
234: inline const char* backend_driver_target_name(BackendDriverTarget target) {
235:     switch (target) {
236:     case BackendDriverTarget::arm64: return "arm64";
```
- Symbol `write_binary_file`
  Winner: include/dgtl/linker/driver/Backend_Driver_Shared_Utils.ixx:132 (score 40)
  Evidence:

```cpp
130: }
131: 
132: inline bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
133:     const std::filesystem::path p(path);
134:     if (p.has_parent_path()) {
```
- Symbol `parse_polyhedral_parallel_policy`
  Winner: include/dgtl/linker/driver/Backend_Driver_Shared_Utils.ixx:76 (score 40)
  Evidence:

```cpp
74: 
75: template <typename PolyhedralParallelPolicyT>
76: inline bool parse_polyhedral_parallel_policy(std::string policy_str,
77:                                              PolyhedralParallelPolicyT& out_policy,
78:                                              std::string& out_name) {
```
- Symbol `parse_polyhedral_mode`
  Winner: include/dgtl/linker/driver/Backend_Driver_Shared_Utils.ixx:53 (score 40)
  Evidence:

```cpp
51: 
52: template <typename PolyhedralModeT>
53: inline bool parse_polyhedral_mode(std::string mode_str,
54:                                   PolyhedralModeT& out_mode,
55:                                   std::string& out_name) {
```

## Regression Notes
- Previous run: error cluster count increased from 2 to 25.
- Previous run: top failure signature changed from 'No such file or directory' to '##: error: 'dgtl::backend::driver::BackendDriverTarget' has not'.
