# X64_Reg_Map.hpp Documentation

## Map of the Control Flow

```
x86_64 Register Mapping & Instruction Encoding
├── X64Reg Enum
│   ├── General Purpose Registers: RAX, RBX, RCX, RDX, RSI, RDI
│   ├── Extended Registers: R8-R15
│   ├── Stack Registers: RSP, RBP (protected)
│   └── Special Values: INVALID = 0xFF
├── X64Encoding Class
│   ├── rex(w, r, x, b): REX prefix encoding
│   │   ├── W bit: 64-bit operand size
│   │   ├── R bit: ModR/M reg extension
│   │   ├── X bit: SIB index extension
│   │   └── B bit: ModR/M r/m or SIB base extension
│   ├── modrm(mod, reg, rm): ModR/M byte encoding
│   │   ├── mod: Addressing mode (00=indirect, 01=8-bit disp, 10=32-bit disp, 11=register)
│   │   ├── reg: Source/destination register (3 bits)
│   │   └── rm: Second register or memory base (3 bits)
│   ├── sib(ss, index, base): SIB byte encoding
│   │   ├── ss: Scale factor (0=1, 1=2, 2=4, 3=8)
│   │   ├── index: Index register for scaled addressing
│   │   └── base: Base register for addressing
│   ├── map_DGTL_to_phys(DGTL_id): Virtual to physical register mapping
│   │   ├── IDs 0-5: RAX, RBX, RCX, RDX, RSI, RDI
│   │   ├── Skip RSP(4), RBP(5) for stack protection
│   │   ├── IDs 6-15: R8-R15 extended registers
│   │   └── Invalid IDs: Return X64Reg::INVALID
│   └── requires_extension(reg): Check for REX prefix requirement
│       ├── Registers ≥ 8: Require REX extension bits
│       └── INVALID register: No extension needed
└── Hardware Integration
    ├── x86_64 Architecture: Full 64-bit instruction encoding
    ├── REX Prefixes: Support for extended registers and 64-bit operations
    ├── ModR/M Addressing: Flexible memory and register operations
    ├── SIB Addressing: Scaled index addressing modes
    ├── Register Protection: RSP/RBP isolation for stack safety
    ├── Virtual Mapping: DGTL register IDs to physical registers
```

**Data Flow:**
1. **Virtual Register Input**: DGTL register ID from IR
2. **Physical Mapping**: Convert to X64Reg enum value
3. **Extension Check**: Determine if REX prefix needed
4. **Instruction Encoding**: Generate REX, ModR/M, SIB bytes
5. **Binary Output**: Complete x86_64 instruction bytes

## Summary

X64_Reg_Map.hpp implements x86_64 register mapping and instruction encoding utilities for DGTL's native code generation. The module provides hardware-native encoding functions for REX prefixes, ModR/M bytes, and SIB addressing, along with a safe virtual-to-physical register mapping that protects stack integrity by isolating RSP and RBP.

Key features include:
- **Complete x86_64 Support**: Full instruction encoding for 64-bit operations
- **REX Prefix Handling**: Proper encoding for extended registers and operand sizes
- **ModR/M Addressing**: Flexible register and memory operand encoding
- **SIB Addressing**: Scaled index addressing for complex memory operations
- **Stack Protection**: RSP/RBP isolation prevents stack corruption
- **Virtual Register Mapping**: Safe DGTL-to-physical register translation
- **Extension Detection**: Automatic REX prefix requirement checking

The implementation enables direct generation of x86_64 machine code with hardware-native efficiency.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <cstdint>

