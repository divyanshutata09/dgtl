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
/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * JIT ORCHESTRATOR & TIERED PIPELINE ENGINE
 * FULL SYNCHRONIZATION: Aligned with 178 IR OpCodes, Polyhedral Scope Tiling, & Cross-Platform W^X Enforcement.
 */

module;

#include <cstdlib>
#include <iostream>

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
    #include <sys/mman.h>
#endif

#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <memory>
#include <numeric>
#include <array>
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
#include <span>

// --- APPLE SILICON HARDENED RUNTIME ---
#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #ifndef MAP_
        #define MAP_ 0x0800
    #endif
#endif

export module dgtl.ir_core.jit_orchestrator;

// Runtime intrinsic facade dependency anchor: include/dgtl/runtime/Runtime_Intrinsics.ixx
import dgtl.runtime_intrinsics;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.arena;
import dgtl.ir_core.stream;
import dgtl.ir_core.ai_cost_model;
import dgtl.compiler_context;
import dgtl.target;
import dgtl.ir_core.stack_spiller;
import dgtl.ir_core.backend_driver_api;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.transform_poly_loop;

import dgtl.ir_core.jit_executor;
import dgtl.ir_core.async_dopt_tiered;
import dgtl.ir_core.hardware_scheduler;
import dgtl.ir_core.x64_lowering;
import dgtl.ir_core.x64_reg_map;
import dgtl.ir_core.x64_graphcoloring_regalloc;
import dgtl.ir_core.linker_backend;

// Forward Declarations for specialized backend emitters
export namespace dgtl::exec {
    class DGTLMachOEmitter; 
    class DGTLWasmEncoder;  
}

export namespace dgtl::jit {

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
        std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) {
            return a.hotness_score > b.hotness_score;
        });

        std::vector<uint8_t> final_blob;
        const size_t total_bytes = std::accumulate(segments.begin(), segments.end(), size_t{0},
            [](size_t sum, const auto& seg) noexcept {
                return sum + seg.machine_code.size();
            });
        final_blob.reserve(total_bytes + CPU_CACHE_LINE);

        for (auto& seg : segments) {
            // 2. Cache-Line Alignment
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
            patch_relative_jumps(seg);

            // 4. Final Insertion Strike
            final_blob.insert(final_blob.end(), seg.machine_code.begin(), seg.machine_code.end());
        }

        return final_blob;
    }

private:
    static void inject_optimized_nops(std::vector<uint8_t>& blob, size_t count) {
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
                blob.push_back(0x90);
                count--;
            }
        }
    }

    static void patch_relative_jumps(BinarySegment& seg) {
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

/**
 * @struct CompilationReport
 */
struct CompilationReport {
    double total_ms = 0.0;
    size_t nodes_processed = 0;
    double throughput_nodes_sec = 0.0;
    bool validation_opted = false;
    std::string target_arch_used; 
};

struct EmitUtilizationMetrics {
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    uint64_t dispatcher_routes_x64 = 0;
    uint64_t dispatcher_routes_arm64 = 0;
    uint64_t dispatcher_routes_wasm = 0;
    uint64_t dispatcher_failures = 0;
};

[[nodiscard]] EmitUtilizationMetrics get__emit_utilization_metrics() noexcept;
void reset__emit_utilization_metrics() noexcept;

/**
 * @brief Stage J7 Symbol Binding Engine: Map host C runtime print intrinsics to JIT session symbols.
 */
inline void map_core_symbols(auto& session) {
    session.map_symbol("__dgtl_rt_println_i64", reinterpret_cast<uint64_t>(&__dgtl_rt_println_i64));
    session.map_symbol("__dgtl_rt_print_i64", reinterpret_cast<uint64_t>(&__dgtl_rt_print_i64));
    session.map_symbol("__dgtl_rt_println_f64", reinterpret_cast<uint64_t>(&__dgtl_rt_println_f64));
    session.map_symbol("__dgtl_rt_print_f64", reinterpret_cast<uint64_t>(&__dgtl_rt_print_f64));
    session.map_symbol("__dgtl_rt_println_str", reinterpret_cast<uint64_t>(&__dgtl_rt_println_str));
    session.map_symbol("__dgtl_rt_print_str", reinterpret_cast<uint64_t>(&__dgtl_rt_print_str));
    session.map_symbol("__dgtl_rt_println_bool", reinterpret_cast<uint64_t>(&__dgtl_rt_println_bool));
    session.map_symbol("__dgtl_rt_print_bool", reinterpret_cast<uint64_t>(&__dgtl_rt_print_bool));
}

/**
 * @class DGTLOrchestrator
 */
class DGTLOrchestrator {
    CompilationReport last_report;
    dgtl::core::TargetMachine target; 

public:
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
            std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), [](unsigned char ch) noexcept {
                return static_cast<char>(std::tolower(ch));
            });
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

    [[nodiscard]] const CompilationReport& get_last_report() const noexcept {
        return last_report;
    }

    const dgtl::core::TargetMachine& get_target() const noexcept {
        return target;
    }

