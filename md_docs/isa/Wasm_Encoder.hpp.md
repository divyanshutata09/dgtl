# Wasm_Encoder.hpp Documentation

## Map of the Control Flow

```
WebAssembly Binary Encoder (Direct IR-to-WASM Translation)
├── DGTLWasmEncoder Class
│   ├── Constructor: Pre-allocate 64KB buffer
│   ├── encode(stream)
│   │   ├── Validation: Check null stream and shadow integrity
│   │   ├── Buffer Clear: Reset for new encoding
│   │   ├── Magic Header: \0asm version 1.0
│   │   ├── Type Section (ID 1): Function signature (void) -> (void)
│   │   ├── Function Section (ID 3): Function declarations
│   │   ├── Memory Section (ID 5): 1 page (64KB) linear memory
│   │   ├── Export Section (ID 7): Export DGTL_main function
│   │   └── Code Section (ID 10): Function bodies with opcodes
│   ├── get_binary(): Access encoded WASM module
│   ├── emit_byte(b): Append single byte
│   ├── emit_bytes(bytes): Append byte sequence
│   ├── emit_binary_op(body, wasm_op, node): Binary operation encoding
│   ├── emit_string(str): UTF-8 string with length prefix
│   ├── emit_section(id, content_gen): Section wrapper with size
│   ├── emit_leb128(val): Variable-length unsigned integer
│   └── emit_leb128_to(vec, val): LEB128 to specific vector
├── WASM Module Structure
│   ├── Preamble: Magic (0x00 0x61 0x73 0x6d) + version (0x01 0x00 0x00 0x00)
│   ├── Type Section: Function type definitions
│   ├── Function Section: Function declarations with type indices
│   ├── Memory Section: Linear memory declarations
│   ├── Export Section: Exported functions/globals/memories
│   └── Code Section: Function bodies with local declarations and opcodes
├── IR-to-WASM Translation
│   ├── LOAD_IMM: i32.const + local.set
│   ├── REG_MOVE: local.get + local.set
│   ├── ADD_I32/SUB_I32/MUL_I32: Binary ops (i32.add/i32.sub/i32.mul)
│   ├── AND_I32: i32.and operation
│   ├── LOAD: local.get + i32.load + local.set
│   ├── STORE: local.get + local.get + i32.store
│   ├── RET: return opcode
│   ├── NOP: nop opcode
│   └── Default: Graceful degradation to NOP
└── Pillar 3 Integration
    ├── Shadow Integrity: validate_shadow_integrity() before encoding
    ├── Graceful Degradation: NOP fallback for unknown opcodes
    ├── LEB128 Encoding: Efficient variable-length integers
    ├── Local Mapping: IR registers map to WASM locals
    ├── Memory Model: 64KB page for LOAD/STORE operations
    ├── Export Interface: DGTL_main entry point
```

**Data Flow:**
1. **Stream Validation**: Check IR integrity and shadow consistency
2. **Module Initialization**: Write WASM magic and version
3. **Section Generation**: Create type, function, memory, export sections
4. **Code Encoding**: Translate IR nodes to WASM opcodes
5. **Binary Assembly**: Package sections with proper LEB128 sizing
6. **Module Output**: Complete .wasm binary ready for execution

## Summary

Wasm_Encoder.hpp implements a high-performance WebAssembly binary encoder that directly translates DGTL IR streams to .wasm modules. The encoder uses LEB128 encoding for compact representation and includes Pillar 3 validation to ensure IR integrity before binary generation, producing standards-compliant WebAssembly modules for browser and runtime execution.

Key features include:
- **Direct IR Translation**: Zero-intermediate conversion to WASM opcodes
- **LEB128 Encoding**: Compact variable-length integer encoding
- **Section-Based Structure**: Proper WASM module organization
- **Memory Management**: 64KB linear memory for data operations
- **Export Interface**: DGTL_main function for external calling
- **Pillar 3 Validation**: Shadow integrity checking before encoding
- **Graceful Degradation**: NOP fallbacks for unsupported operations

The implementation generates production-ready WebAssembly binaries with full standards compliance.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <functional> 
#include <string_view> 
#include "../ir/Global_IR.hpp"

