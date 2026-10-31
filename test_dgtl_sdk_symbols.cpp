/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Industry-Grade Complete SDK Symbol & Functionality Verification Suite
 *
 * Location:    D:\TataG\DGTL\test_dgtl_sdk_symbols.cpp
 * Standards:   C++20 / C17 ABI Compatible
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <chrono>
#include <cassert>

// Canonical DGTL SDK Master Header
#include "include/dgtl_sdk/dgtl.h"

namespace test_harness {

static uint32_t g_tests_run = 0;
static uint32_t g_tests_passed = 0;
static uint32_t g_tests_failed = 0;

#define DGTL_TEST_ASSERT(expr, msg)                                            \
    do {                                                                       \
        ++test_harness::g_tests_run;                                           \
        if (expr) {                                                            \
            ++test_harness::g_tests_passed;                                    \
        } else {                                                               \
            ++test_harness::g_tests_failed;                                    \
            std::cerr << "\033[1;31m[FAILED]\033[0m " << msg                   \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
        }                                                                      \
    } while (0)

#define DGTL_TEST_ASSERT_NEAR(actual, expected, tol, msg)                      \
    do {                                                                       \
        ++test_harness::g_tests_run;                                           \
        double _diff = std::fabs(static_cast<double>(actual) - static_cast<double>(expected)); \
        if (_diff <= static_cast<double>(tol)) {                               \
            ++test_harness::g_tests_passed;                                    \
        } else {                                                               \
            ++test_harness::g_tests_failed;                                    \
            std::cerr << "\033[1;31m[FAILED]\033[0m " << msg                   \
                      << " | Expected: " << (expected) << ", Got: " << (actual)\
                      << " (Diff: " << _diff << " > " << (tol) << ")\n";       \
        }                                                                      \
    } while (0)

inline void print_suite_header(const char* name) {
    std::cout << "\n\033[1;36m====================================================================\033[0m\n";
    std::cout << "\033[1;33m>>> SUITE: " << name << "\033[0m\n";
    std::cout << "\033[1;36m====================================================================\033[0m\n";
}

} // namespace test_harness