private:
    [[noreturn]] static void trigger_fatal_trap(const char* code, const char* details) {
        throw std::runtime_error(std::string(code) + ": " + details);
    }

    static constexpr dgtl::core::ArchType select_preferred_x86_arch() noexcept {
        #if defined(__AVX512F__)
            return dgtl::core::ArchType::X86_64_AVX512;
        #else
            return dgtl::core::ArchType::X86_64_AVX2;
        #endif
    }

    void dispatch_to_linker(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_relocatable(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_archive(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_coff_relocatable(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_coff_archive(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_macho(std::string_view path, const dgtl::ir::IRStream& stream);
    void dispatch_to_wasm(std::string_view path, const dgtl::ir::IRStream& stream);
};

struct RelocationPatch {
    size_t buffer_offset{0};         // Byte index in machine_code_buffer of 00 00 00 00
    std::string target_symbol;       // Callee function name
    size_t instruction_end_offset{0};// Byte index IMMEDIATELY AFTER the 4-byte offset
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
        auto it = std::find_if(records.begin(), records.end(), [safepoint_id](const auto& rec) noexcept {
            return rec.safepoint_id == safepoint_id;
        });
        return (it != records.end()) ? &(*it) : nullptr;
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
 */
class StrikeOrchestrator {
    dgtl::core::DGTLContext& ctx;
    dgtl::core::DGTL_Arena& arena;
    dgtl::isa::StackSpiller spiller;   
    dgtl::isa::X64StrikeEmitter emitter;

    // Closed-loop learning components
    dgtl::analysis::DGTLAICostModel cost_model;
    dgtl::runtime::DGTLExecutor executor;
    StrikeRunner strike_runner;
    Deopt_Handler deopt_handler_;
    AsyncSpeculator async_speculator_;
    dgtl::isa::regalloc::X64GraphColoringRegAlloc tier2_regalloc_;

    struct TieredFunctionState {
        std::atomic<dgtl::runtime::Function> tier1_fn{nullptr};
        std::atomic<dgtl::runtime::Function> tier2_fn{nullptr};
        std::atomic<dgtl::runtime::Function> active_fn{nullptr};
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

    // Scratch Buffer
    std::vector<uint8_t> machine_code_buffer;

    std::unordered_map<std::string, uintptr_t> global_symbol_table;
    std::unordered_map<std::string, size_t> buffer_symbol_offsets;
    std::unordered_map<uint32_t, uintptr_t> runtime_slots_;
    std::vector<RelocationPatch> pending_patches;

public:
    void patch_jump(size_t jmp_op_offset, size_t target_offset) {
        int32_t rel32 = static_cast<int32_t>(target_offset - (jmp_op_offset + 5));
        std::memcpy(&machine_code_buffer[jmp_op_offset + 1], &rel32, sizeof(int32_t));
    }

    [[nodiscard]] uintptr_t lookup_global_symbol(const std::string& name) const noexcept {
        auto it = global_symbol_table.find(name);
        return it != global_symbol_table.end() ? it->second : 0;
    }

    static constexpr uint32_t STRIKE_OK = static_cast<uint32_t>(StrikeStatus::Ok);
    static constexpr uint32_t STRIKE_INVALID_STREAM = 0xE001;
    static constexpr uint32_t STRIKE_EMIT_EMPTY = 0xE002;
    static constexpr uint32_t STRIKE_INJECT_FAILED = static_cast<uint32_t>(StrikeStatus::InjectFailed);
    static constexpr uint32_t STRIKE_LINK_FAILED = 0xE003;

    StrikeOrchestrator(dgtl::core::DGTLContext& c, dgtl::core::DGTL_Arena& a)
        : ctx(c), arena(a), spiller(arena),
          executor(128, dgtl::core::select_preferred_x86_arch()),
          async_speculator_(executor, 2) {
        machine_code_buffer.reserve(1024 * 1024 * 16);
        const dgtl::core::TargetMachine target_machine(dgtl::core::select_preferred_x86_arch());
        emitter.set_target_machine(target_machine);
        executor.set_target_machine(target_machine);
        register_bulletins();
    }

    ~StrikeOrchestrator() {
        stop_tier2_worker();
    }

    void enable_deterministic_replay(bool enabled) noexcept {
        async_speculator_.enable_deterministic_replay(enabled);
    }

    [[nodiscard]] std::vector<SpeculationReplayEvent> get_speculation_replay_log() const {
        return async_speculator_.get_replay_log();
    }

    void register_runtime_function(std::string name, uintptr_t address) {
        global_symbol_table[name] = address;
        emitter.register_runtime_function(std::move(name), address);
    }

    void register_runtime_function_slot(uint32_t slot, uintptr_t address) {
        runtime_slots_[slot] = address;
        emitter.register_runtime_function_slot(slot, address);
    }

    /**
     * @brief Register runtime intrinsic symbols into the linker and symbol table.
     */
    void register_bulletins() {
        register_runtime_function("__dgtl_rt_string_length", reinterpret_cast<uintptr_t>(&__dgtl_rt_string_length));
        register_runtime_function("__dgtl_rt_string_compare", reinterpret_cast<uintptr_t>(&__dgtl_rt_string_compare));
        register_runtime_function("__dgtl_rt_string_concat", reinterpret_cast<uintptr_t>(&__dgtl_rt_string_concat));
        register_runtime_function("__dgtl_rt_string_substring", reinterpret_cast<uintptr_t>(&__dgtl_rt_string_substring));

        register_runtime_function("__dgtl_rt_acquire_fence", reinterpret_cast<uintptr_t>(&__dgtl_rt_acquire_fence));
        register_runtime_function("__dgtl_rt_async_await", reinterpret_cast<uintptr_t>(&__dgtl_rt_async_await));
        register_runtime_function("__dgtl_rt_async_launch", reinterpret_cast<uintptr_t>(&__dgtl_rt_async_launch));
        register_runtime_function("__dgtl_rt_async_spawn", reinterpret_cast<uintptr_t>(&__dgtl_rt_async_spawn));
        register_runtime_function("__dgtl_rt_atomic_fence", reinterpret_cast<uintptr_t>(&__dgtl_rt_atomic_fence));
        register_runtime_function("__dgtl_rt_atomic_load", reinterpret_cast<uintptr_t>(&__dgtl_rt_atomic_load));
        register_runtime_function("__dgtl_rt_atomic_snapshot", reinterpret_cast<uintptr_t>(&__dgtl_rt_atomic_snapshot));
        register_runtime_function("__dgtl_rt_atomic_store", reinterpret_cast<uintptr_t>(&__dgtl_rt_atomic_store));
        register_runtime_function("__dgtl_rt_barrier_sync", reinterpret_cast<uintptr_t>(&__dgtl_rt_barrier_sync));
        register_runtime_function("__dgtl_rt_bool_select", reinterpret_cast<uintptr_t>(&__dgtl_rt_bool_select));
        register_runtime_function("__dgtl_rt_select", reinterpret_cast<uintptr_t>(&__dgtl_rt_select));
        register_runtime_function("__dgtl_rt_chan_recv", reinterpret_cast<uintptr_t>(&__dgtl_rt_chan_recv));
        register_runtime_function("__dgtl_rt_chan_send", reinterpret_cast<uintptr_t>(&__dgtl_rt_chan_send));
        register_runtime_function("__dgtl_rt_deopt_trigger", reinterpret_cast<uintptr_t>(&__dgtl_rt_deopt_trigger));
        register_runtime_function("__dgtl_rt_dsa_allocate", reinterpret_cast<uintptr_t>(&__dgtl_rt_dsa_allocate));
        register_runtime_function("__dgtl_rt_dsa_deallocate", reinterpret_cast<uintptr_t>(&__dgtl_rt_dsa_deallocate));
        register_runtime_function("__dgtl_rt_fcvtl_f16_to_f32", reinterpret_cast<uintptr_t>(&__dgtl_rt_fcvtl_f16_to_f32));
        register_runtime_function("__dgtl_rt_fcvtn_f32_to_f16", reinterpret_cast<uintptr_t>(&__dgtl_rt_fcvtn_f32_to_f16));
        register_runtime_function("__dgtl_rt_fiber_yield", reinterpret_cast<uintptr_t>(&__dgtl_rt_fiber_yield));
        register_runtime_function("__dgtl_rt_fma_widen_f16_f32", reinterpret_cast<uintptr_t>(&__dgtl_rt_fma_widen_f16_f32));
        register_runtime_function("__dgtl_rt_gather_16", reinterpret_cast<uintptr_t>(&__dgtl_rt_gather_16));
        register_runtime_function("__dgtl_rt_gpu_launch", reinterpret_cast<uintptr_t>(&__dgtl_rt_gpu_launch));
        register_runtime_function("__dgtl_rt_graph_build", reinterpret_cast<uintptr_t>(&__dgtl_rt_graph_build));
        register_runtime_function("__dgtl_rt_graph_traverse", reinterpret_cast<uintptr_t>(&__dgtl_rt_graph_traverse));
        register_runtime_function("__dgtl_rt_guard_alias", reinterpret_cast<uintptr_t>(&__dgtl_rt_guard_alias));
        register_runtime_function("__dgtl_rt_guard_value", reinterpret_cast<uintptr_t>(&__dgtl_rt_guard_value));
        register_runtime_function("__dgtl_rt_lea", reinterpret_cast<uintptr_t>(&__dgtl_rt_lea));
        register_runtime_function("__dgtl_rt_gep", reinterpret_cast<uintptr_t>(&__dgtl_rt_gep));
        register_runtime_function("__dgtl_rt_ptr_to_int", reinterpret_cast<uintptr_t>(&__dgtl_rt_ptr_to_int));
        register_runtime_function("__dgtl_rt_int_to_ptr", reinterpret_cast<uintptr_t>(&__dgtl_rt_int_to_ptr));
        register_runtime_function("__dgtl_rt_lock_acquire", reinterpret_cast<uintptr_t>(&__dgtl_rt_lock_acquire));
        register_runtime_function("__dgtl_rt_lock_release", reinterpret_cast<uintptr_t>(&__dgtl_rt_lock_release));
        register_runtime_function("__dgtl_rt_memory_fence", reinterpret_cast<uintptr_t>(&__dgtl_rt_memory_fence));
        register_runtime_function("__dgtl_rt_ownership_acquire", reinterpret_cast<uintptr_t>(&__dgtl_rt_ownership_acquire));
        register_runtime_function("__dgtl_rt_ownership_release", reinterpret_cast<uintptr_t>(&__dgtl_rt_ownership_release));
        register_runtime_function("__dgtl_rt_ownership_transfer", reinterpret_cast<uintptr_t>(&__dgtl_rt_ownership_transfer));
        register_runtime_function("__dgtl_rt_phi_multi", reinterpret_cast<uintptr_t>(&__dgtl_rt_phi_multi));
        register_runtime_function("__dgtl_rt_phi_node", reinterpret_cast<uintptr_t>(&__dgtl_rt_phi_node));
        register_runtime_function("__dgtl_rt_prefetch", reinterpret_cast<uintptr_t>(&__dgtl_rt_prefetch));
        register_runtime_function("__dgtl_rt_prefetch_t0", reinterpret_cast<uintptr_t>(&__dgtl_rt_prefetch_t0));
        register_runtime_function("__dgtl_rt_clflush", reinterpret_cast<uintptr_t>(&__dgtl_rt_clflush));
        register_runtime_function("__dgtl_rt_read_tsc", reinterpret_cast<uintptr_t>(&__dgtl_rt_read_tsc));
        register_runtime_function("__dgtl_rt_spin_acquire", reinterpret_cast<uintptr_t>(&__dgtl_rt_spin_acquire));
        register_runtime_function("__dgtl_rt_ring_push_free", reinterpret_cast<uintptr_t>(&__dgtl_rt_ring_push_free));
        register_runtime_function("__dgtl_rt_wasm_invoke", reinterpret_cast<uintptr_t>(&__dgtl_rt_wasm_invoke));
        register_runtime_function("__dgtl_rt_fiber_yield_loop", reinterpret_cast<uintptr_t>(&__dgtl_rt_fiber_yield_loop));
        register_runtime_function("__dgtl_rt_suspend_ctx", reinterpret_cast<uintptr_t>(&__dgtl_rt_suspend_ctx));
        register_runtime_function("__dgtl_rt_resume_ctx", reinterpret_cast<uintptr_t>(&__dgtl_rt_resume_ctx));
        register_runtime_function("__dgtl_rt_neon_vadd_q", reinterpret_cast<uintptr_t>(&__dgtl_rt_neon_vadd_q));
        register_runtime_function("__dgtl_rt_matmul_bf16", reinterpret_cast<uintptr_t>(&__dgtl_rt_matmul_bf16));
        register_runtime_function("__dgtl_rt_matmul_fp8", reinterpret_cast<uintptr_t>(&__dgtl_rt_matmul_fp8));
        register_runtime_function("__dgtl_rt_sdpa_f16", reinterpret_cast<uintptr_t>(&__dgtl_rt_sdpa_f16));
        register_runtime_function("__dgtl_rt_softmax_fast", reinterpret_cast<uintptr_t>(&__dgtl_rt_softmax_fast));
        register_runtime_function("__dgtl_rt_layer_norm", reinterpret_cast<uintptr_t>(&__dgtl_rt_layer_norm));
        register_runtime_function("__dgtl_rt_rope_embed", reinterpret_cast<uintptr_t>(&__dgtl_rt_rope_embed));
        register_runtime_function("__dgtl_rt_fast_inv_sqrt", reinterpret_cast<uintptr_t>(&__dgtl_rt_fast_inv_sqrt));
        register_runtime_function("__dgtl_rt_vec3_cross", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec3_cross));
        register_runtime_function("__dgtl_rt_vec4_dot", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec4_dot));
        register_runtime_function("__dgtl_rt_mat4_mul", reinterpret_cast<uintptr_t>(&__dgtl_rt_mat4_mul));
        register_runtime_function("__dgtl_rt_quat_slerp", reinterpret_cast<uintptr_t>(&__dgtl_rt_quat_slerp));
        register_runtime_function("__dgtl_rt_ray_aabb_isect", reinterpret_cast<uintptr_t>(&__dgtl_rt_ray_aabb_isect));
        register_runtime_function("__dgtl_rt_ptr_diff", reinterpret_cast<uintptr_t>(&__dgtl_rt_ptr_diff));
        register_runtime_function("__dgtl_rt_release_fence", reinterpret_cast<uintptr_t>(&__dgtl_rt_release_fence));
        register_runtime_function("__dgtl_rt_scop_analyze", reinterpret_cast<uintptr_t>(&__dgtl_rt_scop_analyze));
        register_runtime_function("__dgtl_rt_select_multi", reinterpret_cast<uintptr_t>(&__dgtl_rt_select_multi));
        register_runtime_function("__dgtl_rt_simd_bsl_predicate", reinterpret_cast<uintptr_t>(&__dgtl_rt_simd_bsl_predicate));
        register_runtime_function("__dgtl_rt_switch_dispatch", reinterpret_cast<uintptr_t>(&__dgtl_rt_switch_dispatch));
        register_runtime_function("__dgtl_rt_sync_barrier", reinterpret_cast<uintptr_t>(&__dgtl_rt_sync_barrier));
        register_runtime_function("__dgtl_rt_tensor_broadcast", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_broadcast));
        register_runtime_function("__dgtl_rt_tensor_concat", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_concat));
        register_runtime_function("__dgtl_rt_tensor_conv", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_conv));
        register_runtime_function("__dgtl_rt_tensor_gemm", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_gemm));
        register_runtime_function("__dgtl_rt_tensor_pool", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_pool));
        register_runtime_function("__dgtl_rt_tensor_reduce", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_reduce));
        register_runtime_function("__dgtl_rt_tensor_reshape", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_reshape));
        register_runtime_function("__dgtl_rt_tensor_slice", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_slice));
        register_runtime_function("__dgtl_rt_tensor_softmax", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_softmax));
        register_runtime_function("__dgtl_rt_tensor_transpose", reinterpret_cast<uintptr_t>(&__dgtl_rt_tensor_transpose));
        register_runtime_function("__dgtl_rt_thread_spawn", reinterpret_cast<uintptr_t>(&__dgtl_rt_thread_spawn));
        register_runtime_function("__dgtl_rt_tree_build", reinterpret_cast<uintptr_t>(&__dgtl_rt_tree_build));
        register_runtime_function("__dgtl_rt_tree_traverse", reinterpret_cast<uintptr_t>(&__dgtl_rt_tree_traverse));
        register_runtime_function("__dgtl_rt_trie_build", reinterpret_cast<uintptr_t>(&__dgtl_rt_trie_build));
        register_runtime_function("__dgtl_rt_trie_insert", reinterpret_cast<uintptr_t>(&__dgtl_rt_trie_insert));
        register_runtime_function("__dgtl_rt_trie_lookup", reinterpret_cast<uintptr_t>(&__dgtl_rt_trie_lookup));
        register_runtime_function("__dgtl_rt_tuple_pack", reinterpret_cast<uintptr_t>(&__dgtl_rt_tuple_pack));
        register_runtime_function("__dgtl_rt_tuple_unpack", reinterpret_cast<uintptr_t>(&__dgtl_rt_tuple_unpack));
        register_runtime_function("__dgtl_rt_vec_load", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec_load));
        register_runtime_function("__dgtl_rt_vec_reduce_add", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec_reduce_add));
        register_runtime_function("__dgtl_rt_vec_shuffle", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec_shuffle));
        register_runtime_function("__dgtl_rt_vec_store", reinterpret_cast<uintptr_t>(&__dgtl_rt_vec_store));

        // Language-level High-Level Print Intrinsics
        register_runtime_function("__dgtl_rt_println_i64", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_i64));
        register_runtime_function("__dgtl_rt_print_i64", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_i64));
        register_runtime_function("__dgtl_rt_println_f64", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_f64));
        register_runtime_function("__dgtl_rt_print_f64", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_f64));
        register_runtime_function("__dgtl_rt_println_str", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_str));
        register_runtime_function("__dgtl_rt_print_str", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_str));
        register_runtime_function("__dgtl_rt_println_bool", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_bool));
        register_runtime_function("__dgtl_rt_print_bool", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_bool));

        // Friendly aliases
        register_runtime_function("println_i64", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_i64));
        register_runtime_function("print_i64", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_i64));
        register_runtime_function("println_f64", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_f64));
        register_runtime_function("print_f64", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_f64));
        register_runtime_function("println_str", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_str));
        register_runtime_function("print_str", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_str));
        register_runtime_function("println_bool", reinterpret_cast<uintptr_t>(&__dgtl_rt_println_bool));
        register_runtime_function("print_bool", reinterpret_cast<uintptr_t>(&__dgtl_rt_print_bool));
    }

    /**
     * @brief Relocation Linker (Module-Level Strike) with verified cross-module symbol resolution.
     */
    StrikeMetrics fire_module_strike(const std::vector<std::pair<std::string, dgtl::ir::IRStream>>& module_streams) {
        machine_code_buffer.clear();
        buffer_symbol_offsets.clear();
        pending_patches.clear();

        // 1. LINEAR EMISSION PHASE & SYMBOL GATHERING
        for (const auto& [name, stream] : module_streams) {
            if (stream.is_null() || stream.size() == 0) continue;

            buffer_symbol_offsets[name] = machine_code_buffer.size();

            const bool emit_ok = emitter.process_stream(stream, machine_code_buffer);
            if (!emit_ok) {
                return {STRIKE_EMIT_EMPTY};
            }
        }

        // 2. INJECTION PHASE
        size_t injected_size = strike_runner.inject_code(arena, machine_code_buffer);
        if (injected_size == 0) {
            return {STRIKE_INJECT_FAILED};
        }

        uintptr_t module_base_address = strike_runner.last_injected_address();

        for (const auto& [name, offset] : buffer_symbol_offsets) {
            global_symbol_table[name] = module_base_address + offset;
        }

        // 3. SECURE BACKPATCH PHASE
        for (const auto& patch : pending_patches) {
            auto it = global_symbol_table.find(patch.target_symbol);
            if (it == global_symbol_table.end()) {
                std::fprintf(stderr, "[DGTL Linker] Fatal: Unresolved external symbol: %s\n", patch.target_symbol.c_str());
                return {STRIKE_LINK_FAILED};
            }

            uintptr_t target_address = it->second;
            uintptr_t instruction_end_addr = module_base_address + patch.instruction_end_offset;
            uintptr_t patch_ptr = module_base_address + patch.buffer_offset;

            int32_t relative_offset = static_cast<int32_t>(target_address - instruction_end_addr);

            // W^X Relocation Protection
#if defined(_WIN32) || defined(_WIN64)
            DWORD old_protect = 0;
            VirtualProtect(reinterpret_cast<void*>(patch_ptr), sizeof(int32_t), PAGE_READWRITE, &old_protect);
            std::memcpy(reinterpret_cast<void*>(patch_ptr), &relative_offset, sizeof(int32_t));
            VirtualProtect(reinterpret_cast<void*>(patch_ptr), sizeof(int32_t), old_protect, &old_protect);
#else
            uintptr_t page_addr = patch_ptr & ~0xFFFULL;
            mprotect(reinterpret_cast<void*>(page_addr), 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
            std::memcpy(reinterpret_cast<void*>(patch_ptr), &relative_offset, sizeof(int32_t));
            mprotect(reinterpret_cast<void*>(page_addr), 4096, PROT_READ | PROT_EXEC);
#endif
        }

        // 4. EXECUTION PHASE
        auto main_it = global_symbol_table.find("main");
        if (main_it != global_symbol_table.end()) {
            auto metrics = strike_runner.execute(arena, injected_size, nullptr); 
            if (metrics.status_code == static_cast<uint32_t>(StrikeStatus::Ok)) {
                metrics.status_code = STRIKE_OK;
            }
            return metrics;
        }

        return {STRIKE_OK};
    }

    /**
     * @brief Single-Stream Strike.
     */
    StrikeMetrics fire_strike(dgtl::ir::IRStream& stream, void* data_payload = nullptr) {
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

        if (metrics.status_code == static_cast<uint32_t>(StrikeStatus::Ok)) {
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

        dgtl::runtime::Function tier1_fn = executor.load_and_prepare(tier1_code, stream);
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
                ensure_tier2_worker_started();
                tier2_streams_[id] = std::make_shared<dgtl::ir::IRStream>(stream.clone());
                tier2_queue_.push(id);
            }
        }
        if (queue_tier2) {
            tier2_cv_.notify_one();
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
        std::for_each(bytecode.begin(), bytecode.end(), [&stream](const auto& node) noexcept {
            stream.push_back(node);
        });
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

    [[nodiscard]] dgtl::runtime::Function maybe_osr_poll(uint64_t function_id,
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
                                        DeoptReason reason,
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
                                                     DeoptReason reason,
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
    std::once_flag tier2_started_flag_{};

    void ensure_tier2_worker_started() {
        std::call_once(tier2_started_flag_, [this]() {
            start_tier2_worker();
        });
    }

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

        std::unordered_map<uint32_t, dgtl::exec::X64Reg> virtual_to_reg;
        uint32_t safepoint_id = 1;

        for (size_t i = 0; i < stream.size(); ++i) {
            const dgtl::ir::IRNode node = stream.get_node_readonly(i);

            auto map_vreg = [&](uint32_t v) {
                if (v != dgtl::ir::INVALID_SLOT) {
                    virtual_to_reg.try_emplace(v, dgtl::exec::X64SinglePassRegisterMapper::map_gpr_branchless(v));
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

            dgtl::pass::transform::DGTLLoopFusion loop_fusion_pass{};
            loop_fusion_pass.execute(optimized);
            dgtl::pass::transform::GlobalValueNumbering().execute(optimized);
            dgtl::pass::transform::DeadCodeElimination().execute(optimized);
            float predicted_tier2_cost = 0.0f;
            for (size_t i = 0; i < optimized.size(); ++i) {
                predicted_tier2_cost += cost_model.predict_node_cost(optimized.get_node_readonly(i));
            }
            (void)predicted_tier2_cost;

            const auto alloc = tier2_regalloc_.allocate(optimized);
            if (!alloc.spilled_virtuals.empty()) {
                for (size_t i = 0; i < optimized.size(); ++i) {
                    auto node = optimized.get_node(i);
                    if (std::find(alloc.spilled_virtuals.begin(), alloc.spilled_virtuals.end(), node.result_id)
                        != alloc.spilled_virtuals.end()) {
                        node.logic_flags |= dgtl::ir::HintFlags::IsSpillCandidate;
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
        
        const bool emit_ok = emitter.process_stream(stream, machine_code_buffer);
        if (!emit_ok || machine_code_buffer.empty()) return 0;
        
        size_t injected = strike_runner.inject_code(arena, machine_code_buffer);
        if (injected == 0) return 0;
        
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
        runtime_slots_.clear();
        buffer_symbol_offsets.clear();
        pending_patches.clear();
        register_bulletins();
    }

    [[nodiscard]] size_t get_last_code_size() const noexcept { return machine_code_buffer.size(); }
    [[nodiscard]] size_t get_label_offset(uint32_t label_id) const noexcept { return emitter.get_label_offset(label_id); }
    [[nodiscard]] static uint32_t get_stack_frame_requirement() noexcept { return 0; }
};

namespace verifier {

constexpr uint32_t kVerifierIpcAbiVersion = 1;
constexpr size_t kVerifierMaxPayloadBytes = 256 * 1024;
constexpr size_t kVerifierResultBytes = 512;

enum class SharedState : uint32_t {
    Idle = 0,
    RequestReady = 1,
    WorkerBusy = 2,
    ResultReady = 3,
    WorkerFaulted = 4,
};

enum class VerifyRc : uint32_t {
    Ok = 0,
    IrInvalid = 1,
    Timeout = 2,
    WorkerCrash = 3,
    ProtocolError = 4,
};

enum class CrashReason : uint32_t {
    None = 0,
    AccessViolation = 1,
    Terminated = 2,
    Timeout = 3,
    ProtocolCorruption = 4,
};

struct alignas(64) SharedHeader final {
    uint32_t abi_version = kVerifierIpcAbiVersion;
    uint32_t header_bytes = static_cast<uint32_t>(sizeof(SharedHeader));
    std::atomic<uint32_t> state{static_cast<uint32_t>(SharedState::Idle)};
    std::atomic<uint32_t> request_seq{0};
    std::atomic<uint32_t> response_seq{0};
    uint32_t payload_bytes = 0;
    uint32_t result_code = static_cast<uint32_t>(VerifyRc::ProtocolError);
    uint32_t crash_reason = static_cast<uint32_t>(CrashReason::None);
    uint32_t reserved = 0;
    uint64_t producer_tsc = 0;
    uint64_t consumer_tsc = 0;
};

struct alignas(64) SharedRegion final {
    SharedHeader header;
    alignas(64) std::array<uint8_t, kVerifierMaxPayloadBytes> payload{};
    alignas(64) std::array<char, kVerifierResultBytes> result_text{};

    [[nodiscard]] std::span<const uint8_t> get_payload_span() const noexcept {
        return std::span<const uint8_t>(payload.data(), std::min<size_t>(header.payload_bytes, payload.size()));
    }
    [[nodiscard]] std::span<uint8_t> get_payload_span_mut() noexcept {
        return std::span<uint8_t>(payload.data(), payload.size());
    }
    [[nodiscard]] std::string_view get_result_text() const noexcept {
        return std::string_view(result_text.data(), strnlen(result_text.data(), kVerifierResultBytes));
    }
};

struct WorkerLaunchSpec final {
    std::string worker_exe;
    std::string mapping_name;
    std::string request_event_name;
    std::string response_event_name;
    uint32_t verify_timeout_us = 750;
    uint32_t boot_timeout_ms = 250;
};

struct VerifyRequest final {
    const uint8_t* bytes = nullptr;
    size_t byte_count = 0;
};

struct VerifyResult final {
    VerifyRc rc = VerifyRc::ProtocolError;
    CrashReason crash_reason = CrashReason::None;
    uint32_t worker_exit_code = 0;
    uint32_t latency_us = 0;
    std::string message;
};

class PersistentVerifierDaemon {
public:
    PersistentVerifierDaemon() = default;
    ~PersistentVerifierDaemon() {
        stop();
    }

    bool start(const WorkerLaunchSpec& spec);
    void stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] VerifyResult verify(const VerifyRequest& request);

    void schedule_restart_async();

private:
#if defined(_WIN32) || defined(_WIN64)
    void* mapping_handle_ = nullptr;
    void* mapped_view_ = nullptr;
    void* request_event_ = nullptr;
    void* response_event_ = nullptr;
    void* worker_process_ = nullptr;
    void* worker_thread_ = nullptr;
#endif
    SharedRegion* shared_ = nullptr;
    WorkerLaunchSpec spec_{};
    std::atomic<bool> restart_pending_{false};
    std::atomic<bool> running_{false};
    std::mutex lifecycle_mtx_;
};

inline bool PersistentVerifierDaemon::start(const WorkerLaunchSpec& spec) {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    stop();
    spec_ = spec;

    if (spec_.worker_exe.empty() || spec_.mapping_name.empty() ||
        spec_.request_event_name.empty() || spec_.response_event_name.empty()) {
        running_.store(false, std::memory_order_release);
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        static_cast<DWORD>(sizeof(SharedRegion)),
                                        spec_.mapping_name.c_str());
    if (mapping == nullptr) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedRegion));
    if (view == nullptr) {
        CloseHandle(mapping);
        running_.store(false, std::memory_order_release);
        return false;
    }

    HANDLE req_evt = CreateEventA(nullptr, FALSE, FALSE, spec_.request_event_name.c_str());
    HANDLE rsp_evt = CreateEventA(nullptr, FALSE, FALSE, spec_.response_event_name.c_str());
    if (req_evt == nullptr || rsp_evt == nullptr) {
        if (rsp_evt != nullptr) {
            CloseHandle(rsp_evt);
        }
        if (req_evt != nullptr) {
            CloseHandle(req_evt);
        }
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        running_.store(false, std::memory_order_release);
        return false;
    }

    std::string cmd = "\"" + spec_.worker_exe + "\""
        + " --verifier-worker"
        + " --mapping \"" + spec_.mapping_name + "\""
        + " --request-event \"" + spec_.request_event_name + "\""
        + " --response-event \"" + spec_.response_event_name + "\"";
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr,
                        cmdline.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &si,
                        &pi)) {
        CloseHandle(rsp_evt);
        CloseHandle(req_evt);
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        running_.store(false, std::memory_order_release);
        return false;
    }

    mapping_handle_ = mapping;
    mapped_view_ = view;
    request_event_ = req_evt;
    response_event_ = rsp_evt;
    worker_process_ = pi.hProcess;
    worker_thread_ = pi.hThread;
    shared_ = reinterpret_cast<SharedRegion*>(view);

    std::memset(static_cast<void*>(shared_), 0, sizeof(SharedRegion));
    shared_->header.abi_version = kVerifierIpcAbiVersion;
    shared_->header.header_bytes = static_cast<uint32_t>(sizeof(SharedHeader));
    shared_->header.state.store(static_cast<uint32_t>(SharedState::Idle), std::memory_order_release);
    shared_->header.request_seq.store(0, std::memory_order_release);
    shared_->header.response_seq.store(0, std::memory_order_release);

    running_.store(true, std::memory_order_release);
    return true;
#else
    (void)spec_;
    running_.store(false, std::memory_order_release);
    return false;
#endif
}

inline void PersistentVerifierDaemon::stop() noexcept {
    running_.store(false, std::memory_order_release);

#if defined(_WIN32) || defined(_WIN64)
    if (worker_thread_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(worker_thread_));
        worker_thread_ = nullptr;
    }

    if (worker_process_ != nullptr) {
        HANDLE proc = reinterpret_cast<HANDLE>(worker_process_);
        const DWORD status = WaitForSingleObject(proc, 0);
        if (status == WAIT_TIMEOUT) {
            TerminateProcess(proc, 0xE10Fu);
            WaitForSingleObject(proc, 5);
        }
        CloseHandle(proc);
        worker_process_ = nullptr;
    }

    if (response_event_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(response_event_));
        response_event_ = nullptr;
    }
    if (request_event_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(request_event_));
        request_event_ = nullptr;
    }

    if (mapped_view_ != nullptr) {
        UnmapViewOfFile(mapped_view_);
        mapped_view_ = nullptr;
    }
    if (mapping_handle_ != nullptr) {
        CloseHandle(reinterpret_cast<HANDLE>(mapping_handle_));
        mapping_handle_ = nullptr;
    }
#endif

    shared_ = nullptr;
}

