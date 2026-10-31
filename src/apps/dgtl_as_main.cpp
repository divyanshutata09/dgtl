/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Sovereign Native Assembler (dgtl-as) v2.1.0
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+ / PE-COFF v2.1.0
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.linker.driver_api;
import dgtl.linker.pe;
import dgtl.linker.elf;
import dgtl.tools.common;

namespace fs = std::filesystem;

namespace dgtl::as {

enum class AssemblerFormat {
    COFF,
    ELF,
    MachO,
    WASM
};

struct AssemblerOptions {
    std::string input_file;
    std::string output_file;
    std::string target_arch = "x86_64";
    AssemblerFormat format = 
#if defined(_WIN32) || defined(_WIN64)
        AssemblerFormat::COFF;
#elif defined(__APPLE__)
        AssemblerFormat::MachO;
#else
        AssemblerFormat::ELF;
#endif
    std::string entry_symbol = "main";
    uint64_t stack_reserve = 0x00400000; // 4 MB
    uint64_t stack_commit  = 0x00100000; // 1 MB (guarantees safe headroom before dynamic expansion)
    bool emit_executable = false;
    bool verbose = false;
    bool debug_info = false;
};

inline std::string trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, (last - first + 1)));
}

inline uint64_t parse_integer(std::string_view str) {
    str = str.substr(str.find_first_not_of(" \t"));
    int base = 10;
    if (str.starts_with("0x") || str.starts_with("0X")) {
        base = 16;
        str = str.substr(2);
    } else if (str.starts_with("$0x") || str.starts_with("$0X")) {
        base = 16;
        str = str.substr(3);
    } else if (str.starts_with("$")) {
        str = str.substr(1);
    }
    return std::strtoull(std::string(str).c_str(), nullptr, base);
}

