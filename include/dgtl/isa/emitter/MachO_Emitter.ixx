/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
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

module;

#include <cstdlib>

#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>
#include <algorithm>
#include <unordered_map>

export module dgtl.ir_core.macho_emitter;

import dgtl.ir_core;
import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.abi_lowering;
import dgtl.ir_core.arm64_emitter;
import dgtl.assertions;
import dgtl.flat_map;

export namespace dgtl::isa {

// --- Mach-O 64-bit structures for cross-platform emission ---
namespace macho {

    inline constexpr uint32_t MH_MAGIC_64_VAL                 = 0xFEEDFACF;
    inline constexpr uint32_t MH_OBJECT_TYPE                  = 0x1;
    inline constexpr int32_t  CPU_TYPE_ARM64_VAL              = 0x0100000C;
    inline constexpr int32_t  CPU_SUBTYPE_ALL_VAL             = 0x0;
    inline constexpr uint32_t MH_SUBSECTIONS_VIA_SYMBOLS_FLAG = 0x2000;
    inline constexpr uint32_t LC_SEGMENT_64_CMD               = 0x19;
    inline constexpr uint32_t LC_SYMTAB_CMD                   = 0x02;
    inline constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS        = 0x80000000;
    inline constexpr uint32_t S_ATTR_SOME_INSTRUCTIONS        = 0x00000400;
    inline constexpr uint8_t  N_EXT_FLAG                      = 0x01;
    inline constexpr uint8_t  N_SECT_TYPE                     = 0x0E;
    inline constexpr uint8_t  N_UNDF_TYPE                     = 0x00;
    inline constexpr uint8_t  ARM64_RELOC_BRANCH26            = 0x02;

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
    [[maybe_unused]] uint32_t reserved1;
    [[maybe_unused]] uint32_t reserved2;
    [[maybe_unused]] uint32_t reserved3;
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

struct RelocationInfo {
    int32_t  r_address;
    uint32_t r_info;
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
        bool success{false};
    };

    explicit MachOEmitter(const dgtl::ir::IRStream& stream, const dgtl::semantic::SymbolTable& symbols)
        : m_stream(stream), m_symbols(symbols) {}

