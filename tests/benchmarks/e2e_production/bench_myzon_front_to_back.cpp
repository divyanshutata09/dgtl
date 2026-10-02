/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * Project: Myzon Real-Speed Benchmark Harness (Direct Lowering Engine)
 *
 * This benchmark intentionally uses only the authoritative dgtl.h API.
 * It does not depend on the legacy dgtl_session.h / dgtl_core.h headers.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "dgtl_sdk/dgtl.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace {

constexpr std::size_t kElementCount = 2'000'000;
constexpr int kMeasuredRuns = 15;
constexpr int kWarmupRuns = 3;

[[nodiscard]] std::uint64_t hash_float(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    std::uint64_t v = bits;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ull;
    v ^= v >> 33;
    return v;
}

/*
 * DGTL_CALL is part of the public ABI contract. Keep it on the function
 * pointer so Windows x64/ABI configuration cannot silently diverge.
 */
using MyzonRealKernelFn = float (DGTL_CALL *)(float*, std::uint64_t);

static MyzonRealKernelFn g_myzon_kernel = nullptr;
static dgtl_session_t* g_session = nullptr;

void destroy_session() noexcept {
    if (g_session) {
        dgtl_session_destroy(g_session);
        g_session = nullptr;
    }
    g_myzon_kernel = nullptr;
}

