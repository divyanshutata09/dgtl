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

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 4: BACKEND EMISSION - ARM64 / AARCH64
 * FULL SYNCHRONIZATION: Aligned with 178 IR OpCodes, ISel DSL Bootstrap Rules & Precise ARM64 EABI Layout.
 */

module;

#include <cstdlib>

#include <cstdint>
#include <ostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <functional>

export module dgtl.ir_core.arm64_emitter;

import dgtl.ir_core;

import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.abi_lowering;

import dgtl.assertions;
import dgtl.flat_map;

// Bring in IR types for local scope visibility
using dgtl::ir::OpCode;
using dgtl::ir::IRNode;
using dgtl::ir::IRCore;
using dgtl::ir::IRStream;
using dgtl::semantic::SymbolTable;
using dgtl::ir::INVALID_SLOT;

// FIX: Namespace aliasing instead of 'using' to prevent GCC/MinGW ambiguity
namespace Flags = dgtl::ir::Flags;

export namespace dgtl::isa {

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

        // --- FIX: Pre-scan LABEL nodes to build label_id -> stream_index map ---
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(m_stream.size() / 4 + 4);
        for (size_t i = 0; i < m_stream.size(); ++i) {
            const auto& atom = m_stream.get_atom_readonly(i);
            const auto& core = m_stream.get_core_readonly(i);
            if (atom.opcode == OpCode::LABEL) {
                if (core.result_id != INVALID_SLOT && core.result_id != 0u) {
                    label_map[core.result_id] = i;
                }
                if (core.op_a != INVALID_SLOT && core.op_a != 0u) {
                    label_map[core.op_a] = i;
                }
                if (core.op_b != INVALID_SLOT && core.op_b != 0u) {
                    label_map[core.op_b] = i;
                }
                label_map[static_cast<uint32_t>(i)] = i;
            }
        }
        // Helper: resolve a branch target through label_map or return as-is
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            return m_stream.size(); // fallback to epilogue
        };
        // -----------------------------------------------------------------------

        emit_header(out, config);

        for (size_t i = 0; i < m_stream.size(); ++i) {
            const auto& atom = m_stream.get_atom_readonly(i);
            const auto& core = m_stream.get_core_readonly(i);

            out << ".L" << i << ":\n";
            
            if (atom.opcode == OpCode::NOP) continue;
            if (atom.opcode == OpCode::LABEL) continue; // pseudo-op, no code
            if ((atom.opcode == OpCode::LOAD_IMM || atom.opcode == OpCode::IMMEDIATE) &&
                is_foldable_ret_immediate(i, core)) {
                continue;
            }

            out << "    "; 
            
            // Fixed: Accessing Flags through alias or explicit scope
            const bool is_simd = (atom.flags & Flags::IS_SIMD_BURST);

            switch (atom.opcode) {
                case OpCode::REG_MOVE:  emit_move(out, core); break;
                case OpCode::ADD_I64:
                case OpCode::ADD_I32:   emit_arithmetic(out, "add", core); break;
                case OpCode::SUB_I64:
                case OpCode::SUB_I32:   emit_arithmetic(out, "sub", core); break;
                case OpCode::MUL_I64:
                case OpCode::MUL_I32:   emit_arithmetic(out, "mul", core); break;
                case OpCode::DIV_I64:
                case OpCode::DIV_I32:   emit_arithmetic(out, "sdiv", core); break;
                case OpCode::MOD_I64:
                case OpCode::MOD_I32:   emit_mod_i32(out, core); break;
                case OpCode::AND_I64:
                case OpCode::AND_I32:   emit_arithmetic(out, "and", core); break;
                case OpCode::OR_I64:
                case OpCode::OR_I32:    emit_arithmetic(out, "orr", core); break;
                case OpCode::XOR_I64:
                case OpCode::XOR_I32:   emit_arithmetic(out, "eor", core); break;
                case OpCode::SHL_I64:
                case OpCode::SHL_I32:   emit_shift_left(out, core); break;
                case OpCode::SHR_I64:
                case OpCode::SHR_I32:   emit_shift_right(out, core); break;
                case OpCode::CMP_EQ_I32: emit_cmp_set(out, "eq", core); break;
                case OpCode::CMP_NE_I32: emit_cmp_set(out, "ne", core); break;
                case OpCode::CMP_LT_I32: emit_cmp_set(out, "lt", core); break;
                case OpCode::CMP_LE_I32: emit_cmp_set(out, "le", core); break;
                case OpCode::CMP_GT_I32: emit_cmp_set(out, "gt", core); break;
                case OpCode::CMP_GE_I32: emit_cmp_set(out, "ge", core); break;
                case OpCode::ADD_F32:   emit_arithmetic(out, "fadd", core, true); break;
                case OpCode::SUB_F32:   emit_arithmetic(out, "fsub", core, true); break;
                case OpCode::MUL_F32:   emit_arithmetic(out, "fmul", core, true); break;
                case OpCode::DIV_F32:   emit_arithmetic(out, "fdiv", core, true); break;
                case OpCode::ADD_F64:   emit_arithmetic_f64(out, "fadd", core); break;
                case OpCode::SUB_F64:   emit_arithmetic_f64(out, "fsub", core); break;
                case OpCode::MUL_F64:   emit_arithmetic_f64(out, "fmul", core); break;
                case OpCode::DIV_F64:   emit_arithmetic_f64(out, "fdiv", core); break;
                case OpCode::FMA_F32:   emit_fma(out, core); break;
                case OpCode::DGTL_FAST_FMA: emit_fast_fma_f64(out, core); break;
                case OpCode::SIMD_COMPLEX_FMA: emit_simd_complex_fma(out, core); break;
                case OpCode::FMA_WIDEN_F16_F32: emit_fma_widen(out, core); break;
                case OpCode::LOAD_IMM:
                case OpCode::IMMEDIATE: emit_load_imm(out, core); break;
                case OpCode::LOAD:
                case OpCode::VOLATILE_LOAD: emit_load(out, core); break;
                case OpCode::STORE:
                case OpCode::VOLATILE_STORE: emit_store(out, core); break;
                case OpCode::BRANCH:
                case OpCode::GOTO:      emit_branch(out, core, resolve_target); break;
                case OpCode::BRANCH_COND: emit_branch_cond(out, core, resolve_target); break;
                case OpCode::BRANCH_LIKELY:
                case OpCode::BRANCH_UNLIKELY: emit_branch_cond(out, core, resolve_target); break;
                case OpCode::BRANCH_COMPARE_FUSED: emit_branch_compare_fused(out, core, resolve_target); break;
                case OpCode::PHI_NODE:  emit_phi_comment(out, core); break;
                case OpCode::CALL:      emit_call(out, core, i); break;
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
                    if (emit_runtime_fastpath(out, atom.opcode, core)) {
                        break;
                    }
                    if (const char* runtime_symbol = runtime_symbol_for_opcode(atom.opcode); runtime_symbol != nullptr) {
                        emit_runtime_call(out, runtime_symbol, core);
                        break;
                    }
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
            if (atom.opcode != OpCode::LOAD_IMM && atom.opcode != OpCode::IMMEDIATE &&
                atom.opcode != OpCode::STRING_CONST) continue;

            const auto& core = m_stream.get_core_readonly(i);
            if (core.result_id != reg) continue;

            if (core.intent_hash != 0) {
                imm_out = core.intent_hash;
            } else {
                const uint32_t hi = (core.op_a == INVALID_SLOT) ? 0u : core.op_a;
                imm_out = (static_cast<uint64_t>(hi) << 32) | core.op_b;
            }
            return true;
        }
        return false;
    }

    static void emit_header(std::ostream& out, const Config& config) {
        out << "// --- DGTL ARM64 BACKEND ---\n";
        out << ".arch armv8-a\n.text\n.align 4\n.global _DGTL_entry\n\n_DGTL_entry:\n";
        if (!config.omit_fp) out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n";
    }

    static void emit_footer(std::ostream& out, const Config& config) {
        out << ".L_epilogue:\n";
        if (!config.omit_fp) out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }

    static void emit_arithmetic(std::ostream& out, const char* instr, const IRCore& core, bool is_fp = false) {
        char prefix = is_fp ? 's' : 'x'; 
        out << instr << " " << prefix << core.result_id << ", " << prefix << core.op_a << ", " << prefix << core.op_b;
    }

    static void emit_arithmetic_f64(std::ostream& out, const char* instr, const IRCore& core) {
        out << instr << " d" << core.result_id << ", d" << core.op_a << ", d" << core.op_b;
    }

    static void emit_move(std::ostream& out, const IRCore& core) {
        if (core.result_id == INVALID_SLOT && core.op_a != INVALID_SLOT && core.op_b != INVALID_SLOT) {
            out << "mov x" << core.op_a << ", x" << core.op_b;
        } else {
            out << "mov x" << core.result_id << ", x" << core.op_a;
        }
    }

    static void emit_shift_left(std::ostream& out, const IRCore& core) {
        out << "lslv x" << core.result_id << ", x" << core.op_a << ", x" << core.op_b;
    }

    static void emit_shift_right(std::ostream& out, const IRCore& core) {
        out << "lsrv x" << core.result_id << ", x" << core.op_a << ", x" << core.op_b;
    }

    static void emit_fma(std::ostream& out, const IRCore& core) {
        // fmadd Sd, Sn, Sm, Sa  => d = n*m + a
        out << "fmadd s" << core.result_id << ", s" << core.op_a << ", s" << core.op_b << ", s" << core.op_c;
    }

    static void emit_fast_fma_f64(std::ostream& out, const IRCore& core) {
        // DGTL fast FMA in f64 domain.
        // fmadd Dd, Dn, Dm, Da => d = n*m + a
        out << "fmadd d" << core.result_id << ", d" << core.op_a << ", d" << core.op_b << ", d" << core.op_c;
    }

    static void emit_simd_complex_fma(std::ostream& out, const IRCore& core) {
        // Conservative SIMD FMA representation in pure AArch64:
        // acc = acc + a * b using packed 4-lane single precision registers.
        out << "fmla v" << core.result_id << ".4s, v" << core.op_a << ".4s, v" << core.op_b << ".4s";
    }

    static void emit_fma_widen(std::ostream& out, const IRCore& core) {
        // Temporal fusion: widen half-precision lane and consume immediately in FMA.
        // fcvtl Vd.4S, Vn.4H
        // fmadd Sd, Sd, Sm, Sa
        out << "fcvtl v" << core.result_id << ".4s, v" << core.op_a << ".4h\n";
        out << "    fmadd s" << core.result_id << ", s" << core.result_id
            << ", s" << core.op_b << ", s" << core.op_c;
    }

    static void emit_load_imm(std::ostream& out, const IRCore& core) {
        const uint32_t high = (core.op_a == ir::INVALID_SLOT) ? 0u : core.op_a;
        const uint64_t imm = (static_cast<uint64_t>(high) << 32) | core.op_b;
        const uint32_t part0 = static_cast<uint32_t>(imm & 0xFFFFULL);
        const uint32_t part1 = static_cast<uint32_t>((imm >> 16) & 0xFFFFULL);
        const uint32_t part2 = static_cast<uint32_t>((imm >> 32) & 0xFFFFULL);
        const uint32_t part3 = static_cast<uint32_t>((imm >> 48) & 0xFFFFULL);

        out << "movz x" << core.result_id << ", #" << part0;
        if (part1 != 0) out << "\n    movk x" << core.result_id << ", #" << part1 << ", lsl #16";
        if (part2 != 0) out << "\n    movk x" << core.result_id << ", #" << part2 << ", lsl #32";
        if (part3 != 0) out << "\n    movk x" << core.result_id << ", #" << part3 << ", lsl #48";
    }

    static void emit_load(std::ostream& out, const IRCore& core) {
        const uint32_t offset = (core.op_c != ir::INVALID_SLOT && core.op_c != 0u) ? core.op_c : ((core.op_b != ir::INVALID_SLOT) ? core.op_b : 0u);
        out << "ldr x" << core.result_id << ", [x" << core.op_a << ", #" << offset << "]";
    }

    static void emit_store(std::ostream& out, const IRCore& core) {
        const uint32_t offset = (core.op_c != ir::INVALID_SLOT && core.op_c != 0u) ? core.op_c : 0u;
        out << "str x" << core.op_b << ", [x" << core.op_a << ", #" << offset << "]";
    }

    static void emit_neon_vec_op(std::ostream& out, const char* instr, const IRCore& core) {
        out << instr << " v" << core.result_id << ".4s, v" << core.op_a << ".4s, v" << core.op_b << ".4s";
    }

    static void emit_fcvtl(std::ostream& out, const IRCore& core) {
        // fcvtl Vd.4S, Vn.4H
        out << "fcvtl v" << core.result_id << ".4s, v" << core.op_a << ".4h";
    }

    static void emit_fcvtn(std::ostream& out, const IRCore& core) {
        // fcvtn Vd.4H, Vn.4S
        out << "fcvtn v" << core.result_id << ".4h, v" << core.op_a << ".4s";
    }

    static void emit_bsl_predicate(std::ostream& out, const IRCore& core) {
        // result_id := masked blend(mask=op_a, true=op_b, false=op_c)
        out << "mov v" << core.result_id << ".16b, v" << core.op_a << ".16b\n";
        out << "    bsl v" << core.result_id << ".16b, v" << core.op_b << ".16b, v" << core.op_c << ".16b";
    }

    static void emit_branch(std::ostream& out, const IRCore& core,
                     const std::function<size_t(uint32_t)>& resolve_target) {
        out << "b .L" << resolve_target(core.op_a);
    }

    static void emit_branch_cond(std::ostream& out, const IRCore& core,
                          const std::function<size_t(uint32_t)>& resolve_target) {
        out << "cbnz x" << core.op_a << ", .L" << resolve_target(core.op_b);
    }

    static void emit_branch_compare_fused(std::ostream& out, const IRCore& core,
                                   const std::function<size_t(uint32_t)>& resolve_target) {
        // op_a = lhs register, op_b = rhs register, op_c = target block label
        out << "cmp x" << core.op_a << ", x" << core.op_b << "\n";
        out << "    b.ne .L" << resolve_target(core.op_c);
    }

    static void emit_phi_comment(std::ostream& out, const IRCore& core) {
        // PHI nodes are resolved by register allocation; emit as comment.
        out << "// PHI x" << core.result_id << " <- x" << core.op_a << ", x" << core.op_b << ", x" << core.op_c;
    }

    static void emit_mod_i32(std::ostream& out, const IRCore& core) {
        // ARM64: msub Rd, Qtmp, Divisor, Dividend (Rd = Dividend - Qtmp * Divisor)
        out << "sdiv x16, x" << core.op_a << ", x" << core.op_b << "\n";
        out << "    msub x" << core.result_id << ", x16, x" << core.op_b << ", x" << core.op_a;
    }

    static void emit_cmp_set(std::ostream& out, const char* cond, const IRCore& core) {
        out << "cmp x" << core.op_a << ", x" << core.op_b << "\n";
        out << "    cset x" << core.result_id << ", " << cond;
    }

    void emit_call(std::ostream& out, const IRCore& core, size_t node_idx) {
        std::vector<uint32_t> args;
        args.reserve(8);
        if (core.op_b != ir::INVALID_SLOT) args.push_back(core.op_b);
        if (core.op_c != ir::INVALID_SLOT) args.push_back(core.op_c);
        if (core.logic_flags != ir::INVALID_SLOT) args.push_back(core.logic_flags);
        const auto extra_args = m_stream.get_call_overflow_args(node_idx);
        std::copy_if(extra_args.begin(), extra_args.end(), std::back_inserter(args),
                     [](uint32_t extra) { return extra != ir::INVALID_SLOT; });

        const std::vector<uint32_t> layout_words = m_stream.get_call_abi_layout_words(node_idx);
        if (layout_words.size() >= 4) {
            const uint32_t param_count = layout_words[0];
            const uint32_t stack_size = layout_words[1];
            const size_t required_words = 4u + static_cast<size_t>(param_count) * 4u;
            if (layout_words.size() >= required_words && stack_size > 0u) {
                out << "sub sp, sp, #" << stack_size << "\n";
            }

            if (layout_words.size() >= required_words) {
                size_t w = 4u;
                const size_t count = std::min<size_t>(param_count, args.size());
                for (size_t i = 0; i < count; ++i, w += 4u) {
                    const bool in_register = (layout_words[w] != 0u);
                    const uint32_t reg_id = layout_words[w + 1u];
                    const uint32_t stack_offset = layout_words[w + 2u];
                    const uint32_t src = args[i] & 0x1Fu;

                    if (in_register && reg_id < 8u) {
                        out << "mov x" << reg_id << ", x" << src << "\n";
                    } else if (in_register && reg_id >= 32u && reg_id < 40u) {
                        out << "fmov d" << (reg_id - 32u) << ", d" << src << "\n";
                    } else {
                        out << "str x" << src << ", [sp, #" << stack_offset << "]\n";
                    }
                }
            }

            out << "bl _DGTL_fn_" << core.op_a;
            if (layout_words.size() >= required_words && stack_size > 0u) {
                out << "\n    add sp, sp, #" << stack_size;
            }
            return;
        }

        // Conservative fallback if ABI sidecar is unavailable.
        if (core.op_b != ir::INVALID_SLOT) {
            out << "mov x0, x" << (core.op_b & 0x1Fu) << "\n";
        }
        if (core.op_c != ir::INVALID_SLOT) {
            out << "    mov x1, x" << (core.op_c & 0x1Fu) << "\n";
        }
        out << "    bl _DGTL_fn_" << core.op_a;
    }

    static void emit_syscall(std::ostream& out) {
        out << "svc #0";
    }

    static void emit_bitfield_insert(std::ostream& out, const IRCore& core) {
        // bfi Xd, Xn, #lsb, #width
        out << "bfi x" << core.result_id << ", x" << core.op_a
            << ", #" << (core.op_b & 63U) << ", #" << ((core.op_c & 63U) + 1U);
    }

    static void emit_bitfield_extract(std::ostream& out, const IRCore& core) {
        // ubfx Xd, Xn, #lsb, #width
        out << "ubfx x" << core.result_id << ", x" << core.op_a
            << ", #" << (core.op_b & 63U) << ", #" << ((core.op_c & 63U) + 1U);
    }

    static void emit_fallback(std::ostream& out, OpCode opcode) {
        out << "nop";
        out << " // [DGTL-EMIT-FALLBACK] OpCode=" << dgtl::ir::opcode_name(opcode)
            << "(" << static_cast<uint32_t>(opcode) << ")";
    }

    static const char* runtime_symbol_for_opcode(OpCode opcode) {
        return dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(opcode);
    }

    static bool emit_runtime_fastpath(std::ostream& out, OpCode opcode, const IRCore& core) {
        switch (opcode) {
            case OpCode::ACQUIRE_FENCE:
                out << "dmb ishld";
                return true;
            case OpCode::RELEASE_FENCE:
            case OpCode::ATOMIC_FENCE:
            case OpCode::MEMORY_FENCE:
            case OpCode::BARRIER_SYNC:
            case OpCode::SYNC_BARRIER:
                out << "dmb ish";
                return true;
            case OpCode::PREFETCH:
            case OpCode::PREFETCH_T0:
                out << "prfm pldl1keep, [x" << core.op_a << "]";
                return true;
            case OpCode::CLFLUSH:
                out << "dc cvau, x" << core.op_a << "\n";
                out << "    dsb ish\n";
                out << "    isb";
                return true;
            case OpCode::READ_TSC:
                out << "mrs x" << core.result_id << ", cntvct_el0";
                return true;
            case OpCode::PTR_DIFF:
                out << "sub x" << core.result_id << ", x" << core.op_a << ", x" << core.op_b;
                return true;
            case OpCode::GEP:
                out << "add x" << core.result_id << ", x" << core.op_a << ", x" << core.op_b;
                return true;
            case OpCode::PTR_TO_INT:
            case OpCode::INT_TO_PTR:
                out << "mov x" << core.result_id << ", x" << core.op_a;
                return true;
            case OpCode::LEA: {
                uint32_t shift = 0;
                switch (core.op_c) {
                    case 1u: shift = 0; break;
                    case 2u: shift = 1; break;
                    case 4u: shift = 2; break;
                    case 8u: shift = 3; break;
                    default: return false;
                }
                out << "add x" << core.result_id << ", x" << core.op_a << ", x" << core.op_b << ", lsl #" << shift;
                return true;
            }
            case OpCode::BOOL_SELECT:
            case OpCode::SELECT:
                out << "cmp x" << core.op_a << ", #0\n";
                out << "    csel x" << core.result_id << ", x" << core.op_b << ", x" << core.op_c << ", ne";
                return true;
            case OpCode::PHI_NODE:
                out << "cmp x" << core.op_c << ", #0\n";
                out << "    csel x" << core.result_id << ", x" << core.op_b << ", x" << core.op_a << ", ne";
                return true;
            case OpCode::GUARD_VALUE:
                out << "cmp x" << core.op_a << ", x" << core.op_b << "\n";
                out << "    csel x" << core.result_id << ", x" << core.op_a << ", x" << core.op_c << ", eq";
                return true;
            case OpCode::GUARD_ALIAS:
                out << "mov x16, #1\n";
                out << "    cmp x" << core.op_a << ", x" << core.op_b << "\n";
                out << "    csel x" << core.result_id << ", x16, x" << core.op_c << ", eq";
                return true;
            default:
                return false;
        }
    }

    static void emit_runtime_call(std::ostream& out, const char* symbol, const IRCore& core) {
        out << "mov x0, x" << core.op_a << "\n";
        out << "    mov x1, x" << core.op_b << "\n";
        out << "    mov x2, x" << core.op_c << "\n";
        out << "    bl " << symbol;
        if (core.result_id != ir::INVALID_SLOT && core.result_id != 0u) {
            out << "\n    mov x" << core.result_id << ", x0";
        }
    }

    void emit_ret(std::ostream& out, const IRCore& core) {
        if (core.op_a != ir::INVALID_SLOT) {
            uint64_t ret_imm = 0;
            if (resolve_ret_immediate(core.op_a, ret_imm)) {
                const uint32_t part0 = static_cast<uint32_t>(ret_imm & 0xFFFFULL);
                const uint32_t part1 = static_cast<uint32_t>((ret_imm >> 16) & 0xFFFFULL);
                const uint32_t part2 = static_cast<uint32_t>((ret_imm >> 32) & 0xFFFFULL);
                const uint32_t part3 = static_cast<uint32_t>((ret_imm >> 48) & 0xFFFFULL);
                out << "movz x0, #" << part0;
                if (part1 != 0) out << "\n    movk x0, #" << part1 << ", lsl #16";
                if (part2 != 0) out << "\n    movk x0, #" << part2 << ", lsl #32";
                if (part3 != 0) out << "\n    movk x0, #" << part3 << ", lsl #48";
            } else {
                out << "mov x0, x" << core.op_a;
            }
        }
        out << "\n    b .L_epilogue";
    }
};

} // namespace dgtl::isa

// This alias is CRITICAL. main.cpp uses ir::ARM64Emitter.
export namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}

