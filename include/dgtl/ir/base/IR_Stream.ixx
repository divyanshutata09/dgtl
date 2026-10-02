/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
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

module;

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <atomic>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <iomanip>
#include <array>
#include <mutex>
#if defined(_MSC_VER)
# include <intrin.h>   // _mm_prefetch on UCRT64 / MSVC
#endif

// Canonical HIR stream representation and transactional storage.
export module dgtl.ir.stream;

// Export native C++ dependencies required by the storage implementation
export import dgtl.runtime.numa;
export import dgtl.arena; // Handles your arena-backed allocation mechanics
export import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir_contracts;
export import dgtl.ir_node; // Contains your base IRAtom, IRCore, IRNode, and OpCode footprints
export import dgtl.flat_map;
export import dgtl.sync;
// Target-specific execution and lowering are outside the HIR module.
// This profile records only target-neutral compilation/runtime observations.

export namespace dgtl::ir {

// ============================================================================
// Target-neutral compilation profile
// ============================================================================

/**
 * @brief Target-neutral compilation/profile metrics used by HIR analysis and scheduling
 */
struct CompilationProfile {
    enum class CompilationTier : uint8_t {
        Fast = 0,
        Intensive = 1,
        Adaptive = 2
    };

    uint64_t execution_time_us = 0;
    uint64_t calls_count = 0;
    uint32_t cache_hits = 0;
    uint32_t cache_misses = 0;
    float avg_compilation_latency_us = 0.0f;
    CompilationTier current_tier = CompilationTier::Fast;
    uint32_t tier_switch_count = 0;

    struct SpeculationMetrics {
        uint32_t speculations_made = 0;
        uint32_t speculations_verified = 0;
        uint32_t deopt_triggers = 0;
        [[nodiscard]] float speculation_success_rate() const noexcept {
            return speculations_made == 0 ? 0.0f
                : 100.0f * static_cast<float>(speculations_verified) / static_cast<float>(speculations_made);
        }
    } speculation;

    struct LoopMetrics {
        uint32_t speculations_made = 0;
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

    [[nodiscard]] const SpeculationMetrics& get_speculation() const noexcept { return speculation; }
    [[nodiscard]] const LoopMetrics& get_loops() const noexcept { return loops; }
    [[nodiscard]] const SIMDMetrics& get_simd() const noexcept { return simd; }
};

/**
 * @brief Profiling Data - Runtime execution profile for PGO
 */
struct ProfilingData {
    struct BlockMetrics {
        uint64_t execution_count = 0;
        float hotness_score = 0.0f;
    };
    
    // 🚀 MODERNIZE: Replaced std::unordered_map with custom FastFlatMap
    dgtl::utils::FastFlatMap<uint32_t, BlockMetrics, 256> block_metrics;
    uint64_t max_execution_count = 1;

    void record_block_execution(uint32_t block_id) noexcept {
        auto* m = block_metrics.find(block_id);
        if (!m) {
            BlockMetrics bm{1, 0.0f};
            block_metrics.insert_or_assign(block_id, bm);
            m = block_metrics.find(block_id);
        } else {
            m->execution_count++;
        }
        if (m->execution_count > max_execution_count)
            max_execution_count = m->execution_count;
    }

    [[nodiscard]] float compute_hotness_score(uint32_t block_id) const noexcept {
        auto* it = block_metrics.find(block_id);
        if (!it) return 0.0f;
        return static_cast<float>(it->execution_count)
               / static_cast<float>(max_execution_count);
    }
};

// ======================================================
// CFG DATA MODEL
// ======================================================
struct IRBasicBlock {
    uint32_t id = INVALID_SLOT;
    size_t begin = 0;  // inclusive node index
    size_t end = 0;    // exclusive node index
    uint32_t primary_label = INVALID_SLOT;
    bool is_entry = false;
};

enum class CFGEdgeKind : uint8_t {
    Fallthrough = 0,
    UnconditionalBranch = 1,
    ConditionalTrue = 2,
    ConditionalFalse = 3
};

struct CFGEdge {
    uint32_t from = INVALID_SLOT;
    uint32_t to = INVALID_SLOT;
    CFGEdgeKind kind = CFGEdgeKind::Fallthrough;
};

// ======================================================
// IR STREAM
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
    IRNode fallback_node{};  // sentinel returned on OOB reads; never mutated by const paths
    
