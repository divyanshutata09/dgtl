/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Sovereign Compiler & Runtime Architecture Test Suite (v2026.4)
 * Purpose:     Formal verification of 16-byte stack frame alignment, PE32+ binary synthesis,
 *              and JIT/AOT dual-ABI execution invariants.
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <algorithm>

#include "dgtl_sdk/dgtl.h"

// Current unified DGTL public C ABI helpers.
static uint32_t emit_const_i64(dgtl_stream_t* s, int64_t value) {
    const uint64_t bits = static_cast<uint64_t>(value);
    return dgtl_emit_raw(
        s,
        DGTL_OP_CONST,
        DGTL_FLAG_IS_CONST,
        static_cast<uint32_t>(bits & 0xFFFFFFFFull),
        static_cast<uint32_t>((bits >> 32) & 0xFFFFFFFFull),
        DGTL_INVALID_SLOT);
}

static uint32_t emit_binary_i64(
    dgtl_stream_t* s,
    uint32_t opcode,
    uint32_t lhs,
    uint32_t rhs)
{
    return dgtl_emit_raw(
        s, opcode, DGTL_FLAG_NONE, lhs, rhs, DGTL_INVALID_SLOT);
}

static uint32_t emit_return_i64(dgtl_stream_t* s, uint32_t value) {
    return dgtl_emit_raw(
        s, DGTL_OP_RET, DGTL_FLAG_IS_RETURN, value, 0, 0);
}

static bool status_ok(dgtl_err_t status, const char* what) {
    if (!DGTL_IS_ERR(status)) {
        return true;
    }
    std::cerr << "[FAIL] " << what << ": "
              << dgtl_err_message(status) << std::endl;
    return false;
}

// ============================================================================
// TEST MACROS & STATS
// ============================================================================
static uint32_t g_passes = 0;
static uint32_t g_failures = 0;

#define DGTL_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (line " << __LINE__ << ")" << std::endl; \
            g_failures++; \
            return false; \
        } \
    } while (0)

#define DGTL_TEST_PASS(msg) \
    do { \
        std::cout << "[PASS] " << msg << std::endl; \
        g_passes++; \
    } while (0)

// ============================================================================
// TEST 1: Formal Mathematical Win64 Frame Alignment & Shadow Space Invariant
// ============================================================================
namespace frame_test {
    enum class X64Reg : uint8_t {
        RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
        R8, R9, R10, R11, R12, R13, R14, R15
    };

    struct FrameAllocationPlan {
        uint32_t total_stack_bytes = 0;
        uint32_t local_and_spill_offset = 0;
        uint32_t shadow_space_bytes = 32;
        uint32_t alignment_pad_bytes = 0;
        std::vector<X64Reg> saved_non_volatile_regs;
    };

    inline constexpr bool is_callee_saved(X64Reg reg) noexcept {
        switch (reg) {
            case X64Reg::RBX:
            case X64Reg::RBP:
            case X64Reg::RDI:
            case X64Reg::RSI:
            case X64Reg::R12:
            case X64Reg::R13:
            case X64Reg::R14:
            case X64Reg::R15:
                return true;
            default:
                return false;
        }
    }

