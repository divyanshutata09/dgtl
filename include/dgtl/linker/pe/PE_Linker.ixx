
/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Sovereign In-Memory PE/COFF Linker (Industrial Grade)
 * Purpose:     Direct in-memory emission of standalone PE32+ (.exe) executables
 *              with complete embedded IAT (kernel32.dll -> ExitProcess),
 *              16-byte Win64 ABI stack alignment, ASLR support, and sub-10ms native execution.
 */

module;

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.linker.pe;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
export import dgtl.linker.pe.sections;

export namespace dgtl::linker::pe {

/* ============================================================================
 * PE Constants & Layout Specifications
 * ========================================================================== */

namespace pe {

inline constexpr uint16_t kDosMagic             = 0x5A4D;     // MZ
inline constexpr uint32_t kPeSignature           = 0x00004550; // PE\0\0

inline constexpr uint16_t kMachineAMD64          = 0x8664;
inline constexpr uint16_t kOptionalHeader64Magic = 0x020B;

inline constexpr uint16_t kCharacteristicsExecutable = 0x0002;
inline constexpr uint16_t kCharacteristicsLargeAddressAware = 0x0020;
inline constexpr uint16_t kCharacteristicsRelocsStripped = 0x0001;

inline constexpr uint16_t kSubsystemWindowsCUI = 0x0003;

inline constexpr uint16_t kDllCharHighEntropyVA = 0x0020;
inline constexpr uint16_t kDllCharDynamicBase   = 0x0040;
inline constexpr uint16_t kDllCharNXCompat      = 0x0100;
inline constexpr uint16_t kDllCharTerminalServerAware = 0x8000;

inline constexpr uint32_t kSectionMemExecute     = 0x20000000;
inline constexpr uint32_t kSectionMemRead        = 0x40000000;
inline constexpr uint32_t kSectionMemWrite       = 0x80000000;
inline constexpr uint32_t kSectionMemDiscardable = 0x02000000;
inline constexpr uint32_t kSectionContainsCode   = 0x00000020;
inline constexpr uint32_t kSectionInitializedData = 0x00000040;

inline constexpr uint32_t kDirImport    = 1;
inline constexpr uint32_t kDirBaseReloc = 5;
inline constexpr uint32_t kDirIAT      = 12;

inline constexpr uint64_t kPreferredImageBase = 0x0000000140000000ULL;
inline constexpr uint32_t kSectionAlignment   = 0x1000;
inline constexpr uint32_t kFileAlignment      = 0x200;
inline constexpr uint32_t kHeadersSize        = 0x400;
inline constexpr uint32_t kDosHeaderOffsetToNT = 0x80;
inline constexpr uint32_t kDefaultTimestamp   = 0x00000000;

inline constexpr uint32_t kStackReserve = 0x00400000; // 4MB
inline constexpr uint32_t kStackCommit  = 0x00100000; // 1MB (guarantees safe headroom before dynamic expansion)
inline constexpr uint32_t kHeapReserve  = 0x00100000; // 1MB
inline constexpr uint32_t kHeapCommit   = 0x00001000; // 4KB

} // namespace pe


/* ============================================================================
 * Low-Level Checked Arithmetic
 * ========================================================================== */

namespace detail {

[[nodiscard]] constexpr bool checked_add_u32(uint32_t a, uint32_t b, uint32_t& out) noexcept {
    if (b > std::numeric_limits<uint32_t>::max() - a) return false;
    out = a + b;
    return true;
}

[[nodiscard]] constexpr uint32_t align_up(uint32_t value, uint32_t alignment) noexcept {
    if (alignment == 0) return value;
    const uint32_t mask = alignment - 1;
    if (value > std::numeric_limits<uint32_t>::max() - mask) return 0;
    return (value + mask) & ~mask;
}

[[nodiscard]] constexpr bool is_power_of_two(uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr bool fits_int32(int64_t value) noexcept {
    return value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max();
}

[[nodiscard]] constexpr bool fits_uint32(uint64_t value) noexcept {
    return value <= std::numeric_limits<uint32_t>::max();
}

} // namespace detail


/* Canonical PE/COFF structures (ImageDosHeader/DosHeader, FileHeader, DataDirectory,
 * OptionalHeader64, SectionHeader, ImportDescriptor) are imported from dgtl.linker.pe.sections */


/* ============================================================================
 * Sovereign PE Linker Class Interface
 * ========================================================================== */

struct SovereignPELinkerOptions {
    uint64_t image_base = pe::kPreferredImageBase;
    uint32_t timestamp = pe::kDefaultTimestamp;
    bool enable_aslr = true;
    bool deterministic = true;
    bool emit_checksum = true;
    bool emit_relocations = true;
    std::string entry_symbol = "main";
    std::string_view import_dll = "KERNEL32.DLL";
    std::string_view entry_import = "ExitProcess";
    std::vector<std::string> additional_imports{};
    std::vector<std::pair<std::string, std::vector<std::string>>> custom_imports{};
    bool sovereign = true; // Pure sovereign freestanding mode: zero external runtime DLLs
    uint64_t stack_reserve = pe::kStackReserve; // Default: 4 MB
    uint64_t stack_commit = pe::kStackCommit;   // Default: 1 MB (guarantees safe headroom before dynamic expansion)
    uint64_t heap_reserve = pe::kHeapReserve;   // Default: 1 MB
    uint64_t heap_commit = pe::kHeapCommit;     // Default: 4 KB
};

class SovereignPELinker {
public:
    enum class Error {
        None,
        EmptyCode,
        MainOffsetOutOfRange,
        CodeTooLarge,
        RDataTooLarge,
        InvalidAlignment,
        SectionLayoutOverflow,
        HeaderLayoutOverflow,
        RelativeCallOutOfRange,
        IATDisplacementOutOfRange,
        InvalidImageSize,
        FileOpenFailed,
        FileWriteFailed
    };

    struct Result {
        bool success{false};
        bool ok{false};
        Error error{Error::None};
        std::string message;

        Result() = default;
        Result(bool s, Error e, std::string msg)
            : success(s), ok(s), error(e), message(std::move(msg)) {}

        explicit operator bool() const noexcept { return success; }
        [[nodiscard]] bool is_ok() const noexcept { return success; }
    };

    using Options = SovereignPELinkerOptions;

private:
    struct Section {
        std::array<uint8_t, 8> name{};
        uint32_t virtual_address{};
        uint32_t virtual_size{};
        uint32_t raw_offset{};
        uint32_t raw_size{};
        uint32_t characteristics{};
        std::vector<uint8_t> data;
    };