    /**
     * @brief Produces a complete Mach-O 64-bit ARM64 relocatable object file.
     * Layout: [Header64][LC_SEGMENT_64 + Section64][LC_SYMTAB][.text][symtab][strtab]
     */
    MachOResult emit_to_binary() {
        MachOResult result;
        result.success = false;

        (void)m_symbols;

        if (m_stream.is_null() || m_stream.size() == 0) return result;

        auto codegen = generate_arm64_binary();
        const std::vector<uint8_t>& text_data = codegen.code;
        const std::vector<RuntimeBranchReloc>& runtime_relocs = codegen.runtime_relocs;
        if (text_data.empty()) return result;

        std::vector<std::string> runtime_symbols;
        runtime_symbols.reserve(runtime_relocs.size());
        for (const auto& reloc : runtime_relocs) {
            if (std::find(runtime_symbols.begin(), runtime_symbols.end(), reloc.symbol) == runtime_symbols.end()) {
                runtime_symbols.push_back(reloc.symbol);
            }
        }

        const uint32_t text_size = static_cast<uint32_t>(text_data.size());
        const uint32_t nreloc = static_cast<uint32_t>(runtime_relocs.size());

        const uint32_t header_size    = sizeof(macho::Header64);
        const uint32_t seg_cmd_size   = sizeof(macho::SegmentCommand64) + sizeof(macho::Section64);
        const uint32_t symtab_cmd_sz  = sizeof(macho::SymtabCommand);
        const uint32_t total_cmds_sz  = seg_cmd_size + symtab_cmd_sz;
        const uint32_t text_offset    = header_size + total_cmds_sz;

        const uint32_t reloc_offset   = text_offset + text_size;
        const uint32_t reloc_size     = nreloc * sizeof(dgtl::isa::macho::RelocationInfo);

        const uint32_t nsyms = 1u + static_cast<uint32_t>(runtime_symbols.size());
        const uint32_t symtab_offset  = reloc_offset + reloc_size;
        const uint32_t nlist_size     = nsyms * sizeof(dgtl::isa::macho::Nlist64);

        std::string strtab;
        strtab.push_back('\0');
        const uint32_t sym_strx = static_cast<uint32_t>(strtab.size());
        strtab += "_DGTL_kernel_entry";
        strtab.push_back('\0');

        std::unordered_map<std::string, uint32_t> runtime_sym_index;
        runtime_sym_index.reserve(runtime_symbols.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(runtime_symbols.size()); ++i) {
            const std::string macho_name = std::string("_") + runtime_symbols[i];
            const uint32_t strx = static_cast<uint32_t>(strtab.size());
            strtab += macho_name;
            strtab.push_back('\0');
            runtime_sym_index.emplace(runtime_symbols[i], i + 1u);
            (void)strx;
        }

        const uint32_t strtab_offset  = symtab_offset + nlist_size;
        const uint32_t strtab_size    = static_cast<uint32_t>(strtab.size());
        const uint32_t total_file_sz  = strtab_offset + strtab_size;

        result.binary_data.assign(total_file_sz, 0);
        uint8_t* buf = result.binary_data.data();

        // 1. Mach-O Header
        auto* hdr = reinterpret_cast<dgtl::isa::macho::Header64*>(buf);
        hdr->magic      = dgtl::isa::macho::MH_MAGIC_64_VAL;
        hdr->cputype    = dgtl::isa::macho::CPU_TYPE_ARM64_VAL;
        hdr->cpusubtype = dgtl::isa::macho::CPU_SUBTYPE_ALL_VAL;
        hdr->filetype   = dgtl::isa::macho::MH_OBJECT_TYPE;
        hdr->ncmds      = 2;
        hdr->sizeofcmds = total_cmds_sz;
        hdr->flags      = dgtl::isa::macho::MH_SUBSECTIONS_VIA_SYMBOLS_FLAG;
        hdr->reserved   = 0;

        // 2. LC_SEGMENT_64 (containing one section)
        auto* seg = reinterpret_cast<dgtl::isa::macho::SegmentCommand64*>(buf + header_size);
        seg->cmd      = dgtl::isa::macho::LC_SEGMENT_64_CMD;
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
        auto* sect = reinterpret_cast<dgtl::isa::macho::Section64*>(
            buf + header_size + sizeof(dgtl::isa::macho::SegmentCommand64));
        std::memset(sect, 0, sizeof(dgtl::isa::macho::Section64));
        std::memcpy(sect->sectname, "__text", 6);
        std::memcpy(sect->segname,  "__TEXT", 6);
        sect->addr     = 0;
        sect->size     = text_size;
        sect->offset   = text_offset;
        sect->align    = 2;  // 2^2 = 4-byte (ARM64 instruction alignment)
        sect->reloff   = (nreloc > 0u) ? reloc_offset : 0u;
        sect->nreloc   = nreloc;
        sect->flags    = dgtl::isa::macho::S_ATTR_PURE_INSTRUCTIONS | dgtl::isa::macho::S_ATTR_SOME_INSTRUCTIONS;

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

        // 6. Relocation table for runtime branch calls
        if (nreloc > 0u) {
            auto* rel = reinterpret_cast<dgtl::isa::macho::RelocationInfo*>(buf + reloc_offset);
            for (uint32_t i = 0; i < nreloc; ++i) {
                const auto& rr = runtime_relocs[i];
                const auto it = runtime_sym_index.find(rr.symbol);
                if (it == runtime_sym_index.end()) {
                    rel[i].r_address = 0;
                    rel[i].r_info = 0;
                    continue;
                }
                const uint32_t sym_index = it->second;
                rel[i].r_address = static_cast<int32_t>(rr.text_offset);
                rel[i].r_info = (sym_index & 0x00FFFFFFu)
                              | (1u << 24) // external symbol
                              | (dgtl::isa::macho::ARM64_RELOC_BRANCH26 << 25)
                              | (1u << 27) // pcrel
                              | (2u << 28); // length = 2 -> 4 bytes
            }
        }

        // 7. Symbol table entries
        auto* sym = reinterpret_cast<macho::Nlist64*>(buf + symtab_offset);
        sym[0].n_strx  = sym_strx;
        sym[0].n_type  = macho::N_SECT_TYPE | macho::N_EXT_FLAG;
        sym[0].n_sect  = 1;
        sym[0].n_desc  = 0;
        sym[0].n_value = 0;

        uint32_t str_cursor = sym_strx + static_cast<uint32_t>(std::strlen("_DGTL_kernel_entry")) + 1u;
        for (uint32_t i = 0; i < static_cast<uint32_t>(runtime_symbols.size()); ++i) {
            const std::string macho_name = std::string("_") + runtime_symbols[i];
            sym[i + 1].n_strx  = str_cursor;
            sym[i + 1].n_type  = macho::N_EXT_FLAG;
            sym[i + 1].n_sect  = 0;
            sym[i + 1].n_desc  = 0;
            sym[i + 1].n_value = 0;
            str_cursor += static_cast<uint32_t>(macho_name.size()) + 1u;
        }

        // 8. String table
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
    const dgtl::semantic::SymbolTable& m_symbols;

    struct RuntimeBranchReloc {
        uint32_t text_offset{0};
        std::string symbol;
    };

    struct Arm64CodegenResult {
        std::vector<uint8_t> code;
        std::vector<RuntimeBranchReloc> runtime_relocs;
    };

    [[nodiscard]] static constexpr bool is_valid_gpr_slot(uint32_t slot) noexcept {
        return slot != dgtl::ir::INVALID_SLOT && slot < 32;
    }

    static void append_u32le(std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    }

    static uint32_t encode_mov_x(uint32_t dst, uint32_t src) {
        return 0xAA0003E0u | ((src & 0x1Fu) << 16) | (dst & 0x1Fu);
    }

    /**
     * @brief Encodes the IR stream into real ARM64 (AArch64) binary machine code.
     * Two-pass: first build label_map + node_offsets, then emit with branch patching.
     * Each instruction is a fixed-width 32-bit encoding in little-endian byte order.
     */
    Arm64CodegenResult generate_arm64_binary() {
        const size_t count = m_stream.size();
        Arm64CodegenResult result;
        std::vector<uint8_t>& code = result.code;
        code.reserve(count * 8);
        result.runtime_relocs.reserve(count / 8 + 4);

        // --- Pass 0: Build label_map (frontend label IDs -> stream node indices) ---
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(count / 4 + 4);
        for (size_t i = 0; i < count; ++i) {
            const auto& node = m_stream.get_node(i);
            if (node.opcode == dgtl::ir::OpCode::LABEL) {
                uint32_t lid = (node.result_id != dgtl::ir::INVALID_SLOT)
                                   ? node.result_id : node.op_a;
                label_map[lid] = i;
            }
        }
        // Resolve a raw branch target through label_map or use as stream index
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < count) return static_cast<size_t>(raw);
            return count; // past-end -> epilogue
        };