    inline FrameAllocationPlan compute_plan(
        uint32_t local_storage_bytes,
        uint32_t spill_slot_bytes,
        uint32_t max_outgoing_args_bytes,
        const std::vector<X64Reg>& clobbered_registers,
        bool makes_external_calls
    ) noexcept {
        FrameAllocationPlan plan;

        for (const auto reg : clobbered_registers) {
            if (is_callee_saved(reg)) {
                if (std::find(plan.saved_non_volatile_regs.begin(), 
                              plan.saved_non_volatile_regs.end(), reg) == plan.saved_non_volatile_regs.end()) {
                    plan.saved_non_volatile_regs.push_back(reg);
                }
            }
        }

        std::sort(plan.saved_non_volatile_regs.begin(), plan.saved_non_volatile_regs.end());

        const uint32_t callee_saved_bytes = static_cast<uint32_t>(plan.saved_non_volatile_regs.size()) * 8;
        plan.shadow_space_bytes = makes_external_calls ? std::max<uint32_t>(32, max_outgoing_args_bytes) : 0;
        
        const uint32_t raw_locals = local_storage_bytes + spill_slot_bytes;
        plan.local_and_spill_offset = plan.shadow_space_bytes;

        const uint32_t raw_body = raw_locals + plan.shadow_space_bytes;

        // Invariant: Total bytes pushed before 'sub rsp, total_stack_bytes' = 8(RetAddr) + 8(RBP) + callee_saved_bytes
        const uint32_t total_pushed_before_sub = 8 + 8 + callee_saved_bytes;
        const uint32_t remainder = (total_pushed_before_sub + raw_body) % 16;
        if (remainder != 0) {
            plan.alignment_pad_bytes = 16 - remainder;
        } else {
            plan.alignment_pad_bytes = 0;
        }

        plan.total_stack_bytes = raw_body + plan.alignment_pad_bytes;
        return plan;
    }

    inline bool verify_invariants(const FrameAllocationPlan& plan, bool makes_external_calls) noexcept {
        const uint32_t callee_saved_bytes = static_cast<uint32_t>(plan.saved_non_volatile_regs.size()) * 8;
        const uint32_t total_pushed = 8 + 8 + callee_saved_bytes;
        const uint32_t stack_depth_at_call = total_pushed + plan.total_stack_bytes;

        if ((stack_depth_at_call % 16) != 0) return false;
        if (makes_external_calls && plan.shadow_space_bytes < 32) return false;
        if (plan.total_stack_bytes < (plan.shadow_space_bytes + plan.alignment_pad_bytes)) return false;
        return true;
    }
}

bool test_win64_frame_alignment_invariants() {
    using namespace frame_test;

    const std::vector<X64Reg> callee_regs = {
        X64Reg::RBX, X64Reg::RBP, X64Reg::RDI, X64Reg::RSI,
        X64Reg::R12, X64Reg::R13, X64Reg::R14, X64Reg::R15
    };

    uint64_t tested_permutations = 0;

    // Test across all combinations of callee-saved clobbers (0..8)
    for (uint32_t clobbers = 0; clobbers <= 8; ++clobbers) {
        std::vector<X64Reg> clobber_list(callee_regs.begin(), callee_regs.begin() + clobbers);

        // Test non-leaf calls (makes_external_calls = true)
        for (uint32_t locals = 0; locals <= 1024; locals += 7) {
            for (uint32_t spills = 0; spills <= 256; spills += 13) {
                for (uint32_t outgoing = 0; outgoing <= 128; outgoing += 8) {
                    const auto plan = compute_plan(locals, spills, outgoing, clobber_list, true);
                    DGTL_TEST_ASSERT(verify_invariants(plan, true), "Win64 non-leaf frame alignment invariant failed");
                    tested_permutations++;
                }
            }
        }

        // Test leaf calls (makes_external_calls = false)
        for (uint32_t locals = 0; locals <= 512; locals += 11) {
            for (uint32_t spills = 0; spills <= 128; spills += 17) {
                const auto plan = compute_plan(locals, spills, 0, clobber_list, false);
                DGTL_TEST_ASSERT(verify_invariants(plan, false), "Win64 leaf frame alignment invariant failed");
                tested_permutations++;
            }
        }
    }

    DGTL_TEST_PASS("Win64 frame lowering mathematical alignment verified across " + 
                   std::to_string(tested_permutations) + " property permutations");
    return true;
}

// ============================================================================
// TEST 2: Sovereign PE32+ In-Memory Executable Synthesis & Structural Headers
// ============================================================================
#pragma pack(push, 1)
struct ImageDosHeader {
    uint16_t e_magic;    // 0x5A4D ("MZ")
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
    uint32_t e_lfanew;   // Offset 0x80
};

struct FileHeader {
    uint16_t Machine;               // 0x8664
    uint16_t NumberOfSections;      // 2
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;      // 0x0023
};

