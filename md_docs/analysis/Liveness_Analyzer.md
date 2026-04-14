# DGTL Liveness Analyzer (Liveness_Analyzer.hpp)

## Control Flow Map

```
[LivenessAnalyzer Class]
    |
    +--> Constructor (LivenessAnalyzer(int target_ram_gb = 32))
    |       +--> Calculate max_results based on RAM (8GB: 512K, 16GB: 1M, 32GB: 2M, >32GB: 8M)
    |       +--> Allocate aligned memory for ranges array (64-byte alignment)
    |       +--> Reserve space for active_ids vector
    |       +--> Call cold_reset() to initialize
    |
    +--> Destructor
    |       +--> Free aligned memory (_aligned_free or std::free)
    |
    +--> Public Methods
    |       +--> analyze_stream(const ir::IRStream& stream)
    |       |       +--> Check for null/empty stream
    |       |       +--> Sparse reset of active_ids ranges
    |       |       +--> Reverse pass through stream
    |       |       |       +--> For each node (reverse order):
    |       |       |       |       +--> Track uses (op_a, op_b, op_c if not SVE)
    |       |       |       |       +--> Track definitions (result_id)
    |       |       |       |       +--> Check for ID overflow
    |       |       |       +--> Call verify_consistency()
    |       |       |
    |       +--> is_live_at(uint32_t result_id, uint32_t ins_idx)
    |               +--> Check if result_id is live at instruction index
    |               +--> Return range check result
    |
    +--> Private Methods
            +--> cold_reset()
            |       +--> Full memset of ranges array (0xFF for start, 0 for end)
            |
            +--> track_id(uint32_t id)
            |       +--> Add id to active_ids if not already tracked
            |
            +--> track_and_mark_use(uint32_t id, uint32_t idx)
            |       +--> Track id and set end range if first use
            |
            +--> verify_consistency()
            |       +--> Check all active_ids have definitions
            |       +--> Trigger corruption trap for dangling IDs
            |
            +--> trigger_corruption_trap(uint32_t id, const char* msg)
                    +--> Print error message and abort
```

**Flow Description**: The liveness analyzer performs reverse-pass analysis to compute live ranges, with integrated sentinel verification to detect IR corruption. It uses sparse resets and aligned memory for high performance.

## Summary

`Liveness_Analyzer.hpp` implements ultra-fast liveness analysis with Pillar 3 sentinel integration, detecting live ranges of result IDs through reverse-pass traversal while enforcing IR invariants. It provides corruption detection for dangling IDs and supports adaptive memory sizing for different RAM configurations.

Key features:
- Reverse-pass liveness analysis with O(N) complexity
- Sentinel verification for IR corruption detection
- Adaptive memory allocation (512K to 8M result IDs)
- 64-byte aligned memory for SIMD-accelerated scans
- Sparse reset optimization for consecutive analyses
- Zero-corruption guarantee through invariant checking

## Full Explanation

### Architecture Overview
The LivenessAnalyzer serves as Pillar 3's sentinel system for liveness analysis:

1. **Reverse-Pass Algorithm**: Single pass from end to start for efficiency
2. **Sentinel Integration**: Dangling ID detection and invariant verification
3. **Adaptive Sizing**: RAM-based capacity scaling (8GB to 128GB+ support)
4. **SIMD Alignment**: 64-byte aligned ranges for potential vectorization
5. **Sparse Optimization**: Only reset previously used IDs between analyses

### Key Classes and Structures

#### LivenessRange
Live range representation:

```cpp
struct alignas(8) LivenessRange {
    uint32_t start = 0xFFFFFFFF; // 0xFFFFFFFF indicates undefined
    uint32_t end = 0;            // 0 indicates unused
};
```

- **Start**: First definition index (set during reverse pass)
- **End**: Last use index (set during reverse pass)
- **8-byte Alignment**: Dense packing and SIMD compatibility

#### LivenessAnalyzer
Main analyzer class:

