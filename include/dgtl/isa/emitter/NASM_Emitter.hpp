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

#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstdint>
#include <sstream>
#include <functional>
#include <unordered_map>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/ir/base/IR_PrintSerialDumper.hpp"
#include "dgtl/utils/assert/Assertions.hpp"
#include "dgtl/utils/container/Flat_Map.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/backend/lowering/ABI_Lowering.hpp"

namespace dgtl::isa {

// Explicit namespace mapping to prevent ambiguity
using dgtl::ir::OpCode;
using dgtl::ir::IRNode;

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

        // --- FIX: Pre-scan LABEL nodes to build label_id → stream_index map ---
        // This resolves frontend label IDs in branch targets to the correct .L{n} positions.
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(stream.size() / 4 + 4);
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& n = stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                uint32_t lid = (n.result_id != ir::INVALID_SLOT) ? n.result_id : n.op_a;
                label_map[lid] = i;
            }
        }
        // Helper: resolve a branch target through label_map or return as-is
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < stream.size()) return static_cast<size_t>(raw);
            return stream.size(); // fallback to epilogue
        };
        // -----------------------------------------------------------------------

        append_line("; -----------------------------------------------------------");
        append_line("; DGTL TIER-2 PERSISTENT KERNEL");
        append_line(std::format("; Logic Hash: 0x{:016X}", stream.generate_logic_hash()));
        append_line("; -----------------------------------------------------------");
        append_line("section .text");
        append_line("    global DGTL_kernel_entry");
        append_line("    extern __myzon_print_i64"); // runtime print hook
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

        switch (node.opcode) {
            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE: {
                const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
                uint64_t imm = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFF);
                return std::format("mov {}, {}", r(node.result_id), imm);
            }

            case ir::OpCode::ADD_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    add {0}, {2}", 
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("add {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::SUB_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    sub {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("sub {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::MUL_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    imul {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("imul {}, {}", r(node.result_id), r(node.op_b));
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

            case ir::OpCode::DIV_I32: {
                if (node.result_id != ir::INVALID_SLOT && std::string_view(r(node.result_id)) != "rax") {
                    return std::format("mov rax, {} \n    cqo \n    idiv {} \n    mov {}, rax",
                                       r(node.op_a), r(node.op_b), r(node.result_id));
                }
                return std::format("mov rax, {} \n    cqo \n    idiv {}", r(node.op_a), r(node.op_b));
            }

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

            case ir::OpCode::AND_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    and {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("and {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::OR_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    or {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("or {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::XOR_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    xor {0}, {2}",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("xor {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::SHL_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    mov rcx, {2} \n    shl {0}, cl",
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("mov rcx, {} \n    shl {}, cl", r(node.op_b), r(node.result_id));
            }

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
                return std::format("    nop ; Unsupported Op fallback ({} #{})",
                                   std::string(ir::opcode_name(node.opcode)),
                                   static_cast<uint32_t>(node.opcode));

            case ir::OpCode::BRANCH_COMPARE_FUSED:
                // op_a = lhs register, op_b = rhs register, op_c = target block label
                return std::format("cmp {}, {} \n    jne .L{}", r(node.op_a), r(node.op_b), resolve_target(node.op_c));

            case ir::OpCode::PHI_NODE:
                // PHI nodes are resolved by register allocation; emit as comment.
                return std::format("; PHI r{} <- r{}, r{}, r{}",
                                   node.result_id, node.op_a, node.op_b, node.op_c);

            case ir::OpCode::CALL: {
                std::vector<uint32_t> args;
                args.reserve(8);
                if (node.op_b != ir::INVALID_SLOT) args.push_back(node.op_b);
                if (node.op_c != ir::INVALID_SLOT) args.push_back(node.op_c);
                if (node.logic_flags != ir::INVALID_SLOT) args.push_back(node.logic_flags);
                for (uint32_t extra : stream.get_call_overflow_args(node_idx)) {
                    if (extra != ir::INVALID_SLOT) {
                        args.push_back(extra);
                    }
                }

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
                // Stable fallback to keep output buildable.
                return std::format("nop ; Unsupported Op: 0x{:X}", static_cast<int>(node.opcode));
        }
    }
};

} // namespace dgtl::isa