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

#pragma once

// --- OS-SPECIFIC MACRO INITIALIZATION ---
#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // Target Windows 7 or higher to ensure modern memory management macros are visible
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601
    #endif
    #include <windows.h>
    #include <memoryapi.h>

    // Mandatory Ironclad enforcement for UCRT64 environments
    #undef MEM_COMMIT
    #define MEM_COMMIT      0x00001000
    #undef MEM_RESERVE
    #define MEM_RESERVE     0x00002000
    #undef MEM_RELEASE
    #define MEM_RELEASE     0x00008000
    #undef PAGE_READWRITE
    #define PAGE_READWRITE  0x04
    #undef PAGE_EXECUTE_READ
    #define PAGE_EXECUTE_READ 0x20
#else
    #include <unistd.h>
#endif

#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <memory>
#include <array>
#include <cstdlib>
#include <string_view>
#include <stdexcept>
#include <string>
#include <cctype>
#include <fstream>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

// --- APPLE SILICON HARDENED RUNTIME ---
#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #ifndef MAP_JIT
        #define MAP_JIT 0x0800
    #endif
#endif

// --- DGTL INFRASTRUCTURE ---
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/core/Compiler_Context.hpp"
#include "dgtl/backend/lowering/Stack_Spiller.hpp"
#include "dgtl/backend/driver/Backend_Driver_API.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include "dgtl/isa/lowering/X64_Machine_Form_Lowering.hpp"
#include "dgtl/isa/regalloc/X64_GraphColoring_RegAlloc.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"
#include "dgtl/pass/transform/Transform_Poly_Loop.hpp"
#include "dgtl/runtime/Runtime_Intrinsics.hpp"
#include "dgtl/jit/JIT_Executor.hpp"
#include "dgtl/jit/Async_Dopt_Tiered.hpp"

// Forward Declarations for specialized backend emitters
namespace dgtl::exec {
    class DGTLMachOEmitter; 
    class DGTLWasmEncoder;  
}

// StrikeRunner, StrikeMetrics, StrikeStatus, and detail:: helpers have been
// moved to JIT_Executor.hpp (namespace dgtl::jit). They are owned by the
// code-execution layer, not the orchestration layer. JIT_Executor.hpp is
// already included above, so all three types are visible here.

namespace dgtl::jit {

// SECTION 1 (StrikeRunner) removed â€” see JIT_Executor.hpp.

// StrikeRunner, StrikeMetrics, StrikeStatus, and detail:: helpers are defined
// in JIT_Executor.hpp (namespace dgtl::jit) â€” see JIT_Executor.hpp, Section 2.
// This file (JIT_Orchestrator.hpp) consumes those types for pipeline coordination only.

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// SECTION 2: BINARY_ORCHESTRATOR.HPP (Hardware-Native Binary Layout)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

/**
 * @brief Represents a compiled chunk of machine code ready for final layout.
 */
struct BinarySegment {
    std::vector<uint8_t> machine_code;
    float hotness_score = 0.0f;
    uint32_t original_id = 0;
    uintptr_t final_load_address = 0; // Relative to blob start
};

/**
 * @class DGTLBinaryOrchestratorFinal
 * @brief Performs the "Final Strike" by weaving segments into an optimized binary blob.
 */
class DGTLBinaryOrchestratorFinal {
    static constexpr size_t CPU_CACHE_LINE = 64;
    static constexpr float HOT_THRESHOLD = 0.85f;
    
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    explicit DGTLBinaryOrchestratorFinal(dgtl::analysis::DGTLAICostModel& ai) : ai_model(ai) {}

    /**
     * @brief The "Global Strike" Layout Engine.
     * Performs final layout optimization and link-time code shifting.
     */
    std::vector<uint8_t> finalize_production_binary(std::vector<BinarySegment>& segments) {
        if (segments.empty()) [[unlikely]] return {};
        (void)ai_model;

        // 1. Hot-to-Cold Partitioning
        // Sorting by hotness ensures high-frequency code lives in the same TLB pages,
        // reducing TLB misses and improving instruction cache locality.
        std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) {
            return a.hotness_score > b.hotness_score;
        });

        std::vector<uint8_t> final_blob;
        size_t total_bytes = 0;
        for (const auto& seg : segments) {
            total_bytes += seg.machine_code.size();
        }
        // Reserve exact payload plus one cache line headroom for alignment padding.
        final_blob.reserve(total_bytes + CPU_CACHE_LINE);

