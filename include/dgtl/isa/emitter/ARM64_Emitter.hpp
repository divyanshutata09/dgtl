/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 4: BACKEND EMISSION - ARM64 / AARCH64
 * FINAL SYNC: GCC/MinGW Default Initializer Fix & Namespace Alignment
 */

#pragma once
#include <cstdint>
#include <ostream>
#include <iomanip>
#include <string>

#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/core/Symbol_Table.hpp"

namespace dgtl::isa {

// Bring in IR types for local scope visibility
using dgtl::ir::OpCode;
using dgtl::ir::IRNode;
using dgtl::ir::IRCore;
using dgtl::ir::IRStream;
using dgtl::core::SymbolTable;
using dgtl::ir::INVALID_SLOT;

// FIX: Namespace aliasing instead of 'using' to prevent GCC/MinGW ambiguity
namespace Flags = dgtl::ir::Flags;

class alignas(64) ARM64Emitter {
public:
    struct Config {
        bool use_neon         = true;
        bool position_indep  = true; 
        bool omit_fp         = true; 
        bool verbose_asm     = true;
        
        // Explicit constructor prevents MinGW "required before end of class" error
        constexpr Config() noexcept {} 
    };

    explicit ARM64Emitter(const IRStream& stream, const SymbolTable& symbols)
        : m_stream(stream), m_symbols(symbols) {}

    /**
     * @brief Generates AArch64 Assembly.
     * Fixed: Use brace-init for default to bypass GCC's incomplete type check.
     */
    void emit_assembly(std::ostream& out, const Config config = {}) {
        if (m_stream.is_null() || m_stream.size() == 0) return;

        emit_header(out, config);

        for (size_t i = 0; i < m_stream.size(); ++i) {
            const auto& atom = m_stream.get_atom_readonly(i);
            const auto& core = m_stream.get_core_readonly(i);
            
            if (atom.opcode == OpCode::NOP) continue;
            if ((atom.opcode == OpCode::LOAD_IMM || atom.opcode == OpCode::IMMEDIATE) &&
                is_foldable_ret_immediate(i, core)) {
                continue;
            }

            out << "    "; 
            
            // Fixed: Accessing Flags through alias or explicit scope
            const bool is_simd = (atom.flags & Flags::IS_SIMD_BURST);

            switch (atom.opcode) {
                case OpCode::REG_MOVE:  emit_move(out, core); break;
                case OpCode::ADD_I32:   emit_arithmetic(out, "add", core); break;
                case OpCode::SUB_I32:   emit_arithmetic(out, "sub", core); break;
                case OpCode::MUL_I32:   emit_arithmetic(out, "mul", core); break;
                case OpCode::DIV_I32:   emit_arithmetic(out, "sdiv", core); break;
                case OpCode::AND_I32:   emit_arithmetic(out, "and", core); break;
                case OpCode::OR_I32:    emit_arithmetic(out, "orr", core); break;
                case OpCode::XOR_I32:   emit_arithmetic(out, "eor", core); break;
                case OpCode::SHL_I32:   emit_shift_left(out, core); break;
                case OpCode::ADD_F32:   emit_arithmetic(out, "fadd", core, true); break;
                case OpCode::SUB_F32:   emit_arithmetic(out, "fsub", core, true); break;
                case OpCode::MUL_F32:   emit_arithmetic(out, "fmul", core, true); break;
                case OpCode::DIV_F32:   emit_arithmetic(out, "fdiv", core, true); break;
                case OpCode::FMA_F32:   emit_fma(out, core); break;
                case OpCode::FMA_WIDEN_F16_F32: emit_fma_widen(out, core); break;
                case OpCode::LOAD_IMM:
                case OpCode::IMMEDIATE: emit_load_imm(out, core); break;
                case OpCode::LOAD:      emit_load(out, core); break;
                case OpCode::STORE:     emit_store(out, core); break;
                case OpCode::BRANCH:
                case OpCode::GOTO:      emit_branch(out, core); break;
                case OpCode::BRANCH_COND: emit_branch_cond(out, core); break;
                case OpCode::CALL:      emit_call(out, core); break;
                case OpCode::SYSCALL:   emit_syscall(out); break;
                case OpCode::BIT_FIELD_INSERT: emit_bitfield_insert(out, core); break;
                case OpCode::BIT_FIELD_EXTRACT: emit_bitfield_extract(out, core); break;
                case OpCode::RET:       emit_ret(out, core); break;
                case OpCode::VEC_ADD_F32_512: 
                    if (config.use_neon || is_simd) emit_neon_vec_op(out, "fadd", core);
                    break;
                case OpCode::VEC_MUL_F32_512:
                    if (config.use_neon || is_simd) emit_neon_vec_op(out, "fmul", core);
                    break;
                case OpCode::FCVTL_F16_TO_F32:
                    if (config.use_neon || is_simd) emit_fcvtl(out, core);
                    break;
                case OpCode::FCVTN_F32_TO_F16:
                    if (config.use_neon || is_simd) emit_fcvtn(out, core);
                    break;
                case OpCode::SIMD_BSL_PREDICATE:
                    if (config.use_neon || is_simd) emit_bsl_predicate(out, core);
                    break;
                default:
                    emit_fallback(out, atom.opcode);
                    break;
            }

            if (config.verbose_asm) {
                out << std::setw(28) << std::right << " // TR_HASH:0x" 
                    << std::hex << std::uppercase << core.intent_hash << std::dec << std::nouppercase;
            }
            out << "\n";
        }
        emit_footer(out, config);
    }

private:
    const IRStream& m_stream;
    const SymbolTable& m_symbols;