    static std::vector<uint8_t> get_sovereign_intrinsic_x64(std::string_view raw_name) {
        std::string_view name = raw_name;
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        std::string_view stripped = name;
        if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
        else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
        else if (stripped.starts_with("__myzon_")) stripped.remove_prefix(8);
        else if (stripped.starts_with("myzon_")) stripped.remove_prefix(6);
        else if (stripped.starts_with("__")) stripped.remove_prefix(2);
        else if (stripped.starts_with("_")) stripped.remove_prefix(1);

        // Hardware FPU / SSE Math Intrinsics
        if (name == "__dgtl_rt_sqrt" || stripped == "sqrt") {
            // sqrtsd xmm0, xmm0; ret
            return { 0xF2, 0x0F, 0x51, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_sqrtf" || stripped == "sqrtf") {
            // sqrtss xmm0, xmm0; ret
            return { 0xF3, 0x0F, 0x51, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_fast_inv_sqrt" || stripped == "fast_inv_sqrt" || stripped == "fast_inv_sqrt_bits") {
            // rsqrtss xmm1, xmm0; movaps xmm0, xmm1; ret
            return { 0xF3, 0x0F, 0x52, 0xC8, 0x0F, 0x28, 0xC1, 0xC3 };
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" ||
            name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs" ||
            stripped == "abs" || stripped == "fabs") {
            // movq rax, xmm0; btr rax, 63; movq xmm0, rax; ret
            return { 0x66, 0x48, 0x0F, 0x7E, 0xC0, 0x48, 0x0F, 0xBA, 0xF0, 0x3F, 0x66, 0x48, 0x0F, 0x6E, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_absf" || name == "dgtl_rt_absf" ||
            name == "__dgtl_rt_fabsf" || name == "dgtl_rt_fabsf" ||
            stripped == "absf" || stripped == "fabsf") {
            // movd eax, xmm0; btr eax, 31; movd xmm0, eax; ret
            return { 0x66, 0x0F, 0x7E, 0xC0, 0x0F, 0xBA, 0xF0, 0x1F, 0x66, 0x0F, 0x6E, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_floor" || name == "dgtl_rt_floor" || stripped == "floor") {
            // roundsd xmm0, xmm0, 1; ret
            return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x01, 0xC3 };
        }
        if (name == "__dgtl_rt_ceil" || name == "dgtl_rt_ceil" || stripped == "ceil") {
            // roundsd xmm0, xmm0, 2; ret
            return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x02, 0xC3 };
        }
        if (name == "__dgtl_rt_round" || name == "dgtl_rt_round" || stripped == "round") {
            // roundsd xmm0, xmm0, 0; ret
            return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x00, 0xC3 };
        }
        if (name == "__dgtl_rt_trunc" || name == "dgtl_rt_trunc" || stripped == "trunc") {
            // roundsd xmm0, xmm0, 3; ret
            return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x03, 0xC3 };
        }
        if (name == "__dgtl_rt_read_tsc" || name == "dgtl_rt_read_tsc" || stripped == "read_tsc") {
            // rdtsc; shl rdx, 32; or rax, rdx; ret
            return { 0x0F, 0x31, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0xC3 };
        }
        if (name == "__dgtl_rt_prefetch" || name == "dgtl_rt_prefetch" ||
            name == "__dgtl_rt_prefetch_t0" || name == "dgtl_rt_prefetch_t0" ||
            stripped == "prefetch" || stripped == "prefetch_t0") {
            // prefetcht0 [rcx]; ret
            return { 0x0F, 0x18, 0x01, 0xC3 };
        }
        if (name == "__dgtl_rt_clflush" || name == "dgtl_rt_clflush" || stripped == "clflush") {
            // clflush [rcx]; ret
            return { 0x0F, 0xAE, 0x39, 0xC3 };
        }
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32" || stripped == "fma_f32") {
            // vfmadd213ss xmm0, xmm1, xmm2; ret
            return { 0xC4, 0xE2, 0x71, 0xA9, 0xC2, 0xC3 };
        }
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64" || stripped == "fma_f64") {
            // vfmadd213sd xmm0, xmm1, xmm2; ret
            return { 0xC4, 0xE2, 0xF9, 0xA9, 0xC2, 0xC3 };
        }
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || stripped == "relu") {
            // xorpd xmm1, xmm1; maxsd xmm0, xmm1; ret
            return { 0x66, 0x0F, 0x57, 0xC9, 0xF2, 0x0F, 0x5F, 0xC1, 0xC3 };
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || stripped == "reluf") {
            // xorps xmm1, xmm1; maxss xmm0, xmm1; ret
            return { 0x0F, 0x57, 0xC9, 0x0F, 0x5F, 0xC1, 0xC3 };
        }
        if (name == "__dgtl_rt_sin" || name == "dgtl_rt_sin" || stripped == "sin") {
            // sub rsp, 8; movsd [rsp], xmm0; fld qword ptr [rsp]; fsin; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFE, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cos" || name == "dgtl_rt_cos" || stripped == "cos") {
            // sub rsp, 8; movsd [rsp], xmm0; fld qword ptr [rsp]; fcos; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFF, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tan" || name == "dgtl_rt_tan" || stripped == "tan") {
            // sub rsp, 8; movsd [rsp], xmm0; fld qword ptr [rsp]; fptan; fstp st(0); fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan" || name == "dgtl_rt_atan" || stripped == "atan") {
            // sub rsp, 8; movsd [rsp], xmm0; fld qword ptr [rsp]; fld1; fpatan; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xE8, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan2" || name == "dgtl_rt_atan2" || stripped == "atan2") {
            // sub rsp, 16; movsd [rsp], xmm0; movsd [rsp+8], xmm1; fld qword ptr [rsp]; fld qword ptr [rsp+8]; fpatan; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 16; ret
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xDD, 0x44, 0x24, 0x08, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_log" || name == "dgtl_rt_log" || stripped == "log") {
            // sub rsp, 8; movsd [rsp], xmm0; fldln2; fld qword ptr [rsp]; fyl2x; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log2" || name == "dgtl_rt_log2" || stripped == "log2") {
            // sub rsp, 8; movsd [rsp], xmm0; fld1; fld qword ptr [rsp]; fyl2x; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xE8, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log10" || name == "dgtl_rt_log10" || stripped == "log10") {
            // sub rsp, 8; movsd [rsp], xmm0; fldlg2; fld qword ptr [rsp]; fyl2x; fstp qword ptr [rsp]; movsd xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEC, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_exp" || name == "dgtl_rt_exp" || stripped == "exp" || stripped == "exp2") {
            // e^x = 2^(x * log2(e)) via x87 f2xm1
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xDC, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_pow" || name == "dgtl_rt_pow" || stripped == "pow") {
            // x^y = 2^(y * log2(x)) via x87
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_sinf" || name == "dgtl_rt_sinf" || stripped == "sinf") {
            // sub rsp, 8; movss [rsp], xmm0; fld dword ptr [rsp]; fsin; fstp dword ptr [rsp]; movss xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFE, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cosf" || name == "dgtl_rt_cosf" || stripped == "cosf") {
            // sub rsp, 8; movss [rsp], xmm0; fld dword ptr [rsp]; fcos; fstp dword ptr [rsp]; movss xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFF, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tanf" || name == "dgtl_rt_tanf" || stripped == "tanf") {
            // sub rsp, 8; movss [rsp], xmm0; fld dword ptr [rsp]; fptan; fstp st(0); fstp dword ptr [rsp]; movss xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_logf" || name == "dgtl_rt_logf" || stripped == "logf") {
            // sub rsp, 8; movss [rsp], xmm0; fldln2; fld dword ptr [rsp]; fyl2x; fstp dword ptr [rsp]; movss xmm0, [rsp]; add rsp, 8; ret
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_expf" || name == "dgtl_rt_expf" || stripped == "expf" || stripped == "exp2f") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xD8, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_powf" || name == "dgtl_rt_powf" || stripped == "powf") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x04, 0xD9, 0x44, 0x24, 0x04, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_fmod" || name == "dgtl_rt_fmod" || stripped == "fmod" || stripped == "fmodf") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF8, 0xDD, 0x1C, 0x24, 0xDD, 0xD8, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_vec4_dot" || name == "dgtl_rt_vec4_dot" || stripped == "vec4_dot") {
            // movups xmm0, [rcx]; movups xmm1, [rdx]; dpps xmm0, xmm1, 0xF1; ret
            return { 0x0F, 0x10, 0x01, 0x0F, 0x10, 0x0A, 0x66, 0x0F, 0x3A, 0x40, 0xC1, 0xF1, 0xC3 };
        }
        if (name == "__dgtl_rt_vec3_cross" || name == "dgtl_rt_vec3_cross" || stripped == "vec3_cross") {
            // dst [rcx], a [rdx], b [r8]
            return {
                0xF3, 0x0F, 0x10, 0x42, 0x04,             // movss xmm0, [rdx+4]
                0xF3, 0x0F, 0x59, 0x40, 0x08,             // mulss xmm0, [r8+8]
                0xF3, 0x0F, 0x10, 0x4A, 0x08,             // movss xmm1, [rdx+8]
                0xF3, 0x0F, 0x59, 0x48, 0x04,             // mulss xmm1, [r8+4]
                0xF3, 0x0F, 0x5C, 0xC1,                   // subss xmm0, xmm1
                0xF3, 0x0F, 0x11, 0x01,                   // movss [rcx], xmm0
                0xF3, 0x0F, 0x10, 0x42, 0x08,             // movss xmm0, [rdx+8]
                0xF3, 0x0F, 0x59, 0x00,                   // mulss xmm0, [r8]
                0xF3, 0x0F, 0x10, 0x0A,                   // movss xmm1, [rdx]
                0xF3, 0x0F, 0x59, 0x48, 0x08,             // mulss xmm1, [r8+8]
                0xF3, 0x0F, 0x5C, 0xC1,                   // subss xmm0, xmm1
                0xF3, 0x0F, 0x11, 0x41, 0x04,             // movss [rcx+4], xmm0
                0xF3, 0x0F, 0x10, 0x02,                   // movss xmm0, [rdx]
                0xF3, 0x0F, 0x59, 0x48, 0x04,             // mulss xmm0, [r8+4]
                0xF3, 0x0F, 0x10, 0x4A, 0x04,             // movss xmm1, [rdx+4]
                0xF3, 0x0F, 0x59, 0x00,                   // mulss xmm1, [r8]
                0xF3, 0x0F, 0x5C, 0xC1,                   // subss xmm0, xmm1
                0xF3, 0x0F, 0x11, 0x41, 0x08,             // movss [rcx+8], xmm0
                0xC3                                      // ret
            };
        }
        if (name == "memcpy" || name == "__dgtl_rt_memcpy" || stripped == "memcpy") {
            // Sovereign memcpy x64: rcx=dst, rdx=src, r8=len, rax=dst
            return {
                0x48, 0x89, 0xC8,                         // mov rax, rcx
                0x49, 0x89, 0xC1,                         // mov r9, r8
                0x49, 0xC1, 0xE9, 0x03,                   // shr r9, 3
                0x4D, 0x85, 0xC9,                         // test r9, r9
                0x74, 0x15,                               // jz +21
                // .qword_loop:
                0x4C, 0x8B, 0x12,                         // mov r10, [rdx]
                0x4C, 0x89, 0x11,                         // mov [rcx], r10
                0x48, 0x83, 0xC2, 0x08,                   // add rdx, 8
                0x48, 0x83, 0xC1, 0x08,                   // add rcx, 8
                0x49, 0xFF, 0xC9,                         // dec r9
                0x75, 0xEE,                               // jnz -18
                // .byte_rem:
                0x49, 0x83, 0xE0, 0x07,                   // and r8, 7
                0x74, 0x10,                               // jz +16
                // .byte_loop:
                0x44, 0x8A, 0x12,                         // mov r10b, [rdx]
                0x44, 0x88, 0x11,                         // mov [rcx], r10b
                0x48, 0xFF, 0xC2,                         // inc rdx
                0x48, 0xFF, 0xC1,                         // inc rcx
                0x49, 0xFF, 0xC8,                         // dec r8
                0x75, 0xF3,                               // jnz -13
                0xC3                                      // ret
            };
        }
        if (name == "memmove" || name == "__dgtl_rt_memmove" || stripped == "memmove") {
            // Sovereign memmove x64: rcx=dst, rdx=src, r8=len, rax=dst
            return {
                0x48, 0x89, 0xC8,                         // mov rax, rcx
                0x48, 0x39, 0xD1,                         // cmp rcx, rdx
                0x74, 0x37,                               // je .done (+55)
                0x72, 0x0F,                               // jb .forward (+15)
                // Check if overlap: if dst >= src + len, forward copy is safe
                0x4C, 0x89, 0xD1,                         // mov r9, rdx
                0x49, 0x01, 0xC1,                         // add r9, r8
                0x49, 0x39, 0xC9,                         // cmp r9, rcx
                0x76, 0x04,                               // jbe .forward (+4)
                // Overlapping backward copy:
                0x4D, 0x85, 0xC0,                         // test r8, r8
                0x74, 0x29,                               // jz .done (+41)
                // .backward_loop:
                0x49, 0xFF, 0xC8,                         // dec r8
                0x44, 0x8A, 0x14, 0x02,                   // mov r10b, [rdx + r8]
                0x44, 0x88, 0x14, 0x01,                   // mov [rcx + r8], r10b
                0x4D, 0x85, 0xC0,                         // test r8, r8
                0x75, 0xF2,                               // jnz .backward_loop (-14)
                0xC3,                                     // ret
                // .forward:
                0x49, 0x89, 0xC1,                         // mov r9, r8
                0x49, 0xC1, 0xE9, 0x03,                   // shr r9, 3
                0x4D, 0x85, 0xC9,                         // test r9, r9
                0x74, 0x15,                               // jz +21
                // .qword_loop:
                0x4C, 0x8B, 0x12,                         // mov r10, [rdx]
                0x4C, 0x89, 0x11,                         // mov [rcx], r10
                0x48, 0x83, 0xC2, 0x08,                   // add rdx, 8
                0x48, 0x83, 0xC1, 0x08,                   // add rcx, 8
                0x49, 0xFF, 0xC9,                         // dec r9
                0x75, 0xEE,                               // jnz -18
                // .byte_rem:
                0x49, 0x83, 0xE0, 0x07,                   // and r8, 7
                0x74, 0x10,                               // jz +16
                // .byte_loop:
                0x44, 0x8A, 0x12,                         // mov r10b, [rdx]
                0x44, 0x88, 0x11,                         // mov [rcx], r10b
                0x48, 0xFF, 0xC2,                         // inc rdx
                0x48, 0xFF, 0xC1,                         // inc rcx
                0x49, 0xFF, 0xC8,                         // dec r8
                0x75, 0xF3,                               // jnz -13
                // .done:
                0xC3                                      // ret
            };
        }
        if (name == "memset" || name == "__dgtl_rt_memset" || stripped == "memset") {
            // Sovereign memset x64: rcx=dst, edx=val, r8=len, rax=dst
            return {
                0x48, 0x89, 0xC8,                         // mov rax, rcx
                0x4D, 0x85, 0xC0,                         // test r8, r8
                0x74, 0x0B,                               // jz +11
                // .loop:
                0x88, 0x11,                               // mov [rcx], dl
                0x48, 0xFF, 0xC1,                         // inc rcx
                0x49, 0xFF, 0xC8,                         // dec r8
                0x75, 0xF7,                               // jnz -9
                0xC3                                      // ret
            };
        }
        if (name == "memcmp" || name == "__dgtl_rt_memcmp" || stripped == "memcmp") {
            // Sovereign memcmp x64: rcx=s1, rdx=s2, r8=len, eax=result
            return {
                0x31, 0xC0,                               // xor eax, eax
                0x4D, 0x85, 0xC0,                         // test r8, r8
                0x74, 0x16,                               // jz +22
                // .loop:
                0x44, 0x0F, 0xB6, 0x09,                   // movzx r9d, byte ptr [rcx]
                0x44, 0x0F, 0xB6, 0x12,                   // movzx r10d, byte ptr [rdx]
                0x45, 0x39, 0xD1,                         // cmp r9d, r10d
                0x75, 0x08,                               // jne +8
                0x48, 0xFF, 0xC1,                         // inc rcx
                0x48, 0xFF, 0xC2,                         // inc rdx
                0x49, 0xFF, 0xC8,                         // dec r8
                0x75, 0xEC,                               // jnz -20
                0xC3,                                     // ret
                // .diff:
                0x45, 0x29, 0xD1,                         // sub r9d, r10d
                0x44, 0x89, 0xC8,                         // mov eax, r9d
                0xC3                                      // ret
            };
        }
        if (name == "__chkstk" || name == "_chkstk" || name == "___chkstk_ms" || name == "__chkstk_ms" || name == "_alloca" || name == "__alloca" || stripped == "chkstk" || stripped == "chkstk_ms" || stripped == "alloca") {
            // Sovereign Windows x64 Dynamic Stack Prober (__chkstk / ___chkstk_ms)
            // Conforms to Microsoft x64 and MinGW GCC ABI:
            // Input: RAX = allocation size in bytes
            // Probes 4096-byte guard pages downward from caller RSP without altering RSP.
            // Preserves RAX, RCX, RDX, and RFLAGS. Returns via standard ret.
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
        if (name == "__acrt_iob_func" || stripped == "acrt_iob_func") {
            // Sovereign UCRT stream sentinel: return nullptr in rax
            return {
                0x31, 0xC0,                               // xor eax, eax
                0xC3                                      // ret
            };
        }
        return {};
    }

