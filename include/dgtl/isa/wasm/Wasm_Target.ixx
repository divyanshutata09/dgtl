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


export module dgtl.ir_core.wasm_target;

import dgtl.target;
import dgtl.ir_core.abi_lowering;
import dgtl.ir_core;
import dgtl.assertions;
import dgtl.flat_map;
import dgtl.ir_core.runtime_contract;

export namespace dgtl::target {

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
     * @brief Translates IRStream to NASM Assembly String with Invariant Guard.
     * PILLAR 3: Validates bit-packed constraints before generating text.
     */
    static void to_nasm(const ir::IRStream& stream, std::string& out) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        // --- PILLAR 3: THE SENTINEL PRE-CHECK ---
        // Ensures IR hasn't been corrupted by previous optimization optes.
        verify_DGTL_invariants(stream);

        out.clear();
        out.reserve(stream.size() * 80); 
        out += "; DGTL (Deep General-purpose Transformation Layer) version 1.0.0 Generated Assembly [Validated]\n";
        out += "section .text\n    global _start\n    extern __dgtl_rt_print_i64\n_start:\n";

        const uint32_t count = static_cast<uint32_t>(stream.size());

        // --- Label pre-scan: build label_id -> stream index map ---
        std::unordered_map<uint32_t, uint32_t> label_map;
        label_map.reserve(count / 4 + 4);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LABEL) {
                if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                    label_map[node.result_id] = i;
                }
                if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                    label_map[node.op_a] = i;
                }
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                    label_map[node.op_b] = i;
                }
                label_map[i] = i;
            }
        }
        auto resolve_target = [&](uint32_t raw) -> uint32_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            return count - 1;
        };

        for (uint32_t i = 0; i < count; ++i) {
            const auto& n = stream.get_node(i);
            const uint32_t label_id = (n.result_id == ir::INVALID_SLOT) ? i : n.result_id;
            out += "L_" + std::to_string(label_id) + ":\n";

            switch (n.opcode) {
                case ir::OpCode::LABEL:
                    // Pseudo-op: position label was already emitted above; no code.
                    break;
                case ir::OpCode::PHI_NODE:
                    out += "    ; PHI r" + std::to_string(n.result_id) + "\n";
                    break;
                case ir::OpCode::LOAD_IMM:
                case ir::OpCode::IMMEDIATE:
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", " + std::to_string(n.op_b) + "\n";
                    break;
                case ir::OpCode::REG_MOVE:
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    break;
                case ir::OpCode::ADD_I32:
                      out += "    mov r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_a % 16) + "\n";
                      out += "    add r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::SUB_I32:
                      out += "    mov r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_a % 16) + "\n";
                      out += "    sub r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::MUL_I32:
                      out += "    mov r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_a % 16) + "\n";
                      out += "    imul r" + std::to_string(n.result_id % 16) +
                          ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::DIV_I32:
                    out += "    mov rax, r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    cqo\n";
                    out += "    idiv r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    mov r" + std::to_string(n.result_id % 16) + ", rax\n";
                    break;
                case ir::OpCode::MOD_I32:
                    out += "    mov rax, r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    cqo\n";
                    out += "    idiv r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    mov r" + std::to_string(n.result_id % 16) + ", rdx\n";
                    break;
                case ir::OpCode::AND_I32:
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    and r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::OR_I32:
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    or r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::XOR_I32:
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    xor r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::SHL_I32:
                    out += "    mov rcx, r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    shl r" + std::to_string(n.result_id % 16) + ", cl\n";
                    break;
                case ir::OpCode::SHR_I32:
                    out += "    mov rcx, r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    mov r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    shr r" + std::to_string(n.result_id % 16) + ", cl\n";
                    break;
                case ir::OpCode::CMP_EQ_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    sete al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::CMP_NE_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    setne al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::CMP_LT_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    setl al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::CMP_LE_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    setle al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::CMP_GT_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    setg al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::CMP_GE_I32:
                    out += "    cmp r" + std::to_string(n.op_a % 16) + ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    setge al\n    movzx r" + std::to_string(n.result_id % 16) + ", al\n";
                    break;
                case ir::OpCode::BRANCH:
                case ir::OpCode::GOTO:
                    out += "    jmp L_" + std::to_string(resolve_target(n.op_a)) + "\n";
                    break;
                case ir::OpCode::BRANCH_COND:
                  case ir::OpCode::BRANCH_LIKELY:
                  case ir::OpCode::BRANCH_UNLIKELY:
                    out += "    test r" + std::to_string(n.op_a % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    out += "    jnz L_" + std::to_string(resolve_target(n.op_b)) + "\n";
                    break;
                case ir::OpCode::BRANCH_COMPARE_FUSED:
                    out += "    cmp r" + std::to_string(n.op_a % 16) +
                           ", r" + std::to_string(n.op_b % 16) + "\n";
                    out += "    jne L_" + std::to_string(resolve_target(n.op_c)) + "\n";
                    break;
                case ir::OpCode::CALL:
                    {
                        std::vector<uint32_t> args;
                        args.reserve(8);
                        if (n.op_b != ir::INVALID_SLOT) args.push_back(n.op_b);
                        if (n.op_c != ir::INVALID_SLOT) args.push_back(n.op_c);
                        if (n.logic_flags != ir::INVALID_SLOT) args.push_back(n.logic_flags);
                        const auto overflow = stream.get_call_overflow_args(i);
                        std::copy_if(overflow.begin(), overflow.end(), std::back_inserter(args), [](uint32_t extra) noexcept {
                            return extra != ir::INVALID_SLOT;
                        });

                        auto map_reg_name = [](uint32_t reg_id) -> const char* {
                            switch (reg_id) {
                                case 0u: return "rdi";
                                case 1u: return "rsi";
                                case 2u: return "rdx";
                                case 3u: return "rcx";
                                case 4u: return "r8";
                                case 5u: return "r9";
                                default: return nullptr;
                            }
                        };

                        const std::vector<uint32_t> layout_words = stream.get_call_abi_layout_words(i);
                        if (layout_words.size() >= 4) {
                            const uint32_t param_count = layout_words[0];
                            const uint32_t stack_size = layout_words[1];
                            const size_t required_words = 4u + static_cast<size_t>(param_count) * 4u;
                            if (layout_words.size() >= required_words && stack_size > 0u) {
                                out += "    sub rsp, " + std::to_string(stack_size) + "\n";
                            }

                            if (layout_words.size() >= required_words) {
                                size_t w = 4u;
                                const size_t arg_count = std::min<size_t>(param_count, args.size());
                                for (size_t ai = 0; ai < arg_count; ++ai, w += 4u) {
                                    const bool in_register = (layout_words[w] != 0u);
                                    const uint32_t reg_id = layout_words[w + 1u];
                                    const uint32_t stack_offset = layout_words[w + 2u];

                                    if (in_register) {
                                        if (const char* dst = map_reg_name(reg_id); dst != nullptr) {
                                            out += "    mov " + std::string(dst) + ", r" + std::to_string(args[ai] % 16u) + "\n";
                                        }
                                    } else {
                                        out += "    mov rax, r" + std::to_string(args[ai] % 16u) + "\n";
                                        out += "    mov [rsp + " + std::to_string(stack_offset) + "], rax\n";
                                    }
                                }
                            }

                            out += "    call _DGTL_fn_" + std::to_string(n.op_a) + "\n";
                            if (layout_words.size() >= required_words && stack_size > 0u) {
                                out += "    add rsp, " + std::to_string(stack_size) + "\n";
                            }
                        } else {
                            out += "    sub rsp, 32\n";
                            if (n.op_b != ir::INVALID_SLOT)
                                out += "    mov rcx, r" + std::to_string(n.op_b % 16) + "\n";
                            if (n.op_c != ir::INVALID_SLOT)
                                out += "    mov rdx, r" + std::to_string(n.op_c % 16) + "\n";
                            out += "    call _DGTL_fn_" + std::to_string(n.op_a) + "\n";
                            out += "    add rsp, 32\n";
                        }
                    }
                    break;
                case ir::OpCode::SYSCALL:
                    out += "    syscall\n";
                    break;
                case ir::OpCode::LOAD:
                    out += "    mov r" + std::to_string(n.result_id % 16) + 
                           ", [rdi + " + std::to_string(n.op_a) + "]\n";
                    break;
                case ir::OpCode::STORE:
                    out += "    mov [rdi + " + std::to_string(n.op_a) + "], r" +
                           std::to_string(n.op_b % 16) + "\n";
                    break;
                case ir::OpCode::RET:
                    out += "    ret\n";
                    break;
                case ir::OpCode::NOP:
                    out += "    nop\n";
                    break;
                default:
                    if (const char* rt = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(n.opcode);
                        rt != nullptr) {
                        out += "    sub rsp, 32\n";
                        out += "    mov rcx, r" + std::to_string(n.op_a % 16) + "\n";
                        out += "    mov rdx, r" + std::to_string(n.op_b % 16) + "\n";
                        out += "    mov r8, r" + std::to_string(n.op_c % 16) + "\n";
                        out += "    call ";
                        out += rt;
                        out += "\n";
                        if (n.result_id != ir::INVALID_SLOT) {
                            out += "    mov r" + std::to_string(n.result_id % 16) + ", rax\n";
                        }
                        out += "    add rsp, 32\n";
                        break;
                    }
                    // Stable fallback to preserve assembly validity.
                    out += "    nop ; Unsupported Op fallback (";
                    out += std::string(ir::opcode_name(n.opcode));
                    out += " #";
                    out += std::to_string(static_cast<uint32_t>(n.opcode));
                    out += ")\n";
                    break;
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
     * @brief Scans for Bit-Level Logic Holes.
     * Prevents "Dangling IDs" or malformed bit-packed structures from binary emission.
     */
    static void verify_DGTL_invariants(const ir::IRStream& stream) {
        const uint32_t count = static_cast<uint32_t>(stream.size());
        for (uint32_t i = 0; i < count; ++i) {
            const auto& n = stream.get_node(i);

            auto requires_result = [](ir::OpCode opcode) {
                switch (opcode) {
                    case ir::OpCode::LOAD_IMM:
                    case ir::OpCode::IMMEDIATE:
                    case ir::OpCode::REG_MOVE:
                    case ir::OpCode::ADD_I32:
                    case ir::OpCode::SUB_I32:
                    case ir::OpCode::MUL_I32:
                    case ir::OpCode::DIV_I32:
                    case ir::OpCode::MOD_I32:
                    case ir::OpCode::CMP_EQ_I32:
                    case ir::OpCode::CMP_NE_I32:
                    case ir::OpCode::CMP_LT_I32:
                    case ir::OpCode::CMP_LE_I32:
                    case ir::OpCode::CMP_GT_I32:
                    case ir::OpCode::CMP_GE_I32:
                    case ir::OpCode::AND_I32:
                    case ir::OpCode::OR_I32:
                    case ir::OpCode::XOR_I32:
                    case ir::OpCode::SHL_I32:
                    case ir::OpCode::SHR_I32:
                    case ir::OpCode::LOAD:
                    case ir::OpCode::CALL:
                        return true;
                    default:
                        return false;
                }
            };

            if (requires_result(n.opcode) && n.result_id == ir::INVALID_SLOT) [[unlikely]] {
                throw std::runtime_error("WASM target invariant violation: missing result_id for value-producing opcode");
            }
            
            // Check for op_a/op_b out of bounds (exempt branch targets Ã¢â‚¬â€ these hold stream indices).
            bool is_branch = (n.opcode == ir::OpCode::BRANCH ||
                              n.opcode == ir::OpCode::GOTO ||
                              n.opcode == ir::OpCode::BRANCH_COND ||
                              n.opcode == ir::OpCode::BRANCH_COMPARE_FUSED);
            if (!is_branch && (n.op_a > 16777215 || n.op_b > 16777215)) [[unlikely]] {
                throw std::runtime_error("WASM target invariant violation: operand index out of range");
            }
        }
    }
};

} // namespace dgtl::target