        for (auto& seg : segments) {
            // 2. Cache-Line Alignment (The "Zero-Straddle" Rule)
            // If the segment is 'Hot', we force 64-byte alignment.
            // This ensures the CPU decoder doesn't fetch across two cache lines for loop entries.
            if (seg.hotness_score >= HOT_THRESHOLD) {
                size_t current_pos = final_blob.size();
                size_t padding = (CPU_CACHE_LINE - (current_pos % CPU_CACHE_LINE)) % CPU_CACHE_LINE;
                
                if (padding > 0) {
                    inject_optimized_nops(final_blob, padding);
                }
            }

            // Record finalized relative address
            seg.final_load_address = static_cast<uintptr_t>(final_blob.size());

            // 3. Global Branch Patching
            // Refines jump offsets based on the finalized sorted order.
            patch_relative_jumps(seg);

            // 4. Final Insertion Strike
            final_blob.insert(final_blob.end(), seg.machine_code.begin(), seg.machine_code.end());
        }

        return final_blob;
    }

private:
    /**
     * @brief High-Performance NOP injection (x86_64 Optimized).
     * Uses Intel/AMD recommended multi-byte sequences to keep 
     * decoder pipelines full without consuming execution ports.
     */
    void inject_optimized_nops(std::vector<uint8_t>& blob, size_t count) {
        // Multi-Byte NOP sequences (Standard x86_64)
        static const uint8_t NOP9[] = {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t NOP4[] = {0x0F, 0x1F, 0x40, 0x00};
        
        while (count > 0) {
            if (count >= 9) {
                blob.insert(blob.end(), NOP9, NOP9 + 9);
                count -= 9;
            } else if (count >= 4) {
                blob.insert(blob.end(), NOP4, NOP4 + 4);
                count -= 4;
            } else {
                blob.push_back(0x90); // 1-byte NOP
                count--;
            }
        }
    }

    /**
     * @brief Shrink/Expand Jumps.
     * Swaps E9 (JMP rel32) with EB (JMP rel8) if the target is within 127 bytes.
     * This improves code density and reduces instruction fetch bandwidth.
     */
    void patch_relative_jumps(BinarySegment& seg) {
        // Distill long near jumps (E9 rel32) into short jumps (EB rel8) when possible,
        // while preserving instruction footprint with trailing NOPs.
        for (size_t i = 0; i + 4 < seg.machine_code.size(); ++i) {
            if (seg.machine_code[i] != 0xE9) {
                continue;
            }

            int32_t rel32 = 0;
            std::memcpy(&rel32, &seg.machine_code[i + 1], sizeof(rel32));

            const int64_t target = static_cast<int64_t>(i) + 5 + static_cast<int64_t>(rel32);
            const int64_t rel8 = target - (static_cast<int64_t>(i) + 2);

            if (rel8 >= -128 && rel8 <= 127) {
                seg.machine_code[i] = 0xEB;
                seg.machine_code[i + 1] = static_cast<uint8_t>(static_cast<int8_t>(rel8));
                seg.machine_code[i + 2] = 0x90;
                seg.machine_code[i + 3] = 0x90;
                seg.machine_code[i + 4] = 0x90;
            }
        }
    }
};

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// SECTION 3: MULTIARCH_ORCHESTRATOR.HPP (Multi-Architecture Dispatch)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

/**
 * @struct CompilationReport
 * @brief High-precision telemetry for the compilation strike.
 */
struct CompilationReport {
    double total_ms = 0.0;
    size_t nodes_processed = 0;
    double throughput_nodes_sec = 0.0;
    bool validation_opted = false;
    std::string target_arch_used; 
};

/**
 * @class DGTLOrchestrator
 * @brief The final gatekeeper before hardware-native binary generation.
 */
class DGTLOrchestrator {
    CompilationReport last_report;
    dgtl::core::TargetMachine target; 

public:
    /**
     * @brief Constructor initializes hardware discovery.
     * Uses preprocessor detection to satisfy TargetMachine(ArchType).
     */
    explicit DGTLOrchestrator() 
        : target([]() {
            #if defined(__x86_64__) || defined(_M_X64)
                return select_preferred_x86_arch();
            #elif defined(__aarch64__) || defined(_M_ARM64)
                return dgtl::core::ArchType::AARCH64_SVE;
            #else
                return dgtl::core::ArchType::WASM_SIMD;
            #endif
          }()) 
    {}

    /**
     * @brief Zero-Corruption Entry Point for Binary Generation.
     * @param output_path The filesystem destination for the binary.
     * @param stream The validated, committed IRStream.
     */
    void compile_to_binary(std::string_view output_path, const dgtl::ir::IRStream& stream) {
        last_report = CompilationReport{};

        if (output_path.empty()) [[unlikely]] {
            trigger_fatal_trap("INVALID_OUTPUT_PATH", "Output path for binary emission cannot be empty.");
        }

        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            return;
        }

        if (!stream.validate_shadow_integrity()) [[unlikely]] {
            trigger_fatal_trap("TRANSACTION_NOT_COMMITTED", 
                "Attempted to emit binary from an unvalidated Shadow Stream.");
        }

        last_report.validation_opted = true;
        
