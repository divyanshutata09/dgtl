/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * Project: Myzon Real-Speed Benchmark Harness (Direct Lowering Engine)
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_session.h"
#include "dgtl_sdk/dgtl_core.h"
#include "dgtl_sdk/"

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
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    uint64_t v = bits;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ull;
    v ^= v >> 33;
    return v;
}

// ABI matching: fn compute_pipeline(data: ^f32, count: u64) -> f32
typedef float (*MyzonRealKernelFn)(float*, uint64_t);

static MyzonRealKernelFn g_myzon_kernel = nullptr;
static dgtl_session_t*   g_session = nullptr;

bool compile_myzon_real_kernel() {
    dgtl_context_t* ctx = dgtl_context_create();
    if (!ctx) return false;

    dgtl_stream_t* s = dgtl_stream_create(ctx, 16384);
    if (!s) {
        dgtl_context_destroy(ctx);
        return false;
    }

    // Function parameter slots: Slot 1 = data pointer (RCX), Slot 2 = count (RDX)
    uint32_t param_slots[2] = {1, 2};
    dgtl_emit_function_label_with_params(s, 100, param_slots, 2);
    dgtl_stream_bind_callee_slot(s, 100, "DGTL_kernel_entry");
    dgtl_register_string_handle(100, "DGTL_kernel_entry", 17);

    // Dynamic Constants
    uint32_t c_mult  = dgtl_emit_const_f32(s, 1.000001f);
    uint32_t c_add   = dgtl_emit_const_f32(s, 0.000001f);
    uint32_t c_one   = dgtl_emit_const_i64(s, 1);
    uint32_t c_step4 = dgtl_emit_const_i64(s, 4);

    // Initial State
    // Slot 10: running float value x = 1.0f
    uint32_t c_init_f32 = dgtl_emit_const_f32(s, 1.0f);
    uint32_t c_zero_i64 = dgtl_emit_const_i64(s, 0);

    uint32_t c_fzero = dgtl_emit_const_f32(s, 0.0f);
    dgtl_emit_op_with_result(s, 10, DGTL_OP_FADD, c_init_f32, c_fzero, 0, DGTL_FLAG_NONE);

    // Slot 11: loop counter i = 0
    dgtl_emit_op_with_result(s, 11, DGTL_OP_ADD_I64, c_zero_i64, c_zero_i64, 0, DGTL_FLAG_NONE);

    // Slot 12: running pointer ptr = data
    dgtl_emit_op_with_result(s, 12, DGTL_OP_ADD_I64, 1, c_zero_i64, 0, DGTL_FLAG_NONE);

    // Loop Labels
    uint32_t label_loop = 200;
    uint32_t label_exit = 201;

    dgtl_emit_label_slot(s, label_loop);

    // Exit condition: if (i >= count) goto exit
    uint32_t cmp = dgtl_emit_op(s, DGTL_OP_CMP_GE, 11, 2, 0, DGTL_FLAG_NONE);
    dgtl_emit_branch_cond(s, cmp, label_exit);

    // Arithmetic: x = x * 1.000001f + 0.000001f
    uint32_t next_x = dgtl_emit_fma(s, 10, c_mult, c_add);
    dgtl_emit_op_with_result(s, 10, DGTL_OP_FSUB, next_x, c_fzero, 0, DGTL_FLAG_NONE);

    // Store: *ptr = x
    dgtl_emit_op(s, DGTL_OP_STORE, 12, 10, 0, DGTL_FLAG_IS_SPEC_CONST);

    // Increment Pointer: ptr += 4
    uint32_t next_ptr = dgtl_emit_add_i64(s, 12, c_step4);
    dgtl_emit_op_with_result(s, 12, DGTL_OP_ADD_I64, next_ptr, c_zero_i64, 0, DGTL_FLAG_NONE);

    // Increment Counter: i += 1
    uint32_t next_i = dgtl_emit_add_i64(s, 11, c_one);
    dgtl_emit_op_with_result(s, 11, DGTL_OP_ADD_I64, next_i, c_zero_i64, 0, DGTL_FLAG_NONE);

    dgtl_emit_branch(s, label_loop);

    dgtl_emit_label_slot(s, label_exit);
    dgtl_emit_return(s, 10);

    dgtl_session_config_t cfg = dgtl_session_config_default();
    cfg.mode = DGTL_SESSION_MODE_JIT;
    cfg.opt_level = DGTL_SESSION_OPT_O3;
    cfg.enable_vectorization = 1;

    g_session = dgtl_session_create(&cfg);
    if (!g_session) {
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        return false;
    }

    dgtl_status_t st = dgtl_session_compile_stream(g_session, s);
    if (st.ok) {
        uint64_t addr = dgtl_session_get_symbol_address(g_session, "DGTL_kernel_entry");
        if (addr != 0 && addr != static_cast<uint64_t>(-1)) {
            g_myzon_kernel = reinterpret_cast<MyzonRealKernelFn>(addr);
        }
    }

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return g_myzon_kernel != nullptr;
}

