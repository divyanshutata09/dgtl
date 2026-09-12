/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* 
 * DGTL AOT LINKER & SOVEREIGN OBJECT WEAVER
 * MODULE: dgtl.backend.aot_linker
 * PURPOSE: Decoupled, industrial-grade AOT binary synthesizer for standalone
 *          COFF relocatables (.obj), ELF objects (.o), static archives (.lib/.a),
 *          and PE executables (.exe) with dual-section .text + .rdata layout.
 */

module;

#include <cstdlib>
#include <vector>
#include <string_view>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <stdlib.h>
    #undef CONST
    #undef IMAGE_FILE_MACHINE_AMD64
    #undef IMAGE_REL_AMD64_REL32
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

export module dgtl.backend.aot_linker;

import dgtl.ir_core;
import dgtl.ir_core.isel_gen;
import dgtl.ir_core.binary_loader;
import dgtl.ir_core.runtime_contract;

export namespace dgtl::backend::aot {

namespace elf {
    struct Elf64_Ehdr { 
        unsigned char e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version; 
        uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags; 
        uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; 
    };
    struct Elf64_Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint64_t sh_flags;
        uint64_t sh_addr;
        uint64_t sh_offset;
        uint64_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint64_t sh_addralign;
        uint64_t sh_entsize;
    };
    struct Elf64_Sym {
        uint32_t st_name;
        unsigned char st_info;
        unsigned char st_other;
        uint16_t st_shndx;
        uint64_t st_value;
        uint64_t st_size;
    };
    struct Elf64_Rela {
        uint64_t r_offset;
        uint64_t r_info;
        int64_t  r_addend;
    };
    #ifndef ELFMAG
        #define ELFMAG "\177ELF"
        #define SELFMAG 4
        #define ELFCLASS64 2
        #define ELFDATA2LSB 1
        #define EV_CURRENT 1
        #define ET_REL 1
        #define EM_X86_64 62
        #define SHT_PROGBITS 1
        #define SHT_SYMTAB 2
        #define SHT_STRTAB 3
        #define SHT_RELA 4
        #define SHF_ALLOC 0x2
        #define SHF_EXECINSTR 0x4
        #define SHF_INFO_LINK 0x40
        #define STB_LOCAL 0
        #define STB_GLOBAL 1
        #define STT_SECTION 3
        #define STT_FUNC 2
        #define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
    #endif
} // namespace elf

namespace coff {
#pragma pack(push, 1)
    struct FileHeader {
        uint16_t Machine;
        uint16_t NumberOfSections;
        uint32_t TimeDateStamp;
        uint32_t PointerToSymbolTable;
        uint32_t NumberOfSymbols;
        uint16_t SizeOfOptionalHeader;
        uint16_t Characteristics;
    };

    struct SectionHeader {
        char     Name[8];
        uint32_t VirtualSize;
        uint32_t VirtualAddress;
        uint32_t SizeOfRawData;
        uint32_t PointerToRawData;
        uint32_t PointerToRelocations;
        uint32_t PointerToLinenumbers;
        uint16_t NumberOfRelocations;
        uint16_t NumberOfLinenumbers;
        uint32_t Characteristics;
    };

    struct Relocation {
        uint32_t VirtualAddress;
        uint32_t SymbolTableIndex;
        uint16_t Type;
    };

    struct Symbol {
        union {
            char ShortName[8];
            struct {
                uint32_t Zeroes;
                uint32_t Offset;
            } LongName;
        } N;
        uint32_t Value;
        int16_t  SectionNumber;
        uint16_t Type;
        uint8_t  StorageClass;
        uint8_t  NumberOfAuxSymbols;
    };

    struct ImageDosHeader {
        uint16_t e_magic;
        uint16_t e_cblp;
        uint16_t e_cp;
        uint16_t e_crlc;
        uint16_t e_cparhdr;
        uint16_t e_minalloc;
        uint16_t e_maxalloc;
        uint16_t e_ss;
        uint16_t e_sp;
        uint16_t e_csum;
        uint16_t e_ip;
        uint16_t e_cs;
        uint16_t e_lfarlc;
        uint16_t e_ovno;
        uint16_t e_res[4];
        uint16_t e_oemid;
        uint16_t e_oeminfo;
        uint16_t e_res2[10];
        uint32_t e_lfanew;
    };

    struct ImageDataDirectory {
        uint32_t VirtualAddress;
        uint32_t Size;
    };

    struct ImportDescriptor {
        uint32_t OriginalFirstThunk;
        uint32_t TimeDateStamp;
        uint32_t ForwarderChain;
        uint32_t Name;
        uint32_t FirstThunk;
    };

    struct ImageOptionalHeader64 {
        uint16_t Magic;
        uint8_t  MajorLinkerVersion;
        uint8_t  MinorLinkerVersion;
        uint32_t SizeOfCode;
        uint32_t SizeOfInitializedData;
        uint32_t SizeOfUninitializedData;
        uint32_t AddressOfEntryPoint;
        uint32_t BaseOfCode;
        uint64_t ImageBase;
        uint32_t SectionAlignment;
        uint32_t FileAlignment;
        uint16_t MajorOperatingSystemVersion;
        uint16_t MinorOperatingSystemVersion;
        uint16_t MajorImageVersion;
        uint16_t MinorImageVersion;
        uint16_t MajorSubsystemVersion;
        uint16_t MinorSubsystemVersion;
        uint32_t Win32VersionValue;
        uint32_t SizeOfImage;
        uint32_t SizeOfHeaders;
        uint32_t CheckSum;
        uint16_t Subsystem;
        uint16_t DllCharacteristics;
        uint64_t SizeOfStackReserve;
        uint64_t SizeOfStackCommit;
        uint64_t SizeOfHeapReserve;
        uint64_t SizeOfHeapCommit;
        uint32_t LoaderFlags;
        uint32_t NumberOfRvaAndSizes;
        ImageDataDirectory DataDirectory[16];
    };
#pragma pack(pop)

    inline constexpr uint16_t MACHINE_AMD64             = 0x8664;
    inline constexpr uint32_t SCN_CNT_CODE              = 0x00000020;
    inline constexpr uint32_t SCN_CNT_INITIALIZED_DATA  = 0x00000040;
    inline constexpr uint32_t SCN_MEM_EXECUTE           = 0x20000000;
    inline constexpr uint32_t SCN_MEM_READ              = 0x40000000;
    inline constexpr uint32_t SCN_MEM_WRITE             = 0x80000000;
    inline constexpr uint32_t SCN_ALIGN_16BYTES         = 0x00500000;

    inline constexpr uint8_t  SYM_CLASS_EXTERNAL        = 2;
    inline constexpr uint8_t  SYM_CLASS_STATIC          = 3;
    inline constexpr uint16_t SYM_TYPE_FUNCTION         = 0x20;
    inline constexpr uint16_t REL_AMD64_REL32           = 4;
} // namespace coff

struct MagicMultiplierS64 {
    uint64_t M = 0;
    uint8_t s = 0;
    bool is_power_of_2 = false;
    bool valid = false;
};

inline MagicMultiplierS64 compute_magic_s64(int64_t d) noexcept {
    MagicMultiplierS64 res{};
    if (d <= 0) return res;
    if ((d & (d - 1)) == 0) {
        res.is_power_of_2 = true;
        res.s = static_cast<uint8_t>(__builtin_ctzll(static_cast<unsigned long long>(d)));
        res.valid = true;
        return res;
    }
    unsigned __int128 two_63 = static_cast<unsigned __int128>(1) << 63;
    unsigned __int128 ud = static_cast<unsigned __int128>(d);
    unsigned __int128 anc = two_63 - 1 - (two_63 - 1) % ud;
    uint8_t p = 63;
    unsigned __int128 q1 = two_63 / anc;
    unsigned __int128 r1 = two_63 - q1 * anc;
    unsigned __int128 q2 = two_63 / ud;
    unsigned __int128 r2 = two_63 - q2 * ud;
    unsigned __int128 delta = 0;
    do {
        p++;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1++;
            r1 -= anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ud) {
            q2++;
            r2 -= ud;
        }
        delta = ud - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    res.M = static_cast<uint64_t>(q2 + 1);
    res.s = p - 64;
    res.valid = (res.M < (1ULL << 63));
    return res;
}

struct CoffCallReloc {
    uint32_t    text_offset;
    std::string callee_name;
};

struct CoffReloc {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
};

class DGTLAOTLinker {
protected:
    std::vector<std::string> fallback_diagnostics_;

    inline void clear_fallback_diagnostics() noexcept {
        fallback_diagnostics_.clear();
    }

    inline void record_fallback_opcode(size_t node_index, ir::OpCode opcode) {
        std::string msg;
        msg.reserve(64);
        msg.append("node#").append(std::to_string(node_index))
           .append(" opcode=").append(ir::opcode_name(opcode))
           .append(" (#").append(std::to_string(static_cast<uint16_t>(opcode)))
           .append(")");
        fallback_diagnostics_.push_back(std::move(msg));
    }

    [[nodiscard]] static constexpr size_t align_up(size_t value, size_t alignment) noexcept {
        if (alignment <= 1) [[unlikely]] return value;
        const size_t rem = value % alignment;
        return rem == 0 ? value : (value + (alignment - rem));
    }

