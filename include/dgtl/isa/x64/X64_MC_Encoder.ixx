/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   X64 MC (Machine Code) Object Encoder
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.isa.x64.mc_encoder;

import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.isa.x64.x64_instruction_info;

export namespace dgtl::isa::x64 {

using namespace dgtl::machine;
using namespace dgtl::object;

struct X64BranchPatch {
    std::size_t patch_offset = 0;
    BlockId target_block = INVALID_BLOCK_ID;
    bool is_rel8 = false;
};

class X64MCEncoder {
public:
    static bool encode_function(
        const MachineFunction& function,
        ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table = {},
        std::string* out_diag = nullptr,
        const std::unordered_map<std::string, std::uintptr_t>& runtime_symbols = {},
        std::unordered_map<BlockId, std::size_t>* out_block_offsets = nullptr) {

        Section& text = object.get_text_section();
        const std::uint64_t func_start = text.size();

        std::unordered_map<BlockId, std::size_t> block_offsets;
        std::vector<X64BranchPatch> patches;

        // Pass 1: Encode instructions block by block
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            const MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;

            block_offsets[bb->id] = text.size();

            for (const InstrId inst_id : bb->instructions) {
                const MachineInstruction* inst = function.instruction(inst_id);
                if (!inst || inst->parent_block == INVALID_BLOCK_ID) continue;

                if (!encode_instruction(*inst, function, text, object, patches, symbol_table, out_diag, runtime_symbols)) {
                    if (out_diag && out_diag->empty()) {
                        *out_diag = "Failed at instruction ID " + std::to_string(inst->id) +
                                    ", form=" + std::string(machine_form_name(inst->form)) +
                                    " (raw form " + std::to_string(static_cast<uint32_t>(inst->form)) +
                                    "), semantic_kind=" + std::to_string(static_cast<uint32_t>(inst->semantic_kind)) +
                                    ", opcode=" + std::to_string(inst->opcode) +
                                    ", operands_count=" + std::to_string(inst->operands.size());
                    }
                    return false;
                }
            }
        }

        // Pass 2: Patch branches and jumps
        for (const auto& patch : patches) {
            auto it = block_offsets.find(patch.target_block);
            if (it == block_offsets.end()) {
                if (out_diag) {
                    *out_diag = "Branch patch failed: target block " + std::to_string(patch.target_block) + " not found";
                }
                return false;
            }
            const std::size_t target_offset = it->second;
            if (patch.is_rel8) {
                const std::int64_t disp = static_cast<std::int64_t>(target_offset) -
                                          static_cast<std::int64_t>(patch.patch_offset + 1);
                if (disp < -128 || disp > 127) {
                    if (out_diag) {
                        *out_diag = "Branch patch rel8 displacement out of range: " + std::to_string(disp);
                    }
                    return false;
                }
                text.bytes.data()[patch.patch_offset] = static_cast<std::uint8_t>(disp & 0xFF);
            } else {
                const std::int64_t disp = static_cast<std::int64_t>(target_offset) -
                                          static_cast<std::int64_t>(patch.patch_offset + 4);
                const std::uint32_t disp32 = static_cast<std::uint32_t>(disp);
                text.bytes.data()[patch.patch_offset + 0] = static_cast<std::uint8_t>(disp32 & 0xFF);
                text.bytes.data()[patch.patch_offset + 1] = static_cast<std::uint8_t>((disp32 >> 8) & 0xFF);
                text.bytes.data()[patch.patch_offset + 2] = static_cast<std::uint8_t>((disp32 >> 16) & 0xFF);
                text.bytes.data()[patch.patch_offset + 3] = static_cast<std::uint8_t>((disp32 >> 24) & 0xFF);
            }
        }

        if (out_block_offsets) {
            *out_block_offsets = block_offsets;
        }

        const std::uint64_t func_size = text.size() - func_start;
        object.add_function_symbol(std::string(symbol_name), func_start, func_size, true);

        // Register internal function symbols mapped by entry labels
        for (const auto& [slot, name] : symbol_table) {
            if (name.empty() || name == symbol_name) continue;
            if (const auto block_id = function.find_block_for_label(slot)) {
                auto it = block_offsets.find(*block_id);
                if (it != block_offsets.end()) {
                    if (!object.find_symbol(name)) {
                        object.add_function_symbol(name, it->second, 0, true);
                    }
                }
            }
        }

        return true;
    }

    static bool encode_to_bytes(
        const MachineFunction& function,
        std::vector<std::uint8_t>& out_bytes,
        std::string_view symbol_name = "DGTL_kernel_entry",
        const std::unordered_map<std::uint32_t, std::string>& symbol_table = {},
        std::string* out_diag = nullptr,
        const std::unordered_map<std::string, std::uintptr_t>& runtime_symbols = {},
        std::unordered_map<BlockId, std::size_t>* out_block_offsets = nullptr) {
        out_bytes.clear();
        ObjectFile object;
        object.set_format(ObjectFormat::COFF);
        if (!encode_function(function, object, symbol_name, symbol_table, out_diag, runtime_symbols, out_block_offsets)) {
            return false;
        }
        const auto& text = object.get_text_section();
        out_bytes.assign(text.bytes.begin(), text.bytes.end());
        return true;
    }

private:
    static constexpr std::uint8_t REX_W = 0x48;

    static void emit_u8(Section& sec, std::uint8_t byte) {
        sec.emit_u8(byte);
    }

    static void emit_u32(Section& sec, std::uint32_t val) {
        sec.emit_u32(val);
    }

    static void emit_u64(Section& sec, std::uint64_t val) {
        sec.emit_u64(val);
    }

    static std::uint8_t make_rex(bool w, std::uint8_t r, std::uint8_t x, std::uint8_t b) noexcept {
        return static_cast<std::uint8_t>(
            0x40u |
            (w ? 8u : 0u) |
            ((r >= 8u) ? 4u : 0u) |
            ((x >= 8u) ? 2u : 0u) |
            ((b >= 8u) ? 1u : 0u)
        );
    }