        // Manual string representation since get_triple() is missing in Target.hpp
        switch (target.get_arch()) {
            case dgtl::core::ArchType::X86_64_AVX512:
            case dgtl::core::ArchType::X86_64_AVX2:
                last_report.target_arch_used = "x86_64-pc-linux-gnu";
                break;
            case dgtl::core::ArchType::AARCH64_SVE:
                last_report.target_arch_used = "aarch64-unknown-linux-gnu";
                break;
            case dgtl::core::ArchType::WASM_SIMD:
                last_report.target_arch_used = "wasm32-unknown-unknown";
                break;
            case dgtl::core::ArchType::FPGA_VERILOG:
            default:
                last_report.target_arch_used = "generic-unknown-unknown";
                break;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        std::string out_path(output_path);
        std::string ext_lower;
        if (const size_t dot = out_path.find_last_of('.'); dot != std::string::npos) {
            ext_lower = out_path.substr(dot);
            for (char& ch : ext_lower) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }

        if (ext_lower == ".wasm") {
            this->dispatch_to_wasm(output_path, stream);
        }
        else if (ext_lower == ".o") {
            this->dispatch_to_relocatable(output_path, stream);
        }
        else if (ext_lower == ".obj") {
            this->dispatch_to_coff_relocatable(output_path, stream);
        }
        else if (ext_lower == ".a") {
            this->dispatch_to_archive(output_path, stream);
        }
        else if (ext_lower == ".lib") {
            this->dispatch_to_coff_archive(output_path, stream);
        }
        else if (ext_lower == ".macho") {
            this->dispatch_to_macho(output_path, stream);
        }
        else {
            this->dispatch_to_linker(output_path, stream);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        
        last_report.total_ms = elapsed.count();
        last_report.nodes_processed = stream.size();

        if (last_report.total_ms > 1e-9) {
            last_report.throughput_nodes_sec = (static_cast<double>(stream.size()) / (last_report.total_ms / 1000.0));
        } else {
            last_report.throughput_nodes_sec = static_cast<double>(stream.size()) * 1000000.0; 
        }
    }

    [[nodiscard]] CompilationReport get_last_report() const noexcept {
        return last_report;
    }

    const dgtl::core::TargetMachine& get_target() const noexcept {
        return target;
    }

private:
    [[noreturn]] void trigger_fatal_trap(const char* code, const char* details) {
        throw std::runtime_error(std::string(code) + ": " + details);
    }

    static constexpr dgtl::core::ArchType select_preferred_x86_arch() noexcept {
        #if defined(__AVX512F__)
            return dgtl::core::ArchType::X86_64_AVX512;
        #else
            return dgtl::core::ArchType::X86_64_AVX2;
        #endif
    }

    /* Dispatch methods - implemented in .cpp file */
    void dispatch_to_linker(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_relocatable(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_archive(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_coff_relocatable(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_coff_archive(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_macho(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_wasm(std::string_view path, const dgtl::ir::IRStream& stream);
};

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// SECTION 4: STRIKE_ORCHESTRATOR.HPP (Command Center for JIT Compilation)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

/**
 * @struct RelocationPatch
 * @brief Represents a "post-it note" for the JIT linker to backpatch an unresolved CALL.
 */
struct RelocationPatch {
    size_t buffer_offset;         // The exact byte index in the machine_code_buffer of the 00 00 00 00
    std::string target_symbol;    // The name of the function we want to call (e.g., "fib")
    size_t instruction_end_offset;// The byte index IMMEDIATELY AFTER the 4-byte offset (used for relative math)
};

enum class TierKind : uint8_t {
    Tier1_Baseline = 1,
    Tier2_Optimized = 2,
};

enum class PointerLocationKind : uint8_t {
    Register = 0,
    StackSlot = 1,
};

struct PointerLocation {
    PointerLocationKind kind = PointerLocationKind::StackSlot;
    uint32_t id = 0;
};

struct StackMapRecord {
    uint32_t safepoint_id = 0;
    uint32_t ir_node_index = 0;
    uint32_t code_offset = 0;
    std::vector<PointerLocation> live_pointers;
};

struct StackMapTable {
    std::vector<StackMapRecord> records;

    void add_record(StackMapRecord record) {
        records.push_back(std::move(record));
    }

    [[nodiscard]] const StackMapRecord* find(uint32_t safepoint_id) const noexcept {
        for (const auto& rec : records) {
            if (rec.safepoint_id == safepoint_id) {
                return &rec;
            }
        }
        return nullptr;
    }
};

struct Phase3Stats {
    double tier1_compile_ms = 0.0;
    double tier2_compile_ms = 0.0;
    bool tier1_under_1ms = false;
    uint64_t osr_switches = 0;
    uint64_t deopt_bailouts = 0;
};

/**
 * @class StrikeOrchestrator
 * @brief Coordinates the transition from IRStream to high-frequency execution.
 * Orchestrates the "Fire" Strike using a Single-Pass Fixup Architecture to 
 * perfectly handle cyclic call graphs and infinite recursion.
 */
class StrikeOrchestrator {
    dgtl::core::DGTLContext& ctx;
    dgtl::core::DGTL_Arena& arena;
    dgtl::isa::StackSpiller spiller;   
    dgtl::isa::X64StrikeEmitter emitter;

    // Closed-loop learning components
    dgtl::analysis::DGTLAICostModel cost_model;
    dgtl::jit::runtime::DGTLJitExecutor executor;
    dgtl::jit::StrikeRunner strike_runner;
    dgtl::jit::Deopt_Handler deopt_handler_;
    dgtl::jit::AsyncSpeculator async_speculator_;
    dgtl::isa::regalloc::X64GraphColoringRegAlloc tier2_regalloc_;

    struct TieredFunctionState {
        std::atomic<dgtl::jit::runtime::JitFunction> tier1_fn{nullptr};
        std::atomic<dgtl::jit::runtime::JitFunction> tier2_fn{nullptr};
        std::atomic<dgtl::jit::runtime::JitFunction> active_fn{nullptr};
        std::atomic<TierKind> active_tier{TierKind::Tier1_Baseline};
        std::atomic<bool> tier2_ready{false};
        std::atomic<uint64_t> invocation_count{0};
        std::unordered_set<uint32_t> pointer_virtuals;
        StackMapTable stack_maps;
        Phase3Stats stats;
    };

    mutable std::mutex tiered_mtx_;
    std::unordered_map<uint64_t, std::shared_ptr<TieredFunctionState>> tiered_functions_;
    std::unordered_map<uint64_t, std::shared_ptr<dgtl::ir::IRStream>> tier2_streams_;
    std::queue<uint64_t> tier2_queue_;
    std::condition_variable tier2_cv_;
    std::thread tier2_worker_;
    std::atomic<bool> tier2_stop_{false};
    std::atomic<uint64_t> next_tiered_id_{1};

    // PRE-ALLOCATED SCRATCH PAD: 16MB Initial Reserve
    std::vector<uint8_t> machine_code_buffer;

    // â”€â”€ THE RELOCATION LINKER ARCHITECTURE â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    
    // Maps a function/symbol name to its absolute memory address in the injected Arena.
    std::unordered_map<std::string, uintptr_t> global_symbol_table;
    
    // Maps a function/symbol name to its relative byte offset inside the scratch buffer.
    std::unordered_map<std::string, size_t> buffer_symbol_offsets;

    // The list of unresolved E8 CALL offsets that need to be mathematically patched.
    std::vector<RelocationPatch> pending_patches;

public:
    static constexpr uint32_t STRIKE_OK = static_cast<uint32_t>(dgtl::jit::StrikeStatus::Ok);
    static constexpr uint32_t STRIKE_INVALID_STREAM = 0xE001;
    static constexpr uint32_t STRIKE_EMIT_EMPTY = 0xE002;
    static constexpr uint32_t STRIKE_INJECT_FAILED = static_cast<uint32_t>(dgtl::jit::StrikeStatus::InjectFailed);
    static constexpr uint32_t STRIKE_LINK_FAILED = 0xE003;

    StrikeOrchestrator(dgtl::core::DGTLContext& c, dgtl::core::DGTL_Arena& a)
        : ctx(c), arena(a), spiller(arena),
          executor(128, dgtl::core::select_preferred_x86_arch()),
          async_speculator_(executor, 2) {
        // Reserve 16MB to handle typical high-performance kernels without reallocation
        machine_code_buffer.reserve(1024 * 1024 * 16);
        const dgtl::core::TargetMachine target_machine(dgtl::core::select_preferred_x86_arch());
        emitter.set_target_machine(target_machine);
        executor.set_target_machine(target_machine);
        start_tier2_worker();
    }

    ~StrikeOrchestrator() {
        stop_tier2_worker();
    }

    /**
     * @brief Register a runtime function address into the Global Symbol Table.
     */
    void register_runtime_function(std::string name, uintptr_t address) {
        global_symbol_table[name] = address;
        emitter.register_runtime_function(std::move(name), address);
    }

    /**
     * @brief Register a runtime function address for CALL nodes by callee slot.
     */
    void register_runtime_function_slot(uint32_t slot, uintptr_t address) {
        emitter.register_runtime_function_slot(slot, address);
    }

    /**
     * @brief Register runtime intrinsic symbols into the linker.
     * (Placeholder: MYZON-specific intrinsic registration removed for DGTL boundary enforcement)
     */
    void register_bulletins() {
        // NOTE: This was previously linked to MYZON::for_each_runtime_intrinsic()
        // In DGTL, runtime intrinsics are registered via the backend driver API
        // See: dgtl::backend::driver::backend_target_abi_convention()
    }

    /**
     * @brief The Ultimate Relocation Linker (Module-Level Strike).
     * Compiles an entire module of cyclic/recursive functions seamlessly.
     */
    dgtl::jit::StrikeMetrics fire_module_strike(const std::vector<std::pair<std::string, dgtl::ir::IRStream>>& module_streams) {
        machine_code_buffer.clear();
        buffer_symbol_offsets.clear();
        pending_patches.clear();

        // 1. LINEAR EMISSION PHASE
        for (const auto& [name, stream] : module_streams) {
            if (stream.is_null() || stream.size() == 0) continue;

            // Record exactly where this function starts in the buffer
            buffer_symbol_offsets[name] = machine_code_buffer.size();

            // Tell the emitter to compile this stream.
            // NOTE: The emitter must be updated so that if it hits an unresolved CALL, 
            // it emits E8 00 00 00 00 and calls a callback or returns a list of RelocationPatches.
            // For now, we assume the emitter appends to machine_code_buffer natively.
            const bool emit_ok = emitter.process_stream(stream, machine_code_buffer);
            if (!emit_ok) {
                return {STRIKE_EMIT_EMPTY};
            }
        }

        // 2. INJECTION PHASE (W^X Security: Write to memory, then Seal)
        size_t injected_size = strike_runner.inject_code(arena, machine_code_buffer);
        if (injected_size == 0) {
            return {STRIKE_INJECT_FAILED};
        }

        uintptr_t module_base_address = strike_runner.last_injected_address();

        // Register the physical addresses of the newly compiled functions
        for (const auto& [name, offset] : buffer_symbol_offsets) {
            global_symbol_table[name] = module_base_address + offset;
        }

        // 3. THE BACKPATCH PHASE (Fixup Relocations)
        for (const auto& patch : pending_patches) {
            auto it = global_symbol_table.find(patch.target_symbol);
            if (it == global_symbol_table.end()) {
                std::fprintf(stderr, "[DGTL Linker] Fatal: Unresolved external symbol: %s\n", patch.target_symbol.c_str());
                return {STRIKE_LINK_FAILED};
            }

            uintptr_t target_address = it->second;
            uintptr_t instruction_end_addr = module_base_address + patch.instruction_end_offset;
            uintptr_t patch_ptr = module_base_address + patch.buffer_offset;

            // Calculate x64 relative offset: Target - (AddressOfNextInstruction)
            int32_t relative_offset = static_cast<int32_t>(target_address - instruction_end_addr);

            // Overwrite the 00 00 00 00 dummy bytes in the live memory
            // (Assumes the arena memory is currently PAGE_EXECUTE_READWRITE)
            std::memcpy(reinterpret_cast<void*>(patch_ptr), &relative_offset, sizeof(int32_t));
        }

        // TODO: Call arena.protect_read_execute_only() here for strict W^X security!

        // 4. EXECUTION PHASE: Find 'main' and jump!
        auto main_it = global_symbol_table.find("main");
        if (main_it != global_symbol_table.end()) {
            // Execute the resolved main function
            // (strike_runner.execute needs to jump to main_it->second)
            auto metrics = strike_runner.execute(arena, injected_size, nullptr); // Adjust as needed
            if (metrics.status_code == static_cast<uint32_t>(dgtl::jit::StrikeStatus::Ok)) {
                metrics.status_code = STRIKE_OK;
            }
            return metrics;
        }

        return {STRIKE_OK}; // Compiled successfully, but no main to run.
    }

    /**
     * @brief Legacy Single-Stream Strike (Maintains Backwards Compatibility)
     */
    dgtl::jit::StrikeMetrics fire_strike(dgtl::ir::IRStream& stream, void* data_payload = nullptr) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] {
            return {STRIKE_INVALID_STREAM};
        }

        machine_code_buffer.clear(); 
        const bool emit_ok = emitter.process_stream(stream, machine_code_buffer);
        if (!emit_ok || machine_code_buffer.empty()) {
            if (emitter.had_emission_error()) {
                std::fprintf(stderr, "[DGTL] Emission failed: %s (opcode=%u)\n", 
                    emitter.last_emission_error(), 
                    static_cast<uint32_t>(emitter.last_failed_opcode()));
            }
            return {STRIKE_EMIT_EMPTY};
        }

        size_t injected_size = strike_runner.inject_code(arena, machine_code_buffer);
        if (injected_size == 0) {
            return {STRIKE_INJECT_FAILED};
        }

        auto metrics = strike_runner.execute(arena, injected_size, data_payload);

        // CLOSED-LOOP FEEDBACK: Collect runtime metrics and update cost model
        if (!stream.is_null() && stream.size() > 0) {
            uintptr_t function_id = strike_runner.last_injected_address();
            if (function_id != 0) {
                auto feedback = executor.collect_feedback(function_id);
                for (size_t i = 0; i < stream.size(); ++i) {
                    const auto node = stream.get_node_readonly(i);
                    cost_model.update_with_feedback(node, feedback);
                }
            }
        }

        if (metrics.status_code == static_cast<uint32_t>(dgtl::jit::StrikeStatus::Ok)) {
            metrics.status_code = STRIKE_OK;
        }

        return metrics;
    }

    [[nodiscard]] uint64_t compile_tiered_function(const dgtl::ir::IRStream& stream,
                                                   bool queue_tier2 = true,
                                                   const std::vector<uint32_t>& gc_pointer_virtuals = {}) {
        if (stream.is_null() || stream.size() == 0) {
            return 0;
        }

        auto state = std::make_shared<TieredFunctionState>();
        state->pointer_virtuals.insert(gc_pointer_virtuals.begin(), gc_pointer_virtuals.end());
        std::vector<uint8_t> tier1_code;
        StackMapTable maps;
        const bool ok = compile_tier1_baseline(stream,
                               tier1_code,
                               maps,
                               state->stats.tier1_compile_ms,
                               &state->pointer_virtuals);
        if (!ok || tier1_code.empty()) {
            return 0;
        }

        dgtl::jit::runtime::JitFunction tier1_fn = executor.load_and_prepare(tier1_code, stream);
        if (!tier1_fn) {
            return 0;
        }

        state->tier1_fn.store(tier1_fn, std::memory_order_release);
        state->active_fn.store(tier1_fn, std::memory_order_release);
        state->active_tier.store(TierKind::Tier1_Baseline, std::memory_order_release);
        state->stack_maps = std::move(maps);
        state->stats.tier1_under_1ms = (state->stats.tier1_compile_ms < 1.0);

        const uint64_t id = next_tiered_id_.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(tiered_mtx_);
            tiered_functions_[id] = state;
            if (queue_tier2) {
                tier2_streams_[id] = std::make_shared<dgtl::ir::IRStream>(stream.clone());
                tier2_queue_.push(id);
            }
        }
        if (queue_tier2) {
            tier2_cv_.notify_one();
            // Opportunistically trigger speculative compilation in parallel.
            async_speculator_.speculate_branch(stream, 0);
        }
        return id;
    }

    [[nodiscard]] uint64_t compile_tiered_bytecode(const std::vector<dgtl::ir::IRNode>& bytecode,
                                                   bool queue_tier2 = true,
                                                   const std::vector<uint32_t>& gc_pointer_virtuals = {}) {
        if (bytecode.empty()) {
            return 0;
        }

        dgtl::ir::IRStream stream = dgtl::ir::IRStream::allocate_transactional(bytecode.size() + 8);
        for (const auto& node : bytecode) {
            stream.push_back(node);
        }
        return compile_tiered_function(stream, queue_tier2, gc_pointer_virtuals);
    }

    void register_gc_pointer_virtual(uint64_t function_id, uint32_t virtual_id) {
        std::lock_guard<std::mutex> lock(tiered_mtx_);
        auto it = tiered_functions_.find(function_id);
        if (it == tiered_functions_.end() || !it->second || virtual_id == dgtl::ir::INVALID_SLOT) {
            return;
        }
        it->second->pointer_virtuals.insert(virtual_id);
    }

    [[nodiscard]] uint64_t execute_tiered(uint64_t function_id, void* payload = nullptr) {
        std::shared_ptr<TieredFunctionState> state;
        {
            std::lock_guard<std::mutex> lock(tiered_mtx_);
            auto it = tiered_functions_.find(function_id);
            if (it == tiered_functions_.end()) {
                return 0;
            }
            state = it->second;
        }

        auto fn = state->active_fn.load(std::memory_order_acquire);
        if (!fn) {
            return 0;
        }

        const uint64_t result = fn(payload);
        const uint64_t count = state->invocation_count.fetch_add(1, std::memory_order_relaxed) + 1;

        if (state->tier2_ready.load(std::memory_order_acquire) && count >= 64 &&
            state->active_tier.load(std::memory_order_acquire) != TierKind::Tier2_Optimized) {
            if (try_osr_switch(function_id)) {
                state->stats.osr_switches++;
            }
        }

        return result;
    }

    [[nodiscard]] bool try_osr_switch(uint64_t function_id) {
        std::shared_ptr<TieredFunctionState> state;
        {
            std::lock_guard<std::mutex> lock(tiered_mtx_);
            auto it = tiered_functions_.find(function_id);
            if (it == tiered_functions_.end()) {
                return false;
            }
            state = it->second;
        }

        if (!state->tier2_ready.load(std::memory_order_acquire)) {
            return false;
        }

        auto tier2_fn = state->tier2_fn.load(std::memory_order_acquire);
        if (!tier2_fn) {
            return false;
        }

        state->active_fn.store(tier2_fn, std::memory_order_release);
        state->active_tier.store(TierKind::Tier2_Optimized, std::memory_order_release);
        return true;
    }

    [[nodiscard]] dgtl::jit::runtime::JitFunction maybe_osr_poll(uint64_t function_id,
                                                                  [[maybe_unused]] uint32_t safepoint_id,
                                                                  [[maybe_unused]] uint64_t loop_iteration) {
        std::shared_ptr<TieredFunctionState> state;
        {
            std::lock_guard<std::mutex> lock(tiered_mtx_);
            auto it = tiered_functions_.find(function_id);
            if (it == tiered_functions_.end() || !it->second) {
                return nullptr;
            }
            state = it->second;
        }

        if (state->tier2_ready.load(std::memory_order_acquire) &&
            state->active_tier.load(std::memory_order_acquire) != TierKind::Tier2_Optimized) {
            (void)try_osr_switch(function_id);
        }
        return state->active_fn.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool bailout_to_tier1(uint64_t function_id,
                                        dgtl::jit::DeoptReason reason,
                                        void* context = nullptr) {
        std::shared_ptr<TieredFunctionState> state;
        {
            std::lock_guard<std::mutex> lock(tiered_mtx_);
            auto it = tiered_functions_.find(function_id);
            if (it == tiered_functions_.end()) {
                return false;
            }
            state = it->second;
        }

        const auto tier1_fn = state->tier1_fn.load(std::memory_order_acquire);
        if (!tier1_fn) {
            return false;
        }

        state->active_fn.store(tier1_fn, std::memory_order_release);
        state->active_tier.store(TierKind::Tier1_Baseline, std::memory_order_release);
        state->stats.deopt_bailouts++;

        (void)deopt_handler_.handle_deopt(static_cast<uint32_t>(function_id), reason, context);
        return true;
    }

    [[nodiscard]] bool trigger_deopt_if_guard_failed(uint64_t function_id,
                                                     bool guard_ok,
                                                     dgtl::jit::DeoptReason reason,
                                                     void* context = nullptr) {
        if (guard_ok) {
            return false;
        }
        return bailout_to_tier1(function_id, reason, context);
    }

    [[nodiscard]] const StackMapRecord* find_stack_map(uint64_t function_id, uint32_t safepoint_id) const {
        std::lock_guard<std::mutex> lock(tiered_mtx_);
        auto it = tiered_functions_.find(function_id);
        if (it == tiered_functions_.end() || !it->second) {
            return nullptr;
        }
        return it->second->stack_maps.find(safepoint_id);
    }

    [[nodiscard]] Phase3Stats get_phase3_stats(uint64_t function_id) const {
        std::lock_guard<std::mutex> lock(tiered_mtx_);
        auto it = tiered_functions_.find(function_id);
        if (it == tiered_functions_.end() || !it->second) {
            return {};
        }
        return it->second->stats;
    }

private:
    void start_tier2_worker() {
        tier2_stop_.store(false, std::memory_order_release);
        tier2_worker_ = std::thread([this]() { tier2_worker_loop(); });
    }

    void stop_tier2_worker() {
        tier2_stop_.store(true, std::memory_order_release);
        tier2_cv_.notify_all();
        if (tier2_worker_.joinable()) {
            tier2_worker_.join();
        }
    }

    [[nodiscard]] bool compile_tier1_baseline(const dgtl::ir::IRStream& stream,
                                              std::vector<uint8_t>& out,
                                              StackMapTable& maps,
                                              double& out_ms,
                                              const std::unordered_set<uint32_t>* pointer_virtuals) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        out.clear();
        out.reserve(stream.size() * 8 + 128);

        // Greedy linear scan mapping over virtual IDs in one forward pass.
        std::unordered_map<uint32_t, dgtl::exec::X64Reg> virtual_to_reg;
        uint32_t safepoint_id = 1;

        for (size_t i = 0; i < stream.size(); ++i) {
            const dgtl::ir::IRNode node = stream.get_node_readonly(i);

            auto map_vreg = [&](uint32_t v) {
                if (v == dgtl::ir::INVALID_SLOT) {
                    return;
                }
                if (virtual_to_reg.find(v) == virtual_to_reg.end()) {
                    virtual_to_reg.emplace(v, dgtl::exec::X64SinglePassRegisterMapper::map_gpr_branchless(v));
                }
            };

            map_vreg(node.result_id);
            map_vreg(node.op_a);
            map_vreg(node.op_b);
            map_vreg(node.op_c);

            const bool is_safepoint =
                (node.logic_flags & dgtl::ir::ExtendedFlags::DEOPT_SAFEPOINT) != 0 ||
                (node.flags & dgtl::ir::Flags::MAY_DEOPT) != 0 ||
                node.opcode == dgtl::ir::OpCode::CALL ||
                node.opcode == dgtl::ir::OpCode::SYSCALL ||
                node.opcode == dgtl::ir::OpCode::DEOPT_TRIGGER;

            if (is_safepoint) {
                StackMapRecord rec{};
                rec.safepoint_id = safepoint_id++;
                rec.ir_node_index = static_cast<uint32_t>(i);
                rec.code_offset = static_cast<uint32_t>(out.size());

                auto append_ptr = [&](uint32_t v) {
                    if (v == dgtl::ir::INVALID_SLOT) {
                        return;
                    }
                    const bool explicit_pointer = pointer_virtuals && (pointer_virtuals->find(v) != pointer_virtuals->end());
                    const bool pointer_by_opcode =
                        (node.opcode == dgtl::ir::OpCode::LOAD) ||
                        (node.opcode == dgtl::ir::OpCode::STORE) ||
                        (node.opcode == dgtl::ir::OpCode::CALL);
                    if (!explicit_pointer && !pointer_by_opcode) {
                        return;
                    }
                    auto it = virtual_to_reg.find(v);
                    if (it != virtual_to_reg.end()) {
                        rec.live_pointers.push_back(
                            PointerLocation{PointerLocationKind::Register, static_cast<uint32_t>(it->second)});
                    } else {
                        rec.live_pointers.push_back(PointerLocation{PointerLocationKind::StackSlot, v});
                    }
                };

                append_ptr(node.op_a);
                append_ptr(node.op_b);
                append_ptr(node.op_c);
                maps.add_record(std::move(rec));
            }
        }

        if (!emitter.process_stream(stream, out) || out.empty()) {
            return false;
        }

        const auto t1 = std::chrono::high_resolution_clock::now();
        out_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return true;
    }

    void tier2_worker_loop() {
        while (true) {
            uint64_t function_id = 0;
            std::shared_ptr<dgtl::ir::IRStream> stream_ptr;

            {
                std::unique_lock<std::mutex> lock(tiered_mtx_);
                tier2_cv_.wait(lock, [this]() {
                    return tier2_stop_.load(std::memory_order_acquire) || !tier2_queue_.empty();
                });

                if (tier2_stop_.load(std::memory_order_acquire) && tier2_queue_.empty()) {
                    return;
                }

                function_id = tier2_queue_.front();
                tier2_queue_.pop();
                auto it = tier2_streams_.find(function_id);
                if (it != tier2_streams_.end()) {
                    stream_ptr = it->second;
                    tier2_streams_.erase(it);
                }
            }

            if (!stream_ptr || stream_ptr->is_null() || stream_ptr->size() == 0) {
                continue;
            }

            dgtl::ir::IRStream optimized = stream_ptr->clone();
            const auto t0 = std::chrono::high_resolution_clock::now();

            // Tier-2 heavy optimizing path in background.
            dgtl::pass::transform::DGTLLoopFusion loop_fusion_pass{};
            loop_fusion_pass.execute(optimized);
            dgtl::pass::transform::GlobalValueNumbering().execute(optimized);
            dgtl::pass::transform::DeadCodeElimination().execute(optimized);
            (void)cost_model.predict_cost(optimized);

            // Chaitin-Briggs style graph-coloring allocation pass.
            const auto alloc = tier2_regalloc_.allocate(optimized);
            if (!alloc.spilled_virtuals.empty()) {
                for (size_t i = 0; i < optimized.size(); ++i) {
                    auto node = optimized.get_node(i);
                    if (std::find(alloc.spilled_virtuals.begin(), alloc.spilled_virtuals.end(), node.result_id)
                        != alloc.spilled_virtuals.end()) {
                        node.flags |= dgtl::ir::HintFlags::IsSpillCandidate;
                        node.refresh_intent_hash();
                        optimized.set_node(i, node);
                    }
                }
            }

            std::vector<uint8_t> tier2_code;
            const bool emit_ok = emitter.process_stream(optimized, tier2_code);
            auto tier2_fn = emit_ok ? executor.load_and_prepare(tier2_code, optimized) : nullptr;

            const auto t1 = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::lock_guard<std::mutex> lock(tiered_mtx_);
            auto st_it = tiered_functions_.find(function_id);
            if (st_it == tiered_functions_.end() || !st_it->second) {
                continue;
            }
            if (tier2_fn) {
                st_it->second->tier2_fn.store(tier2_fn, std::memory_order_release);
                st_it->second->tier2_ready.store(true, std::memory_order_release);
                st_it->second->stats.tier2_compile_ms = ms;
            }
        }
    }

public:

    [[nodiscard]] uintptr_t compile_to_address(dgtl::ir::IRStream& stream) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return 0;
        machine_code_buffer.clear();
        if (!emitter.process_stream(stream, machine_code_buffer) || machine_code_buffer.empty()) return 0;
        if (strike_runner.inject_code(arena, machine_code_buffer) == 0) return 0;
        return strike_runner.last_injected_address();
    }

    void hard_reset() {
        arena.reset_all();
        machine_code_buffer.clear();
        if (machine_code_buffer.capacity() > (1024 * 1024 * 32)) {
            machine_code_buffer.shrink_to_fit();
            machine_code_buffer.reserve(1024 * 1024 * 16);
        }
        global_symbol_table.clear();
        buffer_symbol_offsets.clear();
        pending_patches.clear();
        register_bulletins(); // Re-populate builtins after reset
    }

    [[nodiscard]] size_t get_last_code_size() const noexcept { return machine_code_buffer.size(); }
    [[nodiscard]] uint32_t get_stack_frame_requirement() const noexcept { return 0; }
};

} // namespace dgtl::jit