    [[nodiscard]] static bool write_binary_file(std::string_view path, const std::vector<uint8_t>& bytes) {
        if (path.empty()) [[unlikely]] return false;
        std::ofstream out(path.data(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) [[unlikely]] return false;
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        out.close();
        return out.good();
    }

    [[nodiscard]] static constexpr uint32_t to_big_endian_u32(uint32_t val) noexcept {
#if defined(_MSC_VER)
        return _byteswap_ulong(val);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap32(val);
#else
        return ((val & 0xFF000000u) >> 24) |
               ((val & 0x00FF0000u) >> 8)  |
               ((val & 0x0000FF00u) << 8)  |
               ((val & 0x000000FFu) << 24);
#endif
    }

public:
    DGTLAOTLinker() = default;

    [[nodiscard]] const std::vector<std::string>& fallback_diagnostics() const noexcept {
        return fallback_diagnostics_;
    }

    static size_t x64_prologue_size(const ir::IRStream& stream) {
        size_t size = 11; // push rbp; mov rbp, rsp; sub rsp, imm32
        if (!stream.is_null() && stream.size() > 0 && stream.get_node(0).opcode == ir::OpCode::LABEL) {
            const auto& fn_hdr = stream.get_node(0);
            if (fn_hdr.op_a != ir::INVALID_SLOT && fn_hdr.op_a != 0u) size += 7;
            if (fn_hdr.op_b != ir::INVALID_SLOT && fn_hdr.op_b != 0u) size += 7;
            if (fn_hdr.op_c != ir::INVALID_SLOT && fn_hdr.op_c != 0u) size += 7;
            if (fn_hdr.logic_flags != ir::INVALID_SLOT && fn_hdr.logic_flags != 0u) size += 7;
        }
        return size;
    }

    static size_t x64_node_size(const ir::IRNode& node) {
        using namespace dgtl::isa::lowering;
        if (runtime_contract::has_runtime_symbol(node.opcode)) {
            return 64;
        }

        const MachineForm form = generated::classify_machine_form(node.opcode);
        switch (form) {
            case MachineForm::NoOp:             return 1;
            case MachineForm::Move:             return 14;
            case MachineForm::LoadStack:        return 14;
            case MachineForm::StoreStack:       return 14;
            case MachineForm::LoadImmediate:    return 24;
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64:  return 80;
            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64:    return 32;
            case MachineForm::FloatTernaryFma32:return 36;
            case MachineForm::VectorBinary512:  return 24;
            case MachineForm::VectorStore512:   return 12;
            case MachineForm::VectorGather512:  return 12;
            case MachineForm::Compare32:        return 20;
            case MachineForm::Call:             return 96;
            case MachineForm::Goto:             return 5;
            case MachineForm::BranchCond:       return 16;
            case MachineForm::BranchCompare:    return 20;
            case MachineForm::Return:           return 12;
            case MachineForm::Syscall:          return 28;
            default:                            return 8;
        }
    }

    bool emit_aot_machine_code(
        uint8_t* dest,
        size_t dest_capacity,
        const ir::IRStream& stream,
        size_t* emitted_size = nullptr,
        const std::unordered_map<uint32_t, uintptr_t>& slot_map = {},
        std::vector<CoffCallReloc>* out_relocs = nullptr,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {})
    {
        if (dest == nullptr || stream.is_null()) return false;
        clear_fallback_diagnostics();
        const size_t count = stream.size();

        std::unordered_map<uint32_t, uint64_t> known_constants;
        known_constants.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::CONST || node.opcode == ir::OpCode::IMMEDIATE) {
                uint64_t imm = (static_cast<uint64_t>(node.op_b) << 32u) | (static_cast<uint64_t>(node.op_a) & 0xFFFFFFFFu);
                if (imm == 0 && node.intent_hash != 0) {
                    imm = node.intent_hash;
                }
                if (node.result_id != ir::INVALID_SLOT) {
                    known_constants[node.result_id] = imm;
                }
            }
        }

        std::unordered_map<uint32_t, size_t> label_map;
        std::unordered_map<uint32_t, int32_t> slot_offsets;
        slot_offsets.reserve(count * 2 + 8);
        uint32_t next_slot_index = 0;
        auto assign_slot = [&](uint32_t slot_id) {
            if (slot_id == ir::INVALID_SLOT) return;
            if (slot_offsets.find(slot_id) != slot_offsets.end()) return;
            slot_offsets.emplace(slot_id, -8 * static_cast<int32_t>(++next_slot_index));
        };