    // Stream-level lock used by std::lock_guard in implementation units.
    mutable std::recursive_mutex stream_mutex;

    // Memory Ownership Flags
    bool owns_aligned_alloc_storage = false;
    bool owns_joined_buffer = false;

    // ========================================================================
    // NEW: FRONTEND CONTEXT & LANGUAGE SUPPORT
    // ========================================================================
    FrontendLanguage frontend_lang = FrontendLanguage::UNIVERSAL;
    TypeContext default_type_ctx;
    
    // 🚀 MODERNIZE: Replacing massive unordered_maps with FastFlatMaps to prevent compilation crashes
    dgtl::utils::FastFlatMap<uint32_t, OwnershipLifetime, 1024> ownership_tracking;  // Node ID -> ownership info
    dgtl::utils::FastFlatMap<uint32_t, ConcurrencyClass, 1024> concurrency_map;       // Node ID -> concurrency class
    dgtl::utils::FastFlatMap<uint32_t, DSAType, 1024> dsa_map;                       // Node ID -> DSA type
    
    // 🚀 FIXED: Lightweight layout tracking vectors bypass private map internals cleanly
    std::vector<uint32_t> concurrency_keys_;
    std::vector<uint32_t> dsa_keys_;

    // CALL overflow-argument sidecar: node index -> args[3..N-1] virtual IDs.
    dgtl::utils::FastFlatMap<size_t, std::vector<uint32_t>, 1024> call_overflow_args_;
    // CALL ABI layout sidecar: node index -> packed ABI plan words.
    dgtl::utils::FastFlatMap<size_t, std::vector<uint32_t>, 1024> call_abi_layout_words_;

    // Phase D: Variadic and Multi-Result Sidecars
    dgtl::utils::FastFlatMap<size_t, std::vector<uint32_t>, 1024> operand_overflow_;     // node_idx -> [op4, op5, ...]
    dgtl::utils::FastFlatMap<size_t, std::vector<uint32_t>, 1024> result_ids_;           // node_idx -> [rid0, rid1, ...]
    dgtl::utils::FastFlatMap<uint32_t, uint32_t, 8192> result_types_;                    // result_id -> TypeHandle
    dgtl::utils::FastFlatMap<size_t, uint32_t, 1024> node_effects_;                       // node_idx -> EffectSet

    // Cached CFG artifacts (rebuilt on-demand after structural mutations)
    mutable bool cfg_cache_valid = false;
    mutable std::vector<IRBasicBlock> cfg_blocks_cache;
    mutable std::vector<CFGEdge> cfg_edges_cache;
    mutable std::vector<std::vector<uint32_t>> cfg_succ_cache;
    mutable std::vector<std::vector<uint32_t>> cfg_pred_cache;
    mutable std::vector<uint32_t> cfg_node_to_block_cache;
    mutable dgtl::utils::FastFlatMap<uint32_t, uint32_t, 1024> cfg_label_to_block_cache;