    bool is_foldable_ret_immediate(size_t load_idx, const IRCore& load_core) const {
        if (load_core.result_id == INVALID_SLOT) return false;
        bool used_by_ret = false;

        for (size_t i = load_idx + 1; i < m_stream.size(); ++i) {
            const auto& atom = m_stream.get_atom_readonly(i);
            const auto& core = m_stream.get_core_readonly(i);
            if (atom.opcode == OpCode::NOP) continue;

            const bool references_reg =
                core.op_a == load_core.result_id ||
                core.op_b == load_core.result_id ||
                core.op_c == load_core.result_id;

            if (!references_reg) continue;

            if (atom.opcode == OpCode::RET && core.op_a == load_core.result_id && !used_by_ret) {
                used_by_ret = true;
                continue;
            }

            return false;
        }

        return used_by_ret;
    }

    bool resolve_ret_immediate(uint32_t reg, uint64_t& imm_out) const {
        for (size_t i = m_stream.size(); i-- > 0;) {
            const auto& atom = m_stream.get_atom_readonly(i);
            if (atom.opcode != OpCode::LOAD_IMM && atom.opcode != OpCode::IMMEDIATE) continue;

            const auto& core = m_stream.get_core_readonly(i);
            if (core.result_id != reg) continue;

            imm_out = (static_cast<uint64_t>(core.op_a) << 32) | core.op_b;
            return true;
        }
        return false;
    }

    void emit_header(std::ostream& out, const Config& config) {
        out << "// --- DGTL-MLIR-X ARM64 BACKEND ---\n";
        out << ".arch armv8-a\n.text\n.align 4\n.global _DGTL_entry\n\n_DGTL_entry:\n";
        if (!config.omit_fp) out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n";
    }

    void emit_footer(std::ostream& out, const Config& config) {
        if (!config.omit_fp) out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }

    void emit_arithmetic(std::ostream& out, const char* instr, const IRCore& core, bool is_fp = false) {
        char prefix = is_fp ? 's' : 'x'; 
        out << instr << " " << prefix << core.result_id << ", " << prefix << core.op_a << ", " << prefix << core.op_b;
    }

    void emit_move(std::ostream& out, const IRCore& core) {
        out << "mov x" << core.result_id << ", x" << core.op_a;
    }

    void emit_shift_left(std::ostream& out, const IRCore& core) {
        out << "lsl x" << core.result_id << ", x" << core.op_a << ", #" << (core.op_b & 63U);
    }

    void emit_fma(std::ostream& out, const IRCore& core) {
        // fmadd Sd, Sn, Sm, Sa  => d = n*m + a
        out << "fmadd s" << core.result_id << ", s" << core.op_a << ", s" << core.op_b << ", s" << core.op_c;
    }

