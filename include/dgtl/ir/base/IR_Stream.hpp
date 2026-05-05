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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 3.0 (TITAN-STABLE)
 * PILLAR 4: IR STREAM CORE IMPLEMENTATION WITH ARENA-BACKED SOA
 * DESIGN: 64-Byte Aligned Transactions for HFT-Grade Optimization Stability
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once

#include "IR_Types.hpp"
#include "IR_Nodes.hpp"
#include "IR_Traits.hpp"
#include "IR_Storage.hpp"
#include "dgtl/core/Arena.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace dgtl::ir {

// ============================================================================
// FORWARD DECLARATIONS - Backend Profiling & Hybrid Execution (NEW - DGTL v1.0)
// ============================================================================

/**
 * @brief Backend Profile - Tracking per-target compilation metrics
 * Enables adaptive compilation tier selection and cross-platform optimization
 */
struct BackendProfile {
    enum class TargetISA : uint8_t {
        X64_AVX512 = 0,
        ARM64_SVE = 1,
        ARM32_NEON = 2,
        RISCV64_RV64GC = 3,
        WASM = 4,
        CUDA_PTX = 5,
        ROCM_LLVMIR = 6,
        NASM_X64 = 7,
        FPGA_VERILOG = 8
    };

    enum class CompilationTier : uint8_t {
        JIT_FAST = 0,
        AOT_INTENSIVE = 1,
        ADAPTIVE_HYBRID = 2
    };

    TargetISA target = TargetISA::X64_AVX512;
    uint64_t execution_time_us = 0;
    uint64_t calls_count = 0;
    uint32_t cache_hits = 0;
    uint32_t cache_misses = 0;
    float avg_compilation_latency_us = 0.0f;
    CompilationTier current_tier = CompilationTier::JIT_FAST;
    uint32_t tier_switch_count = 0;

    struct SpeculationMetrics {
        uint32_t speculations_made = 0;
        uint32_t speculations_verified = 0;
        uint32_t deopt_triggers = 0;
        float success_rate() const noexcept {
            if (speculations_made == 0) return 0.0f;
            return 100.0f * speculations_verified / speculations_made;
        }
    } speculation;

    struct LoopMetrics {
        uint32_t loops_detected = 0;
        uint32_t loops_unrolled = 0;
        uint64_t total_iterations = 0;
        float avg_trip_count = 0.0f;
    } loops;

    struct SIMDMetrics {
        uint32_t vectorizable_ops = 0;
        uint32_t vectorized_ops = 0;
        uint32_t gather_ops = 0;
        uint32_t scatter_ops = 0;
        float utilization_percent = 0.0f;
    } simd;
};

/**
 * @brief Profiling Data - Runtime execution profile for PGO
 */
struct ProfilingData {
    struct BlockMetrics {
        uint64_t execution_count = 0;
        float hotness_score = 0.0f;
    };
    
    std::unordered_map<uint32_t, BlockMetrics> block_metrics;
    
    void record_block_execution(uint32_t block_id) noexcept {
        block_metrics[block_id].execution_count++;
    }
    
    float compute_hotness_score(uint32_t block_id) const noexcept {
        auto it = block_metrics.find(block_id);
        if (it == block_metrics.end()) return 0.0f;
        
        static thread_local uint64_t max_count = 1;
        max_count = std::max(max_count, it->second.execution_count);
        return static_cast<float>(it->second.execution_count) / max_count;
    }
};

// ======================================================
// 🔹 IR STREAM
// ======================================================
class alignas(64) IRStream {
private:
    size_t count = 0;
    size_t _capacity = 0;

    void refresh_joined_buffer_locked(bool trans) const noexcept;

    // Guaranteed 64-byte aligned pointers for SoA (Structure of Arrays)
    // Backed by dgtl::core::ThreadLocalArena for deterministic performance
    IRAtom* atoms = nullptr;
    IRCore* cores = nullptr;

    // Shadow buffers for transactional mutation (Pillar 2)
    IRAtom* shadow_atoms = nullptr;
    IRCore* shadow_cores = nullptr;

    size_t shadow_count = 0;

    // Atomic transaction state with release/acquire semantics
    std::atomic<bool> in_transaction{false};

    // FIX: track transaction state for joined buffer proxy
    mutable bool joined_buffer_in_transaction = false;