        // --- Pass 1: Emit instructions; record branch patch sites ---
        struct BranchPatch32 {
            size_t insn_byte_offset; // byte offset in `code` of the 4-byte instruction
            size_t target_node;      // stream node index to branch to
            bool is_cond;            // conditional (CBZ/CBNZ/B.cond) vs unconditional B
            uint32_t cond_reg;       // for CBNZ: register index
        };
        std::vector<BranchPatch32> patches;
        patches.reserve(count / 4 + 4);

        // node_byte_offsets[i] = byte offset where node i's instruction starts
        std::vector<size_t> node_byte_offsets(count + 1, 0);

        for (size_t i = 0; i < count; ++i) {
            node_byte_offsets[i] = code.size();
            const auto& node = m_stream.get_node(i);

            // Skip pseudo-ops that emit no instruction
            if (node.opcode == dgtl::ir::OpCode::NOP ||
                node.opcode == dgtl::ir::OpCode::LABEL ||
                node.opcode == dgtl::ir::OpCode::PHI_NODE) {
                // Emit NOP placeholder to keep fixed-width stride for offsets
                const uint32_t nop = 0xD503201Fu;
                append_u32le(code, nop);
                continue;
            }

            // Branch instructions — emit placeholder, record patch
            if (node.opcode == dgtl::ir::OpCode::BRANCH ||
                node.opcode == dgtl::ir::OpCode::GOTO) {
                patches.push_back({code.size(), resolve_target(node.op_a), false, 0});
                const uint32_t placeholder = 0x14000000u; // B #0
                append_u32le(code, placeholder);
                continue;
            }
            if (node.opcode == dgtl::ir::OpCode::BRANCH_COND) {
                // CBNZ x{op_a}, target
                patches.push_back({code.size(), resolve_target(node.op_b), true, node.op_a & 0x1Fu});
                const uint32_t placeholder = 0xB5000000u | (node.op_a & 0x1Fu); // CBNZ x{reg}, #0
                append_u32le(code, placeholder);
                continue;
            }
            if (node.opcode == dgtl::ir::OpCode::BRANCH_COMPARE_FUSED) {
                // CMP x{op_a}, x{op_b} + B.NE target  (2 instructions)
                const uint32_t rn = node.op_a & 0x1Fu;
                const uint32_t rm = node.op_b & 0x1Fu;
                // SUBS XZR, Xn, Xm  (CMP Xn,Xm)
                const uint32_t cmp_insn = 0xEB000000u | (rm << 16) | (rn << 5) | 0x1Fu;
                append_u32le(code, cmp_insn);
                // B.NE placeholder
                patches.push_back({code.size(), resolve_target(node.op_c), true, 0xFFu}); // 0xFF = sentinel for B.cond
                const uint32_t bne_placeholder = 0x54000001u; // B.NE #0
                append_u32le(code, bne_placeholder);
                continue;
            }

            if (const char* runtime_symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                runtime_symbol != nullptr) {
                if (is_valid_gpr_slot(node.op_a) && node.op_a != 0u) {
                    append_u32le(code, encode_mov_x(0u, node.op_a));
                }
                if (is_valid_gpr_slot(node.op_b) && node.op_b != 1u) {
                    append_u32le(code, encode_mov_x(1u, node.op_b));
                }
                if (is_valid_gpr_slot(node.op_c) && node.op_c != 2u) {
                    append_u32le(code, encode_mov_x(2u, node.op_c));
                }

                const uint32_t bl_offset = static_cast<uint32_t>(code.size());
                append_u32le(code, 0x94000000u); // BL #0, patched by relocation
                result.runtime_relocs.push_back({bl_offset, runtime_symbol});

                if (is_valid_gpr_slot(node.result_id) && node.result_id != 0u) {
                    append_u32le(code, encode_mov_x(node.result_id, 0u));
                }
                continue;
            }

            const uint32_t insn = encode_arm64_instruction(node);
            append_u32le(code, insn);
        }
        node_byte_offsets[count] = code.size();

