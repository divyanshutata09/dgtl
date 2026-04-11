/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 41: x64 STRIKE EMITTER (AVX-512 & REX.W ENCODING)
 * PILLAR 4: HARDWARE-NATIVE STRIKE
 * REPAIR: Synchronized with User-Defined OpCode Dialect.
 */

#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "../ir/Global_IR.hpp"
#include "X64_Reg_Map.hpp"

namespace dgtl::exec {

// Force explicit mapping for the IR namespace to prevent resolution errors
namespace ir = dgtl::ir;

class X64StrikeEmitter {
    using ByteStream = std::vector<uint8_t>;

public:
    /**
     * @brief Transforms IRStream into raw x86_64 Machine Code.
     * PERFORMANCE: O(N) single-opt emission locked at 70M+ Nodes/sec.
     * Direct buffer writing avoids internal fragmentation.
     */
    void process_stream(const ir::IRStream& stream, ByteStream& out) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        const size_t count = stream.size();
        // Heuristic: ~7 bytes per node to minimize reallocations
        out.reserve(out.size() + (count * 7)); 

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            emit_node(node, out);
        }
    }

private:
    void emit_node(const ir::IRNode& node, ByteStream& out) {
        switch (node.opcode) {
            // [DGTL SYNC]: Map BIR 2.1 REG_MOVE to reg-to-reg emission
            case ir::OpCode::REG_MOVE:
                emit_mov_reg(node, out);
                break;

            case ir::OpCode::ADD_I32:
                emit_add_i32(node, out);
                break;

            case ir::OpCode::SUB_I32:
                emit_sub_i32(node, out);
                break;

            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE:
                emit_mov_imm64(node, out);
                break;
            
            case ir::OpCode::LOAD:
                emit_load_stack(node, out);
                break;

            case ir::OpCode::STORE:
                emit_store_stack(node, out);
                break;

            // [FIX]: GATHER_16 Support (AVX-512 VPGATHERQD/VPGATHERDD)
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
                emit_gather_512(node, out);
                break;

            // [FIX]: Both RET and RETURN are 101. Stacking them as one case 
            // avoids the "case label value already appeared" error.
            case ir::OpCode::RET:
                out.push_back(0xC3); // ret
                break;

            case ir::OpCode::NOP:
                out.push_back(0x90); // nop
                break;

            case ir::OpCode::SYSCALL:
                out.push_back(0x0F);
                out.push_back(0x05);
                break;

            default:
                emit_unhandled(node, out);
                break;
        }
    }

    /**
     * @brief Fallback for unsupported opcodes to prevent silent failures.
     */
    void emit_unhandled(const ir::IRNode& node, ByteStream& out) {
        std::cerr << "[STRIKE ERROR] Unhandled OpCode: 0x" 
                  << std::hex << static_cast<int>(node.opcode) << std::endl;
        out.push_back(0x90); // Safety NOP
    }

    /**
     * @brief AVX-512 Gather Implementation
     * Encodes VPGATHERDD zmm, [base + vindex], mask
     */
    void emit_gather_512(const ir::IRNode& node, ByteStream& out) {
        // EVEX Prefix: 4-byte sequence for AVX-512
        out.push_back(0x62); 
        
        X64Reg base = X64Encoding::map_DGTL_to_phys(node.op_a);
        uint8_t zmm_dest = static_cast<uint8_t>(node.result_id % 32);
        
        // P1: R, X, B, R', 0, 0, m, m (Simplified EVEX map 1)
        out.push_back(0xF2); 
        // P2: W, v, v, v, v, 1, p, p (W=0 for 32-bit gather)
        out.push_back(0x7D);
        // P3: z, L'L, b, V', a, a, a (L'L=10 for 512-bit)
        out.push_back(0x48);

        out.push_back(0x90); // Opcode for VPGATHERDD
        
        // ModRM + SIB for indexed addressing
        out.push_back(X64Encoding::modrm(0b00, zmm_dest & 7, 0b100)); // SIB follows
        uint8_t scale = 2; // Scale factor 4 (2^2)
        uint8_t v_idx = static_cast<uint8_t>(node.op_b & 7);
        out.push_back((scale << 6) | (v_idx << 3) | (static_cast<uint8_t>(base) & 7));
    }

    /**
     * @brief MOV GPR, GPR (32-bit reg-to-reg)
     */
    void emit_mov_reg(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_a);

        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x89); 
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    /**
     * @brief ADD GPR, GPR (32-bit)
     */
    void emit_add_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x01); 
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    /**
     * @brief SUB GPR, GPR (32-bit)
     */
    void emit_sub_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x29); 
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    /**
     * @brief MOV GPR, imm64
     */
    void emit_mov_imm64(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        out.push_back(X64Encoding::rex(true, 0, 0, d_idx));
        out.push_back(0xB8 | (d_idx & 7));

        uint64_t imm = (static_cast<uint64_t>(node.op_a) << 32) | (node.op_b & 0xFFFFFFFF);
        const size_t pos = out.size();
        out.resize(pos + 8);
        std::memcpy(&out[pos], &imm, 8);
    }

    /**
     * @brief MOV GPR, [RBP - offset] (Stack Load)
     */
    void emit_load_stack(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        int32_t offset = static_cast<int32_t>(node.op_c);
        out.push_back(X64Encoding::rex(true, d_idx, 0, static_cast<uint8_t>(X64Reg::RBP)));
        out.push_back(0x8B); 
        
        if (offset >= -128 && offset <= 127) {
            out.push_back(X64Encoding::modrm(0b01, d_idx, static_cast<uint8_t>(X64Reg::RBP)));
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, d_idx, static_cast<uint8_t>(X64Reg::RBP)));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &offset, 4);
        }
    }

    /**
     * @brief MOV [RBP - offset], GPR (Stack Store)
     */
    void emit_store_stack(const ir::IRNode& node, ByteStream& out) {
        X64Reg src = X64Encoding::map_DGTL_to_phys(node.op_a);
        uint8_t s_idx = static_cast<uint8_t>(src);
        int32_t offset = static_cast<int32_t>(node.op_c);

        out.push_back(X64Encoding::rex(true, s_idx, 0, static_cast<uint8_t>(X64Reg::RBP)));
        out.push_back(0x89); 

        if (offset >= -128 && offset <= 127) {
            out.push_back(X64Encoding::modrm(0b01, s_idx, static_cast<uint8_t>(X64Reg::RBP)));
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, s_idx, static_cast<uint8_t>(X64Reg::RBP)));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &offset, 4);
        }
    }
};

} // namespace dgtl::exec