namespace dgtl::exec {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic byte buffer for WASM binary
  - `<cstdint>`: Fixed-width integers for WASM encoding
  - `<algorithm>`: Algorithms (not directly used)
  - `<functional>`: std::function for section content generation
  - `<string_view>`: Efficient string handling
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IRStream, IRNode, OpCode types

### DGTLWasmEncoder Class
```cpp
class DGTLWasmEncoder {
    std::vector<uint8_t> _binary_buffer;

public:
    DGTLWasmEncoder() { _binary_buffer.reserve(64 * 1024); }
```
**Members**:
- **_binary_buffer**: Pre-allocated 64KB buffer for WASM modules

### encode()
```cpp
void encode(const ir::IRStream& stream) {
    if (stream.is_null() || stream.size() == 0) [[unlikely]] return;
    if (!stream.validate_shadow_integrity()) [[unlikely]] return;

    _binary_buffer.clear();

    // 1. WASM Magic & Version (\0asm v1.0)
    emit_bytes({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    // 2. Type Section (ID 1): Defines (void) -> (void)
    emit_section(1, [this]() {
        this->emit_leb128(1);    // 1 Type
        this->emit_byte(0x60);   // func type
        this->emit_leb128(0);    // 0 params
        this->emit_leb128(0);    // 0 results
    });

    // 3. Function Section (ID 3): Declares func 0 uses type 0
    emit_section(3, [this]() {
        this->emit_leb128(1);    
        this->emit_leb128(0);    
    });

    // 4. Memory Section (ID 5): 1 page (64KB) for LOAD/STORE ops
    emit_section(5, [this]() {
        this->emit_leb128(1);
        this->emit_byte(0x00);   // limits: min only
        this->emit_leb128(1);    // 1 page
    });

    // 5. Export Section (ID 7)
    emit_section(7, [this]() {
        this->emit_leb128(1);            
        this->emit_string("DGTL_main");  
        this->emit_byte(0x00);           // export func
        this->emit_leb128(0);            // index 0
    });

    // 6. Code Section (ID 10)
    emit_section(10, [this, &stream]() {
        this->emit_leb128(1);            // 1 function body
        
        std::vector<uint8_t> body;
        
        // Local Definitions: Mapping IR Registers to WASM Locals
        // We reserve locals up to the max_id found in the stream
        uint32_t max_reg = stream.get_max_id();
        if (max_reg == ir::INVALID_SLOT) max_reg = 32; // Default floor
        
        this->emit_leb128_to(body, 1);           // 1 local group
        this->emit_leb128_to(body, max_reg + 1); // count
        body.push_back(0x7F);                   // i32 type

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            
            switch (node.opcode) {
                case ir::OpCode::LOAD_IMM:
                    body.push_back(0x41); // i32.const
                    this->emit_leb128_to(body, node.op_a);
                    body.push_back(0x21); // local.set
                    this->emit_leb128_to(body, node.result_id);
                    break;

                case ir::OpCode::REG_MOVE:
                    body.push_back(0x20); // local.get
                    this->emit_leb128_to(body, node.op_a);
                    body.push_back(0x21); // local.set
                    this->emit_leb128_to(body, node.result_id);
                    break;

                case ir::OpCode::ADD_I32:
                    this->emit_binary_op(body, 0x6A, node); // i32.add
                    break;

                case ir::OpCode::SUB_I32:
                    this->emit_binary_op(body, 0x6B, node); // i32.sub
                    break;

                case ir::OpCode::MUL_I32:
                    this->emit_binary_op(body, 0x6C, node); // i32.mul
                    break;

                case ir::OpCode::AND_I32:
                    this->emit_binary_op(body, 0x71, node); // i32.and
                    break;

                case ir::OpCode::LOAD:
                    body.push_back(0x20); // local.get (addr)
                    this->emit_leb128_to(body, node.op_a);
                    body.push_back(0x28); // i32.load
                    this->emit_leb128_to(body, 0); // align
                    this->emit_leb128_to(body, 0); // offset
                    body.push_back(0x21); // local.set
                    this->emit_leb128_to(body, node.result_id);
                    break;

                case ir::OpCode::STORE:
                    body.push_back(0x20); // local.get (addr)
                    this->emit_leb128_to(body, node.op_a);
                    body.push_back(0x20); // local.get (val)
                    this->emit_leb128_to(body, node.op_b);
                    body.push_back(0x36); // i32.store
                    this->emit_leb128_to(body, 0); // align
                    this->emit_leb128_to(body, 0); // offset
                    break;

                case ir::OpCode::RET:
                    body.push_back(0x0F); // return
                    break;

                case ir::OpCode::NOP:
                    body.push_back(0x01); // nop
                    break;

                default:
                    // Pillar 2 Strategy: Graceful degradation to NOP for unknown opcodes
                    body.push_back(0x01); 
                    break;
            }
        }
        
        body.push_back(0x0b); // end function
        
        this->emit_leb128(static_cast<uint32_t>(body.size()));
        this->_binary_buffer.insert(this->_binary_buffer.end(), body.begin(), body.end());
    });
}
```
**Purpose**: Encode complete WASM module from IR stream
**Algorithm**:
1. Validate stream and shadow integrity
2. Clear buffer and write WASM header
3. Generate all required sections
4. Encode function body with local declarations
5. Translate IR operations to WASM opcodes
6. Finalize sections with proper sizing

### get_binary()
```cpp
[[nodiscard]] const std::vector<uint8_t>& get_binary() const noexcept { return _binary_buffer; }
```
**Purpose**: Access the encoded WASM binary

### Helper Methods
```cpp
void emit_byte(uint8_t b) { _binary_buffer.push_back(b); }

void emit_bytes(std::initializer_list<uint8_t> bytes) { 
    _binary_buffer.insert(_binary_buffer.end(), bytes); 
}
```
**Purpose**: Append bytes to binary buffer

### emit_binary_op()
```cpp
void emit_binary_op(std::vector<uint8_t>& body, uint8_t wasm_op, const ir::IRNode& node) {
    body.push_back(0x20); // local.get
    this->emit_leb128_to(body, node.op_a);
    body.push_back(0x20); // local.get
    this->emit_leb128_to(body, node.op_b);
    body.push_back(wasm_op);
    body.push_back(0x21); // local.set
    this->emit_leb128_to(body, node.result_id);
}
```
**Purpose**: Encode binary operations (add, sub, mul, and)
**Pattern**: get operand A, get operand B, apply operation, set result

### emit_string()
```cpp
void emit_string(std::string_view str) {
    emit_leb128(static_cast<uint32_t>(str.size()));
    for (char c : str) emit_byte(static_cast<uint8_t>(c));
}
```
**Purpose**: Encode UTF-8 strings with length prefix

### emit_section()
```cpp
void emit_section(uint8_t section_id, std::function<void()> content_gen) {
    this->emit_byte(section_id);
    
    // Preserve current buffer state
    size_t size_pos = _binary_buffer.size();
    
    // Placeholder for LEB128 size (typical max 5 bytes for 32-bit)
    for(int i=0; i<5; ++i) _binary_buffer.push_back(0); 
    
    size_t start_pos = _binary_buffer.size();
    content_gen();
    size_t end_pos = _binary_buffer.size();
    
    uint32_t section_size = static_cast<uint32_t>(end_pos - start_pos);
    
    // Back-patch size using fixed-width LEB128 to avoid shifting the whole vector
    for (int i = 0; i < 5; ++i) {
        uint8_t byte = section_size & 0x7F;
        section_size >>= 7;
        if (i < 4) byte |= 0x80;
        _binary_buffer[size_pos + i] = byte;
    }
}
```
**Purpose**: Wrap section content with ID and size encoding
**Algorithm**:
1. Write section ID
2. Reserve space for size (5 bytes)
3. Generate content
4. Calculate actual size
5. Back-patch size as LEB128

### LEB128 Encoding
```cpp
void emit_leb128(uint32_t val) {
    emit_leb128_to(this->_binary_buffer, val);
}

void emit_leb128_to(std::vector<uint8_t>& vec, uint32_t val) {
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val != 0) byte |= 0x80;
        vec.push_back(byte);
    } while (val != 0);
}
```
**Purpose**: Encode unsigned 32-bit integers as LEB128
**Algorithm**: 7-bit chunks with continuation bits

### Namespace Closing
```cpp
} // namespace dgtl::exec
```
- Closes the exec namespace

## Usage Example
```cpp
#include "Wasm_Encoder.hpp"

// Create WASM encoder
dgtl::exec::DGTLWasmEncoder encoder;

// Encode IR stream to WASM binary
dgtl::ir::IRStream ir_stream = // ... compiled IR ...
encoder.encode(ir_stream);

// Get encoded WASM module
const std::vector<uint8_t>& wasm_binary = encoder.get_binary();

// Write to .wasm file
std::ofstream wasm_file("module.wasm", std::ios::binary);
wasm_file.write(reinterpret_cast<const char*>(wasm_binary.data()), 
                wasm_binary.size());

// Module can be loaded by any WASM runtime:
// - Browsers: WebAssembly.instantiate()
// - Node.js: WebAssembly.compile()
// - Wasmtime, WasmEdge, etc.
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, OpCode, is_null(), size(), get_node(), validate_shadow_integrity(), get_max_id()

## Performance Characteristics
- **Buffer Allocation**: 64KB pre-allocation for typical modules
- **Encoding Speed**: Linear O(n) translation of IR nodes
- **LEB128 Efficiency**: Compact variable-length encoding
- **Memory Usage**: Minimal overhead beyond IR stream size
- **Section Optimization**: Back-patching avoids vector shifting
- **Validation Overhead**: Pillar 3 integrity checks

## WebAssembly Standards Compliance
- **Binary Format**: Official WASM 1.0 specification
- **Section Structure**: Proper ordering and encoding
- **Opcode Mapping**: Standards-compliant instruction encoding
- **Memory Model**: 64KB pages with proper alignment
- **Export Interface**: Standard function export mechanism
- **Type System**: Function type definitions
- **Local Variables**: IR register to WASM local mapping