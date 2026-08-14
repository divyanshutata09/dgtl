# DGTL Build Diagnostic Report

- Status: **SUCCESS** (0 errors detected)
- Root: D:\TataG\dgtl
- Last command: `dgtl GEN-VALIDATE`
- Last exit code: `0`
- Compiler fingerprint: `cc06a44a6546513b`
- Command fingerprint: `005a1c650ba5ebd8`
- Logs scanned: 1
- Declaration cache: 0 hits, 241 misses, 241 rescanned files

## Error Clusters
- No errors detected.

## Ownership Recommendations
- Symbol `find_generated_op_def`
  Winner: include/dgtl/ir/gen/DGTL_Gen_Ops.ixx:8998 (score 110)
  Evidence:

```cpp
8996: }();
8997: 
8998: [[nodiscard]] inline const OpDefMetadata* find_generated_op_def(dgtl::ir::OpCode opcode) noexcept {
8999:     const size_t index = static_cast<size_t>(opcode);
9000:     return index < kGeneratedOpDefLookupTable.size() ? kGeneratedOpDefLookupTable[index] : nullptr;
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
  Winner: include/dgtl/isa/lowering/Instruction_Selector.ixx:462 (score 40)
  Evidence:

```cpp
460: }
461: 
462: [[nodiscard]] inline bool supports_rule_requirements(const SelectionRule& rule, const core::TargetMachine& target) noexcept {
463:     if (rule.require_fma && !target.get_caps().has_fma) {
464:         return false;
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
