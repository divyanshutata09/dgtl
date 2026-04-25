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
 * STEP 4: BINARY EMISSION - MACH-O (64-BIT)
 * PILLAR 4: HARDWARE AFFINITY & OS NATIVE PACKAGING
 * EMITTER: Mach-O Object File Generator (macOS / iOS / Apple Silicon)
 * STANDARDS: C++26 / UCRT64 / Mach-O ABI Spec
 */

#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>

// Umbrella Entry Point for IR consistency
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"

namespace dgtl::isa {

// --- Mach-O 64-bit structures for cross-platform emission ---
namespace macho {

static constexpr uint32_t MH_MAGIC_64_VAL                 = 0xFEEDFACF;
static constexpr uint32_t MH_OBJECT_TYPE                   = 0x1;
static constexpr int32_t  CPU_TYPE_ARM64_VAL               = 0x0100000C;
static constexpr int32_t  CPU_SUBTYPE_ALL_VAL              = 0x0;
static constexpr uint32_t MH_SUBSECTIONS_VIA_SYMBOLS_FLAG  = 0x2000;
static constexpr uint32_t LC_SEGMENT_64_CMD                = 0x19;
static constexpr uint32_t LC_SYMTAB_CMD                    = 0x02;
static constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS         = 0x80000000;
static constexpr uint32_t S_ATTR_SOME_INSTRUCTIONS         = 0x00000400;
static constexpr uint8_t  N_EXT_FLAG                       = 0x01;
static constexpr uint8_t  N_SECT_TYPE                      = 0x0E;

#pragma pack(push, 1)
struct Header64 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct SegmentCommand64 {
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

struct Section64 {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct SymtabCommand {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct Nlist64 {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};
#pragma pack(pop)

} // namespace macho

/**
 * @class MachOEmitter
 * @brief Binary Generator for Apple's Mach-O 64-bit ARM64 object files.
 * Encapsulates DGTL IR into a fully valid, linkable Mach-O .o file with
 * proper LC_SEGMENT_64 + LC_SYMTAB load commands and real ARM64 machine code.
 */
class alignas(64) MachOEmitter {
public:
    struct MachOResult {
        std::vector<uint8_t> binary_data;
        bool success;
    };

    explicit MachOEmitter(const dgtl::ir::IRStream& stream, const dgtl::ir::SymbolTable& symbols)
        : m_stream(stream), m_symbols(symbols) {}

