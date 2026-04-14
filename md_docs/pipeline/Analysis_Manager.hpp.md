# Analysis_Manager.hpp Documentation

## Map of the Control Flow

```
DGTL Analysis Manager (Lazy Analysis Caching)
├── DGTLAnalysis Abstract Base
│   ├── virtual ~DGTLAnalysis()
│   └── virtual get_analysis_name() const noexcept = 0
├── AnalysisCacheEntry Struct
│   ├── std::unique_ptr<DGTLAnalysis> result
│   └── uint64_t ir_version_at_compute
├── DGTLAnalysisManager Class
│   ├── Cache Storage
│   │   ├── std::unordered_map<type_index, AnalysisCacheEntry> cache
│   │   └── uint64_t last_known_ir_version = 0
│   ├── Analysis Retrieval
│   │   └── get_result<AnalysisT>(stream) → const AnalysisT&
│   │       ├── Version Check: invalidate_all() if IR changed
│   │       ├── Cache Lookup: find by type_index
│   │       ├── Lazy Computation: compute() if missing
│   │       └── Cache Storage: emplace with current version
│   ├── Cache Management
│   │   ├── invalidate_all() → void (clear entire cache)
│   │   ├── is_cached<AnalysisT>() → bool
│   │   └── invalidate_specific<AnalysisT>() → void
│   └── Rule of Five
│       ├── Copy: deleted (performance reasons)
│       └── Move: defaulted
└── Integration Points
    ├── IRStream: get_version() for invalidation
    ├── Analysis Classes: Must inherit DGTLAnalysis and implement compute()
    ├── Pass Manager: Automatic invalidation on transaction commit
```

**Data Flow:**
1. **Version Check**: Compare IR version with last known version
2. **Invalidation**: Clear cache if IR has changed since last analysis
3. **Cache Lookup**: Search for analysis type in unordered_map
4. **Cache Hit**: Return existing result if found and valid
5. **Lazy Computation**: Create new analysis instance and call compute()
6. **Cache Storage**: Store result with current IR version
7. **Return**: Provide const reference to cached or computed result

## Summary

Analysis_Manager.hpp implements the DGTL Analysis Manager (TAM), a lazy computation and caching system for IR analysis results. The manager provides zero-overhead access to expensive analysis computations through version-based invalidation and template-based type safety. It integrates with the transactional pass manager to automatically invalidate stale analyses on IR mutations.

Key features include:
- **Lazy Evaluation**: Analyses computed only when first requested
- **Version-Based Caching**: Automatic invalidation on IR changes
- **Type-Safe Templates**: Compile-time analysis type resolution
- **Memory Efficiency**: Unique ownership prevents unnecessary copies
- **Performance Optimization**: Cached results for repeated queries
- **Transactional Integration**: Syncs with pass manager commits

The implementation achieves HFT-grade analysis caching with zero-overhead retrieval and automatic correctness guarantees.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <string_view>
#include "../ir/Global_IR.hpp"

namespace dgtl::pipeline {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<unordered_map>`: Hash map for analysis cache
  - `<memory>`: Unique pointers for ownership
  - `<typeindex>`: Runtime type identification
  - `<string_view>`: Analysis name strings
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IRStream and version tracking
- **Namespace**: dgtl::pipeline

### DGTLAnalysis Abstract Base
```cpp
class DGTLAnalysis {
public:
    virtual ~DGTLAnalysis() = default;
    [[nodiscard]] virtual std::string_view get_analysis_name() const noexcept = 0;
};
```
**Purpose**: Base class for all analysis implementations
**Requirements**:
- **Virtual Destructor**: Proper cleanup of derived classes
- **get_analysis_name()**: Human-readable analysis identifier

### AnalysisCacheEntry Struct
```cpp
struct AnalysisCacheEntry {
    std::unique_ptr<DGTLAnalysis> result;
    uint64_t ir_version_at_compute;
};
```
**Purpose**: Cache entry with version tracking
**Members**:
- **result**: Unique ownership of analysis result
- **ir_version_at_compute**: IR version when analysis was computed

### DGTLAnalysisManager Class Members
```cpp
class DGTLAnalysisManager {
private:
    struct AnalysisCacheEntry {
        std::unique_ptr<DGTLAnalysis> result;
        uint64_t ir_version_at_compute;
    };