inline bool PersistentVerifierDaemon::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

inline VerifyResult PersistentVerifierDaemon::verify(const VerifyRequest& request) {
    VerifyResult out{};

    if (!request.bytes || request.byte_count == 0 || request.byte_count > kVerifierMaxPayloadBytes) {
        out.rc = VerifyRc::ProtocolError;
        out.message = "invalid verifier request payload";
        return out;
    }

    if (!is_running() || shared_ == nullptr) {
        out.rc = VerifyRc::WorkerCrash;
        out.crash_reason = CrashReason::Terminated;
        out.message = "verifier worker not running";
        return out;
    }

    const auto t0 = std::chrono::high_resolution_clock::now();

#if defined(_WIN32) || defined(_WIN64)
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (!running_.load(std::memory_order_acquire)) {
        out.rc = VerifyRc::WorkerCrash;
        out.crash_reason = CrashReason::Terminated;
        out.message = "verifier worker became unavailable";
        return out;
    }

    auto* hdr = &shared_->header;
    const uint32_t req_seq = hdr->request_seq.load(std::memory_order_acquire) + 1;
    std::memcpy(shared_->payload.data(), request.bytes, request.byte_count);
    hdr->payload_bytes = static_cast<uint32_t>(request.byte_count);
    hdr->result_code = static_cast<uint32_t>(VerifyRc::ProtocolError);
    hdr->crash_reason = static_cast<uint32_t>(CrashReason::None);
    hdr->request_seq.store(req_seq, std::memory_order_release);
    hdr->state.store(static_cast<uint32_t>(SharedState::RequestReady), std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    if (!SetEvent(reinterpret_cast<HANDLE>(request_event_))) {
        out.rc = VerifyRc::WorkerCrash;
        out.crash_reason = CrashReason::Terminated;
        out.message = "failed to signal verifier request event";
        running_.store(false, std::memory_order_release);
        return out;
    }

    const auto deadline = t0 + std::chrono::microseconds(spec_.verify_timeout_us);
    bool response_signaled = false;
    while (std::chrono::high_resolution_clock::now() < deadline) {
        if (WaitForSingleObject(reinterpret_cast<HANDLE>(response_event_), 0) == WAIT_OBJECT_0) {
            response_signaled = true;
            break;
        }

        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(worker_process_), &exit_code) ||
            exit_code != STILL_ACTIVE) {
            out.rc = VerifyRc::WorkerCrash;
            out.crash_reason = CrashReason::Terminated;
            out.worker_exit_code = (exit_code == STILL_ACTIVE) ? 0u : static_cast<uint32_t>(exit_code);
            out.message = "verifier worker exited during request";
            running_.store(false, std::memory_order_release);
            const auto t1 = std::chrono::high_resolution_clock::now();
            out.latency_us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
            return out;
        }

        std::this_thread::yield();
    }

    if (!response_signaled) {
        out.rc = VerifyRc::Timeout;
        out.crash_reason = CrashReason::Timeout;
        out.message = "verifier timeout";
        const auto t1 = std::chrono::high_resolution_clock::now();
        out.latency_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        return out;
    }

    const uint32_t rsp_seq = hdr->response_seq.load(std::memory_order_acquire);
    if (rsp_seq != req_seq) {
        out.rc = VerifyRc::ProtocolError;
        out.crash_reason = CrashReason::ProtocolCorruption;
        out.message = "verifier response sequence mismatch";
    } else {
        out.rc = static_cast<VerifyRc>(hdr->result_code);
        out.crash_reason = static_cast<CrashReason>(hdr->crash_reason);
        out.message.assign(shared_->result_text.data(), strnlen(shared_->result_text.data(), shared_->result_text.size()));
    }
#else
    out.rc = VerifyRc::ProtocolError;
    out.message = "oop verifier is currently only implemented for win32";
#endif

    const auto t1 = std::chrono::high_resolution_clock::now();
    out.latency_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    return out;
}

inline void PersistentVerifierDaemon::schedule_restart_async() {
    bool expected = false;
    if (!restart_pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    std::thread([this]() {
        WorkerLaunchSpec spec_copy;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mtx_);
            spec_copy = spec_;
        }
        stop();
        (void)start(spec_copy);
        restart_pending_.store(false, std::memory_order_release);
    }).detach();
}

} // namespace verifier

} // namespace dgtl::jit