        // --- Pass 2: Patch branch displacements ---
        for (const auto& p : patches) {
            const size_t target_node = (p.target_node < count) ? p.target_node : count;
            const size_t target_byte = node_byte_offsets[target_node];
            // PC-relative offset in instructions (ARM64 branches are instruction-aligned)
            const int64_t delta_bytes = static_cast<int64_t>(target_byte) -
                                        static_cast<int64_t>(p.insn_byte_offset);
            const int64_t delta_insns = delta_bytes / 4;

            uint32_t insn = 0;
            std::memcpy(&insn, &code[p.insn_byte_offset], 4);

            if (!p.is_cond) {
                // Unconditional B: bits [25:0] = imm26 (signed)
                if (delta_insns >= -(1 << 25) && delta_insns < (1 << 25)) {
                    insn = 0x14000000u | (static_cast<uint32_t>(delta_insns) & 0x3FFFFFFu);
                }
            } else if (p.cond_reg == 0xFFu) {
                // B.NE: bits [23:5] = imm19 (signed)
                if (delta_insns >= -(1 << 18) && delta_insns < (1 << 18)) {
                    insn = 0x54000001u | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5);
                }
            } else {
                // CBNZ x{reg}: bits [23:5] = imm19 (signed)
                if (delta_insns >= -(1 << 18) && delta_insns < (1 << 18)) {
                    insn = 0xB5000000u | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5)
                                       | (p.cond_reg & 0x1Fu);
                }
            }

            std::memcpy(&code[p.insn_byte_offset], &insn, 4);
        }

        return result;
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
                // MOV Wd, Wn -> ORR Wd, WZR, Wn
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
                if ((node.op_c & 0x3u) != 0u) {
                    return 0xD503201Fu;
                }
                const uint32_t off_bytes = node.op_c & 0xFFF;
                const uint32_t off12 = off_bytes >> 2;
                return 0xB9400000u | (off12 << 10) | (base << 5) | rd;
            }
            case dgtl::ir::OpCode::STORE: {
                if (!is_valid_gpr_slot(node.op_a) || !is_valid_gpr_slot(node.op_b)) {
                    return 0xD503201Fu;
                }
                // STR Wb, [Xn, #offset]
                const uint32_t val = node.op_a & 0x1F;
                const uint32_t base = node.op_b & 0x1F;
                if ((node.op_c & 0x3u) != 0u) {
                    return 0xD503201Fu;
                }
                const uint32_t off_bytes = node.op_c & 0xFFF;
                const uint32_t off12 = off_bytes >> 2;
                return 0xB9000000u | (off12 << 10) | (base << 5) | val;
            }
            case dgtl::ir::OpCode::RET:
                return 0xD65F03C0u; // RET
            case dgtl::ir::OpCode::NOP:
                return 0xD503201Fu; // NOP
            default:
                // Runtime-backed intrinsics are lowered through shared contract in text backends;
                // Mach-O binary emitter cannot bind runtime imports in this path yet.
                // Emit BRK instead of silently dropping semantics.
                if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                    return 0xD4200000u; // BRK #0
                }
                return 0xD503201Fu; // NOP fallback for unsupported ops
        }
    }
};

} // namespace dgtl::isa

/**
 * @brief Backward Compatibility Alias.
 */
export namespace dgtl::isa {
    using MachOEmitterAlias = MachOEmitter;
}