[[nodiscard]] bool compile_myzon_real_kernel() {
    dgtl_context_t* ctx = dgtl_context_create(0, 64);
    if (!ctx) {
        return false;
    }

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 16384);
    if (!stream) {
        dgtl_context_destroy(ctx);
        return false;
    }

    /*
     * Build the kernel through the canonical Graph-SSA function-builder API:
     *
     *   fn compute_pipeline(data: ptr, count: i64) -> f32
     *
     *   entry -> loop
     *   loop  -> exit | body
     *   body  -> loop
     *
     * The loop-carried values are represented by PHIs:
     *   x   : f32
     *   i   : i64
     *   ptr : ptr
     */
    dgtl_func_builder_t* fb =
        dgtl_func_builder_create(stream, "DGTL_kernel_entry", DGTL_TYPE_F32);

    if (!fb) {
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    const dgtl_value_t data =
        dgtl_func_add_param(fb, DGTL_TYPE_PTR);
    const dgtl_value_t count =
        dgtl_func_add_param(fb, DGTL_TYPE_I64);

    const dgtl_block_t entry =
        dgtl_func_append_block(fb, "entry");
    const dgtl_block_t loop =
        dgtl_func_append_block(fb, "loop");
    const dgtl_block_t body =
        dgtl_func_append_block(fb, "body");
    const dgtl_block_t exit =
        dgtl_func_append_block(fb, "exit");

    if (!dgtl_block_is_valid(entry) ||
        !dgtl_block_is_valid(loop) ||
        !dgtl_block_is_valid(body) ||
        !dgtl_block_is_valid(exit)) {
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    /* Create loop-carried PHIs in the loop block. */
    dgtl_func_set_insert_block(fb, loop);

    const dgtl_value_t x_phi =
        dgtl_build_phi(fb, DGTL_TYPE_F32);
    const dgtl_value_t i_phi =
        dgtl_build_phi(fb, DGTL_TYPE_I64);
    const dgtl_value_t ptr_phi =
        dgtl_build_phi(fb, DGTL_TYPE_PTR);

    const dgtl_value_t c_one_f32 =
        dgtl_build_const_f32(fb, 1.0f);
    const dgtl_value_t c_mult =
        dgtl_build_const_f32(fb, 1.000001f);
    const dgtl_value_t c_add =
        dgtl_build_const_f32(fb, 0.000001f);
    const dgtl_value_t c_zero_i64 =
        dgtl_build_const_i64(fb, 0);
    const dgtl_value_t c_one_i64 =
        dgtl_build_const_i64(fb, 1);
    const dgtl_value_t c_step_bytes =
        dgtl_build_const_i64(fb, 4);

    /*
     * Entry initializes the three loop-carried values.
     * The back-edge values are supplied by the body below.
     */
    dgtl_func_set_insert_block(fb, entry);

    dgtl_phi_add_incoming(fb, x_phi, c_one_f32, entry);
    dgtl_phi_add_incoming(fb, i_phi, c_zero_i64, entry);
    dgtl_phi_add_incoming(fb, ptr_phi, data, entry);
    dgtl_build_br(fb, loop);

    /* Loop condition: i >= count -> exit, otherwise -> body. */
    dgtl_func_set_insert_block(fb, loop);

    const dgtl_value_t cmp =
        dgtl_build_icmp(fb, DGTL_ICMP_UGE, i_phi, count);
    dgtl_build_cond_br(fb, cmp, exit, body);

    /* Kernel body. */
    dgtl_func_set_insert_block(fb, body);

    /*
     * Use the canonical FMA primitive. The native reference below therefore
     * uses std::fmaf as well, so the correctness comparison is meaningful
     * at the floating-point rounding level.
     */
    const dgtl_value_t next_x =
        dgtl_build_fma(fb, x_phi, c_mult, c_add);

    (void)dgtl_build_store(fb, next_x, ptr_phi);

    const dgtl_value_t next_ptr =
        dgtl_build_gep(fb, ptr_phi, c_step_bytes);

    const dgtl_value_t next_i =
        dgtl_build_add(fb, i_phi, c_one_i64);

    dgtl_phi_add_incoming(fb, x_phi, next_x, body);
    dgtl_phi_add_incoming(fb, i_phi, next_i, body);
    dgtl_phi_add_incoming(fb, ptr_phi, next_ptr, body);

    dgtl_build_br(fb, loop);

    /* Exit returns the final loop-carried x value. */
    dgtl_func_set_insert_block(fb, exit);
    dgtl_build_ret(fb, x_phi);

    /*
     * Finalize + verify before handing the function to the session.
     * This keeps malformed benchmark IR from being confused with a backend
     * or JIT failure.
     */
    const dgtl_err_t finalize_status =
        dgtl_func_builder_finalize(fb);

    char verify_message[256] = {};
    const dgtl_err_t verify_status =
        dgtl_func_builder_verify(fb, verify_message, sizeof(verify_message));

    if (DGTL_IS_ERR(finalize_status) || DGTL_IS_ERR(verify_status)) {
        std::cerr << "[FAIL] DGTL function verification failed";
        if (verify_message[0] != '\0') {
            std::cerr << ": " << verify_message;
        }
        std::cerr << '\n';

        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    dgtl_func_builder_destroy(fb);

    /* Let the authoritative stream/session optimization pipeline run. */
    dgtl_stream_set_auto_optimize(stream, true);
    dgtl_stream_set_opt_level(stream, DGTL_OPT_O3);

    dgtl_session_config_t cfg = dgtl_session_config_default();
    cfg.mode = DGTL_SESSION_MODE_JIT;
    cfg.opt_level = DGTL_OPT_O3;
    cfg.target = DGTL_TARGET_HOST;
    cfg.enable_simd = true;
    cfg.enable_inliner = true;

    g_session = dgtl_session_create(&cfg);
    if (!g_session) {
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    const dgtl_err_t compile_status =
        dgtl_session_compile(g_session, stream);

    if (DGTL_IS_ERR(compile_status)) {
        std::cerr << "[FAIL] DGTL JIT compilation failed: "
                  << dgtl_err_message(compile_status) << '\n';
        destroy_session();
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    /*
     * Deliberately obtain the raw executable entry point. This preserves the
     * original benchmark's "unmasked JIT execution" intent and measures the
     * generated function directly rather than the universal marshaller.
     */
    void* entry_ptr =
        dgtl_session_get_function_ptr(g_session, "DGTL_kernel_entry");

    if (!entry_ptr) {
        std::cerr << "[FAIL] DGTL_kernel_entry was not resolved.\n";
        destroy_session();
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return false;
    }

    g_myzon_kernel =
        reinterpret_cast<MyzonRealKernelFn>(entry_ptr);

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);
    return g_myzon_kernel != nullptr;
}

double run_cxx_reference(
    float* data,
    std::size_t count,
    std::uint64_t& out_checksum) {

    const auto t0 = std::chrono::steady_clock::now();

    float x = 1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        x = std::fmaf(x, 1.000001f, 0.000001f);
        data[i] = x;
    }

    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t1 - t0).count());

    std::uint64_t cs = 0;
    for (std::size_t i = 0; i < count; ++i) {
        cs ^= hash_float(
            data[i] + static_cast<float>(i & 255u));
    }

    out_checksum = cs;
    return elapsed_ns;
}

