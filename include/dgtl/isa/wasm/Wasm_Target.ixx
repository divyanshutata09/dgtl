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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : UNIVERSAL TARGET EXPANSION
 * STEP 17.5: CROSS-PLATFORM MACH-O & WASM STUBS (V5)
 * PILLAR 3 INTEGRATION: EMITTER-LEVEL SENTINEL
 * DESIGN: Validated IR-to-Binary mapping with Bit-Level Invariant Checks.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
export module dgtl.isa.wasm.target;
import dgtl.mir.machine_ir;
import dgtl.mir.abi_lowering;
import dgtl.mir.runtime_contract;
export namespace dgtl::isa::wasm {

// --- MACH-O CROSS-PLATFORM STUBS (For Windows/Linux/DGTL-OS Hosts) ---
// These structures allow non-macOS hosts to weave Mach-O 64-bit binaries.
#if !defined(__APPLE__)
struct mach_header_64 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct segment_command_64 {
    [[maybe_unused]] uint32_t cmd;
    [[maybe_unused]] uint32_t cmdsize;
    [[maybe_unused]] char     segname[16];
    [[maybe_unused]] uint64_t vmaddr;
    [[maybe_unused]] uint64_t vmsize;
    [[maybe_unused]] uint64_t fileoff;
    [[maybe_unused]] uint64_t filesize;
    [[maybe_unused]] int32_t  maxprot;
    [[maybe_unused]] int32_t  initprot;
    [[maybe_unused]] uint32_t nsects;
    [[maybe_unused]] uint32_t flags;
};

struct entry_point_command {
    [[maybe_unused]] uint32_t cmd;
    [[maybe_unused]] uint32_t cmdsize;
    [[maybe_unused]] uint64_t entryoff;
    [[maybe_unused]] uint64_t stacksize;
};

#define MH_MAGIC_64           0xfeedfacf
#define MH_EXECUTE            0x2
#define LC_SEGMENT_64         0x19
#define LC_MAIN               0x80000028
#define CPU_TYPE_ARM64        (0x01000000 | 12)
#define CPU_SUBTYPE_ARM64_ALL 0x0
#define MH_NOUNDEFS           0x1
#define MH_DYLDLINK           0x4
#define MH_TWOLEVEL           0x80
#define MH_PIE                0x200000
#endif

/**
 * @struct WasmHeader
 * @brief Canonical WebAssembly binary preamble.
 */
struct WasmHeader {
    uint32_t magic = 0x6d736100; // "\0asm" in little-endian
    uint32_t version = 0x1;
};

// --- DGTL ASM EMITTER (With Sentinel Validation) ---
class DGTLAsmEmitter {
public:
    /**
     * @brief Translates MachineFunction to NASM Assembly String with Invariant Guard.
     */
    static void to_nasm(const dgtl::machine::MachineFunction& function, std::string& out) {
        verify_DGTL_invariants(function);

        out.clear();
        out += "; DGTL (Deep General-purpose Transformation Layer) MIR Generated Assembly [Validated]\n";
        out += "section .text\n    global _start\n    extern __dgtl_rt_print_i64\n_start:\n";

        for (const auto& block : function.blocks()) {
            out += ".Lbb" + std::to_string(block.id) + ":\n";

            for (dgtl::machine::InstrId instruction_id : block.instructions) {
                const auto* instruction = function.instruction(instruction_id);
                if (!instruction) continue;

                switch (instruction->form) {
                    case dgtl::machine::MachineForm::NoOp:
                        out += "    nop\n";
                        break;
                    case dgtl::machine::MachineForm::Move:
                        if (instruction->operands.size() >= 2 &&
                            instruction->operands[0].is_register() &&
                            instruction->operands[1].is_register()) {
                            out += "    mov r" + std::to_string(instruction->operands[0].reg.id % 16) +
                                   ", r" + std::to_string(instruction->operands[1].reg.id % 16) + "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::LoadImmediate:
                        if (!instruction->operands.empty() && instruction->operands[0].is_register()) {
                            int64_t imm = instruction->operands.size() > 1 && instruction->operands[1].kind == dgtl::machine::MachineOperandKind::Immediate
                                ? instruction->operands[1].immediate : 0;
                            out += "    mov r" + std::to_string(instruction->operands[0].reg.id % 16) +
                                   ", " + std::to_string(imm) + "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::IntegerBinary32:
                    case dgtl::machine::MachineForm::IntegerBinary64: {
                        if (instruction->operands.size() >= 3 && instruction->operands[0].is_register()) {
                            const char* op_str = "add";
                            if (instruction->semantic_kind == dgtl::machine::MachineSemanticKind::IntegerSub) op_str = "sub";
                            else if (instruction->semantic_kind == dgtl::machine::MachineSemanticKind::IntegerMul) op_str = "imul";
                            else if (instruction->semantic_kind == dgtl::machine::MachineSemanticKind::IntegerAnd) op_str = "and";
                            else if (instruction->semantic_kind == dgtl::machine::MachineSemanticKind::IntegerOr) op_str = "or";
                            else if (instruction->semantic_kind == dgtl::machine::MachineSemanticKind::IntegerXor) op_str = "xor";

                            uint32_t dst = instruction->operands[0].reg.id % 16;
                            std::string src = instruction->operands[2].is_register()
                                ? "r" + std::to_string(instruction->operands[2].reg.id % 16)
                                : std::to_string(instruction->operands[2].immediate);
                            out += "    " + std::string(op_str) + " r" + std::to_string(dst) + ", " + src + "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::Goto:
                        if (!instruction->operands.empty() && instruction->operands[0].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            out += "    jmp .Lbb" + std::to_string(instruction->operands[0].block) + "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCond:
                        if (instruction->operands.size() >= 2 && instruction->operands[1].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            if (instruction->operands[0].is_register()) {
                                out += "    test r" + std::to_string(instruction->operands[0].reg.id % 16) +
                                       ", r" + std::to_string(instruction->operands[0].reg.id % 16) + "\n";
                                out += "    jnz .Lbb" + std::to_string(instruction->operands[1].block) + "\n";
                            }
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCompare:
                        if (instruction->operands.size() >= 3 && instruction->operands[2].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            std::string lhs = instruction->operands[0].is_register() ? "r" + std::to_string(instruction->operands[0].reg.id % 16) : "0";
                            std::string rhs = instruction->operands[1].is_register() ? "r" + std::to_string(instruction->operands[1].reg.id % 16) : "0";
                            out += "    cmp " + lhs + ", " + rhs + "\n";
                            out += "    jne .Lbb" + std::to_string(instruction->operands[2].block) + "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::LoadStack:
                        if (instruction->operands.size() >= 2 && instruction->operands[0].is_register()) {
                            std::string base = instruction->operands[1].is_register() ? "r" + std::to_string(instruction->operands[1].reg.id % 16) : "rsp";
                            out += "    mov r" + std::to_string(instruction->operands[0].reg.id % 16) + ", [" + base + "]\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::StoreStack:
                        if (instruction->operands.size() >= 2 && instruction->operands[0].is_register()) {
                            std::string base = instruction->operands[1].is_register() ? "r" + std::to_string(instruction->operands[1].reg.id % 16) : "rsp";
                            out += "    mov [" + base + "], r" + std::to_string(instruction->operands[0].reg.id % 16) + "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::Call:
                        if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(instruction->opcode)) {
                            out += "    call " + std::string(sym) + "\n";
                        } else {
                            out += "    call unknown_target\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::Syscall:
                        out += "    syscall\n";
                        break;
                    case dgtl::machine::MachineForm::Return:
                        out += "    ret\n";
                        break;
                    default:
                        break;
                }
            }
        }
    }




    /**
     * @brief High-speed Mach-O binary construction for ARM64.
     */
    static std::vector<uint8_t> emit_macho_binary(const std::vector<uint32_t>& machine_code) {
        std::vector<uint8_t> bin;
        mach_header_64 head{};
        
        head.magic = MH_MAGIC_64;
        head.cputype = CPU_TYPE_ARM64;
        head.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
        head.filetype = MH_EXECUTE;
        head.ncmds = 0; 
        head.sizeofcmds = 0;
        head.flags = MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL | MH_PIE;
        head.reserved = 0;
        
        bin.resize(sizeof(mach_header_64));
        std::memcpy(bin.data(), &head, sizeof(head));
        
        // Append raw machine code (simplified emission for stub validation)
        size_t code_bytes = machine_code.size() * sizeof(uint32_t);
        size_t prev_size = bin.size();
        bin.resize(prev_size + code_bytes);
        std::memcpy(bin.data() + prev_size, machine_code.data(), code_bytes);
        
        return bin;
    }

private:
    /**
     * @brief Verifies MachineFunction invariants at target boundary.
     */
    static void verify_DGTL_invariants(const dgtl::machine::MachineFunction& function) {
        const auto verify = dgtl::machine::MachineVerifier::verify(function);
        if (!verify.ok()) {
            throw std::runtime_error("DGTL Invariant Error: Machine IR corruption detected at target boundary");
        }
    }

};

} // namespace dgtl::isa::wasm::wasm