// ============================================================================
// SUITE 1: ABI Versioning, Opcodes & Error Handling
// ============================================================================
void test_suite_version_and_opcodes() {
    test_harness::print_suite_header("Suite 1: ABI Versioning, Opcodes & Diagnostic APIs");

    // Versioning
    const char* ver = dgtl_version_string();
    DGTL_TEST_ASSERT(ver != nullptr && std::strlen(ver) > 0, "dgtl_version_string returns valid string");
    DGTL_TEST_ASSERT(DGTL_VERSION_MAJOR >= 2, "Major version >= 2");
    DGTL_TEST_ASSERT(DGTL_ABI_VERSION >= 4, "ABI version >= 4");

    // Error APIs
    dgtl_clear_error();
    const char* last_err = dgtl_get_last_error();
    DGTL_TEST_ASSERT(last_err != nullptr, "dgtl_get_last_error returns non-null sentinel");

    const char* err_ok = dgtl_err_message(DGTL_OK);
    DGTL_TEST_ASSERT(err_ok != nullptr && std::strlen(err_ok) > 0, "DGTL_OK error message is defined");

    // Verify recent Opcode definitions are present and distinct
    DGTL_TEST_ASSERT(DGTL_OP_TENSOR_RELU == 0x8030u, "DGTL_OP_TENSOR_RELU is 0x8030");
    DGTL_TEST_ASSERT(DGTL_OP_TENSOR_BATCHNORM == 0x8031u, "DGTL_OP_TENSOR_BATCHNORM is 0x8031");
    DGTL_TEST_ASSERT(DGTL_OP_TENSOR_GATHER == 0x8032u, "DGTL_OP_TENSOR_GATHER is 0x8032");
    DGTL_TEST_ASSERT(DGTL_OP_TENSOR_SCATTER_D == 0x8033u, "DGTL_OP_TENSOR_SCATTER_D is 0x8033");
    DGTL_TEST_ASSERT(DGTL_OP_GPU_ANNOTATE_DEVICE == 0x8024u, "DGTL_OP_GPU_ANNOTATE_DEVICE is 0x8024");
    DGTL_TEST_ASSERT(DGTL_OP_DGTL_FAST_ADD == 0x8001u, "DGTL_OP_DGTL_FAST_ADD is 0x8001");
    DGTL_TEST_ASSERT(DGTL_OP_DGTL_FAST_FMA == 0x8002u, "DGTL_OP_DGTL_FAST_FMA is 0x8002");
    DGTL_TEST_ASSERT(DGTL_OP_CPP_TEMPLATE_SPEC == 0x8101u, "DGTL_OP_CPP_TEMPLATE_SPEC is 0x8101");
    DGTL_TEST_ASSERT(DGTL_OP_RUST_BORROW_MUT == 0x8102u, "DGTL_OP_RUST_BORROW_MUT is 0x8102");
    DGTL_TEST_ASSERT(DGTL_OP_WASM_INVOKE == 0x0410u, "DGTL_OP_WASM_INVOKE is 0x0410");
    DGTL_TEST_ASSERT(DGTL_OP_FIBER_YIELD_LOOP == 0x0411u, "DGTL_OP_FIBER_YIELD_LOOP is 0x0411");
    DGTL_TEST_ASSERT(DGTL_OP_SUSPEND_CTX == 0x0412u, "DGTL_OP_SUSPEND_CTX is 0x0412");
    DGTL_TEST_ASSERT(DGTL_OP_RESUME_CTX == 0x0413u, "DGTL_OP_RESUME_CTX is 0x0413");
    DGTL_TEST_ASSERT(DGTL_OP_NEON_VADD_Q == 0x0414u, "DGTL_OP_NEON_VADD_Q is 0x0414");

    // Verify Compilation Targets
    DGTL_TEST_ASSERT(DGTL_TARGET_SPIRV == 9, "DGTL_TARGET_SPIRV == 9");
    DGTL_TEST_ASSERT(DGTL_TARGET_PTX == 10, "DGTL_TARGET_PTX == 10");
    DGTL_TEST_ASSERT(DGTL_TARGET_AMDGPU == 11, "DGTL_TARGET_AMDGPU == 11");
    DGTL_TEST_ASSERT(DGTL_ARCH_PTX == 9, "DGTL_ARCH_PTX == 9");
    DGTL_TEST_ASSERT(DGTL_ARCH_AMDGPU == 10, "DGTL_ARCH_AMDGPU == 10");
}

// ============================================================================
// SUITE 2: Sovereign Mathematical & GenAI Runtime Intrinsics
// ============================================================================
void test_suite_math_intrinsics() {
    test_harness::print_suite_header("Suite 2: Sovereign Math & GenAI Intrinsics Correctness");

    // 1. ReLU
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_relu(5.5), 5.5, 1e-9, "ReLU positive double: relu(5.5) == 5.5");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_relu(-3.7), 0.0, 1e-9, "ReLU negative double: relu(-3.7) == 0.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_relu(0.0), 0.0, 1e-9, "ReLU zero double: relu(0.0) == 0.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_reluf(4.2f), 4.2f, 1e-6f, "ReLU positive float: reluf(4.2) == 4.2");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_reluf(-8.9f), 0.0f, 1e-6f, "ReLU negative float: reluf(-8.9) == 0.0");

    // 2. Trigonometry
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_sin(0.0), 0.0, 1e-9, "sin(0.0) == 0.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_cos(0.0), 1.0, 1e-9, "cos(0.0) == 1.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_sinf(0.0f), 0.0f, 1e-6f, "sinf(0.0) == 0.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_cosf(0.0f), 1.0f, 1e-6f, "cosf(0.0) == 1.0");

    // 3. Exponentials & Logarithms
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_exp(0.0), 1.0, 1e-9, "exp(0.0) == 1.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_log(1.0), 0.0, 1e-9, "log(1.0) == 0.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_sqrt(16.0), 4.0, 1e-9, "sqrt(16.0) == 4.0");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_sqrtf(25.0f), 5.0f, 1e-6f, "sqrtf(25.0) == 5.0");

    // 4. FMA (Fused Multiply-Add)
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_fma_f32(2.0f, 3.0f, 4.0f), 10.0f, 1e-6f, "fma_f32(2, 3, 4) == 10");
    DGTL_TEST_ASSERT_NEAR(__dgtl_rt_fma_f64(3.0, 4.0, 5.0), 17.0, 1e-9, "fma_f64(3, 4, 5) == 17");

    // 5. GenAI Activations: GELU, SiLU, Sigmoid
    double gelu_0 = __dgtl_rt_gelu(0.0);
    DGTL_TEST_ASSERT_NEAR(gelu_0, 0.0, 1e-6, "gelu(0.0) == 0.0");
    float geluf_0 = __dgtl_rt_geluf(0.0f);
    DGTL_TEST_ASSERT_NEAR(geluf_0, 0.0f, 1e-5f, "geluf(0.0) == 0.0");

    double silu_0 = __dgtl_rt_silu(0.0);
    DGTL_TEST_ASSERT_NEAR(silu_0, 0.0, 1e-6, "silu(0.0) == 0.0");

    double sig_0 = __dgtl_rt_sigmoid(0.0);
    DGTL_TEST_ASSERT_NEAR(sig_0, 0.5, 1e-6, "sigmoid(0.0) == 0.5");
}