double run_myzon_actual(
    float* data,
    std::size_t count,
    std::uint64_t& out_checksum) {

    const auto t0 = std::chrono::steady_clock::now();

    const float final_val =
        g_myzon_kernel(data, static_cast<std::uint64_t>(count));
    (void)final_val;

    const auto t1 = std::chrono::steady_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t1 - t0).count());

    std::uint64_t cs = 0;
    for (std::size_t i = 0; i < count; ++i) {
        cs ^= hash_float(
            data[i] + static_cast<float>(i & 255u));
    }

    out_checksum = cs;
    return elapsed_ns;
}

} // namespace

int main() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), 0x4); // Pin to Core 2.
#endif

    std::cout
        << "=========================================================\n"
        << " MYZON AUTHENTIC SPEED EVALUATION (UNMASKED JIT EXECUTION)\n"
        << "=========================================================\n\n";

    if (!compile_myzon_real_kernel()) {
        std::cerr
            << "[FAIL] Could not compile Myzon kernel to executable memory.\n";
        return 1;
    }

    std::vector<float> buffer_cxx(kElementCount, 0.0f);
    std::vector<float> buffer_myzon(kElementCount, 0.0f);

    std::uint64_t dummy_cs = 0;

    for (int i = 0; i < kWarmupRuns; ++i) {
        run_cxx_reference(
            buffer_cxx.data(), kElementCount, dummy_cs);
        run_myzon_actual(
            buffer_myzon.data(), kElementCount, dummy_cs);
    }

    std::vector<double> cxx_times;
    std::vector<double> myzon_times;
    cxx_times.reserve(kMeasuredRuns);
    myzon_times.reserve(kMeasuredRuns);

    std::uint64_t cs_cxx = 0;
    std::uint64_t cs_myz = 0;

    for (int i = 0; i < kMeasuredRuns; ++i) {
        std::memset(
            buffer_cxx.data(), 0,
            kElementCount * sizeof(float));
        std::memset(
            buffer_myzon.data(), 0,
            kElementCount * sizeof(float));

        cxx_times.push_back(
            run_cxx_reference(
                buffer_cxx.data(), kElementCount, cs_cxx));

        myzon_times.push_back(
            run_myzon_actual(
                buffer_myzon.data(), kElementCount, cs_myz));
    }

    std::sort(cxx_times.begin(), cxx_times.end());
    std::sort(myzon_times.begin(), myzon_times.end());

    const double median_cxx_ns =
        cxx_times[kMeasuredRuns / 2];
    const double median_myzon_ns =
        myzon_times[kMeasuredRuns / 2];

    const double cxx_throughput =
        (static_cast<double>(kElementCount) * 1e9) /
        std::max(median_cxx_ns, 1.0);

    const double myzon_throughput =
        (static_cast<double>(kElementCount) * 1e9) /
        std::max(median_myzon_ns, 1.0);

    const double cer =
        (myzon_throughput / cxx_throughput) * 100.0;

    const bool match =
        (cs_cxx == cs_myz) && (cs_cxx != 0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout
        << "Raw Execution Results (Median of "
        << kMeasuredRuns << " runs):\n"
        << "  Native C++ (-O3):      "
        << (median_cxx_ns / 1e6)
        << " ms (" << cxx_throughput << " elements/sec)\n"
        << "  Myzon JIT (DGTL IR):   "
        << (median_myzon_ns / 1e6)
        << " ms (" << myzon_throughput << " elements/sec)\n"
        << "---------------------------------------------------------\n"
        << "  Compiler Efficiency Ratio (CER): "
        << cer << "%\n"
        << "  Native Checksum: " << cs_cxx << "\n"
        << "  Myzon Checksum:  " << cs_myz << "\n"
        << "  Data Parity:     "
        << (match ? "EXACT MATCH (100% Correct)" : "MISMATCH")
        << "\n"
        << "=========================================================\n";

    destroy_session();
    return match ? 0 : 2;
}