    // AoS fallback/proxy buffer
    mutable IRNode* joined_buffer = nullptr;
    mutable bool joined_valid = false;
    mutable IRNode fallback_node{};
    mutable std::recursive_mutex stream_mutex;

    // Memory Ownership Flags
    bool owns_aligned_alloc_storage = false;
    bool owns_joined_buffer = false;

    // ========================================================================
    // NEW: FRONTEND CONTEXT & LANGUAGE SUPPORT
    // ========================================================================
    FrontendLanguage frontend_lang = FrontendLanguage::UNIVERSAL;
    TypeContext default_type_ctx;
    std::unordered_map<uint32_t, OwnershipLifetime> ownership_tracking;  // Node ID -> ownership info
    std::unordered_map<uint32_t, ConcurrencyClass> concurrency_map;      // Node ID -> concurrency class
    std::unordered_map<uint32_t, DSAType> dsa_map;                       // Node ID -> DSA type

public:
    IRStream() = default;

    IRStream([[maybe_unused]] dgtl::core::DGTL_Arena& arena, size_t node_count);
    IRStream(IRNode* external_buffer, size_t node_count);
    ~IRStream();

    IRStream(IRStream&& other) noexcept;
    IRStream& operator=(IRStream&& other) noexcept;

    [[nodiscard]] static IRStream allocate_transactional(size_t size);
    [[nodiscard]] static IRStream allocate_shadow(size_t size);