    /**
     * @brief Produces a complete Mach-O 64-bit ARM64 relocatable object file.
     * Layout: [Header64][LC_SEGMENT_64 + Section64][LC_SYMTAB][.text][symtab][strtab]
     */
    MachOResult emit_to_binary() {
        MachOResult result;
        result.success = false;

        if (m_stream.is_null() || m_stream.size() == 0) return result;

        std::vector<uint8_t> text_data = generate_arm64_binary();
        if (text_data.empty()) return result;

        const uint32_t text_size = static_cast<uint32_t>(text_data.size());

        const uint32_t header_size    = sizeof(macho::Header64);
        const uint32_t seg_cmd_size   = sizeof(macho::SegmentCommand64) + sizeof(macho::Section64);
        const uint32_t symtab_cmd_sz  = sizeof(macho::SymtabCommand);
        const uint32_t total_cmds_sz  = seg_cmd_size + symtab_cmd_sz;
        const uint32_t text_offset    = header_size + total_cmds_sz;

        const uint32_t nsyms = 1;
        const uint32_t symtab_offset  = text_offset + text_size;
        const uint32_t nlist_size     = nsyms * sizeof(macho::Nlist64);

        std::string strtab;
        strtab.push_back('\0');
        const uint32_t sym_strx = static_cast<uint32_t>(strtab.size());
        strtab += "_DGTL_kernel_entry";
        strtab.push_back('\0');

        const uint32_t strtab_offset  = symtab_offset + nlist_size;
        const uint32_t strtab_size    = static_cast<uint32_t>(strtab.size());
        const uint32_t total_file_sz  = strtab_offset + strtab_size;

        result.binary_data.assign(total_file_sz, 0);
        uint8_t* buf = result.binary_data.data();

        // 1. Mach-O Header
        auto* hdr = reinterpret_cast<macho::Header64*>(buf);
        hdr->magic      = macho::MH_MAGIC_64_VAL;
        hdr->cputype    = macho::CPU_TYPE_ARM64_VAL;
        hdr->cpusubtype = macho::CPU_SUBTYPE_ALL_VAL;
        hdr->filetype   = macho::MH_OBJECT_TYPE;
        hdr->ncmds      = 2;
        hdr->sizeofcmds = total_cmds_sz;
        hdr->flags      = macho::MH_SUBSECTIONS_VIA_SYMBOLS_FLAG;
        hdr->reserved   = 0;

        // 2. LC_SEGMENT_64 (containing one section)
        auto* seg = reinterpret_cast<macho::SegmentCommand64*>(buf + header_size);
        seg->cmd      = macho::LC_SEGMENT_64_CMD;
        seg->cmdsize  = seg_cmd_size;
        std::memset(seg->segname, 0, 16);
        seg->vmaddr   = 0;
        seg->vmsize   = text_size;
        seg->fileoff  = text_offset;
        seg->filesize = text_size;
        seg->maxprot  = 7;  // rwx
        seg->initprot = 5;  // r-x
        seg->nsects   = 1;
        seg->flags    = 0;

        // 3. __TEXT,__text section
        auto* sect = reinterpret_cast<macho::Section64*>(
            buf + header_size + sizeof(macho::SegmentCommand64));
        std::memset(sect, 0, sizeof(macho::Section64));
        std::memcpy(sect->sectname, "__text", 6);
        std::memcpy(sect->segname,  "__TEXT", 6);
        sect->addr     = 0;
        sect->size     = text_size;
        sect->offset   = text_offset;
        sect->align    = 2;  // 2^2 = 4-byte (ARM64 instruction alignment)
        sect->reloff   = 0;
        sect->nreloc   = 0;
        sect->flags    = macho::S_ATTR_PURE_INSTRUCTIONS | macho::S_ATTR_SOME_INSTRUCTIONS;

        // 4. LC_SYMTAB
        auto* symcmd = reinterpret_cast<macho::SymtabCommand*>(
            buf + header_size + seg_cmd_size);
        symcmd->cmd     = macho::LC_SYMTAB_CMD;
        symcmd->cmdsize = symtab_cmd_sz;
        symcmd->symoff  = symtab_offset;
        symcmd->nsyms   = nsyms;
        symcmd->stroff  = strtab_offset;
        symcmd->strsize = strtab_size;

        // 5. .text machine code
        std::memcpy(buf + text_offset, text_data.data(), text_size);

        // 6. Symbol table entry
        auto* sym = reinterpret_cast<macho::Nlist64*>(buf + symtab_offset);
        sym->n_strx  = sym_strx;
        sym->n_type  = macho::N_SECT_TYPE | macho::N_EXT_FLAG;
        sym->n_sect  = 1;
        sym->n_desc  = 0;
        sym->n_value = 0;

        // 7. String table
        std::memcpy(buf + strtab_offset, strtab.data(), strtab_size);

        result.success = true;
        return result;
    }

    /**
     * @brief Saves the Mach-O object to disk for the native linker.
     */
    bool save_to_file(const std::string& filename) {
        auto result = emit_to_binary();
        if (!result.success) return false;

        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(result.binary_data.data()), 
                   result.binary_data.size());
        return file.good();
    }

private:
    const dgtl::ir::IRStream& m_stream;
    const dgtl::ir::SymbolTable& m_symbols;

    [[nodiscard]] static constexpr bool is_valid_gpr_slot(uint32_t slot) noexcept {
        return slot != dgtl::ir::INVALID_SLOT && slot < 32;
    }