public:
    static Result emit_standalone_binary(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_binary,
        const Options& options = Options{})
    {
        // Collect all code sections into a unified code buffer
        struct SecTextOff {
            uint32_t sec_index;
            std::string name;
            uint64_t offset;
        };
        std::vector<uint8_t> user_text;
        std::vector<SecTextOff> section_text_offsets;

        auto is_code_section = [](const auto& s) {
            return s.is_code() || s.name == ".text" || s.name.starts_with(".text$");
        };

        // 1. Place canonical .text first if present
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (sec.name == ".text" && !sec.bytes.empty()) {
                while ((user_text.size() % 16) != 0) user_text.push_back(0x90);
                section_text_offsets.push_back({static_cast<uint32_t>(i), sec.name, user_text.size()});
                user_text.insert(user_text.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }
        // 2. Place remaining code sections
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (sec.name != ".text" && is_code_section(sec) && !sec.bytes.empty()) {
                while ((user_text.size() % 16) != 0) user_text.push_back(0x90);
                section_text_offsets.push_back({static_cast<uint32_t>(i), sec.name, user_text.size()});
                user_text.insert(user_text.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }

        if (user_text.empty()) {
            return failure(Error::EmptyCode, "DGTL PE linker: ObjectFile has no .text code section");
        }

        // Synthesize Sovereign In-Binary Runtime Intrinsics (Zero External DLLs)
        struct SynthesizedIntrinsic {
            std::string name;
            uint64_t text_offset;
        };
        std::vector<SynthesizedIntrinsic> synthesized_intrinsics;

        if (options.sovereign) {
            auto try_synth = [&](std::string_view sym_name) {
                if (sym_name.empty()) return;
                std::string sym(sym_name);
                if (sym.starts_with("__imp_")) sym = sym.substr(6);
                auto code = get_sovereign_intrinsic_x64(sym);
                bool is_intr = !code.empty() || sym.starts_with("__dgtl_rt_") || sym.starts_with("dgtl_rt_") ||
                               sym == "memcpy" || sym == "memset" || sym == "memcmp" || sym == "memmove";
                if (!is_intr) return;

                const auto* existing_sym = obj.find_symbol(sym);
                if (existing_sym && existing_sym->is_defined()) return;

                for (const auto& s : synthesized_intrinsics) {
                    if (s.name == sym) return;
                }

                if (code.empty()) {
                    code = { 0x31, 0xC0, 0xC3 }; // xor eax, eax; ret fallback
                }

                while ((user_text.size() % 16) != 0) user_text.push_back(0x90);
                const uint64_t off = user_text.size();
                user_text.insert(user_text.end(), code.begin(), code.end());
                synthesized_intrinsics.push_back({sym, off});
            };

            for (const auto& rel : obj.relocations()) {
                try_synth(rel.symbol_name);
            }
            for (const auto& s : obj.symbols()) {
                if (!s.is_defined()) try_synth(s.name);
            }
        }

        auto find_text_offset = [&](uint32_t sec_idx, std::string_view name) -> std::optional<uint64_t> {
            if (sec_idx != UINT32_MAX) {
                for (const auto& item : section_text_offsets) {
                    if (item.sec_index == sec_idx) return item.offset;
                }
            }
            if (!name.empty()) {
                for (const auto& item : section_text_offsets) {
                    if (item.name == name) return item.offset;
                }
            }
            return std::nullopt;
        };

        auto is_writable_sec = [](const auto& s) {
            return s.is_writable() || s.is_bss() || s.name.starts_with(".data") || s.name.starts_with(".bss");
        };

        // Collect read-only data sections into user_rdata (.rdata)
        struct SecDataOff {
            uint32_t sec_index;
            std::string name;
            uint64_t offset;
        };
        std::vector<uint8_t> user_rdata;
        std::vector<SecDataOff> section_rdata_offsets;
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (is_code_section(sec) || is_writable_sec(sec)) continue;
            if (sec.name.starts_with(".rdata") || sec.name.starts_with(".rodata") || (!sec.is_code() && !sec.empty())) {
                while ((user_rdata.size() % 16) != 0) user_rdata.push_back(0);
                section_rdata_offsets.push_back({static_cast<uint32_t>(i), sec.name, user_rdata.size()});
                user_rdata.insert(user_rdata.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }
        auto find_rdata_offset = [&](uint32_t sec_idx, std::string_view name) -> std::optional<uint64_t> {
            if (sec_idx != UINT32_MAX) {
                for (const auto& item : section_rdata_offsets) {
                    if (item.sec_index == sec_idx) return item.offset;
                }
            }
            if (!name.empty()) {
                for (const auto& item : section_rdata_offsets) {
                    if (item.name == name) return item.offset;
                }
            }
            return std::nullopt;
        };

        // Collect mutable and zero-initialized (BSS) data sections into user_data (.data)
        std::vector<uint8_t> user_data;
        std::vector<SecDataOff> section_data_offsets;
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (is_code_section(sec) || !is_writable_sec(sec)) continue;
            while ((user_data.size() % 16) != 0) user_data.push_back(0);
            section_data_offsets.push_back({static_cast<uint32_t>(i), sec.name, user_data.size()});
            if (sec.is_bss()) {
                user_data.resize(user_data.size() + sec.bss_size, 0);
            } else {
                user_data.insert(user_data.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }
        auto find_data_offset = [&](uint32_t sec_idx, std::string_view name) -> std::optional<uint64_t> {
            if (sec_idx != UINT32_MAX) {
                for (const auto& item : section_data_offsets) {
                    if (item.sec_index == sec_idx) return item.offset;
                }
            }
            if (!name.empty()) {
                for (const auto& item : section_data_offsets) {
                    if (item.name == name) return item.offset;
                }
            }
            return std::nullopt;
        };

        // Identify undefined external symbols referenced by relocations
        std::vector<std::string> undefined_syms;
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            const auto* sym = obj.find_symbol(rel.symbol_name);
            if (!sym || sym->is_external()) {
                if (std::find(undefined_syms.begin(), undefined_syms.end(), rel.symbol_name) == undefined_syms.end()) {
                    undefined_syms.push_back(rel.symbol_name);
                }
            }
        }

        // Win32 API DLL classification rules
        auto is_valid_c_ident = [](std::string_view name) {
            if (name.empty()) return false;
            if (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_') return false;
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
            }
            return true;
        };

        auto is_ws2_func = [&](std::string_view name) {
            if (!is_valid_c_ident(name)) return false;
            if (name.starts_with("WSA") || name.starts_with("wsa")) return true;
            static constexpr std::string_view ws2_list[] = {
                "socket", "closesocket", "bind", "listen", "accept", "connect", "shutdown",
                "send", "recv", "sendto", "recvfrom", "select",
                "ioctlsocket", "setsockopt", "getsockopt", "getsockname", "getpeername",
                "htons", "ntohs", "htonl", "ntohl",
                "inet_addr", "inet_ntoa", "inet_pton", "inet_ntop",
                "getaddrinfo", "freeaddrinfo", "getnameinfo",
                "gethostbyname", "gethostbyaddr", "gethostname",
                "__WSAFDIsSet"
            };
            for (const auto& fn : ws2_list) {
                if (fn == name) return true;
            }
            return false;
        };

        auto is_ntdll_func = [&](std::string_view name) {
            if (!is_valid_c_ident(name)) return false;
            return name.starts_with("Nt") || name.starts_with("Zw") || name.starts_with("Rtl");
        };

        auto is_ucrt_func = [&](std::string_view name) {
            if (!is_valid_c_ident(name)) return false;
            if (name.starts_with("__acrt") || name.starts_with("__stdio_common")) return true;
            static constexpr std::string_view ucrt_funcs_list[] = {
                "atoll", "strtoll", "strtoull", "strtof", "llabs",
                "log2", "cbrt", "trunc", "round", "remainder", "hypot", "roundf",
                "copysign", "copysignf", "fmin", "fminf", "fmax", "fmaxf",
                "fdim", "fdimf", "nearbyint", "nearbyintf", "rint", "rintf",
                "lrint", "lrintf", "llrint", "llrintf", "tgamma", "tgammaf",
                "lgamma", "lgammaf", "erf", "erff", "erfc", "erfcf",
                "exp2", "exp2f", "expm1", "expm1f", "log1p", "log1pf",
                "logb", "logbf", "ilogb", "ilogbf", "scalbn", "scalbnf",
                "scalbln", "scalblnf", "cbrtf", "hypotf", "truncf",
                "__acrt_iob_func"
            };
            for (const auto& fn : ucrt_funcs_list) {
                if (fn == name) return true;
            }
            return false;
        };

        auto is_msvcrt_func = [&](std::string_view name) {
            if (is_ucrt_func(name)) return false;
            if (!is_valid_c_ident(name)) return false;
            if (name.starts_with("__myzon") || name.starts_with("myzon_") || name.starts_with("dgtl_")) return false;
            if (name == "__iob_func") return true;
            if (name.find("__") != std::string_view::npos && !name.starts_with("__stdio") && !name.starts_with("__p__")) return false;
            if (name.starts_with("_") && !name.starts_with("__") && !name.starts_with("_Z") && !name.starts_with("_Unwind")) return true;
            static constexpr std::string_view crt_funcs[] = {
                "malloc", "free", "realloc", "calloc",
                "printf", "puts", "putchar", "getchar", "snprintf", "sprintf", "fprintf",
                "vsnprintf", "vfprintf", "fopen", "fclose", "fread", "fwrite", "fseek",
                "ftell", "rewind", "fflush", "fgets", "fputs", "sscanf", "scanf",
                "exit", "abort", "qsort", "bsearch", "atoi", "atol", "atoll", "atof",
                "strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof",
                "rand", "srand", "system", "getenv", "abs", "labs", "llabs",
                "memcpy", "memset", "memmove", "memcmp", "memchr",
                "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat", "strncat",
                "strstr", "strchr", "strrchr", "strspn", "strcspn", "strtok",
                "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
                "exp", "log", "log10", "log2", "pow", "sqrt", "cbrt", "ceil", "floor", "trunc",
                "round", "fmod", "remainder", "fabs", "ldexp", "frexp", "modf", "hypot",
                "sinf", "cosf", "tanf", "asinf", "acosf", "atanf", "atan2f", "sqrtf", "powf",
                "expf", "logf", "ceilf", "floorf", "roundf", "fmodf", "fabsf",
                "time", "clock", "difftime", "mktime", "strftime",
                "isalpha", "isdigit", "isalnum", "isspace", "isupper", "islower", "tolower", "toupper",
                "atexit"
            };
            for (const auto& fn : crt_funcs) {
                if (fn == name) return true;
            }
            return false;
        };

        auto is_kernel32_func = [&](std::string_view name) {
            if (!is_valid_c_ident(name)) return false;
            if (name.find("__") != std::string_view::npos || name.starts_with("myzon_") || name.starts_with("dgtl_")) return false;
            static constexpr std::string_view k32_funcs_list[] = {
                "ExitProcess", "VirtualAlloc", "VirtualFree", "VirtualProtect", "VirtualQuery",
                "GetStdHandle", "WriteFile", "ReadFile", "GetProcessHeap", "HeapAlloc", "HeapFree", "HeapReAlloc",
                "GetModuleHandleA", "GetModuleHandleW", "GetProcAddress", "LoadLibraryA", "LoadLibraryW", "FreeLibrary",
                "Sleep", "GetLastError", "SetLastError", "CreateFileA", "CreateFileW", "CloseHandle",
                "GetCommandLineA", "GetCommandLineW", "GetCurrentProcess", "GetCurrentProcessId", "GetCurrentThreadId",
                "QueryPerformanceCounter", "QueryPerformanceFrequency", "GetSystemTimeAsFileTime",
                "InitializeCriticalSection", "DeleteCriticalSection", "EnterCriticalSection", "LeaveCriticalSection",
                "CreateEventA", "CreateEventW", "SetEvent", "ResetEvent", "WaitForSingleObject", "WaitForMultipleObjects",
                "RtlCaptureContext", "RtlLookupFunctionEntry", "RtlVirtualUnwind", "UnhandledExceptionFilter", "SetUnhandledExceptionFilter",
                "WideCharToMultiByte", "MultiByteToWideChar", "FormatMessageA", "FormatMessageW", "LocalFree",
                "GetFileType", "GetConsoleMode", "SetConsoleMode", "GetEnvironmentVariableA", "GetEnvironmentVariableW",
                "SetEnvironmentVariableA", "SetEnvironmentVariableW", "GetTickCount", "GetTickCount64",
                "TlsAlloc", "TlsFree", "TlsGetValue", "TlsSetValue", "SwitchToThread", "CreateThread", "TerminateProcess",
                "FlushFileBuffers", "SetFilePointer", "SetFilePointerEx", "GetFileSize", "GetFileSizeEx"
            };
            for (const auto& fn : k32_funcs_list) {
                if (fn == name) return true;
            }
            return false;
        };

        std::vector<std::string> k32_funcs;
        std::vector<std::string> ws2_funcs;
        std::vector<std::string> ntdll_funcs;
        std::vector<std::string> ucrt_funcs;
        std::vector<std::string> msvcrt_funcs;
        std::vector<std::string> dgtl_rt_funcs;

        const std::string entry_exit = options.entry_import.empty() ? "ExitProcess" : std::string(options.entry_import);
        k32_funcs.push_back(entry_exit);

        auto map_dgtl_rt_to_crt = [](std::string_view name) -> std::string_view {
            if (name == "__dgtl_rt_sin") return "sin";
            if (name == "__dgtl_rt_cos") return "cos";
            if (name == "__dgtl_rt_tan") return "tan";
            if (name == "__dgtl_rt_asin") return "asin";
            if (name == "__dgtl_rt_acos") return "acos";
            if (name == "__dgtl_rt_atan") return "atan";
            if (name == "__dgtl_rt_atan2") return "atan2";
            if (name == "__dgtl_rt_sinh") return "sinh";
            if (name == "__dgtl_rt_cosh") return "cosh";
            if (name == "__dgtl_rt_tanh") return "tanh";
            if (name == "__dgtl_rt_exp") return "exp";
            if (name == "__dgtl_rt_exp2") return "exp2";
            if (name == "__dgtl_rt_log") return "log";
            if (name == "__dgtl_rt_log2") return "log2";
            if (name == "__dgtl_rt_log10") return "log10";
            if (name == "__dgtl_rt_pow") return "pow";
            if (name == "__dgtl_rt_sqrt") return "sqrt";
            if (name == "__dgtl_rt_floor") return "floor";
            if (name == "__dgtl_rt_ceil") return "ceil";
            if (name == "__dgtl_rt_round") return "round";
            if (name == "__dgtl_rt_trunc") return "trunc";
            if (name == "__dgtl_rt_abs") return "abs";
            if (name == "__dgtl_rt_fmod") return "fmod";
            return {};
        };

        for (const auto& sym : undefined_syms) {
            std::string raw_name = sym;
            if (raw_name.starts_with("__imp_")) {
                raw_name = raw_name.substr(6);
            }
            if (raw_name.empty()) continue;

            bool is_synth = false;
            for (const auto& s : synthesized_intrinsics) {
                if (s.name == raw_name) { is_synth = true; break; }
            }
            if (is_synth) continue;

            if (options.sovereign) {
                // In sovereign mode:
                // 1. Math / runtime / intrinsics are synthesized in-binary, not imported
                if (!get_sovereign_intrinsic_x64(raw_name).empty() ||
                    raw_name.starts_with("__dgtl_rt_") || raw_name.starts_with("dgtl_rt_") ||
                    raw_name.starts_with("__myzon_") || raw_name.starts_with("myzon_") ||
                    raw_name == "memcpy" || raw_name == "memset" || raw_name == "memmove" || raw_name == "memcmp") {
                    continue;
                }
                // 2. Network socket API -> WS2_32.DLL
                if (is_ws2_func(raw_name)) {
                    if (std::find(ws2_funcs.begin(), ws2_funcs.end(), raw_name) == ws2_funcs.end()) {
                        ws2_funcs.push_back(raw_name);
                    }
                } else if (is_kernel32_func(raw_name)) {
                    if (std::find(k32_funcs.begin(), k32_funcs.end(), raw_name) == k32_funcs.end()) {
                        k32_funcs.push_back(raw_name);
                    }
                }
                // Zero external CRT/NTDLL imports in sovereign mode
                continue;
            }

            const auto crt_alias = map_dgtl_rt_to_crt(raw_name);
            if (!crt_alias.empty()) {
                if (std::find(msvcrt_funcs.begin(), msvcrt_funcs.end(), std::string(crt_alias)) == msvcrt_funcs.end()) {
                    msvcrt_funcs.push_back(std::string(crt_alias));
                }
                continue;
            }

            if (raw_name.starts_with("__dgtl_rt_") || raw_name.starts_with("dgtl_rt_")) {
                if (std::find(dgtl_rt_funcs.begin(), dgtl_rt_funcs.end(), raw_name) == dgtl_rt_funcs.end()) {
                    dgtl_rt_funcs.push_back(raw_name);
                }
                continue;
            }

            if (is_ws2_func(raw_name)) {
                if (std::find(ws2_funcs.begin(), ws2_funcs.end(), raw_name) == ws2_funcs.end()) {
                    ws2_funcs.push_back(raw_name);
                }
            } else if (is_ntdll_func(raw_name)) {
                if (std::find(ntdll_funcs.begin(), ntdll_funcs.end(), raw_name) == ntdll_funcs.end()) {
                    ntdll_funcs.push_back(raw_name);
                }
            } else if (is_ucrt_func(raw_name)) {
                if (std::find(ucrt_funcs.begin(), ucrt_funcs.end(), raw_name) == ucrt_funcs.end()) {
                    ucrt_funcs.push_back(raw_name);
                }
            } else if (is_msvcrt_func(raw_name)) {
                if (std::find(msvcrt_funcs.begin(), msvcrt_funcs.end(), raw_name) == msvcrt_funcs.end()) {
                    msvcrt_funcs.push_back(raw_name);
                }
            } else if (is_kernel32_func(raw_name)) {
                if (std::find(k32_funcs.begin(), k32_funcs.end(), raw_name) == k32_funcs.end()) {
                    k32_funcs.push_back(raw_name);
                }
            }
        }

        for (const auto& add_fn : options.additional_imports) {
            if (!add_fn.empty() && is_kernel32_func(add_fn) && std::find(k32_funcs.begin(), k32_funcs.end(), add_fn) == k32_funcs.end()) {
                k32_funcs.push_back(add_fn);
            }
        }

        struct ImportSpec {
            std::string dll_name;
            std::vector<std::string> function_names;
        };
        std::vector<ImportSpec> import_specs;
        if (!options.custom_imports.empty()) {
            for (const auto& [dll, fns] : options.custom_imports) {
                if (!dll.empty() && !fns.empty()) {
                    import_specs.push_back({dll, fns});
                }
            }
        } else if (options.sovereign) {
            if (!k32_funcs.empty()) import_specs.push_back({"KERNEL32.DLL", k32_funcs});
            if (!ws2_funcs.empty()) import_specs.push_back({"WS2_32.DLL", ws2_funcs});
        } else {
            if (!k32_funcs.empty()) import_specs.push_back({"KERNEL32.DLL", k32_funcs});
            if (!ws2_funcs.empty()) import_specs.push_back({"WS2_32.DLL", ws2_funcs});
            if (!ntdll_funcs.empty()) import_specs.push_back({"NTDLL.DLL", ntdll_funcs});
            if (!msvcrt_funcs.empty()) import_specs.push_back({"MSVCRT.DLL", msvcrt_funcs});
            if (!ucrt_funcs.empty()) import_specs.push_back({"ucrtbase.dll", ucrt_funcs});
            if (!dgtl_rt_funcs.empty()) import_specs.push_back({"dgtl_rt.dll", dgtl_rt_funcs});
        }

        constexpr uint32_t trampoline_size = 24;
        Section text;
        Section rdata;
        Section data;
        Section reloc;

        initialize_section(text, ".text", pe::kSectionMemExecute | pe::kSectionMemRead | pe::kSectionContainsCode);
        initialize_section(rdata, ".rdata", pe::kSectionMemRead | pe::kSectionInitializedData);
        initialize_section(data, ".data", pe::kSectionMemRead | pe::kSectionMemWrite | pe::kSectionInitializedData);
        initialize_section(reloc, ".reloc", pe::kSectionMemRead | pe::kSectionInitializedData | pe::kSectionMemDiscardable);

        rdata.data.assign(user_rdata.begin(), user_rdata.end());
        data.data.assign(user_data.begin(), user_data.end());
        const bool has_data = !data.data.empty();

        text.data.reserve(trampoline_size + user_text.size() + 1024);
        text.data.resize(trampoline_size, 0x90);
        text.data.insert(text.data.end(), user_text.begin(), user_text.end());

        // Align text before emitting import thunks
        while ((text.data.size() % 16) != 0) text.data.push_back(0x90);

        // Map each imported function to its jump thunk offset in .text
        struct ThunkOffsetEntry {
            std::string name;
            uint32_t offset;
        };
        std::vector<ThunkOffsetEntry> func_to_thunk_offset;
        for (const auto& spec : import_specs) {
            for (const auto& fn : spec.function_names) {
                func_to_thunk_offset.push_back({fn, static_cast<uint32_t>(text.data.size())});
                // Emit 8-byte stub: FF 25 00 00 00 00 90 90 (jmp qword ptr [rip + disp32])
                text.data.push_back(0xFF);
                text.data.push_back(0x25);
                text.data.push_back(0x00);
                text.data.push_back(0x00);
                text.data.push_back(0x00);
                text.data.push_back(0x00);
                text.data.push_back(0x90);
                text.data.push_back(0x90);
            }
        }

        constexpr uint32_t text_rva = 0x1000;
        const uint32_t text_virtual_size = static_cast<uint32_t>(text.data.size());
        const uint32_t text_raw_size = detail::align_up(text_virtual_size, pe::kFileAlignment);

        const uint32_t rdata_rva = detail::align_up(text_rva + text_virtual_size, pe::kSectionAlignment);

        // Align user rdata before embedding imports
        while ((rdata.data.size() % 16) != 0) rdata.data.push_back(0);
        const uint32_t import_base_in_rdata = static_cast<uint32_t>(rdata.data.size());

        // Build Import Table directly inside .rdata (Modern industry-grade consolidated layout)
        const uint32_t num_dlls = static_cast<uint32_t>(import_specs.size());
        const uint32_t descriptor_count = num_dlls + 1u;
        const uint32_t descriptor_size = descriptor_count * static_cast<uint32_t>(sizeof(ImportDescriptor));

        uint32_t total_thunk_count = 0;
        uint32_t total_hint_size = 0;
        uint32_t total_dll_name_size = 0;

        for (const auto& spec : import_specs) {
            const uint32_t count_with_null = static_cast<uint32_t>(spec.function_names.size() + 1u);
            total_thunk_count += count_with_null;
            for (const auto& fn_name : spec.function_names) {
                uint32_t hsz = 2u + static_cast<uint32_t>(fn_name.size()) + 1u;
                if ((hsz & 1u) != 0u) hsz++;
                total_hint_size += hsz;
            }
            total_dll_name_size += static_cast<uint32_t>(spec.dll_name.size() + 1u);
        }

        const uint32_t total_ilt_size = total_thunk_count * static_cast<uint32_t>(sizeof(uint64_t));
        const uint32_t total_iat_size = total_thunk_count * static_cast<uint32_t>(sizeof(uint64_t));

        const uint32_t desc_off = import_base_in_rdata;
        const uint32_t ilt_off  = detail::align_up(desc_off + descriptor_size, 8);
        const uint32_t iat_off  = detail::align_up(ilt_off + total_ilt_size, 8);
        const uint32_t hint_off = detail::align_up(iat_off + total_iat_size, 8);
        const uint32_t dll_off  = detail::align_up(hint_off + total_hint_size, 8);
        const uint32_t import_total_size = (dll_off + total_dll_name_size) - import_base_in_rdata;

        rdata.data.resize(import_base_in_rdata + import_total_size + 64, 0);

        uint32_t cur_ilt_off  = ilt_off;
        uint32_t cur_iat_off  = iat_off;
        uint32_t cur_hint_off = hint_off;
        uint32_t cur_dll_off  = dll_off;

        struct IatRvaEntry {
            std::string name;
            uint32_t rva;
        };
        std::vector<IatRvaEntry> func_to_iat_rva;
        auto find_iat_rva = [&](std::string_view name) -> std::optional<uint32_t> {
            for (const auto& entry : func_to_iat_rva) {
                if (entry.name == name) return entry.rva;
            }
            const auto crt_name = map_dgtl_rt_to_crt(name);
            if (!crt_name.empty()) {
                for (const auto& entry : func_to_iat_rva) {
                    if (entry.name == crt_name) return entry.rva;
                }
            }
            if (!name.starts_with("__myzon_") && !name.starts_with("__builtin_") && !name.starts_with("__dgtl_rt_")) {
                if (size_t pos = name.rfind("__"); pos != std::string_view::npos && pos > 0) {
                    std::string_view bare = name.substr(pos + 2);
                    for (const auto& entry : func_to_iat_rva) {
                        if (entry.name == bare) return entry.rva;
                    }
                }
            }
            return std::nullopt;
        };

        for (uint32_t d = 0; d < num_dlls; ++d) {
            const auto& spec = import_specs[d];
            const uint32_t dll_ilt_rva  = rdata_rva + cur_ilt_off;
            const uint32_t dll_iat_rva  = rdata_rva + cur_iat_off;
            const uint32_t dll_name_rva = rdata_rva + cur_dll_off;

            const uint32_t d_off = desc_off + d * static_cast<uint32_t>(sizeof(ImportDescriptor));
            write_u32(rdata.data, d_off + 0, dll_ilt_rva);
            write_u32(rdata.data, d_off + 4, 0);
            write_u32(rdata.data, d_off + 8, 0);
            write_u32(rdata.data, d_off + 12, dll_name_rva);
            write_u32(rdata.data, d_off + 16, dll_iat_rva);

            write_bytes(rdata.data, cur_dll_off, spec.dll_name);
            cur_dll_off += static_cast<uint32_t>(spec.dll_name.size() + 1u);

            for (size_t f = 0; f < spec.function_names.size(); ++f) {
                const auto& fn_name = spec.function_names[f];
                const uint32_t fn_hint_rva = rdata_rva + cur_hint_off;
                const uint32_t fn_iat_rva  = rdata_rva + cur_iat_off;

                func_to_iat_rva.push_back({fn_name, fn_iat_rva});

                write_u64(rdata.data, cur_ilt_off, fn_hint_rva);
                write_u64(rdata.data, cur_iat_off, fn_hint_rva);

                write_u16(rdata.data, cur_hint_off, static_cast<uint16_t>(f));
                write_bytes(rdata.data, cur_hint_off + 2, fn_name);

                cur_ilt_off += static_cast<uint32_t>(sizeof(uint64_t));
                cur_iat_off += static_cast<uint32_t>(sizeof(uint64_t));

                uint32_t hsz = 2u + static_cast<uint32_t>(fn_name.size()) + 1u;
                if ((hsz & 1u) != 0u) hsz++;
                cur_hint_off += hsz;
            }

            write_u64(rdata.data, cur_ilt_off, 0ULL);
            write_u64(rdata.data, cur_iat_off, 0ULL);
            cur_ilt_off += static_cast<uint32_t>(sizeof(uint64_t));
            cur_iat_off += static_cast<uint32_t>(sizeof(uint64_t));
        }

        const uint32_t rdata_virtual_size = static_cast<uint32_t>(rdata.data.size());
        const uint32_t data_rva = detail::align_up(rdata_rva + rdata_virtual_size, pe::kSectionAlignment);

        // Patch Import Thunks (FF 25 [disp32]) to jump through IAT slots
        for (const auto& thunk : func_to_thunk_offset) {
            if (auto iat_rva = find_iat_rva(thunk.name)) {
                const uint32_t thunk_rva = text_rva + thunk.offset;
                const int64_t disp = static_cast<int64_t>(*iat_rva) - static_cast<int64_t>(thunk_rva + 6);
                write_i32(text.data, thunk.offset + 2, static_cast<int32_t>(disp));
            }
        }

        // Build Symbol RVA resolution table
        struct SymRvaEntry {
            std::string name;
            uint64_t rva;
        };
        std::vector<SymRvaEntry> symbol_rva_map;
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined()) {
                const bool is_sec_sym = sym.name.starts_with(".") ||
                                       sym.name == ".text" || sym.name == ".data" ||
                                       sym.name == ".rdata" || sym.name == ".bss";
                const uint64_t eff_val = is_sec_sym ? 0 : sym.value;
                if (sym.section_index < obj.sections().size()) {
                    const auto& sec = obj.sections()[sym.section_index];
                    if (auto toff = find_text_offset(sym.section_index, sec.name)) {
                        symbol_rva_map.push_back({sym.name, text_rva + trampoline_size + *toff + eff_val});
                    } else if (auto roff = find_rdata_offset(sym.section_index, sec.name)) {
                        symbol_rva_map.push_back({sym.name, rdata_rva + *roff + eff_val});
                    } else if (auto doff = find_data_offset(sym.section_index, sec.name)) {
                        symbol_rva_map.push_back({sym.name, data_rva + *doff + eff_val});
                    } else {
                        symbol_rva_map.push_back({sym.name, text_rva + trampoline_size + eff_val});
                    }
                } else {
                    symbol_rva_map.push_back({sym.name, text_rva + trampoline_size + eff_val});
                }
            }
        }
        for (const auto& synth : synthesized_intrinsics) {
            symbol_rva_map.push_back({synth.name, text_rva + trampoline_size + synth.text_offset});
            symbol_rva_map.push_back({"__imp_" + synth.name, text_rva + trampoline_size + synth.text_offset});
            std::string_view stripped = synth.name;
            if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
            else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("__myzon_")) stripped.remove_prefix(8);
            else if (stripped.starts_with("myzon_")) stripped.remove_prefix(6);
            else if (stripped.starts_with("__")) stripped.remove_prefix(2);
            else if (stripped.starts_with("_")) stripped.remove_prefix(1);
            if (!stripped.empty()) {
                std::string s(stripped);
                symbol_rva_map.push_back({s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"__imp_" + s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"__dgtl_rt_" + s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"__imp___dgtl_rt_" + s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"dgtl_rt_" + s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"__myzon_" + s, text_rva + trampoline_size + synth.text_offset});
                symbol_rva_map.push_back({"myzon_" + s, text_rva + trampoline_size + synth.text_offset});
            }
        }
        for (const auto& thunk : func_to_thunk_offset) {
            symbol_rva_map.push_back({thunk.name, text_rva + thunk.offset});
            std::string dgtl_alias = "__dgtl_rt_" + thunk.name;
            if (map_dgtl_rt_to_crt(dgtl_alias) == thunk.name) {
                symbol_rva_map.push_back({dgtl_alias, text_rva + thunk.offset});
            }
        }
        for (const auto& iat : func_to_iat_rva) {
            symbol_rva_map.push_back({"__imp_" + iat.name, iat.rva});
            std::string dgtl_alias = "__dgtl_rt_" + iat.name;
            if (map_dgtl_rt_to_crt(dgtl_alias) == iat.name) {
                symbol_rva_map.push_back({"__imp_" + dgtl_alias, iat.rva});
            }
        }

        std::sort(symbol_rva_map.begin(), symbol_rva_map.end(),
                  [](const SymRvaEntry& a, const SymRvaEntry& b) {
                      return a.name < b.name;
                  });

        auto last = std::unique(symbol_rva_map.begin(), symbol_rva_map.end(),
                                [](const SymRvaEntry& a, const SymRvaEntry& b) {
                                    return a.name == b.name;
                                });
        symbol_rva_map.erase(last, symbol_rva_map.end());

        auto find_symbol_rva = [&](std::string_view name) -> std::optional<uint64_t> {
            auto it = std::lower_bound(symbol_rva_map.begin(), symbol_rva_map.end(), name,
                                       [](const SymRvaEntry& entry, std::string_view val) {
                                           return entry.name < val;
                                       });
            if (it != symbol_rva_map.end() && it->name == name) {
                return it->rva;
            }
            // Dynamic alias resolution fallback
            const auto crt_name = map_dgtl_rt_to_crt(name);
            if (!crt_name.empty()) {
                auto it_crt = std::lower_bound(symbol_rva_map.begin(), symbol_rva_map.end(), crt_name,
                                               [](const SymRvaEntry& entry, std::string_view val) {
                                                   return entry.name < val;
                                               });
                if (it_crt != symbol_rva_map.end() && it_crt->name == crt_name) {
                    return it_crt->rva;
                }
            }
            if (name.starts_with("__imp_")) {
                const auto stripped = name.substr(6);
                const auto crt_stripped = map_dgtl_rt_to_crt(stripped);
                if (!crt_stripped.empty()) {
                    std::string imp_crt = "__imp_" + std::string(crt_stripped);
                    auto it_imp = std::lower_bound(symbol_rva_map.begin(), symbol_rva_map.end(), imp_crt,
                                                   [](const SymRvaEntry& entry, std::string_view val) {
                                                       return entry.name < val;
                                                   });
                    if (it_imp != symbol_rva_map.end() && it_imp->name == imp_crt) {
                        return it_imp->rva;
                    }
                }
            }
            if (!name.starts_with("__myzon_") && !name.starts_with("__builtin_") && !name.starts_with("__dgtl_rt_") && !name.starts_with(".")) {
                if (size_t pos = name.rfind("__"); pos != std::string_view::npos && pos > 0) {
                    std::string_view bare = name.substr(pos + 2);
                    auto it_bare = std::lower_bound(symbol_rva_map.begin(), symbol_rva_map.end(), bare,
                                                    [](const SymRvaEntry& entry, std::string_view val) {
                                                        return entry.name < val;
                                                    });
                    if (it_bare != symbol_rva_map.end() && it_bare->name == bare) {
                        return it_bare->rva;
                    }
                }
            }

            // Bare-name alias-stripping fallback
            std::string_view stripped = name;
            if (stripped.starts_with("__imp_")) stripped.remove_prefix(6);
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
                    auto it_cand = std::lower_bound(symbol_rva_map.begin(), symbol_rva_map.end(), cand,
                                                    [](const SymRvaEntry& entry, std::string_view val) {
                                                        return entry.name < val;
                                                    });
                    if (it_cand != symbol_rva_map.end() && it_cand->name == cand) {
                        return it_cand->rva;
                    }
                }
            }
            if (name == ".text") return text_rva + trampoline_size;
            if (name == ".rdata" || name == ".rodata") return rdata_rva;
            if (name == ".data") {
                if (auto doff = find_data_offset(UINT32_MAX, ".data")) return data_rva + *doff;
                return data_rva;
            }
            if (name == ".bss") {
                if (auto doff = find_data_offset(UINT32_MAX, ".bss")) return data_rva + *doff;
                return data_rva;
            }
            return std::nullopt;
        };

        // Resolve main function entry point
        std::string_view target_entry = !options.entry_symbol.empty() ? options.entry_symbol : "main";
        uint64_t resolved_entry_rva = 0;
        if (auto rva = find_symbol_rva(target_entry)) {
            resolved_entry_rva = *rva;
        } else if (auto rva = find_symbol_rva("DGTL_kernel_entry")) {
            resolved_entry_rva = *rva;
        } else if (auto rva = find_symbol_rva("main")) {
            resolved_entry_rva = *rva;
        } else {
            resolved_entry_rva = text_rva + trampoline_size;
        }

        // Patch Entry Trampoline: sub rsp, 28h; call entry; mov ecx, eax; call ExitProcess; add rsp, 28h; ret
        {
            const int64_t main_rva = static_cast<int64_t>(resolved_entry_rva);
            const int64_t call_next_rva = static_cast<int64_t>(text_rva) + 9;
            const int64_t call_disp = main_rva - call_next_rva;

            if (!detail::fits_int32(call_disp)) {
                return failure(Error::RelativeCallOutOfRange, "DGTL PE linker: main function is outside rel32 range");
            }

            text.data[0] = 0x48; text.data[1] = 0x83; text.data[2] = 0xEC; text.data[3] = 0x28; // sub rsp, 0x28
            text.data[4] = 0xE8; // call rel32
            write_i32(text.data, 5, static_cast<int32_t>(call_disp));
            text.data[9] = 0x89; text.data[10] = 0xC1; // mov ecx, eax

            uint32_t exit_iat_rva = rdata_rva + iat_off;
            if (auto rva = find_iat_rva(entry_exit)) {
                exit_iat_rva = *rva;
            }
            const int64_t iat_disp = static_cast<int64_t>(exit_iat_rva) - static_cast<int64_t>(text_rva + 17);
            if (!detail::fits_int32(iat_disp)) {
                return failure(Error::IATDisplacementOutOfRange, "DGTL PE linker: IAT is outside RIP-relative range");
            }
            text.data[11] = 0xFF; text.data[12] = 0x15; // call qword ptr [rip + disp32]
            write_i32(text.data, 13, static_cast<int32_t>(iat_disp));
            text.data[17] = 0x48; text.data[18] = 0x83; text.data[19] = 0xC4; text.data[20] = 0x28; // add rsp, 0x28
            text.data[21] = 0xC3; // ret
        }

        struct BaseRelocEntry {
            uint32_t rva;
            bool is_64;
        };
        std::vector<BaseRelocEntry> base_relocs;

        // Apply all Object Relocations (rel32, abs64, abs32)
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;

            uint64_t target_rva = 0;
            if (auto rva = find_symbol_rva(rel.symbol_name)) {
                target_rva = *rva;
            } else if (rel.symbol_name.starts_with("__imp_")) {
                if (auto imp_rva = find_iat_rva(rel.symbol_name.substr(6))) {
                    target_rva = *imp_rva;
                } else {
                    continue;
                }
            } else if (auto imp_rva = find_iat_rva(rel.symbol_name)) {
                target_rva = *imp_rva;
            } else {
                // Defensive sovereignty safeguard: If an unresolvable call rel32 resides in .text,
                // replace the 5-byte 'call' instruction (0xE8 xx xx xx xx) with a standard multi-byte NOP
                // (0x0F 0x1F 0x44 0x00 0x00). This guarantees that unresolved calls NEVER silently execute
                // 'call $+5', which pushes RIP, misaligns RSP, and corrupts saved frames on 'ret'.
                if (rel.kind == dgtl::object::RelocationKind::PCRel32 || rel.kind == dgtl::object::RelocationKind::PLT32) {
                    uint64_t t_base = 0;
                    if (rel.section_index < obj.sections().size()) {
                        if (auto toff = find_text_offset(rel.section_index, obj.sections()[rel.section_index].name)) {
                            t_base = *toff;
                        }
                    }
                    const uint64_t place_offset = trampoline_size + t_base + rel.offset;
                    if (place_offset > 0 && place_offset + 4 <= text.data.size() && text.data[place_offset - 1] == 0xE8) {
                        text.data[place_offset - 1] = 0x0F;
                        text.data[place_offset]     = 0x1F;
                        text.data[place_offset + 1] = 0x44;
                        text.data[place_offset + 2] = 0x00;
                        text.data[place_offset + 3] = 0x00;
                    }
                }
                continue;
            }

            // Check if relocation target is in text, rdata, or data
            bool in_rdata_sec = false;
            bool in_data_sec = false;
            uint64_t sec_base = 0;
            uint64_t text_base = 0;
            if (rel.section_index < obj.sections().size()) {
                const auto& target_sec = obj.sections()[rel.section_index];
                if (auto roff = find_rdata_offset(rel.section_index, target_sec.name)) {
                    in_rdata_sec = true;
                    sec_base = *roff;
                } else if (auto doff = find_data_offset(rel.section_index, target_sec.name)) {
                    in_data_sec = true;
                    sec_base = *doff;
                } else if (auto toff = find_text_offset(rel.section_index, target_sec.name)) {
                    text_base = *toff;
                }
            }

            if (in_rdata_sec) {
                const uint64_t place_offset = sec_base + rel.offset;
                if (place_offset + 4 <= rdata.data.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::Absolute64 && place_offset + 8 <= rdata.data.size()) {
                        const uint64_t abs_addr = options.image_base + target_rva + rel.addend;
                        write_u64(rdata.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(rdata_rva + place_offset), true});
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute32) {
                        const uint32_t abs_addr = static_cast<uint32_t>(options.image_base + target_rva + rel.addend);
                        write_u32(rdata.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(rdata_rva + place_offset), false});
                    } else if (rel.kind == dgtl::object::RelocationKind::SecRel32) {
                        const uint32_t rva = static_cast<uint32_t>(target_rva + rel.addend);
                        write_u32(rdata.data, place_offset, rva);
                    } else if (rel.kind == dgtl::object::RelocationKind::PCRel32) {
                        const uint64_t reloc_rva = rdata_rva + place_offset;
                        const int64_t delta = static_cast<int64_t>(target_rva) + rel.addend - static_cast<int64_t>(reloc_rva);
                        write_i32(rdata.data, place_offset, static_cast<int32_t>(delta));
                    }
                }
            } else if (in_data_sec) {
                const uint64_t place_offset = sec_base + rel.offset;
                if (place_offset + 4 <= data.data.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::Absolute64 && place_offset + 8 <= data.data.size()) {
                        const uint64_t abs_addr = options.image_base + target_rva + rel.addend;
                        write_u64(data.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(data_rva + place_offset), true});
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute32) {
                        const uint32_t abs_addr = static_cast<uint32_t>(options.image_base + target_rva + rel.addend);
                        write_u32(data.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(data_rva + place_offset), false});
                    } else if (rel.kind == dgtl::object::RelocationKind::SecRel32) {
                        const uint32_t rva = static_cast<uint32_t>(target_rva + rel.addend);
                        write_u32(data.data, place_offset, rva);
                    } else if (rel.kind == dgtl::object::RelocationKind::PCRel32) {
                        const uint64_t reloc_rva = data_rva + place_offset;
                        const int64_t delta = static_cast<int64_t>(target_rva) + rel.addend - static_cast<int64_t>(reloc_rva);
                        write_i32(data.data, place_offset, static_cast<int32_t>(delta));
                    }
                }
            } else {
                const uint64_t place_offset = trampoline_size + text_base + rel.offset;
                const uint64_t place_rva = text_rva + place_offset;

                if (place_offset + 4 <= text.data.size()) {
                    if (rel.kind == dgtl::object::RelocationKind::PCRel32 || rel.kind == dgtl::object::RelocationKind::PLT32) {
                        const uint64_t reloc_rva = place_rva;
                        const int64_t delta = static_cast<int64_t>(target_rva) + rel.addend - static_cast<int64_t>(reloc_rva);
                        write_i32(text.data, place_offset, static_cast<int32_t>(delta));
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute64 && place_offset + 8 <= text.data.size()) {
                        const uint64_t abs_addr = options.image_base + target_rva + rel.addend;
                        write_u64(text.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(place_rva), true});
                    } else if (rel.kind == dgtl::object::RelocationKind::Absolute32) {
                        const uint32_t abs_addr = static_cast<uint32_t>(options.image_base + target_rva + rel.addend);
                        write_u32(text.data, place_offset, abs_addr);
                        base_relocs.push_back({static_cast<uint32_t>(place_rva), false});
                    } else if (rel.kind == dgtl::object::RelocationKind::SecRel32) {
                        const uint32_t rva = static_cast<uint32_t>(target_rva + rel.addend);
                        write_u32(text.data, place_offset, rva);
                    }
                }
            }
        }

        // Build base relocation blocks for .reloc if relocations were requested and present
        if (options.emit_relocations && !base_relocs.empty()) {
            std::sort(base_relocs.begin(), base_relocs.end(), [](const auto& a, const auto& b) {
                return a.rva < b.rva;
            });
            base_relocs.erase(std::unique(base_relocs.begin(), base_relocs.end(), [](const auto& a, const auto& b) {
                return a.rva == b.rva;
            }), base_relocs.end());

            size_t r_idx = 0;
            while (r_idx < base_relocs.size()) {
                const uint32_t page_rva = base_relocs[r_idx].rva & ~0xFFFu;
                const size_t page_start = r_idx;
                while (r_idx < base_relocs.size() && (base_relocs[r_idx].rva & ~0xFFFu) == page_rva) {
                    ++r_idx;
                }
                const uint32_t entry_count = static_cast<uint32_t>(r_idx - page_start);
                uint32_t block_size = 8u + (entry_count * 2u);
                const bool needs_padding = (entry_count % 2) != 0;
                if (needs_padding) block_size += 2u;

                const size_t block_start = reloc.data.size();
                reloc.data.resize(block_start + block_size, 0);

                write_u32(reloc.data, block_start + 0, page_rva);
                write_u32(reloc.data, block_start + 4, block_size);

                size_t entry_off = block_start + 8;
                for (size_t k = page_start; k < r_idx; ++k) {
                    const uint16_t off_in_page = static_cast<uint16_t>(base_relocs[k].rva & 0xFFFu);
                    const uint16_t r_type = base_relocs[k].is_64 ? 0xA000u : 0x3000u; // IMAGE_REL_BASED_DIR64 / HIGHLOW
                    write_u16(reloc.data, entry_off, r_type | off_in_page);
                    entry_off += 2;
                }
                if (needs_padding) {
                    write_u16(reloc.data, entry_off, 0); // IMAGE_REL_BASED_ABSOLUTE padding
                }
            }
        }

        // Section Virtual and Raw size metrics
        text.virtual_address = text_rva;
        text.virtual_size = static_cast<uint32_t>(text.data.size());
        text.raw_size = detail::align_up(text.virtual_size, pe::kFileAlignment);

        rdata.virtual_address = rdata_rva;
        rdata.virtual_size = static_cast<uint32_t>(rdata.data.size());
        rdata.raw_size = detail::align_up(rdata.virtual_size, pe::kFileAlignment);

        data.virtual_address = data_rva;
        data.virtual_size = static_cast<uint32_t>(data.data.size());
        data.raw_size = detail::align_up(data.virtual_size, pe::kFileAlignment);

        const uint32_t post_data_rva = has_data
            ? detail::align_up(data_rva + data.virtual_size, pe::kSectionAlignment)
            : data_rva;
        const uint32_t reloc_rva = post_data_rva;

        const bool has_reloc = options.emit_relocations && !base_relocs.empty();
        if (has_reloc) {
            reloc.virtual_address = reloc_rva;
            reloc.virtual_size = static_cast<uint32_t>(reloc.data.size());
            reloc.raw_size = detail::align_up(reloc.virtual_size, pe::kFileAlignment);
        }

        uint32_t cursor = pe::kHeadersSize;
        text.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, text.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .text file offset overflow");
        }
        rdata.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, rdata.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .rdata file offset overflow");
        }
        if (has_data) {
            data.raw_offset = cursor;
            if (!detail::checked_add_u32(cursor, data.raw_size, cursor)) {
                return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .data file offset overflow");
            }
        }
        if (has_reloc) {
            reloc.raw_offset = cursor;
            if (!detail::checked_add_u32(cursor, reloc.raw_size, cursor)) {
                return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .reloc file offset overflow");
            }
        }

        const uint32_t file_size = cursor;
        uint32_t image_end = text_rva + detail::align_up(text.virtual_size, pe::kSectionAlignment);
        image_end = std::max(image_end, rdata_rva + detail::align_up(rdata.virtual_size, pe::kSectionAlignment));
        if (has_data) {
            image_end = std::max(image_end, data_rva + detail::align_up(data.virtual_size, pe::kSectionAlignment));
        }
        if (has_reloc) {
            image_end = std::max(image_end, reloc_rva + detail::align_up(reloc.virtual_size, pe::kSectionAlignment));
        }
        const uint32_t size_of_image = detail::align_up(image_end, pe::kSectionAlignment);

        DosHeader dos{};
        dos.e_magic = pe::kDosMagic;
        dos.e_lfanew = pe::kDosHeaderOffsetToNT;

        std::vector<SectionHeader> section_headers;
        section_headers.push_back({});
        fill_section_header(section_headers.back(), text);

        section_headers.push_back({});
        fill_section_header(section_headers.back(), rdata);

        if (has_data) {
            section_headers.push_back({});
            fill_section_header(section_headers.back(), data);
        }

        if (has_reloc) {
            section_headers.push_back({});
            fill_section_header(section_headers.back(), reloc);
        }

        FileHeader file_hdr{};
        file_hdr.Machine = pe::kMachineAMD64;
        file_hdr.NumberOfSections = static_cast<uint16_t>(section_headers.size());
        file_hdr.TimeDateStamp = options.deterministic ? 0 : options.timestamp;
        file_hdr.SizeOfOptionalHeader = 240;
        file_hdr.Characteristics = pe::kCharacteristicsExecutable | pe::kCharacteristicsLargeAddressAware;
        if (!has_reloc) {
            file_hdr.Characteristics |= pe::kCharacteristicsRelocsStripped;
        }

        OptionalHeader64 opt{};
        opt.Magic = pe::kOptionalHeader64Magic;
        opt.MajorLinkerVersion = 14;
        opt.SizeOfCode = text.raw_size;
        opt.SizeOfInitializedData = rdata.raw_size + (has_data ? data.raw_size : 0) + (has_reloc ? reloc.raw_size : 0);
        opt.AddressOfEntryPoint = text_rva;
        opt.BaseOfCode = text_rva;
        opt.ImageBase = options.image_base;
        opt.SectionAlignment = pe::kSectionAlignment;
        opt.FileAlignment = pe::kFileAlignment;
        opt.MajorOperatingSystemVersion = 6;
        opt.MajorSubsystemVersion = 6;
        opt.SizeOfImage = size_of_image;
        opt.SizeOfHeaders = pe::kHeadersSize;
        opt.Subsystem = pe::kSubsystemWindowsCUI;
        opt.DllCharacteristics = pe::kDllCharNXCompat | pe::kDllCharTerminalServerAware;
        if (options.enable_aslr && has_reloc) {
            opt.DllCharacteristics |= pe::kDllCharDynamicBase | pe::kDllCharHighEntropyVA;
        }
        opt.SizeOfStackReserve = options.stack_reserve ? options.stack_reserve : pe::kStackReserve;
        opt.SizeOfStackCommit = options.stack_commit ? options.stack_commit : pe::kStackCommit;
        opt.SizeOfHeapReserve = options.heap_reserve ? options.heap_reserve : pe::kHeapReserve;
        opt.SizeOfHeapCommit = options.heap_commit ? options.heap_commit : pe::kHeapCommit;
        opt.NumberOfRvaAndSizes = 16;
        opt.DataDirectories[pe::kDirImport].VirtualAddress = rdata_rva + desc_off;
        opt.DataDirectories[pe::kDirImport].Size = descriptor_size;
        opt.DataDirectories[pe::kDirIAT].VirtualAddress = rdata_rva + iat_off;
        opt.DataDirectories[pe::kDirIAT].Size = total_iat_size;
        if (has_reloc) {
            opt.DataDirectories[pe::kDirBaseReloc].VirtualAddress = reloc_rva;
            opt.DataDirectories[pe::kDirBaseReloc].Size = reloc.virtual_size;
        } else {
            opt.DataDirectories[pe::kDirBaseReloc].VirtualAddress = 0;
            opt.DataDirectories[pe::kDirBaseReloc].Size = 0;
        }

        out_binary.assign(file_size, 0);
        write_dos_header(out_binary, 0, dos);

        static constexpr std::array<uint8_t, 64> dos_stub = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
            'D','G','T','L',' ','P','E','3','2','+','\r','\n','$',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        copy_bytes(out_binary, 64, dos_stub);
        write_u32(out_binary, pe::kDosHeaderOffsetToNT, pe::kPeSignature);

        size_t nt_cursor = pe::kDosHeaderOffsetToNT + 4;
        write_file_header(out_binary, nt_cursor, file_hdr);
        nt_cursor += 20;
        write_optional_header(out_binary, nt_cursor, opt);
        nt_cursor += 240;

        for (const auto& sh : section_headers) {
            write_section_header(out_binary, nt_cursor, sh);
            nt_cursor += 40;
        }

        copy_bytes(out_binary, text.raw_offset, text.data);
        copy_bytes(out_binary, rdata.raw_offset, rdata.data);
        if (has_data) {
            copy_bytes(out_binary, data.raw_offset, data.data);
        }
        if (has_reloc) {
            copy_bytes(out_binary, reloc.raw_offset, reloc.data);
        }

        if (options.emit_checksum) {
            const uint32_t checksum = calculate_pe_checksum(out_binary);
            write_u32(out_binary, pe::kDosHeaderOffsetToNT + 4 + 20 + 64, checksum);
        }

        return {true, Error::None, ""};
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        const dgtl::object::ObjectFile& obj,
        const Options& options = Options{})
    {
        std::vector<uint8_t> binary;
        auto res = emit_standalone_binary(obj, binary, options);
        if (!res.success) return res;

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return failure(Error::FileOpenFailed, "DGTL PE linker: unable to open output file");
        }
        out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
        out.flush();
        if (!out.good()) {
            return failure(Error::FileWriteFailed, "DGTL PE linker: failed while writing output file");
        }
        return {true, Error::None, ""};
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        std::span<const uint8_t> code_bytes,
        std::span<const uint8_t> user_rdata,
        uint32_t main_func_offset = 0)
    {
        return emit_standalone_executable(output_path, code_bytes, user_rdata, main_func_offset, Options{});
    }

    static Result emit_standalone_executable(
        const std::string& output_path,
        std::span<const uint8_t> code_bytes,
        std::span<const uint8_t> user_rdata,
        uint32_t main_func_offset,
        const Options& options)
    {
        std::vector<uint8_t> binary;
        auto res = emit_standalone_binary(code_bytes, user_rdata, binary, main_func_offset, options);
        if (!res.success) return res;

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return failure(Error::FileOpenFailed, "DGTL PE linker: unable to open output file");
        }
        out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
        out.flush();
        if (!out.good()) {
            return failure(Error::FileWriteFailed, "DGTL PE linker: failed while writing output file");
        }
        return {true, Error::None, ""};
    }

    static Result emit_standalone_binary(
        std::span<const uint8_t> code_bytes,
        std::span<const uint8_t> user_rdata,
        std::vector<uint8_t>& binary,
        uint32_t main_func_offset = 0,
        const Options& options = Options{})
    {
        if (code_bytes.empty()) {
            return failure(Error::EmptyCode, "DGTL PE linker: code section is empty");
        }
        if (main_func_offset >= code_bytes.size()) {
            return failure(Error::MainOffsetOutOfRange, "DGTL PE linker: main_func_offset is outside code bounds");
        }
        if (!detail::is_power_of_two(pe::kFileAlignment) || !detail::is_power_of_two(pe::kSectionAlignment)) {
            return failure(Error::InvalidAlignment, "DGTL PE linker: invalid PE alignment configuration");
        }
        if (!detail::fits_uint32(code_bytes.size()) || !detail::fits_uint32(user_rdata.size())) {
            return failure(Error::CodeTooLarge, "DGTL PE linker: input exceeds PE32+ 32-bit RVA limits");
        }

        Section text;
        Section rdata;
        Section reloc;

        initialize_section(text, ".text", pe::kSectionMemExecute | pe::kSectionMemRead | pe::kSectionContainsCode);
        initialize_section(rdata, ".rdata", pe::kSectionMemRead | pe::kSectionInitializedData);
        initialize_section(reloc, ".reloc", pe::kSectionMemRead | pe::kSectionInitializedData | pe::kSectionMemDiscardable);

        rdata.data.assign(user_rdata.begin(), user_rdata.end());

        constexpr uint32_t trampoline_size = 24;
        text.data.reserve(trampoline_size + code_bytes.size());
        text.data.resize(trampoline_size, 0x90);
        text.data.insert(text.data.end(), code_bytes.begin(), code_bytes.end());

        constexpr uint32_t text_rva = 0x1000;
        const uint32_t text_virtual_size = static_cast<uint32_t>(text.data.size());
        const uint32_t text_raw_size = detail::align_up(text_virtual_size, pe::kFileAlignment);

        if (text_raw_size == 0) {
            return failure(Error::SectionLayoutOverflow, "DGTL PE linker: .text alignment overflow");
        }

        const uint32_t rdata_rva = detail::align_up(text_rva + text_virtual_size, pe::kSectionAlignment);

        // Build Import Table Layout directly inside .rdata (Consolidated PE Architecture)
        struct ImportSpec {
            std::string dll_name;
            std::vector<std::string> function_names;
        };
        std::vector<ImportSpec> import_specs;
        if (!options.custom_imports.empty()) {
            for (const auto& [dll, fns] : options.custom_imports) {
                if (!dll.empty() && !fns.empty()) {
                    import_specs.push_back({dll, fns});
                }
            }
        }
        if (import_specs.empty()) {
            ImportSpec default_spec;
            default_spec.dll_name = options.import_dll.empty() ? "KERNEL32.DLL" : std::string(options.import_dll);
            default_spec.function_names.push_back(options.entry_import.empty() ? "ExitProcess" : std::string(options.entry_import));
            for (const auto& add_fn : options.additional_imports) {
                if (!add_fn.empty()) {
                    default_spec.function_names.push_back(add_fn);
                }
            }
            import_specs.push_back(std::move(default_spec));
        }

        // Align user rdata before embedding imports
        while ((rdata.data.size() % 16) != 0) rdata.data.push_back(0);
        const uint32_t import_base_in_rdata = static_cast<uint32_t>(rdata.data.size());

        const uint32_t num_dlls = static_cast<uint32_t>(import_specs.size());
        const uint32_t descriptor_count = num_dlls + 1u;
        const uint32_t descriptor_size = descriptor_count * static_cast<uint32_t>(sizeof(ImportDescriptor));

        uint32_t total_thunk_count = 0;
        uint32_t total_hint_size = 0;
        uint32_t total_dll_name_size = 0;

        for (const auto& spec : import_specs) {
            const uint32_t count_with_null = static_cast<uint32_t>(spec.function_names.size() + 1u);
            total_thunk_count += count_with_null;
            for (const auto& fn_name : spec.function_names) {
                uint32_t hsz = 2u + static_cast<uint32_t>(fn_name.size()) + 1u;
                if ((hsz & 1u) != 0u) hsz++;
                total_hint_size += hsz;
            }
            total_dll_name_size += static_cast<uint32_t>(spec.dll_name.size() + 1u);
        }

        const uint32_t total_ilt_size = total_thunk_count * static_cast<uint32_t>(sizeof(uint64_t));
        const uint32_t total_iat_size = total_thunk_count * static_cast<uint32_t>(sizeof(uint64_t));

        const uint32_t desc_off = import_base_in_rdata;
        const uint32_t ilt_off  = detail::align_up(desc_off + descriptor_size, 8);
        const uint32_t iat_off  = detail::align_up(ilt_off + total_ilt_size, 8);
        const uint32_t hint_off = detail::align_up(iat_off + total_iat_size, 8);
        const uint32_t dll_off  = detail::align_up(hint_off + total_hint_size, 8);
        const uint32_t import_total_size = (dll_off + total_dll_name_size) - import_base_in_rdata;

        rdata.data.resize(import_base_in_rdata + import_total_size + 64, 0);

        uint32_t cur_ilt_off  = ilt_off;
        uint32_t cur_iat_off  = iat_off;
        uint32_t cur_hint_off = hint_off;
        uint32_t cur_dll_off  = dll_off;

        struct RawIatEntry {
            std::string name;
            uint32_t rva;
        };
        std::vector<RawIatEntry> func_to_iat_rva;

        for (uint32_t d = 0; d < num_dlls; ++d) {
            const auto& spec = import_specs[d];
            const uint32_t dll_ilt_rva  = rdata_rva + cur_ilt_off;
            const uint32_t dll_iat_rva  = rdata_rva + cur_iat_off;
            const uint32_t dll_name_rva = rdata_rva + cur_dll_off;

            const uint32_t d_off = desc_off + d * static_cast<uint32_t>(sizeof(ImportDescriptor));
            write_u32(rdata.data, d_off + 0, dll_ilt_rva);
            write_u32(rdata.data, d_off + 4, 0);
            write_u32(rdata.data, d_off + 8, 0);
            write_u32(rdata.data, d_off + 12, dll_name_rva);
            write_u32(rdata.data, d_off + 16, dll_iat_rva);

            write_bytes(rdata.data, cur_dll_off, spec.dll_name);
            cur_dll_off += static_cast<uint32_t>(spec.dll_name.size() + 1u);

            for (size_t f = 0; f < spec.function_names.size(); ++f) {
                const auto& fn_name = spec.function_names[f];
                const uint32_t fn_hint_rva = rdata_rva + cur_hint_off;
                const uint32_t fn_iat_rva  = rdata_rva + cur_iat_off;

                func_to_iat_rva.push_back({fn_name, fn_iat_rva});

                write_u64(rdata.data, cur_ilt_off, fn_hint_rva);
                write_u64(rdata.data, cur_iat_off, fn_hint_rva);

                write_u16(rdata.data, cur_hint_off, static_cast<uint16_t>(f));
                write_bytes(rdata.data, cur_hint_off + 2, fn_name);

                cur_ilt_off += static_cast<uint32_t>(sizeof(uint64_t));
                cur_iat_off += static_cast<uint32_t>(sizeof(uint64_t));

                uint32_t hsz = 2u + static_cast<uint32_t>(fn_name.size()) + 1u;
                if ((hsz & 1u) != 0u) hsz++;
                cur_hint_off += hsz;
            }

            write_u64(rdata.data, cur_ilt_off, 0ULL);
            write_u64(rdata.data, cur_iat_off, 0ULL);
            cur_ilt_off += static_cast<uint32_t>(sizeof(uint64_t));
            cur_iat_off += static_cast<uint32_t>(sizeof(uint64_t));
        }

        const uint32_t rdata_virtual_size = static_cast<uint32_t>(rdata.data.size());
        const uint32_t reloc_rva = detail::align_up(rdata_rva + rdata_virtual_size, pe::kSectionAlignment);

        bool has_reloc = options.emit_relocations;
        if (has_reloc) {
            reloc.data.clear();
            reloc.virtual_address = reloc_rva;
            reloc.virtual_size = static_cast<uint32_t>(reloc.data.size());
            reloc.raw_size = detail::align_up(reloc.virtual_size, pe::kFileAlignment);
        }

        text.virtual_address = text_rva;
        text.virtual_size = text_virtual_size;

        rdata.virtual_address = rdata_rva;
        rdata.virtual_size = rdata_virtual_size;

        text.raw_size = text_raw_size;
        rdata.raw_size = detail::align_up(rdata.virtual_size, pe::kFileAlignment);

        uint32_t cursor = pe::kHeadersSize;
        text.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, text.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .text file offset overflow");
        }
        rdata.raw_offset = cursor;
        if (!detail::checked_add_u32(cursor, rdata.raw_size, cursor)) {
            return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .rdata file offset overflow");
        }
        if (has_reloc) {
            reloc.raw_offset = cursor;
            if (!detail::checked_add_u32(cursor, reloc.raw_size, cursor)) {
                return failure(Error::HeaderLayoutOverflow, "DGTL PE linker: .reloc file offset overflow");
            }
        }

        const uint32_t file_size = cursor;
        uint32_t image_end = text_rva + detail::align_up(text.virtual_size, pe::kSectionAlignment);
        image_end = std::max(image_end, rdata_rva + detail::align_up(rdata.virtual_size, pe::kSectionAlignment));
        if (has_reloc) {
            image_end = std::max(image_end, reloc_rva + detail::align_up(reloc.virtual_size, pe::kSectionAlignment));
        }
        const uint32_t size_of_image = detail::align_up(image_end, pe::kSectionAlignment);

        // Patch Entry Trampoline
        {
            const int64_t main_rva = static_cast<int64_t>(text_rva) + static_cast<int64_t>(trampoline_size) + static_cast<int64_t>(main_func_offset);
            const int64_t call_next_rva = static_cast<int64_t>(text_rva) + 9;
            const int64_t call_disp = main_rva - call_next_rva;

            if (!detail::fits_int32(call_disp)) {
                return failure(Error::RelativeCallOutOfRange, "DGTL PE linker: main function is outside rel32 range");
            }

            text.data[0] = 0x48; text.data[1] = 0x83; text.data[2] = 0xEC; text.data[3] = 0x28; // sub rsp, 0x28
            text.data[4] = 0xE8; // call rel32
            write_i32(text.data, 5, static_cast<int32_t>(call_disp));
            text.data[9] = 0x89; text.data[10] = 0xC1; // mov ecx, eax

            uint32_t exit_iat_rva = rdata_rva + iat_off;
            for (const auto& entry : func_to_iat_rva) {
                if (entry.name == options.entry_import) {
                    exit_iat_rva = entry.rva;
                    break;
                }
            }
            const int64_t iat_disp = static_cast<int64_t>(exit_iat_rva) - static_cast<int64_t>(text_rva + 17);
            if (!detail::fits_int32(iat_disp)) {
                return failure(Error::IATDisplacementOutOfRange, "DGTL PE linker: IAT is outside RIP-relative range");
            }
            text.data[11] = 0xFF; text.data[12] = 0x15; // call qword ptr [rip + disp32]
            write_i32(text.data, 13, static_cast<int32_t>(iat_disp));
            text.data[17] = 0x48; text.data[18] = 0x83; text.data[19] = 0xC4; text.data[20] = 0x28; // add rsp, 0x28
            text.data[21] = 0xC3; // ret
        }

        std::vector<SectionHeader> section_headers;
        section_headers.push_back({});
        fill_section_header(section_headers.back(), text);

        section_headers.push_back({});
        fill_section_header(section_headers.back(), rdata);

        if (has_reloc) {
            section_headers.push_back({});
            fill_section_header(section_headers.back(), reloc);
        }

        DosHeader dos{};
        dos.e_magic = pe::kDosMagic;
        dos.e_lfanew = pe::kDosHeaderOffsetToNT;

        FileHeader file_hdr{};
        file_hdr.Machine = pe::kMachineAMD64;
        file_hdr.NumberOfSections = static_cast<uint16_t>(section_headers.size());
        file_hdr.TimeDateStamp = options.deterministic ? 0 : options.timestamp;
        file_hdr.SizeOfOptionalHeader = 240;
        file_hdr.Characteristics = pe::kCharacteristicsExecutable | pe::kCharacteristicsLargeAddressAware;
        if (!has_reloc) {
            file_hdr.Characteristics |= pe::kCharacteristicsRelocsStripped;
        }

        OptionalHeader64 opt{};
        opt.Magic = pe::kOptionalHeader64Magic;
        opt.MajorLinkerVersion = 14;
        opt.SizeOfCode = text.raw_size;
        opt.SizeOfInitializedData = rdata.raw_size + (has_reloc ? reloc.raw_size : 0);
        opt.AddressOfEntryPoint = text_rva;
        opt.BaseOfCode = text_rva;
        opt.ImageBase = options.image_base;
        opt.SectionAlignment = pe::kSectionAlignment;
        opt.FileAlignment = pe::kFileAlignment;
        opt.MajorOperatingSystemVersion = 6;
        opt.MajorSubsystemVersion = 6;
        opt.SizeOfImage = size_of_image;
        opt.SizeOfHeaders = pe::kHeadersSize;
        opt.Subsystem = pe::kSubsystemWindowsCUI;
        opt.DllCharacteristics = pe::kDllCharNXCompat | pe::kDllCharTerminalServerAware;
        if (options.enable_aslr && has_reloc) {
            opt.DllCharacteristics |= pe::kDllCharDynamicBase | pe::kDllCharHighEntropyVA;
        }
        opt.SizeOfStackReserve = pe::kStackReserve;
        opt.SizeOfStackCommit = pe::kStackCommit;
        opt.SizeOfHeapReserve = pe::kHeapReserve;
        opt.SizeOfHeapCommit = pe::kHeapCommit;
        opt.NumberOfRvaAndSizes = 16;
        opt.DataDirectories[pe::kDirImport].VirtualAddress = rdata_rva + desc_off;
        opt.DataDirectories[pe::kDirImport].Size = descriptor_size;
        opt.DataDirectories[pe::kDirIAT].VirtualAddress = rdata_rva + iat_off;
        opt.DataDirectories[pe::kDirIAT].Size = total_iat_size;
        if (has_reloc) {
            opt.DataDirectories[pe::kDirBaseReloc].VirtualAddress = reloc_rva;
            opt.DataDirectories[pe::kDirBaseReloc].Size = reloc.virtual_size;
        } else {
            opt.DataDirectories[pe::kDirBaseReloc].VirtualAddress = 0;
            opt.DataDirectories[pe::kDirBaseReloc].Size = 0;
        }

        binary.assign(file_size, 0);
        write_dos_header(binary, 0, dos);

        static constexpr std::array<uint8_t, 64> dos_stub = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
            'D','G','T','L',' ','P','E','3','2','+','\r','\n','$',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        };
        copy_bytes(binary, 64, dos_stub);
        write_u32(binary, pe::kDosHeaderOffsetToNT, pe::kPeSignature);

        size_t nt_cursor = pe::kDosHeaderOffsetToNT + 4;
        write_file_header(binary, nt_cursor, file_hdr);
        nt_cursor += 20;
        write_optional_header(binary, nt_cursor, opt);
        nt_cursor += 240;

        for (const auto& sh : section_headers) {
            write_section_header(binary, nt_cursor, sh);
            nt_cursor += 40;
        }

        copy_bytes(binary, text.raw_offset, text.data);
        copy_bytes(binary, rdata.raw_offset, rdata.data);
        if (has_reloc) {
            copy_bytes(binary, reloc.raw_offset, reloc.data);
        }

        if (options.emit_checksum) {
            const uint32_t checksum = calculate_pe_checksum(binary);
            write_u32(binary, pe::kDosHeaderOffsetToNT + 4 + 20 + 64, checksum);
        }

        return {true, Error::None, ""};
    }