// ============================================================================
// SUITE 3: Context, IR Stream & C-API Convenience Emitters
// ============================================================================
void test_suite_context_and_stream() {
    test_harness::print_suite_header("Suite 3: Context & IR Stream C-API Verification");

    dgtl_context_t* ctx = dgtl_context_create(2, 64);
    DGTL_TEST_ASSERT(ctx != nullptr, "dgtl_context_create returns valid context");

    dgtl_stream_t* s = dgtl_stream_create(ctx, 128);
    DGTL_TEST_ASSERT(s != nullptr, "dgtl_stream_create returns valid stream");
    DGTL_TEST_ASSERT(dgtl_stream_size(s) == 0, "Initial stream size == 0");

    // Convenience emitters
    uint32_t c1 = dgtl_emit_const_i64(s, 100);
    DGTL_TEST_ASSERT(c1 != DGTL_INVALID_SLOT, "dgtl_emit_const_i64 emits valid slot");

    uint32_t c2 = dgtl_emit_const_i64(s, 200);
    DGTL_TEST_ASSERT(c2 != DGTL_INVALID_SLOT, "dgtl_emit_const_i64 emits second slot");

    uint32_t add_slot = dgtl_emit_add_i64(s, c1, c2);
    DGTL_TEST_ASSERT(add_slot != DGTL_INVALID_SLOT, "dgtl_emit_add_i64 emits valid slot");

    uint32_t relu_slot = dgtl_emit_relu(s, add_slot);
    DGTL_TEST_ASSERT(relu_slot != DGTL_INVALID_SLOT, "dgtl_emit_relu emits valid slot");

    uint32_t ret_slot = dgtl_emit_ret(s, relu_slot);
    DGTL_TEST_ASSERT(ret_slot != DGTL_INVALID_SLOT, "dgtl_emit_ret emits valid slot");

    DGTL_TEST_ASSERT(dgtl_stream_size(s) == 5, "Stream has 5 nodes recorded");

    // Text dump
    dgtl_text_t ir_txt = dgtl_stream_print_ir(s);
    DGTL_TEST_ASSERT(ir_txt.data != nullptr && ir_txt.length > 0, "dgtl_stream_print_ir produces text");
    dgtl_free_text(ir_txt);

    // Stream reset
    dgtl_stream_reset(s);
    DGTL_TEST_ASSERT(dgtl_stream_size(s) == 0, "Stream reset clears all nodes");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
}