    void invalidate_cfg_cache_locked() noexcept;
    void ensure_cfg_cache_locked() const noexcept;
    void rebuild_cfg_locked() const noexcept;

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
    // BASICS
    // ======================================================
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] size_t capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] const IRNode* get_joined_buffer() const noexcept { return joined_buffer; }

    // NUMA utilities for IR execution/merge callers.
    [[nodiscard]] static uint32_t numa_node_count() noexcept {
        return dgtl::runtime::numa::get_num_nodes();
    }

    [[nodiscard]] static uint32_t current_numa_node() noexcept {
        return dgtl::runtime::numa::get_current_node();
    }

    [[nodiscard]] static bool pin_current_thread_to_numa_node(uint32_t node) noexcept {
        return dgtl::runtime::numa::pin_current_thread_to_node(node);
    }

    [[nodiscard]] static bool pin_worker_affinity(uint32_t worker_index) noexcept {
        const uint32_t node = dgtl::runtime::numa::recommend_worker_node(worker_index);
        const uint32_t core = dgtl::runtime::numa::recommend_worker_core(worker_index);
        const bool node_ok = dgtl::runtime::numa::pin_current_thread_to_node(node);
        const bool core_ok = dgtl::runtime::numa::pin_thread_to_core(core);
        return node_ok && core_ok;
    }

    [[nodiscard]] static void* allocate_numa_local(size_t bytes,
                                                   size_t alignment = 64u) noexcept {
        return dgtl::runtime::numa::allocate_on_node(bytes,
                                                     dgtl::runtime::numa::get_current_node(),
                                                     alignment);
    }

    static void free_numa_local(void* ptr) noexcept {
        dgtl::runtime::numa::free_allocation(ptr);
    }
    
    /**
     * @brief Get logical version based on structural hash
     * Critical for Analysis Manager invalidation.
     */
    [[nodiscard]] uint64_t get_version() const noexcept { return generate_logic_hash(); }

    void set_count(size_t v) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool reserve_capacity(size_t requested_capacity) noexcept;
    void update_shadow_size(size_t s) noexcept;
    void set_shadow_count(size_t v) noexcept;

    // ======================================================
    // ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] IRNode get_node(size_t idx) const noexcept;
    [[nodiscard]] const IRNode& get_node_readonly(size_t idx) const noexcept;

    [[nodiscard]] IRNode& get_node_ref(size_t idx) noexcept;

    [[nodiscard]] const IRAtom& get_atom_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRCore& get_core(size_t idx) const noexcept { return get_core_readonly(idx); }

    // ======================================================
    // SHADOW ACCESS (FOR PASSES)
    // Must only be called while a transaction is active (shadow buffers are valid).
    // ======================================================
    [[nodiscard]] IRAtom& get_shadow_atom(size_t idx) noexcept {
        assert(shadow_atoms != nullptr && idx < _capacity &&
               "get_shadow_atom: called outside an active transaction or OOB");
        return shadow_atoms[idx];
    }
    [[nodiscard]] IRCore& get_shadow_core(size_t idx) noexcept {
        assert(shadow_cores != nullptr && idx < _capacity &&
               "get_shadow_core: called outside an active transaction or OOB");
        return shadow_cores[idx];
    }

    // ======================================================
    // EMIT
    // ======================================================
    void push_node(const IRAtom& atom, const IRCore& core) noexcept;
    void push_node(OpCode op,
                   uint16_t fl,
                   uint32_t res,
                   uint32_t a,
                   uint32_t b,
                   uint32_t c,
                   uint64_t h = 0) noexcept;
    void push_back(const IRNode& node) noexcept;
    void set_node(size_t idx, const IRNode& node) noexcept;

    // ======================================================
    // CALL OVERFLOW ARG SIDECAR
    // ======================================================
    void set_call_overflow_args(size_t node_idx, std::span<const uint32_t> extra_args);
    [[nodiscard]] std::vector<uint32_t> get_call_overflow_args(size_t node_idx) const;
    void clear_call_overflow_args(size_t node_idx);
    void set_call_abi_layout_words(size_t node_idx, std::span<const uint32_t> words);
    [[nodiscard]] std::vector<uint32_t> get_call_abi_layout_words(size_t node_idx) const;
    void clear_call_abi_layout_words(size_t node_idx);

    // ======================================================
    // Phase D: Variadic & Multi-Result Sidecars
    // ======================================================
    void set_operand_overflow(size_t node_idx, std::span<const uint32_t> overflow_operands) noexcept;
    [[nodiscard]] std::vector<uint32_t> get_operand_overflow(size_t node_idx) const noexcept;
    void clear_operand_overflow(size_t node_idx) noexcept;

    void set_result_ids(size_t node_idx, std::span<const uint32_t> result_ids) noexcept;
    [[nodiscard]] std::vector<uint32_t> get_result_ids(size_t node_idx) const noexcept;
    void clear_result_ids(size_t node_idx) noexcept;

    void set_result_type(uint32_t result_id, uint32_t type_handle) noexcept;
    void set_result_type(uint32_t result_id, TypeCode type_code) noexcept;
    [[nodiscard]] uint32_t get_result_type(uint32_t result_id) const noexcept;
    [[nodiscard]] TypeCode get_result_type_code(uint32_t result_id) const noexcept;
    void clear_result_type(uint32_t result_id) noexcept;

    void set_node_effects(size_t node_idx, uint32_t effect_set) noexcept;
    [[nodiscard]] uint32_t get_node_effects(size_t node_idx) const noexcept;
    void clear_node_effects(size_t node_idx) noexcept;

    // ======================================================
    // TRANSACTION CORE
    // ======================================================
    void begin_transaction() noexcept;
    void commit_transaction() noexcept;
    void rollback_transaction() noexcept;
    void abort_transaction() noexcept;

    // ======================================================
    // SHADOW MANAGEMENT
    // ======================================================
    void prune_shadow_buffer() noexcept;
    void push_full_to_shadow() noexcept;
    void commit_from_shadow(IRStream& shadow_stream) noexcept;
    void commit_from_shadow() noexcept;
    uint32_t reserve_shadow_slot() noexcept;
    void push_to_shadow(const IRNode& node) noexcept;
    [[nodiscard]] IRNode get_shadow_node_proxy(size_t idx) const noexcept;

    // ======================================================
    // BUFFER ACCESS (STRICT ALIGNMENT HINTS)
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
    // CLONING & VALIDATION
    // ======================================================
    void copy_from(const IRStream& other);
    [[nodiscard]] IRStream clone() const;
    [[nodiscard]] bool validate_shadow_integrity() const noexcept;
    [[nodiscard]] uint64_t generate_logic_hash() const noexcept;
    [[nodiscard]] uint32_t intent_crc() const noexcept;
    [[nodiscard]] uint32_t get_max_id() const noexcept;


    void optimize_memory_access_patterns() noexcept;

    // ============================================================================
    // ADVANCED OPTIMIZATION PASSES (NEW - BETTER THAN LLVM)
    // ============================================================================
    void perform_speculative_optimization() noexcept;
    void perform_loop_unrolling(uint8_t unroll_factor = 4) noexcept;
    void optimize_branch_prediction() noexcept;
    void perform_profile_guided_optimization(const ProfilingData& profile_data) noexcept;
    void analyze_vectorization_patterns() noexcept;

    [[nodiscard]] std::span<const IRBasicBlock> get_cached_cfg_blocks() const noexcept {
        ensure_cfg_cache_locked();
        return cfg_blocks_cache;
    }
    [[nodiscard]] std::span<const CFGEdge> get_cached_cfg_edges() const noexcept {
        ensure_cfg_cache_locked();
        return cfg_edges_cache;
    }
    [[nodiscard]] std::span<const std::vector<uint32_t>> get_cached_cfg_succ() const noexcept {
        ensure_cfg_cache_locked();
        return cfg_succ_cache;
    }
    [[nodiscard]] std::span<const std::vector<uint32_t>> get_cached_cfg_pred() const noexcept {
        ensure_cfg_cache_locked();
        return cfg_pred_cache;
    }
    [[nodiscard]] std::span<const uint32_t> get_cached_cfg_node_to_block() const noexcept {
        ensure_cfg_cache_locked();
        return cfg_node_to_block_cache;
    }
    
    // ============================================================================
    // PROFILE-GUIDED OPTIMIZATION &  TIER TRACKING (NEW)
    // ============================================================================
    [[nodiscard]] CompilationProfile get_compilation_profile(uint32_t stream_id) const noexcept;
    [[nodiscard]] CompilationProfile::CompilationTier compute_adaptive_tier(uint32_t function_id) const noexcept;

    void record_function_call(uint32_t function_id) noexcept;
    void update_compilation_stats(uint32_t stream_id, uint64_t compilation_time_us) noexcept;
    void record_block_hotness(uint32_t block_id) noexcept;
    void record_compilation_cache_access(uint32_t stream_id, bool hit) noexcept;
    void ingest_compilation_feedback(uint32_t stream_id,
                                 double actual_latency,
                                 double cache_miss_rate,
                                 double branch_mispredict_rate,
                                 uint64_t execution_count) noexcept;
    void analyze_simd_opportunities() noexcept;
    void analyze_speculation_opportunities() noexcept;

    // ======================================================
    // CFG: BASIC BLOCKS / EDGES / TRAVERSALS
    // ======================================================
    void invalidate_cfg_cache() noexcept;
    [[nodiscard]] const std::vector<IRBasicBlock>& get_basic_blocks() const noexcept;
    [[nodiscard]] const std::vector<CFGEdge>& get_cfg_edges() const noexcept;
    [[nodiscard]] const std::vector<uint32_t>& get_successors(uint32_t block_id) const noexcept;
    // [[nodisclean]] const std::vector<uint32_t>& get_predecessors(uint32_t block_id) const noexcept;
    [[nodiscard]] const std::vector<uint32_t>& get_predecessors(uint32_t block_id) const noexcept;
    [[nodiscard]] uint32_t get_entry_block_id() const noexcept;
    [[nodiscard]] uint32_t get_block_id_for_node(size_t node_index) const noexcept;
    [[nodiscard]] std::vector<uint32_t> traversal_preorder(uint32_t entry_block = INVALID_SLOT) const noexcept;
    [[nodiscard]] std::vector<uint32_t> traversal_postorder(uint32_t entry_block = INVALID_SLOT) const noexcept;
    [[nodiscard]] std::vector<uint32_t> traversal_reverse_postorder(uint32_t entry_block = INVALID_SLOT) const noexcept;
    [[nodiscard]] std::vector<uint32_t> traversal_breadth_first(uint32_t entry_block = INVALID_SLOT) const noexcept;

    // ======================================================
    // CFG: DOMINANCE & LOOP DISCOVERY
    // ======================================================
    [[nodiscard]] std::vector<uint32_t> compute_immediate_dominators(uint32_t entry_block = INVALID_SLOT) const noexcept;
    [[nodiscard]] std::vector<std::vector<uint32_t>> compute_dominance_frontier(uint32_t entry_block = INVALID_SLOT) const noexcept;
    [[nodiscard]] std::vector<uint32_t> find_natural_loop_headers(uint32_t entry_block = INVALID_SLOT) const noexcept;

    // ======================================================
    // RAW ACCESS (TRANSACTION-AWARE)
    // ======================================================
    [[nodiscard]] const IRAtom* raw_atoms() const noexcept {
        return in_transaction.load(std::memory_order_acquire) ? shadow_atoms : atoms;
    }
    [[nodiscard]] const IRCore* raw_cores() const noexcept {
        return in_transaction.load(std::memory_order_acquire) ? shadow_cores : cores;
    }

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
#elif defined(_MSC_VER)
        _mm_prefetch(reinterpret_cast<const char*>(&atoms[idx]), _MM_HINT_T1);
        _mm_prefetch(reinterpret_cast<const char*>(&cores[idx]), _MM_HINT_T1);