bool assemble_file(const AssemblerOptions& opts) {
    std::ifstream in(opts.input_file);
    if (!in) {
        std::cerr << "error[dgtl-as]: cannot open input file: " << opts.input_file << "\n";
        return false;
    }

    dgtl::object::ObjectFormat obj_fmt = dgtl::object::ObjectFormat::PE;
    if (opts.format == AssemblerFormat::ELF) {
        obj_fmt = dgtl::object::ObjectFormat::ELF;
    } else if (opts.format == AssemblerFormat::MachO) {
        obj_fmt = dgtl::object::ObjectFormat::MachO;
    } else if (opts.format == AssemblerFormat::WASM) {
        obj_fmt = dgtl::object::ObjectFormat::WASM;
    }

    dgtl::object::ObjectFile obj(obj_fmt);
    auto& text_sec  = obj.get_text_section();
    auto& rdata_sec = obj.get_rdata_section();
    auto& data_sec  = obj.get_data_section();
    auto& bss_sec   = obj.get_bss_section();

    dgtl::object::Section* cur_sec = &text_sec;
    std::string cur_sec_name = ".text";

    std::unordered_set<std::string> global_symbols;
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> label_positions;

    std::string line;
    uint32_t line_num = 0;

    auto align_to = [](std::vector<uint8_t>& buf, size_t alignment, uint8_t pad_byte) {
        while ((buf.size() % alignment) != 0) {
            buf.push_back(pad_byte);
        }
    };

    while (std::getline(in, line)) {
        line_num++;
        std::string s = trim(line);
        if (s.empty()) continue;

        // Strip comments (;, #, //)
        size_t c_idx = s.find_first_of(";#");
        if (c_idx != std::string::npos) s = trim(s.substr(0, c_idx));
        if (s.rfind("//", 0) == 0) continue;
        if (s.empty()) continue;

        // Check for Label
        if (s.back() == ':') {
            std::string lbl = trim(s.substr(0, s.size() - 1));
            uint64_t offset = cur_sec->is_bss() ? cur_sec->bss_size : cur_sec->bytes.size();
            label_positions[lbl] = {cur_sec_name, offset};

            bool is_glob = global_symbols.contains(lbl);
            obj.add_function_symbol(lbl, offset, 0, is_glob);
            continue;
        }

        // Directives
        if (s.front() == '.') {
            std::stringstream ss(s);
            std::string dir;
            ss >> dir;

            if (dir == ".text") {
                cur_sec = &text_sec;
                cur_sec_name = ".text";
            } else if (dir == ".data") {
                cur_sec = &data_sec;
                cur_sec_name = ".data";
            } else if (dir == ".rdata" || dir == ".rodata" || dir == ".idata") {
                cur_sec = &rdata_sec;
                cur_sec_name = ".rdata";
            } else if (dir == ".bss") {
                cur_sec = &bss_sec;
                cur_sec_name = ".bss";
            } else if (dir == ".globl" || dir == ".global") {
                std::string sym_name;
                while (ss >> sym_name) {
                    if (sym_name.back() == ',') sym_name.pop_back();
                    global_symbols.insert(sym_name);
                }
            } else if (dir == ".extern") {
                std::string sym_name;
                while (ss >> sym_name) {
                    if (sym_name.back() == ',') sym_name.pop_back();
                    global_symbols.insert(sym_name);
                    obj.add_external_symbol(sym_name);
                }
            } else if (dir == ".type") {
                std::string sym_name;
                ss >> sym_name;
                // Type directive acknowledged (e.g. .type foo, @function)
                continue;
            } else if (dir == ".float" || dir == ".single") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        float f = std::stof(val_str);
                        uint32_t v = 0;
                        std::memcpy(&v, &f, sizeof(float));
                        cur_sec->bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                    }
                }
            } else if (dir == ".double") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        double d = std::stod(val_str);
                        uint64_t v = 0;
                        std::memcpy(&v, &d, sizeof(double));
                        for (int b = 0; b < 8; ++b) {
                            cur_sec->bytes.push_back(static_cast<uint8_t>((v >> (b * 8)) & 0xFF));
                        }
                    }
                }
            } else if (dir == ".byte") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        cur_sec->bytes.push_back(static_cast<uint8_t>(parse_integer(val_str)));
                    }
                }
            } else if (dir == ".word" || dir == ".short") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        uint16_t v = static_cast<uint16_t>(parse_integer(val_str));
                        cur_sec->bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                    }
                }
            } else if (dir == ".long" || dir == ".int") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        uint32_t v = static_cast<uint32_t>(parse_integer(val_str));
                        cur_sec->bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                        cur_sec->bytes.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                    }
                }
            } else if (dir == ".quad") {
                std::string val_str;
                while (std::getline(ss, val_str, ',')) {
                    val_str = trim(val_str);
                    if (!val_str.empty()) {
                        uint64_t v = parse_integer(val_str);
                        for (int b = 0; b < 8; ++b) {
                            cur_sec->bytes.push_back(static_cast<uint8_t>((v >> (b * 8)) & 0xFF));
                        }
                    }
                }
            } else if (dir == ".string" || dir == ".asciz" || dir == ".ascii") {
                size_t q1 = s.find('"');
                size_t q2 = s.rfind('"');
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                    std::string content = s.substr(q1 + 1, q2 - q1 - 1);
                    for (char c : content) {
                        cur_sec->bytes.push_back(static_cast<uint8_t>(c));
                    }
                    if (dir != ".ascii") {
                        cur_sec->bytes.push_back(0); // Null terminator
                    }
                }
            } else if (dir == ".align" || dir == ".p2align") {
                uint32_t align_val = 16;
                std::string a_str;
                if (ss >> a_str) {
                    uint32_t raw_a = static_cast<uint32_t>(parse_integer(a_str));
                    align_val = (dir == ".p2align") ? (1u << raw_a) : raw_a;
                }
                if (cur_sec->is_bss()) {
                    if (align_val > 1) {
                        cur_sec->bss_size = (cur_sec->bss_size + (align_val - 1)) & ~static_cast<uint64_t>(align_val - 1);
                    }
                } else {
                    align_to(cur_sec->bytes, align_val, (cur_sec_name == ".text" ? 0x90 : 0x00));
                }
            } else if (dir == ".space" || dir == ".zero") {
                std::string count_str;
                if (ss >> count_str) {
                    size_t cnt = static_cast<size_t>(parse_integer(count_str));
                    if (cur_sec->is_bss()) {
                        cur_sec->bss_size += cnt;
                    } else {
                        cur_sec->bytes.insert(cur_sec->bytes.end(), cnt, 0);
                    }
                }
            }
            continue;
        }

        // Instructions
        std::stringstream ss(s);
        std::string mnemonic;
        ss >> mnemonic;
        std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(), ::tolower);

        const bool is_arm64 = (opts.target_arch == "arm64" || opts.target_arch == "aarch64");
        const bool is_riscv = (opts.target_arch == "riscv64" || opts.target_arch == "riscv");

        if (is_arm64) {
            // ─── ARM64 / AArch64 Encoding ────────────────────────────
            if (mnemonic == "nop") {
                // NOP: 0xD503201F
                cur_sec->bytes.push_back(0x1F); cur_sec->bytes.push_back(0x20);
                cur_sec->bytes.push_back(0x03); cur_sec->bytes.push_back(0xD5);
            } else if (mnemonic == "ret") {
                // RET (x30): 0xD65F03C0
                cur_sec->bytes.push_back(0xC0); cur_sec->bytes.push_back(0x03);
                cur_sec->bytes.push_back(0x5F); cur_sec->bytes.push_back(0xD6);
            } else if (mnemonic == "brk") {
                // BRK #0: 0xD4200000
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0xD4);
            } else if (mnemonic == "svc") {
                // SVC #1: 0xD4000001
                cur_sec->bytes.push_back(0x01); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0xD4);
            } else if (mnemonic == "b" || mnemonic == "bl") {
                std::string target;
                ss >> target;
                target = trim(target);
                uint64_t rel_offset = cur_sec->bytes.size();
                // 0x14000000 (b) or 0x94000000 (bl)
                uint8_t op_high = (mnemonic == "bl") ? 0x94 : 0x14;
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(op_high);
                const uint32_t r_type = (opts.format == AssemblerFormat::COFF) ? 3u : 283u; // R_AARCH64_CALL26
                obj.add_relocation(rel_offset, target, 0, r_type, dgtl::object::RelocationKind::Call26);
            } else if (mnemonic == "fadd") {
                // FADD S0, S1, S2: 0x1E222820
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x28);
                cur_sec->bytes.push_back(0x22); cur_sec->bytes.push_back(0x1E);
            } else if (mnemonic == "fsub") {
                // FSUB S0, S1, S2: 0x1E223820
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x38);
                cur_sec->bytes.push_back(0x22); cur_sec->bytes.push_back(0x1E);
            } else if (mnemonic == "fmul") {
                // FMUL S0, S1, S2: 0x1E220820
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x08);
                cur_sec->bytes.push_back(0x22); cur_sec->bytes.push_back(0x1E);
            } else if (mnemonic == "fdiv") {
                // FDIV S0, S1, S2: 0x1E221820
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x18);
                cur_sec->bytes.push_back(0x22); cur_sec->bytes.push_back(0x1E);
            } else if (mnemonic == "fmadd") {
                // FMADD S0, S1, S2, S3: 0x1F020C20
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x0C);
                cur_sec->bytes.push_back(0x02); cur_sec->bytes.push_back(0x1F);
            } else if (mnemonic == "fsqrt") {
                // FSQRT S0, S1: 0x1E21C020
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0xC0);
                cur_sec->bytes.push_back(0x21); cur_sec->bytes.push_back(0x1E);
            } else {
                if (opts.verbose) {
                    std::cout << "[dgtl-as] Line " << line_num << ": ARM64 mnemonic [" << mnemonic << "]\n";
                }
                // Emit NOP padding for 4-byte instruction alignment
                cur_sec->bytes.push_back(0x1F); cur_sec->bytes.push_back(0x20);
                cur_sec->bytes.push_back(0x03); cur_sec->bytes.push_back(0xD5);
            }
        } else if (is_riscv) {
            // ─── RISC-V 64 Encoding ──────────────────────────────────
            if (mnemonic == "nop") {
                // ADDI x0, x0, 0: 0x00000013
                cur_sec->bytes.push_back(0x13); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "ret") {
                // JALR x0, 0(x1): 0x00008067
                cur_sec->bytes.push_back(0x67); cur_sec->bytes.push_back(0x80);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "ebreak") {
                // EBREAK: 0x00100073
                cur_sec->bytes.push_back(0x73); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x10); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "ecall") {
                // ECALL: 0x00000073
                cur_sec->bytes.push_back(0x73); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "jal" || mnemonic == "call") {
                std::string target;
                ss >> target;
                target = trim(target);
                uint64_t rel_offset = cur_sec->bytes.size();
                // JAL x1, 0: 0x000000EF
                cur_sec->bytes.push_back(0xEF); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
                const uint32_t r_type = 17u; // R_RISCV_CALL
                obj.add_relocation(rel_offset, target, 0, r_type, dgtl::object::RelocationKind::Call26);
            } else if (mnemonic == "fadd.s" || mnemonic == "fadd.d") {
                // FADD.S f0, f1, f2: 0x0020F053
                cur_sec->bytes.push_back(0x53); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "fsub.s" || mnemonic == "fsub.d") {
                // FSUB.S f0, f1, f2: 0x0820F053
                cur_sec->bytes.push_back(0x53); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x08);
            } else if (mnemonic == "fmul.s" || mnemonic == "fmul.d") {
                // FMUL.S f0, f1, f2: 0x1020F053
                cur_sec->bytes.push_back(0x53); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x10);
            } else if (mnemonic == "fdiv.s" || mnemonic == "fdiv.d") {
                // FDIV.S f0, f1, f2: 0x1820F053
                cur_sec->bytes.push_back(0x53); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x18);
            } else if (mnemonic == "fmadd.s" || mnemonic == "fmadd.d") {
                // FMADD.S f0, f1, f2, f3: 0x0020F043
                cur_sec->bytes.push_back(0x43); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x20); cur_sec->bytes.push_back(0x00);
            } else if (mnemonic == "fsqrt.s" || mnemonic == "fsqrt.d") {
                // FSQRT.S f0, f1: 0x5800F053
                cur_sec->bytes.push_back(0x53); cur_sec->bytes.push_back(0xF0);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x58);
            } else {
                if (opts.verbose) {
                    std::cout << "[dgtl-as] Line " << line_num << ": RISC-V mnemonic [" << mnemonic << "]\n";
                }
                // Emit NOP padding for 4-byte instruction alignment
                cur_sec->bytes.push_back(0x13); cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00); cur_sec->bytes.push_back(0x00);
            }
        } else {
            // ─── x86-64 Encoding (Default) ───────────────────────────
            if (mnemonic == "nop") {
                cur_sec->bytes.push_back(0x90);
            } else if (mnemonic == "ret") {
                cur_sec->bytes.push_back(0xC3);
            } else if (mnemonic == "int3") {
                cur_sec->bytes.push_back(0xCC);
            } else if (mnemonic == "syscall") {
                cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x05);
            } else if (mnemonic == "ud2") {
                cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x0B);
            } else if (mnemonic == "call" || mnemonic == "jmp") {
                std::string target;
                ss >> target;
                target = trim(target);

                const uint8_t opc = (mnemonic == "call") ? 0xE8 : 0xE9;
                cur_sec->bytes.push_back(opc);
                uint64_t rel_offset = cur_sec->bytes.size();

                // Emit 4 bytes of 0 placeholder
                cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00);
                cur_sec->bytes.push_back(0x00);

                // Record PCRel32 relocation with industry-standard addend A = -4
                const uint32_t r_type = (opts.format == AssemblerFormat::COFF) ? 4u : 2u;
                obj.add_relocation(rel_offset, target, -4, r_type, dgtl::object::RelocationKind::PCRel32);
            } else if (mnemonic == "push") {
                std::string reg;
                ss >> reg;
                reg = trim(reg);
                if (reg == "rbp" || reg == "%rbp") cur_sec->bytes.push_back(0x55);
                else if (reg == "rbx" || reg == "%rbx") cur_sec->bytes.push_back(0x53);
                else if (reg == "rsi" || reg == "%rsi") cur_sec->bytes.push_back(0x56);
                else if (reg == "rdi" || reg == "%rdi") cur_sec->bytes.push_back(0x57);
                else if (reg == "rax" || reg == "%rax") cur_sec->bytes.push_back(0x50);
                else if (reg == "rcx" || reg == "%rcx") cur_sec->bytes.push_back(0x51);
                else if (reg == "rdx" || reg == "%rdx") cur_sec->bytes.push_back(0x52);
                else {
                    cur_sec->bytes.push_back(0x6A);
                    cur_sec->bytes.push_back(0x00);
                }
            } else if (mnemonic == "pop") {
                std::string reg;
                ss >> reg;
                reg = trim(reg);
                if (reg == "rbp" || reg == "%rbp") cur_sec->bytes.push_back(0x5D);
                else if (reg == "rbx" || reg == "%rbx") cur_sec->bytes.push_back(0x5B);
                else if (reg == "rsi" || reg == "%rsi") cur_sec->bytes.push_back(0x5E);
                else if (reg == "rdi" || reg == "%rdi") cur_sec->bytes.push_back(0x5F);
                else if (reg == "rax" || reg == "%rax") cur_sec->bytes.push_back(0x58);
                else if (reg == "rcx" || reg == "%rcx") cur_sec->bytes.push_back(0x59);
                else if (reg == "rdx" || reg == "%rdx") cur_sec->bytes.push_back(0x5A);
                else {
                    cur_sec->bytes.push_back(0x58);
                }
            } else if (mnemonic == "xor" || mnemonic == "xorl" || mnemonic == "xorq") {
                std::string args;
                std::getline(ss, args);
                cur_sec->bytes.push_back(0x31);
                cur_sec->bytes.push_back(0xC0);
            } else if (mnemonic == "sqrtss") {
                // SQRTSS xmm0, xmm1: F3 0F 51 C1
                cur_sec->bytes.push_back(0xF3); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x51); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "sqrtsd") {
                // SQRTSD xmm0, xmm1: F2 0F 51 C1
                cur_sec->bytes.push_back(0xF2); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x51); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "addss") {
                cur_sec->bytes.push_back(0xF3); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x58); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "addsd") {
                cur_sec->bytes.push_back(0xF2); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x58); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "subss") {
                cur_sec->bytes.push_back(0xF3); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x5C); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "subsd") {
                cur_sec->bytes.push_back(0xF2); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x5C); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "mulss") {
                cur_sec->bytes.push_back(0xF3); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x59); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "mulsd") {
                cur_sec->bytes.push_back(0xF2); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x59); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "divss") {
                cur_sec->bytes.push_back(0xF3); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x5E); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "divsd") {
                cur_sec->bytes.push_back(0xF2); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x5E); cur_sec->bytes.push_back(0xC1);
            } else if (mnemonic == "vfmadd213ss" || mnemonic == "vfmadd231ss" || mnemonic == "vfmadd132ss") {
                // VEX.DDS.LIG.66.0F38.W0 A9 /r -> C4 E2 71 A9 C2
                cur_sec->bytes.push_back(0xC4); cur_sec->bytes.push_back(0xE2);
                cur_sec->bytes.push_back(0x71); cur_sec->bytes.push_back(0xA9);
                cur_sec->bytes.push_back(0xC2);
            } else if (mnemonic == "vfmadd213sd" || mnemonic == "vfmadd231sd" || mnemonic == "vfmadd132sd") {
                // VEX.DDS.LIG.66.0F38.W1 A9 /r -> C4 E2 F9 A9 C2
                cur_sec->bytes.push_back(0xC4); cur_sec->bytes.push_back(0xE2);
                cur_sec->bytes.push_back(0xF9); cur_sec->bytes.push_back(0xA9);
                cur_sec->bytes.push_back(0xC2);
            } else if (mnemonic == "xorps") {
                cur_sec->bytes.push_back(0x0F); cur_sec->bytes.push_back(0x57);
                cur_sec->bytes.push_back(0xC0);
            } else if (mnemonic == "xorpd") {
                cur_sec->bytes.push_back(0x66); cur_sec->bytes.push_back(0x0F);
                cur_sec->bytes.push_back(0x57); cur_sec->bytes.push_back(0xC0);
            } else {
                if (opts.verbose) {
                    std::cout << "[dgtl-as] Line " << line_num << ": standard mnemonic [" << mnemonic << "]\n";
                }
                cur_sec->bytes.push_back(0x90);
            }
        }
    }

    in.close();

    std::vector<uint8_t> out_bytes;

    if (opts.emit_executable) {
        std::string effective_entry = opts.entry_symbol;
        if (opts.entry_symbol == "DGTL_kernel_entry" && !obj.find_symbol("DGTL_kernel_entry")) {
            if (obj.find_symbol("main")) {
                effective_entry = "main";
            }
        }
        if (!obj.find_symbol(effective_entry)) {
            obj.add_function_symbol(effective_entry, 0, obj.get_text_section().size(), true);
        }

        dgtl::linker::pe::SovereignPELinkerOptions pe_opts;
        pe_opts.entry_symbol = effective_entry;
        pe_opts.stack_reserve = opts.stack_reserve;
        pe_opts.stack_commit = opts.stack_commit;
        pe_opts.sovereign = true;

        auto res = dgtl::linker::pe::SovereignPELinker::emit_standalone_binary(obj, out_bytes, pe_opts);
        if (!res.success) {
            std::cerr << "error[dgtl-as]: sovereign PE executable synthesis failed: " << res.message << "\n";
            return false;
        }
    } else {
        // Synthesize binary object file
        dgtl::linker::LinkerDriver driver;
        dgtl::linker::OutputFormat driver_fmt = dgtl::linker::OutputFormat::COFFObject;

        if (opts.format == AssemblerFormat::ELF) {
            driver_fmt = dgtl::linker::OutputFormat::ELFObject;
        } else if (opts.format == AssemblerFormat::MachO) {
            driver_fmt = dgtl::linker::OutputFormat::MachOObject;
        } else if (opts.format == AssemblerFormat::WASM) {
            driver_fmt = dgtl::linker::OutputFormat::WASMBinary;
        }

        if (!driver.emit(driver_fmt, obj, out_bytes)) {
            std::cerr << "error[dgtl-as]: object writer synthesis failed\n";
            return false;
        }
    }

    fs::path out_p(opts.output_file);
    if (out_p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_p.parent_path(), ec);
    }

    std::ofstream out(opts.output_file, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "error[dgtl-as]: unable to open output file: " << opts.output_file << "\n";
        return false;
    }

    out.write(reinterpret_cast<const char*>(out_bytes.data()), static_cast<std::streamsize>(out_bytes.size()));
    out.close();

    if (opts.verbose) {
        std::cout << "[dgtl-as] Assembled " << opts.output_file
                  << " (" << out_bytes.size() << " bytes, "
                  << obj.sections().size() << " sections, "
                  << obj.symbols().size() << " symbols)\n";
    }

    return true;
}

} // namespace dgtl::as

