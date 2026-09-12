# DGTL Incident Report

- Root: D:\tatag\dgtl
- Last command: `dgtl --help`
- Last exit code: `1`
- Compiler fingerprint: `4c170f441e5daac2`
- Command fingerprint: `1501e832aca2951f`
- Logs scanned: 1
- Declaration cache: 253 hits, 0 misses, 0 rescanned files
- Likely failing command: `g++ -std=c++26 -fmodules-ts -O3 -pipe -I"D:\TataG\DGTL\include" -c "D:\TataG\DGTL\src\sdk\dgtl_sdk_impl.cpp" -o "D:\TataG\DGTL\build\sdk_obj\sdk_dgtl_sdk_impl.o"`

## Error Clusters
- (x1) --help
- (x1) --help:String) [], RemoteException

## Failing Command Context

## Ownership Recommendations
- Symbol `find_generated_op_def`
  Winner: include/dgtl/ir/gen/DGTL_Gen_Ops.ixx:9354 (score 110)
  Evidence:

```cpp
9352: }();
9353: 
9354: [[nodiscard]] inline const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept {
9355:     const size_t index = static_cast<size_t>(opcode);
9356:     return index < kGeneratedOpDefLookupTable.size() ? kGeneratedOpDefLookupTable[index] : nullptr;
```
- Symbol `IRStream`
  Winner: include/dgtl/backend/driver/Backend_Driver_API.ixx:281 (score 40)
  Evidence:

```cpp
279: }
280: 
281: inline bool emit_arm64_direct(const dgtl::ir::IRStream& stream,
282:                               const dgtl::semantic::SymbolTable& symbols,
283:                               std::string& out_text) {
```
- Symbol `TargetMachine`
  Winner: include/dgtl/isa/common/Instruction_Selector.ixx:460 (score 40)
  Evidence:

```cpp
458: }
459: 
460: [[nodiscard]] inline bool supports_rule_requirements(const SelectionRule& rule, const core::TargetMachine& target) noexcept {
461:     if (rule.require_fma && !target.get_caps().has_fma) {
462:         return false;
```
- Symbol `RuntimeFeedback`
  Winner: include/dgtl/analysis/AI_Cost_Model.ixx:47 (score 20)
  Evidence:

```cpp
45: 
46: // PRODUCTION RESOLUTION: True, fully-defined exported struct attaches layout to this module unit.
47: struct RuntimeFeedback {
48:     double actual_latency = 0.0;
49:     double cache_miss_rate = 0.0;
```

## Regression Notes
- Previous run: error cluster count increased from 0 to 2.
