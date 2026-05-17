/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/jit/JIT_Orchestrator.hpp"

#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>

#include "dgtl/backend/linker/Linker_Backend.hpp"
#include "dgtl/isa/target/Wasm_Target.hpp"
#include "dgtl/isa/emitter/MachO_Emitter.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 1: MULTIARCH_ORCHESTRATOR.CPP (Multi-Architecture Dispatch Implementation)
// ═══════════════════════════════════════════════════════════════════════════════

namespace dgtl::jit {

void DGTLOrchestrator::dispatch_to_linker(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::backend::linker::DGTLLinker linker;
    // Pass the orchestrator's target context to the linker for hardware-specific ELF headers
    linker.emit_elf64(path, stream);
}

void DGTLOrchestrator::dispatch_to_relocatable(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::exec::DGTLLinker linker;
    // Ensure relocatable objects carry the correct ISA markers from our TargetMachine
    std::vector<uint8_t> out_bytes;
    if (!linker.build_elf_relocatable_binary(stream, out_bytes, "DGTL_kernel_entry")) {
        throw std::runtime_error("ELF_RELOCATABLE_WRITE_FAILED: Unable to emit ELF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_archive(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_static_archive_binary(stream, out_bytes, "module.o")) {
        throw std::runtime_error("ARCHIVE_WRITE_FAILED: Unable to emit static archive");
    }
}

void DGTLOrchestrator::dispatch_to_coff_relocatable(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_coff_relocatable_binary(stream, out_bytes, "DGTL_kernel_entry")) {
        throw std::runtime_error("COFF_RELOCATABLE_WRITE_FAILED: Unable to emit COFF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_coff_archive(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> out_bytes;
    if (!linker.build_coff_archive_binary(stream, out_bytes, "module.obj", "DGTL_kernel_entry")) {
        throw std::runtime_error("COFF_ARCHIVE_WRITE_FAILED: Unable to emit COFF archive");
    }
}

void DGTLOrchestrator::dispatch_to_macho(std::string_view path, const dgtl::ir::IRStream& stream) {
    dgtl::semantic::SymbolTable symbols;
    // Mach-O emission is architecture-sensitive (ARM64 vs x86_64)
    dgtl::isa::MachOEmitter emitter(stream, symbols);
    auto result = emitter.emit_to_binary();
    if (!result.success) {
        throw std::runtime_error("MACHO_WRITE_FAILED: Failed to emit Mach-O object binary");
    }
    // Optionally, write result.binary_data to file if needed
}

void DGTLOrchestrator::dispatch_to_wasm(std::string_view path, const dgtl::ir::IRStream& stream) {
    // WebAssembly emission uses the dedicated WASM encoder
    dgtl::exec::DGTLWasmEncoder encoder;
    encoder.encode(stream);
    // TODO: Write encoder output to path
}

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 2: STRIKE_RUNNER.CPP (Runtime Execution Implementation)
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef DGTL_STRIKE_RUNNER_USE_CPP_IMPL
#define DGTL_STRIKE_RUNNER_USE_CPP_IMPL 0
#endif

#if DGTL_STRIKE_RUNNER_USE_CPP_IMPL

namespace detail {

void prime_runner_simd_units() noexcept {
    dgtl::core::prime_preferred_simd_units(2048);
}

void stabilize_hardware_clock() noexcept {
    constexpr auto kStabilizationWindow = std::chrono::milliseconds(500);
    const auto deadline = std::chrono::steady_clock::now() + kStabilizationWindow;
    volatile std::uint64_t spin_sink = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        spin_sink = (spin_sink * 1664525ull) + 1013904223ull;
    }

    (void)spin_sink;
}

} // namespace detail

void StrikeRunner::release_exec_region() noexcept {
    if (!exec_region_) {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    VirtualFree(exec_region_, 0, MEM_RELEASE);
#else
    munmap(exec_region_, exec_size_);
#endif

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

#if defined(_WIN32) || defined(_WIN64)
    exec_region_ = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    );
    if (!exec_region_) {
        return 0;
    }

    std::memcpy(exec_region_, machine_code.data(), machine_code.size());

    DWORD old_protect = 0;
    if (!VirtualProtect(exec_region_, alloc_size, PAGE_EXECUTE_READ, &old_protect)) {
        release_exec_region();
        return 0;
    }
#else
    exec_region_ = static_cast<uint8_t*>(
        mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    if (!exec_region_ || exec_region_ == MAP_FAILED) {
        exec_region_ = nullptr;
        return 0;
    }

    std::memcpy(exec_region_, machine_code.data(), machine_code.size());

    if (mprotect(exec_region_, alloc_size, PROT_READ | PROT_EXEC) != 0) {
        release_exec_region();
        return 0;
    }

    __builtin___clear_cache(reinterpret_cast<char*>(exec_region_),
                            reinterpret_cast<char*>(exec_region_ + alloc_size));
#endif

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

    // Safety gate: execution is opt-in via environment variable.
    // Set DGTL_STRIKE_EXECUTE=1 to enable native jump execution.
    const char* env = std::getenv("DGTL_STRIKE_EXECUTE");
    if (!env || std::strcmp(env, "1") != 0) {
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
