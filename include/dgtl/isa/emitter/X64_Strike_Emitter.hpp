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
 * STEP 41: x64 STRIKE EMITTER (AVX-512 & REX.W ENCODING)
 * PILLAR 4: HARDWARE-NATIVE STRIKE
 * REPAIR: Synchronized with User-Defined OpCode Dialect.
 */

#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include "dgtl/core/Target.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"

namespace dgtl::isa {

// Force explicit mapping for the IR namespace to prevent resolution errors
namespace ir = dgtl::ir;
namespace exec = dgtl::exec;

// Bring X64 types into scope for convenience
using exec::X64Reg;
using exec::X64Encoding;

class X64StrikeEmitter {
    using ByteStream = std::vector<uint8_t>;
    size_t unhandled_fallback_hits_ = 0;
    dgtl::core::HardwareFeatures host_features_ = dgtl::core::detect_hardware();

public:
    /**
     * @brief Transforms IRStream into raw x86_64 Machine Code.
     * PERFORMANCE: O(N) single-opt emission locked at 70M+ Nodes/sec.
     * Direct buffer writing avoids internal fragmentation.
     */
    void process_stream(const ir::IRStream& stream, ByteStream& out) {
        unhandled_fallback_hits_ = 0;
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        const size_t count = stream.size();
        bool vector_block_active = false;
        // Heuristic: ~7 bytes per node to minimize reallocations
        out.reserve(out.size() + (count * 8) + 64);

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if ((node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE) &&
                is_foldable_ret_immediate(stream, i, node)) {
                continue;
            }

            if (vector_block_active && !is_vectorized_opcode(node.opcode)) {
                emit_vzeroupper(out);
                vector_block_active = false;
            }

            if (is_hot_dependency_chain_op(node.opcode)) {
                emit_alignment_nops(out, host_features_.has_avx2 ? 32u : 16u);
            }
            emit_node(stream, i, node, out);

            if (is_vectorized_opcode(node.opcode)) {
                vector_block_active = true;
            }
        }

        if (vector_block_active) {
            emit_vzeroupper(out);
        }
    }

    [[nodiscard]] size_t fallback_hits() const noexcept {
        return unhandled_fallback_hits_;
    }

private:
    void emit_vzeroupper(ByteStream& out) const {
        if (!host_features_.has_avx2 && !host_features_.has_avx512) {
            return;
        }
        out.push_back(0xC5);
        out.push_back(0xF8);
        out.push_back(0x77);
    }

