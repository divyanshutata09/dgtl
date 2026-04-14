# DGTL Target (Target.hpp)

## Control Flow Map

```
[TargetMachine Class]
    |
    +--> Constructor (TargetMachine(ArchType arch))
    |       +--> Store architecture type
    |       +--> Call initialize_target()
    |
    +--> Architecture Queries
    |       +--> get_arch()
    |       |       +--> Return architecture type
    |       |
    |       +--> supports_native(OpCode op)
    |       |       +--> Check AVX-512 support for vector ops
    |       |       +--> Return native support boolean
    |       |
    |       +--> get_gp_register_count()
    |       |       +--> Return 16 for x86-64, 32 for others
    |       |
    |       +--> get_caps()
    |               +--> Return capabilities struct
    |
    +--> Emission Constraints
    |       +--> get_preferred_alignment(OpCode op)
    |       |       +--> Return 64 for vector ops, 8 for others
    |       |
    |       +--> prefer_branchless()
    |               +--> Return true for x86-64 AVX-512
    |
    +--> Private Initialization
            +--> initialize_target()
                    +--> Set capabilities based on architecture
                    +--> Enable AVX-512, FMA, BMI2 for x86-64
```

**Flow Description**: The target machine encapsulates hardware-specific knowledge for code generation. It provides architecture queries for native instruction support, register counts, and emission constraints to guide the compiler's lowering decisions.

## Summary

`Target.hpp` implements hardware-aware target machine abstraction with architecture-specific capabilities and emission constraints, enabling DGTL's "optimization through physical awareness" philosophy. It provides compile-time hardware specialization for x86-64 AVX-512, AArch64 SVE, FPGA, and WASM targets with native instruction support detection and alignment requirements.

Key features:
- Multi-architecture support (x86-64, AArch64, FPGA, WASM)
- Hardware capability detection and feature flags
- Native instruction support queries
- Register count and alignment constraint specification
- Branchless code generation preferences
- Pipeline configuration for optimization levels

## Full Explanation

### Architecture Overview
The TargetMachine serves as Pillar 4's hardware affinity system for DGTL's backend:

1. **Architecture Abstraction**: Unified interface across target platforms
2. **Capability Detection**: Feature flags for hardware-specific optimizations
3. **Emission Guidance**: Alignment and instruction selection constraints
4. **Performance Tuning**: Architecture-specific optimization preferences
5. **Extensibility**: Support for new target architectures

### Key Classes and Structures

#### ArchType Enumeration
Supported target architectures:

```cpp
enum class ArchType : uint8_t {
    X86_64_AVX512,  // Primary: HFT/AI infrastructure
    AARCH64_SVE,    // Mobile/Edge performance
    FPGA_VERILOG,   // Custom silicon acceleration
    WASM_SIMD       // Web performance
};
```

- **Tiered Support**: Primary and secondary architectures
- **Specialized Use**: HFT, mobile, hardware, web deployment
- **SIMD Focus**: Vector instruction set support

#### Capabilities Structure
Hardware feature flags:

```cpp
struct Capabilities {
    bool has_avx512 = false;     // AVX-512 vector instructions
    bool has_amx = false;        // AMX matrix operations
    bool has_fma = false;        // Fused multiply-add
    bool has_bmi2 = false;       // Bit manipulation v2
    uint32_t cache_line_size = 64; // Cache line size in bytes
};
```

- **Vector Extensions**: AVX-512 for 512-bit operations
- **Matrix Support**: AMX for AI workloads
- **Arithmetic**: FMA for fused operations
- **Bit Operations**: BMI2 for advanced bit manipulation
- **Cache Awareness**: Line size for alignment decisions

#### TargetMachine Class
Main target abstraction:

```cpp
class alignas(64) TargetMachine {
    ArchType m_arch;
    Capabilities m_caps;
    
public:
    explicit TargetMachine(ArchType arch);
    
    ArchType get_arch() const noexcept;
    bool supports_native(OpCode op) const noexcept;
    uint32_t get_gp_register_count() const noexcept;
    const Capabilities& get_caps() const noexcept;
    
    size_t get_preferred_alignment(OpCode op) const noexcept;
    bool prefer_branchless() const noexcept;
};
```

### Architecture-Specific Logic

#### Native Instruction Support
```cpp
bool supports_native(OpCode op) const noexcept {
    if (m_arch == ArchType::X86_64_AVX512) {
        // Native 512-bit vector operations
        if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::GATHER_F32_512) 
            return m_caps.has_avx512;
        return true; // Assume other ops supported
    }
    return false; // Conservative for other architectures
}
```