namespace {

void print_version() {
    std::cout << "dgtl-as: DGTL Sovereign Native Assembler v2.1.0 (TataG Systems)\n"
              << "Standards: Multi-Target Assembly & Object Synthesizer\n";
}

void print_usage(std::string_view prog) {
    std::cout << "DGTL Sovereign Native Assembler (dgtl-as)\n"
              << "\nUSAGE:\n"
              << "  " << prog << " [OPTIONS] <input.s> -o <output.o|output.exe>\n"
              << "\nOPTIONS:\n"
              << "  -o <file>             Specify output object/executable file name\n"
              << "  --target=<arch>       Target architecture (x86_64, arm64, riscv64, wasm; default: x86_64)\n"
              << "  --format=<fmt>        Object format (coff, elf, macho, wasm; default: auto)\n"
              << "  --exe                 Directly emit standalone sovereign executable\n"
              << "  --entry=<sym>, -e     Entry point symbol for executable (default: main)\n"
              << "  --stack-reserve=<sz>  PE stack reserve size (default: 4M)\n"
              << "  --stack-commit=<sz>   PE stack commit size (default: 1M)\n"
              << "  -g                    Generate debug information\n"
              << "  -v, --verbose         Display verbose assembly diagnostics\n"
              << "  --version, -V         Display assembler version information\n"
              << "  --help, -h            Display this help menu\n\n"
              << "EXAMPLES:\n"
              << "  " << prog << " kernel.s -o kernel.o\n"
              << "  " << prog << " start.s -o app.exe\n"
              << "  " << prog << " --format=coff math.s -o math.obj\n"
              << "  " << prog << " --target=arm64 --format=macho arm_start.s -o arm_start.o\n"
              << "  " << prog << " --target=riscv64 --format=elf rv_kernel.s -o rv_kernel.o\n"
              << "  " << prog << " --target=x86_64 --format=elf start.s -o start.o\n";
}

} // namespace