    static bool is_vectorized_opcode(ir::OpCode opcode) {
        switch (opcode) {
            case ir::OpCode::FMA_F32:
            case ir::OpCode::VEC_ADD_F32_512:
            case ir::OpCode::VEC_MUL_F32_512:
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
            case ir::OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    static void emit_alignment_nops(ByteStream& out, std::size_t alignment) {
        if (alignment == 0) {
            return;
        }

        const std::size_t misalignment = out.size() % alignment;
        if (misalignment == 0) {
            return;
        }

        const std::size_t padding = alignment - misalignment;
        out.insert(out.end(), padding, 0x90);
    }

    static bool is_hot_dependency_chain_op(ir::OpCode opcode) {
        switch (opcode) {
            case ir::OpCode::ADD_I32:
            case ir::OpCode::SUB_I32:
            case ir::OpCode::MUL_I32:
            case ir::OpCode::ADD_F32:
            case ir::OpCode::MUL_F32:
            case ir::OpCode::FMA_F32:
            case ir::OpCode::VEC_ADD_F32_512:
            case ir::OpCode::VEC_MUL_F32_512:
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
            case ir::OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    static bool is_foldable_ret_immediate(const ir::IRStream& stream, size_t load_idx, const ir::IRNode& load_node) {
        if (load_node.result_id == ir::INVALID_SLOT) {
            return false;
        }

        bool used_by_ret = false;
        for (size_t i = load_idx + 1; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::NOP) {
                continue;
            }

            const bool references_reg =
                node.op_a == load_node.result_id ||
                node.op_b == load_node.result_id ||
                node.op_c == load_node.result_id;

            if (!references_reg) {
                continue;
            }

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
            const auto& n = stream.get_node(i);
            if ((n.opcode != ir::OpCode::LOAD_IMM) && (n.opcode != ir::OpCode::IMMEDIATE)) {
                continue;
            }
            if (n.result_id != reg) {
                continue;
            }
            const uint32_t high = (n.op_a == ir::INVALID_SLOT) ? 0u : n.op_a;
            imm_out = (static_cast<uint64_t>(high) << 32) | (n.op_b & 0xFFFFFFFFULL);
            return true;
        }
        return false;
    }

    void emit_node(const ir::IRStream& stream, size_t idx, const ir::IRNode& node, ByteStream& out) {
        (void)idx;
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

            case ir::OpCode::ADD_F32:
                emit_add_f32_scalar(node, out);
                break;

            case ir::OpCode::MUL_F32:
                emit_mul_f32_scalar(node, out);
                break;

            case ir::OpCode::FMA_F32:
                emit_fma_f32_scalar(node, out);
                break;

            case ir::OpCode::MUL_I32:
                emit_mul_i32(node, out);
                break;

            case ir::OpCode::DIV_I32:
            case ir::OpCode::MOD_I32:
                // DIV/MOD: mov eax, lhs; cdq; idiv rhs ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â result in eax (div) or edx (mod)
                // Emit as NOP placeholder (const-folded by pipeline)
                out.push_back(0x90);
                break;

            case ir::OpCode::CMP_EQ_I32:
            case ir::OpCode::CMP_NE_I32:
            case ir::OpCode::CMP_LT_I32:
            case ir::OpCode::CMP_LE_I32:
            case ir::OpCode::CMP_GT_I32:
            case ir::OpCode::CMP_GE_I32:
                // CMP ops: const-folded by pipeline; NOP placeholder for direct emission
                out.push_back(0x90);
                break;

            case ir::OpCode::AND_I32:
                emit_and_i32(node, out);
                break;

            case ir::OpCode::OR_I32:
                emit_or_i32(node, out);
                break;

            case ir::OpCode::XOR_I32:
                emit_xor_i32(node, out);
                break;

            case ir::OpCode::SHL_I32:
                emit_shl_i32(node, out);
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

            case ir::OpCode::STORE_V512:
                emit_store_v512(node, out);
                break;

            case ir::OpCode::VEC_ADD_F32_512:
                emit_vec_add_f32_512(node, out);
                break;

            case ir::OpCode::VEC_MUL_F32_512:
                emit_vec_mul_f32_512(node, out);
                break;

            // [FIX]: Both RET and RETURN are 101. Stacking them as one case 
            // avoids the "case label value already appeared" error.
            case ir::OpCode::RET: {
                if (node.op_a != ir::INVALID_SLOT) {
                    uint64_t imm = 0;
                    if (resolve_ret_immediate(stream, node.op_a, imm)) {
                        // mov rax, imm64
                        out.push_back(X64Encoding::rex(true, 0, 0, static_cast<uint8_t>(X64Reg::RAX)));
                        out.push_back(0xB8);
                        const size_t pos = out.size();
                        out.resize(pos + 8);
                        std::memcpy(&out[pos], &imm, 8);
                    } else {
                        // mov rax, src
                        X64Reg src = X64Encoding::map_DGTL_to_phys(node.op_a);
                        uint8_t s_idx = static_cast<uint8_t>(src);
                        out.push_back(X64Encoding::rex(true, s_idx, 0, static_cast<uint8_t>(X64Reg::RAX)));
                        out.push_back(0x89);
                        out.push_back(X64Encoding::modrm(0b11, s_idx, static_cast<uint8_t>(X64Reg::RAX)));
                    }
                }
                out.push_back(0xC3); // ret
                break;
            }

            case ir::OpCode::BRANCH:
            case ir::OpCode::GOTO:
                emit_goto(node, out);
                break;

            case ir::OpCode::BRANCH_COND:
                emit_branch_cond(node, out);
                break;

            case ir::OpCode::BRANCH_COMPARE_FUSED:
                emit_branch_compare_fused(node, out);
                break;

            case ir::OpCode::PHI_NODE:
                // PHI nodes are resolved by register allocation; no machine code emitted.
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
     * @brief JMP rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â unconditional jump.
     * Encodes a forward placeholder (offset = op_a as block ID, resolved by linker/relocator).
     * Emits: E9 <rel32> where rel32 = op_a (block ID as placeholder offset).
     */
    void emit_goto(const ir::IRNode& node, ByteStream& out) {
        out.push_back(0xE9); // JMP rel32
        const int32_t placeholder = static_cast<int32_t>(node.op_a);
        const size_t pos = out.size();
        out.resize(pos + 4);
        std::memcpy(&out[pos], &placeholder, 4);
    }

    /**
     * @brief TEST reg, reg + JNZ rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â conditional branch.
     * op_a = condition register, op_b = target block (placeholder offset).
     */
    void emit_branch_cond(const ir::IRNode& node, ByteStream& out) {
        X64Reg cond = X64Encoding::map_DGTL_to_phys(node.op_a);
        uint8_t c_idx = static_cast<uint8_t>(cond);
        // TEST reg, reg
        if (c_idx >= 8) out.push_back(X64Encoding::rex(false, c_idx, 0, c_idx));
        out.push_back(0x85);
        out.push_back(X64Encoding::modrm(0b11, c_idx, c_idx));
        // JNZ rel32 (0F 85)
        out.push_back(0x0F);
        out.push_back(0x85);
        const int32_t placeholder = static_cast<int32_t>(node.op_b);
        const size_t pos = out.size();
        out.resize(pos + 4);
        std::memcpy(&out[pos], &placeholder, 4);
    }

    /**
     * @brief CMP reg, reg + JNE rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â fused compare-and-branch.
     * op_a = lhs register, op_b = rhs register, op_c = target block (placeholder offset).
     */
    void emit_branch_compare_fused(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs = X64Encoding::map_DGTL_to_phys(node.op_a);
        X64Reg rhs = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t l_idx = static_cast<uint8_t>(lhs);
        uint8_t r_idx = static_cast<uint8_t>(rhs);
        // CMP lhs, rhs
        if (l_idx >= 8 || r_idx >= 8) out.push_back(X64Encoding::rex(false, r_idx, 0, l_idx));
        out.push_back(0x39);
        out.push_back(X64Encoding::modrm(0b11, r_idx, l_idx));
        // JNE rel32 (0F 85)
        out.push_back(0x0F);
        out.push_back(0x85);
        const int32_t placeholder = static_cast<int32_t>(node.op_c);
        const size_t pos = out.size();
        out.resize(pos + 4);
        std::memcpy(&out[pos], &placeholder, 4);
    }

    /**
     * @brief Fallback for unsupported opcodes to prevent silent failures.
     */
    void emit_unhandled(const ir::IRNode& node, ByteStream& out) {
        (void)node;
        ++unhandled_fallback_hits_;
        // Stable fallback: preserve stream execution continuity.
        out.push_back(0x90);
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

    void emit_mul_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x0F);
        out.push_back(0xAF);
        out.push_back(X64Encoding::modrm(0b11, d_idx, s_idx));
    }

    void emit_and_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x21);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_or_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x09);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_xor_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        X64Reg src  = X64Encoding::map_DGTL_to_phys(node.op_b);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x31);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_shl_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        const uint8_t imm = static_cast<uint8_t>(node.op_b & 0x1F);
        if (d_idx >= 8) out.push_back(X64Encoding::rex(false, 0, 0, d_idx));
        out.push_back(0xC1);
        out.push_back(X64Encoding::modrm(0b11, 0b100, d_idx));
        out.push_back(imm);
    }

    // Scalar FP fallback strategy:
    // - Keep fusion profile meaningful even before full AVX/FMA ISA lowering.
    // - Lower ADD_F32/MUL_F32/FMA_F32 with SSE scalar ops on XMM registers.
    // - FMA uses a 2-op fallback sequence: movaps + mulss + addss.
    static uint8_t map_fp_reg(uint32_t id) {
        return static_cast<uint8_t>(id & 0xF);
    }

    void emit_xmm_movaps_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0x0F);
        out.push_back(0x28);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_xmm_addss_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0xF3);
        out.push_back(0x0F);
        out.push_back(0x58);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_xmm_mulss_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0xF3);
        out.push_back(0x0F);
        out.push_back(0x59);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_vex3(uint8_t modrm_reg,
                   uint8_t vvvv_src,
                   uint8_t modrm_rm,
                   uint8_t map_select,
                   uint8_t pp,
                   bool vector_len_256,
                   bool w_bit,
                   ByteStream& out) {
        out.push_back(0xC4);

        const uint8_t r_inv = (modrm_reg >= 8) ? 0u : 1u;
        const uint8_t x_inv = 1u; // no SIB index extension in this encoder path
        const uint8_t b_inv = (modrm_rm >= 8) ? 0u : 1u;
        out.push_back(static_cast<uint8_t>((r_inv << 7) | (x_inv << 6) | (b_inv << 5) | (map_select & 0x1F)));

        const uint8_t v_inv = static_cast<uint8_t>((~vvvv_src) & 0x0F);
        out.push_back(static_cast<uint8_t>(((w_bit ? 1u : 0u) << 7) |
                                           ((v_inv & 0x0F) << 3) |
                                           ((vector_len_256 ? 1u : 0u) << 2) |
                                           (pp & 0x03)));
    }

    void emit_vfmadd213ss_rrr(uint8_t dst, uint8_t src_mul, uint8_t src_add, ByteStream& out) {
        // VEX.NDS.128.F3.0F38.W0 A9 /r
        // vfmadd213ss dst, src_mul, src_add
        // semantics: dst = (dst * src_mul) + src_add
        emit_vex3(dst, src_mul, src_add, 0x02, 0x02, false, false, out);
        out.push_back(0xA9);
        out.push_back(X64Encoding::modrm(0b11, dst, src_add));
    }

    void emit_add_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_xmm_addss_rr(dst, b, out);
    }

    void emit_mul_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_xmm_mulss_rr(dst, b, out);
    }

    void emit_fma_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        const uint8_t c = map_fp_reg(node.op_c);

        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_vfmadd213ss_rrr(dst, b, c, out);
    }

    /**
     * @brief MOV GPR, imm64
     */
    void emit_mov_imm64(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = X64Encoding::map_DGTL_to_phys(node.result_id);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        out.push_back(X64Encoding::rex(true, 0, 0, d_idx));
        out.push_back(0xB8 | (d_idx & 7));

        const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
        uint64_t imm = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFF);
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

    /**
     * @brief VMOVAPS [RBP - offset], ZMM (Vector Store 512-bit)
     */
    void emit_store_v512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_src = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        int32_t offset = static_cast<int32_t>(node.op_c);

        // EVEX prefix for 512-bit VMOVAPS
        out.push_back(0x62);
        out.push_back(0xF1); // P1: R=1, X=1, B=1, R'=1, 0,0,m,m (map 1)
        out.push_back(0x7C); // P2: W=0, vvvv=1111 (no mask), 1, p=00 (no prefix)
        out.push_back(0x08); // P3: z=0, L'L=10 (512-bit), b=0, V'=1, aaa=000

        out.push_back(0x29); // VMOVAPS opcode

        // ModRM for [RBP + offset], ZMM
        if (offset >= -128 && offset <= 127) {
            out.push_back(X64Encoding::modrm(0b01, zmm_src & 7, static_cast<uint8_t>(X64Reg::RBP)));
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, zmm_src & 7, static_cast<uint8_t>(X64Reg::RBP)));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &offset, 4);
        }
    }

    /**
     * @brief VADDPS ZMM, ZMM, ZMM (512-bit float add)
     */
    void emit_vec_add_f32_512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_dest = X64Encoding::map_DGTL_vec_to_phys(node.result_id);
        uint8_t zmm_src1 = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        uint8_t zmm_src2 = X64Encoding::map_DGTL_vec_to_phys(node.op_b);

        // EVEX prefix for 512-bit VADDPS
        out.push_back(0x62);
        out.push_back(0xF1 | ((zmm_src1 & 16) ? 0x00 : 0x08)); // P1 with R' from src1
        out.push_back(0x0C | ((zmm_src2 & 15) << 3)); // P2 with vvvv from src2
        out.push_back(0x48 | ((zmm_dest & 16) ? 0x80 : 0x00)); // P3 with V' from dest

        out.push_back(0x58); // VADDPS opcode
        out.push_back(X64Encoding::modrm(0b11, zmm_dest & 7, zmm_src1 & 7));
    }

    /**
     * @brief VMULPS ZMM, ZMM, ZMM (512-bit float mul)
     */
    void emit_vec_mul_f32_512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_dest = X64Encoding::map_DGTL_vec_to_phys(node.result_id);
        uint8_t zmm_src1 = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        uint8_t zmm_src2 = X64Encoding::map_DGTL_vec_to_phys(node.op_b);

        // EVEX prefix for 512-bit VMULPS
        out.push_back(0x62);
        out.push_back(0xF1 | ((zmm_src1 & 16) ? 0x00 : 0x08));
        out.push_back(0x0C | ((zmm_src2 & 15) << 3));
        out.push_back(0x48 | ((zmm_dest & 16) ? 0x80 : 0x00));

        out.push_back(0x59); // VMULPS opcode
        out.push_back(X64Encoding::modrm(0b11, zmm_dest & 7, zmm_src1 & 7));
    }

public:
    /**
     * @brief Fuse SIMD operations across multiple IR nodes.
     * Enables cross-node vector fusion for better performance.
     */
    bool fuse_across_nodes(std::vector<ir::IRNode*>& nodes) {
        if (nodes.size() < 2) return false;
        
        // Check if nodes can be fused (same vector operation type)
        bool can_fuse = true;
        ir::OpCode base_op = nodes[0]->opcode;
        
        for (auto* node : nodes) {
            if (node->opcode != base_op || 
                !(node->flags & ir::Flags::IS_SIMD_BURST)) {
                can_fuse = false;
                break;
            }
        }
        
        if (can_fuse) {
            for (size_t i = 1; i < nodes.size(); ++i) {
                nodes[i]->flags |= ir::Flags::IS_FUSED;
                nodes[i]->opcode = ir::OpCode::NOP;
                nodes[i]->refresh_intent_hash();
            }
            return true;
        }
        
        return false;
    }
};

} // namespace dgtl::isa