- **AVX-512 Detection**: Hardware capability checking
- **Vector Operations**: 512-bit instruction support
- **Conservative Fallback**: Unknown ops assumed unsupported
- **Architecture Specific**: Different logic per target

#### Register Count Specification
```cpp
uint32_t get_gp_register_count() const noexcept {
    return (m_arch == ArchType::X86_64_AVX512) ? 16 : 32;
}
```

- **x86-64**: 16 general-purpose registers (RAX-R15)
- **AArch64**: 32 general-purpose registers (X0-X31)
- **Allocation Guidance**: Register allocator constraints

### Emission Constraints

#### Memory Alignment Requirements
```cpp
size_t get_preferred_alignment(OpCode op) const noexcept {
    if (op == OpCode::STORE_V512 || op == OpCode::LOAD) return 64;
    return 8; // Default byte alignment
}
```

- **Vector Operations**: 64-byte alignment for AVX-512
- **Memory Operations**: Cache line alignment for performance
- **Default**: 8-byte alignment for general operations

#### Branching Preferences
```cpp
bool prefer_branchless() const noexcept {
    return m_arch == ArchType::X86_64_AVX512;
}
```

- **x86-64**: CMOV instructions for branchless code
- **Performance**: Avoid branch misprediction penalties
- **Architecture Specific**: Branchless vs. branching tradeoffs

### Target Initialization

#### Capability Setup
```cpp
void initialize_target() {
    if (m_arch == ArchType::X86_64_AVX512) {
        m_caps.has_avx512 = true;
        m_caps.has_fma = true;
        m_caps.has_bmi2 = true;
        m_caps.cache_line_size = 64;
    }
    // Additional architecture setup...
}
```

- **Feature Detection**: Hardware capability flags
- **Cache Line Size**: Architecture-specific values
- **Extensible**: Easy addition of new capabilities

### Pipeline Configuration

#### TargetPipeline Structure
Optimization pipeline settings:

```cpp
struct TargetPipeline {
    bool enable_gvn = true;         // Global value numbering
    bool enable_dce = true;         // Dead code elimination
    bool enable_loop_unroll = true; // Loop unrolling
    uint32_t optimization_level = 3; // 0-3 (DGTL Extreme)
};
```

- **Standard Optimizations**: GVN, DCE, loop transformations
- **Level Control**: Optimization aggressiveness
- **Target Specific**: Pipeline configuration per architecture

### Performance Characteristics

- **Query Speed**: O(1) capability and constraint lookups
- **Memory Usage**: Minimal (enum + struct storage)
- **Initialization**: O(1) capability setup
- **Extensibility**: Easy addition of new architectures

### Integration Points

- **Code Generation**: Native instruction selection
- **Register Allocation**: Architecture-specific constraints
- **Instruction Selection**: Alignment and branching decisions
- **Optimization Passes**: Target-aware transformations
- **JIT Compilation**: Runtime hardware detection

### Usage Example

```cpp
#include "dgtl/core/Target.hpp"

int main() {
    // Create target for AVX-512 capable system
    dgtl::ir::TargetMachine target(dgtl::ir::ArchType::X86_64_AVX512);
    
    // Query architecture
    auto arch = target.get_arch(); // ArchType::X86_64_AVX512
    
    // Check native instruction support
    bool has_vec_add = target.supports_native(dgtl::ir::OpCode::VEC_ADD_F32_512);
    
    // Get register count for allocation
    uint32_t gp_regs = target.get_gp_register_count(); // 16
    
    // Query capabilities
    const auto& caps = target.get_caps();
    if (caps.has_avx512) {
        // Enable AVX-512 optimizations
    }
    
    // Get alignment requirements
    size_t align = target.get_preferred_alignment(dgtl::ir::OpCode::STORE_V512); // 64
    
    // Check branching preference
    bool branchless = target.prefer_branchless(); // true for x86-64
    
    return 0;
}
```

### Backward Compatibility

```cpp
// Legacy namespace aliases
namespace dgtl::ir {
    using ArchType = dgtl::ir::ArchType;
    using TargetMachine = dgtl::ir::TargetMachine;
    using TargetPipeline = dgtl::ir::TargetPipeline;
}
```

### Future Enhancements

- Runtime CPU feature detection
- Dynamic capability updates
- Microarchitecture-specific tuning
- Custom ISA support
- Performance counter integration
- Energy-aware optimization

This header provides the hardware abstraction layer for DGTL's backend, enabling architecture-aware code generation and optimization across multiple target platforms.