    // ======================================================
    // 🔹 BASICS
    // ======================================================
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] size_t capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool is_null() const noexcept;
    
    /**
     * @brief Get logical version based on structural hash
     * Critical for Analysis Manager invalidation.
     */
    [[nodiscard]] uint64_t get_version() const noexcept { return generate_logic_hash(); }

    void set_count(size_t v) noexcept;
    void reset() noexcept;
    void update_shadow_size(size_t s) noexcept;
    void set_shadow_count(size_t v) noexcept;

    // ======================================================
    // 🔹 ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] IRNode get_node(size_t idx) const noexcept;
    [[nodiscard]] const IRNode& get_node_readonly(size_t idx) const noexcept;

    [[nodiscard]] IRNode& get_node_ref(size_t idx) noexcept;

    [[nodiscard]] const IRAtom& get_atom_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core(size_t idx) const noexcept { return get_core_readonly(idx); }

    // ======================================================
    // 🔹 SHADOW ACCESS (FOR PASSES)
    // ======================================================
    IRAtom& get_shadow_atom(size_t idx) noexcept { return shadow_atoms[idx]; }
    IRCore& get_shadow_core(size_t idx) noexcept { return shadow_cores[idx]; }

    // ======================================================
    // 🔹 EMIT
    // ======================================================
    void push_node(const IRAtom& atom, const IRCore& core) noexcept;
    void push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint64_t h = 0) noexcept;
    void push_back(const IRNode& node) noexcept;
    void set_node(size_t idx, const IRNode& node) noexcept;

    // ======================================================
    // 🔹 TRANSACTION CORE
    // ======================================================
    void begin_transaction() noexcept;
    void commit_transaction() noexcept;
    void rollback_transaction() noexcept;
    void abort_transaction() noexcept;

    // ======================================================
    // 🔹 SHADOW MANAGEMENT
    // ======================================================
    void prune_shadow_buffer() noexcept;
    void push_full_to_shadow() noexcept;
    void commit_from_shadow(IRStream& shadow_stream) noexcept;
    void commit_from_shadow() noexcept;
    uint32_t reserve_shadow_slot() noexcept;
    void push_to_shadow(const IRNode& node) noexcept;
    [[nodiscard]] IRNode get_shadow_node_proxy(size_t idx) const noexcept;

    // ======================================================
    // 🔹 BUFFER ACCESS (STRICT ALIGNMENT HINTS)
    // ======================================================
    [[nodiscard]] IRNode* get_buffer_ptr() const noexcept;

    [[nodiscard]] bool hint_aligned_strike() const noexcept {
        if (!atoms || !cores) {
            return false;
        }

        return (reinterpret_cast<uintptr_t>(atoms) & 63) == 0 &&
               (reinterpret_cast<uintptr_t>(cores) & 63) == 0;
    }

    // ======================================================
    // 🔹 CLONING & VALIDATION
    // ======================================================
    void copy_from(const IRStream& other);
    [[nodiscard]] IRStream clone() const;
    [[nodiscard]] bool validate_shadow_integrity() const noexcept;
    [[nodiscard]] uint64_t generate_logic_hash() const noexcept;
    [[nodiscard]] uint32_t intent_crc() const noexcept;
    [[nodiscard]] uint32_t get_max_id() const noexcept;

    void push_shadow_fold(const IRNode& node) noexcept;
    void perform_gvn() noexcept;
    void perform_dead_code_elimination() noexcept;

    void optimize_memory_access_patterns() noexcept;
    void evolve_node(IRNode* node);

    // ============================================================================
    // ADVANCED OPTIMIZATION PASSES (NEW - BETTER THAN LLVM)
    // ============================================================================
    void perform_speculative_optimization() noexcept;
    void perform_loop_unrolling(uint8_t unroll_factor = 4) noexcept;
    void optimize_branch_prediction() noexcept;
    void apply_backend_specific_optimizations(BackendProfile::TargetISA target) noexcept;
    void perform_profile_guided_optimization(const ProfilingData& profile_data) noexcept;
    void analyze_vectorization_patterns() noexcept;
    
    // ============================================================================
    // BACKEND-AWARE INSTRUCTION OPTIMIZATION (NEW)
    // ============================================================================
    void apply_backend_instruction_selection(BackendProfile::TargetISA target) noexcept;
    void dispatch_simd_operations(BackendProfile::TargetISA target) noexcept;
    void optimize_gather_scatter_patterns(BackendProfile::TargetISA target) noexcept;
    
    // ============================================================================
    // PROFILE-GUIDED OPTIMIZATION & HYBRID TIER TRACKING (NEW)
    // ============================================================================
    BackendProfile::CompilationTier compute_adaptive_tier(uint32_t function_id) noexcept;
    void record_function_call(uint32_t function_id) noexcept;
    void record_block_hotness(uint32_t block_id) noexcept;
    BackendProfile& get_backend_profile(uint32_t stream_id) noexcept;
    void update_backend_stats(uint32_t stream_id, uint64_t compilation_time_us) noexcept;
    void record_cache_access(uint32_t stream_id, bool hit) noexcept;
    void analyze_simd_opportunities() noexcept;
    void analyze_speculation_opportunities() noexcept;

    // ======================================================
    // 🔹 RAW ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] const IRAtom* raw_atoms() const noexcept { return atoms; }
    [[nodiscard]] const IRCore* raw_cores() const noexcept { return cores; }
    
    [[nodiscard]] IRAtom* raw_atoms() noexcept { 
        return in_transaction.load(std::memory_order_relaxed) ? shadow_atoms : atoms; 
    }
    [[nodiscard]] IRCore* raw_cores() noexcept { 
        return in_transaction.load(std::memory_order_relaxed) ? shadow_cores : cores; 
    }

    [[nodiscard]] bool is_active_transaction() const noexcept { 
        return in_transaction.load(std::memory_order_acquire); 
    }

    void prefetch_node(size_t idx) const noexcept {
        if (idx >= _capacity || !atoms || !cores) return;
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&atoms[idx], 0, 1);
        __builtin_prefetch(&cores[idx], 0, 1);
#else
        (void)idx;