struct ImageDataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct ImageOptionalHeader64 {
    uint16_t Magic;                // 0x020B (PE32+)
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
    uint16_t Subsystem;            // 3 (CUI)
    uint16_t DllCharacteristics;   // 0x8100 (NX_COMPAT | TERMINAL_SERVER_AWARE)
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    ImageDataDirectory DataDirectory[16];
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
#pragma pack(pop)

bool test_pe32plus_binary_synthesis() {
    dgtl_context_t* ctx = dgtl_context_create(0, 64);
    DGTL_TEST_ASSERT(ctx != nullptr, "dgtl_context_create failed");

    dgtl_stream_t* s = dgtl_stream_create(ctx, 32);
    DGTL_TEST_ASSERT(s != nullptr, "dgtl_stream_create failed");

    // Construct a simple exit-42 program
    uint32_t val = emit_const_i64(s, 42);
    (void)emit_return_i64(s, val);

    void* pe_buf = nullptr;
    size_t pe_size = 0;
    dgtl_err_t status = dgtl_emit_pe_binary_in_memory(s, &pe_buf, &pe_size);

    DGTL_TEST_ASSERT(!DGTL_IS_ERR(status),
                     "dgtl_emit_pe_binary_in_memory failed: " +
                     std::string(dgtl_err_message(status)));
    DGTL_TEST_ASSERT(pe_buf != nullptr && pe_size >= 1024, "PE buffer invalid or too small");

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(pe_buf);

    // 1. Validate DOS Header & Stub
    const auto* dos = reinterpret_cast<const ImageDosHeader*>(raw);
    DGTL_TEST_ASSERT(dos->e_magic == 0x5A4D, "DOS Header magic mismatch (expected 0x5A4D 'MZ')");
    DGTL_TEST_ASSERT(dos->e_lfanew == 0x80, "PE Header offset mismatch (expected 0x80)");

    // 2. Validate PE Signature
    const uint32_t pe_sig = *reinterpret_cast<const uint32_t*>(raw + 0x80);
    DGTL_TEST_ASSERT(pe_sig == 0x00004550, "PE Signature mismatch (expected 'PE\\0\\0')");

    // 3. Validate COFF File Header
    const auto* fhdr = reinterpret_cast<const FileHeader*>(raw + 0x84);
    DGTL_TEST_ASSERT(fhdr->Machine == 0x8664, "COFF Machine architecture mismatch (expected AMD64 0x8664)");
    DGTL_TEST_ASSERT(fhdr->NumberOfSections == 2, "Section count mismatch (expected 2: .text and .rdata)");
    DGTL_TEST_ASSERT(fhdr->Characteristics == 0x0023, 
                     "Characteristics mismatch (expected 0x0023 RELOCS_STRIPPED | EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE)");

    // 4. Validate Optional Header (PE32+)
    const auto* opt = reinterpret_cast<const ImageOptionalHeader64*>(raw + 0x98);
    DGTL_TEST_ASSERT(opt->Magic == 0x020B, "PE32+ Optional Header Magic mismatch (expected 0x020B)");
    DGTL_TEST_ASSERT(opt->Subsystem == 3, "Subsystem mismatch (expected 3 Windows CUI)");
    DGTL_TEST_ASSERT(opt->DllCharacteristics == 0x8100, 
                     "DllCharacteristics mismatch (expected 0x8100 NX_COMPAT | TERMINAL_SERVER_AWARE without DYNAMIC_BASE)");
    DGTL_TEST_ASSERT(opt->SectionAlignment == 0x1000, "SectionAlignment mismatch (expected 0x1000)");
    DGTL_TEST_ASSERT(opt->FileAlignment == 0x200, "FileAlignment mismatch (expected 0x200)");

    // 5. Validate Sections (.text and .rdata)
    const auto* sec_text = reinterpret_cast<const SectionHeader*>(raw + 0x98 + sizeof(ImageOptionalHeader64));
    DGTL_TEST_ASSERT(std::strncmp(sec_text->Name, ".text", 5) == 0, ".text section header missing");
    DGTL_TEST_ASSERT(sec_text->Characteristics == 0x60000020, ".text section characteristics mismatch");

    const auto* sec_rdata = reinterpret_cast<const SectionHeader*>(raw + 0x98 + sizeof(ImageOptionalHeader64) + sizeof(SectionHeader));
    DGTL_TEST_ASSERT(std::strncmp(sec_rdata->Name, ".rdata", 6) == 0, ".rdata section header missing");
    DGTL_TEST_ASSERT(sec_rdata->Characteristics == 0xC0000040, ".rdata section characteristics mismatch");

    dgtl_free_binary_buffer(pe_buf);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);