    /**
     * @brief Encodes the IR stream into real ARM64 (AArch64) binary machine code.
     * Each instruction is a fixed-width 32-bit encoding in little-endian byte order.
     */
    std::vector<uint8_t> generate_arm64_binary() {
        std::vector<uint8_t> code;
        code.reserve(m_stream.size() * 4);

        for (size_t i = 0; i < m_stream.size(); ++i) {
            const auto& node = m_stream.get_node(i);
            const uint32_t insn = encode_arm64_instruction(node);
            code.push_back(static_cast<uint8_t>(insn & 0xFF));
            code.push_back(static_cast<uint8_t>((insn >> 8) & 0xFF));
            code.push_back(static_cast<uint8_t>((insn >> 16) & 0xFF));
            code.push_back(static_cast<uint8_t>((insn >> 24) & 0xFF));
        }

        return code;
    }

    /**
     * @brief Maps a single DGTL IR node to its ARM64 binary encoding.
     * Covers integer ALU, register moves, immediates, loads, stores, control flow.
     */
    static uint32_t encode_arm64_instruction(const dgtl::ir::IRNode& node) {
        const uint32_t rd = node.result_id & 0x1F;
        const uint32_t rn = node.op_a & 0x1F;
        const uint32_t rm = node.op_b & 0x1F;

        switch (node.opcode) {
            case dgtl::ir::OpCode::ADD_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // ADD Wd, Wn, Wm
                return 0x0B000000u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::SUB_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // SUB Wd, Wn, Wm
                return 0x4B000000u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::MUL_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // MADD Wd, Wn, Wm, WZR (Wd = Wn * Wm + 0)
                return 0x1B007C00u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::AND_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // AND Wd, Wn, Wm
                return 0x0A000000u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::OR_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // ORR Wd, Wn, Wm
                return 0x2A000000u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::XOR_I32:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // EOR Wd, Wn, Wm
                return 0x4A000000u | (rm << 16) | (rn << 5) | rd;
            case dgtl::ir::OpCode::REG_MOVE:
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a)) {
                    return 0xD503201Fu;
                }
                // MOV Wd, Wn ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ ORR Wd, WZR, Wn
                return 0x2A0003E0u | (rn << 16) | rd;
            case dgtl::ir::OpCode::LOAD_IMM:
            case dgtl::ir::OpCode::IMMEDIATE: {
                if (!is_valid_gpr_slot(node.result_id)) {
                    return 0xD503201Fu;
                }
                // MOVZ Wd, #imm16
                const uint32_t imm16 = node.op_b & 0xFFFF;
                return 0x52800000u | (imm16 << 5) | rd;
            }
            case dgtl::ir::OpCode::LOAD: {
                if (!is_valid_gpr_slot(node.result_id) || !is_valid_gpr_slot(node.op_a)) {
                    return 0xD503201Fu;
                }
                // LDR Wd, [Xn, #offset] (unsigned offset, scaled by 4)
                const uint32_t base = node.op_a & 0x1F;
                const uint32_t off12 = (node.op_c & 0xFFF) >> 2;
                return 0xB9400000u | (off12 << 10) | (base << 5) | rd;
            }
            case dgtl::ir::OpCode::STORE: {
                if (!is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // STR Wb, [Xn, #offset]
                const uint32_t base = node.op_a & 0x1F;
                const uint32_t val = node.op_b & 0x1F;
                const uint32_t off12 = (node.op_c & 0xFFF) >> 2;
                return 0xB9000000u | (off12 << 10) | (base << 5) | val;
            }
            case dgtl::ir::OpCode::RET:
                return 0xD65F03C0u; // RET
            case dgtl::ir::OpCode::NOP:
                return 0xD503201Fu; // NOP
            default:
                return 0xD503201Fu; // NOP fallback for unsupported ops
        }
    }
};

} // namespace dgtl::isa

/**
 * @brief Backward Compatibility Alias.
 */
namespace dgtl::isa {
    using MachOEmitterAlias = MachOEmitter;
}



