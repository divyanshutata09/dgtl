/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
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

module;

#include <cstdlib>

#include <vector>
#include <cstdint>
#include <algorithm>
#include <functional> 
#include <string_view> 
#include <limits>
#include <string>

export module dgtl.ir_core.wasm_encoder;

// 🚀 C++26 FIX: Change partition import to standalone module import
import dgtl.target; // Fixed from 'import dgtl.target;'
import dgtl.ir_core.abi_lowering;
import dgtl.ir_core;
import dgtl.ir_core.bridge;
import dgtl.ir_core.runtime_contract;

import dgtl.assertions;
import dgtl.flat_map;

export namespace dgtl::exec {

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
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream, true)) [[unlikely]] return;

        _binary_buffer.clear();

        std::vector<std::string> runtime_import_symbols;
        runtime_import_symbols.reserve(32);
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                symbol != nullptr) {
                if (std::find(runtime_import_symbols.begin(), runtime_import_symbols.end(), symbol) == runtime_import_symbols.end()) {
                    runtime_import_symbols.emplace_back(symbol);
                }
            }
        }
        const uint32_t runtime_import_count = static_cast<uint32_t>(runtime_import_symbols.size());

        // 1. WASM Magic & Version (\0asm v1.0)
        emit_bytes({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

        // 2. Type Section (ID 1)
        emit_section(1, [this]() {
            this->emit_leb128(2);    // 2 Types

            // Type 0: runtime intrinsic bridge (i32, i32, i32) -> i32
            this->emit_byte(0x60);
            this->emit_leb128(3);
            this->emit_byte(0x7F);
            this->emit_byte(0x7F);
            this->emit_byte(0x7F);
            this->emit_leb128(1);
            this->emit_byte(0x7F);

            // Type 1: DGTL_main () -> i32
            this->emit_byte(0x60);
            this->emit_leb128(0);
            this->emit_leb128(1);
            this->emit_byte(0x7F);
        });

        // 3. Import Section (ID 2): runtime intrinsics from env
        if (runtime_import_count > 0u) {
            emit_section(2, [this, &runtime_import_symbols]() {
                this->emit_leb128(static_cast<uint32_t>(runtime_import_symbols.size()));
                for (const auto& symbol : runtime_import_symbols) {
                    this->emit_string("env");
                    this->emit_string(symbol);
                    this->emit_byte(0x00); // import kind: function
                    this->emit_leb128(0);  // type index 0
                }
            });
        }

        // 4. Function Section (ID 3): Declares func 0 uses type 1 (DGTL_main)
        emit_section(3, [this]() {
            this->emit_leb128(1);    
            this->emit_leb128(1);    
        });

        // 5. Memory Section (ID 5): 1 page (64KB) for LOAD/STORE ops
        emit_section(5, [this]() {
            this->emit_leb128(1);
            this->emit_byte(0x00);   // limits: min only
            this->emit_leb128(1);    // 1 page
        });

        // 6. Export Section (ID 7)
        emit_section(7, [this, runtime_import_count]() {
            this->emit_leb128(1);            
            this->emit_string("DGTL_main");  
            this->emit_byte(0x00);           // export func
            this->emit_leb128(runtime_import_count); // first defined func index
        });

        // 7. Code Section (ID 10)
        emit_section(10, [this, &stream, &runtime_import_symbols, runtime_import_count]() {
            this->emit_leb128(1);            // 1 function body
            
            std::vector<uint8_t> body;
            
            // Local Definitions: Mapping IR Registers to WASM Locals
            // We reserve locals up to the max_id found in the stream
            uint32_t max_reg = stream.get_max_id();
            if (max_reg == ir::INVALID_SLOT || max_reg + 1 < kDefaultLocals) {
                max_reg = kDefaultLocals - 1;
            }
            max_reg = std::min(max_reg, kMaxWasmLocals - 1);
            const uint32_t local_count = max_reg + 1;
            
            this->emit_leb128_to(body, 1);           // 1 local group
            this->emit_leb128_to(body, local_count); // count
            body.push_back(0x7F);                   // i32 type

            for (size_t i = 0; i < stream.size(); ++i) {
                const auto& node = stream.get_node(i);
                if ((node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE ||
                     node.opcode == ir::OpCode::STRING_CONST) &&
                    this->is_foldable_ret_immediate(stream, i, node)) {
                    continue;
                }
                
                switch (node.opcode) {
                    case ir::OpCode::LOAD_IMM:
                    case ir::OpCode::IMMEDIATE:
                    case ir::OpCode::STRING_CONST:
                        if (!this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x41); // i32.const
                        this->emit_i32_const_to(body, node);
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

                    case ir::OpCode::OR_I32:
                        this->emit_binary_op(body, 0x72, node, max_reg); // i32.or
                        break;

                    case ir::OpCode::XOR_I32:
                        this->emit_binary_op(body, 0x73, node, max_reg); // i32.xor
                        break;

                    case ir::OpCode::SHL_I32:
                        this->emit_binary_op(body, 0x74, node, max_reg); // i32.shl
                        break;

                    case ir::OpCode::SHR_I32:
                        this->emit_binary_op(body, 0x75, node, max_reg); // i32.shr_s
                        break;

                    case ir::OpCode::LABEL:
                        // Label pseudo-ops carry no WASM output; flow is structured.
                        break;

                    case ir::OpCode::CALL: {
                        std::vector<uint32_t> args;
                        args.reserve(8);
                        if (node.op_b != ir::INVALID_SLOT) args.push_back(node.op_b);
                        if (node.op_c != ir::INVALID_SLOT) args.push_back(node.op_c);
                        if (node.logic_flags != ir::INVALID_SLOT) args.push_back(node.logic_flags);
                        const auto overflow = stream.get_call_overflow_args(i);
                        std::copy_if(overflow.begin(), overflow.end(), std::back_inserter(args), [](uint32_t extra) noexcept {
                            return extra != ir::INVALID_SLOT;
                        });

                        const std::vector<uint32_t> layout_words = stream.get_call_abi_layout_words(i);
                        if (layout_words.size() >= 4) {
                            const uint32_t param_count = layout_words[0];
                            const size_t required_words = 4u + static_cast<size_t>(param_count) * 4u;
                            if (layout_words.size() >= required_words) {
                                const size_t count = std::min<size_t>(param_count, args.size());
                                for (size_t ai = 0; ai < count; ++ai) {
                                    if (!this->is_valid_local(args[ai], max_reg)) {
                                        continue;
                                        }
                                    body.push_back(0x20); // local.get
                                    this->emit_leb128_to(body, args[ai]);
                                }
                            }
                        } else {
                            for (uint32_t arg : args) {
                                if (!this->is_valid_local(arg, max_reg)) {
                                    continue;
                                }
                                body.push_back(0x20); // local.get
                                this->emit_leb128_to(body, arg);
                            }
                        }

                        body.push_back(0x10); // call
                        const uint32_t raw_target = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
                        this->emit_leb128_to(body, runtime_import_count + raw_target);

                        if (node.result_id != ir::INVALID_SLOT && this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x21); // local.set
                            this->emit_leb128_to(body, node.result_id);
                        } else {
                            body.push_back(0x1A); // drop
                        }
                        break;
                    }

                    case ir::OpCode::BOOL_SELECT:
                    case ir::OpCode::SELECT:
                    case ir::OpCode::PHI_NODE:
                        if (!this->is_valid_local(node.op_a, max_reg) ||
                            !this->is_valid_local(node.op_b, max_reg) ||
                            !this->is_valid_local(node.op_c, max_reg) ||
                            !this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get selector/cond
                        this->emit_leb128_to(body, node.opcode == ir::OpCode::PHI_NODE ? node.op_c : node.op_a);
                        body.push_back(0x04); // if
                        body.push_back(0x7F); // result i32
                        body.push_back(0x20); // local.get true arm / from_b
                        this->emit_leb128_to(body, node.op_b);
                        body.push_back(0x05); // else
                        body.push_back(0x20); // local.get false arm / from_a
                        this->emit_leb128_to(body, node.opcode == ir::OpCode::PHI_NODE ? node.op_a : node.op_c);
                        body.push_back(0x0B); // end
                        body.push_back(0x21); // local.set
                        this->emit_leb128_to(body, node.result_id);
                        break;

                    case ir::OpCode::GUARD_VALUE:
                    case ir::OpCode::GUARD_ALIAS:
                        if (!this->is_valid_local(node.op_a, max_reg) ||
                            !this->is_valid_local(node.op_b, max_reg) ||
                            !this->is_valid_local(node.op_c, max_reg) ||
                            !this->is_valid_local(node.result_id, max_reg)) {
                            body.push_back(0x01);
                            break;
                        }
                        body.push_back(0x20); // local.get a
                        this->emit_leb128_to(body, node.op_a);
                        body.push_back(0x20); // local.get b
                        this->emit_leb128_to(body, node.op_b);
                        body.push_back(0x46); // i32.eq
                        body.push_back(0x04); // if
                        body.push_back(0x7F); // result i32
                        if (node.opcode == ir::OpCode::GUARD_ALIAS) {
                            body.push_back(0x41); // i32.const
                            this->emit_leb128_to(body, 1u);
                        } else {
                            body.push_back(0x20); // local.get value
                            this->emit_leb128_to(body, node.op_a);
                        }
                        body.push_back(0x05); // else
                        body.push_back(0x20); // local.get fail
                        this->emit_leb128_to(body, node.op_c);
                        body.push_back(0x0B); // end
                        body.push_back(0x21); // local.set
                        this->emit_leb128_to(body, node.result_id);
                        break;

                    case ir::OpCode::PTR_DIFF:
                        this->emit_binary_op(body, 0x6B, node, max_reg); // i32.sub
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
                        body.push_back(0x01);
                        break;

                    case ir::OpCode::BRANCH_COND:
                    case ir::OpCode::BRANCH_LIKELY:
                    case ir::OpCode::BRANCH_UNLIKELY:
                        body.push_back(0x01);
                        break;

                    case ir::OpCode::BRANCH_COMPARE_FUSED:
                        body.push_back(0x01);
                        break;

                    default:
                        if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                            const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                            if (symbol == nullptr) {
                                body.push_back(0x00); // unreachable
                                break;
                            }
                            const auto it = std::find(runtime_import_symbols.begin(), runtime_import_symbols.end(), symbol);
                            if (it == runtime_import_symbols.end()) {
                                body.push_back(0x00); // unreachable
                                break;
                            }
                            const uint32_t fn_index = static_cast<uint32_t>(std::distance(runtime_import_symbols.begin(), it));

                            this->emit_local_or_zero(body, node.op_a, max_reg);
                            this->emit_local_or_zero(body, node.op_b, max_reg);
                            this->emit_local_or_zero(body, node.op_c, max_reg);
                            body.push_back(0x10); // call imported runtime function
                            this->emit_leb128_to(body, fn_index);

                            if (node.result_id != ir::INVALID_SLOT && this->is_valid_local(node.result_id, max_reg)) {
                                body.push_back(0x21); // local.set
                                this->emit_leb128_to(body, node.result_id);
                            } else {
                                body.push_back(0x1A); // drop
                            }
                            break;
                        }
                        body.push_back(0x01); 
                        break;
                }
            }

            if (body.empty() || body.back() != 0x0F) {
                body.push_back(0x41); // i32.const
                this->emit_sleb128_to(body, 0);
                body.push_back(0x0F); // return
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

    static bool is_foldable_ret_immediate(const ir::IRStream& stream, size_t load_idx, const ir::IRNode& load_node) {
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

    static bool resolve_ret_immediate(const ir::IRStream& stream, uint32_t reg, uint64_t& imm_out) {
        for (size_t i = stream.size(); i-- > 0;) {
            const auto& node = stream.get_node(i);
            if (node.opcode != ir::OpCode::LOAD_IMM && node.opcode != ir::OpCode::IMMEDIATE &&
                node.opcode != ir::OpCode::STRING_CONST) continue;
            if (node.result_id != reg) continue;
            if (node.intent_hash != 0) {
                imm_out = node.intent_hash;
            } else {
                const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
                imm_out = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFFULL);
            }
            return true;
        }
        return false;
    }

    void emit_byte(uint8_t b) { _binary_buffer.push_back(b); }
    
    void emit_bytes(std::initializer_list<uint8_t> bytes) { 
        _binary_buffer.insert(_binary_buffer.end(), bytes); 
    }

    static void emit_binary_op(std::vector<uint8_t>& body, uint8_t wasm_op, const ir::IRNode& node, uint32_t max_local) {
        if (!is_valid_local(node.op_a, max_local) || !is_valid_local(node.op_b, max_local) || !is_valid_local(node.result_id, max_local)) {
            body.push_back(0x01);
            return;
        }
        body.push_back(0x20); // local.get
        emit_leb128_to(body, node.op_a);
        body.push_back(0x20); // local.get
        emit_leb128_to(body, node.op_b);
        body.push_back(wasm_op);
        body.push_back(0x21); // local.set
        emit_leb128_to(body, node.result_id);
    }

    static void emit_local_or_zero(std::vector<uint8_t>& body, uint32_t local_id, uint32_t max_local) {
        if (is_valid_local(local_id, max_local)) {
            body.push_back(0x20); // local.get
            emit_leb128_to(body, local_id);
        } else {
            body.push_back(0x41); // i32.const
            emit_sleb128_to(body, 0);
        }
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

    static void emit_i32_const_to(std::vector<uint8_t>& vec, const ir::IRNode& node) {
        uint64_t imm64 = node.intent_hash;
        if (imm64 == 0) {
            const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
            imm64 = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFFULL);
        }
        const int32_t imm32 = static_cast<int32_t>(imm64 & 0xFFFFFFFFULL);
        emit_sleb128_to(vec, imm32);
    }

    static void emit_leb128_to(std::vector<uint8_t>& vec, uint32_t val) {
        do {
            uint8_t byte = val & 0x7F;
            val >>= 7;
            if (val != 0) byte |= 0x80;
            vec.push_back(byte);
        } while (val != 0);
    }

    static void emit_sleb128_to(std::vector<uint8_t>& vec, int32_t val) {
        bool more = true;
        while (more) {
            uint8_t byte = static_cast<uint8_t>(val & 0x7F);
            val >>= 7;
            const bool sign_bit = (byte & 0x40) != 0;
            if ((val == 0 && !sign_bit) || (val == -1 && sign_bit)) {
                more = false;
            } else {
                byte |= 0x80;
            }
            vec.push_back(byte);
        }
    }
};

} // namespace dgtl::exec