// ============================================================================
// SUITE 4: FuncBuilder High-Level SSA Emitter & GenAI Nodes
// ============================================================================
void test_suite_func_builder() {
    test_harness::print_suite_header("Suite 4: FuncBuilder High-Level SSA Emitter Verification");

    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    dgtl_stream_t* s = dgtl_stream_create(ctx, 128);

    dgtl_func_builder_t* fb = dgtl_func_builder_create(s, "kernel_relu_add", DGTL_TYPE_F32);
    DGTL_TEST_ASSERT(fb != nullptr, "dgtl_func_builder_create succeeds");

    dgtl_value_t p0 = dgtl_func_add_param(fb, DGTL_TYPE_F32);
    dgtl_value_t p1 = dgtl_func_add_param(fb, DGTL_TYPE_F32);
    DGTL_TEST_ASSERT(dgtl_value_is_valid(p0), "Param 0 is valid");
    DGTL_TEST_ASSERT(dgtl_value_is_valid(p1), "Param 1 is valid");

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    DGTL_TEST_ASSERT(dgtl_block_is_valid(entry), "Entry block is valid");
    dgtl_func_set_insert_block(fb, entry);

    // Build: relu(add(p0, p1))
    dgtl_value_t sum = dgtl_build_add(fb, p0, p1);
    DGTL_TEST_ASSERT(dgtl_value_is_valid(sum), "dgtl_build_add returns valid value");

    dgtl_value_t relu_val = dgtl_build_relu(fb, sum);
    DGTL_TEST_ASSERT(dgtl_value_is_valid(relu_val), "dgtl_build_relu returns valid value");

    dgtl_build_ret(fb, relu_val);

    dgtl_err_t finalize_err = dgtl_func_builder_finalize(fb);
    DGTL_TEST_ASSERT(finalize_err == DGTL_OK, "dgtl_func_builder_finalize returns DGTL_OK");

    DGTL_TEST_ASSERT(dgtl_stream_size(s) > 0, "Stream contains finalized function body");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
}

