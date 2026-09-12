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

module;

#include <cstdlib>

#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstdint>
#include <sstream>
#include <functional>
#include <unordered_map>



export module dgtl.ir_core.nasm_emitter;

import dgtl.ir_core;
import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.abi_lowering;
import dgtl.assertions;
import dgtl.flat_map;

// Explicit namespace mapping to prevent ambiguity
using dgtl::ir::OpCode;
using dgtl::ir::IRNode;

export namespace dgtl::isa {

class DGTLNasmEmitter {
    std::string _asm_buffer;

public:
    DGTLNasmEmitter() { 
        _asm_buffer.reserve(1024 * 1024); 
    }

    /**
     * @brief Updated signature to utilize the dgtl::semantic::SymbolTable.
     * This matches the high-velocity identity resolution Pillar 2.
     */
    void emit(const ir::IRStream& stream, const dgtl::semantic::SymbolTable& symbols) {
        // PERMANENT FIX: Clear previous runs to ensure the assertion 
        // in baked_kernel.cpp sees fresh data.
        this->clear();

        if (stream.size() == 0) {
            append_line("; [WARNING] Empty IR Stream provided. No code generated.");
            return;
        }

        // --- FIX: Pre-scan LABEL nodes to build label_id â†’ stream_index map ---
        // This resolves frontend label IDs in branch targets to the correct .L{n} positions.
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(stream.size() / 4 + 4);
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& n = stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                if (n.result_id != ir::INVALID_SLOT && n.result_id != 0u) {
                    label_map[n.result_id] = i;
                }
                if (n.op_a != ir::INVALID_SLOT && n.op_a != 0u) {
                    label_map[n.op_a] = i;
                }
                if (n.op_b != ir::INVALID_SLOT && n.op_b != 0u) {
                    label_map[n.op_b] = i;
                }
                label_map[static_cast<uint32_t>(i)] = i;
            }
        }
        // Helper: resolve a branch target through label_map or return as-is
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            return stream.size(); // fallback to epilogue
        };
        // -----------------------------------------------------------------------

        append_line("; -----------------------------------------------------------");
        append_line("; DGTL TIER-2 PERSISTENT KERNEL");
        append_line(std::format("; Logic Hash: 0x{:016X}", stream.generate_logic_hash()));
        append_line("; -----------------------------------------------------------");
        append_line("section .text");
        append_line("    global DGTL_kernel_entry");
        append_line("    extern __dgtl_rt_print_i64"); // runtime print hook
        append_line("");
        append_line("DGTL_kernel_entry:");
        
        // --- PROLOGUE: Save Windows x64 Non-Volatile Registers ---
        append_line("    push rbp");
        append_line("    mov  rbp, rsp");
        append_line("    push rbx");
        append_line("    push rsi");
        append_line("    push rdi");
        append_line("    push r12");
        append_line("    push r13");
        append_line("    push r14");
        append_line("    push r15");
        append_line("    sub  rsp, 32 ; Allocate Shadow Space");
        append_line("");

        (void)symbols;
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            append_line(std::format(".L{}:", i));
            if (node.opcode == ir::OpCode::NOP) continue;
            if (node.opcode == ir::OpCode::LABEL) continue; // label pseudo-op, no code
            if ((node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE) &&
                is_foldable_ret_immediate(stream, i, node)) {
                continue;
            }

            std::string asm_code = translate_node(stream, node, i, resolve_target);
            
            // Format intent hash for debugging within the ASM
            std::string comment = std::format(" ; [Node {:03d}] Hash:0x{:08X}", i, node.intent_hash);
            append_line(std::format("    {:<45}{}", asm_code, comment));
        }

        // --- EPILOGUE: Restore Registers and Return ---
        append_line(".L_epilogue:");
        append_line("");
        append_line("    add  rsp, 32");
        append_line("    pop  r15");
        append_line("    pop  r14");
        append_line("    pop  r13");
        append_line("    pop  r12");
        append_line("    pop  rdi");
        append_line("    pop  rsi");
        append_line("    pop  rbx");
        append_line("    pop  rbp");
        append_line("    ret");
    }

    const std::string& get_output() const noexcept { return _asm_buffer; }
    
    void clear() noexcept { 
        _asm_buffer.clear(); 
    }