#endif
    }

    // ======================================================
    // 🔹 ITERATOR
    // ======================================================
    struct Iterator {
        const IRStream* stream;
        size_t index;

        bool operator!=(const Iterator& o) const noexcept { return index != o.index; }
        void operator++() noexcept { ++index; }
        IRNode operator*() const noexcept { return stream->get_node(index); }
    };

    Iterator begin() const noexcept { return {this, 0}; }
    Iterator end() const noexcept { return {this, count}; }

    // ============================================================================
    // NEW: FRONTEND LANGUAGE & OWNERSHIP TRACKING (UNIVERSAL LANGUAGE SUPPORT)
    // ============================================================================

    void set_frontend_language(FrontendLanguage lang) noexcept {
        frontend_lang = lang;
    }

    [[nodiscard]] FrontendLanguage get_frontend_language() const noexcept {
        return frontend_lang;
    }

    void set_default_type_context(const TypeContext& ctx) noexcept {
        default_type_ctx = ctx;
    }

    [[nodiscard]] const TypeContext& get_default_type_context() const noexcept {
        return default_type_ctx;
    }

    // Ownership Tracking Methods
    void track_ownership(uint32_t node_id, OwnershipModel model, uint32_t acquire_site) noexcept {
        ownership_tracking[node_id] = {
            model, acquire_site, 0, 0, false, false
        };
    }

    void record_ownership_transfer(uint32_t node_id, uint32_t transfer_site) noexcept {
        auto it = ownership_tracking.find(node_id);
        if (it != ownership_tracking.end()) {
            it->second.transfer_count++;
            it->second.last_transfer_site = transfer_site;  // Record diagnostic site
        }
    }

    void release_ownership(uint32_t node_id, uint32_t release_site) noexcept {
        auto it = ownership_tracking.find(node_id);
        if (it != ownership_tracking.end()) {
            it->second.release_site = release_site;
        }
    }

    [[nodiscard]] const OwnershipLifetime* get_ownership_info(uint32_t node_id) const noexcept {
        auto it = ownership_tracking.find(node_id);
        return it != ownership_tracking.end() ? &it->second : nullptr;
    }

    // Concurrency Tracking Methods
    void mark_concurrent(uint32_t node_id, ConcurrencyClass concur_class) noexcept {
        concurrency_map[node_id] = concur_class;
    }

    [[nodiscard]] ConcurrencyClass get_concurrency_class(uint32_t node_id) const noexcept {
        auto it = concurrency_map.find(node_id);
        return it != concurrency_map.end() ? it->second : ConcurrencyClass::SEQUENTIAL;
    }

    [[nodiscard]] uint32_t count_concurrent_ops() const noexcept {
        uint32_t count = 0;
        for (const auto& [_, concur] : concurrency_map) {
            if (concur != ConcurrencyClass::SEQUENTIAL) count++;
        }
        return count;
    }

    // DSA Type Tracking Methods
    void mark_dsa_operation(uint32_t node_id, DSAType dsa_type) noexcept {
        dsa_map[node_id] = dsa_type;
    }

    [[nodiscard]] DSAType get_dsa_type(uint32_t node_id) const noexcept {
        auto it = dsa_map.find(node_id);
        return it != dsa_map.end() ? it->second : DSAType::SCALAR;
    }

    [[nodiscard]] bool has_graph_operations() const noexcept {
        for (const auto& [_, dsa] : dsa_map) {
            if (dsa == DSAType::GRAPH) return true;
        }
        return false;
    }

    [[nodiscard]] bool has_tensor_operations() const noexcept {
        for (const auto& [_, dsa] : dsa_map) {
            if (dsa == DSAType::TENSOR) return true;
        }
        return false;
    }

    // Language-Specific Compilation Decisions
    [[nodiscard]] bool is_mlir_capable() const noexcept {
        // Check if IR can be lowered to MLIR representation
        return frontend_lang == FrontendLanguage::UNIVERSAL ||
               frontend_lang == FrontendLanguage::MYZON ||
               frontend_lang == FrontendLanguage::CPP;
    }

    [[nodiscard]] bool requires_llvm_lowering() const noexcept {
        // Check if IR requires LLVM-style lowering
        return has_graph_operations() || 
               count_concurrent_ops() > 0 ||
               has_tensor_operations();
    }

    [[nodiscard]] uint32_t estimate_complexity_score() const noexcept {
        // Estimate compilation complexity for tier selection
        uint32_t score = 0;
        score += count_concurrent_ops() * 10;        // Concurrency is expensive
        score += ownership_tracking.size() * 2;      // Ownership tracking overhead
        if (has_graph_operations()) score += 50;     // Graphs need polyhedral analysis
        if (has_tensor_operations()) score += 30;    // Tensors need shape inference
        return score;
    }
};

// ======================================================
// 🔹 TRANSACTION GUARD (RAII SAFE)
// ======================================================
struct IRTransactionGuard {
    IRStream& stream;
    bool committed = false;

    explicit IRTransactionGuard(IRStream& s) : stream(s) {
        stream.begin_transaction();
    }

    void commit() {
        stream.commit_transaction();
        committed = true;
    }

    ~IRTransactionGuard() {
        if (!committed) {
            stream.rollback_transaction();
        }
    }
};

} // namespace dgtl::ir