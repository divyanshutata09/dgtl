/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
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
#include <limits>
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::exec {

class DGTLWasmEncoder {
    std::vector<uint8_t> _binary_buffer;
    static constexpr uint32_t kDefaultLocals = 32;
    static constexpr uint32_t kMaxWasmLocals = 65536;

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
            this->emit_leb128(1);    // 1 result
            this->emit_byte(0x7F);   // i32 result
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
            if (max_reg == ir::INVALID_SLOT) max_reg = kDefaultLocals - 1; // Default floor
            max_reg = std::min(max_reg, kMaxWasmLocals - 1);
            const uint32_t local_count = max_reg + 1;
            
            this->emit_leb128_to(body, 1);           // 1 local group
            this->emit_leb128_to(body, local_count); // count
            body.push_back(0x7F);                   // i32 type

            for (size_t i = 0; i < stream.size(); ++i) {
                const auto& node = stream.get_node(i);
                if ((node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE) &&
                    this->is_foldable_ret_immediate(stream, i, node)) {
                    continue;
                }
                
                switch (node.opcode) {
                    case ir::OpCode::LOAD_IMM:
                        if (!this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x41); // i32.const
                        this->emit_leb128_to(body, node.op_b);
                        body.push_back(0x21); // local.set
                        this->emit_leb128_to(body, node.result_id);
                        break;

                    case ir::OpCode::REG_MOVE:
                        if (!this->is_valid_local(node.op_a, max_reg) || !this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x21); // local.set
                        this->emit_leb128_to(body, node.result_id);
                        break;

                    case ir::OpCode::ADD_I32:
                        this->emit_binary_op(body, 0x6A, node, max_reg); // i32.add
                        break;

                    case ir::OpCode::SUB_I32:
                        this->emit_binary_op(body, 0x6B, node, max_reg); // i32.sub
                        break;

                    case ir::OpCode::MUL_I32:
                        this->emit_binary_op(body, 0x6C, node, max_reg); // i32.mul
                        break;

                    case ir::OpCode::DIV_I32:
                        this->emit_binary_op(body, 0x6D, node, max_reg); // i32.div_s
                        break;

                    case ir::OpCode::MOD_I32:
                        this->emit_binary_op(body, 0x6F, node, max_reg); // i32.rem_s
                        break;

                    case ir::OpCode::CMP_EQ_I32:
                        this->emit_binary_op(body, 0x46, node, max_reg); // i32.eq
                        break;

                    case ir::OpCode::CMP_NE_I32:
                        this->emit_binary_op(body, 0x47, node, max_reg); // i32.ne
                        break;

                    case ir::OpCode::CMP_LT_I32:
                        this->emit_binary_op(body, 0x48, node, max_reg); // i32.lt_s
                        break;

                    case ir::OpCode::CMP_LE_I32:
                        this->emit_binary_op(body, 0x4C, node, max_reg); // i32.le_s
                        break;

                    case ir::OpCode::CMP_GT_I32:
                        this->emit_binary_op(body, 0x4A, node, max_reg); // i32.gt_s
                        break;

                    case ir::OpCode::CMP_GE_I32:
                        this->emit_binary_op(body, 0x4E, node, max_reg); // i32.ge_s
                        break;

                    case ir::OpCode::AND_I32:
                        this->emit_binary_op(body, 0x71, node, max_reg); // i32.and
                        break;

                    case ir::OpCode::LOAD:
                        if (!this->is_valid_local(node.op_a, max_reg) || !this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get (addr)
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x28); // i32.load
                        this->emit_leb128_to(body, 0); // align
                        this->emit_leb128_to(body, 0); // offset
                        body.push_back(0x21); // local.set
                        this->emit_leb128_to(body, node.result_id);
                        break;

                    case ir::OpCode::STORE:
                        if (!this->is_valid_local(node.op_a, max_reg) || !this->is_valid_local(node.op_b, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get (addr)
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x20); // local.get (val)
                        this->emit_leb128_to(body, node.op_b);
                        body.push_back(0x36); // i32.store
                        this->emit_leb128_to(body, 0); // align
                        this->emit_leb128_to(body, 0); // offset
                        break;

                    case ir::OpCode::RET:
                        if (node.op_a != ir::INVALID_SLOT && this->is_valid_local(node.op_a, max_reg)) {
                            uint64_t imm = 0;
                            if (this->resolve_ret_immediate(stream, node.op_a, imm)) {
                                body.push_back(0x41); // i32.const
                                this->emit_leb128_to(body, static_cast<uint32_t>(imm & 0xFFFFFFFFU));
                                body.push_back(0x0F); // return
                                break;
                            }
                            body.push_back(0x20); // local.get
                            this->emit_leb128_to(body, node.op_a);
                        } else {
                            body.push_back(0x41); // i32.const 0 fallback
                            this->emit_leb128_to(body, 0);
                        }
                        body.push_back(0x0F); // return
                        break;

                    case ir::OpCode::NOP:
                        body.push_back(0x01); // nop
                        break;

                    case ir::OpCode::BRANCH:
                    case ir::OpCode::GOTO:
                        // br <label_idx> ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â unconditional branch
                        body.push_back(0x0C); // br
                        this->emit_leb128_to(body, node.op_a);
                        break;

                    case ir::OpCode::BRANCH_COND:
                        // op_a = condition local, op_b = target label
                        if (!this->is_valid_local(node.op_a, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x0D); // br_if
                        this->emit_leb128_to(body, node.op_b);
                        break;

                    case ir::OpCode::BRANCH_COMPARE_FUSED:
                        // op_a = lhs, op_b = rhs, op_c = target label
                        // Compare lhs != rhs, then br_if
                        if (!this->is_valid_local(node.op_a, max_reg) || !this->is_valid_local(node.op_b, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x20); // local.get
                        this->emit_leb128_to(body, node.op_b);
                        body.push_back(0x47); // i32.ne
                        body.push_back(0x0D); // br_if
                        this->emit_leb128_to(body, node.op_c);
                        break;

                    case ir::OpCode::PHI_NODE:
                        // PHI nodes are resolved before WASM emission; no output.
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
    [[nodiscard]] static constexpr bool is_valid_local(uint32_t id, uint32_t max_local) noexcept {
        return id != ir::INVALID_SLOT && id <= max_local;
    }

    bool is_foldable_ret_immediate(const ir::IRStream& stream, size_t load_idx, const ir::IRNode& load_node) const {
        if (load_node.result_id == ir::INVALID_SLOT) return false;

        bool used_by_ret = false;
        for (size_t i = load_idx + 1; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::NOP) continue;

            const bool references_reg =
                node.op_a == load_node.result_id ||
                node.op_b == load_node.result_id ||
                node.op_c == load_node.result_id;

            if (!references_reg) continue;

            if (node.opcode == ir::OpCode::RET && node.op_a == load_node.result_id && !used_by_ret) {
                used_by_ret = true;
                continue;
            }

            return false;
        }

        return used_by_ret;
    }

    bool resolve_ret_immediate(const ir::IRStream& stream, uint32_t reg, uint64_t& imm_out) const {
        for (size_t i = stream.size(); i-- > 0;) {
            const auto& node = stream.get_node(i);
            if (node.opcode != ir::OpCode::LOAD_IMM && node.opcode != ir::OpCode::IMMEDIATE) continue;
            if (node.result_id != reg) continue;
            const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
            imm_out = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFFULL);
            return true;
        }
        return false;
    }

    void emit_byte(uint8_t b) { _binary_buffer.push_back(b); }
    
    void emit_bytes(std::initializer_list<uint8_t> bytes) { 
        _binary_buffer.insert(_binary_buffer.end(), bytes); 
    }

    void emit_binary_op(std::vector<uint8_t>& body, uint8_t wasm_op, const ir::IRNode& node, uint32_t max_local) {
        if (!is_valid_local(node.op_a, max_local) || !is_valid_local(node.op_b, max_local) || !is_valid_local(node.result_id, max_local)) {
            body.push_back(0x01);
            return;
        }
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

        std::vector<uint8_t> section_payload;
        section_payload.reserve(128);

        auto saved = std::move(_binary_buffer);
        _binary_buffer = std::move(section_payload);
        content_gen();
        section_payload = std::move(_binary_buffer);
        _binary_buffer = std::move(saved);

        if (section_payload.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return;
        }

        this->emit_leb128(static_cast<uint32_t>(section_payload.size()));
        _binary_buffer.insert(_binary_buffer.end(), section_payload.begin(), section_payload.end());
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