#endif
    }

    // ======================================================
    // ITERATOR
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
        ownership_tracking.insert_or_assign(node_id, OwnershipLifetime{
            model, acquire_site, 0, 0, false, false
        });
    }

    void record_ownership_transfer(uint32_t node_id, uint32_t transfer_site) noexcept {
        auto* it = ownership_tracking.find(node_id);
        if (it) {
            it->transfer_count++;
            it->last_transfer_site = transfer_site;  
        }
    }

    void release_ownership(uint32_t node_id, uint32_t release_site) noexcept {
        auto* it = ownership_tracking.find(node_id);
        if (it) {
            it->release_site = release_site;
        }
    }

    [[nodiscard]] const OwnershipLifetime* get_ownership_info(uint32_t node_id) const noexcept {
        auto* it = ownership_tracking.find(node_id);
        return it ? it : nullptr;
    }

    // Concurrency Tracking Methods
    void mark_concurrent(uint32_t node_id, ConcurrencyClass concur_class) noexcept {
        if (!concurrency_map.find(node_id)) {
            concurrency_keys_.push_back(node_id);
        }
        concurrency_map.insert_or_assign(node_id, concur_class);
    }

    [[nodiscard]] ConcurrencyClass get_concurrency_class(uint32_t node_id) const noexcept {
        auto* it = concurrency_map.find(node_id);
        return it ? *it : ConcurrencyClass::SEQUENTIAL;
    }

    [[nodiscard]] uint32_t count_concurrent_ops() const noexcept {
        uint32_t n = 0;
        for (uint32_t key : concurrency_keys_) {
            auto* it = concurrency_map.find(key);
            if (it && *it != ConcurrencyClass::SEQUENTIAL) ++n;
        }
        return n;
    }

    // DSA Type Tracking Methods
    void mark_dsa_operation(uint32_t node_id, DSAType dsa_type) noexcept {
        if (!dsa_map.find(node_id)) {
            dsa_keys_.push_back(node_id);
        }
        dsa_map.insert_or_assign(node_id, dsa_type);
    }

    [[nodiscard]] DSAType get_dsa_type(uint32_t node_id) const noexcept {
        auto* it = dsa_map.find(node_id);
        return it ? *it : DSAType::SCALAR;
    }

    [[nodiscard]] bool has_graph_operations() const noexcept {
        for (uint32_t key : dsa_keys_) {
            auto* it = dsa_map.find(key);
            if (it && *it == DSAType::GRAPH) return true;
        }
        return false;
    }

    [[nodiscard]] bool has_tensor_operations() const noexcept {
        for (uint32_t key : dsa_keys_) {
            auto* it = dsa_map.find(key);
            if (it && *it == DSAType::TENSOR) return true;
        }
        return false;
    }

    // Language-Specific Compilation Decisions
    [[nodiscard]] bool is_mlir_capable() const noexcept {
        return frontend_lang == FrontendLanguage::UNIVERSAL ||
               frontend_lang == FrontendLanguage::DSL ||
               frontend_lang == FrontendLanguage::CPP;
    }

    [[nodiscard]] bool requires_llvm_lowering() const noexcept {
        return has_graph_operations() || 
               count_concurrent_ops() > 0 ||
               has_tensor_operations();
    }

    [[nodiscard]] uint32_t estimate_complexity_score() const noexcept {
        uint32_t score = 0;
        score += count_concurrent_ops() * 10;        
        score += ownership_tracking.size() * 2;      
        if (has_graph_operations()) score += 50;     
        if (has_tensor_operations()) score += 30;    
        return score;
    }
};

