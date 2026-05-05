# SDK Update Requirement - PASS ARCHITECTURE ANALYSIS

## Problem Identified

The SDK's `dgtl_optimize_with_profile()` function creates a DGTLoptManager but the pass execution model is different than initially assessed.

### Current Architecture Discovery

**Key Finding**: The passes are NOT manually registered via `add_opt()` in the SDK. Instead:

1. **DGTLoptManager::execute()** is called directly with a `TargetPrecision` level
2. Passes are selected internally based on precision level and execution policy
3. Many passes have complex dependencies (FormalVerifier, MemorySmasher, CostModel, etc.)
4. SDK consumers don't directly instantiate passes - the pipeline handles this

### Current SDK Code (dgtl_sdk_impl.cpp, line 732-750)
```cpp
extern "C" DGTL_API dgtl_status_t dgtl_optimize_with_profile(dgtl_stream_t* s,
                                                              dgtl_opt_profile_t profile) {
    // ... setup ...
    
    dgtl::pipeline::DGTLoptManager mgr(precision);  // Created with TargetPrecision
    mgr.set_execution_policy(policy);               // Policy controls execution
    mgr.execute(stream, precision);                 // Precision-based pass selection
    
    // SIMD profile gets additional legalizer
    if (profile == DGTL_OPT_PROFILE_SIMD) {
        // ... SIMD legalizer setup ...
    }
    
    return make_ok();
}
```

## Architecture: How Pass Selection Really Works

### DGTLoptManager Design
The pass manager works through **precision-driven selection**, not explicit registration:

```cpp
class DGTLoptManager {
    std::vector<std::unique_ptr<DGTLOptimizationPass>> passes;  // Internal pass list
    
    bool execute(ir::IRStream& stream, TargetPrecision precision) {
        // 1. Iterates through passes in stage order
        // 2. Skips passes based on policy and stage matching
        // 3. Runs matching passes through their execute() method
    }
    
    std::vector<DGTLOptimizationPass*> select_passes_for_region(
        const graph::Region* region,
        DGTLAICostModel& cost_model
    ) {
        // Selects passes based on region characteristics and affinities
    }
};
```

### Pass Initialization Mystery
**UNRESOLVED**: Where are the passes initially populated into the manager's internal `passes` vector?

- [ ] Not done in SDK (confirmed - no `add_opt()` calls)
- [ ] Not in `execute()` function
- [ ] Likely done in **Pass_Decision_Engine** or **Analysis_Manager**
- [ ] OR passes might be lazily instantiated on first use
- [ ] OR might be global/static initialization

**This needs investigation** - the passes list is populated somehow, but the mechanism isn't clear from SDK code alone.

## Actual SDK Pass Availability

### What Gets Run
Based on current SDK code:

| Profile | Passes That Run | Method |
|---------|-----------------|--------|
| DEFAULT | Determined by `TargetPrecision::HFT` | Policy-based selection |
| FOLD | Determined by policy `favor_binary_size=true` | Policy-based selection |
| FUSION | Determined by `TargetPrecision::HPC` | Precision-based selection |
| SIMD | HPC precision + SIMD_Legalizer + custom arch tuning | Mixed |
| PRESSURE | `TargetPrecision::MOBILE` + policy | Policy-based selection |
| PREDICATION | Default + safety policy `enforce_safety_checks=true` | Policy-based selection |

### SIMD Profile (Only Explicit Pass)
The ONLY pass explicitly run is the SIMD_Legalizer:
```cpp
if (profile == DGTL_OPT_PROFILE_SIMD) {
    dgtl::pass::DGTLSIMDLegalizer legalizer(simd_arch);
    legalizer.legalize(stream);  // ← Directly called, not via manager
}
```

## Status: Not Actually "Broken"

**Revision**: The SDK is NOT broken - it's working as designed, just differently:

1. ✅ SDK runs SOME optimizations via `DGTLoptManager::execute()`
2. ✅ Pass selection is precision/policy-driven, not explicit
3. ✅ SIMD gets dedicated legalizer
4. ✅ Compilation succeeds, runs without errors

**But**: We don't know WHICH passes actually run per profile until we find where they're registered.

## Required Investigation

### Files to Check
1. [ ] **Pass_Decision_Engine.hpp** - Does it register passes?
2. [ ] **Analysis_Manager.hpp** - Does it initialize passes?
3. [ ] **Global initialization** - Static constructors? Module initialization?
4. [ ] **Main.cpp** - How does it set up passes for comparison?

### Questions Needing Answers
1. Where are the 26 passes loaded into DGTLoptManager?
2. What determines which passes run at each TargetPrecision level?
3. Is the pass list the same for all profiles or profile-specific?
4. Can the SDK profile API actually select different pass sets?

## Files to Update

### Priority 1 (CLARIFICATION)
- [ ] Investigate pass initialization mechanism in pipeline components
- [ ] Document actual pass selection logic per profile
- [ ] Verify whether SDK truly provides profile-specific optimization

### Priority 2 (DOCUMENTATION)
- [ ] Update SDK comments in dgtl.h with actual pass behavior
- [ ] Add documentation about precision-driven vs. explicit pass selection
- [ ] Clarify which passes are guaranteed to run per profile

### Priority 3 (IF REQUIRED)
- [ ] If pass selection is insufficient, implement explicit `add_opt()` registration
- [ ] Create pass factory that handles complex dependencies
- [ ] Add profile-specific pass configuration API

## Current Assessment

**Status**: ⚠️ DESIGN WORKING AS INTENDED (but needs clarification)

The SDK is NOT a "broken empty pipeline" - it's a precision/policy-driven system. But without knowing the actual pass selection logic, we can't verify that the 26 passes are accessible through the SDK interface.

**Next Step**: Find where passes are actually registered and ensure SDK profiles actually provide value beyond just changing TargetPrecision and PassExecutionPolicy.