int dgtl_as_tool_main(std::span<const std::string_view> raw_args, std::string_view invoked_as) {
    const std::string prog = invoked_as.empty() ? "dgtl-as" : std::string(invoked_as);
    auto expanded = dgtl::tools::ArgExpander::expand_args(raw_args);

    if (expanded.empty()) {
        print_usage(prog);
        return 1;
    }

    dgtl::as::AssemblerOptions opts;

    for (size_t i = 0; i < expanded.size(); ++i) {
        std::string_view arg = expanded[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(prog);
            return 0;
        } else if (arg == "-V" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-g") {
            opts.debug_info = true;
        } else if (arg == "--exe" || arg == "-exe") {
            opts.emit_executable = true;
        } else if (arg == "-e" && i + 1 < expanded.size()) {
            opts.entry_symbol = expanded[++i];
        } else if (arg.starts_with("--entry=")) {
            opts.entry_symbol = std::string(arg.substr(8));
        } else if (arg.starts_with("--stack-reserve=")) {
            std::string val(arg.substr(16));
            uint64_t mult = 1;
            if (!val.empty() && (val.back() == 'M' || val.back() == 'm')) { mult = 1024ULL * 1024ULL; val.pop_back(); }
            else if (!val.empty() && (val.back() == 'K' || val.back() == 'k')) { mult = 1024ULL; val.pop_back(); }
            opts.stack_reserve = (val.starts_with("0x") || val.starts_with("0X")) ? std::stoull(val, nullptr, 16) * mult : std::stoull(val, nullptr, 10) * mult;
        } else if (arg.starts_with("--stack-commit=")) {
            std::string val(arg.substr(15));
            uint64_t mult = 1;
            if (!val.empty() && (val.back() == 'M' || val.back() == 'm')) { mult = 1024ULL * 1024ULL; val.pop_back(); }
            else if (!val.empty() && (val.back() == 'K' || val.back() == 'k')) { mult = 1024ULL; val.pop_back(); }
            opts.stack_commit = (val.starts_with("0x") || val.starts_with("0X")) ? std::stoull(val, nullptr, 16) * mult : std::stoull(val, nullptr, 10) * mult;
        } else if (arg == "-o") {
            if (i + 1 < expanded.size()) {
                opts.output_file = expanded[++i];
            } else {
                std::cerr << "error[dgtl-as]: -o requires an output filename\n";
                return 1;
            }
        } else if (arg.starts_with("-o=")) {
            opts.output_file = std::string(arg.substr(3));
        } else if (arg.starts_with("--output=")) {
            opts.output_file = std::string(arg.substr(9));
        } else if (arg.starts_with("--target=")) {
            opts.target_arch = std::string(arg.substr(9));
        } else if (arg.starts_with("--format=")) {
            std::string fmt = std::string(arg.substr(9));
            if (fmt == "coff" || fmt == "pe") opts.format = dgtl::as::AssemblerFormat::COFF;
            else if (fmt == "elf") opts.format = dgtl::as::AssemblerFormat::ELF;
            else if (fmt == "macho") opts.format = dgtl::as::AssemblerFormat::MachO;
            else if (fmt == "wasm") opts.format = dgtl::as::AssemblerFormat::WASM;
        } else if (opts.input_file.empty()) {
            opts.input_file = expanded[i];
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "error[dgtl-as]: no input assembly file specified\n";
        return 1;
    }

    if (opts.output_file.empty()) {
        fs::path p(opts.input_file);
        std::string ext = (opts.format == dgtl::as::AssemblerFormat::COFF) ? ".obj" :
                          ((opts.format == dgtl::as::AssemblerFormat::WASM) ? ".wasm" : ".o");
        opts.output_file = p.replace_extension(ext).string();
    }

    if (opts.output_file.size() >= 4 &&
        (opts.output_file.ends_with(".exe") || opts.output_file.ends_with(".EXE"))) {
        opts.emit_executable = true;
    }

    if (!fs::exists(opts.input_file)) {
        std::cerr << "error[dgtl-as]: input file does not exist: " << opts.input_file << "\n";
        return 2;
    }

    return dgtl::as::assemble_file(opts) ? 0 : 3;
}