namespace dgtl::exec {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<cstdint>`: Fixed-width integer types for encoding

### X64Reg Enum
```cpp
enum class X64Reg : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    INVALID = 0xFF
};
```
**Purpose**: Physical x86_64 register enumeration
**Standard Registers**: RAX, RBX, RCX, RDX, RSI, RDI
**Extended Registers**: R8-R15 (require REX prefix)
**Stack Registers**: RSP, RBP (protected from general use)
**Special Values**: INVALID for error handling

### X64Encoding Class

#### rex()
```cpp
static constexpr inline uint8_t rex(bool w, uint8_t r, uint8_t x, uint8_t b) noexcept {
    uint8_t res = 0x40;
    if (w) res |= 0x08;         // Bit 3: 64-bit operand size
    if (r & 0x08) res |= 0x04;  // Bit 2: Extension of ModR/M reg
    if (x & 0x08) res |= 0x02;  // Bit 1: Extension of SIB index
    if (b & 0x08) res |= 0x01;  // Bit 0: Extension of ModR/M r/m or SIB base
    return res;
}
```
**Purpose**: Encode REX prefix byte for x86_64 instructions
**Parameters**:
- **w**: 64-bit operand size (REX.W)
- **r**: ModR/M reg field extension (REX.R)
- **x**: SIB index field extension (REX.X)
- **b**: ModR/M r/m or SIB base extension (REX.B)
**Return**: Complete REX prefix byte (0x40-0x4F)

#### modrm()
```cpp
static constexpr inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) noexcept {
    return ((mod & 0x03) << 6) | ((reg & 0x07) << 3) | (rm & 0x07);
}
```
**Purpose**: Encode ModR/M addressing byte
**Parameters**:
- **mod**: Addressing mode (2 bits)
  - 00: Register indirect or SIB with no displacement
  - 01: 8-bit displacement
  - 10: 32-bit displacement
  - 11: Register-to-register
- **reg**: Source/destination register (3 bits)
- **rm**: Second register or memory base (3 bits)
**Return**: Complete ModR/M byte

#### sib()
```cpp
static constexpr inline uint8_t sib(uint8_t ss, uint8_t index, uint8_t base) noexcept {
    return ((ss & 0x03) << 6) | ((index & 0x07) << 3) | (base & 0x07);
}
```
**Purpose**: Encode SIB (Scale-Index-Base) byte for complex addressing
**Parameters**:
- **ss**: Scale factor (2 bits): 0=1, 1=2, 2=4, 3=8
- **index**: Index register (3 bits)
- **base**: Base register (3 bits)
**Return**: Complete SIB byte

#### map_DGTL_to_phys()
```cpp
static constexpr X64Reg map_DGTL_to_phys(uint32_t DGTL_id) noexcept {
    switch (DGTL_id) {
        case 0: return X64Reg::RAX;
        case 1: return X64Reg::RBX;
        case 2: return X64Reg::RCX;
        case 3: return X64Reg::RDX;
        case 4: return X64Reg::RSI;
        case 5: return X64Reg::RDI;
        // Gap 4 (RSP) and 5 (RBP) are skipped for stability
        default: 
            // [DGTL UPDATE]: Expanded mapping to include R14 and R15
            // Map DGTL IDs 6 through 15 to R8 through R15
            if (DGTL_id >= 6 && DGTL_id <= 15) {
                return static_cast<X64Reg>(8 + (DGTL_id - 6));
            }
            return X64Reg::INVALID;
    }
}
```
**Purpose**: Map DGTL virtual registers to physical x86_64 registers
**Mapping**:
- **IDs 0-5**: RAX, RBX, RCX, RDX, RSI, RDI
- **Skip 4-5**: RSP, RBP protected for stack management
- **IDs 6-15**: R8-R15 extended registers
- **Invalid**: Return X64Reg::INVALID

#### requires_extension()
```cpp
static constexpr inline bool requires_extension(X64Reg reg) noexcept {
    return static_cast<uint8_t>(reg) >= 8 && static_cast<uint8_t>(reg) != 0xFF;
}
```
**Purpose**: Check if register requires REX prefix extension
**Logic**: Registers ≥ 8 need REX bits, except INVALID (0xFF)

### Namespace Closing
```cpp
} // namespace dgtl::exec
```
- Closes the exec namespace

## Usage Example
```cpp
#include "X64_Reg_Map.hpp"

// Map DGTL virtual register to physical
uint32_t dgtl_reg = 7; // DGTL register ID
dgtl::exec::X64Reg phys_reg = dgtl::exec::X64Encoding::map_DGTL_to_phys(dgtl_reg);
// phys_reg = X64Reg::R9

// Check if REX prefix needed
bool needs_rex = dgtl::exec::X64Encoding::requires_extension(phys_reg);
// needs_rex = true (R9 >= 8)

// Encode REX prefix for 64-bit ADD operation
uint8_t rex_byte = dgtl::exec::X64Encoding::rex(
    true,   // W: 64-bit operands
    1,      // R: reg extension (for destination)
    0,      // X: no index extension
    9       // B: rm extension (for R9)
);
// rex_byte = 0x49 (REX.W = 1, R = 0, X = 0, B = 1)

// Encode ModR/M for register-to-register ADD
uint8_t modrm_byte = dgtl::exec::X64Encoding::modrm(
    3,      // mod: 11 (register-to-register)
    0,      // reg: 000 (RAX as source)
    1       // rm: 001 (RCX as destination, but with REX.B makes it R9)
);
// modrm_byte = 0xC1

// Complete instruction: REX + ADD + ModR/M = 49 01 C1 (add r9, rax)
```

## Performance Characteristics
- **Encoding Speed**: All functions are constexpr and inline
- **Zero Overhead**: Compile-time evaluation where possible
- **Memory Usage**: No dynamic allocation
- **Register Mapping**: O(1) lookup with bounded switch
- **Extension Checking**: Simple integer comparison
- **Instruction Size**: Minimal encoding overhead

## x86_64 Architecture Compliance
- **REX Prefixes**: Full support for 64-bit extensions
- **Register Set**: Complete GPR coverage (RAX-R15)
- **Addressing Modes**: ModR/M and SIB encoding
- **Stack Protection**: RSP/RBP isolation for safety
- **Extension Bits**: Proper REX.R, REX.X, REX.B handling
- **Operand Sizes**: 64-bit operation support (REX.W)
- **Virtual Mapping**: Safe DGTL-to-physical translation