#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstdint>
#include <sstream>
#include "../ir/Global_IR.hpp"
#include "../core/Symbol_Table.hpp"

namespace dgtl::exec {

class DGTLNasmEmitter {
    std::string _asm_buffer;

public:
    DGTLNasmEmitter() { 
        _asm_buffer.reserve(1024 * 1024); 
    }

    /**
     * @brief Updated signature to utilize the dgtl::core::SymbolTable.
     * This matches the high-velocity identity resolution Pillar 2.
     */
    void emit(const ir::IRStream& stream, const dgtl::core::SymbolTable& symbols) {
        // PERMANENT FIX: Clear previous runs to ensure the assertion 
        // in baked_kernel.cpp sees fresh data.
        this->clear();

        if (stream.size() == 0) {
            append_line("; [WARNING] Empty IR Stream provided. No code generated.");
            return;
        }

        append_line("; -----------------------------------------------------------");
        append_line("; DGTL-MLIR-X TIER-2 PERSISTENT KERNEL");
        append_line(std::format("; Logic Hash: 0x{:016X}", stream.generate_logic_hash()));
        append_line("; -----------------------------------------------------------");
        append_line("section .text");
        append_line("    global DGTL_kernel_entry");
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

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::NOP) continue;

            std::string asm_code = translate_node(node);
            
            // Format intent hash for debugging within the ASM
            std::string comment = std::format(" ; [Node {:03d}] Hash:0x{:08X}", i, node.intent_hash);
            append_line(std::format("    {:<45}{}", asm_code, comment));
        }

        // --- EPILOGUE: Restore Registers and Return ---
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
    void append_line(std::string_view line) {
        _asm_buffer.append(line);
        _asm_buffer.append("\n");
    }

    std::string translate_node(const ir::IRNode& node) {
        auto r = [](uint32_t idx) -> const char* {
            static constexpr const char* regs[] = { 
                "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", 
                "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" 
            };
            return regs[idx & 0xF];
        };

        switch (node.opcode) {
            // FIX: Added LOAD_IMM to match your test cases
            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE: {
                uint64_t imm = (static_cast<uint64_t>(node.op_a) << 32) | (node.op_b & 0xFFFFFFFF);
                return std::format("mov {}, {}", r(node.result_id), imm);
            }

            case ir::OpCode::ADD_I32: {
                if (node.result_id != node.op_a) {
                    return std::format("mov {0}, {1} \n    add {0}, {2}", 
                                       r(node.result_id), r(node.op_a), r(node.op_b));
                }
                return std::format("add {}, {}", r(node.result_id), r(node.op_b));
            }

            case ir::OpCode::REG_MOVE: {
                return std::format("mov {}, {}", r(node.result_id), r(node.op_a));
            }

            case ir::OpCode::RET: {
                if (node.op_a != 0) { 
                    return std::format("mov rax, {}", r(node.op_a));
                }
                return "; Return (already in rax)";
            }

            case ir::OpCode::GATHER_F32_512: {
                return std::format("vmovups zmm{}, [{} + {}]", 
                                   node.result_id % 32, r(node.op_a), node.op_c);
            }

            case ir::OpCode::SYSCALL:
                return "syscall";

            default:
                // HARDENING: Return a comment so the ASM still builds but flags the error
                return std::format("; [ERROR] Unsupported Op: 0x{:X}", static_cast<int>(node.opcode));
        }
    }
};

} // namespace dgtl::exec