// ======================================================
// TRANSACTION GUARD (RAII SAFE)
// ======================================================
struct IRTransactionGuard {
    IRStream& stream;
    bool committed = false;

    explicit IRTransactionGuard(IRStream& s) : stream(s) {
        stream.begin_transaction();
    }

    IRTransactionGuard(const IRTransactionGuard&) = delete;
    IRTransactionGuard& operator=(const IRTransactionGuard&) = delete;

    void commit() noexcept {
        stream.commit_transaction();
        committed = true;
    }

    ~IRTransactionGuard() {
        if (!committed) {
            stream.rollback_transaction();
        }
    }
};

namespace numa = dgtl::runtime::numa;

[[nodiscard]] inline uint32_t numa_node_count() noexcept {
    return numa::get_num_nodes();
}

[[nodiscard]] inline uint32_t numa_current_node() noexcept {
    return numa::get_current_node();
}

[[nodiscard]] inline bool numa_pin_worker(uint32_t worker_index) noexcept {
    const uint32_t node = numa::recommend_worker_node(worker_index);
    const uint32_t core = numa::recommend_worker_core(worker_index);
    const bool node_ok = numa::pin_current_thread_to_node(node);
    const bool core_ok = numa::pin_thread_to_core(core);
    return node_ok && core_ok;
}

} // namespace dgtl::ir




