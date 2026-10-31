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

module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module dgtl.linker.elf;

import dgtl.object.writers.elf;
import dgtl.object.readers.elf;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.mir;
export import dgtl.linker.elf.sections;

export namespace dgtl::linker::elf {

class ELFLinker {
public:
    // Sovereign MIR-native linking
    bool link_relocatable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write_machine_function(fn, out_bytes, entry_symbol);
    }

    bool link_relocatable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link_static_archive(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write_machine_function(fn, obj_bytes, fn.name())) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    bool link_static_archive(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write(obj, obj_bytes)) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    struct SovereignELFLinkerOptions {
        uint64_t image_base = 0x400000;
        bool enable_pie = false;
        bool deterministic = true;
        std::string entry_symbol = "main";
        bool emit_relocations = true;
        uint64_t page_size = 0x1000;
        uint16_t target_machine = EM_X86_64;
        bool sovereign = true;
    };

    static std::vector<uint8_t> get_sovereign_intrinsic_x64(std::string_view name) {
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        if (name == "__dgtl_rt_sqrt" || name == "dgtl_rt_sqrt") {
            return { 0xF2, 0x0F, 0x51, 0xC0, 0xC3 }; // sqrtsd xmm0, xmm0; ret
        }
        if (name == "__dgtl_rt_sqrtf" || name == "dgtl_rt_sqrtf") {
            return { 0xF3, 0x0F, 0x51, 0xC0, 0xC3 }; // sqrtss xmm0, xmm0; ret
        }
        if (name == "__dgtl_rt_fast_inv_sqrt" || name == "dgtl_rt_fast_inv_sqrt" ||
            name == "__dgtl_rt_fast_inv_sqrt_bits" || name == "dgtl_rt_fast_inv_sqrt_bits") {
            return { 0xF3, 0x0F, 0x52, 0xC8, 0x0F, 0x28, 0xC1, 0xC3 }; // rsqrtss; movaps; ret
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" || name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs") {
            return { 0x66, 0x48, 0x0F, 0x7E, 0xC0, 0x48, 0x0F, 0xBA, 0xF0, 0x3F, 0x66, 0x48, 0x0F, 0x6E, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_floor" || name == "dgtl_rt_floor") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x01, 0xC3 };
        if (name == "__dgtl_rt_ceil" || name == "dgtl_rt_ceil")   return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x02, 0xC3 };
        if (name == "__dgtl_rt_round" || name == "dgtl_rt_round") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x00, 0xC3 };
        if (name == "__dgtl_rt_trunc" || name == "dgtl_rt_trunc") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x03, 0xC3 };
        if (name == "__dgtl_rt_read_tsc" || name == "dgtl_rt_read_tsc") {
            return { 0x0F, 0x31, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0xC3 };
        }
        if (name == "__dgtl_rt_prefetch" || name == "dgtl_rt_prefetch" || name == "__dgtl_rt_prefetch_t0" || name == "dgtl_rt_prefetch_t0") {
            return { 0x0F, 0x18, 0x01, 0xC3 };
        }
        if (name == "__dgtl_rt_clflush" || name == "dgtl_rt_clflush") return { 0x0F, 0xAE, 0x39, 0xC3 };
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32") return { 0xC4, 0xE2, 0x71, 0xA9, 0xC2, 0xC3 };
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64") return { 0xC4, 0xE2, 0xF9, 0xA9, 0xC2, 0xC3 };
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || name == "relu") {
            return { 0x66, 0x0F, 0x57, 0xC9, 0xF2, 0x0F, 0x5F, 0xC1, 0xC3 }; // xorpd xmm1, xmm1; maxsd xmm0, xmm1; ret
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || name == "reluf") {
            return { 0x0F, 0x57, 0xC9, 0x0F, 0x5F, 0xC1, 0xC3 }; // xorps xmm1, xmm1; maxss xmm0, xmm1; ret
        }
        if (name == "__dgtl_rt_sin" || name == "dgtl_rt_sin") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFE, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cos" || name == "dgtl_rt_cos") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFF, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tan" || name == "dgtl_rt_tan") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan" || name == "dgtl_rt_atan") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xE8, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan2" || name == "dgtl_rt_atan2") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xDD, 0x44, 0x24, 0x08, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_log" || name == "dgtl_rt_log") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log2" || name == "dgtl_rt_log2") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xE8, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log10" || name == "dgtl_rt_log10") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEC, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_exp" || name == "dgtl_rt_exp") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xDC, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_pow" || name == "dgtl_rt_pow") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_sinf" || name == "dgtl_rt_sinf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFE, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cosf" || name == "dgtl_rt_cosf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFF, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tanf" || name == "dgtl_rt_tanf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_logf" || name == "dgtl_rt_logf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_expf" || name == "dgtl_rt_expf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xD8, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_powf" || name == "dgtl_rt_powf") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x04, 0xD9, 0x44, 0x24, 0x04, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_fmod" || name == "dgtl_rt_fmod") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF8, 0xDD, 0x1C, 0x24, 0xDD, 0xD8, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_vec4_dot" || name == "dgtl_rt_vec4_dot") {
            return { 0x0F, 0x10, 0x01, 0x0F, 0x10, 0x0A, 0x66, 0x0F, 0x3A, 0x40, 0xC1, 0xF1, 0xC3 };
        }
        if (name == "__dgtl_rt_vec3_cross" || name == "dgtl_rt_vec3_cross") {
            return {
                0xF3, 0x0F, 0x10, 0x42, 0x04, 0xF3, 0x0F, 0x59, 0x40, 0x08, 0xF3, 0x0F, 0x10, 0x4A, 0x08, 0xF3, 0x0F, 0x59, 0x48, 0x04,
                0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x01, 0xF3, 0x0F, 0x10, 0x42, 0x08, 0xF3, 0x0F, 0x59, 0x00, 0xF3, 0x0F, 0x10, 0x0A,
                0xF3, 0x0F, 0x59, 0x48, 0x08, 0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x41, 0x04, 0xF3, 0x0F, 0x10, 0x02, 0xF3, 0x0F, 0x59, 0x48, 0x04,
                0xF3, 0x0F, 0x10, 0x4A, 0x04, 0xF3, 0x0F, 0x59, 0x00, 0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x41, 0x08, 0xC3
            };
        }
        if (name == "memcpy" || name == "__memcpy") {
            // System V AMD64 ABI: rdi=dest, rsi=src, rdx=count. Return dest in rax.
            return {
                0x48, 0x89, 0xF8,             // mov rax, rdi
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xA4,                   // rep movsb
                0xC3                          // ret
            };
        }
        if (name == "memset" || name == "__memset") {
            // System V AMD64 ABI: rdi=dest, rsi=val, rdx=count. Return dest in rax.
            return {
                0x49, 0x89, 0xF8,             // mov r8, rdi
                0x48, 0x89, 0xF0,             // mov rax, rsi
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xAA,                   // rep stosb
                0x4C, 0x89, 0xC0,             // mov rax, r8
                0xC3                          // ret
            };
        }
        if (name == "memcmp" || name == "__memcmp") {
            // System V AMD64 ABI: rdi=s1, rsi=s2, rdx=count.
            return {
                0x48, 0x85, 0xD2,             // test rdx, rdx
                0x74, 0x12,                   // jz .equal
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xA6,                   // repe cmpsb
                0x74, 0x0B,                   // je .equal
                0x0F, 0xB6, 0x47, 0xFF,       // movzx eax, byte ptr [rdi-1]
                0x0F, 0xB6, 0x56, 0xFF,       // movzx edx, byte ptr [rsi-1]
                0x29, 0xD0,                   // sub eax, edx
                0xC3,                         // ret
                0x31, 0xC0,                   // .equal: xor eax, eax
                0xC3                          // ret
            };
        }
        if (name == "memmove" || name == "__memmove") {
            // System V AMD64 ABI: rdi=dest, rsi=src, rdx=count. Return dest in rax.
            return {
                0x48, 0x89, 0xF8,                         // mov rax, rdi
                0x48, 0x85, 0xD2,                         // test rdx, rdx
                0x74, 0x1D,                               // jz .done
                0x48, 0x39, 0xF7,                         // cmp rdi, rsi
                0x76, 0x12,                               // jbe .forward
                0x48, 0x8D, 0x7C, 0x17, 0xFF,             // lea rdi, [rdi+rdx-1]
                0x48, 0x8D, 0x74, 0x16, 0xFF,             // lea rsi, [rsi+rdx-1]
                0x48, 0x89, 0xD1,                         // mov rcx, rdx
                0xFD,                                     // std
                0xF3, 0xA4,                               // rep movsb
                0xFC,                                     // cld
                0xC3,                                     // ret
                0x48, 0x89, 0xD1,                         // .forward: mov rcx, rdx
                0xFC,                                     // cld
                0xF3, 0xA4,                               // rep movsb
                0xC3                                      // .done: ret
            };
        }
        if (name == "__chkstk" || name == "_chkstk" || name == "___chkstk_ms" || name == "__chkstk_ms" || name == "_alloca" || name == "__alloca") {
            // Sovereign x64 Stack Probe (__chkstk / ___chkstk_ms)
            return {
                0x51,                                     // push rcx
                0x52,                                     // push rdx
                0x50,                                     // push rax
                0x9C,                                     // pushfq
                0x48, 0x8D, 0x4C, 0x24, 0x28,             // lea rcx, [rsp + 40] (caller RSP)
                0x48, 0x89, 0xC2,                         // mov rdx, rax (bytes to probe)
                // .probe_pages:
                0x48, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp rdx, 4096
                0x72, 0x12,                               // jb .probe_tail (+18)
                0x48, 0x81, 0xE9, 0x00, 0x10, 0x00, 0x00, // sub rcx, 4096
                0x85, 0x01,                               // test [rcx], eax (probe guard page)
                0x48, 0x81, 0xEA, 0x00, 0x10, 0x00, 0x00, // sub rdx, 4096
                0xEB, 0xE5,                               // jmp .probe_pages (-27)
                // .probe_tail:
                0x48, 0x29, 0xD1,                         // sub rcx, rdx
                0x85, 0x01,                               // test [rcx], eax (probe remainder)
                0x9D,                                     // popfq
                0x58,                                     // pop rax
                0x5A,                                     // pop rdx
                0x59,                                     // pop rcx
                0xC3                                      // ret
            };
        }
        return {};
    }

    static std::vector<uint8_t> get_sovereign_intrinsic_arm64(std::string_view name) {
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        if (name == "__dgtl_rt_sqrt" || name == "dgtl_rt_sqrt") {
            return { 0x00, 0x38, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fsqrt d0, d0; ret
        }
        if (name == "__dgtl_rt_sqrtf" || name == "dgtl_rt_sqrtf") {
            return { 0x00, 0x38, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fsqrt s0, s0; ret
        }
        if (name == "__dgtl_rt_fast_inv_sqrt" || name == "dgtl_rt_fast_inv_sqrt" ||
            name == "__dgtl_rt_fast_inv_sqrt_bits" || name == "dgtl_rt_fast_inv_sqrt_bits") {
            return { 0x01, 0xD8, 0x2C, 0x7E, 0x20, 0x40, 0x20, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frsqrte s1, s0; fmov s0, s1; ret
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" || name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs") {
            return { 0x00, 0x20, 0x60, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fabs d0, d0; ret
        }
        if (name == "__dgtl_rt_absf" || name == "dgtl_rt_absf" || name == "__dgtl_rt_fabsf" || name == "dgtl_rt_fabsf") {
            return { 0x00, 0x20, 0x20, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fabs s0, s0; ret
        }
        if (name == "__dgtl_rt_floor" || name == "dgtl_rt_floor") return { 0x00, 0x50, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frintm
        if (name == "__dgtl_rt_ceil"  || name == "dgtl_rt_ceil")  return { 0x00, 0x48, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frintp
        if (name == "__dgtl_rt_round" || name == "dgtl_rt_round") return { 0x00, 0x60, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frinta
        if (name == "__dgtl_rt_trunc" || name == "dgtl_rt_trunc") return { 0x00, 0x58, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frintz
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32") return { 0x00, 0x08, 0x21, 0x1F, 0xC0, 0x03, 0x5F, 0xD6 }; // fmadd s0, s0, s1, s2
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64") return { 0x00, 0x08, 0x61, 0x1F, 0xC0, 0x03, 0x5F, 0xD6 }; // fmadd d0, d0, d1, d2
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || name == "relu") {
            return { 0xE1, 0x03, 0x27, 0x9E, 0x00, 0x48, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov d1, xzr; fmax d0, d0, d1; ret
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || name == "reluf") {
            return { 0xE1, 0x03, 0x27, 0x1E, 0x00, 0x48, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov s1, wzr; fmax s0, s0, s1; ret
        }
        if (name == "memcpy" || name == "__memcpy") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x24, 0x14, 0x40, 0x38, 0x64, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xAC, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memset" || name == "__memset") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x61, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xCD, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memcmp" || name == "__memcmp") {
            return { 0xC2, 0x00, 0x00, 0xB4, 0x03, 0x14, 0x40, 0x38, 0x24, 0x14, 0x40, 0x38, 0x7F, 0x00, 0x04, 0x6B, 0x81, 0x00, 0x00, 0x54, 0x42, 0x04, 0x00, 0xF1, 0x61, 0xFF, 0xFF, 0x54, 0x00, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x60, 0x00, 0x04, 0x4B, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memmove" || name == "__memmove") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x24, 0x14, 0x40, 0x38, 0x64, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xAC, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        return {};
    }

    static std::vector<uint8_t> get_sovereign_intrinsic_rv64(std::string_view name) {
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        if (name == "__dgtl_rt_sqrt" || name == "dgtl_rt_sqrt") {
            return { 0x53, 0x75, 0x05, 0x5A, 0x67, 0x80, 0x00, 0x00 }; // fsqrt.d fa0, fa0; ret
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" || name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs") {
            return { 0x53, 0x15, 0x05, 0x22, 0x67, 0x80, 0x00, 0x00 }; // fsgnjx.d fa0, fa0, fa0; ret
        }
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || name == "relu") {
            return { 0xD3, 0x00, 0x00, 0xF2, 0x53, 0x10, 0x10, 0x2A, 0x67, 0x80, 0x00, 0x00 }; // fmv.d.x fa1, zero; fmax.d fa0, fa0, fa1; ret
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || name == "reluf") {
            return { 0xD3, 0x00, 0x00, 0xF0, 0x53, 0x10, 0x10, 0x28, 0x67, 0x80, 0x00, 0x00 }; // fmv.w.x fa1, zero; fmax.s fa0, fa0, fa1; ret
        }
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32") {
            return { 0x43, 0x00, 0x10, 0x10, 0x67, 0x80, 0x00, 0x00 }; // fmadd.s fa0, fa0, fa1, fa2; ret
        }
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64") {
            return { 0x43, 0x00, 0x10, 0x12, 0x67, 0x80, 0x00, 0x00 }; // fmadd.d fa0, fa0, fa1, fa2; ret
        }
        if (name == "memcpy" || name == "__memcpy" || name == "memmove" || name == "__memmove") {
            // mv a3, a0; beqz a2, .done; .loop: lbu a4, 0(a1); addi a1, a1, 1; sb a4, 0(a3); addi a3, a3, 1; addi a2, a2, -1; bnez a2, .loop; .done: ret
            return { 0x13, 0x07, 0x05, 0x00, 0x63, 0x08, 0x06, 0x00, 0x03, 0x47, 0x05, 0x00, 0x13, 0x06, 0x16, 0x00, 0x23, 0x00, 0xE7, 0x00, 0x93, 0x06, 0x17, 0x00, 0x13, 0x06, 0xF6, 0xFF, 0xE3, 0x16, 0x06, 0xFE, 0x67, 0x80, 0x00, 0x00 };
        }
        if (name == "memset" || name == "__memset") {
            // mv a3, a0; beqz a2, .done; .loop: sb a1, 0(a3); addi a3, a3, 1; addi a2, a2, -1; bnez a2, .loop; .done: ret
            return { 0x13, 0x07, 0x05, 0x00, 0x63, 0x06, 0x06, 0x00, 0x23, 0x00, 0xB7, 0x00, 0x93, 0x06, 0x17, 0x00, 0x13, 0x06, 0xF6, 0xFF, 0xE3, 0x18, 0x06, 0xFE, 0x67, 0x80, 0x00, 0x00 };
        }
        return {};
    }

    bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        uint64_t image_base = 0x400000,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        SovereignELFLinkerOptions opts;
        opts.image_base = image_base;
        opts.entry_symbol = std::string(entry_symbol);
        return link_executable(obj, out_bytes, opts);
    }

    bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        const SovereignELFLinkerOptions& options) const {

        out_bytes.clear();

        // 1. Locate text section
        const auto* text_sec = obj.find_section(".text");
        if (!text_sec || text_sec->bytes.empty()) {
            for (const auto& s : obj.sections()) {
                if (s.is_code() && !s.bytes.empty()) {
                    text_sec = &s;
                    break;
                }
            }
        }
        if (!text_sec || text_sec->bytes.empty()) {
            return false;
        }

        const uint64_t kPageSize = options.page_size;

        // Auto-detect target architecture
        uint16_t effective_machine = options.target_machine;
        for (const auto& rel : obj.relocations()) {
            if (rel.kind == dgtl::object::RelocationKind::Call26 || rel.kind == dgtl::object::RelocationKind::Branch26 || rel.type == 0x011B) {
                effective_machine = EM_AARCH64;
                break;
            }
            if (rel.type == 0x0012) { // R_RISCV_CALL
                effective_machine = EM_RISCV;
                break;
            }
        }

        // 2. Build Sovereign Freestanding Entry Trampoline
        std::vector<uint8_t> code;
        uint64_t trampoline_size = 0;
        uint64_t main_reloc_offset = 0;
        const bool needs_freestanding_trampoline = (options.entry_symbol == "main" || options.entry_symbol == "DGTL_kernel_entry");

        if (needs_freestanding_trampoline) {
            if (effective_machine == EM_AARCH64) {
                // AArch64 entry trampoline: mov x29, 0; mov x30, 0; ldr x0, [sp]; add x1, sp, #8; bl main; mov x8, #93; svc #0
                static constexpr uint8_t kStartTrampolineARM64[] = {
                    0x1F, 0x00, 0x00, 0xD2, // mov x29, #0
                    0x1E, 0x00, 0x00, 0xD2, // mov x30, #0
                    0xE0, 0x03, 0x40, 0xF9, // ldr x0, [sp] (argc)
                    0xE1, 0x23, 0x00, 0x91, // add x1, sp, #8 (argv)
                    0x00, 0x00, 0x00, 0x94, // bl main (placeholder)
                    0xAA, 0x0B, 0x80, 0xD2, // mov x8, #93 (sys_exit)
                    0x01, 0x00, 0x00, 0xD4  // svc #0
                };
                trampoline_size = sizeof(kStartTrampolineARM64);
                main_reloc_offset = 16;
                code.insert(code.end(), kStartTrampolineARM64, kStartTrampolineARM64 + trampoline_size);
            } else if (effective_machine == EM_RISCV) {
                // RISC-V 64 entry trampoline: addi s0, zero, 0; ld a0, 0(sp); addi a1, sp, 8; jal ra, main; addi a7, zero, 93; ecall
                static constexpr uint8_t kStartTrampolineRV64[] = {
                    0x13, 0x04, 0x00, 0x00, // addi s0, zero, 0
                    0x03, 0x35, 0x01, 0x00, // ld a0, 0(sp)
                    0x93, 0x05, 0x81, 0x00, // addi a1, sp, 8
                    0xEF, 0x00, 0x00, 0x00, // jal ra, main (placeholder)
                    0x93, 0x08, 0xD0, 0x05, // addi a7, zero, 93
                    0x73, 0x00, 0x00, 0x00  // ecall
                };
                trampoline_size = sizeof(kStartTrampolineRV64);
                main_reloc_offset = 12;
                code.insert(code.end(), kStartTrampolineRV64, kStartTrampolineRV64 + trampoline_size);
            } else {
                // System V AMD64 ABI entry trampoline
                static constexpr uint8_t kStartTrampolineX64[] = {
                    0x31, 0xED,                   // xor ebp, ebp
                    0x5F,                         // pop rdi (argc)
                    0x48, 0x89, 0xE6,             // mov rsi, rsp (argv)
                    0x48, 0x83, 0xE4, 0xF0,       // and rsp, -16 (align stack)
                    0xE8, 0x00, 0x00, 0x00, 0x00, // call main (placeholder)
                    0x89, 0xC7,                   // mov edi, eax (exit code)
                    0xB8, 0x3C, 0x00, 0x00, 0x00, // mov eax, 60 (sys_exit)
                    0x0F, 0x05,                   // syscall
                    0x90, 0x90, 0x90              // align padding
                };
                trampoline_size = sizeof(kStartTrampolineX64);
                main_reloc_offset = 11;
                code.insert(code.end(), kStartTrampolineX64, kStartTrampolineX64 + trampoline_size);
            }
        }

        const uint64_t user_text_offset = code.size();
        code.insert(code.end(), text_sec->bytes.begin(), text_sec->bytes.end());

        // Synthesize Sovereign In-Binary Runtime Intrinsics (Zero External Dependencies)
        struct SynthesizedIntrinsic {
            std::string name;
            uint64_t text_offset;
        };
        std::vector<SynthesizedIntrinsic> synthesized_intrinsics;

        if (options.sovereign) {
            for (const auto& rel : obj.relocations()) {
                if (rel.symbol_name.empty()) continue;
                std::string sym = rel.symbol_name;

                const auto* existing_sym = obj.find_symbol(sym);
                if (existing_sym && existing_sym->is_defined()) continue;

                bool already_done = false;
                for (const auto& s : synthesized_intrinsics) {
                    if (s.name == sym) { already_done = true; break; }
                }
                if (already_done) continue;

                std::vector<uint8_t> instr_bytes;
                if (effective_machine == EM_AARCH64) {
                    instr_bytes = get_sovereign_intrinsic_arm64(sym);
                } else if (effective_machine == EM_RISCV) {
                    instr_bytes = get_sovereign_intrinsic_rv64(sym);
                } else {
                    instr_bytes = get_sovereign_intrinsic_x64(sym);
                }
                if (instr_bytes.empty()) {
                    bool is_intrinsic = sym.starts_with("__dgtl_rt_") || sym.starts_with("dgtl_rt_") ||
                                        sym == "memcpy" || sym == "memset" || sym == "memcmp" || sym == "memmove" ||
                                        sym == "__memcpy" || sym == "__memset" || sym == "__memcmp" || sym == "__memmove" ||
                                        sym == "__chkstk" || sym == "_chkstk" || sym == "___chkstk_ms" || sym == "__chkstk_ms" ||
                                        sym == "_alloca" || sym == "__alloca";
                    if (!is_intrinsic) continue;
                }
                if (instr_bytes.empty()) {
                    if (effective_machine == EM_AARCH64) {
                        instr_bytes = { 0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6 }; // mov x0, 0; ret
                    } else if (effective_machine == EM_RISCV) {
                        instr_bytes = { 0x13, 0x05, 0x00, 0x00, 0x67, 0x80, 0x00, 0x00 }; // li a0, 0; ret
                    } else {
                        instr_bytes = { 0x31, 0xC0, 0xC3 }; // xor eax, eax; ret
                    }
                }

                while ((code.size() % 16) != 0) {
                    if (effective_machine == EM_AARCH64) {
                        code.insert(code.end(), { 0x1F, 0x20, 0x03, 0xD5 }); // nop
                    } else if (effective_machine == EM_RISCV) {
                        code.insert(code.end(), { 0x13, 0x00, 0x00, 0x00 }); // nop
                    } else {
                        code.push_back(0x90);
                    }
                }
                const uint64_t off = code.size();
                code.insert(code.end(), instr_bytes.begin(), instr_bytes.end());
                synthesized_intrinsics.push_back({sym, off});
            }
        }

        // 3. Collect Data Sections (.rodata, .data, .bss)
        std::vector<uint8_t> data;
        struct SecOffset {
            std::string name;
            uint64_t offset;
            bool is_bss;
        };
        std::vector<SecOffset> data_offsets;
        uint64_t total_bss_size = 0;

        // Collect initialized data (.rodata, .data)
        for (const auto& sec : obj.sections()) {
            if (sec.is_code() || sec.is_bss()) continue;
            if (sec.name == ".rodata" || sec.name == ".data" || sec.name == ".rdata" || (!sec.empty() && sec.name != ".text")) {
                while ((data.size() % 16) != 0) data.push_back(0);
                data_offsets.push_back({sec.name, data.size(), false});
                data.insert(data.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }

        // Collect BSS sections (.bss)
        const uint64_t bss_start_in_data = data.size();
        for (const auto& sec : obj.sections()) {
            if (sec.is_bss() || sec.name == ".bss") {
                const uint64_t align_val = std::max<uint64_t>(16u, sec.alignment);
                const uint64_t rem = total_bss_size % align_val;
                if (rem != 0) total_bss_size += (align_val - rem);
                data_offsets.push_back({sec.name, bss_start_in_data + total_bss_size, true});
                total_bss_size += sec.bss_size;
            }
        }

        auto find_data_offset = [&](std::string_view name) -> std::optional<uint64_t> {
            for (const auto& item : data_offsets) {
                if (item.name == name) return item.offset;
            }
            return std::nullopt;
        };

        const uint64_t code_file_off = kPageSize;
        const uint64_t code_vaddr = options.image_base + code_file_off;

        uint64_t data_file_off = 0;
        uint64_t data_vaddr = 0;
        const bool has_data_or_bss = (!data.empty() || total_bss_size > 0);
        if (has_data_or_bss) {
            data_file_off = (code_file_off + code.size() + (kPageSize - 1)) & ~(kPageSize - 1);
            data_vaddr = options.image_base + data_file_off;
        }

        // 4. Locate entry point
        uint64_t entry_offset = 0;
        if (!options.entry_symbol.empty()) {
            if (const auto* sym = obj.find_symbol(options.entry_symbol)) {
                entry_offset = sym->value;
            }
        }
        if (entry_offset == 0) {
            if (const auto* sym = obj.find_symbol("DGTL_kernel_entry")) {
                entry_offset = sym->value;
            } else if (const auto* sym_m = obj.find_symbol("main")) {
                entry_offset = sym_m->value;
            } else if (const auto* sym_s = obj.find_symbol("_start")) {
                entry_offset = sym_s->value;
            }
        }

        // Patch freestanding trampoline call displacement to user entry
        if (needs_freestanding_trampoline) {
            const int64_t call_dest = static_cast<int64_t>(user_text_offset + entry_offset);
            const int64_t call_src = static_cast<int64_t>(main_reloc_offset + 4);
            const auto delta = static_cast<int32_t>(call_dest - call_src);
            for (int i = 0; i < 4; ++i) {
                code[main_reloc_offset + i] = static_cast<uint8_t>((delta >> (i * 8)) & 0xFF);
            }
        }

        const uint64_t entry_vaddr = needs_freestanding_trampoline ? code_vaddr : (code_vaddr + user_text_offset + entry_offset);

        // 5. Build Symbol Virtual Address Map with O(1) Binary Search
        struct SymVAddr { std::string name; uint64_t vaddr; };
        std::vector<SymVAddr> symbol_vaddrs;
        symbol_vaddrs.reserve(obj.symbols().size());
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined()) {
                uint64_t base = code_vaddr + user_text_offset;
                if (sym.section_index < obj.sections().size()) {
                    const auto& sec_name = obj.sections()[sym.section_index].name;
                    if (sec_name != ".text") {
                        if (auto off = find_data_offset(sec_name)) {
                            base = data_vaddr + *off;
                        }
                    }
                }
                symbol_vaddrs.push_back({sym.name, base + sym.value});
            }
        }
        for (const auto& synth : synthesized_intrinsics) {
            symbol_vaddrs.push_back({synth.name, code_vaddr + synth.text_offset});
            std::string_view stripped = synth.name;
            if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
            else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("__myzon_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("myzon_")) stripped.remove_prefix(6);
            else if (stripped.starts_with("__")) stripped.remove_prefix(2);
            else if (stripped.starts_with("_")) stripped.remove_prefix(1);
            if (!stripped.empty()) {
                std::string s(stripped);
                symbol_vaddrs.push_back({s, code_vaddr + synth.text_offset});
                symbol_vaddrs.push_back({"__dgtl_rt_" + s, code_vaddr + synth.text_offset});
                symbol_vaddrs.push_back({"dgtl_rt_" + s, code_vaddr + synth.text_offset});
                symbol_vaddrs.push_back({"__myzon_" + s, code_vaddr + synth.text_offset});
                symbol_vaddrs.push_back({"myzon_" + s, code_vaddr + synth.text_offset});
            }
        }
        std::sort(symbol_vaddrs.begin(), symbol_vaddrs.end(), [](const SymVAddr& a, const SymVAddr& b) {
            return a.name < b.name;
        });

        auto find_symbol_vaddr = [&](std::string_view name) -> std::optional<uint64_t> {
            auto it = std::lower_bound(symbol_vaddrs.begin(), symbol_vaddrs.end(), name,
                [](const SymVAddr& item, std::string_view n) { return item.name < n; });
            if (it != symbol_vaddrs.end() && it->name == name) return it->vaddr;

            // Bare-name alias-stripping fallback
            std::string_view stripped = name;
            if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
            else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("__myzon_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("myzon_")) stripped.remove_prefix(6);
            else if (stripped.starts_with("__")) stripped.remove_prefix(2);
            else if (stripped.starts_with("_")) stripped.remove_prefix(1);
            if (stripped != name) {
                std::string candidates[] = {
                    std::string(stripped),
                    "__dgtl_rt_" + std::string(stripped),
                    "dgtl_rt_" + std::string(stripped),
                    "__myzon_" + std::string(stripped),
                    "myzon_" + std::string(stripped),
                    "_" + std::string(stripped),
                    "__" + std::string(stripped)
                };
                for (const auto& cand : candidates) {
                    auto it2 = std::lower_bound(symbol_vaddrs.begin(), symbol_vaddrs.end(), cand,
                        [](const SymVAddr& item, std::string_view n) { return item.name < n; });
                    if (it2 != symbol_vaddrs.end() && it2->name == cand) return it2->vaddr;
                }
            }

            // Fallback: strip alias prefixes before '__' (e.g. M__abs -> abs)
            auto pos = name.rfind("__");
            if (pos != std::string_view::npos && pos + 2 < name.size()) {
                std::string_view bare = name.substr(pos + 2);
                auto it2 = std::lower_bound(symbol_vaddrs.begin(), symbol_vaddrs.end(), bare,
                    [](const SymVAddr& item, std::string_view n) { return item.name < n; });
                if (it2 != symbol_vaddrs.end() && it2->name == bare) return it2->vaddr;
            }
            return std::nullopt;
        };

        // 6. Apply Relocations
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            auto sym_opt = find_symbol_vaddr(rel.symbol_name);
            if (!sym_opt) {
                if (rel.kind == dgtl::object::RelocationKind::PCRel32 || rel.kind == dgtl::object::RelocationKind::PLT32) {
                    const uint64_t place_off = user_text_offset + rel.offset;
                    if (place_off > 0 && place_off + 4 <= code.size() && code[place_off - 1] == 0xE8) {
                        code[place_off - 1] = 0x0F;
                        code[place_off]     = 0x1F;
                        code[place_off + 1] = 0x44;
                        code[place_off + 2] = 0x00;
                        code[place_off + 3] = 0x00;
                    }
                }
                continue;
            }
            const uint64_t target_vaddr = *sym_opt;

            std::optional<uint64_t> rel_data_off = std::nullopt;
            if (rel.section_index < obj.sections().size()) {
                rel_data_off = find_data_offset(obj.sections()[rel.section_index].name);
            }

            if (rel_data_off) {
                const uint64_t place_off = *rel_data_off + rel.offset;
                if (place_off + 8 <= data.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::Absolute64) {
                        const uint64_t val = target_vaddr + rel.addend;
                        for (int i = 0; i < 8; ++i) data[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute32) {
                        const uint32_t val = static_cast<uint32_t>(target_vaddr + rel.addend);
                        for (int i = 0; i < 4; ++i) data[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    }
                }
            } else {
                const uint64_t place_off = user_text_offset + rel.offset;
                const uint64_t place_vaddr = code_vaddr + place_off;
                if (place_off + 4 <= code.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::Call26 || rel.kind == dgtl::object::RelocationKind::Branch26 || rel.type == 0x011B) {
                        dgtl::object::RelocationApplier::apply_branch26_arm64(code.data() + place_off, place_vaddr, target_vaddr);
                    } else if (rel.kind == dgtl::object::RelocationKind::PCRel32 || rel.kind == dgtl::object::RelocationKind::PLT32) {
                        const uint64_t reloc_va = place_vaddr;
                        const int64_t delta = static_cast<int64_t>(target_vaddr) + rel.addend - static_cast<int64_t>(reloc_va);
                        const auto val = static_cast<int32_t>(delta);
                        for (int i = 0; i < 4; ++i) code[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute64 && place_off + 8 <= code.size()) {
                        const uint64_t val = target_vaddr + rel.addend;
                        for (int i = 0; i < 8; ++i) code[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute32) {
                        const uint32_t val = static_cast<uint32_t>(target_vaddr + rel.addend);
                        for (int i = 0; i < 4; ++i) code[place_off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
                    }
                }
            }
        }

        // 7. Emit ELF64 Binary Image
        const uint64_t total_file_size = has_data_or_bss ? (data_file_off + data.size()) : (code_file_off + code.size());
        out_bytes.assign(total_file_size, 0);

        // 1. ELF Header (64 bytes)
        out_bytes[0] = 0x7F; out_bytes[1] = 'E'; out_bytes[2] = 'L'; out_bytes[3] = 'F';
        out_bytes[4] = 2; // ELFCLASS64
        out_bytes[5] = 1; // ELFDATA2LSB
        out_bytes[6] = 1; // EV_CURRENT
        out_bytes[7] = 0; // ELFOSABI_NONE (System V)

        auto write_u16 = [&](size_t off, uint16_t val) {
            out_bytes[off + 0] = static_cast<uint8_t>(val & 0xFF);
            out_bytes[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        };
        auto write_u32 = [&](size_t off, uint32_t val) {
            for (int i = 0; i < 4; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };
        auto write_u64 = [&](size_t off, uint64_t val) {
            for (int i = 0; i < 8; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };

        const uint16_t num_phdrs = static_cast<uint16_t>((has_data_or_bss ? 2 : 1) + 1); // +1 for PT_GNU_STACK

        write_u16(16, options.enable_pie ? ET_DYN : ET_EXEC); // e_type
        write_u16(18, effective_machine);                     // e_machine (EM_X86_64, EM_AARCH64, EM_RISCV)
        write_u32(20, 1);                                     // e_version = 1
        write_u64(24, entry_vaddr);                           // e_entry
        write_u64(32, 64);                                    // e_phoff = 64
        write_u64(40, 0);                                     // e_shoff = 0
        write_u32(48, 0);                                     // e_flags = 0
        write_u16(52, 64);                                    // e_ehsize = 64
        write_u16(54, 56);                                    // e_phentsize = 56
        write_u16(56, num_phdrs);                             // e_phnum
        write_u16(58, 0);                                     // e_shentsize = 0
        write_u16(60, 0);                                     // e_shnum = 0
        write_u16(62, 0);                                     // e_shstrndx = 0

        // 2. Program Headers (56 bytes each)
        // Segment 0: Code (.text, RX)
        write_u32(64 + 0,  PT_LOAD);           // p_type = PT_LOAD (1)
        write_u32(64 + 4,  PF_R | PF_X);       // p_flags = PF_R | PF_X (5)
        write_u64(64 + 8,  0);                 // p_offset = 0 (includes header)
        write_u64(64 + 16, options.image_base);// p_vaddr
        write_u64(64 + 24, options.image_base);// p_paddr
        write_u64(64 + 32, code_file_off + code.size()); // p_filesz
        write_u64(64 + 40, code_file_off + code.size()); // p_memsz
        write_u64(64 + 48, kPageSize);         // p_align = 0x1000

        size_t next_ph = 64 + 56;

        // Segment 1: Data & BSS (.data + .bss, RW)
        if (has_data_or_bss) {
            write_u32(next_ph + 0,  PT_LOAD);                  // p_type = PT_LOAD (1)
            write_u32(next_ph + 4,  PF_R | PF_W);              // p_flags = PF_R | PF_W (6)
            write_u64(next_ph + 8,  data_file_off);            // p_offset
            write_u64(next_ph + 16, data_vaddr);               // p_vaddr
            write_u64(next_ph + 24, data_vaddr);               // p_paddr
            write_u64(next_ph + 32, data.size());              // p_filesz
            write_u64(next_ph + 40, data.size() + total_bss_size); // p_memsz (data + BSS)
            write_u64(next_ph + 48, kPageSize);                // p_align = 0x1000
            next_ph += 56;
        }

        // Segment 2: GNU Stack (PT_GNU_STACK: non-executable stack security)
        write_u32(next_ph + 0,  PT_GNU_STACK);                 // p_type = PT_GNU_STACK
        write_u32(next_ph + 4,  PF_R | PF_W);                  // p_flags = PF_R | PF_W (RW, non-executable)
        write_u64(next_ph + 8,  0);
        write_u64(next_ph + 16, 0);
        write_u64(next_ph + 24, 0);
        write_u64(next_ph + 32, 0);
        write_u64(next_ph + 40, 0);
        write_u64(next_ph + 48, 16);                           // 16-byte alignment

        // 3. Write Section Bytes
        std::copy(code.begin(), code.end(), out_bytes.begin() + code_file_off);
        if (!data.empty()) {
            std::copy(data.begin(), data.end(), out_bytes.begin() + data_file_off);
        }

        return true;
    }

    bool link_executable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write_machine_function(fn, obj_bytes, entry_symbol)) return false;
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::ELF);
        dgtl::object::readers::ELFObjectReader reader;
        if (!reader.read(obj_bytes.data(), obj_bytes.size(), obj)) return false;
        return link_executable(obj, out_bytes, 0x400000, entry_symbol);
    }
};

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using ELFLinker = dgtl::linker::elf::ELFLinker;
}