private:
    static Result failure(Error error, std::string message) noexcept {
        return {false, error, std::move(message)};
    }

    static void initialize_section(Section& section, std::string_view name, uint32_t characteristics) noexcept {
        section.name.fill(0);
        const size_t count = std::min(name.size(), section.name.size());
        for (size_t i = 0; i < count; ++i) section.name[i] = static_cast<uint8_t>(name[i]);
        section.characteristics = characteristics;
    }

    static void fill_section_header(SectionHeader& header, const Section& section) noexcept {
        header = {};
        std::memcpy(header.Name, section.name.data(), 8);
        header.VirtualSize = section.virtual_size;
        header.VirtualAddress = section.virtual_address;
        header.SizeOfRawData = section.raw_size;
        header.PointerToRawData = section.raw_offset;
        header.Characteristics = section.characteristics;
    }

    static void write_u16(std::vector<uint8_t>& out, size_t offset, uint16_t value) noexcept {
        if (offset + 2 <= out.size()) {
            out[offset + 0] = static_cast<uint8_t>(value & 0xFF);
            out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        }
    }

    static void write_u32(std::vector<uint8_t>& out, size_t offset, uint32_t value) noexcept {
        if (offset + 4 <= out.size()) {
            out[offset + 0] = static_cast<uint8_t>(value & 0xFF);
            out[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
            out[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
            out[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        }
    }

    static void write_i32(std::vector<uint8_t>& out, size_t offset, int32_t value) noexcept {
        write_u32(out, offset, static_cast<uint32_t>(value));
    }

    static void write_u64(std::vector<uint8_t>& out, size_t offset, uint64_t value) noexcept {
        if (offset + 8 <= out.size()) {
            for (unsigned i = 0; i < 8; ++i) {
                out[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
            }
        }
    }

    static void write_bytes(std::vector<uint8_t>& out, size_t offset, std::string_view value) noexcept {
        if (offset > out.size()) return;
        const size_t count = std::min(out.size() - offset, value.size());
        for (size_t i = 0; i < count; ++i) out[offset + i] = static_cast<uint8_t>(value[i]);
    }

    static void copy_bytes(std::vector<uint8_t>& out, size_t offset, std::span<const uint8_t> data) noexcept {
        if (offset > out.size()) return;
        const size_t count = std::min(out.size() - offset, data.size());
        std::copy_n(data.begin(), count, out.begin() + offset);
    }

    static void write_dos_header(std::vector<uint8_t>& out, size_t offset, const DosHeader& h) noexcept {
        write_u16(out, offset + 0, h.e_magic);
        write_u16(out, offset + 60, h.e_lfanew);
    }

    static void write_file_header(std::vector<uint8_t>& out, size_t offset, const FileHeader& h) noexcept {
        write_u16(out, offset + 0, h.Machine);
        write_u16(out, offset + 2, h.NumberOfSections);
        write_u32(out, offset + 4, h.TimeDateStamp);
        write_u32(out, offset + 8, h.PointerToSymbolTable);
        write_u32(out, offset + 12, h.NumberOfSymbols);
        write_u16(out, offset + 16, h.SizeOfOptionalHeader);
        write_u16(out, offset + 18, h.Characteristics);
    }

    static void write_optional_header(std::vector<uint8_t>& out, size_t offset, const OptionalHeader64& h) noexcept {
        size_t p = offset;
        write_u16(out, p, h.Magic); p += 2;
        if (p + 2 <= out.size()) {
            out[p++] = h.MajorLinkerVersion;
            out[p++] = h.MinorLinkerVersion;
        }
        write_u32(out, p, h.SizeOfCode); p += 4;
        write_u32(out, p, h.SizeOfInitializedData); p += 4;
        write_u32(out, p, h.SizeOfUninitializedData); p += 4;
        write_u32(out, p, h.AddressOfEntryPoint); p += 4;
        write_u32(out, p, h.BaseOfCode); p += 4;
        write_u64(out, p, h.ImageBase); p += 8;
        write_u32(out, p, h.SectionAlignment); p += 4;
        write_u32(out, p, h.FileAlignment); p += 4;
        write_u16(out, p, h.MajorOperatingSystemVersion); p += 2;
        write_u16(out, p, h.MinorOperatingSystemVersion); p += 2;
        write_u16(out, p, h.MajorImageVersion); p += 2;
        write_u16(out, p, h.MinorImageVersion); p += 2;
        write_u16(out, p, h.MajorSubsystemVersion); p += 2;
        write_u16(out, p, h.MinorSubsystemVersion); p += 2;
        write_u32(out, p, h.Win32VersionValue); p += 4;
        write_u32(out, p, h.SizeOfImage); p += 4;
        write_u32(out, p, h.SizeOfHeaders); p += 4;
        write_u32(out, p, h.CheckSum); p += 4;
        write_u16(out, p, h.Subsystem); p += 2;
        write_u16(out, p, h.DllCharacteristics); p += 2;
        write_u64(out, p, h.SizeOfStackReserve); p += 8;
        write_u64(out, p, h.SizeOfStackCommit); p += 8;
        write_u64(out, p, h.SizeOfHeapReserve); p += 8;
        write_u64(out, p, h.SizeOfHeapCommit); p += 8;
        write_u32(out, p, h.LoaderFlags); p += 4;
        write_u32(out, p, h.NumberOfRvaAndSizes); p += 4;

        for (const auto& directory : h.DataDirectories) {
            write_u32(out, p, directory.VirtualAddress); p += 4;
            write_u32(out, p, directory.Size); p += 4;
        }
    }

    static void write_section_header(std::vector<uint8_t>& out, size_t offset, const SectionHeader& h) noexcept {
        if (offset + 40 > out.size()) return;
        for (size_t i = 0; i < 8; ++i) out[offset + i] = h.Name[i];
        write_u32(out, offset + 8, h.VirtualSize);
        write_u32(out, offset + 12, h.VirtualAddress);
        write_u32(out, offset + 16, h.SizeOfRawData);
        write_u32(out, offset + 20, h.PointerToRawData);
        write_u32(out, offset + 24, h.PointerToRelocations);
        write_u32(out, offset + 28, h.PointerToLinenumbers);
        write_u16(out, offset + 32, h.NumberOfRelocations);
        write_u16(out, offset + 34, h.NumberOfLinenumbers);
        write_u32(out, offset + 36, h.Characteristics);
    }

    [[nodiscard]] static uint32_t calculate_pe_checksum(const std::vector<uint8_t>& image) noexcept {
        constexpr size_t checksum_offset = 0x80 + 4 + 20 + 64;
        uint64_t sum = 0;
        const size_t size = image.size();
        for (size_t i = 0; i < size; i += 2) {
            uint16_t word = 0;
            if (i != checksum_offset) {
                word = static_cast<uint16_t>(image[i]);
                if (i + 1 < size) word |= static_cast<uint16_t>(image[i + 1]) << 8;
            }
            sum += word;
            sum = (sum & 0xFFFFULL) + (sum >> 16);
        }
        sum = (sum & 0xFFFFULL) + (sum >> 16);
        sum += size;
        return static_cast<uint32_t>(sum);
    }
};

} // namespace dgtl::linker::pe

export namespace dgtl::backend::linker::pe {
    using SovereignPELinker = dgtl::linker::pe::SovereignPELinker;
    using SovereignPELinkerOptions = dgtl::linker::pe::SovereignPELinkerOptions;
}


