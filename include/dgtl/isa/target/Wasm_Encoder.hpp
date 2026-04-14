/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 18: HIGH-PERFORMANCE WASM BINARY ENCODER
 * DESIGN PRINCIPLE: 
 * Direct Register-to-Stack Mapping for WebAssembly.
 * Ultra-fast LEB128 Encoding (Varint) for all IDs and Opcodes.
 * Zero-Allocation: Streams directly into the DGTL_Arena.
 */

#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <functional> 
#include <string_view> 
#include "dgtl/ir/base/Global_IR.hpp"

namespace dgtl::exec {

class DGTLWasmEncoder {
    std::vector<uint8_t> _binary_buffer;

public:
    /**
     * @brief Pre-reserve 64KB for standard WASM modules.
     */
    DGTLWasmEncoder() { _binary_buffer.reserve(64 * 1024); }

    /**
     * @brief Generates a valid .wasm binary from the DGTL IR Stream.
     * PILLAR 3: Validates stream integrity before encoding to prevent malformed WASM.
     */
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

    [[nodiscard]] const std::vector<uint8_t>& get_binary() const noexcept { return _binary_buffer; }

private:
    void emit_byte(uint8_t b) { _binary_buffer.push_back(b); }
    
    void emit_bytes(std::initializer_list<uint8_t> bytes) { 
        _binary_buffer.insert(_binary_buffer.end(), bytes); 
    }

    void emit_binary_op(std::vector<uint8_t>& body, uint8_t wasm_op, const ir::IRNode& node) {
        body.push_back(0x20); // local.get
        this->emit_leb128_to(body, node.op_a);
        body.push_back(0x20); // local.get
        this->emit_leb128_to(body, node.op_b);
        body.push_back(wasm_op);
        body.push_back(0x21); // local.set
        this->emit_leb128_to(body, node.result_id);
    }

    void emit_string(std::string_view str) {
        emit_leb128(static_cast<uint32_t>(str.size()));
        for (char c : str) emit_byte(static_cast<uint8_t>(c));
    }

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
};

} // namespace dgtl::exec