double run_cxx_reference(float* data, std::size_t count, uint64_t& out_checksum) {
    const auto t0 = std::chrono::steady_clock::now();

    float x = 1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        x = x * 1.000001f + 0.000001f;
        data[i] = x;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    uint64_t cs = 0;
    for (std::size_t i = 0; i < count; ++i) {
        cs ^= hash_float(data[i] + static_cast<float>(i & 255u));
    }
    out_checksum = cs;
    return elapsed_ns;
}

double run_myzon_actual(float* data, std::size_t count, uint64_t& out_checksum) {
    const auto t0 = std::chrono::steady_clock::now();

    float final_val = g_myzon_kernel(data, count);
    (void)final_val;

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    uint64_t cs = 0;
    for (std::size_t i = 0; i < count; ++i) {
        cs ^= hash_float(data[i] + static_cast<float>(i & 255u));
    }
    out_checksum = cs;
    return elapsed_ns;
}

} // namespace

int main() {
    #ifdef _WIN32
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        SetThreadAffinityMask(GetCurrentThread(), 0x4); // Pin to Core 2
    #endif

    std::cout << "=========================================================\n";
    std::cout << " MYZON AUTHENTIC SPEED EVALUATION (UNMASKED JIT EXECUTION)\n";
    std::cout << "=========================================================\n\n";

    if (!compile_myzon_real_kernel()) {
        std::cerr << "[FAIL] Could not compile Myzon kernel to executable memory.\n";
        return 1;
    }

    std::vector<float> buffer_cxx(kElementCount, 0.0f);
    std::vector<float> buffer_myzon(kElementCount, 0.0f);

    uint64_t dummy_cs = 0;
    for (int i = 0; i < kWarmupRuns; ++i) {
        run_cxx_reference(buffer_cxx.data(), kElementCount, dummy_cs);
        run_myzon_actual(buffer_myzon.data(), kElementCount, dummy_cs);
    }

    std::vector<double> cxx_times;
    std::vector<double> myzon_times;
    uint64_t cs_cxx = 0;
    uint64_t cs_myz = 0;

    for (int i = 0; i < kMeasuredRuns; ++i) {
        std::memset(buffer_cxx.data(), 0, kElementCount * sizeof(float));
        std::memset(buffer_myzon.data(), 0, kElementCount * sizeof(float));

        cxx_times.push_back(run_cxx_reference(buffer_cxx.data(), kElementCount, cs_cxx));
        myzon_times.push_back(run_myzon_actual(buffer_myzon.data(), kElementCount, cs_myz));
    }

    std::sort(cxx_times.begin(), cxx_times.end());
    std::sort(myzon_times.begin(), myzon_times.end());

    double median_cxx_ns   = cxx_times[kMeasuredRuns / 2];
    double median_myzon_ns = myzon_times[kMeasuredRuns / 2];

    double cxx_throughput   = (static_cast<double>(kElementCount) * 1e9) / std::max(median_cxx_ns, 1.0);
    double myzon_throughput = (static_cast<double>(kElementCount) * 1e9) / std::max(median_myzon_ns, 1.0);
    double cer = (myzon_throughput / cxx_throughput) * 100.0;
    bool match = (cs_cxx == cs_myz) && (cs_cxx != 0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Raw Execution Results (Median of " << kMeasuredRuns << " runs):\n";
    std::cout << "  Native C++ (g++ -O3): " << (median_cxx_ns / 1e6) << " ms (" << cxx_throughput << " nodes/sec)\n";
    std::cout << "  Myzon JIT (DGTL IR):  " << (median_myzon_ns / 1e6) << " ms (" << myzon_throughput << " nodes/sec)\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "  Compiler Efficiency Ratio (CER): " << cer << "%\n";
    std::cout << "  Native Checksum: " << cs_cxx << "\n";
    std::cout << "  Myzon Checksum:  " << cs_myz << "\n";
    std::cout << "  Data Parity:     " << (match ? "EXACT MATCH (100% Correct)" : "MISMATCH") << "\n";
    std::cout << "=========================================================\n";

    if (g_session) {
        dgtl_session_destroy(g_session);
    }
    return match ? 0 : 2;
}