    std::unordered_map<std::type_index, AnalysisCacheEntry> cache;
    uint64_t last_known_ir_version = 0;
```
**Members**:
- **cache**: Type-indexed analysis results
- **last_known_ir_version**: Last known IR version for invalidation

### Constructor and Rule of Five
```cpp
DGTLAnalysisManager() noexcept = default;

// RAII: Strict ownership - copying an analysis manager is a performance sin
DGTLAnalysisManager(const DGTLAnalysisManager&) = delete;
DGTLAnalysisManager& operator=(const DGTLAnalysisManager&) = delete;

DGTLAnalysisManager(DGTLAnalysisManager&&) noexcept = default;
DGTLAnalysisManager& operator=(DGTLAnalysisManager&&) noexcept = default;
```
**Constructor**: Default initialization
**Copy Operations**: Deleted for performance (no copying analysis caches)
**Move Operations**: Defaulted for ownership transfer

### Analysis Retrieval

#### get_result()
```cpp
template <typename AnalysisT>
[[nodiscard]] const AnalysisT& get_result(ir::IRStream& stream) {
    const uint64_t current_version = stream.get_version();

    // 1. Version Gate: If IR changed, purge all stale facts immediately
    if (current_version != last_known_ir_version) [[unlikely]] {
        invalidate_all();
        last_known_ir_version = current_version;
    }

    const std::type_index id = std::type_index(typeid(AnalysisT));
    
    // 2. Optimized Lookup: Use find() to avoid double-hashing
    auto it = cache.find(id);
    if (it != cache.end()) [[likely]] {
        return *static_cast<const AnalysisT*>(it->second.result.get());
    }

    // 3. Lazy Strike: Compute only when requested
    auto result = std::make_unique<AnalysisT>();
    
    // Ensure AnalysisT has a compute(ir::IRStream&) method
    result->compute(stream);
    
    const AnalysisT& ref = *result;
    cache.emplace(id, AnalysisCacheEntry{ std::move(result), current_version });
    
    return ref;
}
```
**Purpose**: Retrieve cached or compute new analysis result
**Template Parameter**: AnalysisT (must inherit from DGTLAnalysis)

**Algorithm**:
1. **Version Check**: Get current IR version, invalidate if changed
2. **Cache Lookup**: Search for analysis type using type_index
3. **Cache Hit**: Return existing result if found
4. **Lazy Computation**: Create new analysis instance
5. **Computation**: Call compute(stream) method
6. **Cache Storage**: Store result with current version
7. **Return**: Const reference to result

**Requirements**: AnalysisT must have compute(ir::IRStream&) method

### Cache Management

#### invalidate_all()
```cpp
void invalidate_all() noexcept {
    cache.clear();
}
```
**Purpose**: Clear entire analysis cache
**Usage**: Called when IR version changes or pass manager commits

#### is_cached()
```cpp
template <typename AnalysisT>
[[nodiscard]] bool is_cached() const noexcept {
    return cache.contains(std::type_index(typeid(AnalysisT)));
}
```
**Purpose**: Check if analysis is already computed and cached
**Return**: true if analysis exists in cache

#### invalidate_specific()
```cpp
template <typename AnalysisT>
void invalidate_specific() noexcept {
    cache.erase(std::type_index(typeid(AnalysisT)));
}
```
**Purpose**: Remove specific analysis from cache
**Usage**: When passes know only certain analyses are invalidated

### Namespace Closing
```cpp
} // namespace dgtl::pipeline
```
- Closes the pipeline namespace

## Usage Example
```cpp
#include "Analysis_Manager.hpp"

// Create analysis manager
dgtl::pipeline::DGTLAnalysisManager am;

// Define custom analysis
class MyLivenessAnalysis : public dgtl::pipeline::DGTLAnalysis {
public:
    void compute(dgtl::ir::IRStream& stream) {
        // ... compute liveness information ...
    }
    
    std::string_view get_analysis_name() const noexcept override {
        return "MyLivenessAnalysis";
    }
    
    // ... analysis data and methods ...
};

// In optimization pass
void MyOptimizationPass::execute(dgtl::ir::IRStream& stream, 
                                dgtl::pipeline::DGTLAnalysisManager& am) {
    // Get liveness analysis (computed lazily if needed)
    const auto& liveness = am.get_result<MyLivenessAnalysis>(stream);
    
    // Use analysis results
    // ... optimization logic using liveness info ...
}

// Check if analysis is cached
if (am.is_cached<MyLivenessAnalysis>()) {
    // Analysis already computed
}

// Invalidate specific analysis if pass modifies relevant properties
am.invalidate_specific<MyLivenessAnalysis>();
```

## Dependencies
- **Global_IR.hpp**: IRStream and get_version() method
- **C++ RTTI**: typeid and type_index for type-safe caching

## Thread Safety
- **Analysis Retrieval**: Not thread-safe, requires exclusive access
- **Cache Operations**: Not thread-safe, modify during single-threaded optimization
- **Result Access**: Const references are safe for concurrent reads once computed

## Performance Characteristics
- **Lookup**: O(1) average case unordered_map access
- **Invalidation**: O(1) for specific, O(C) for all where C is cache size
- **Memory**: Unique pointers prevent unnecessary copies
- **Version Tracking**: 64-bit version numbers prevent ABA problems
- **Lazy Evaluation**: Defers computation until actually needed
- **Cache Efficiency**: Type-indexed storage minimizes collisions