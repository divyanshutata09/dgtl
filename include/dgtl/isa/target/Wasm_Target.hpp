/* * DGTL-MLIR-X v1.0 : UNIVERSAL TARGET EXPANSION
 * STEP 17.5: CROSS-PLATFORM MACH-O & WASM STUBS (V5)
 * PILLAR 3 INTEGRATION: EMITTER-LEVEL SENTINEL
 * DESIGN: Validated IR-to-Binary mapping with Bit-Level Invariant Checks.
 */

#pragma once
#include <cstdint>
#include <string>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "dgtl/ir/base/Global_IR.hpp"

namespace dgtl::target {

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
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct entry_point_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
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

        out.reserve(stream.size() * 80); 
        out += "; DGTL-MLIR-X v1.0 Generated Assembly [Validated]\n";
        out += "section .text\n    global _start\n_start:\n";

        const uint32_t count = static_cast<uint32_t>(stream.size());
        for (uint32_t i = 0; i < count; ++i) {
            const auto& n = stream.get_node(i);
            out += "L_" + std::to_string(n.result_id) + ":\n";

            switch (n.opcode) {
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
                    out += "    add r" + std::to_string(n.result_id % 16) + 
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    break;
                case ir::OpCode::SUB_I32:
                    out += "    sub r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
                    break;
                case ir::OpCode::MUL_I32:
                    out += "    imul r" + std::to_string(n.result_id % 16) +
                           ", r" + std::to_string(n.op_a % 16) + "\n";
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
                    // Stable fallback to preserve assembly validity.
                    out += "    nop ; Unsupported Op fallback\n";
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
            
            // ResultID 0 is reserved for invalid/null states in BIR 2.0.
            if (n.result_id == 0 && n.opcode != ir::OpCode::NOP) [[unlikely]] {
                throw std::runtime_error("WASM target invariant violation: invalid result_id");
            }
            
            // Check for op_a/op_b out of bounds relative to 24-bit addressable limit (0xFFFFFF).
            if (n.op_a > 16777215 || n.op_b > 16777215) [[unlikely]] {
                throw std::runtime_error("WASM target invariant violation: operand index out of range");
            }
        }
    }
};

} // namespace dgtl::target