        assign_slot(0u);
        assign_slot(1u);
        assign_slot(2u);
        assign_slot(3u);

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::LABEL) {
                if (node.result_id != ir::INVALID_SLOT) {
                    label_map[node.result_id] = i;
                }
                const bool is_func_header = (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                if (!is_func_header) {
                    if (node.op_a != ir::INVALID_SLOT) {
                        label_map[node.op_a] = i;
                    }
                    if (node.op_b != ir::INVALID_SLOT) {
                        label_map[node.op_b] = i;
                    }
                }
                if (node.op_a != ir::INVALID_SLOT) assign_slot(node.op_a);
                if (node.op_b != ir::INVALID_SLOT) assign_slot(node.op_b);
                if (node.op_c != ir::INVALID_SLOT) assign_slot(node.op_c);
                if (node.logic_flags != ir::INVALID_SLOT) assign_slot(node.logic_flags);
                continue;
            }

            if (node.result_id != ir::INVALID_SLOT) assign_slot(node.result_id);
            if (node.op_a != ir::INVALID_SLOT) assign_slot(node.op_a);
            if (node.op_b != ir::INVALID_SLOT) assign_slot(node.op_b);
            if (node.op_c != ir::INVALID_SLOT) assign_slot(node.op_c);
            if (node.logic_flags != ir::INVALID_SLOT) assign_slot(node.logic_flags);

            const auto overflow = stream.get_call_overflow_args(i);
            for (uint32_t ov : overflow) {
                if (ov != ir::INVALID_SLOT) assign_slot(ov);
            }
        }

        auto resolve_target = [&](uint32_t raw) -> size_t {
            if (raw == ir::INVALID_SLOT) return count;
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            for (size_t k = 0; k < count; ++k) {
                const auto& knode = stream.get_node(k);
                if (knode.opcode == ir::OpCode::LABEL) {
                    if (knode.result_id == raw || knode.op_a == raw || knode.op_b == raw) {
                        return k;
                    }
                }
            }
            return count;
        };

        const uint32_t raw_frame_size = (next_slot_index + 4u) * 8u;
        const uint32_t frame_size = (raw_frame_size + 15u) & ~15u;

        std::vector<size_t> node_offsets(count + 1, 0);
        size_t probe = x64_prologue_size(stream);
        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = probe;
            const auto& node = stream.get_node(i);
            probe += x64_node_size(node);
        }
        node_offsets[count] = probe;

        if (probe > dest_capacity) [[unlikely]] return false;

        struct BranchPatch { size_t disp_offset; size_t source_end; size_t target_node; };
        std::vector<BranchPatch> patches;

        const int32_t fallback_offset = -8 * static_cast<int32_t>(next_slot_index + 1u);
        auto slot_disp = [&](uint32_t slot_id) -> int32_t {
            if (slot_id == ir::INVALID_SLOT) return fallback_offset;
            const auto it = slot_offsets.find(slot_id);
            return (it != slot_offsets.end()) ? it->second : fallback_offset;
        };
        patches.reserve(count / 8 + 4);

        size_t offset = 0;
        auto write_u8 = [&](uint8_t byte) { dest[offset++] = byte; };
        auto write_u32 = [&](uint32_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        auto write_i32 = [&](int32_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };
        auto write_u64 = [&](uint64_t value) {
            std::memcpy(dest + offset, &value, sizeof(value));
            offset += sizeof(value);
        };

        auto emit_mov_reg_from_slot = [&](uint8_t rex, uint8_t modrm, uint32_t slot_id) {
            write_u8(rex);
            write_u8(0x8B);
            write_u8(modrm);
            write_i32(slot_disp(slot_id));
        };
        auto emit_mov_slot_from_reg = [&](uint8_t rex, uint8_t modrm, uint32_t slot_id) {
            write_u8(rex);
            write_u8(0x89);
            write_u8(modrm);
            write_i32(slot_disp(slot_id));
        };

        auto emit_mov_rax_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x85, slot_id); };
        auto emit_mov_rcx_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x8D, slot_id); };
        auto emit_mov_rdx_from_slot = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x48, 0x95, slot_id); };
        auto emit_mov_r8_from_slot  = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x4C, 0x85, slot_id); };
        auto emit_mov_r9_from_slot  = [&](uint32_t slot_id) { emit_mov_reg_from_slot(0x4C, 0x8D, slot_id); };

        auto emit_store_rax_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x85, slot_id); };
        auto emit_store_rcx_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x8D, slot_id); };
        auto emit_store_rdx_to_slot = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x48, 0x95, slot_id); };
        auto emit_store_r8_to_slot  = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x4C, 0x85, slot_id); };
        auto emit_store_r9_to_slot  = [&](uint32_t slot_id) { emit_mov_slot_from_reg(0x4C, 0x8D, slot_id); };

        // 1. Prologue: Setup stack frame and home incoming parameters strictly ONCE
        write_u8(0x55);                                // push rbp
        write_u8(0x48); write_u8(0x89); write_u8(0xE5); // mov rbp, rsp
        write_u8(0x48); write_u8(0x81); write_u8(0xEC); write_u32(frame_size); // sub rsp, frame_size

        // Zero out initial scratch slots (0..3)
        write_u8(0x48); write_u8(0x31); write_u8(0xC0); // xor rax, rax
        emit_store_rax_to_slot(0u);
        emit_store_rax_to_slot(1u);
        emit_store_rax_to_slot(2u);
        emit_store_rax_to_slot(3u);

        if (count > 0 && stream.get_node(0).opcode == ir::OpCode::LABEL) {
            const auto& fn_hdr = stream.get_node(0);
            if (fn_hdr.op_a != ir::INVALID_SLOT) emit_store_rcx_to_slot(fn_hdr.op_a);
            if (fn_hdr.op_b != ir::INVALID_SLOT) emit_store_rdx_to_slot(fn_hdr.op_b);
            if (fn_hdr.op_c != ir::INVALID_SLOT) emit_store_r8_to_slot(fn_hdr.op_c);
            if (fn_hdr.logic_flags != ir::INVALID_SLOT) emit_store_r9_to_slot(fn_hdr.logic_flags);
            const auto fn_overflow = stream.get_call_overflow_args(0);
            for (size_t k = 0; k < fn_overflow.size(); ++k) {
                if (fn_overflow[k] != ir::INVALID_SLOT) {
                    write_u8(0x48); write_u8(0x8B); write_u8(0x85); write_i32(static_cast<int32_t>(48 + k * 8)); // mov rax, [rbp + 48 + k*8]
                    emit_store_rax_to_slot(fn_overflow[k]);
                }
            }
        }

        // 2. Linear Node Lowering
        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = offset;
            const auto& node = stream.get_node(i);
            using namespace dgtl::isa::lowering;
            const MachineForm form = generated::classify_machine_form(node.opcode);

            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                if (node.op_a != ir::INVALID_SLOT) emit_mov_rcx_from_slot(node.op_a);
                else { write_u8(0x48); write_u8(0x31); write_u8(0xC9); } // xor rcx, rcx

                if (node.op_b != ir::INVALID_SLOT) emit_mov_rdx_from_slot(node.op_b);
                else { write_u8(0x48); write_u8(0x31); write_u8(0xD2); } // xor rdx, rdx

                if (node.op_c != ir::INVALID_SLOT) emit_mov_r8_from_slot(node.op_c);
                else { write_u8(0x4D); write_u8(0x31); write_u8(0xC0); } // xor r8, r8

                if (node.logic_flags != ir::INVALID_SLOT) {
                    emit_mov_r9_from_slot(node.logic_flags);
                } else {
                    write_u8(0x4D); write_u8(0x31); write_u8(0xC9); // xor r9, r9
                }

                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx

                write_u8(0x48); write_u8(0x83); write_u8(0xEC); write_u8(0x20); // sub rsp, 32

                uintptr_t rt_target_addr = 0;
                if (out_relocs == nullptr) {
                    if (!slot_map.empty()) {
                        auto it = slot_map.find(static_cast<uint32_t>(node.opcode));
                        if (it != slot_map.end()) {
                            rt_target_addr = it->second;
                        }
                    }
                    if (rt_target_addr == 0 && node.intent_hash > 0x7FFF00000000ULL) {
                        rt_target_addr = static_cast<uintptr_t>(node.intent_hash);
                    }
                }

                if (rt_target_addr != 0) {
                    write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(rt_target_addr));
                    write_u8(0xFF); write_u8(0xD0); // call rax
                } else {
                    write_u8(0xE8); // call rel32
                    const size_t reloc_disp_offset = offset;
                    write_u8(0x00); write_u8(0x00); write_u8(0x00); write_u8(0x00);
                    if (out_relocs != nullptr) {
                        const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                        if (symbol != nullptr) {
                            out_relocs->push_back({ static_cast<uint32_t>(reloc_disp_offset), std::string(symbol) });
                        }
                    }
                }
                write_u8(0x48); write_u8(0x83); write_u8(0xC4); write_u8(0x20); // add rsp, 32

                if (node.result_id != ir::INVALID_SLOT) emit_store_rax_to_slot(node.result_id);
                else write_u8(0x90);
                continue;
            }

            switch (form) {
                case MachineForm::NoOp:
                    if (node.opcode == ir::OpCode::LABEL) {
                        const bool is_func_header = (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                        if (is_func_header && i > 0) {
                            if (node.op_a != ir::INVALID_SLOT) emit_store_rcx_to_slot(node.op_a);
                            if (node.op_b != ir::INVALID_SLOT) emit_store_rdx_to_slot(node.op_b);
                            if (node.op_c != ir::INVALID_SLOT) emit_store_r8_to_slot(node.op_c);
                            if (node.logic_flags != ir::INVALID_SLOT) emit_store_r9_to_slot(node.logic_flags);
                            const auto fn_overflow = stream.get_call_overflow_args(i);
                            for (size_t k = 0; k < fn_overflow.size(); ++k) {
                                if (fn_overflow[k] != ir::INVALID_SLOT) {
                                    write_u8(0x48); write_u8(0x8B); write_u8(0x85); write_i32(static_cast<int32_t>(48 + k * 8));
                                    emit_store_rax_to_slot(fn_overflow[k]);
                                }
                            }
                        }
                    } else {
                        write_u8(0x90);
                    }
                    break;

                case MachineForm::Move:
                case MachineForm::LoadStack:
                case MachineForm::StoreStack: {
                    if (form == MachineForm::StoreStack) {
                        emit_mov_rax_from_slot(node.op_b);
                        emit_store_rax_to_slot(node.op_a);
                    } else {
                        emit_mov_rax_from_slot(node.op_a);
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::LoadImmediate: {
                    uint64_t imm = (static_cast<uint64_t>(node.op_b) << 32u) | (static_cast<uint64_t>(node.op_a) & 0xFFFFFFFFu);
                    if (node.opcode == ir::OpCode::STRING_CONST) {
                        imm = static_cast<uint64_t>(node.op_a);
                    }

                    const bool is_float_const = (node.opcode == ir::OpCode::CONST && ((node.flags & ir::Flags::IS_SPEC_CONST) != 0));

                    if (is_float_const) {
                        write_u8(0x48); write_u8(0xB8); write_u64(imm); // movabs rax, imm64
                        write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC0); // movq xmm0, rax
                        write_u8(0xF2); write_u8(0x0F); write_u8(0x11); write_u8(0x85); // movsd [rbp-disp], xmm0
                        write_i32(slot_disp(node.result_id));
                    } else {
                        write_u8(0x48); write_u8(0xB8); write_u64(imm); // movabs rax, imm64
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::IntegerBinary32:
                case MachineForm::IntegerBinary64: {
                    const bool is64 = (form == MachineForm::IntegerBinary64);
                    generated::X64IntegerBinary32Kind kind = generated::X64IntegerBinary32Kind::None;
                    if (is64) {
                        switch (generated::integer_binary64_kind(node.opcode)) {
                            case generated::X64IntegerBinary64Kind::Add: kind = generated::X64IntegerBinary32Kind::Add; break;
                            case generated::X64IntegerBinary64Kind::Sub: kind = generated::X64IntegerBinary32Kind::Sub; break;
                            case generated::X64IntegerBinary64Kind::Mul: kind = generated::X64IntegerBinary32Kind::Mul; break;
                            case generated::X64IntegerBinary64Kind::Div: kind = generated::X64IntegerBinary32Kind::Div; break;
                            case generated::X64IntegerBinary64Kind::Mod: kind = generated::X64IntegerBinary32Kind::Mod; break;
                            case generated::X64IntegerBinary64Kind::And: kind = generated::X64IntegerBinary32Kind::And; break;
                            case generated::X64IntegerBinary64Kind::Or:  kind = generated::X64IntegerBinary32Kind::Or;  break;
                            case generated::X64IntegerBinary64Kind::Xor: kind = generated::X64IntegerBinary32Kind::Xor; break;
                            case generated::X64IntegerBinary64Kind::Shl: kind = generated::X64IntegerBinary32Kind::Shl; break;
                            case generated::X64IntegerBinary64Kind::Shr: kind = generated::X64IntegerBinary32Kind::Shr; break;
                            default: break;
                        }
                    } else {
                        kind = generated::integer_binary32_kind(node.opcode);
                    }

                    if (kind == generated::X64IntegerBinary32Kind::Shl || 
                        kind == generated::X64IntegerBinary32Kind::Shr) {
                        emit_mov_rax_from_slot(node.op_a);
                        emit_mov_rcx_from_slot(node.op_b);
                        if (is64) write_u8(0x48);
                        write_u8(0xD3);
                        write_u8(kind == generated::X64IntegerBinary32Kind::Shl ? 0xE0 : 0xF8);
                        emit_store_rax_to_slot(node.result_id);
                    }
                    else if (kind == generated::X64IntegerBinary32Kind::Div || 
                             kind == generated::X64IntegerBinary32Kind::Mod) {
                        auto const_it = (node.op_b != ir::INVALID_SLOT) ? known_constants.find(node.op_b) : known_constants.end();
                        bool has_const_divisor = (const_it != known_constants.end() && const_it->second > 0);
                        int64_t divisor = has_const_divisor ? static_cast<int64_t>(const_it->second) : 0;
                        auto magic = (has_const_divisor && is64) ? compute_magic_s64(divisor) : MagicMultiplierS64{};

                        if (is64 && has_const_divisor && divisor == 1) {
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_mov_rax_from_slot(node.op_a);
                                emit_store_rax_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0x31); write_u8(0xC0); // xor rax, rax
                                emit_store_rax_to_slot(node.result_id);
                            }
                        }
                        else if (is64 && has_const_divisor && magic.valid && !magic.is_power_of_2) {
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0xB9); write_u64(magic.M);
                            write_u8(0x48); write_u8(0xF7); write_u8(0xE9);
                            if (magic.s > 0) {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xFA); write_u8(magic.s);
                            }
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0xC1); write_u8(0xF8); write_u8(0x3F);
                            write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_store_rdx_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0x89); write_u8(0xD0);
                                write_u8(0x48); write_u8(0xB9); write_u64(static_cast<uint64_t>(divisor));
                                write_u8(0x48); write_u8(0x0F); write_u8(0xAF); write_u8(0xC1);
                                emit_mov_rdx_from_slot(node.op_a);
                                write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                                emit_store_rdx_to_slot(node.result_id);
                            }
                        }
                        else if (is64 && has_const_divisor && magic.is_power_of_2 && divisor > 1) {
                            emit_mov_rax_from_slot(node.op_a);
                            write_u8(0x48); write_u8(0x99);
                            uint8_t k = magic.s;
                            if (k == 1) {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xEA); write_u8(0x3F);
                            } else if (k < 31) {
                                write_u8(0x48); write_u8(0x81); write_u8(0xE2); write_u32(static_cast<uint32_t>((1ULL << k) - 1));
                            } else {
                                write_u8(0x48); write_u8(0xB9); write_u64((1ULL << k) - 1);
                                write_u8(0x48); write_u8(0x21); write_u8(0xCA);
                            }
                            write_u8(0x48); write_u8(0x01); write_u8(0xD0);
                            write_u8(0x48); write_u8(0xC1); write_u8(0xF8); write_u8(k);
                            if (kind == generated::X64IntegerBinary32Kind::Div) {
                                emit_store_rax_to_slot(node.result_id);
                            } else {
                                write_u8(0x48); write_u8(0xC1); write_u8(0xE0); write_u8(k);
                                emit_mov_rdx_from_slot(node.op_a);
                                write_u8(0x48); write_u8(0x29); write_u8(0xC2);
                                emit_store_rdx_to_slot(node.result_id);
                            }
                        }
                        else {
                            emit_mov_rax_from_slot(node.op_a);
                            emit_mov_rcx_from_slot(node.op_b);
                            if (is64) {
                                write_u8(0x48); write_u8(0x85); write_u8(0xC9); // test rcx, rcx
                                write_u8(0x75); write_u8(0x06);                 // jnz +6
                                write_u8(0x31); write_u8(0xC0);                 // xor eax, eax
                                write_u8(0x31); write_u8(0xD2);                 // xor edx, edx
                                write_u8(0xEB); write_u8(0x05);                 // jmp +5
                                write_u8(0x48); write_u8(0x99);                 // cqo
                                write_u8(0x48); write_u8(0xF7); write_u8(0xF9); // idiv rcx
                            } else {
                                write_u8(0x85); write_u8(0xC9);                 // test ecx, ecx
                                write_u8(0x75); write_u8(0x06);                 // jnz +6
                                write_u8(0x31); write_u8(0xC0);                 // xor eax, eax
                                write_u8(0x31); write_u8(0xD2);                 // xor edx, edx
                                write_u8(0xEB); write_u8(0x03);                 // jmp +3
                                write_u8(0x99);                                 // cdq
                                write_u8(0xF7); write_u8(0xF9);                 // idiv ecx
                            }
                            if (kind == generated::X64IntegerBinary32Kind::Div)
                                emit_store_rax_to_slot(node.result_id);
                            else emit_store_rdx_to_slot(node.result_id);
                        }
                    }
                    else {
                        emit_mov_rax_from_slot(node.op_a);
                        if (is64) write_u8(0x48);
                        if (kind == generated::X64IntegerBinary32Kind::Add) {
                            write_u8(0x03); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Sub) {
                            write_u8(0x2B); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::And) {
                            write_u8(0x23); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Or) {
                            write_u8(0x0B); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Xor) {
                            write_u8(0x33); write_u8(0x85);
                        } else if (kind == generated::X64IntegerBinary32Kind::Mul) {
                            write_u8(0x0F); write_u8(0xAF); write_u8(0x85);
                        }
                        write_i32(slot_disp(node.op_b));
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                case MachineForm::FloatBinary32:
                case MachineForm::FloatBinary64: {
                    const bool is64 = (form == MachineForm::FloatBinary64);
                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F); write_u8(0x10); write_u8(0x85);
                    write_i32(slot_disp(node.op_a));

                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F);
                    if (is64) {
                        const auto kind = generated::float_binary64_kind(node.opcode);
                        using K = generated::X64FloatBinary64Kind;
                        if (kind == K::Add) write_u8(0x58);
                        else if (kind == K::Sub) write_u8(0x5C);
                        else if (kind == K::Mul) write_u8(0x59);
                        else if (kind == K::Div) write_u8(0x5E);
                        else write_u8(0x58);
                    } else {
                        const auto kind = generated::float_binary32_kind(node.opcode);
                        using K = generated::X64FloatBinary32Kind;
                        if (kind == K::Add) write_u8(0x58);
                        else if (kind == K::Sub) write_u8(0x5C);
                        else if (kind == K::Mul) write_u8(0x59);
                        else if (kind == K::Div) write_u8(0x5E);
                        else write_u8(0x58);
                    }
                    write_u8(0x85);
                    write_i32(slot_disp(node.op_b));

                    write_u8(is64 ? 0xF2 : 0xF3);
                    write_u8(0x0F); write_u8(0x11); write_u8(0x85);
                    write_i32(slot_disp(node.result_id));
                    break;
                }

                case MachineForm::Compare32: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x3B); write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x0F);
                    switch (node.opcode) {
                        case ir::OpCode::CMP_EQ_I32: write_u8(0x94); break;
                        case ir::OpCode::CMP_NE_I32: write_u8(0x95); break;
                        case ir::OpCode::CMP_LT_I32: write_u8(0x9C); break;
                        case ir::OpCode::CMP_LE_I32: write_u8(0x9E); break;
                        case ir::OpCode::CMP_GT_I32: write_u8(0x9F); break;
                        case ir::OpCode::CMP_GE_I32: write_u8(0x9D); break;
                        default: write_u8(0x9E); break;
                    }
                    write_u8(0xC0);
                    write_u8(0x0F); write_u8(0xB6); write_u8(0xC0);
                    emit_store_rax_to_slot(node.result_id);
                    break;
                }

                case MachineForm::Call: {
                    if (node.op_b != ir::INVALID_SLOT) {
                        emit_mov_rcx_from_slot(node.op_b);
                    } else { 
                        write_u8(0x48); write_u8(0x31); write_u8(0xC9); // xor rcx, rcx
                    }

                    if (node.op_c != ir::INVALID_SLOT) {
                        emit_mov_rdx_from_slot(node.op_c);
                    } else { 
                        write_u8(0x48); write_u8(0x31); write_u8(0xD2); // xor rdx, rdx
                    }

                    if (node.logic_flags != ir::INVALID_SLOT) {
                        emit_mov_r8_from_slot(node.logic_flags);
                    } else { 
                        write_u8(0x4D); write_u8(0x31); write_u8(0xC0); // xor r8, r8
                    }

                    const auto overflow = stream.get_call_overflow_args(i);
                    if (!overflow.empty() && overflow[0] != ir::INVALID_SLOT) {
                        emit_mov_r9_from_slot(overflow[0]);
                    } else {
                        write_u8(0x4D); write_u8(0x31); write_u8(0xC9); // xor r9, r9
                    }

                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC1); // movq xmm0, rcx
                    write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xCA); // movq xmm1, rdx
                    write_u8(0x66); write_u8(0x49); write_u8(0x0F); write_u8(0x6E); write_u8(0xD0); // movq xmm2, r8
                    write_u8(0x66); write_u8(0x49); write_u8(0x0F); write_u8(0x6E); write_u8(0xD9); // movq xmm3, r9

                    const size_t num_stack_args = (overflow.size() > 1) ? (overflow.size() - 1) : 0;
                    const size_t stack_alloc = 32 + ((num_stack_args + 1) / 2) * 16;
                    write_u8(0x48); write_u8(0x81); write_u8(0xEC); write_u32(static_cast<uint32_t>(stack_alloc)); // sub rsp, stack_alloc

                    for (size_t k = 1; k < overflow.size(); ++k) {
                        if (overflow[k] != ir::INVALID_SLOT) {
                            emit_mov_rax_from_slot(overflow[k]);
                            write_u8(0x48); write_u8(0x89); write_u8(0x84); write_u8(0x24);
                            write_i32(static_cast<int32_t>(32 + (k - 1) * 8)); // mov [rsp + 32 + (k-1)*8], rax
                        }
                    }

                    uintptr_t target_addr = 0;
                    if (out_relocs == nullptr) {
                        if (!slot_map.empty()) {
                            auto it = slot_map.find(static_cast<uint32_t>(node.op_a));
                            if (it != slot_map.end() && it->second != 0) {
                                target_addr = it->second;
                            }
                        }
                        if (target_addr == 0 && node.op_a > 0x7FFF00000000ULL) {
                            target_addr = static_cast<uintptr_t>(node.op_a);
                        }
                        if (target_addr == 0 && node.intent_hash > 0x7FFF00000000ULL) {
                            target_addr = static_cast<uintptr_t>(node.intent_hash);
                        }
                    }

                    if (target_addr != 0) {
                        write_u8(0x48); write_u8(0xB8); write_u64(static_cast<uint64_t>(target_addr));
                        write_u8(0xFF); write_u8(0xD0); // call rax
                    } else {
                        write_u8(0xE8); // call rel32
                        const size_t patch_disp_offset = offset;
                        write_u32(0);
                        const size_t patch_source_end = offset;
                        bool is_external_reloc = false;
                        if (node.opcode == ir::OpCode::STRING_LENGTH) {
                            if (out_relocs != nullptr) out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), "__dgtl_rt_string_length" });
                            is_external_reloc = true;
                        } else if (node.opcode == ir::OpCode::STRING_COMPARE) {
                            if (out_relocs != nullptr) out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), "__dgtl_rt_string_compare" });
                            is_external_reloc = true;
                        } else if (node.opcode == ir::OpCode::STRING_CONCAT) {
                            if (out_relocs != nullptr) out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), "__dgtl_rt_string_concat" });
                            is_external_reloc = true;
                        } else if (node.opcode == ir::OpCode::STRING_SUBSTRING) {
                            if (out_relocs != nullptr) out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), "__dgtl_rt_string_substring" });
                            is_external_reloc = true;
                        } else if (out_relocs != nullptr) {
                            auto it = slot_to_callee.find(node.op_a);
                            if (it != slot_to_callee.end() && !it->second.empty()) {
                                out_relocs->push_back({ static_cast<uint32_t>(patch_disp_offset), it->second });
                                is_external_reloc = true;
                            }
                        }
                        if (!is_external_reloc) {
                            patches.push_back({patch_disp_offset, patch_source_end, resolve_target(node.op_a)});
                        }
                    }
                    write_u8(0x48); write_u8(0x81); write_u8(0xC4); write_u32(static_cast<uint32_t>(stack_alloc)); // add rsp, stack_alloc

                    if (node.result_id != ir::INVALID_SLOT) {
                        bool returns_in_xmm0 = false;
                        auto it_callee = slot_to_callee.find(node.op_a);
                        if (it_callee != slot_to_callee.end() && !it_callee->second.empty()) {
                            const std::string& name = it_callee->second;
                            if (name.find("_bits") != std::string::npos) {
                                returns_in_xmm0 = false;
                            } else if (name == "pow" || name == "sqrt" || name == "sin" ||
                                       name == "cos" || name == "tan" || name == "exp" ||
                                       name == "log" || name == "fabs" || name == "floor" ||
                                       name == "ceil") {
                                returns_in_xmm0 = true;
                            } else {
                                const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                                returns_in_xmm0 = ir::is_floating_type_code(tc);
                            }
                        } else {
                            const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                            returns_in_xmm0 = ir::is_floating_type_code(tc);
                        }

                        if (returns_in_xmm0) {
                            write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x7E); write_u8(0xC0); // movq rax, xmm0
                        }
                        emit_store_rax_to_slot(node.result_id);
                    } else {
                        write_u8(0x90);
                    }
                    continue;
                }

                case MachineForm::Goto: {
                    write_u8(0xE9);
                    const uint32_t target_slot = (node.op_a != ir::INVALID_SLOT) ? node.op_a : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCond: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x85); write_u8(0xC0); // test rax, rax
                    write_u8(0x0F);
                    write_u8(node.opcode == ir::OpCode::BRANCH_UNLIKELY ? 0x84 : 0x85);
                    const uint32_t target_slot = (node.op_b != ir::INVALID_SLOT) ? node.op_b : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::BranchCompare: {
                    emit_mov_rax_from_slot(node.op_a);
                    write_u8(0x48); write_u8(0x3B); write_u8(0x85); write_i32(slot_disp(node.op_b));
                    write_u8(0x0F);
                    uint8_t bcond = 0x85;
                    uint32_t cmp_kind = static_cast<uint32_t>(node.opcode);
                    if (cmp_kind == static_cast<uint32_t>(ir::OpCode::BRANCH_COMPARE_FUSED)) {
                        cmp_kind = node.flags & 0xFFu;
                        if (cmp_kind == 0) cmp_kind = node.logic_flags;
                    }
                    switch (cmp_kind) {
                        case static_cast<uint32_t>(ir::OpCode::CMP_EQ_I32): bcond = 0x84; break;
                        case static_cast<uint32_t>(ir::OpCode::CMP_NE_I32): bcond = 0x85; break;
                        case static_cast<uint32_t>(ir::OpCode::CMP_LT_I32): bcond = 0x8C; break;
                        case static_cast<uint32_t>(ir::OpCode::CMP_LE_I32): bcond = 0x8E; break;
                        case static_cast<uint32_t>(ir::OpCode::CMP_GT_I32): bcond = 0x8F; break;
                        case static_cast<uint32_t>(ir::OpCode::CMP_GE_I32): bcond = 0x8D; break;
                        default: bcond = 0x85; break;
                    }
                    write_u8(bcond);
                    const uint32_t target_slot = (node.op_c != ir::INVALID_SLOT) ? node.op_c : node.result_id;
                    patches.push_back({offset, offset + 4, resolve_target(target_slot)});
                    write_u32(0);
                    break;
                }

                case MachineForm::Return: {
                    if (node.op_a != ir::INVALID_SLOT) {
                        emit_mov_rax_from_slot(node.op_a);
                        write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC0); // movq xmm0, rax (Dual ABI Return)
                    } else if (node.result_id != ir::INVALID_SLOT) {
                        emit_mov_rax_from_slot(node.result_id);
                        write_u8(0x66); write_u8(0x48); write_u8(0x0F); write_u8(0x6E); write_u8(0xC0);
                    } else {
                        write_u8(0x31); write_u8(0xC0);
                        write_u8(0x66); write_u8(0x0F); write_u8(0xEF); write_u8(0xC0); // pxor xmm0, xmm0
                    }
                    write_u8(0xC9); write_u8(0xC3); // leave; ret
                    break;
                }

                case MachineForm::Syscall: {
                    emit_mov_rax_from_slot(node.op_a);
                    if (node.op_b != ir::INVALID_SLOT) {
                        emit_mov_reg_from_slot(0x48, 0xBD, node.op_b);
                    } else {
                        write_u8(0x48); write_u8(0x31); write_u8(0xFF);
                    }
                    if (node.op_c != ir::INVALID_SLOT) {
                        emit_mov_reg_from_slot(0x48, 0xB5, node.op_c);
                    } else {
                        write_u8(0x48); write_u8(0x31); write_u8(0xF6);
                    }
                    write_u8(0x0F); write_u8(0x05); // syscall
                    if (node.result_id != ir::INVALID_SLOT) {
                        emit_store_rax_to_slot(node.result_id);
                    }
                    break;
                }

                default:
                    record_fallback_opcode(i, node.opcode);
                    write_u8(0x90);
                    break;
            }
        }

        // Defensive fallthrough safety: guarantee clean leave; ret if last instruction was not Return
        const auto last_op = (count > 0) ? stream.get_node(count - 1).opcode : ir::OpCode::NOP;
        if (count == 0 || (last_op != ir::OpCode::RET && last_op != ir::OpCode::RETURN)) {
            write_u8(0x31); write_u8(0xC0); // xor eax, eax
            write_u8(0xC9); write_u8(0xC3); // leave; ret
        }

        node_offsets[count] = offset;
        for (const auto& p : patches) {
            if (p.target_node >= count) {
                continue;
            }
            const size_t tgt_byte = node_offsets[p.target_node];
            const int64_t rel64 = static_cast<int64_t>(tgt_byte) - static_cast<int64_t>(p.source_end);
            const int32_t rel32 = static_cast<int32_t>(rel64);
            std::memcpy(dest + p.disp_offset, &rel32, sizeof(rel32));
        }

        if (emitted_size) *emitted_size = offset;
        return true;
    }

    bool build_module_coff_with_relocs(
        const std::vector<std::pair<std::string, ir::IRStream>>& module_streams,
        std::vector<uint8_t>& out,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        std::span<const uint8_t> rdata_payload = {})
    {
        using namespace coff;
        static constexpr uint16_t kCoffRelAmd64Rel32 = 4;

        std::vector<uint8_t> master_text_bytes;
        master_text_bytes.reserve(1024 * 1024);

        struct ModuleFunc { std::string name; size_t text_offset = 0; size_t size = 0; };
        std::vector<ModuleFunc> functions;
        std::vector<CoffCallReloc> master_relocs;
        std::unordered_map<std::string, uint32_t> internal_func_index;

        internal_func_index.reserve(module_streams.size());

        for (const auto& [name, stream] : module_streams) {
            if (stream.is_null() || stream.size() == 0) continue;
            
            size_t func_start_offset = master_text_bytes.size();
            size_t capacity_needed = stream.size() * 64 + 256;
            master_text_bytes.resize(func_start_offset + capacity_needed);
            
            size_t emitted = 0;
            std::vector<CoffCallReloc> local_relocs;
            if (!emit_aot_machine_code(master_text_bytes.data() + func_start_offset, capacity_needed, stream, &emitted, {}, &local_relocs, slot_to_callee)) {
                return false;
            }
            
            for (auto& r : local_relocs) {
                r.text_offset += static_cast<uint32_t>(func_start_offset);
                master_relocs.push_back(std::move(r));
            }
            
            master_text_bytes.resize(func_start_offset + emitted);
            internal_func_index.emplace(name, static_cast<uint32_t>(functions.size()));
            functions.push_back({name, func_start_offset, emitted});
        }

        if (master_text_bytes.empty()) return false;

        const bool has_rdata = !rdata_payload.empty();
        const size_t num_sections = has_rdata ? 2 : 1;

        std::vector<std::string> extern_names;
        std::unordered_map<std::string, uint32_t> extern_index;
        extern_index.reserve(master_relocs.size());

        for (const auto& r : master_relocs) {
            if (internal_func_index.find(r.callee_name) != internal_func_index.end()) {
                continue;
            }
            auto [it, inserted] = extern_index.emplace(r.callee_name, 0u);
            if (inserted) {
                extern_names.push_back(r.callee_name);
            }
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(extern_names.size()); ++i) {
            extern_index[extern_names[i]] = i;
        }

        std::vector<CoffReloc> coff_relocs;
        coff_relocs.reserve(master_relocs.size());

        for (const auto& r : master_relocs) {
            uint32_t offset = r.text_offset;
            if (offset + sizeof(uint32_t) > master_text_bytes.size()) {
                if (master_text_bytes.size() >= 4) {
                    offset = static_cast<uint32_t>(master_text_bytes.size() - 4);
                } else {
                    continue;
                }
            }
            uint32_t sym_idx = static_cast<uint32_t>(num_sections + (has_rdata ? 1 : 0));
            const auto it_internal = internal_func_index.find(r.callee_name);
            if (it_internal != internal_func_index.end()) {
                sym_idx += it_internal->second;
            } else {
                sym_idx += static_cast<uint32_t>(functions.size());
                const auto it_external = extern_index.find(r.callee_name);
                if (it_external == extern_index.end()) {
                    continue;
                }
                sym_idx += it_external->second;
            }
            coff_relocs.push_back({offset, sym_idx, kCoffRelAmd64Rel32});
        }
        if (coff_relocs.size() > std::numeric_limits<uint16_t>::max()) {
            return false;
        }

        const size_t raw_text_sz = master_text_bytes.size();
        const size_t aligned_text_sz = (raw_text_sz + 15) & ~size_t(15);
        master_text_bytes.resize(aligned_text_sz, 0x90);

        std::vector<uint8_t> aligned_rdata;
        if (has_rdata) {
            aligned_rdata.assign(rdata_payload.begin(), rdata_payload.end());
            const size_t aligned_rdata_sz = (aligned_rdata.size() + 15) & ~size_t(15);
            aligned_rdata.resize(aligned_rdata_sz, 0x00);
        }

        const uint32_t hdr_off       = 0;
        const uint32_t sec_hdr_off   = static_cast<uint32_t>(hdr_off + sizeof(FileHeader));
        const uint32_t text_sec_off  = static_cast<uint32_t>(sec_hdr_off + num_sections * sizeof(SectionHeader));
        const uint32_t reloc_off     = static_cast<uint32_t>(text_sec_off + master_text_bytes.size());
        const uint32_t rdata_sec_off = static_cast<uint32_t>(reloc_off + coff_relocs.size() * sizeof(Relocation));
        const uint32_t symtab_off    = static_cast<uint32_t>(rdata_sec_off + aligned_rdata.size());

        const size_t num_symbols = num_sections + (has_rdata ? 1 : 0) + functions.size() + extern_names.size();

        std::string strtab_data;
        strtab_data.resize(4, '\0');

        auto add_string = [&](const std::string& name) -> std::pair<bool, uint32_t> {
            if (name.size() <= 8) return {false, 0};
            uint32_t off = static_cast<uint32_t>(strtab_data.size());
            strtab_data.append(name.data(), name.size() + 1);
            return {true, off};
        };

        std::vector<std::pair<bool, uint32_t>> func_str_infos;
        for (const auto& f : functions) func_str_infos.push_back(add_string(f.name));

        std::vector<std::pair<bool, uint32_t>> extern_str_infos;
        for (const auto& name : extern_names) extern_str_infos.push_back(add_string(name));

        const std::pair<bool, uint32_t> rdata_str = has_rdata ? add_string("__dgtl_rdata_strings_blob") : std::pair<bool, uint32_t>{false, 0};

        uint32_t strtab_len = static_cast<uint32_t>(strtab_data.size());
        std::memcpy(strtab_data.data(), &strtab_len, sizeof(uint32_t));

        const size_t total_file_size = symtab_off + num_symbols * sizeof(Symbol) + strtab_data.size();
        out.assign(total_file_size, 0);

        auto* file_hdr = reinterpret_cast<FileHeader*>(out.data() + hdr_off);
        file_hdr->Machine              = MACHINE_AMD64;
        file_hdr->NumberOfSections     = static_cast<uint16_t>(num_sections);
        file_hdr->TimeDateStamp        = 0;
        file_hdr->PointerToSymbolTable = symtab_off;
        file_hdr->NumberOfSymbols      = static_cast<uint32_t>(num_symbols);
        file_hdr->SizeOfOptionalHeader = 0;
        file_hdr->Characteristics      = 0;

        auto* sec_hdrs = reinterpret_cast<SectionHeader*>(out.data() + sec_hdr_off);
        
        // Section 1: .text
        std::memcpy(sec_hdrs[0].Name, ".text\0\0\0", 8);
        sec_hdrs[0].VirtualSize          = 0;
        sec_hdrs[0].VirtualAddress       = 0;
        sec_hdrs[0].SizeOfRawData        = static_cast<uint32_t>(master_text_bytes.size());
        sec_hdrs[0].PointerToRawData     = text_sec_off;
        sec_hdrs[0].PointerToRelocations = coff_relocs.empty() ? 0 : reloc_off;
        sec_hdrs[0].PointerToLinenumbers = 0;
        sec_hdrs[0].NumberOfRelocations  = static_cast<uint16_t>(coff_relocs.size());
        sec_hdrs[0].NumberOfLinenumbers  = 0;
        sec_hdrs[0].Characteristics      = SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ | SCN_ALIGN_16BYTES;

        // Section 2: .rdata (if present)
        if (has_rdata) {
            std::memcpy(sec_hdrs[1].Name, ".rdata\0\0", 8);
            sec_hdrs[1].VirtualSize          = 0;
            sec_hdrs[1].VirtualAddress       = 0;
            sec_hdrs[1].SizeOfRawData        = static_cast<uint32_t>(aligned_rdata.size());
            sec_hdrs[1].PointerToRawData     = rdata_sec_off;
            sec_hdrs[1].PointerToRelocations = 0;
            sec_hdrs[1].PointerToLinenumbers = 0;
            sec_hdrs[1].NumberOfRelocations  = 0;
            sec_hdrs[1].NumberOfLinenumbers  = 0;
            sec_hdrs[1].Characteristics      = SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ | SCN_ALIGN_16BYTES;
        }

        std::memcpy(out.data() + text_sec_off, master_text_bytes.data(), master_text_bytes.size());

        if (!coff_relocs.empty()) {
            auto* reloc_tbl = reinterpret_cast<Relocation*>(out.data() + reloc_off);
            for (size_t i = 0; i < coff_relocs.size(); ++i) {
                reloc_tbl[i].VirtualAddress   = coff_relocs[i].VirtualAddress;
                reloc_tbl[i].SymbolTableIndex = coff_relocs[i].SymbolTableIndex;
                reloc_tbl[i].Type             = coff_relocs[i].Type;
            }
        }

        if (has_rdata) {
            std::memcpy(out.data() + rdata_sec_off, aligned_rdata.data(), aligned_rdata.size());
        }

        auto fill_sym = [](Symbol& sym, const std::string& name, const std::pair<bool, uint32_t>& str_info,
                           uint32_t val, int16_t sec, uint8_t sclass) {
            if (str_info.first) {
                sym.N.LongName.Zeroes = 0;
                sym.N.LongName.Offset = str_info.second;
            } else {
                std::memset(sym.N.ShortName, 0, 8);
                std::memcpy(sym.N.ShortName, name.data(), std::min(name.size(), size_t(8)));
            }
            sym.Value              = val;
            sym.SectionNumber      = sec;
            sym.Type               = SYM_TYPE_FUNCTION;
            sym.StorageClass       = sclass;
            sym.NumberOfAuxSymbols = 0;
        };

        auto* sym_tbl = reinterpret_cast<Symbol*>(out.data() + symtab_off);
        fill_sym(sym_tbl[0], ".text", {false, 0}, 0, 1, SYM_CLASS_STATIC);
        size_t current_sym_idx = 1;
        if (has_rdata) {
            fill_sym(sym_tbl[current_sym_idx++], ".rdata", {false, 0}, 0, 2, SYM_CLASS_STATIC);
            fill_sym(sym_tbl[current_sym_idx++], "__dgtl_rdata_strings_blob", rdata_str, 0, 2, SYM_CLASS_EXTERNAL);
        }
        
        for (size_t i = 0; i < functions.size(); ++i) {
            fill_sym(sym_tbl[current_sym_idx++], functions[i].name, func_str_infos[i], 
                     static_cast<uint32_t>(functions[i].text_offset), 1, SYM_CLASS_EXTERNAL);
        }
        for (size_t i = 0; i < extern_names.size(); ++i) {
            fill_sym(sym_tbl[current_sym_idx++], extern_names[i], extern_str_infos[i], 
                     0, 0, SYM_CLASS_EXTERNAL);
        }

        std::memcpy(out.data() + symtab_off + num_symbols * sizeof(Symbol),
                    strtab_data.data(), strtab_data.size());

        return true;
    }

    bool build_coff_relocatable_binary_with_relocs(
        const ir::IRStream& stream,
        std::vector<uint8_t>& out,
        std::string_view entry_symbol_name = "DGTL_kernel_entry",
        const std::vector<CoffCallReloc>& relocs = {},
        const std::unordered_map<uint32_t, std::string>& slot_to_callee = {},
        std::span<const uint8_t> rdata_payload = {})
    {
        std::vector<std::pair<std::string, ir::IRStream>> single_module;
        single_module.emplace_back(std::string(entry_symbol_name), stream.clone());
        return build_module_coff_with_relocs(single_module, out, slot_to_callee, rdata_payload);
    }

    bool build_coff_relocatable_binary(const ir::IRStream& stream,
                                       std::vector<uint8_t>& out,
                                       std::string_view entry_symbol = "DGTL_kernel_entry") {
        return build_coff_relocatable_binary_with_relocs(stream, out, entry_symbol);
    }

    bool build_coff_archive_binary(const ir::IRStream& stream,
                                   std::vector<uint8_t>& out,
                                   std::string_view member_name = "module.obj",
                                   std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();
        std::vector<uint8_t> object_bytes;
        if (!build_coff_relocatable_binary(stream, object_bytes, entry_symbol)) return false;

        std::string sym_name(entry_symbol);
        sym_name.push_back('\0');

        const uint32_t num_symbols_be = to_big_endian_u32(1);
        const size_t first_member_content_size = 4 + 4 + sym_name.size();

        std::array<char, 60> lm_hdr{};
        std::fill(lm_hdr.begin(), lm_hdr.end(), ' ');
        std::snprintf(lm_hdr.data(), 17, "%-16s", "/");
        std::snprintf(lm_hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(lm_hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(lm_hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(lm_hdr.data() + 40, 9, "%-8o", 0);
        std::snprintf(lm_hdr.data() + 48, 11, "%-10u", static_cast<unsigned int>(first_member_content_size));
        lm_hdr[58] = '`';
        lm_hdr[59] = '\n';

        const size_t lm_pad = (first_member_content_size & 1U) ? 1 : 0;
        const size_t obj_member_offset = 8 + 60 + first_member_content_size + lm_pad;
        const uint32_t obj_offset_be = to_big_endian_u32(static_cast<uint32_t>(obj_member_offset));

        std::string obj_name(member_name);
        if (obj_name.size() > 15) obj_name.resize(15);
        obj_name.push_back('/');

        std::array<char, 60> obj_hdr{};
        std::fill(obj_hdr.begin(), obj_hdr.end(), ' ');
        std::snprintf(obj_hdr.data(), 17, "%-16s", obj_name.c_str());
        std::snprintf(obj_hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(obj_hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(obj_hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(obj_hdr.data() + 40, 9, "%-8o", 0644);
        std::snprintf(obj_hdr.data() + 48, 11, "%-10u", static_cast<unsigned int>(object_bytes.size()));
        obj_hdr[58] = '`';
        obj_hdr[59] = '\n';

        const size_t obj_pad = (object_bytes.size() & 1U) ? 1 : 0;

        static constexpr char kArMagic[] = "!<arch>\n";
        out.reserve(8 + 60 + first_member_content_size + lm_pad + 60 + object_bytes.size() + obj_pad);

        out.insert(out.end(), kArMagic, kArMagic + 8);
        out.insert(out.end(), lm_hdr.begin(), lm_hdr.end());
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&num_symbols_be),
                   reinterpret_cast<const uint8_t*>(&num_symbols_be) + 4);
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&obj_offset_be),
                   reinterpret_cast<const uint8_t*>(&obj_offset_be) + 4);
        out.insert(out.end(), sym_name.begin(), sym_name.end());
        if (lm_pad) out.push_back('\n');
        out.insert(out.end(), obj_hdr.begin(), obj_hdr.end());
        out.insert(out.end(), object_bytes.begin(), object_bytes.end());
        if (obj_pad) out.push_back('\n');

        return true;
    }

    bool build_pe_executable_buffer(const ir::IRStream& ir_stream, std::vector<uint8_t>& pe_image) {
        using namespace coff;
        if (ir_stream.is_null()) return false;

        constexpr uint32_t kFileAlign = 0x200;   // 512
        constexpr uint32_t kSecAlign  = 0x1000;  // 4096
        constexpr uint32_t kHeadersSize = 0x400; // 1024

        auto align_up_u32 = [](uint32_t val, uint32_t align) -> uint32_t {
            return (val + align - 1) & ~(align - 1);
        };

        const size_t raw_code_est = (ir_stream.size() * 64) + 1024;
        std::vector<uint8_t> machine_code(raw_code_est, 0);
        size_t emitted_size = 0;
        const bool ok = emit_aot_machine_code(machine_code.data(),
                                              machine_code.size(),
                                              ir_stream,
                                              &emitted_size);
        if (!ok) return false;
        machine_code.resize(emitted_size);

        // 1. Synthesize .text with Sovereign Entry Trampoline
        std::vector<uint8_t> text_full;
        const size_t trampoline_size = 24;
        text_full.resize(trampoline_size, 0x90);

        text_full[0] = 0x48; text_full[1] = 0x83; text_full[2] = 0xEC; text_full[3] = 0x28; // sub rsp, 40
        const int32_t call_main_disp = static_cast<int32_t>(trampoline_size - 9);
        text_full[4] = 0xE8;
        std::memcpy(&text_full[5], &call_main_disp, 4); // call machine_code
        text_full[9] = 0x89; text_full[10] = 0xC1; // mov ecx, eax
        text_full[11] = 0xFF; text_full[12] = 0x15;
        text_full[13] = 0x00; text_full[14] = 0x00; text_full[15] = 0x00; text_full[16] = 0x00; // placeholder for IAT displacement
        text_full[17] = 0x48; text_full[18] = 0x83; text_full[19] = 0xC4; text_full[20] = 0x28; // add rsp, 40
        text_full[21] = 0xC3; // ret
        text_full[22] = 0x90; text_full[23] = 0x90;

        text_full.insert(text_full.end(), machine_code.begin(), machine_code.end());

        const uint32_t text_vaddr = 0x1000;
        const uint32_t text_raw_size = align_up_u32(static_cast<uint32_t>(text_full.size()), kFileAlign);
        const uint32_t rdata_vaddr = text_vaddr + align_up_u32(static_cast<uint32_t>(text_full.size()), kSecAlign);

        // 2. Synthesize .rdata containing KERNEL32.DLL -> ExitProcess IAT
        std::vector<uint8_t> rdata_full;
        const uint32_t desc_offset = 0;
        const uint32_t iat_offset  = desc_offset + sizeof(ImportDescriptor) * 2;
        const uint32_t ilt_offset  = iat_offset + sizeof(uint64_t) * 2;
        const uint32_t hint_offset = ilt_offset + sizeof(uint64_t) * 2;
        const uint32_t dll_offset  = hint_offset + 14;

        const uint32_t desc_rva = rdata_vaddr + desc_offset;
        const uint32_t iat_rva  = rdata_vaddr + iat_offset;
        const uint32_t ilt_rva  = rdata_vaddr + ilt_offset;
        const uint32_t hint_rva = rdata_vaddr + hint_offset;
        const uint32_t dll_rva  = rdata_vaddr + dll_offset;

        // Backpatch IAT displacement into trampoline
        const int32_t iat_disp = static_cast<int32_t>(iat_rva - (text_vaddr + 17));
        std::memcpy(&text_full[13], &iat_disp, 4);

        ImportDescriptor desc[2]{};
        desc[0].OriginalFirstThunk = ilt_rva;
        desc[0].TimeDateStamp = 0;
        desc[0].ForwarderChain = 0;
        desc[0].Name = dll_rva;
        desc[0].FirstThunk = iat_rva;

        uint64_t iat_entries[2] = { hint_rva, 0 };
        uint64_t ilt_entries[2] = { hint_rva, 0 };

        uint8_t hint_name[14] = { 0x00, 0x00, 'E', 'x', 'i', 't', 'P', 'r', 'o', 'c', 'e', 's', 's', '\0' };
        const char dll_name[] = "KERNEL32.DLL\0";

        const size_t import_total_size = sizeof(desc) + sizeof(iat_entries) + sizeof(ilt_entries) + sizeof(hint_name) + sizeof(dll_name);
        rdata_full.resize(import_total_size + 16, 0);

        std::memcpy(rdata_full.data() + desc_offset, desc, sizeof(desc));
        std::memcpy(rdata_full.data() + iat_offset, iat_entries, sizeof(iat_entries));
        std::memcpy(rdata_full.data() + ilt_offset, ilt_entries, sizeof(ilt_entries));
        std::memcpy(rdata_full.data() + hint_offset, hint_name, sizeof(hint_name));
        std::memcpy(rdata_full.data() + dll_offset, dll_name, sizeof(dll_name));

        const uint32_t rdata_raw_size = align_up_u32(static_cast<uint32_t>(rdata_full.size()), kFileAlign);

        // 3. Assemble full PE32+ image
        pe_image.assign(kHeadersSize + text_raw_size + rdata_raw_size, 0);

        auto* dos = reinterpret_cast<ImageDosHeader*>(pe_image.data());
        dos->e_magic = 0x5A4D; // "MZ"
        dos->e_cblp = 0x0090;
        dos->e_cp = 0x0003;
        dos->e_cparhdr = 0x0004;
        dos->e_maxalloc = 0xFFFF;
        dos->e_sp = 0x00B8;
        dos->e_lfarlc = 0x0040;
        dos->e_lfanew = 0x0080; // 128

        static const uint8_t kDosStub[64] = {
            0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21,
            'T', 'h', 'i', 's', ' ', 'p', 'r', 'o', 'g', 'r', 'a', 'm', ' ',
            'c', 'a', 'n', 'n', 'o', 't', ' ', 'b', 'e', ' ', 'r', 'u', 'n', ' ',
            'i', 'n', ' ', 'D', 'O', 'S', ' ', 'm', 'o', 'd', 'e', '.', '\r', '\r', '\n', '$',
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        std::memcpy(pe_image.data() + 0x40, kDosStub, sizeof(kDosStub));

        *reinterpret_cast<uint32_t*>(pe_image.data() + 0x80) = 0x00004550; // "PE\0\0"

        auto* fhdr = reinterpret_cast<FileHeader*>(pe_image.data() + 0x84);
        fhdr->Machine = MACHINE_AMD64;
        fhdr->NumberOfSections = 2;
        fhdr->TimeDateStamp = 0x69A12345;
        fhdr->PointerToSymbolTable = 0;
        fhdr->NumberOfSymbols = 0;
        fhdr->SizeOfOptionalHeader = sizeof(ImageOptionalHeader64);
        fhdr->Characteristics = 0x0023; // IMAGE_FILE_RELOCS_STRIPPED | IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE

        auto* opt = reinterpret_cast<ImageOptionalHeader64*>(pe_image.data() + 0x98);
        opt->Magic = 0x020B; // PE32+
        opt->MajorLinkerVersion = 14;
        opt->MinorLinkerVersion = 0;
        opt->SizeOfCode = text_raw_size;
        opt->SizeOfInitializedData = rdata_raw_size;
        opt->SizeOfUninitializedData = 0;
        opt->AddressOfEntryPoint = text_vaddr;
        opt->BaseOfCode = text_vaddr;
        opt->ImageBase = 0x0000000140000000ULL;
        opt->SectionAlignment = kSecAlign;
        opt->FileAlignment = kFileAlign;
        opt->MajorOperatingSystemVersion = 6;
        opt->MinorOperatingSystemVersion = 0;
        opt->MajorImageVersion = 0;
        opt->MinorImageVersion = 0;
        opt->MajorSubsystemVersion = 6;
        opt->MinorSubsystemVersion = 0;
        opt->Win32VersionValue = 0;
        opt->SizeOfImage = rdata_vaddr + align_up_u32(static_cast<uint32_t>(rdata_full.size()), kSecAlign);
        opt->SizeOfHeaders = kHeadersSize;
        opt->CheckSum = 0;
        opt->Subsystem = 3; // IMAGE_SUBSYSTEM_WINDOWS_CUI
        opt->DllCharacteristics = 0x8100; // IMAGE_DLLCHARACTERISTICS_NX_COMPAT | IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE
        opt->SizeOfStackReserve = 0x4000000;
        opt->SizeOfStackCommit = 0x10000;
        opt->SizeOfHeapReserve = 0x4000000;
        opt->SizeOfHeapCommit = 0x10000;
        opt->LoaderFlags = 0;
        opt->NumberOfRvaAndSizes = 16;
        opt->DataDirectory[1].VirtualAddress = desc_rva;
        opt->DataDirectory[1].Size = sizeof(desc);
        opt->DataDirectory[12].VirtualAddress = iat_rva;
        opt->DataDirectory[12].Size = sizeof(iat_entries);

        auto* sec_text = reinterpret_cast<SectionHeader*>(pe_image.data() + 0x98 + sizeof(ImageOptionalHeader64));
        std::memcpy(sec_text->Name, ".text\0\0\0", 8);
        sec_text->VirtualAddress = text_vaddr;
        sec_text->VirtualSize = static_cast<uint32_t>(text_full.size());
        sec_text->PointerToRawData = kHeadersSize;
        sec_text->SizeOfRawData = text_raw_size;
        sec_text->PointerToRelocations = 0;
        sec_text->PointerToLinenumbers = 0;
        sec_text->NumberOfRelocations = 0;
        sec_text->NumberOfLinenumbers = 0;
        sec_text->Characteristics = 0x60000020; // SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ

        auto* sec_rdata = reinterpret_cast<SectionHeader*>(pe_image.data() + 0x98 + sizeof(ImageOptionalHeader64) + sizeof(SectionHeader));
        std::memcpy(sec_rdata->Name, ".rdata\0\0", 8);
        sec_rdata->VirtualAddress = rdata_vaddr;
        sec_rdata->VirtualSize = static_cast<uint32_t>(rdata_full.size());
        sec_rdata->PointerToRawData = kHeadersSize + text_raw_size;
        sec_rdata->SizeOfRawData = rdata_raw_size;
        sec_rdata->PointerToRelocations = 0;
        sec_rdata->PointerToLinenumbers = 0;
        sec_rdata->NumberOfRelocations = 0;
        sec_rdata->NumberOfLinenumbers = 0;
        sec_rdata->Characteristics = 0xC0000040; // SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ | SCN_MEM_WRITE

        std::memcpy(pe_image.data() + kHeadersSize, text_full.data(), text_full.size());
        std::memcpy(pe_image.data() + kHeadersSize + text_raw_size, rdata_full.data(), rdata_full.size());

        return true;
    }

    bool build_pe_executable(std::string_view path, const ir::IRStream& ir_stream) {
        if (path.empty() || ir_stream.is_null()) return false;
        std::vector<uint8_t> pe_image;
        if (!build_pe_executable_buffer(ir_stream, pe_image)) return false;
        return write_binary_file(path, pe_image);
    }

    bool emit_pe_executable(std::string_view path, const ir::IRStream& ir_stream) {
        return build_pe_executable(path, ir_stream);
    }

    std::vector<CoffCallReloc> collect_call_relocs(
        const ir::IRStream& stream,
        const std::unordered_map<uint32_t, std::string>& slot_to_callee)
    {
        std::vector<CoffCallReloc> relocs;
        if (stream.is_null()) [[unlikely]] return relocs;
        relocs.reserve(stream.size() / 10);

        size_t text_offset = x64_prologue_size(stream);
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(node.opcode)) {
                const size_t rcx_size = (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) ? 7u : 3u;
                const size_t rdx_size = (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) ? 7u : 3u;
                const size_t r8_size  = (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) ? 7u : 3u;
                const size_t disp_offset = text_offset + rcx_size + rdx_size + r8_size + 15u;
                const char* symbol = dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(node.opcode);
                if (symbol != nullptr) {
                    relocs.push_back({static_cast<uint32_t>(disp_offset), std::string(symbol)});
                }
                text_offset += x64_node_size(node);
            } else {
                switch (node.opcode) {
                    case ir::OpCode::CALL: {
                        const size_t rcx_size = (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) ? 7u : 3u;
                        const size_t rdx_size = (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) ? 7u : 3u;
                        const size_t r8_size  = (node.logic_flags != ir::INVALID_SLOT && node.logic_flags != 0u) ? 7u : 3u;
                        const size_t disp_offset = text_offset + rcx_size + rdx_size + r8_size + 15u;
                        auto it = slot_to_callee.find(node.op_a);
                        if (it != slot_to_callee.end()) {
                            relocs.push_back({static_cast<uint32_t>(disp_offset), it->second});
                        }
                        text_offset += x64_node_size(node);
                        break;
                    }
                    default:
                        text_offset += x64_node_size(node);
                        break;
                }
            }
        }
        return relocs;
    }

    bool build_elf_relocatable_binary(const ir::IRStream& stream,
                                      std::vector<uint8_t>& out,
                                      std::string_view entry_symbol = "DGTL_kernel_entry") {
        out.clear();
        if (stream.is_null() || stream.size() == 0) return false;

        std::vector<uint8_t> text_bytes;
        text_bytes.resize(stream.size() * 64 + 256);
        size_t text_size = 0;
        if (!emit_aot_machine_code(text_bytes.data(), text_bytes.size(), stream, &text_size)) {
            return false;
        }
        text_bytes.resize(text_size);
        if (text_bytes.empty()) return false;

        std::string shstrtab;
        shstrtab.reserve(64);
        shstrtab.push_back('\0');
        const uint32_t sh_name_text = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".text";
        shstrtab.push_back('\0');
        const uint32_t sh_name_shstrtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".shstrtab";
        shstrtab.push_back('\0');
        const uint32_t sh_name_strtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".strtab";
        shstrtab.push_back('\0');
        const uint32_t sh_name_symtab = static_cast<uint32_t>(shstrtab.size());
        shstrtab += ".symtab";
        shstrtab.push_back('\0');

        std::string strtab;
        strtab.reserve(entry_symbol.size() + 2);
        strtab.push_back('\0');
        const uint32_t sym_name_off = static_cast<uint32_t>(strtab.size());
        strtab.append(entry_symbol.data(), entry_symbol.size());
        strtab.push_back('\0');

        std::array<elf::Elf64_Sym, 3> symtab{};
        symtab[0] = elf::Elf64_Sym{};
        symtab[1].st_name = 0;
        symtab[1].st_info = static_cast<unsigned char>(ELF64_ST_INFO(STB_LOCAL, STT_SECTION));
        symtab[1].st_other = 0;
        symtab[1].st_shndx = 1;
        symtab[1].st_value = 0;
        symtab[1].st_size = 0;

        symtab[2].st_name = sym_name_off;
        symtab[2].st_info = static_cast<unsigned char>(ELF64_ST_INFO(STB_GLOBAL, STT_FUNC));
        symtab[2].st_other = 0;
        symtab[2].st_shndx = 1;
        symtab[2].st_value = 0;
        symtab[2].st_size = text_bytes.size();

        const size_t ehdr_size = sizeof(elf::Elf64_Ehdr);
        const size_t text_off = align_up(ehdr_size, 16);
        const size_t shstrtab_off = align_up(text_off + text_bytes.size(), 1);
        const size_t strtab_off = align_up(shstrtab_off + shstrtab.size(), 1);
        const size_t symtab_off = align_up(strtab_off + strtab.size(), 8);
        const size_t shoff = align_up(symtab_off + sizeof(symtab), 8);

        const size_t section_count = 5;
        const size_t total_size = shoff + section_count * sizeof(elf::Elf64_Shdr);
        out.assign(total_size, 0);

        auto* ehdr = reinterpret_cast<elf::Elf64_Ehdr*>(out.data());
        std::memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
        ehdr->e_ident[4] = ELFCLASS64;
        ehdr->e_ident[5] = ELFDATA2LSB;
        ehdr->e_ident[6] = EV_CURRENT;
        ehdr->e_type = ET_REL;
        ehdr->e_machine = EM_X86_64;
        ehdr->e_version = EV_CURRENT;
        ehdr->e_entry = 0;
        ehdr->e_phoff = 0;
        ehdr->e_shoff = shoff;
        ehdr->e_flags = 0;
        ehdr->e_ehsize = sizeof(elf::Elf64_Ehdr);
        ehdr->e_phentsize = 0;
        ehdr->e_phnum = 0;
        ehdr->e_shentsize = sizeof(elf::Elf64_Shdr);
        ehdr->e_shnum = static_cast<uint16_t>(section_count);
        ehdr->e_shstrndx = 2;

        std::memcpy(out.data() + text_off, text_bytes.data(), text_bytes.size());
        std::memcpy(out.data() + shstrtab_off, shstrtab.data(), shstrtab.size());
        std::memcpy(out.data() + strtab_off, strtab.data(), strtab.size());
        std::memcpy(out.data() + symtab_off, symtab.data(), sizeof(symtab));

        auto* shdr = reinterpret_cast<elf::Elf64_Shdr*>(out.data() + shoff);
        shdr[0] = elf::Elf64_Shdr{};

        shdr[1].sh_name = sh_name_text;
        shdr[1].sh_type = SHT_PROGBITS;
        shdr[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        shdr[1].sh_addr = 0;
        shdr[1].sh_offset = text_off;
        shdr[1].sh_size = text_bytes.size();
        shdr[1].sh_link = 0;
        shdr[1].sh_info = 0;
        shdr[1].sh_addralign = 16;
        shdr[1].sh_entsize = 0;

        shdr[2].sh_name = sh_name_shstrtab;
        shdr[2].sh_type = SHT_STRTAB;
        shdr[2].sh_flags = 0;
        shdr[2].sh_addr = 0;
        shdr[2].sh_offset = shstrtab_off;
        shdr[2].sh_size = shstrtab.size();
        shdr[2].sh_link = 0;
        shdr[2].sh_info = 0;
        shdr[2].sh_addralign = 1;
        shdr[2].sh_entsize = 0;

        shdr[3].sh_name = sh_name_strtab;
        shdr[3].sh_type = SHT_STRTAB;
        shdr[3].sh_flags = 0;
        shdr[3].sh_addr = 0;
        shdr[3].sh_offset = strtab_off;
        shdr[3].sh_size = strtab.size();
        shdr[3].sh_link = 0;
        shdr[3].sh_info = 0;
        shdr[3].sh_addralign = 1;
        shdr[3].sh_entsize = 0;

        shdr[4].sh_name = sh_name_symtab;
        shdr[4].sh_type = SHT_SYMTAB;
        shdr[4].sh_flags = 0;
        shdr[4].sh_addr = 0;
        shdr[4].sh_offset = symtab_off;
        shdr[4].sh_size = sizeof(symtab);
        shdr[4].sh_link = 3;
        shdr[4].sh_info = 2;
        shdr[4].sh_addralign = 8;
        shdr[4].sh_entsize = sizeof(elf::Elf64_Sym);

        return true;
    }

    bool build_static_archive_binary(const ir::IRStream& stream,
                                     std::vector<uint8_t>& out,
                                     std::string_view member_name = "module.o") {
        out.clear();
        std::vector<uint8_t> object_bytes;
        if (!build_elf_relocatable_binary(stream, object_bytes, "DGTL_kernel_entry")) {
            return false;
        }

        std::string header_name(member_name);
        if (header_name.empty()) header_name = "module.o";
        if (header_name.size() > 15) header_name.resize(15);
        header_name.push_back('/');

        std::array<char, 60> hdr{};
        std::fill(hdr.begin(), hdr.end(), ' ');

        std::snprintf(hdr.data(), 17, "%-16s", header_name.c_str());
        std::snprintf(hdr.data() + 16, 13, "%-12d", 0);
        std::snprintf(hdr.data() + 28, 7, "%-6d", 0);
        std::snprintf(hdr.data() + 34, 7, "%-6d", 0);
        std::snprintf(hdr.data() + 40, 9, "%-8o", 0644);
        std::snprintf(hdr.data() + 48, 11, "%-10u", static_cast<unsigned int>(object_bytes.size()));
        hdr[58] = '`';
        hdr[59] = '\n';

        static constexpr char kArMagic[] = "!<arch>\n";
        out.insert(out.end(), kArMagic, kArMagic + 8);
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), object_bytes.begin(), object_bytes.end());
        if ((object_bytes.size() & 1U) != 0U) {
            out.push_back('\n');
        }

        return true;
    }
};

} // namespace dgtl::backend::aot

export namespace dgtl::backend::linker {
    using DGTLAOTLinker = dgtl::backend::aot::DGTLAOTLinker;
    namespace aot_coff = dgtl::backend::aot::coff;
}