    void emit_fma_widen(std::ostream& out, const IRCore& core) {
        // Temporal fusion: widen half-precision lane and consume immediately in FMA.
        // fcvtl Vd.4S, Vn.4H
        // fmadd Sd, Sd, Sm, Sa
        out << "fcvtl v" << core.result_id << ".4s, v" << core.op_a << ".4h\n";
        out << "    fmadd s" << core.result_id << ", s" << core.result_id
            << ", s" << core.op_b << ", s" << core.op_c;
    }

    void emit_load_imm(std::ostream& out, const IRCore& core) {
        const uint64_t imm = (static_cast<uint64_t>(core.op_a) << 32) | core.op_b;
        out << "mov x" << core.result_id << ", #" << imm;
    }

    void emit_load(std::ostream& out, const IRCore& core) {
        out << "ldr x" << core.result_id << ", [x" << core.op_a << ", #" << core.op_b << "]";
    }

    void emit_store(std::ostream& out, const IRCore& core) {
        out << "str x" << core.op_a << ", [x" << core.op_b << ", #" << core.op_c << "]";
    }

    void emit_neon_vec_op(std::ostream& out, const char* instr, const IRCore& core) {
        out << instr << " v" << core.op_c << ".4s, v" << core.op_a << ".4s, v" << core.op_b << ".4s";
    }

    void emit_fcvtl(std::ostream& out, const IRCore& core) {
        // fcvtl Vd.4S, Vn.4H
        out << "fcvtl v" << core.result_id << ".4s, v" << core.op_a << ".4h";
    }

    void emit_fcvtn(std::ostream& out, const IRCore& core) {
        // fcvtn Vd.4H, Vn.4S
        out << "fcvtn v" << core.result_id << ".4h, v" << core.op_a << ".4s";
    }

    void emit_bsl_predicate(std::ostream& out, const IRCore& core) {
        // result_id := masked blend(mask=op_a, true=op_b, false=op_c)
        out << "mov v" << core.result_id << ".16b, v" << core.op_a << ".16b\n";
        out << "    bsl v" << core.result_id << ".16b, v" << core.op_b << ".16b, v" << core.op_c << ".16b";
    }

    void emit_branch(std::ostream& out, const IRCore& core) {
        out << "b .L" << core.op_a;
    }

    void emit_branch_cond(std::ostream& out, const IRCore& core) {
        out << "cbnz x" << core.op_a << ", .L" << core.op_b;
    }

    void emit_call(std::ostream& out, const IRCore& core) {
        out << "bl _DGTL_fn_" << core.op_a;
    }

    void emit_syscall(std::ostream& out) {
        out << "svc #0";
    }

    void emit_bitfield_insert(std::ostream& out, const IRCore& core) {
        // bfi Xd, Xn, #lsb, #width
        out << "bfi x" << core.result_id << ", x" << core.op_a
            << ", #" << (core.op_b & 63U) << ", #" << ((core.op_c & 63U) + 1U);
    }

    void emit_bitfield_extract(std::ostream& out, const IRCore& core) {
        // ubfx Xd, Xn, #lsb, #width
        out << "ubfx x" << core.result_id << ", x" << core.op_a
            << ", #" << (core.op_b & 63U) << ", #" << ((core.op_c & 63U) + 1U);
    }

    void emit_fallback(std::ostream& out, OpCode opcode) {
        out << "nop";
        out << " // [DGTL-EMIT-FALLBACK] OpCode=" << static_cast<uint32_t>(opcode);
    }

    void emit_ret(std::ostream& out, const IRCore& core) {
        if (core.op_a != 0xFF && core.op_a != 0) {
            uint64_t ret_imm = 0;
            if (resolve_ret_immediate(core.op_a, ret_imm)) {
                out << "mov x0, #" << ret_imm << "\n    ";
            } else {
                out << "mov x0, x" << core.op_a << "\n    ";
            }
        }
    }
};

} // namespace dgtl::isa

// This alias is CRITICAL. main.cpp uses ir::ARM64Emitter.
namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}