    // Reg-Reg instruction: REX [Opcode] [ModR/M]
    static void emit_rr(Section& sec, std::uint8_t opcode, std::uint8_t reg, std::uint8_t rm, bool is_64 = true) {
        const std::uint8_t rex = make_rex(is_64, reg, 0, rm);
        if (rex != 0x40u || is_64) emit_u8(sec, rex);
        emit_u8(sec, opcode);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((reg & 7u) << 3) | (rm & 7u)));
    }

    // Reg-Reg 2-byte 0F opcode: REX [0x0F] [Opcode2] [ModR/M]
    static void emit_rr_0f(Section& sec, std::uint8_t op2, std::uint8_t reg, std::uint8_t rm, bool is_64 = true) {
        const std::uint8_t rex = make_rex(is_64, reg, 0, rm);
        if (rex != 0x40u || is_64) emit_u8(sec, rex);
        emit_u8(sec, 0x0F);
        emit_u8(sec, op2);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((reg & 7u) << 3) | (rm & 7u)));
    }

    // Reg-Imm32 instruction: REX [Opcode] [ModR/M /op_ext] [Imm32]
    static void emit_ri(Section& sec, std::uint8_t opcode, std::uint8_t op_ext, std::uint8_t rm, std::uint32_t imm, bool is_64 = true) {
        const std::uint8_t rex = make_rex(is_64, op_ext, 0, rm);
        if (rex != 0x40u || is_64) emit_u8(sec, rex);
        emit_u8(sec, opcode);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((op_ext & 7u) << 3) | (rm & 7u)));
        emit_u32(sec, imm);
    }

    // Reg-Mem or Mem-Reg instruction: REX [Opcode] [ModR/M] [SIB]? [Disp]?
    static void emit_rm_or_mr(
        Section& sec,
        std::uint8_t opcode,
        std::uint8_t reg_or_op,
        std::uint8_t base,
        std::int64_t disp,
        bool is_64 = true,
        std::uint8_t index = 0xFFu,
        std::uint32_t scale = 1) {

        const bool has_index = (index != 0xFFu && index != 4u);
        const std::uint8_t rex = make_rex(is_64, reg_or_op, has_index ? index : 0u, base);
        if (rex != 0x40u || is_64) emit_u8(sec, rex);

        emit_u8(sec, opcode);

        const bool need_sib = (base == 4u || base == 12u || has_index);
        const bool is_disp8 = (disp >= -128 && disp <= 127 && (disp != 0 || base == 5u || base == 13u));
        const bool is_disp32 = (disp < -128 || disp > 127);

        std::uint8_t mod = 0u;
        if (is_disp8) mod = 1u;
        else if (is_disp32) mod = 2u;
        else if (base == 5u || base == 13u) mod = 1u; // RBP/R13 [base] requires disp8=0

        const std::uint8_t rm_field = need_sib ? 4u : (base & 7u);
        emit_u8(sec, static_cast<std::uint8_t>((mod << 6) | ((reg_or_op & 7u) << 3) | rm_field));

        if (need_sib) {
            std::uint8_t ss = 0u;
            if (scale == 2) ss = 1u;
            else if (scale == 4) ss = 2u;
            else if (scale == 8) ss = 3u;
            const std::uint8_t index_field = has_index ? (index & 7u) : 4u;
            emit_u8(sec, static_cast<std::uint8_t>((ss << 6) | (index_field << 3) | (base & 7u)));
        }

        if (mod == 1u) {
            emit_u8(sec, static_cast<std::uint8_t>(disp & 0xFF));
        } else if (mod == 2u) {
            emit_u32(sec, static_cast<std::uint32_t>(disp & 0xFFFFFFFF));
        }
    }

    // SSE Reg-Reg: [Prefix]? [REX]? 0F [Opcode2] [ModR/M]
    static void emit_sse_rr(Section& sec, std::uint8_t prefix, std::uint8_t op2, std::uint8_t reg, std::uint8_t rm) {
        if (prefix != 0u) emit_u8(sec, prefix);
        const std::uint8_t rex = make_rex(false, reg, 0, rm);
        if (rex != 0x40u) emit_u8(sec, rex);
        emit_u8(sec, 0x0F);
        emit_u8(sec, op2);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((reg & 7u) << 3) | (rm & 7u)));
    }

    // SSE Reg-Mem: [Prefix]? [REX]? 0F [Opcode2] [ModR/M] [SIB]? [Disp]?
    static void emit_sse_rm(Section& sec, std::uint8_t prefix, std::uint8_t op2, std::uint8_t reg, std::uint8_t base, std::int64_t disp) {
        if (prefix != 0u) emit_u8(sec, prefix);
        const std::uint8_t rex = make_rex(false, reg, 0, base);
        if (rex != 0x40u) emit_u8(sec, rex);
        emit_u8(sec, 0x0F);
        emit_u8(sec, op2);

        const bool need_sib = (base == 4u || base == 12u);
        const bool is_disp8 = (disp >= -128 && disp <= 127 && (disp != 0 || base == 5u || base == 13u));
        const bool is_disp32 = (disp < -128 || disp > 127);

        std::uint8_t mod = 0u;
        if (is_disp8) mod = 1u;
        else if (is_disp32) mod = 2u;
        else if (base == 5u || base == 13u) mod = 1u;

        const std::uint8_t rm_field = need_sib ? 4u : (base & 7u);
        emit_u8(sec, static_cast<std::uint8_t>((mod << 6) | ((reg & 7u) << 3) | rm_field));

        if (need_sib) {
            emit_u8(sec, static_cast<std::uint8_t>((4u << 3) | (base & 7u)));
        }

        if (mod == 1u) {
            emit_u8(sec, static_cast<std::uint8_t>(disp & 0xFF));
        } else if (mod == 2u) {
            emit_u32(sec, static_cast<std::uint32_t>(disp & 0xFFFFFFFF));
        }
    }

    // movq xmm, r64 (66 REX.W 0F 6E /r)
    static void emit_movq_to_xmm(Section& sec, std::uint8_t xmm_reg, std::uint8_t gpr_reg) {
        emit_u8(sec, 0x66);
        const std::uint8_t rex = make_rex(true, xmm_reg, 0, gpr_reg);
        emit_u8(sec, rex);
        emit_u8(sec, 0x0F);
        emit_u8(sec, 0x6E);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((xmm_reg & 7u) << 3) | (gpr_reg & 7u)));
    }

    // movq r64, xmm (66 REX.W 0F 7E /r)
    static void emit_movq_from_xmm(Section& sec, std::uint8_t gpr_reg, std::uint8_t xmm_reg) {
        emit_u8(sec, 0x66);
        const std::uint8_t rex = make_rex(true, xmm_reg, 0, gpr_reg);
        emit_u8(sec, rex);
        emit_u8(sec, 0x0F);
        emit_u8(sec, 0x7E);
        emit_u8(sec, static_cast<std::uint8_t>(0xC0u | ((xmm_reg & 7u) << 3) | (gpr_reg & 7u)));
    }

    // Reg-Mem or Mem-Reg 2-byte 0F opcode: REX [0x0F] [Opcode2] [ModR/M] [SIB]? [Disp]?
    static void emit_rm_or_mr_0f(
        Section& sec,
        std::uint8_t op2,
        std::uint8_t reg_or_op,
        std::uint8_t base,
        std::int64_t disp,
        bool is_64 = true,
        std::uint8_t index = 0xFFu,
        std::uint32_t scale = 1) {

        const bool has_index = (index != 0xFFu && index != 4u);
        const std::uint8_t rex = make_rex(is_64, reg_or_op, has_index ? index : 0u, base);
        if (rex != 0x40u || is_64) emit_u8(sec, rex);

        emit_u8(sec, 0x0F);
        emit_u8(sec, op2);

        const bool need_sib = (base == 4u || base == 12u || has_index);
        const bool is_disp8 = (disp >= -128 && disp <= 127 && (disp != 0 || base == 5u || base == 13u));
        const bool is_disp32 = (disp < -128 || disp > 127);

        std::uint8_t mod = 0u;
        if (is_disp8) mod = 1u;
        else if (is_disp32) mod = 2u;
        else if (base == 5u || base == 13u) mod = 1u;

        const std::uint8_t rm_field = need_sib ? 4u : (base & 7u);
        emit_u8(sec, static_cast<std::uint8_t>((mod << 6) | ((reg_or_op & 7u) << 3) | rm_field));

        if (need_sib) {
            std::uint8_t ss = 0u;
            if (scale == 2) ss = 1u;
            else if (scale == 4) ss = 2u;
            else if (scale == 8) ss = 3u;
            const std::uint8_t index_field = has_index ? (index & 7u) : 4u;
            emit_u8(sec, static_cast<std::uint8_t>((ss << 6) | (index_field << 3) | (base & 7u)));
        }

        if (mod == 1u) {
            emit_u8(sec, static_cast<std::uint8_t>(disp & 0xFF));
        } else if (mod == 2) {
            emit_u32(sec, static_cast<std::uint32_t>(disp & 0xFFFFFFFF));
        }
    }

    // VEX 2-byte prefix: 0xC5 [R ~vvvv L pp]
    static void emit_vex2(Section& sec, std::uint8_t dest, std::uint8_t src1, std::uint8_t pp, bool l = false) {
        const std::uint8_t r_bit = (dest >= 8u) ? 0u : 1u;
        const std::uint8_t vvvv = static_cast<std::uint8_t>(src1 & 0x0Fu);
        sec.emit_u8(0xC5);
        sec.emit_u8(static_cast<std::uint8_t>((r_bit << 7) | ((~vvvv & 0x0Fu) << 3) | (l ? 0x04u : 0u) | (pp & 0x03u)));
    }

    // VEX 3-byte prefix: 0xC4 [R X B mmmmm] [W ~vvvv L pp]
    static void emit_vex3(
        Section& sec,
        std::uint8_t dest,
        std::uint8_t src1,
        std::uint8_t src2,
        std::uint8_t pp,
        std::uint8_t mmmmm = 1u,
        bool w = false,
        bool l = false) {
        const std::uint8_t r_bit = (dest >= 8u) ? 0u : 1u;
        const std::uint8_t x_bit = 1u;
        const std::uint8_t b_bit = (src2 >= 8u) ? 0u : 1u;
        const std::uint8_t vvvv = static_cast<std::uint8_t>(src1 & 0x0Fu);

        sec.emit_u8(0xC4);
        sec.emit_u8(static_cast<std::uint8_t>((r_bit << 7) | (x_bit << 6) | (b_bit << 5) | (mmmmm & 0x1Fu)));
        sec.emit_u8(static_cast<std::uint8_t>((w ? 0x80u : 0u) | ((~vvvv & 0x0Fu) << 3) | (l ? 0x04u : 0u) | (pp & 0x03u)));
    }

    // CLFLUSH m8: 0x0F 0xAE /7
    static void emit_clflush(Section& sec, std::uint8_t base, std::int64_t disp = 0, std::uint8_t index = 0xFFu, std::uint32_t scale = 1) {
        emit_rm_or_mr_0f(sec, 0xAE, 7u, base, disp, false, index, scale);
    }

    // PREFETCHh m8: 0x0F 0x18 /hint (hint 1 = T0, 2 = T1, 3 = T2, 0 = NTA)
    static void emit_prefetch(Section& sec, std::uint8_t hint, std::uint8_t base, std::int64_t disp = 0, std::uint8_t index = 0xFFu, std::uint32_t scale = 1) {
        emit_rm_or_mr_0f(sec, 0x18, hint, base, disp, false, index, scale);
    }

    static bool encode_instruction(
        const MachineInstruction& inst,
        const MachineFunction& function,
        Section& text,
        ObjectFile& object,
        std::vector<X64BranchPatch>& patches,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr,
        const std::unordered_map<std::string, std::uintptr_t>& runtime_symbols = {}) {

        switch (inst.form) {
            case MachineForm::NoOp:
                emit_u8(text, 0x90); // NOP
                return true;

            case MachineForm::Move: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "Move operand count < 2";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& src = inst.operands[1];

                const bool is_dst_fp = (dst.is_register() && dst.reg.reg_class == RegisterClassKind::FloatingPoint);
                const bool is_src_fp = (src.is_register() && src.reg.reg_class == RegisterClassKind::FloatingPoint);

                // LEA: Load Effective Address
                if (inst.opcode == GM_LEA || (dst.is_register() && src.is_memory() && inst.opcode == GM_LEA)) {
                    if (!dst.is_register() || !src.is_memory()) {
                        if (out_diag) *out_diag = "LEA requires register dst and memory src";
                        return false;
                    }
                    emit_rm_or_mr(text, 0x8D, static_cast<std::uint8_t>(dst.reg.id),
                                  static_cast<std::uint8_t>(src.memory.base.id),
                                  src.memory.displacement, true,
                                  src.memory.index.valid() ? static_cast<std::uint8_t>(src.memory.index.id) : 0xFFu,
                                  src.memory.scale);
                    return true;
                }

                // Register-to-register moves
                if (dst.is_register() && src.is_register()) {
                    if (dst.reg == src.reg) return true; // redundant move
                    if (is_dst_fp && is_src_fp) {
                        // movsd dst_xmm, src_xmm (F2 0F 10 /r)
                        emit_sse_rr(text, 0xF2, 0x10, static_cast<std::uint8_t>(dst.reg.id), static_cast<std::uint8_t>(src.reg.id));
                        return true;
                    }
                    if (is_dst_fp && !is_src_fp) {
                        // movq dst_xmm, src_gpr (66 REX.W 0F 6E /r)
                        emit_movq_to_xmm(text, static_cast<std::uint8_t>(dst.reg.id), static_cast<std::uint8_t>(src.reg.id));
                        return true;
                    }
                    if (!is_dst_fp && is_src_fp) {
                        // movq dst_gpr, src_xmm (66 REX.W 0F 7E /r)
                        emit_movq_from_xmm(text, static_cast<std::uint8_t>(dst.reg.id), static_cast<std::uint8_t>(src.reg.id));
                        return true;
                    }
                    // mov dst, src (48 89 /r src -> dst)
                    emit_rr(text, 0x89, static_cast<std::uint8_t>(src.reg.id), static_cast<std::uint8_t>(dst.reg.id), true);
                    return true;
                }

                // Register-from-memory loads
                if (dst.is_register() && src.is_memory()) {
                    const std::uint8_t idx = src.memory.index.valid() ? static_cast<std::uint8_t>(src.memory.index.id) : 0xFFu;
                    if (is_dst_fp) {
                        const std::uint8_t pfx = (src.memory.size_bytes == 4u) ? 0xF3u : 0xF2u;
                        emit_sse_rm(text, pfx, 0x10, static_cast<std::uint8_t>(dst.reg.id),
                                    static_cast<std::uint8_t>(src.memory.base.id),
                                    src.memory.displacement);
                        return true;
                    }
                    if (src.memory.size_bytes == 1u) {
                        // movzx r64, byte ptr [mem]
                        emit_rm_or_mr_0f(text, 0xB6, static_cast<std::uint8_t>(dst.reg.id),
                                         static_cast<std::uint8_t>(src.memory.base.id),
                                         src.memory.displacement, true, idx, src.memory.scale);
                        return true;
                    }
                    if (src.memory.size_bytes == 2u) {
                        // movzx r64, word ptr [mem]
                        emit_rm_or_mr_0f(text, 0xB7, static_cast<std::uint8_t>(dst.reg.id),
                                         static_cast<std::uint8_t>(src.memory.base.id),
                                         src.memory.displacement, true, idx, src.memory.scale);
                        return true;
                    }
                    if (src.memory.size_bytes == 4u) {
                        // mov r32, dword ptr [mem] (zero-extends into 64-bit register)
                        emit_rm_or_mr(text, 0x8B, static_cast<std::uint8_t>(dst.reg.id),
                                      static_cast<std::uint8_t>(src.memory.base.id),
                                      src.memory.displacement, false, idx, src.memory.scale);
                        return true;
                    }
                    // mov dst, [src_mem] (48 8B /r dst <- src)
                    emit_rm_or_mr(text, 0x8B, static_cast<std::uint8_t>(dst.reg.id),
                                   static_cast<std::uint8_t>(src.memory.base.id),
                                   src.memory.displacement, true, idx, src.memory.scale);
                    return true;
                }

                // Memory-from-register stores
                if (dst.is_memory() && src.is_register()) {
                    const std::uint8_t idx = dst.memory.index.valid() ? static_cast<std::uint8_t>(dst.memory.index.id) : 0xFFu;
                    if (is_src_fp) {
                        const std::uint8_t pfx = (dst.memory.size_bytes == 4u) ? 0xF3u : 0xF2u;
                        emit_sse_rm(text, pfx, 0x11, static_cast<std::uint8_t>(src.reg.id),
                                    static_cast<std::uint8_t>(dst.memory.base.id),
                                    dst.memory.displacement);
                        return true;
                    }
                    if (dst.memory.size_bytes == 1u) {
                        // mov byte ptr [mem], src_lo
                        emit_rm_or_mr(text, 0x88, static_cast<std::uint8_t>(src.reg.id),
                                      static_cast<std::uint8_t>(dst.memory.base.id),
                                      dst.memory.displacement, false, idx, dst.memory.scale);
                        return true;
                    }
                    if (dst.memory.size_bytes == 2u) {
                        emit_u8(text, 0x66);
                        emit_rm_or_mr(text, 0x89, static_cast<std::uint8_t>(src.reg.id),
                                      static_cast<std::uint8_t>(dst.memory.base.id),
                                      dst.memory.displacement, false, idx, dst.memory.scale);
                        return true;
                    }
                    if (dst.memory.size_bytes == 4u) {
                        emit_rm_or_mr(text, 0x89, static_cast<std::uint8_t>(src.reg.id),
                                      static_cast<std::uint8_t>(dst.memory.base.id),
                                      dst.memory.displacement, false, idx, dst.memory.scale);
                        return true;
                    }
                    // mov [dst_mem], src (48 89 /r src -> dst_mem)
                    emit_rm_or_mr(text, 0x89, static_cast<std::uint8_t>(src.reg.id),
                                   static_cast<std::uint8_t>(dst.memory.base.id),
                                   dst.memory.displacement, true, idx, dst.memory.scale);
                    return true;
                }

                // Register-from-immediate
                if (dst.is_register() && src.is_immediate()) {
                    const std::uint8_t reg = static_cast<std::uint8_t>(dst.reg.id);
                    if (dst.reg.reg_class == RegisterClassKind::FloatingPoint) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(src.immediate));
                        emit_movq_to_xmm(text, reg, 11u);
                        return true;
                    }
                    const std::uint8_t rex = make_rex(true, 0, 0, reg);
                    emit_u8(text, rex);
                    emit_u8(text, static_cast<std::uint8_t>(0xB8u + (reg & 7u)));
                    emit_u64(text, static_cast<std::uint64_t>(src.immediate));
                    return true;
                }

                // Register-from-floating-immediate
                if (dst.is_register() && src.kind == MachineOperandKind::FloatingImmediate) {
                    const std::uint8_t reg = static_cast<std::uint8_t>(dst.reg.id);
                    const std::uint64_t bits = std::bit_cast<std::uint64_t>(src.floating_immediate);
                    if (dst.reg.reg_class == RegisterClassKind::FloatingPoint) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, bits);
                        emit_movq_to_xmm(text, reg, 11u);
                        return true;
                    }
                    const std::uint8_t rex = make_rex(true, 0, 0, reg);
                    emit_u8(text, rex);
                    emit_u8(text, static_cast<std::uint8_t>(0xB8u + (reg & 7u)));
                    emit_u64(text, bits);
                    return true;
                }

                // Register-from-symbol
                if (dst.is_register() && src.kind == MachineOperandKind::Symbol) {
                    std::string sym_name;
                    auto it = symbol_table.find(src.symbol);
                    if (it != symbol_table.end()) {
                        sym_name = it->second;
                    } else {
                        sym_name = "sym_" + std::to_string(src.symbol);
                    }
                    const std::uint8_t reg = static_cast<std::uint8_t>(dst.reg.id);
                    const std::uint8_t rex = make_rex(true, 0, 0, reg);
                    emit_u8(text, rex);
                    emit_u8(text, static_cast<std::uint8_t>(0xB8u + (reg & 7u)));
                    const std::size_t reloc_offset = text.size();
                    emit_u64(text, 0);
                    object.add_relocation(reloc_offset, std::move(sym_name), 0, 1, dgtl::object::RelocationKind::Absolute64);
                    return true;
                }

                // Memory-from-immediate
                if (dst.is_memory() && src.is_immediate()) {
                    const std::uint8_t idx = dst.memory.index.valid() ? static_cast<std::uint8_t>(dst.memory.index.id) : 0xFFu;
                    emit_rm_or_mr(text, 0xC7, 0, static_cast<std::uint8_t>(dst.memory.base.id),
                                  dst.memory.displacement, true, idx, dst.memory.scale);
                    emit_u32(text, static_cast<std::uint32_t>(src.immediate & 0xFFFFFFFF));
                    return true;
                }

                // StackSlot loads / stores
                if (dst.is_register() && src.kind == MachineOperandKind::StackSlot) {
                    std::int64_t disp = 0;
                    const auto& frame = function.frame_info();
                    if (src.stack_slot < frame.slots.size()) {
                        disp = frame.slots[src.stack_slot].offset;
                    }
                    if (is_dst_fp) {
                        emit_sse_rm(text, 0xF2, 0x10, static_cast<std::uint8_t>(dst.reg.id), 5u /* RBP */, disp);
                    } else {
                        emit_rm_or_mr(text, 0x8B, static_cast<std::uint8_t>(dst.reg.id), 5u /* RBP */, disp, true);
                    }
                    return true;
                }
                if (dst.kind == MachineOperandKind::StackSlot && src.is_register()) {
                    std::int64_t disp = 0;
                    const auto& frame = function.frame_info();
                    if (dst.stack_slot < frame.slots.size()) {
                        disp = frame.slots[dst.stack_slot].offset;
                    }
                    if (is_src_fp) {
                        emit_sse_rm(text, 0xF2, 0x11, static_cast<std::uint8_t>(src.reg.id), 5u /* RBP */, disp);
                    } else {
                        emit_rm_or_mr(text, 0x89, static_cast<std::uint8_t>(src.reg.id), 5u /* RBP */, disp, true);
                    }
                    return true;
                }

                if (out_diag) *out_diag = "Move unsupported operand configuration";
                return false;
            }

            case MachineForm::LoadImmediate: {
                if (inst.operands.empty()) {
                    if (out_diag) *out_diag = "LoadImmediate operands empty";
                    return false;
                }
                const auto& dst = inst.operands[0];
                if (!dst.is_register()) {
                    if (out_diag) *out_diag = "LoadImmediate dst is not a register";
                    return false;
                }

                const std::uint8_t reg = static_cast<std::uint8_t>(dst.reg.id);

                for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                    if (inst.operands[i].kind == MachineOperandKind::Symbol) {
                        std::string sym_name;
                        auto it = symbol_table.find(inst.operands[i].symbol);
                        if (it != symbol_table.end()) {
                            sym_name = it->second;
                        } else {
                            sym_name = "sym_" + std::to_string(inst.operands[i].symbol);
                        }
                        const std::uint8_t rex = make_rex(true, 0, 0, reg);
                        emit_u8(text, rex);
                        emit_u8(text, static_cast<std::uint8_t>(0xB8u + (reg & 7u)));
                        const std::size_t reloc_offset = text.size();
                        emit_u64(text, 0);
                        object.add_relocation(reloc_offset, std::move(sym_name), 0, 1, dgtl::object::RelocationKind::Absolute64);
                        return true;
                    }
                }

                std::uint64_t imm = 0;
                for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                    if (inst.operands[i].is_immediate()) {
                        imm = static_cast<std::uint64_t>(inst.operands[i].immediate);
                        break;
                    }
                    if (inst.operands[i].kind == MachineOperandKind::FloatingImmediate) {
                        imm = std::bit_cast<std::uint64_t>(inst.operands[i].floating_immediate);
                        break;
                    }
                }

                if (dst.reg.reg_class == RegisterClassKind::FloatingPoint) {
                    // mov r11, imm64 (49 BB <imm64>)
                    emit_u8(text, 0x49);
                    emit_u8(text, 0xBB);
                    emit_u64(text, imm);
                    // movq dst_xmm, r11
                    emit_movq_to_xmm(text, reg, 11u);
                    return true;
                }

                // mov reg64, imm64 (48 B8+rd <imm64>)
                const std::uint8_t rex = make_rex(true, 0, 0, reg);
                emit_u8(text, rex);
                emit_u8(text, static_cast<std::uint8_t>(0xB8u + (reg & 7u)));
                emit_u64(text, imm);
                return true;
            }

            case MachineForm::LoadStack: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "LoadStack operands < 2";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& src = inst.operands[1];
                if (!dst.is_register()) {
                    if (out_diag) *out_diag = "LoadStack dst is not a register";
                    return false;
                }

                std::uint8_t base_reg = 5u; // RBP
                std::int64_t disp = 0;
                std::uint32_t width = 8u;

                if (src.is_memory()) {
                    base_reg = static_cast<std::uint8_t>(src.memory.base.id);
                    disp = src.memory.displacement;
                    width = src.memory.size_bytes;
                } else if (src.kind == MachineOperandKind::StackSlot) {
                    const auto& frame = function.frame_info();
                    if (src.stack_slot < frame.slots.size()) {
                        disp = frame.slots[src.stack_slot].offset;
                        width = frame.slots[src.stack_slot].size_bytes;
                    }
                } else if (src.is_immediate()) {
                    disp = src.immediate;
                } else {
                    if (out_diag) *out_diag = "LoadStack invalid source operand";
                    return false;
                }

                if (dst.reg.reg_class == RegisterClassKind::FloatingPoint) {
                    const std::uint8_t pfx = (width == 4u) ? 0xF3u : 0xF2u;
                    emit_sse_rm(text, pfx, 0x10, static_cast<std::uint8_t>(dst.reg.id), base_reg, disp);
                    return true;
                }

                emit_rm_or_mr(text, 0x8B, static_cast<std::uint8_t>(dst.reg.id), base_reg, disp, true);
                return true;
            }

            case MachineForm::StoreStack: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "StoreStack operands < 2";
                    return false;
                }
                const auto& mem_op = inst.operands[0];
                const auto& val_op = inst.operands[1];
                if (!val_op.is_register()) {
                    if (out_diag) *out_diag = "StoreStack value is not a register";
                    return false;
                }

                std::uint8_t base_reg = 5u; // RBP
                std::int64_t disp = 0;
                std::uint32_t width = 8u;

                if (mem_op.is_memory()) {
                    base_reg = static_cast<std::uint8_t>(mem_op.memory.base.id);
                    disp = mem_op.memory.displacement;
                    width = mem_op.memory.size_bytes;
                } else if (mem_op.kind == MachineOperandKind::StackSlot) {
                    const auto& frame = function.frame_info();
                    if (mem_op.stack_slot < frame.slots.size()) {
                        disp = frame.slots[mem_op.stack_slot].offset;
                        width = frame.slots[mem_op.stack_slot].size_bytes;
                    }
                } else if (mem_op.is_immediate()) {
                    disp = mem_op.immediate;
                } else {
                    if (out_diag) *out_diag = "StoreStack invalid memory operand";
                    return false;
                }

                if (val_op.reg.reg_class == RegisterClassKind::FloatingPoint) {
                    const std::uint8_t pfx = (width == 4u) ? 0xF3u : 0xF2u;
                    emit_sse_rm(text, pfx, 0x11, static_cast<std::uint8_t>(val_op.reg.id), base_reg, disp);
                    return true;
                }

                emit_rm_or_mr(text, 0x89, static_cast<std::uint8_t>(val_op.reg.id), base_reg, disp, true);
                return true;
            }

            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "IntegerBinary operands < 2";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& lhs = inst.operands.size() >= 3 ? inst.operands[1] : inst.operands[0];
                const auto& rhs = inst.operands.size() >= 3 ? inst.operands[2] : inst.operands[1];
                if (!dst.is_register()) {
                    if (out_diag) *out_diag = "IntegerBinary dst is not a register";
                    return false;
                }

                const std::uint8_t dst_reg = static_cast<std::uint8_t>(dst.reg.id);
                const bool is_64 = (inst.form == MachineForm::IntegerBinary64);

                // Handle Division and Modulo
                if (inst.semantic_kind == MachineSemanticKind::IntegerDiv ||
                    inst.semantic_kind == MachineSemanticKind::IntegerMod) {
                    std::uint8_t divisor_reg = 11u; // R11 scratch default
                    bool is_mem_divisor = false;
                    std::uint8_t div_base = 0;
                    std::int64_t div_disp = 0;
                    std::uint8_t div_idx = 0xFFu;
                    std::uint32_t div_scale = 1;

                    if (rhs.is_register()) {
                        if (rhs.reg.id == 0u /* RAX */ || rhs.reg.id == 2u /* RDX */) {
                            emit_rr(text, 0x89, static_cast<std::uint8_t>(rhs.reg.id), 11u, is_64);
                            divisor_reg = 11u;
                        } else {
                            divisor_reg = static_cast<std::uint8_t>(rhs.reg.id);
                        }
                    } else if (rhs.is_immediate()) {
                        emit_u8(text, 0x49);
                        emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(rhs.immediate));
                        divisor_reg = 11u;
                    } else if (rhs.is_memory()) {
                        is_mem_divisor = true;
                        div_base = static_cast<std::uint8_t>(rhs.memory.base.id);
                        div_disp = rhs.memory.displacement;
                        div_idx = rhs.memory.index.valid() ? static_cast<std::uint8_t>(rhs.memory.index.id) : 0xFFu;
                        div_scale = rhs.memory.scale;
                    } else if (rhs.kind == MachineOperandKind::StackSlot) {
                        is_mem_divisor = true;
                        div_base = 5u; // RBP
                        const auto& frame = function.frame_info();
                        if (rhs.stack_slot < frame.slots.size()) {
                            div_disp = frame.slots[rhs.stack_slot].offset;
                        }
                    } else {
                        if (out_diag) *out_diag = "IntegerDiv/Mod unsupported rhs operand";
                        return false;
                    }

                    if (lhs.is_register()) {
                        if (lhs.reg.id != 0u) {
                            emit_rr(text, 0x89, static_cast<std::uint8_t>(lhs.reg.id), 0u, is_64);
                        }
                    } else if (lhs.is_immediate()) {
                        emit_u8(text, 0x48);
                        emit_u8(text, 0xB8);
                        emit_u64(text, static_cast<std::uint64_t>(lhs.immediate));
                    } else if (lhs.is_memory()) {
                        std::uint8_t lhs_base = static_cast<std::uint8_t>(lhs.memory.base.id);
                        std::int64_t lhs_disp = lhs.memory.displacement;
                        std::uint8_t lhs_idx = lhs.memory.index.valid() ? static_cast<std::uint8_t>(lhs.memory.index.id) : 0xFFu;
                        std::uint32_t lhs_scale = lhs.memory.scale;
                        emit_rm_or_mr(text, 0x8B, 0u, lhs_base, lhs_disp, is_64, lhs_idx, lhs_scale);
                    } else if (lhs.kind == MachineOperandKind::StackSlot) {
                        const auto& frame = function.frame_info();
                        std::int64_t lhs_disp = 0;
                        if (lhs.stack_slot < frame.slots.size()) {
                            lhs_disp = frame.slots[lhs.stack_slot].offset;
                        }
                        emit_rm_or_mr(text, 0x8B, 0u, 5u, lhs_disp, is_64);
                    }

                    if (is_64) {
                        emit_u8(text, 0x48);
                        emit_u8(text, 0x99); // cqo
                    } else {
                        emit_u8(text, 0x99); // cdq
                    }

                    if (is_mem_divisor) {
                        emit_rm_or_mr(text, 0xF7, 7u, div_base, div_disp, is_64, div_idx, div_scale);
                    } else {
                        const std::uint8_t rex = make_rex(is_64, 0, 0, divisor_reg);
                        if (rex != 0x40u || is_64) emit_u8(text, rex);
                        emit_u8(text, 0xF7);
                        emit_u8(text, static_cast<std::uint8_t>(0xF8u | (divisor_reg & 7u))); // idiv
                    }

                    const std::uint8_t res_src = (inst.semantic_kind == MachineSemanticKind::IntegerDiv) ? 0u : 2u;
                    if (dst_reg != res_src) {
                        emit_rr(text, 0x89, res_src, dst_reg, is_64);
                    }
                    if (!is_64) {
                        emit_rr(text, 0x63, dst_reg, dst_reg, true); // movsxd
                    }
                    return true;
                }

                // Handle Shifts (SHL, SHR)
                if (inst.semantic_kind == MachineSemanticKind::IntegerShl ||
                    inst.semantic_kind == MachineSemanticKind::IntegerShr) {
                    if (lhs.is_register() && dst_reg != static_cast<std::uint8_t>(lhs.reg.id)) {
                        emit_rr(text, 0x89, static_cast<std::uint8_t>(lhs.reg.id), dst_reg, is_64);
                    }
                    const std::uint8_t shift_ext = (inst.semantic_kind == MachineSemanticKind::IntegerShl) ? 4u : 7u; // /4 for SHL, /7 for SAR
                    if (rhs.is_immediate()) {
                        const std::uint8_t rex = make_rex(is_64, shift_ext, 0, dst_reg);
                        if (rex != 0x40u || is_64) emit_u8(text, rex);
                        emit_u8(text, 0xC1);
                        emit_u8(text, static_cast<std::uint8_t>(0xC0u | ((shift_ext & 7u) << 3) | (dst_reg & 7u)));
                        emit_u8(text, static_cast<std::uint8_t>(rhs.immediate & 0xFF));
                    } else if (rhs.is_register()) {
                        if (rhs.reg.id != 1u /* RCX */) {
                            emit_rr(text, 0x89, static_cast<std::uint8_t>(rhs.reg.id), 1u, is_64);
                        }
                        const std::uint8_t rex = make_rex(is_64, shift_ext, 0, dst_reg);
                        if (rex != 0x40u || is_64) emit_u8(text, rex);
                        emit_u8(text, 0xD3);
                        emit_u8(text, static_cast<std::uint8_t>(0xC0u | ((shift_ext & 7u) << 3) | (dst_reg & 7u)));
                    }
                    if (!is_64) {
                        emit_rr(text, 0x63, dst_reg, dst_reg, true);
                    }
                    return true;
                }

                // Standard 2-address prep: if dst != lhs, mov dst, lhs
                if (lhs.is_register() && dst_reg != static_cast<std::uint8_t>(lhs.reg.id)) {
                    emit_rr(text, 0x89, static_cast<std::uint8_t>(lhs.reg.id), dst_reg, is_64);
                }

                // Arithmetic opcode mapping
                std::uint8_t op_byte = 0x01; // ADD
                std::uint8_t op_ext = 0;     // for immediate
                switch (inst.semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: op_byte = 0x01; op_ext = 0; break;
                    case MachineSemanticKind::IntegerSub: op_byte = 0x29; op_ext = 5; break;
                    case MachineSemanticKind::IntegerAnd: op_byte = 0x21; op_ext = 4; break;
                    case MachineSemanticKind::IntegerOr:  op_byte = 0x09; op_ext = 1; break;
                    case MachineSemanticKind::IntegerXor: op_byte = 0x31; op_ext = 6; break;
                    case MachineSemanticKind::IntegerMul: {
                        if (rhs.is_register()) {
                            // imul dst, src (0F AF /r)
                            emit_rr_0f(text, 0xAF, dst_reg, static_cast<std::uint8_t>(rhs.reg.id), is_64);
                            if (!is_64) emit_rr(text, 0x63, dst_reg, dst_reg, true);
                            return true;
                        }
                        if (rhs.is_immediate()) {
                            const std::uint8_t rex = make_rex(is_64, dst_reg, 0, dst_reg);
                            if (rex != 0x40u || is_64) emit_u8(text, rex);
                            emit_u8(text, 0x69);
                            emit_u8(text, static_cast<std::uint8_t>(0xC0u | ((dst_reg & 7u) << 3) | (dst_reg & 7u)));
                            emit_u32(text, static_cast<std::uint32_t>(rhs.immediate & 0xFFFFFFFF));
                            if (!is_64) emit_rr(text, 0x63, dst_reg, dst_reg, true);
                            return true;
                        }
                        if (rhs.is_memory()) {
                            const std::uint8_t idx = rhs.memory.index.valid() ? static_cast<std::uint8_t>(rhs.memory.index.id) : 0xFFu;
                            emit_rm_or_mr_0f(text, 0xAF, dst_reg, static_cast<std::uint8_t>(rhs.memory.base.id),
                                             rhs.memory.displacement, is_64, idx, rhs.memory.scale);
                            if (!is_64) emit_rr(text, 0x63, dst_reg, dst_reg, true);
                            return true;
                        }
                        if (rhs.kind == MachineOperandKind::StackSlot) {
                            std::int64_t disp = 0;
                            const auto& frame = function.frame_info();
                            if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                            emit_rm_or_mr_0f(text, 0xAF, dst_reg, 5u /* RBP */, disp, is_64);
                            if (!is_64) emit_rr(text, 0x63, dst_reg, dst_reg, true);
                            return true;
                        }
                        break;
                    }
                    default: op_byte = 0x01; break;
                }

                if (rhs.is_register()) {
                    emit_rr(text, op_byte, static_cast<std::uint8_t>(rhs.reg.id), dst_reg, is_64);
                } else if (rhs.is_immediate()) {
                    // op dst, imm32 (81 /ext <imm32>)
                    emit_ri(text, 0x81, op_ext, dst_reg, static_cast<std::uint32_t>(rhs.immediate & 0xFFFFFFFF), is_64);
                } else if (rhs.is_memory()) {
                    const std::uint8_t idx = rhs.memory.index.valid() ? static_cast<std::uint8_t>(rhs.memory.index.id) : 0xFFu;
                    emit_rm_or_mr(text, static_cast<std::uint8_t>(op_byte + 2), dst_reg,
                                  static_cast<std::uint8_t>(rhs.memory.base.id),
                                  rhs.memory.displacement, is_64, idx, rhs.memory.scale);
                } else if (rhs.kind == MachineOperandKind::StackSlot) {
                    std::int64_t disp = 0;
                    const auto& frame = function.frame_info();
                    if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                    emit_rm_or_mr(text, static_cast<std::uint8_t>(op_byte + 2), dst_reg, 5u /* RBP */, disp, is_64);
                }

                // If 32-bit arithmetic, sign-extend result to 64 bits with movsxd
                if (!is_64) {
                    emit_rr(text, 0x63, dst_reg, dst_reg, true);
                }
                return true;
            }

            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "FloatBinary operands < 2";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& lhs = inst.operands.size() >= 3 ? inst.operands[1] : inst.operands[0];
                const auto& rhs = inst.operands.size() >= 3 ? inst.operands[2] : inst.operands[1];
                if (!dst.is_register()) {
                    if (out_diag) *out_diag = "FloatBinary dst is not a register";
                    return false;
                }

                const bool is_f64 = (inst.form == MachineForm::FloatBinary64);
                const std::uint8_t prefix = is_f64 ? 0xF2u : 0xF3u;
                std::uint8_t sse_op = 0x58u; // ADD
                switch (inst.semantic_kind) {
                    case MachineSemanticKind::FloatAdd: sse_op = 0x58u; break;
                    case MachineSemanticKind::FloatSub: sse_op = 0x5Cu; break;
                    case MachineSemanticKind::FloatMul: sse_op = 0x59u; break;
                    case MachineSemanticKind::FloatDiv: sse_op = 0x5Eu; break;
                    default: {
                        switch (inst.opcode) {
                            case GM_FADD32: case GM_FADD64: sse_op = 0x58u; break;
                            case GM_FSUB32: case GM_FSUB64: sse_op = 0x5Cu; break;
                            case GM_FMUL32: case GM_FMUL64: sse_op = 0x59u; break;
                            case GM_FDIV32: case GM_FDIV64: sse_op = 0x5Eu; break;
                            default: sse_op = 0x58u; break;
                        }
                        break;
                    }
                }

                const bool is_dst_fp = (dst.reg.reg_class == RegisterClassKind::FloatingPoint);
                if (is_dst_fp) {
                    const std::uint8_t dst_xmm = static_cast<std::uint8_t>(dst.reg.id);
                    if (lhs.is_register() && dst.reg != lhs.reg) {
                        emit_sse_rr(text, prefix, 0x10, dst_xmm, static_cast<std::uint8_t>(lhs.reg.id));
                    } else if (lhs.kind == MachineOperandKind::FloatingImmediate) {
                        const std::uint64_t fbits = std::bit_cast<std::uint64_t>(lhs.floating_immediate);
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, fbits);
                        emit_movq_to_xmm(text, dst_xmm, 11u);
                    }
                    if (rhs.is_register()) {
                        emit_sse_rr(text, prefix, sse_op, dst_xmm, static_cast<std::uint8_t>(rhs.reg.id));
                        return true;
                    }
                    if (rhs.is_memory()) {
                        emit_sse_rm(text, prefix, sse_op, dst_xmm, static_cast<std::uint8_t>(rhs.memory.base.id), rhs.memory.displacement);
                        return true;
                    }
                    if (rhs.kind == MachineOperandKind::StackSlot) {
                        std::int64_t disp = 0;
                        const auto& frame = function.frame_info();
                        if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                        emit_sse_rm(text, prefix, sse_op, dst_xmm, 5u /* RBP */, disp);
                        return true;
                    }
                    if (rhs.kind == MachineOperandKind::FloatingImmediate) {
                        const std::uint64_t fbits = std::bit_cast<std::uint64_t>(rhs.floating_immediate);
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, fbits);
                        emit_movq_to_xmm(text, 1u /* XMM1 */, 11u /* R11 */);
                        emit_sse_rr(text, prefix, sse_op, dst_xmm, 1u);
                        return true;
                    }
                    if (rhs.is_immediate()) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(rhs.immediate));
                        emit_movq_to_xmm(text, 1u /* XMM1 */, 11u /* R11 */);
                        emit_sse_rr(text, prefix, sse_op, dst_xmm, 1u);
                        return true;
                    }
                } else {
                    // GPR-backed operands: bridge through volatile XMM0/XMM1
                    if (lhs.is_register()) {
                        emit_movq_to_xmm(text, 0u /* XMM0 */, static_cast<std::uint8_t>(lhs.reg.id));
                    } else if (lhs.kind == MachineOperandKind::FloatingImmediate) {
                        const std::uint64_t fbits = std::bit_cast<std::uint64_t>(lhs.floating_immediate);
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, fbits);
                        emit_movq_to_xmm(text, 0u, 11u);
                    } else if (lhs.is_immediate()) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(lhs.immediate));
                        emit_movq_to_xmm(text, 0u, 11u);
                    }
                    if (rhs.is_register()) {
                        emit_movq_to_xmm(text, 1u /* XMM1 */, static_cast<std::uint8_t>(rhs.reg.id));
                    } else if (rhs.kind == MachineOperandKind::FloatingImmediate) {
                        const std::uint64_t fbits = std::bit_cast<std::uint64_t>(rhs.floating_immediate);
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, fbits);
                        emit_movq_to_xmm(text, 1u, 11u);
                    } else if (rhs.is_immediate()) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(rhs.immediate));
                        emit_movq_to_xmm(text, 1u, 11u);
                    }
                    emit_sse_rr(text, prefix, sse_op, 0u, 1u);
                    emit_movq_from_xmm(text, static_cast<std::uint8_t>(dst.reg.id), 0u);
                    return true;
                }
                if (out_diag) *out_diag = "FloatBinary unhandled operand combination";
                return false;
            }

            case MachineForm::FloatTernaryFma32: {
                if (inst.operands.size() < 3) {
                    if (out_diag) *out_diag = "FloatTernaryFma32 operands < 3";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& lhs = inst.operands[1];
                const auto& rhs = inst.operands[2];
                emit_movq_to_xmm(text, 0u, static_cast<std::uint8_t>(lhs.reg.id));
                emit_movq_to_xmm(text, 1u, static_cast<std::uint8_t>(rhs.reg.id));
                emit_sse_rr(text, 0xF3, 0x59, 0u, 1u); // mulss xmm0, xmm1
                if (inst.operands.size() >= 4) {
                    const auto& acc = inst.operands[3];
                    emit_movq_to_xmm(text, 1u, static_cast<std::uint8_t>(acc.reg.id));
                    emit_sse_rr(text, 0xF3, 0x58, 0u, 1u); // addss xmm0, xmm1
                }
                emit_movq_from_xmm(text, static_cast<std::uint8_t>(dst.reg.id), 0u);
                return true;
            }

            case MachineForm::Compare32: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "Compare32 operands < 2";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& lhs = inst.operands.size() >= 3 ? inst.operands[1] : inst.operands[0];
                const auto& rhs = inst.operands.size() >= 3 ? inst.operands[2] : inst.operands[1];

                if (!dst.is_register()) {
                    if (out_diag) *out_diag = "Compare32 dst is not a register";
                    return false;
                }
                const std::uint8_t dst_reg = static_cast<std::uint8_t>(dst.reg.id);

                const bool is_fp = (lhs.is_register() && lhs.reg.reg_class == RegisterClassKind::FloatingPoint) ||
                                   (rhs.is_register() && rhs.reg.reg_class == RegisterClassKind::FloatingPoint);

                if (is_fp) {
                    if (lhs.is_register() && lhs.reg.reg_class == RegisterClassKind::FloatingPoint) {
                        const std::uint8_t lhs_reg = static_cast<std::uint8_t>(lhs.reg.id);
                        if (rhs.is_register()) {
                            if (rhs.reg.reg_class == RegisterClassKind::FloatingPoint) {
                                emit_sse_rr(text, 0x66, 0x2E, lhs_reg, static_cast<std::uint8_t>(rhs.reg.id));
                            } else {
                                emit_movq_to_xmm(text, 1u, static_cast<std::uint8_t>(rhs.reg.id));
                                emit_sse_rr(text, 0x66, 0x2E, lhs_reg, 1u);
                            }
                        } else if (rhs.is_memory()) {
                            emit_sse_rm(text, 0x66, 0x2E, lhs_reg,
                                        static_cast<std::uint8_t>(rhs.memory.base.id), rhs.memory.displacement);
                        } else if (rhs.kind == MachineOperandKind::StackSlot) {
                            std::int64_t disp = 0;
                            const auto& frame = function.frame_info();
                            if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                            emit_sse_rm(text, 0x66, 0x2E, lhs_reg, 5u /* RBP */, disp);
                        }
                    }
                } else if (lhs.is_register() && rhs.is_register()) {
                    // cmp lhs, rhs (48 39 /r rhs, lhs)
                    emit_rr(text, 0x39, static_cast<std::uint8_t>(rhs.reg.id), static_cast<std::uint8_t>(lhs.reg.id), true);
                } else if (lhs.is_register() && rhs.is_immediate()) {
                    emit_ri(text, 0x81, 7, static_cast<std::uint8_t>(lhs.reg.id), static_cast<std::uint32_t>(rhs.immediate & 0xFFFFFFFF), true);
                } else if (lhs.is_register() && rhs.is_memory()) {
                    const std::uint8_t idx = rhs.memory.index.valid() ? static_cast<std::uint8_t>(rhs.memory.index.id) : 0xFFu;
                    emit_rm_or_mr(text, 0x3B, static_cast<std::uint8_t>(lhs.reg.id),
                                  static_cast<std::uint8_t>(rhs.memory.base.id),
                                  rhs.memory.displacement, true, idx, rhs.memory.scale);
                } else if (lhs.is_register() && rhs.kind == MachineOperandKind::StackSlot) {
                    std::int64_t disp = 0;
                    const auto& frame = function.frame_info();
                    if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                    emit_rm_or_mr(text, 0x3B, static_cast<std::uint8_t>(lhs.reg.id), 5u /* RBP */, disp, true);
                }

                // setcc dst8
                ComparisonPredicate pred = inst.comparison_predicate();
                std::uint8_t cc = 0x94; // sete
                if (is_fp) {
                    switch (pred) {
                        case ComparisonPredicate::Equal:               cc = 0x94; break; // sete
                        case ComparisonPredicate::NotEqual:            cc = 0x95; break; // setne
                        case ComparisonPredicate::SignedLessThan:
                        case ComparisonPredicate::UnsignedLessThan:     cc = 0x92; break; // setb
                        case ComparisonPredicate::SignedLessEqual:
                        case ComparisonPredicate::UnsignedLessEqual:    cc = 0x96; break; // setbe
                        case ComparisonPredicate::SignedGreaterThan:
                        case ComparisonPredicate::UnsignedGreaterThan:  cc = 0x97; break; // seta
                        case ComparisonPredicate::SignedGreaterEqual:
                        case ComparisonPredicate::UnsignedGreaterEqual: cc = 0x93; break; // setae
                        case ComparisonPredicate::Ordered:             cc = 0x9B; break; // setnp
                        case ComparisonPredicate::Unordered:           cc = 0x9A; break; // setp
                        default:                                       cc = 0x94; break;
                    }
                } else {
                    switch (pred) {
                        case ComparisonPredicate::Equal:              cc = 0x94; break; // sete
                        case ComparisonPredicate::NotEqual:           cc = 0x95; break; // setne
                        case ComparisonPredicate::SignedLessThan:      cc = 0x9C; break; // setl
                        case ComparisonPredicate::SignedLessEqual:     cc = 0x9E; break; // setle
                        case ComparisonPredicate::SignedGreaterThan:   cc = 0x9F; break; // setg
                        case ComparisonPredicate::SignedGreaterEqual:  cc = 0x9D; break; // setge
                        case ComparisonPredicate::UnsignedLessThan:    cc = 0x92; break; // setb
                        case ComparisonPredicate::UnsignedLessEqual:   cc = 0x96; break; // setbe
                        case ComparisonPredicate::UnsignedGreaterThan:  cc = 0x97; break; // seta
                        case ComparisonPredicate::UnsignedGreaterEqual: cc = 0x93; break; // setae
                        case ComparisonPredicate::Ordered:            cc = 0x9B; break; // setnp (parity not set)
                        case ComparisonPredicate::Unordered:          cc = 0x9A; break; // setp  (parity set)
                        default:                                      cc = 0x94; break;
                    }
                }

                const std::uint8_t setcc_rex = make_rex(false, 0, 0, dst_reg);
                if (setcc_rex != 0x40u || dst_reg >= 4u) emit_u8(text, setcc_rex);
                emit_u8(text, 0x0F);
                emit_u8(text, cc);
                emit_u8(text, static_cast<std::uint8_t>(0xC0u | (dst_reg & 7u)));

                // movzx dst64, dst8 (48 0F B6 /r dst, dst)
                emit_rr_0f(text, 0xB6, dst_reg, dst_reg, true);
                return true;
            }

            case MachineForm::Goto: {
                BlockId target_block = INVALID_BLOCK_ID;
                for (const auto& op : inst.operands) {
                    if (op.kind == MachineOperandKind::BasicBlock) {
                        target_block = op.block;
                        break;
                    }
                }
                if (target_block == INVALID_BLOCK_ID) {
                    if (out_diag) *out_diag = "Goto missing target block";
                    return false;
                }

                // jmp rel32 (E9 <disp32>)
                emit_u8(text, 0xE9);
                const std::size_t patch_pos = text.size();
                emit_u32(text, 0);
                patches.push_back({patch_pos, target_block, false});
                return true;
            }

            case MachineForm::BranchCond: {
                if (inst.operands.empty()) {
                    if (out_diag) *out_diag = "BranchCond operands empty";
                    return false;
                }
                const auto& cond = inst.operands[0];
                if (!cond.is_register()) {
                    if (out_diag) *out_diag = "BranchCond cond is not a register";
                    return false;
                }

                BlockId true_block = INVALID_BLOCK_ID;
                BlockId false_block = INVALID_BLOCK_ID;
                for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                    if (inst.operands[i].kind == MachineOperandKind::BasicBlock) {
                        if (true_block == INVALID_BLOCK_ID) {
                            true_block = inst.operands[i].block;
                        } else if (false_block == INVALID_BLOCK_ID) {
                            false_block = inst.operands[i].block;
                        }
                    }
                }

                if (true_block == INVALID_BLOCK_ID) {
                    if (out_diag) *out_diag = "BranchCond true_block missing";
                    return false;
                }

                // test cond, cond (48 85 /r cond, cond)
                emit_rr(text, 0x85, static_cast<std::uint8_t>(cond.reg.id), static_cast<std::uint8_t>(cond.reg.id), true);

                // jne true_block (0F 85 <disp32>)
                emit_u8(text, 0x0F);
                emit_u8(text, 0x85);
                const std::size_t patch_pos = text.size();
                emit_u32(text, 0);
                patches.push_back({patch_pos, true_block, false});

                if (false_block != INVALID_BLOCK_ID) {
                    // jmp false_block (E9 <disp32>)
                    emit_u8(text, 0xE9);
                    const std::size_t false_patch = text.size();
                    emit_u32(text, 0);
                    patches.push_back({false_patch, false_block, false});
                }
                return true;
            }

            case MachineForm::BranchCompare: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "BranchCompare operands < 2";
                    return false;
                }
                const auto& lhs = inst.operands[0];
                const auto& rhs = inst.operands[1];

                if (!lhs.is_register()) {
                    if (out_diag) *out_diag = "BranchCompare lhs is not a register";
                    return false;
                }

                const bool is_fp = (lhs.reg.reg_class == RegisterClassKind::FloatingPoint) ||
                                   (rhs.is_register() && rhs.reg.reg_class == RegisterClassKind::FloatingPoint);

                if (is_fp) {
                    const std::uint8_t lhs_reg = static_cast<std::uint8_t>(lhs.reg.id);
                    if (rhs.is_register()) {
                        if (rhs.reg.reg_class == RegisterClassKind::FloatingPoint) {
                            emit_sse_rr(text, 0x66, 0x2E, lhs_reg, static_cast<std::uint8_t>(rhs.reg.id));
                        } else {
                            emit_movq_to_xmm(text, 1u, static_cast<std::uint8_t>(rhs.reg.id));
                            emit_sse_rr(text, 0x66, 0x2E, lhs_reg, 1u);
                        }
                    } else if (rhs.is_memory()) {
                        emit_sse_rm(text, 0x66, 0x2E, lhs_reg,
                                    static_cast<std::uint8_t>(rhs.memory.base.id), rhs.memory.displacement);
                    } else if (rhs.kind == MachineOperandKind::StackSlot) {
                        std::int64_t disp = 0;
                        const auto& frame = function.frame_info();
                        if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                        emit_sse_rm(text, 0x66, 0x2E, lhs_reg, 5u /* RBP */, disp);
                    } else if (rhs.kind == MachineOperandKind::FloatingImmediate) {
                        const std::uint64_t bits = std::bit_cast<std::uint64_t>(rhs.floating_immediate);
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, bits);
                        emit_movq_to_xmm(text, 1u, 11u);
                        emit_sse_rr(text, 0x66, 0x2E, lhs_reg, 1u);
                    } else if (rhs.is_immediate()) {
                        emit_u8(text, 0x49); emit_u8(text, 0xBB);
                        emit_u64(text, static_cast<std::uint64_t>(rhs.immediate));
                        emit_movq_to_xmm(text, 1u, 11u);
                        emit_sse_rr(text, 0x66, 0x2E, lhs_reg, 1u);
                    }
                } else if (rhs.is_register()) {
                    // cmp lhs, rhs (48 39 /r rhs, lhs)
                    emit_rr(text, 0x39, static_cast<std::uint8_t>(rhs.reg.id), static_cast<std::uint8_t>(lhs.reg.id), true);
                } else if (rhs.is_immediate()) {
                    emit_ri(text, 0x81, 7, static_cast<std::uint8_t>(lhs.reg.id), static_cast<std::uint32_t>(rhs.immediate & 0xFFFFFFFF), true);
                } else if (rhs.is_memory()) {
                    const std::uint8_t idx = rhs.memory.index.valid() ? static_cast<std::uint8_t>(rhs.memory.index.id) : 0xFFu;
                    emit_rm_or_mr(text, 0x3B, static_cast<std::uint8_t>(lhs.reg.id),
                                  static_cast<std::uint8_t>(rhs.memory.base.id),
                                  rhs.memory.displacement, true, idx, rhs.memory.scale);
                } else if (rhs.kind == MachineOperandKind::StackSlot) {
                    std::int64_t disp = 0;
                    const auto& frame = function.frame_info();
                    if (rhs.stack_slot < frame.slots.size()) disp = frame.slots[rhs.stack_slot].offset;
                    emit_rm_or_mr(text, 0x3B, static_cast<std::uint8_t>(lhs.reg.id), 5u /* RBP */, disp, true);
                }

                BlockId true_block = INVALID_BLOCK_ID;
                BlockId false_block = INVALID_BLOCK_ID;
                for (std::size_t i = 2; i < inst.operands.size(); ++i) {
                    if (inst.operands[i].kind == MachineOperandKind::BasicBlock) {
                        if (true_block == INVALID_BLOCK_ID) {
                            true_block = inst.operands[i].block;
                        } else if (false_block == INVALID_BLOCK_ID) {
                            false_block = inst.operands[i].block;
                        }
                    }
                }
                if (true_block == INVALID_BLOCK_ID) {
                    for (const auto& op : inst.operands) {
                        if (op.kind == MachineOperandKind::BasicBlock) {
                            true_block = op.block;
                            break;
                        }
                    }
                }
                if (true_block == INVALID_BLOCK_ID) {
                    if (out_diag) *out_diag = "BranchCompare target block missing";
                    return false;
                }

                ComparisonPredicate pred = inst.comparison_predicate();
                std::uint8_t jcc = 0x84; // je
                if (is_fp) {
                    switch (pred) {
                        case ComparisonPredicate::Equal:               jcc = 0x84; break; // je
                        case ComparisonPredicate::NotEqual:            jcc = 0x85; break; // jne
                        case ComparisonPredicate::SignedLessThan:
                        case ComparisonPredicate::UnsignedLessThan:     jcc = 0x82; break; // jb
                        case ComparisonPredicate::SignedLessEqual:
                        case ComparisonPredicate::UnsignedLessEqual:    jcc = 0x86; break; // jbe
                        case ComparisonPredicate::SignedGreaterThan:
                        case ComparisonPredicate::UnsignedGreaterThan:  jcc = 0x87; break; // ja
                        case ComparisonPredicate::SignedGreaterEqual:
                        case ComparisonPredicate::UnsignedGreaterEqual: jcc = 0x83; break; // jae
                        case ComparisonPredicate::Ordered:             jcc = 0x8B; break; // jnp
                        case ComparisonPredicate::Unordered:           jcc = 0x8A; break; // jp
                        default:                                       jcc = 0x84; break;
                    }
                } else {
                    switch (pred) {
                        case ComparisonPredicate::Equal:              jcc = 0x84; break; // je
                        case ComparisonPredicate::NotEqual:           jcc = 0x85; break; // jne
                        case ComparisonPredicate::SignedLessThan:      jcc = 0x8C; break; // jl
                        case ComparisonPredicate::SignedLessEqual:     jcc = 0x8E; break; // jle
                        case ComparisonPredicate::SignedGreaterThan:   jcc = 0x8F; break; // jg
                        case ComparisonPredicate::SignedGreaterEqual:  jcc = 0x8D; break; // jge
                        case ComparisonPredicate::UnsignedLessThan:    jcc = 0x82; break; // jb
                        case ComparisonPredicate::UnsignedLessEqual:   jcc = 0x86; break; // jbe
                        case ComparisonPredicate::UnsignedGreaterThan:  jcc = 0x87; break; // ja
                        case ComparisonPredicate::UnsignedGreaterEqual: jcc = 0x83; break; // jae
                        case ComparisonPredicate::Ordered:            jcc = 0x8B; break; // jnp
                        case ComparisonPredicate::Unordered:          jcc = 0x8A; break; // jp
                        default:                                      jcc = 0x84; break;
                    }
                }

                emit_u8(text, 0x0F);
                emit_u8(text, jcc);
                const std::size_t patch_pos = text.size();
                emit_u32(text, 0);
                patches.push_back({patch_pos, true_block, false});

                if (false_block != INVALID_BLOCK_ID) {
                    emit_u8(text, 0xE9);
                    const std::size_t false_patch = text.size();
                    emit_u32(text, 0);
                    patches.push_back({false_patch, false_block, false});
                }
                return true;
            }

            case MachineForm::Call: {
                std::string callee_name;
                std::uintptr_t direct_addr = 0;
                std::uint8_t reg_target = 0xFF;

                for (const auto& op : inst.operands) {
                    if (op.kind == MachineOperandKind::Symbol) {
                        auto it = symbol_table.find(op.symbol);
                        if (it != symbol_table.end()) {
                            callee_name = it->second;
                        } else {
                            callee_name = "sym_" + std::to_string(op.symbol);
                        }
                        break;
                    } else if (op.is_immediate() && direct_addr == 0) {
                        direct_addr = static_cast<std::uintptr_t>(op.immediate);
                    } else if (op.is_register() && reg_target == 0xFF) {
                        reg_target = static_cast<std::uint8_t>(op.reg.id);
                    }
                }

                // Check runtime contract symbols
                if (callee_name.empty()) {
                    if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst.semantic_kind)) {
                        callee_name = sym;
                    } else if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(inst.opcode)) {
                        callee_name = sym;
                    }
                }

                // JIT runtime symbol direct address resolution
                if (!callee_name.empty() && direct_addr == 0) {
                    auto r_it = runtime_symbols.find(callee_name);
                    if (r_it != runtime_symbols.end() && r_it->second != 0) {
                        direct_addr = r_it->second;
                    }
                }

                if (direct_addr != 0) {
                    // mov r11, direct_addr; call r11
                    // 49 BB <imm64>
                    emit_u8(text, 0x49);
                    emit_u8(text, 0xBB);
                    emit_u64(text, direct_addr);
                    // 41 FF D3 (call r11)
                    emit_u8(text, 0x41);
                    emit_u8(text, 0xFF);
                    emit_u8(text, 0xD3);
                    return true;
                }

                if (!callee_name.empty()) {
                    // Direct call: E8 <disp32> with relocation to symbol
                    emit_u8(text, 0xE8);
                    const std::size_t reloc_offset = text.size();
                    emit_u32(text, 0);

                    // Add PCRel32 relocation (IMAGE_REL_AMD64_REL32 = 4)
                    std::string sym = callee_name.starts_with("__imp_") ? callee_name.substr(6) : callee_name;
                    object.add_relocation(
                        reloc_offset, std::move(sym), -4, 4, dgtl::object::RelocationKind::PCRel32);
                    return true;
                }

                if (reg_target != 0xFF) {
                    // call reg (FF /2)
                    const std::uint8_t rex = make_rex(false, 0, 0, reg_target);
                    if (rex != 0x40u) emit_u8(text, rex);
                    emit_u8(text, 0xFF);
                    emit_u8(text, static_cast<std::uint8_t>(0xD0u | (reg_target & 7u)));
                    return true;
                }

                if (out_diag) *out_diag = "Call has no valid target (symbol, address, or register)";
                return false;
            }

            case MachineForm::Return: {
                emit_u8(text, 0xC3); // RET
                return true;
            }

            case MachineForm::HardwareFence: {
                emit_u8(text, 0x0F);
                emit_u8(text, 0xAE);
                if (inst.opcode == GM_ACQUIRE_FENCE) {
                    emit_u8(text, 0xE8); // LFENCE
                } else if (inst.opcode == GM_RELEASE_FENCE) {
                    emit_u8(text, 0xF8); // SFENCE
                } else {
                    emit_u8(text, 0xF0); // MFENCE
                }
                return true;
            }

            case MachineForm::ConcurrentSync: {
                emit_u8(text, 0x0F);
                emit_u8(text, 0xAE);
                emit_u8(text, 0xF0); // MFENCE
                return true;
            }

            case MachineForm::AtomicOp: {
                if (inst.operands.size() >= 2 && inst.operands[0].is_register() && inst.operands[1].is_register()) {
                    // LOCK XADD [reg0], reg1: F0 REX.W 0F C1 /r
                    emit_u8(text, 0xF0);
                    emit_rr_0f(text, 0xC1, static_cast<std::uint8_t>(inst.operands[1].reg.id),
                               static_cast<std::uint8_t>(inst.operands[0].reg.id), true);
                    return true;
                }
                // Fallback atomic barrier
                emit_u8(text, 0x0F);
                emit_u8(text, 0xAE);
                emit_u8(text, 0xF0);
                return true;
            }

            case MachineForm::VectorBinary512: {
                if (inst.operands.size() < 3) {
                    if (out_diag) *out_diag = "VectorBinary512 requires >= 3 operands";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& src1 = inst.operands[1];
                const auto& src2 = inst.operands[2];
                if (!dst.is_register() || !src1.is_register()) {
                    if (out_diag) *out_diag = "VectorBinary512 invalid operands";
                    return false;
                }

                std::uint8_t opc = 0x58; // VADDPS
                if (inst.semantic_kind == MachineSemanticKind::FloatSub) opc = 0x5C;
                else if (inst.semantic_kind == MachineSemanticKind::FloatMul) opc = 0x59;
                else if (inst.semantic_kind == MachineSemanticKind::FloatDiv) opc = 0x5E;

                const std::uint8_t dst_id = static_cast<std::uint8_t>(dst.reg.id);
                const std::uint8_t s1_id = static_cast<std::uint8_t>(src1.reg.id);

                if (src2.is_register()) {
                    const std::uint8_t s2_id = static_cast<std::uint8_t>(src2.reg.id);
                    emit_vex3(text, dst_id, s1_id, s2_id, 0u /* pp */, 1u /* m=0F */, false /* W */, true /* L=256 */);
                    emit_u8(text, opc);
                    emit_u8(text, static_cast<std::uint8_t>(0xC0u | ((dst_id & 7u) << 3) | (s2_id & 7u)));
                    return true;
                }
                if (src2.is_memory()) {
                    emit_vex3(text, dst_id, s1_id, static_cast<std::uint8_t>(src2.memory.base.id), 0u, 1u, false, true);
                    emit_u8(text, opc);
                    emit_u8(text, static_cast<std::uint8_t>(((dst_id & 7u) << 3) | (src2.memory.base.id & 7u)));
                    return true;
                }
                if (out_diag) *out_diag = "VectorBinary512 unsupported operand 2";
                return false;
            }

            case MachineForm::VectorGather512:
            case MachineForm::VectorStore512: {
                if (inst.operands.size() < 2) {
                    if (out_diag) *out_diag = "VectorLoadStore requires >= 2 operands";
                    return false;
                }
                const auto& op0 = inst.operands[0];
                const auto& op1 = inst.operands[1];
                const bool is_store = (inst.form == MachineForm::VectorStore512);
                const auto& reg_op = is_store ? op1 : op0;
                const auto& mem_op = is_store ? op0 : op1;

                if (!reg_op.is_register() || !mem_op.is_memory()) {
                    if (out_diag) *out_diag = "VectorLoadStore requires register and memory";
                    return false;
                }

                const std::uint8_t reg_id = static_cast<std::uint8_t>(reg_op.reg.id);
                const std::uint8_t base_id = static_cast<std::uint8_t>(mem_op.memory.base.id);
                const std::uint8_t opc = is_store ? 0x11 : 0x10; // VMOVUPS store / load

                emit_vex3(text, reg_id, 0u, base_id, 0u, 1u, false, true);
                emit_u8(text, opc);
                emit_u8(text, static_cast<std::uint8_t>(((reg_id & 7u) << 3) | (base_id & 7u)));
                return true;
            }

            case MachineForm::TensorOp: {
                // VFMADD213PS: 0xC4 m-mmmm=00010 W=0 ~vvvv L=1 pp=01 Opcode=0xA8
                if (inst.operands.size() < 3) {
                    if (out_diag) *out_diag = "TensorOp requires >= 3 operands";
                    return false;
                }
                const auto& dst = inst.operands[0];
                const auto& src1 = inst.operands[1];
                const auto& src2 = inst.operands[2];
                if (!dst.is_register() || !src1.is_register() || !src2.is_register()) {
                    if (out_diag) *out_diag = "TensorOp registers required";
                    return false;
                }
                const std::uint8_t dst_id = static_cast<std::uint8_t>(dst.reg.id);
                const std::uint8_t s1_id = static_cast<std::uint8_t>(src1.reg.id);
                const std::uint8_t s2_id = static_cast<std::uint8_t>(src2.reg.id);
                emit_vex3(text, dst_id, s1_id, s2_id, 1u /* pp=66 */, 2u /* m=0F38 */, false, true);
                emit_u8(text, 0xA8);
                emit_u8(text, static_cast<std::uint8_t>(0xC0u | ((dst_id & 7u) << 3) | (s2_id & 7u)));
                return true;
            }

            case MachineForm::Syscall: {
                emit_u8(text, 0x0F);
                emit_u8(text, 0x05); // syscall
                return true;
            }

            default:
                if (out_diag) {
                    *out_diag = "Unsupported MachineForm: " + std::string(machine_form_name(inst.form)) +
                                " (form_id=" + std::to_string(static_cast<uint32_t>(inst.form)) +
                                "), semantic_kind=" + std::to_string(static_cast<uint32_t>(inst.semantic_kind)) +
                                ", opcode=" + std::to_string(inst.opcode) +
                                ", operands=" + std::to_string(inst.operands.size());
                }
                return false;
        }
    }
};

} // namespace dgtl::isa::x64