// ============================================================================
// SUITE 5: Direct-in-Memory Binary Synthesis (PE, ELF, Mach-O, WASM)
// ============================================================================
void test_suite_binary_synthesis() {
    test_harness::print_suite_header("Suite 5: Direct In-Memory Binary Synthesis Verification");

    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    dgtl_stream_t* s = dgtl_stream_create(ctx, 64);

    // Populate minimal kernel returning constant 42
    uint32_t val = dgtl_emit_const_i64(s, 42);
    dgtl_emit_ret(s, val);

    // 1. WASM in-memory synthesis
    void* wasm_buf = nullptr;
    size_t wasm_size = 0;
    dgtl_err_t wasm_err = dgtl_emit_wasm_binary_in_memory(s, &wasm_buf, &wasm_size);
    DGTL_TEST_ASSERT(wasm_err == DGTL_OK, "dgtl_emit_wasm_binary_in_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(wasm_buf != nullptr && wasm_size >= 4, "WASM buffer synthesized (> 4 bytes)");
    if (wasm_buf && wasm_size >= 4) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(wasm_buf);
        DGTL_TEST_ASSERT(b[0] == 0x00 && b[1] == 0x61 && b[2] == 0x73 && b[3] == 0x6D, "WASM binary has valid \\0asm magic");
    }
    dgtl_free_binary_buffer(wasm_buf);

    // 2. Object binary in-memory synthesis
    void* obj_buf = nullptr;
    size_t obj_size = 0;
    dgtl_err_t obj_err = dgtl_emit_object_binary_in_memory(s, DGTL_TARGET_X86_64, &obj_buf, &obj_size);
    DGTL_TEST_ASSERT(obj_err == DGTL_OK, "dgtl_emit_object_binary_in_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(obj_buf != nullptr && obj_size > 0, "Object binary synthesized");
    dgtl_free_binary_buffer(obj_buf);

    // 3. PE in-memory synthesis
    void* pe_buf = nullptr;
    size_t pe_size = 0;
    dgtl_err_t pe_err = dgtl_emit_pe_binary_in_memory(s, &pe_buf, &pe_size);
    DGTL_TEST_ASSERT(pe_err == DGTL_OK, "dgtl_emit_pe_binary_in_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(pe_buf != nullptr && pe_size > 0, "PE binary synthesized");
    dgtl_free_binary_buffer(pe_buf);

    // 4. ELF in-memory synthesis
    void* elf_buf = nullptr;
    size_t elf_size = 0;
    dgtl_err_t elf_err = dgtl_emit_elf_binary_in_memory(s, &elf_buf, &elf_size);
    DGTL_TEST_ASSERT(elf_err == DGTL_OK, "dgtl_emit_elf_binary_in_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(elf_buf != nullptr && elf_size > 0, "ELF binary synthesized");
    dgtl_free_binary_buffer(elf_buf);

    // 5. Mach-O in-memory synthesis
    void* macho_buf = nullptr;
    size_t macho_size = 0;
    dgtl_err_t macho_err = dgtl_emit_macho_binary_in_memory(s, &macho_buf, &macho_size);
    DGTL_TEST_ASSERT(macho_err == DGTL_OK, "dgtl_emit_macho_binary_in_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(macho_buf != nullptr && macho_size > 0, "Mach-O binary synthesized");
    dgtl_free_binary_buffer(macho_buf);

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
}

// ============================================================================
// SUITE 6: Object & Sovereign In-Memory Linker Verification
// ============================================================================
void test_suite_object_and_linker() {
    test_harness::print_suite_header("Suite 6: Sovereign In-Memory Linker & Object Subsystems");

    // 1. Universal Object File creation
    dgtl_object_t obj = dgtl_object_create(DGTL_OBJECT_FMT_PE);
    DGTL_TEST_ASSERT(obj != nullptr, "dgtl_object_create succeeds");
    DGTL_TEST_ASSERT(dgtl_object_get_format(obj) == DGTL_OBJECT_FMT_PE, "Object format is PE");

    dgtl_section_t text_sec = dgtl_object_add_section(obj, ".text", 16, DGTL_SEC_FLAG_CODE | DGTL_SEC_FLAG_READ | DGTL_SEC_FLAG_EXECUTE);
    DGTL_TEST_ASSERT(text_sec != nullptr, "dgtl_object_add_section .text succeeds");

    const uint8_t code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; // mov eax, 42; ret
    dgtl_err_t append_err = dgtl_section_append_data(text_sec, code, sizeof(code));
    DGTL_TEST_ASSERT(append_err == DGTL_OK, "dgtl_section_append_data succeeds");

    dgtl_symbol_t sym = dgtl_object_add_symbol(obj, "main", 0, sizeof(code), 0, DGTL_SYM_BIND_GLOBAL, DGTL_SYM_TYPE_FUNC);
    DGTL_TEST_ASSERT(sym != nullptr, "dgtl_object_add_symbol main succeeds");

    DGTL_TEST_ASSERT(dgtl_object_get_symbol_count(obj) == 1, "Symbol count == 1");
    DGTL_TEST_ASSERT(dgtl_object_get_section_count(obj) == 1, "Section count == 1");

    // 2. In-memory object emission
    const uint8_t* obj_bytes = nullptr;
    size_t obj_size = 0;
    dgtl_err_t obj_mem_err = dgtl_object_emit_to_memory(obj, &obj_bytes, &obj_size);
    DGTL_TEST_ASSERT(obj_mem_err == DGTL_OK, "dgtl_object_emit_to_memory succeeds");
    DGTL_TEST_ASSERT(obj_bytes != nullptr && obj_size > 0, "Object emitted to memory");

    // 3. Linker Driver
    dgtl_linker_t linker = dgtl_linker_create(DGTL_OBJECT_FMT_PE);
    DGTL_TEST_ASSERT(linker != nullptr, "dgtl_linker_create succeeds");

    dgtl_err_t ep_err = dgtl_linker_set_entry_point(linker, "main");
    DGTL_TEST_ASSERT(ep_err == DGTL_OK, "dgtl_linker_set_entry_point succeeds");

    dgtl_err_t add_err = dgtl_linker_add_object(linker, obj);
    DGTL_TEST_ASSERT(add_err == DGTL_OK, "dgtl_linker_add_object succeeds");

    const uint8_t* linked_bytes = nullptr;
    size_t linked_size = 0;
    dgtl_err_t link_err = dgtl_linker_link_to_memory(linker, DGTL_LINK_EXECUTABLE, &linked_bytes, &linked_size);
    DGTL_TEST_ASSERT(link_err == DGTL_OK, "dgtl_linker_link_to_memory returns DGTL_OK");
    DGTL_TEST_ASSERT(linked_bytes != nullptr && linked_size > 0, "Linked executable synthesized in memory");

    // 4. Static Archive Linking (PE & Mach-O)
    const uint8_t* lib_bytes = nullptr;
    size_t lib_size = 0;
    dgtl_err_t lib_err = dgtl_linker_link_to_memory(linker, DGTL_LINK_STATIC_LIB, &lib_bytes, &lib_size);
    DGTL_TEST_ASSERT(lib_err == DGTL_OK, "dgtl_linker_link_to_memory (DGTL_LINK_STATIC_LIB) returns DGTL_OK");
    DGTL_TEST_ASSERT(lib_bytes != nullptr && lib_size >= 8, "Static library archive synthesized in memory");
    if (lib_bytes && lib_size >= 8) {
        DGTL_TEST_ASSERT(std::memcmp(lib_bytes, "!<arch>\n", 8) == 0, "Static library has valid !<arch>\\n magic header");
    }

    dgtl_linker_t macho_linker = dgtl_linker_create(DGTL_OBJECT_FMT_MACHO);
    DGTL_TEST_ASSERT(macho_linker != nullptr, "dgtl_linker_create(DGTL_OBJECT_FMT_MACHO) succeeds");
    DGTL_TEST_ASSERT(dgtl_linker_add_object(macho_linker, obj) == DGTL_OK, "Add object to Mach-O linker succeeds");
    const uint8_t* macho_lib_bytes = nullptr;
    size_t macho_lib_size = 0;
    dgtl_err_t macho_lib_err = dgtl_linker_link_to_memory(macho_linker, DGTL_LINK_STATIC_LIB, &macho_lib_bytes, &macho_lib_size);
    DGTL_TEST_ASSERT(macho_lib_err == DGTL_OK, "Mach-O static archive link returns DGTL_OK");
    DGTL_TEST_ASSERT(macho_lib_bytes != nullptr && macho_lib_size >= 8, "Mach-O static archive synthesized");
    if (macho_lib_bytes && macho_lib_size >= 8) {
        DGTL_TEST_ASSERT(std::memcmp(macho_lib_bytes, "!<arch>\n", 8) == 0, "Mach-O static archive has valid !<arch>\\n magic header");
    }
    dgtl_linker_destroy(macho_linker);

    dgtl_linker_destroy(linker);
    dgtl_object_destroy(obj);
}

// ============================================================================
// SUITE 7: Target Machine & Sovereign JIT Engine
// ============================================================================
void test_suite_target_and_jit() {
    test_harness::print_suite_header("Suite 7: Target Machine & JIT Engine Verification");

    dgtl_target_machine_t tm = dgtl_target_machine_create_host();
    DGTL_TEST_ASSERT(tm != nullptr, "dgtl_target_machine_create_host succeeds");

    const char* triple = dgtl_target_machine_get_triple(tm);
    DGTL_TEST_ASSERT(triple != nullptr && std::strlen(triple) > 0, "Host triple is non-empty");

    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    dgtl_jit_engine_t jit = dgtl_jit_engine_create(ctx, tm);
    DGTL_TEST_ASSERT(jit != nullptr, "dgtl_jit_engine_create succeeds");

    // Register test symbol
    uintptr_t test_addr = reinterpret_cast<uintptr_t>(&test_suite_version_and_opcodes);
    dgtl_jit_engine_register_symbol(jit, "custom_symbol", test_addr);

    uintptr_t resolved = dgtl_jit_engine_lookup(jit, "custom_symbol");
    DGTL_TEST_ASSERT(resolved == test_addr, "dgtl_jit_engine_lookup correctly resolves registered symbol");

    dgtl_jit_engine_destroy(jit);
    dgtl_context_destroy(ctx);
    dgtl_target_machine_destroy(tm);
}

// ============================================================================
// SUITE 8: C++20 Zero-Overhead RAII Facade Verification
// ============================================================================
void test_suite_cpp_raii_facade() {
    test_harness::print_suite_header("Suite 8: C++20 Ergonomic RAII Facade Parity");

    dgtl::Context ctx(2, 64);
    DGTL_TEST_ASSERT(ctx.get() != nullptr, "C++ Context RAII wrapper is valid");

    dgtl::Stream stream(ctx, 128);
    DGTL_TEST_ASSERT(stream.get() != nullptr, "C++ Stream RAII wrapper is valid");

    {
        dgtl::FuncBuilder fb(stream, "cpp_raii_kernel", DGTL_TYPE_F32);
        dgtl::Value p0 = fb.param(DGTL_TYPE_F32);
        dgtl::Block b0 = fb.append_block("entry");
        fb.set_insert_block(b0);

        dgtl::Value y = fb.relu(p0);
        DGTL_TEST_ASSERT(y.valid(), "fb.relu returns valid Value");

        dgtl::Value z = fb.gelu(y);
        DGTL_TEST_ASSERT(z.valid(), "fb.gelu returns valid Value");

        fb.ret(z);
    }

    DGTL_TEST_ASSERT(stream.size() > 0, "Stream has recorded nodes from C++ FuncBuilder");

    // Test C++ Stream in-memory emission methods
    void* wasm_buf = nullptr;
    size_t wasm_sz = 0;
    dgtl_err_t wasm_err = stream.emit_wasm(&wasm_buf, &wasm_sz);
    DGTL_TEST_ASSERT(wasm_err == DGTL_OK, "Stream::emit_wasm returns DGTL_OK");
    DGTL_TEST_ASSERT(wasm_buf != nullptr && wasm_sz > 0, "Stream::emit_wasm produced buffer");
    dgtl_free_binary_buffer(wasm_buf);

    void* obj_buf = nullptr;
    size_t obj_sz = 0;
    dgtl_err_t obj_err = stream.emit_object(DGTL_TARGET_X86_64, &obj_buf, &obj_sz);
    DGTL_TEST_ASSERT(obj_err == DGTL_OK, "Stream::emit_object returns DGTL_OK");
    DGTL_TEST_ASSERT(obj_buf != nullptr && obj_sz > 0, "Stream::emit_object produced buffer");
    dgtl_free_binary_buffer(obj_buf);

    // C++ ObjectFile and Linker
    dgtl::ObjectFile obj(DGTL_OBJECT_FMT_PE);
    DGTL_TEST_ASSERT(obj.is_valid(), "C++ ObjectFile is valid");

    dgtl::Linker linker(DGTL_OBJECT_FMT_PE);
    DGTL_TEST_ASSERT(linker.is_valid(), "C++ Linker is valid");

    // C++ TargetMachine
    dgtl::TargetMachine tm;
    DGTL_TEST_ASSERT(tm.is_valid(), "C++ TargetMachine is valid");

    // C++ JITEngine
    dgtl::JITEngine jit(ctx, tm);
    DGTL_TEST_ASSERT(jit.is_valid(), "C++ JITEngine is valid");
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main() {
    std::cout << "\n\033[1;32m====================================================================\033[0m\n";
    std::cout << "\033[1;32m      DGTL SOVEREIGN SDK SYMBOL & FUNCTION CONFORMANCE TEST SUITE     \033[0m\n";
    std::cout << "\033[1;32m====================================================================\033[0m\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    test_suite_version_and_opcodes();
    test_suite_math_intrinsics();
    test_suite_context_and_stream();
    test_suite_func_builder();
    test_suite_binary_synthesis();
    test_suite_object_and_linker();
    test_suite_target_and_jit();
    test_suite_cpp_raii_facade();

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "\n\033[1;36m====================================================================\033[0m\n";
    std::cout << "\033[1;33mTEST EXECUTION SUMMARY:\033[0m\n";
    std::cout << "  - Total Checks:  " << test_harness::g_tests_run << "\n";
    std::cout << "  - \033[1;32mPassed:\033[0m        " << test_harness::g_tests_passed << "\n";
    std::cout << "  - \033[1;31mFailed:\033[0m        " << test_harness::g_tests_failed << "\n";
    std::cout << "  - Execution Time: " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
    std::cout << "\033[1;36m====================================================================\033[0m\n";

    if (test_harness::g_tests_failed == 0) {
        std::cout << "\033[1;32m>>> ALL DGTL SDK SYMBOLS & FUNCTIONS ARE FULLY FUNCTIONAL! <<<\033[0m\n\n";
        return 0;
    } else {
        std::cout << "\033[1;31m>>> VERIFICATION FAILED WITH " << test_harness::g_tests_failed << " ERRORS! <<<\033[0m\n\n";
        return 1;
    }
}