private:
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

    void append_line(std::string_view line) {
        _asm_buffer.append(line);
        _asm_buffer.append("\n");
    }

    std::string translate_node(const ir::IRStream& stream,
                               const ir::IRNode& node,
                               size_t node_idx,
                               const std::function<size_t(uint32_t)>& resolve_target) {
        auto r = [](uint32_t idx) -> const char* {
            static constexpr const char* regs[] = { 
                "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
                "r10", "r11", "r12", "r13", "r14", "r15", "rbx", "rcx"
            };
            return regs[idx & 0xF];
        };

        auto xr = [](uint32_t idx) -> const char* {
            static constexpr const char* xregs[] = {
                "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
            };
            return xregs[idx & 0xF];
        };

        auto emit_runtime_call_text = [&](const char* symbol) -> std::string {
            std::string code = "sub rsp, 32";
            code += std::format(" \n    mov rcx, {}", r(node.op_a));
            code += std::format(" \n    mov rdx, {}", r(node.op_b));
            code += std::format(" \n    mov r8, {}", r(node.op_c));
            code += std::format(" \n    call {}", symbol);
            code += " \n    add rsp, 32";
            if (node.result_id != ir::INVALID_SLOT) {
                code += std::format(" \n    mov {}, rax", r(node.result_id));
            }
            return code;
        };

        switch (node.opcode) {
            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE:
            case ir::OpCode::STRING_CONST: {
                uint64_t imm = node.intent_hash;
                if (imm == 0) {
                    const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
                    imm = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFF);
                }
                return std::format("mov {}, {}", r(node.result_id), imm);
            }

            case ir::OpCode::ADD_I64:
            case ir::OpCode::ADD_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    add {0}, {2}", 
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("add {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::SUB_I64:
            case ir::OpCode::SUB_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    sub {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("sub {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::MUL_I64:
            case ir::OpCode::MUL_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    imul {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("imul {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::ADD_F32: {
                if (node.result_id != node.op_a) {
                    return std::format("movss {0}, {1} \n    addss {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("addss {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::SUB_F32: {
                if (node.result_id != node.op_a) {
                    return std::format("movss {0}, {1} \n    subss {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("subss {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::MUL_F32: {
                if (node.result_id != node.op_a) {
                    return std::format("movss {0}, {1} \n    mulss {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("mulss {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::DIV_F32: {
                if (node.result_id != node.op_a) {
                    return std::format("movss {0}, {1} \n    divss {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("divss {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::ADD_F64: {
                if (node.result_id != node.op_a) {
                    return std::format("movsd {0}, {1} \n    addsd {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("addsd {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::SUB_F64: {
                if (node.result_id != node.op_a) {
                    return std::format("movsd {0}, {1} \n    subsd {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("subsd {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::MUL_F64: {
                if (node.result_id != node.op_a) {
                    return std::format("movsd {0}, {1} \n    mulsd {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("mulsd {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::DIV_F64: {
                if (node.result_id != node.op_a) {
                    return std::format("movsd {0}, {1} \n    divsd {0}, {2}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b));
                }
                return std::format("divsd {}, {}", xr(node.result_id), xr(node.op_b));
            }

            case ir::OpCode::FMA_F32: {
                // Preserve fused lowering in text backend: dst = (a * b) + c
                // using scalar FMA when available.
                if (node.result_id != node.op_a) {
                    return std::format("movaps {0}, {1} \n    vfmadd213ss {0}, {2}, {3}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b), xr(node.op_c));
                }
                return std::format("vfmadd213ss {0}, {1}, {2}",
                                   xr(node.result_id), xr(node.op_b), xr(node.op_c));
            }

            case ir::OpCode::DGTL_FAST_FMA: {
                // DGTL fast f64 FMA path.
                if (node.result_id != node.op_a) {
                    return std::format("movapd {0}, {1} \n    vfmadd213sd {0}, {2}, {3}",
                                       xr(node.result_id), xr(node.op_a), xr(node.op_b), xr(node.op_c));
                }
                return std::format("vfmadd213sd {0}, {1}, {2}",
                                   xr(node.result_id), xr(node.op_b), xr(node.op_c));
            }

            case ir::OpCode::SIMD_COMPLEX_FMA: {
                // Conservative vectorized complex-FMA lowering: dst = c + a*b over packed f32.
                if (node.result_id != node.op_c) {
                    return std::format("vmovaps {0}, {1} \n    vfmadd213ps {0}, {2}, {3}",
                                       xr(node.result_id), xr(node.op_c), xr(node.op_a), xr(node.op_b));
                }
                return std::format("vfmadd231ps {0}, {1}, {2}",
                                   xr(node.result_id), xr(node.op_a), xr(node.op_b));
            }

            case ir::OpCode::DIV_I64:
            case ir::OpCode::DIV_I32: {
                if (node.result_id != ir::INVALID_SLOT && std::string_view(r(node.result_id)) != "rax") {
                    return std::format("mov rax, {} \n    cqo \n    idiv {} \n    mov {}, rax",
                                       r(node.op_a), r(node.op_b), r(node.result_id));
                }
                return std::format("mov rax, {} \n    cqo \n    idiv {}", r(node.op_a), r(node.op_b));
            }

            case ir::OpCode::MOD_I64:
            case ir::OpCode::MOD_I32: {
                return std::format("mov rax, {} \n    cqo \n    idiv {} \n    mov {}, rdx",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }

            case ir::OpCode::CMP_EQ_I32: {
                return std::format("cmp {}, {} \n    sete al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }
            case ir::OpCode::CMP_NE_I32: {
                return std::format("cmp {}, {} \n    setne al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }
            case ir::OpCode::CMP_LT_I32: {
                return std::format("cmp {}, {} \n    setl al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }
            case ir::OpCode::CMP_LE_I32: {
                return std::format("cmp {}, {} \n    setle al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }
            case ir::OpCode::CMP_GT_I32: {
                return std::format("cmp {}, {} \n    setg al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }
            case ir::OpCode::CMP_GE_I32: {
                return std::format("cmp {}, {} \n    setge al \n    movzx {}, al",
                                   r(node.op_a), r(node.op_b), r(node.result_id));
            }

            case ir::OpCode::AND_I64:
            case ir::OpCode::AND_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    and {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("and {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::OR_I64:
            case ir::OpCode::OR_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    or {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("or {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::XOR_I64:
            case ir::OpCode::XOR_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    xor {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("xor {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::SHL_I64:
            case ir::OpCode::SHL_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    mov rcx, {2} \n    shl {0}, cl",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("mov rcx, {} \n    shl {}, cl", r(node.op_b), r(node.result_id));
            }

            case ir::OpCode::SHR_I64:
            case ir::OpCode::SHR_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    mov rcx, {2} \n    shr {0}, cl",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("mov rcx, {} \n    shr {}, cl", r(node.op_b), r(node.result_id));
            }

            case ir::OpCode::LOAD: {
                return std::format("mov {}, [{} + {}]", r(node.result_id), r(node.op_a), node.op_c);
            }

            case ir::OpCode::STORE: {
                return std::format("mov [{} + {}], {}", r(node.op_a), node.op_c, r(node.op_b));
            }

            case ir::OpCode::REG_MOVE: {
                return std::format("mov {}, {}", r(node.result_id), r(node.op_a));
            }

            case ir::OpCode::RET: {
                if (node.op_a != ir::INVALID_SLOT) {
                    uint64_t imm = 0;
                    if (resolve_ret_immediate(stream, node.op_a, imm)) {
                        return std::format("mov rax, {} \n    jmp .L_epilogue", imm);
                    }
                    return std::format("mov rax, {} \n    jmp .L_epilogue", r(node.op_a));
                }
                return "jmp .L_epilogue";
            }

            case ir::OpCode::GATHER_F32_512: {
                return std::format("vmovups zmm{}, [{} + {}]", 
                                   node.result_id % 32, r(node.op_a), node.op_c);
            }

            case ir::OpCode::BRANCH:
            case ir::OpCode::GOTO:
                return std::format("jmp .L{}", resolve_target(node.op_a));

            case ir::OpCode::BRANCH_COND:
                // op_a = condition register, op_b = target block label
                return std::format("test {0}, {0} \n    jnz .L{1}",
                                   r(node.op_a), resolve_target(node.op_b));

            case ir::OpCode::BRANCH_LIKELY:
            case ir::OpCode::BRANCH_UNLIKELY:
                // Keep textual emitter semantically aligned with branch-cond lowering.
                return std::format("test {0}, {0} \n    jnz .L{1}",
                                   r(node.op_a), resolve_target(node.op_b));

            case ir::OpCode::BRANCH_COMPARE_FUSED:
                // op_a = lhs register, op_b = rhs register, op_c = target block label
                return std::format("cmp {}, {} \n    jne .L{}", r(node.op_a), r(node.op_b), resolve_target(node.op_c));

            case ir::OpCode::PHI_NODE:
                return std::format("mov {0}, {1} \n    test {2}, {2} \n    cmovne {0}, {3}",
                                   r(node.result_id), r(node.op_a), r(node.op_c), r(node.op_b));

            case ir::OpCode::BOOL_SELECT:
            case ir::OpCode::SELECT:
                return std::format("mov {0}, {2} \n    test {1}, {1} \n    cmovne {0}, {3}",
                                   r(node.result_id), r(node.op_a), r(node.op_c), r(node.op_b));

            case ir::OpCode::GEP:
                return std::format("lea {0}, [{1} + {2}]",
                                   r(node.result_id), r(node.op_a), r(node.op_b));

            case ir::OpCode::PTR_TO_INT:
            case ir::OpCode::INT_TO_PTR:
                return std::format("mov {0}, {1}", r(node.result_id), r(node.op_a));

            case ir::OpCode::GUARD_VALUE:
                return std::format("mov {0}, {3} \n    cmp {1}, {2} \n    cmove {0}, {1}",
                                   r(node.result_id), r(node.op_a), r(node.op_b), r(node.op_c));

            case ir::OpCode::GUARD_ALIAS:
                return std::format("mov {0}, 1 \n    cmp {1}, {2} \n    cmovne {0}, {3}",
                                   r(node.result_id), r(node.op_a), r(node.op_b), r(node.op_c));

            case ir::OpCode::PREFETCH:
            case ir::OpCode::PREFETCH_T0:
                return std::format("prefetcht0 [{}]", r(node.op_a));

            case ir::OpCode::CLFLUSH:
                return std::format("clflush [{}]", r(node.op_a));

            case ir::OpCode::READ_TSC:
                return std::format("rdtsc \n    shl rdx, 32 \n    or rax, rdx \n    mov {0}, rax", r(node.result_id));

            case ir::OpCode::PTR_DIFF:
                return std::format("mov {0}, {1} \n    sub {0}, {2}",
                                   r(node.result_id), r(node.op_a), r(node.op_b));

            case ir::OpCode::LEA: {
                const uint32_t scale = node.op_c;
                if (scale == 1u || scale == 2u || scale == 4u || scale == 8u) {
                    return std::format("lea {0}, [{1} + {2}*{3}]",
                                       r(node.result_id), r(node.op_a), r(node.op_b), scale);
                }
                if (const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode); symbol != nullptr) {
                    return emit_runtime_call_text(symbol);
                }
                return std::format("nop ; Unsupported Op: 0x{:X}", static_cast<int>(node.opcode));
            }

            case ir::OpCode::ACQUIRE_FENCE:
                return "lfence";

            case ir::OpCode::RELEASE_FENCE:
                return "sfence";

            case ir::OpCode::ATOMIC_FENCE:
            case ir::OpCode::MEMORY_FENCE:
            case ir::OpCode::BARRIER_SYNC:
            case ir::OpCode::SYNC_BARRIER:
                return "mfence";

            case ir::OpCode::CALL: {
                std::vector<uint32_t> args;
                args.reserve(8);
                if (node.op_b != ir::INVALID_SLOT) args.push_back(node.op_b);
                if (node.op_c != ir::INVALID_SLOT) args.push_back(node.op_c);
                if (node.logic_flags != ir::INVALID_SLOT) args.push_back(node.logic_flags);
                const auto extra_args = stream.get_call_overflow_args(node_idx);
                std::copy_if(extra_args.begin(), extra_args.end(), std::back_inserter(args),
                             [](uint32_t extra) { return extra != ir::INVALID_SLOT; });

                auto map_reg_name = [](uint32_t reg_id) -> const char* {
                    switch (reg_id) {
                        case 0u: return "rdi";
                        case 1u: return "rsi";
                        case 2u: return "rdx";
                        case 3u: return "rcx";
                        case 4u: return "r8";
                        case 5u: return "r9";
                        // Microsoft x64 canonical integer arg order IDs from ABI_Lowering.
                        case 6u: return "rax";
                        case 7u: return "rbx";
                        case 8u: return "rsp";
                        case 9u: return "rbp";
                        case 10u: return "rsi";
                        case 11u: return "rdi";
                        case 12u: return "r8";
                        case 13u: return "r9";
                        case 14u: return "r10";
                        case 15u: return "r11";
                        default: return nullptr;
                    }
                };

                auto map_xmm_index = [](uint32_t reg_id) -> int {
                    if (reg_id >= 16u && reg_id <= 31u) {
                        return static_cast<int>(reg_id - 16u);
                    }
                    return -1;
                };

                auto is_ms_integer_arg_id = [](uint32_t reg_id) -> bool {
                    return reg_id == 2u || reg_id == 3u || reg_id == 4u || reg_id == 5u;
                };

                auto map_ms_arg_reg_name = [](uint32_t reg_id) -> const char* {
                    switch (reg_id) {
                        case 3u: return "rcx";
                        case 0u: return "rdi";
                        case 2u: return "rdx";
                        case 4u: return "r8";
                        case 5u: return "r9";
                        default: return nullptr;
                    }
                };

                const std::vector<uint32_t> layout_words = stream.get_call_abi_layout_words(node_idx);
                std::string cs;
                if (layout_words.size() >= 4) {
                    const uint32_t param_count = layout_words[0];
                    const uint32_t stack_size = layout_words[1];
                    const size_t required_words = 4u + static_cast<size_t>(param_count) * 4u;

                    if (layout_words.size() >= required_words && stack_size > 0u) {
                        cs += std::format("sub rsp, {}", stack_size);
                    }

                    if (layout_words.size() >= required_words) {
                        size_t w = 4u;
                        const size_t count = std::min<size_t>(param_count, args.size());
                        for (size_t i = 0; i < count; ++i, w += 4u) {
                            const bool in_register = (layout_words[w] != 0u);
                            const uint32_t reg_id = layout_words[w + 1u];
                            const uint32_t stack_offset = layout_words[w + 2u];

                            if (in_register) {
                                const int xmm_idx = map_xmm_index(reg_id);
                                if (xmm_idx >= 0) {
                                    if (!cs.empty()) cs += " \n    ";
                                    cs += std::format("movq xmm{}, {}", xmm_idx, r(args[i]));
                                } else {
                                    const char* dst = is_ms_integer_arg_id(reg_id)
                                        ? map_ms_arg_reg_name(reg_id)
                                        : map_reg_name(reg_id);
                                    if (dst != nullptr) {
                                        if (!cs.empty()) cs += " \n    ";
                                        cs += std::format("mov {}, {}", dst, r(args[i]));
                                    }
                                }
                            } else {
                                if (!cs.empty()) cs += " \n    ";
                                cs += std::format("mov rax, {}", r(args[i]));
                                cs += std::format(" \n    mov [rsp + {}], rax", stack_offset);
                            }
                        }
                    }

                    if (!cs.empty()) cs += " \n    ";
                    cs += std::format("call _DGTL_fn_{}", node.op_a);
                    if (layout_words.size() >= required_words && stack_size > 0u) {
                        cs += std::format(" \n    add rsp, {}", stack_size);
                    }
                } else {
                    cs = "sub rsp, 32";
                    if (node.op_b != ir::INVALID_SLOT)
                        cs += std::format(" \n    mov rcx, {}", r(node.op_b));
                    if (node.op_c != ir::INVALID_SLOT)
                        cs += std::format(" \n    mov rdx, {}", r(node.op_c));
                    cs += std::format(" \n    call _DGTL_fn_{}", node.op_a);
                    cs += " \n    add rsp, 32";
                }
                return cs;
            }

            case ir::OpCode::SYSCALL:
                return "syscall";

            default:
                if (const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode); symbol != nullptr) {
                    return emit_runtime_call_text(symbol);
                }
                // Stable fallback to keep output buildable.
                return std::format("nop ; Unsupported Op: 0x{:X}", static_cast<int>(node.opcode));
        }
    }
};

} // namespace dgtl::isa

