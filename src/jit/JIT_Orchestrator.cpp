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

module;

#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <chrono>
#include <mutex>

#if defined(_WIN32) || defined(_WIN64)
#   include <windows.h>
#else
#   include <sys/mman.h>
#   include <unistd.h>
#endif

module dgtl.ir_core.jit_orchestrator;

import dgtl.numa_runtime;
import dgtl.runtime_intrinsics;
import dgtl.ir_core.code_cache;
import dgtl.ir_core.execution_dispatcher;
import dgtl.ir_core.linker_backend;
import dgtl.ir_core.semantic;
import dgtl.ir_core.macho_emitter;
import dgtl.ir_core.wasmsimd_fma_emitter;

// ============================================================================
// SECTION 1: MULTIARCH_ORCHESTRATOR.CPP (Multi-Architecture Dispatch)
// ============================================================================

namespace dgtl::jit {

namespace {

struct EmitContext {
    const dgtl::ir::IRStream* stream = nullptr;
    std::vector<uint8_t>* out = nullptr;
};

struct EmitCounters {
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> dispatcher_routes_x64{0};
    std::atomic<uint64_t> dispatcher_routes_arm64{0};
    std::atomic<uint64_t> dispatcher_routes_wasm{0};
    std::atomic<uint64_t> dispatcher_failures{0};
};

EmitCounters& emit_counters() {
    static EmitCounters counters{};
    return counters;
}

[[nodiscard]] bool write_binary_file(std::string_view path, const std::vector<uint8_t>& bytes) {
    if (path.empty()) {
        return false;
    }
    std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return out.good();
}

[[nodiscard]] uint64_t stream_fingerprint(const dgtl::ir::IRStream& stream) {
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node_readonly(i);
        auto mix = [&](uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<uint64_t>(n.opcode));
        mix(n.result_id);
        mix(n.op_a);
        mix(n.op_b);
        mix(n.op_c);
        mix(static_cast<uint64_t>(n.flags));
        mix(static_cast<uint64_t>(n.logic_flags));
    }
    return h;
}

dgtl::execution::cache::Code_Cache& emit_cache() {
    static dgtl::execution::cache::Code_Cache cache(64ull * 1024ull * 1024ull);
    return cache;
}

dgtl::execution::dispatcher::Execution_Dispatcher& emit_dispatcher() {
    static dgtl::execution::dispatcher::Execution_Dispatcher dispatcher;
    static std::once_flag once;
    std::call_once(once, [&]() {
        dispatcher.set_executor(dgtl::execution::dispatcher::DispatchTarget::X64_Native,
            [](void* raw) -> int64_t {
                auto* ctx = static_cast<EmitContext*>(raw);
                if (!ctx || !ctx->stream || !ctx->out) return -1;
                dgtl::backend::linker::DGTLLinker linker;
                return linker.build_raw_x64_machine_code(*ctx->stream, *ctx->out) ? 0 : -2;
            });

        dispatcher.set_executor(dgtl::execution::dispatcher::DispatchTarget::ARM64_Native,
            [](void* raw) -> int64_t {
                auto* ctx = static_cast<EmitContext*>(raw);
                if (!ctx || !ctx->stream || !ctx->out) return -1;
                dgtl::semantic::SymbolTable symbols;
                dgtl::isa::MachOEmitter emitter(*ctx->stream, symbols);
                auto result = emitter.emit_to_binary();
                if (!result.success || result.binary_data.empty()) return -2;
                *ctx->out = std::move(result.binary_data);
                return 0;
            });

        dispatcher.set_executor(dgtl::execution::dispatcher::DispatchTarget::WASM_Interpreter,
            [](void* raw) -> int64_t {
                auto* ctx = static_cast<EmitContext*>(raw);
                if (!ctx || !ctx->stream || !ctx->out) return -1;
                dgtl::backend::linker::DGTLLinker linker;
                return linker.build_wasm_binary(*ctx->stream, *ctx->out) ? 0 : -2;
            });
    });
    return dispatcher;
}

[[nodiscard]] bool emit_cached(dgtl::execution::dispatcher::DispatchTarget target,
                               std::string_view key,
                               const dgtl::ir::IRStream& stream,
                               std::vector<uint8_t>& out) {
    if (emit_cache().retrieve_copy(key, out)) {
        emit_counters().cache_hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    emit_counters().cache_misses.fetch_add(1, std::memory_order_relaxed);
    switch (target) {
        case dgtl::execution::dispatcher::DispatchTarget::X64_Native:
            emit_counters().dispatcher_routes_x64.fetch_add(1, std::memory_order_relaxed);
            break;
        case dgtl::execution::dispatcher::DispatchTarget::ARM64_Native:
            emit_counters().dispatcher_routes_arm64.fetch_add(1, std::memory_order_relaxed);
            break;
        case dgtl::execution::dispatcher::DispatchTarget::WASM_Interpreter:
        case dgtl::execution::dispatcher::DispatchTarget::WASM_:
            emit_counters().dispatcher_routes_wasm.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }

    EmitContext ctx{&stream, &out};
    const int64_t rc = emit_dispatcher().dispatch(target, &ctx);
    if (rc != 0 || out.empty()) {
        emit_counters().dispatcher_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    (void)emit_cache().store(key, out.data(), out.size());
    return true;
}

} // namespace

// ============================================================================
// STAGE J7 SYMBOL MAPPING: Bind Host C Intrinsics to JIT Session Symbols
// ============================================================================

// void map_core_symbols(auto& session) {
//     session.map_symbol("__dgtl_rt_println_i64", reinterpret_cast<uint64_t>(&__dgtl_rt_println_i64));
//     session.map_symbol("__dgtl_rt_print_i64", reinterpret_cast<uint64_t>(&__dgtl_rt_print_i64));
//     session.map_symbol("__dgtl_rt_println_f64", reinterpret_cast<uint64_t>(&__dgtl_rt_println_f64));
//     session.map_symbol("__dgtl_rt_print_f64", reinterpret_cast<uint64_t>(&__dgtl_rt_print_f64));
//     session.map_symbol("__dgtl_rt_println_str", reinterpret_cast<uint64_t>(&__dgtl_rt_println_str));
//     session.map_symbol("__dgtl_rt_print_str", reinterpret_cast<uint64_t>(&__dgtl_rt_print_str));
//     session.map_symbol("__dgtl_rt_println_bool", reinterpret_cast<uint64_t>(&__dgtl_rt_println_bool));
//     session.map_symbol("__dgtl_rt_print_bool", reinterpret_cast<uint64_t>(&__dgtl_rt_print_bool));
// }

[[maybe_unused]] EmitUtilizationMetrics get__emit_utilization_metrics() noexcept {
    const auto& c = emit_counters();
    EmitUtilizationMetrics m{};
    m.cache_hits = c.cache_hits.load(std::memory_order_relaxed);
    m.cache_misses = c.cache_misses.load(std::memory_order_relaxed);
    m.dispatcher_routes_x64 = c.dispatcher_routes_x64.load(std::memory_order_relaxed);
    m.dispatcher_routes_arm64 = c.dispatcher_routes_arm64.load(std::memory_order_relaxed);
    m.dispatcher_routes_wasm = c.dispatcher_routes_wasm.load(std::memory_order_relaxed);
    m.dispatcher_failures = c.dispatcher_failures.load(std::memory_order_relaxed);
    return m;
}

[[maybe_unused]] void reset__emit_utilization_metrics() noexcept {
    auto& c = emit_counters();
    c.cache_hits.store(0, std::memory_order_relaxed);
    c.cache_misses.store(0, std::memory_order_relaxed);
    c.dispatcher_routes_x64.store(0, std::memory_order_relaxed);
    c.dispatcher_routes_arm64.store(0, std::memory_order_relaxed);
    c.dispatcher_routes_wasm.store(0, std::memory_order_relaxed);
    c.dispatcher_failures.store(0, std::memory_order_relaxed);
}

void DGTLOrchestrator::dispatch_to_linker(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::backend::linker::DGTLLinker linker;
    linker.emit_elf64(path, stream);
}

void DGTLOrchestrator::dispatch_to_relocatable(std::string_view path, const dgtl::ir::IRStream& stream) {
    std::vector<uint8_t> out_bytes;
    const std::string cache_key = ":elfobj:" + std::to_string(stream_fingerprint(stream));
    if (!emit_cached(dgtl::execution::dispatcher::DispatchTarget::X64_Native, cache_key, stream, out_bytes)) {
        throw std::runtime_error("ELF_RELOCATABLE_WRITE_FAILED: Unable to emit ELF relocatable object");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("ELF_RELOCATABLE_WRITE_FAILED: Unable to write ELF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_archive(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::backend::linker::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_static_archive_binary(stream, out_bytes, "module.o")) {
        throw std::runtime_error("ARCHIVE_WRITE_FAILED: Unable to emit static archive");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("ARCHIVE_WRITE_FAILED: Unable to write static archive");
    }
}

void DGTLOrchestrator::dispatch_to_coff_relocatable(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::backend::linker::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_coff_relocatable_binary(stream, out_bytes, "DGTL_kernel_entry")) {
        throw std::runtime_error("COFF_RELOCATABLE_WRITE_FAILED: Unable to emit COFF relocatable object");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("COFF_RELOCATABLE_WRITE_FAILED: Unable to write COFF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_coff_archive(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::backend::linker::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_coff_archive_binary(stream, out_bytes, "module.obj", "DGTL_kernel_entry")) {
        throw std::runtime_error("COFF_ARCHIVE_WRITE_FAILED: Unable to emit COFF archive");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("COFF_ARCHIVE_WRITE_FAILED: Unable to write COFF archive");
    }
}

void DGTLOrchestrator::dispatch_to_macho(std::string_view path, const dgtl::ir::IRStream& stream) {
    std::vector<uint8_t> out_bytes;
    const std::string cache_key = ":macho:" + std::to_string(stream_fingerprint(stream));
    if (!emit_cached(dgtl::execution::dispatcher::DispatchTarget::ARM64_Native, cache_key, stream, out_bytes)) {
        throw std::runtime_error("MACHO_WRITE_FAILED: Failed to emit Mach-O object binary");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("MACHO_WRITE_FAILED: Failed to write Mach-O object binary");
    }
}

void DGTLOrchestrator::dispatch_to_wasm(std::string_view path, const dgtl::ir::IRStream& stream) {
    std::vector<uint8_t> out_bytes;
    const std::string cache_key = ":wasm:" + std::to_string(stream_fingerprint(stream));
    if (!emit_cached(dgtl::execution::dispatcher::DispatchTarget::WASM_Interpreter, cache_key, stream, out_bytes)) {
        throw std::runtime_error("WASM_WRITE_FAILED: Failed to emit wasm binary");
    }
    if (!write_binary_file(path, out_bytes)) {
        throw std::runtime_error("WASM_WRITE_FAILED: Failed to write wasm binary");
    }
}

// ============================================================================
// SECTION 2: STRIKE_RUNNER.CPP (Runtime Execution Implementation)
// ============================================================================

#ifndef DGTL_STRIKE_RUNNER_USE_CPP_IMPL
#define DGTL_STRIKE_RUNNER_USE_CPP_IMPL 1
#endif

#if DGTL_STRIKE_RUNNER_USE_CPP_IMPL

namespace detail {

// COMMENTED OUT DUPLICATE BLOCK TO PREVENT REDEFINITION IN JIT_Executor.ixx
// void prime_runner_simd_units() noexcept {
//     dgtl::core::prime_preferred_simd_units(2048);
// }

// void stabilize_hardware_clock() noexcept {
//     const char* env = std::getenv("DGTL_STABILIZE_CLOCK");
//     if (!env || std::strcmp(env, "1") != 0) {
//         return;
//     }

//     constexpr auto kStabilizationWindow = std::chrono::milliseconds(10);
//     const auto deadline = std::chrono::steady_clock::now() + kStabilizationWindow;
//     volatile std::uint64_t spin_sink = 0;

//     while (std::chrono::steady_clock::now() < deadline) {
//         spin_sink = (spin_sink * 1664525ull) + 1013904223ull;
//     }

//     (void)spin_sink;
// }

} // namespace detail

void StrikeRunner::release_exec_region() noexcept {
    if (!exec_region_) {
        return;
    }

    dgtl::runtime::numa::free_executable_region(exec_region_, exec_size_);

    exec_region_ = nullptr;
    exec_size_ = 0;
    exec_addr_ = 0;
}

StrikeRunner::~StrikeRunner() {
    release_exec_region();
}

StrikeRunner::StrikeRunner(StrikeRunner&& other) noexcept
    : exec_region_(other.exec_region_),
      exec_size_(other.exec_size_),
      exec_addr_(other.exec_addr_) {
    other.exec_region_ = nullptr;
    other.exec_size_ = 0;
    other.exec_addr_ = 0;
}

StrikeRunner& StrikeRunner::operator=(StrikeRunner&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release_exec_region();
    exec_region_ = other.exec_region_;
    exec_size_ = other.exec_size_;
    exec_addr_ = other.exec_addr_;

    other.exec_region_ = nullptr;
    other.exec_size_ = 0;
    other.exec_addr_ = 0;
    return *this;
}

size_t StrikeRunner::inject_code(dgtl::core::DGTL_Arena& arena, const std::vector<uint8_t>& machine_code) {
    (void)arena;
    release_exec_region();

    if (machine_code.empty()) {
        return 0;
    }

    const size_t alloc_size = (machine_code.size() + 63ULL) & ~63ULL;

    const uint32_t target_node = dgtl::runtime::numa::get_current_node();
    exec_region_ = static_cast<uint8_t*>(dgtl::runtime::numa::allocate_executable_on_node(alloc_size, target_node));
    if (!exec_region_) {
        return 0;
    }

    std::memcpy(exec_region_, machine_code.data(), machine_code.size());

    if (!dgtl::runtime::numa::make_executable(exec_region_, alloc_size)) {
        release_exec_region();
        return 0;
    }

    exec_size_ = alloc_size;
    exec_addr_ = reinterpret_cast<uintptr_t>(exec_region_);
    return machine_code.size();
}

uintptr_t StrikeRunner::last_injected_address() const noexcept {
    return exec_addr_;
}

StrikeMetrics StrikeRunner::execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload) {
    (void)arena;

    if (!exec_region_ || injected_size == 0) {
        return StrikeMetrics{0, static_cast<uint32_t>(StrikeStatus::InvalidInput)};
    }

    StrikeMetrics metrics{};
    metrics.executed_instructions = injected_size;

    (void)dgtl::core::apply_high_performance_affinity();
    detail::prime_runner_simd_units();
    detail::stabilize_hardware_clock();

    const char* env = std::getenv("DGTL_STRIKE_EXECUTE");
    if (env && std::strcmp(env, "0") == 0) {
        metrics.status_code = static_cast<uint32_t>(StrikeStatus::NoExecutePermission);
        return metrics;
    }

    using Entry = uint64_t (*)(void*);
    const auto fn = reinterpret_cast<Entry>(exec_region_);
    if (!fn) {
        metrics.status_code = static_cast<uint32_t>(StrikeStatus::NullEntryPoint);
        return metrics;
    }

    (void)fn(payload);
    metrics.status_code = static_cast<uint32_t>(StrikeStatus::Ok);
    return metrics;
}

#endif // DGTL_STRIKE_RUNNER_USE_CPP_IMPL

} // namespace dgtl::jit