```cpp
class LivenessAnalyzer {
private:
    size_t max_results;
    LivenessRange* ranges;        // Aligned array of live ranges
    std::vector<uint32_t> active_ids; // IDs used in current analysis

public:
    explicit LivenessAnalyzer(int target_ram_gb = 32);
    ~LivenessAnalyzer();
    
    void analyze_stream(const ir::IRStream& stream);
    bool is_live_at(uint32_t result_id, uint32_t ins_idx) const noexcept;
};
```

### Memory Management

#### Adaptive Constructor
```cpp
explicit LivenessAnalyzer(int target_ram_gb = 32) {
    if (target_ram_gb <= 8)        max_results = 1024 * 512;      // 512K
    else if (target_ram_gb <= 16)  max_results = 1024 * 1024;     // 1M
    else if (target_ram_gb <= 32)  max_results = 1024 * 1024 * 2; // 2M
    else                           max_results = 1024 * 1024 * 8; // 8M
    
    size_t total_size = sizeof(LivenessRange) * max_results;
    
    // Platform-specific aligned allocation
    #if defined(_MSC_VER) || defined(__MINGW32__)
        ranges = static_cast<LivenessRange*>(_aligned_malloc(total_size, 64));
    #else
        if (total_size % 64 != 0) total_size += (64 - (total_size % 64));
        ranges = static_cast<LivenessRange*>(std::aligned_alloc(64, total_size));
    #endif
    
    if (!ranges) std::abort();
    
    active_ids.reserve(max_results / 128);
    cold_reset();
}
```

- **Capacity Scaling**: 512K to 8M result IDs based on RAM
- **64-byte Alignment**: SIMD-ready memory layout
- **Platform Support**: Windows (_aligned_malloc) and POSIX (aligned_alloc)
- **Vector Reservation**: active_ids sized for 1/128 of max_results

#### Memory Cleanup
```cpp
~LivenessAnalyzer() {
    #if defined(_MSC_VER) || defined(__MINGW32__)
        _aligned_free(ranges);
    #else
        std::free(ranges);
    #endif
}
```

- **Platform-Specific Free**: Matches allocation method
- **No Exceptions**: Follows zero-exception policy

### Analysis Algorithm

#### Stream Analysis
```cpp
void analyze_stream(const ir::IRStream& stream) {
    if (stream.is_null() || stream.size() == 0) return;
    
    // Sparse reset - only clear previously used IDs
    for (uint32_t id : active_ids) {
        ranges[id].start = 0xFFFFFFFF;
        ranges[id].end = 0;
    }
    active_ids.clear();
    
    const uint32_t count = static_cast<uint32_t>(stream.size());
    
    // Reverse pass analysis
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        const auto& node = stream.get_node(static_cast<size_t>(i));
        const uint32_t current_idx = static_cast<uint32_t>(i);
        
        // Track operand uses
        if (node.op_a != ir::INVALID_SLOT) track_and_mark_use(node.op_a, current_idx);
        if (node.op_b != ir::INVALID_SLOT) track_and_mark_use(node.op_b, current_idx);
        if (node.op_c != ir::INVALID_SLOT && !(node.flags & ir::Flags::SVE_ACTIVE)) {
            track_and_mark_use(node.op_c, current_idx);
        }
        
        // Track result definition
        if (node.result_id != 0) {
            if (node.result_id >= max_results) {
                trigger_corruption_trap(node.result_id, "ID_OVERFLOW");
            }
            
            track_id(node.result_id);
            ranges[node.result_id].start = current_idx;
        }
    }
    
    verify_consistency();
}
```

- **Sparse Reset**: O(active) instead of O(max_results) reset
- **Reverse Traversal**: Definitions found first, uses marked as encountered
- **Operand Tracking**: op_a, op_b, op_c (conditional on SVE flag)
- **Definition Tracking**: result_id with overflow protection
- **Sentinel Verification**: Post-analysis consistency check

#### Live Range Query
```cpp
bool is_live_at(uint32_t result_id, uint32_t ins_idx) const noexcept {
    if (result_id >= max_results) return false;
    const auto& r = ranges[result_id];
    return (ins_idx >= r.start && ins_idx <= r.end);
}
```

- **Bounds Check**: Prevent out-of-bounds access
- **Range Test**: Instruction index within [start, end]
- **Noexcept**: Exception-free query operation