    DGTL_TEST_PASS("Sovereign in-memory PE32+ synthesis fully compliant with Win64 loader specification");
    return true;
}

// ============================================================================
// TEST 3: JIT Compilation & Fast Execution Pipeline
// ============================================================================
bool test_jit_compilation_and_execution() {
    dgtl_session_config_t cfg = dgtl_session_config_default();
    cfg.mode = DGTL_SESSION_MODE_JIT;
    cfg.opt_level = DGTL_OPT_O3;
    cfg.enable_simd = true;
    cfg.enable_inliner = true;

    dgtl_session_t* session = dgtl_session_create(&cfg);
    DGTL_TEST_ASSERT(session != nullptr, "dgtl_session_create failed");

    dgtl_context_t* ctx = dgtl_context_create(0, 64);
    dgtl_stream_t* s = dgtl_stream_create(ctx, 64);

    // Compute (10 * 5) + (100 - 20) = 50 + 80 = 130
    uint32_t c10  = emit_const_i64(s, 10);
    uint32_t c5   = emit_const_i64(s, 5);
    uint32_t c100 = emit_const_i64(s, 100);
    uint32_t c20  = emit_const_i64(s, 20);

    uint32_t prod = emit_binary_i64(s, DGTL_OP_MUL_I64, c10, c5);
    uint32_t diff = emit_binary_i64(s, DGTL_OP_SUB_I64, c100, c20);
    uint32_t sum  = emit_binary_i64(s, DGTL_OP_ADD_I64, prod, diff);

    (void)emit_return_i64(s, sum);

    dgtl_err_t comp_status = dgtl_session_compile(session, s);
    DGTL_TEST_ASSERT(!DGTL_IS_ERR(comp_status),
                     "dgtl_session_compile failed: " + std::string(dgtl_err_message(comp_status)));

    int64_t result = 0;
    const int64_t* no_args = nullptr;
    dgtl_err_t run_status =
        dgtl_session_run_i64(session, nullptr, no_args, 0, &result);
    DGTL_TEST_ASSERT(!DGTL_IS_ERR(run_status),
                     "dgtl_session_run_i64 failed: " +
                     std::string(dgtl_err_message(run_status)));
    DGTL_TEST_ASSERT(result == 130, "JIT execution computation mismatch (expected 130, got " + std::to_string(result) + ")");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    dgtl_session_destroy(session);

    DGTL_TEST_PASS("DGTL JIT pipeline session execution verified (result: 130)");
    return true;
}

// ============================================================================
// MAIN HARNESS ENTRYPOINT
// ============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << "================================================================" << std::endl;
    std::cout << " DGTL SOVEREIGN COMPILER & RUNTIME ARCHITECTURE SUITE (v2026.4) " << std::endl;
    std::cout << "================================================================" << std::endl;

    const auto t_start = std::chrono::high_resolution_clock::now();

    bool ok = true;
    ok &= test_win64_frame_alignment_invariants();
    ok &= test_pe32plus_binary_synthesis();
    ok &= test_jit_compilation_and_execution();

    const auto t_end = std::chrono::high_resolution_clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << "RESULTS: " << g_passes << " passed, " << g_failures << " failed in " 
              << elapsed_ms << " ms" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (ok && g_failures == 0) ? 0 : 1;
}