### Private Implementation Details

#### Cold Reset
```cpp
void cold_reset() noexcept {
    std::memset(ranges, 0xFF, sizeof(LivenessRange) * max_results);
    for(size_t i = 0; i < max_results; ++i) ranges[i].end = 0;
}
```

- **Full Reset**: Initialize all ranges to undefined state
- **Start = 0xFFFFFFFF**: Indicates no definition found
- **End = 0**: Indicates no uses found

#### ID Tracking
```cpp
inline void track_id(uint32_t id) noexcept {
    if (id < max_results && ranges[id].start == 0xFFFFFFFF && ranges[id].end == 0) {
        active_ids.push_back(id);
    }
}
```

- **First-Time Tracking**: Only add if not previously tracked
- **Active Set**: Maintains list of IDs used in current analysis
- **Inline**: Performance-critical function

#### Use Tracking
```cpp
inline void track_and_mark_use(uint32_t id, uint32_t idx) noexcept {
    if (id >= max_results) return;
    track_id(id);
    if (ranges[id].end == 0) ranges[id].end = idx;
}
```

- **Bounds Check**: Skip invalid IDs
- **End Marker**: Set last use index (first encounter in reverse pass)
- **Track Addition**: Ensure ID is in active set

#### Consistency Verification
```cpp
void verify_consistency() {
    for (uint32_t id : active_ids) {
        if (ranges[id].start == 0xFFFFFFFF) {
            trigger_corruption_trap(id, "DANGLING_ID_DETECTED");
        }
    }
}
```

- **Sentinel Check**: Every used ID must have a definition
- **Dangling Detection**: Use without preceding definition
- **Corruption Trap**: Fatal error on invariant violation

#### Corruption Trap
```cpp
[[noreturn]] void trigger_corruption_trap(uint32_t id, const char* msg) {
    std::cerr << "DGTL_CORRUPTION_TRAP: " << msg << " [ResultID: " << id << "]\n";
    std::abort();
}
```

- **Error Reporting**: Descriptive message with problematic ID
- **Fatal Halt**: std::abort() for zero-corruption guarantee
- **No Return**: [[noreturn]] attribute

### Performance Characteristics

- **Time Complexity**: O(N) for analysis, O(1) for queries
- **Space Complexity**: O(max_results) for ranges, O(active) for tracking
- **Memory Usage**: 16-128MB for ranges (8-64MB for 32-bit systems)
- **Cache Efficiency**: Aligned memory and sparse resets
- **SIMD Ready**: 64-byte alignment for potential vectorization

### Integration Points

- **Pass Manager**: Liveness information for optimizations
- **Register Allocation**: Live range data for assignment
- **Dead Code Elimination**: Identify unused definitions
- **Sentinel System**: Pillar 3 corruption detection
- **IR Verification**: Invariant checking during compilation

### Usage Example

```cpp
#include "dgtl/analysis/Liveness_Analyzer.hpp"

int main() {
    // Create analyzer for 32GB system (2M result IDs)
    dgtl::core::LivenessAnalyzer analyzer(32);
    
    // Analyze IR stream
    analyzer.analyze_stream(ir_stream);
    
    // Query liveness at specific points
    bool live_at_10 = analyzer.is_live_at(result_id, 10);
    bool live_at_50 = analyzer.is_live_at(result_id, 50);
    
    // Use for optimization decisions
    if (!live_at_10) {
        // result_id is dead at instruction 10
        // Can eliminate or reuse register
    }
    
    return 0;
}
```

### Error Handling

- **ID Overflow**: Detected during analysis, triggers corruption trap
- **Dangling IDs**: Use without definition, fatal error
- **Memory Allocation**: Failure causes std::abort()
- **Null Streams**: Gracefully handled (no-op)

### Future Enhancements

- SIMD-accelerated range queries
- Incremental analysis for streaming IR
- Memory pressure adaptation
- Parallel analysis for multi-threaded compilation
- Integration with SSA construction

This header provides the liveness analysis foundation for DGTL's optimization pipeline, ensuring accurate live range information while maintaining IR integrity through sentinel verification.