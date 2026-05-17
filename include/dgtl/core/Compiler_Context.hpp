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

/*
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0
 * STEP 5: LOCK-FREE THREAD-LOCAL CONTEXT
 * VERSION 4.2: BIR 1.0.0 PACKED ATOM + V4-ARENA INTEGRATED
 * PILLAR 2: TRUE TLS + SHADOW-BUFFERED TRANSACTIONS
 * Extension: P0-001 Polyhedral Configuration
 *
 * This header is the canonical single-source definition for:
 *   - dgtl::core::PolyhedralConfig   (polyhedral transform settings)
 *   - dgtl::core::ThreadLocalBuffer  (lock-free TLS node buffer)
 *   - dgtl::core::DGTLContext        (internal compiler context implementation)
 *   - dgtl::api::Context             (public API wrapper with move semantics)
 *
 * Legacy compatibility shims in api/Context.hpp and core/Context.hpp
 * both redirect here.  All new code should include this header directly:
 *
 *   #include "dgtl/core/Compiler_Context.hpp"
 */

#pragma once

#include "dgtl/api/Config.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/IR_Node.hpp"    // IRAtom, IRCore, IRNode, OpCode, AtomSplitter

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

// ============================================================================
// SECTION 1 — INTERNAL COMPILER CONTEXT  (dgtl::core)
// ============================================================================

namespace dgtl::core {

// ----------------------------------------------------------------------------
// P0-001: POLYHEDRAL CONFIGURATION
// ----------------------------------------------------------------------------

/**
 * @enum PolyhedralMode
 * @brief Control level for affine SCoP detection and transformation.
 */
enum class PolyhedralMode : uint8_t {
    off        = 0, ///< Disable all polyhedral analysis and transforms
    safe       = 1, ///< Enable safe transforms with strict legality checking
    aggressive = 2, ///< Enable aggressive transforms with profitability gates
};

/**
 * @enum PolyhedralParallelPolicy
 * @brief Parallel lowering strategy for affine loop nests.
 */
enum class PolyhedralParallelPolicy : uint8_t {
    auto_policy = 0, ///< Auto-decide based on loop depth and region size
    off         = 1, ///< Disable parallel lowering
    on          = 2, ///< Force parallel lowering when legal
};

/**
 * @struct PolyhedralConfig
 * @brief Runtime configuration for polyhedral parallelization features.
 *
 * Populated from CLI flags and passed through the optimization pipeline.
 */
struct PolyhedralConfig {
    PolyhedralMode         mode            = PolyhedralMode::off;
    PolyhedralParallelPolicy parallel_policy = PolyhedralParallelPolicy::auto_policy;
    uint32_t               tile_size       = 0; ///< 0 = auto-select via heuristic

    [[nodiscard]] bool is_enabled() const noexcept {
        return mode != PolyhedralMode::off;
    }

    [[nodiscard]] bool allow_aggressive_transforms() const noexcept {
        return mode == PolyhedralMode::aggressive;
    }
};

// ----------------------------------------------------------------------------
// PILLAR 2: THREAD-LOCAL BUFFER  (64-byte cache-line aligned)
// ----------------------------------------------------------------------------

/**
 * @struct ThreadLocalBuffer
 * @brief Per-thread dual-page buffer for lock-free IR node allocation.
 *
 * Maintains a primary (normal) page and a shadow (transactional) page so
 * that speculative strike passes can be rolled back without heap overhead.
 *
 * Layout guarantees:
 *  - Tier 1 (IRAtom): packed opcode/flag fields — BIR 1.0.0 standard
 *  - Tier 2 (IRCore): structural operand & hash storage
 *  - 64-byte alignment prevents false sharing on multi-socket NUMA configs.
 */
struct alignas(64) ThreadLocalBuffer {
    // ---- PRIMARY page (Tier 1 & 2) ----
    ir::IRAtom* base_atom_ptr       = nullptr;
    ir::IRCore* base_core_ptr       = nullptr;
    ir::IRAtom* atom_ptr            = nullptr;
    ir::IRCore* core_ptr            = nullptr;
    size_t      remaining_primary   = 0;

    // ---- SHADOW page (Tier 1 & 2) — used during transactional strikes ----
    ir::IRAtom* base_shadow_atom_ptr = nullptr;
    ir::IRCore* base_shadow_core_ptr = nullptr;
    ir::IRAtom* shadow_atom_ptr      = nullptr;
    ir::IRCore* shadow_core_ptr      = nullptr;
    size_t      remaining_shadow     = 0;

    // ---- CHECKPOINT — restore point for rollback ----
    ir::IRAtom* checkpoint_atom              = nullptr;
    ir::IRCore* checkpoint_core              = nullptr;
    size_t      checkpoint_remaining_shadow  = 0;

    bool in_strike = false;
};

/// @brief Thread-local buffer instance — one per OS thread.
inline thread_local ThreadLocalBuffer g_tls_buffer{};

// ----------------------------------------------------------------------------
// DGTL INTERNAL CONTEXT
// ----------------------------------------------------------------------------

/**
 * @class DGTLContext
 * @brief Internal compiler context: owns the arena and manages per-thread
 *        IR emission, transactional shadow buffers, and polyhedral config.
 *
 * Lifetime model:
 *  - One DGTLContext is constructed per compilation session.
 *  - It is referenced through the public dgtl::api::Context handle.
 *  - Not copyable or movable; the api::Context wrapper owns it exclusively
 *    via std::unique_ptr.
 *
 * Thread safety:
 *  - emit_node_fast() is thread-safe via per-thread TLS pages.
 *  - set_polyhedral_config() must be called before multi-threaded emission.
 */
class DGTLContext {
public:
    /// Number of IR nodes per arena page allocation (primary and shadow).
    static constexpr size_t PageSizeNodes = 262144;

    // ---- Construction / destruction ----

    explicit DGTLContext(DGTL_Arena& arena) noexcept
        : global_arena_(arena), polyhedral_config_() {}

    DGTLContext(const DGTLContext&)            = delete;
    DGTLContext& operator=(const DGTLContext&) = delete;
    DGTLContext(DGTLContext&&)                 = delete;
    DGTLContext& operator=(DGTLContext&&)      = delete;

    // ---- P0-001: Polyhedral configuration ----

    /**
     * @brief Retrieve current polyhedral transform configuration.
     */
    [[nodiscard]] const PolyhedralConfig& get_polyhedral_config() const noexcept {
        return polyhedral_config_;
    }

    /**
     * @brief Replace the polyhedral configuration.
     * @note Must be called before any concurrent emission begins.
     */
    void set_polyhedral_config(const PolyhedralConfig& cfg) noexcept {
        polyhedral_config_ = cfg;
    }

    // ---- Fast node emission ----

    /**
     * @brief Emit a single IR node into the current thread's active page.
     *
     * Selects shadow or primary page based on transactional state.  Refreshes
     * the page automatically when exhausted.  This is the hot path — marked
     * [[likely]] for page-full branch prediction.
     *
     * @param op    IR opcode
     * @param flg   Packed flags (BIR 1.0.0)
     * @param res   Result virtual register ID
     * @param a     First operand ID
     * @param b     Second operand ID
    * @param c     Third operand ID (optional, default INVALID_SLOT)
     * @param hash  Pre-computed intent hash (0 = auto-compute)
     */
    void emit_node_fast(ir::OpCode  op,
                        uint16_t    flg,
                        uint32_t    res,
                        uint32_t    a,
                        uint32_t    b,
                    uint32_t    c    = ir::INVALID_SLOT,
                        uint64_t    hash = 0) noexcept
    {
        auto& tls = g_tls_buffer;

        const bool page_empty = tls.in_strike
            ? (tls.remaining_shadow  == 0)
            : (tls.remaining_primary == 0);

        if (page_empty) [[unlikely]] {
            if (!refresh_thread_pages()) {
                return; // arena exhausted — caller must handle OOM externally
            }
        }

        ir::IRAtom* atom;
        ir::IRCore* core;

        if (tls.in_strike) {
            atom = tls.shadow_atom_ptr++;
            core = tls.shadow_core_ptr++;
            --tls.remaining_shadow;
        } else {
            atom = tls.atom_ptr++;
            core = tls.core_ptr++;
            --tls.remaining_primary;
        }

        // BIR 1.0.0: pack opcode + flags into the 32-bit IRAtom slot.
        *atom = ir::AtomSplitter::pack(op, flg);

        core->result_id   = res;
        core->op_a        = a;
        core->op_b        = b;
        core->op_c        = c;
        core->intent_hash = (hash != 0)
            ? hash
            : ir::IRNode::calculate_hash(op, res, a, b, c);
        core->logic_flags = 0;
        core->aux_flags   = 0;
        core->debug_id    = 0;
        core->epoch       = 0;
    }

    // ---- Atomic node construction API ----

    /**
     * @brief Prepare a scratch IRNode for incremental construction.
     * @return Pointer to the internal temp buffer (valid until next call).
     */
    [[nodiscard]] ir::IRNode* create_node_atomic() noexcept {
        temp_node_buffer_ = ir::IRNode{};
        temp_node_buffer_.result_id = ir::INVALID_SLOT;
        temp_node_buffer_.op_a = ir::INVALID_SLOT;
        temp_node_buffer_.op_b = ir::INVALID_SLOT;
        temp_node_buffer_.op_c = ir::INVALID_SLOT;
        return &temp_node_buffer_;
    }

    /**
     * @brief Commit a fully-constructed IRNode into the active page.
     * @param node The node to commit (copied via emit_node_fast).
     */
    void commit_node(const ir::IRNode& node) noexcept {
        emit_node_fast(node.opcode, node.flags,
                       node.result_id,
                       node.op_a, node.op_b, node.op_c,
                       node.intent_hash);
    }

    // ---- Transactional shadow-buffer API ----

    /**
     * @brief Enter transactional (strike) mode.
     *
     * Subsequent emit_node_fast() calls write to the shadow page.
     * Shadow pages are lazily allocated on first use.  A checkpoint is
     * captured so rollback can restore the shadow write-pointer precisely.
     */
    void enter_transactional_mode() noexcept {
        auto& tls = g_tls_buffer;

        // Ensure shadow pages exist before capturing the checkpoint so that
        // rollback always has a valid restore point.
        if (tls.remaining_shadow == 0 && !refresh_thread_pages()) {
            // Arena exhausted — fall back to non-transactional mode.
            tls.in_strike                    = false;
            tls.checkpoint_atom              = nullptr;
            tls.checkpoint_core              = nullptr;
            tls.checkpoint_remaining_shadow  = 0;
            return;
        }

        tls.in_strike                   = true;
        tls.checkpoint_atom             = tls.shadow_atom_ptr;
        tls.checkpoint_core             = tls.shadow_core_ptr;
        tls.checkpoint_remaining_shadow = tls.remaining_shadow;
    }

    /**
     * @brief Commit the transactional strike buffer.
     *
     * Clears transactional state without rewinding pointers, making
     * shadow-written nodes permanently visible for downstream merge.
     */
    void commit_transaction() noexcept {
        auto& tls        = g_tls_buffer;
        tls.in_strike    = false;
        tls.checkpoint_atom             = nullptr;
        tls.checkpoint_core             = nullptr;
        tls.checkpoint_remaining_shadow = 0;
    }

    /**
     * @brief Roll back the transactional strike buffer to the last checkpoint.
     *
     * Rewinds shadow write-pointers to the captured checkpoint and exits
     * transactional mode.  Effectively discards all nodes written since
     * enter_transactional_mode().
     */
    void rollback_transaction() noexcept {
        auto& tls = g_tls_buffer;
        if (tls.checkpoint_atom && tls.checkpoint_core) {
            tls.shadow_atom_ptr  = tls.checkpoint_atom;
            tls.shadow_core_ptr  = tls.checkpoint_core;
            tls.remaining_shadow = tls.checkpoint_remaining_shadow;
        }
        tls.in_strike                   = false;
        tls.checkpoint_atom             = nullptr;
        tls.checkpoint_core             = nullptr;
        tls.checkpoint_remaining_shadow = 0;
    }

    /**
     * @brief Query whether this thread is currently inside a transaction.
     */
    [[nodiscard]] bool is_in_transaction() const noexcept {
        return g_tls_buffer.in_strike;
    }

    // ---- Arena access ----

    /**
     * @brief Return the global arena managed by this context.
     */
    [[nodiscard]] DGTL_Arena& get_arena() noexcept {
        return global_arena_;
    }

private:
    DGTL_Arena&      global_arena_;
    mutable ir::IRNode temp_node_buffer_{}; ///< Scratch buffer for create_node_atomic()
    PolyhedralConfig   polyhedral_config_{}; ///< P0-001 polyhedral settings

    // ---- Internal: page replenishment (out-of-line to keep hot path small) ----

#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    [[nodiscard]] bool refresh_thread_pages() noexcept {
        auto& tls = g_tls_buffer;

        // --- PRIMARY page ---
        if (tls.remaining_primary == 0) {
            tls.base_atom_ptr     = global_arena_.allocate<ir::IRAtom>(PageSizeNodes);
            tls.base_core_ptr     = global_arena_.allocate<ir::IRCore>(PageSizeNodes);
            tls.atom_ptr          = tls.base_atom_ptr;
            tls.core_ptr          = tls.base_core_ptr;
            tls.remaining_primary = PageSizeNodes;
        }

        // --- SHADOW page ---
        if (tls.remaining_shadow == 0) {
            tls.base_shadow_atom_ptr = global_arena_.allocate<ir::IRAtom>(PageSizeNodes);
            tls.base_shadow_core_ptr = global_arena_.allocate<ir::IRCore>(PageSizeNodes);
            tls.shadow_atom_ptr      = tls.base_shadow_atom_ptr;
            tls.shadow_core_ptr      = tls.base_shadow_core_ptr;
            tls.remaining_shadow     = PageSizeNodes;
        }

        // Validate all pointers — arena may return nullptr on OOM.
        if (!tls.atom_ptr || !tls.core_ptr ||
            !tls.shadow_atom_ptr || !tls.shadow_core_ptr)
        {
            tls.remaining_primary = 0;
            tls.remaining_shadow  = 0;
            tls.in_strike         = false;
            return false;
        }

        return true;
    }
};

} // namespace dgtl::core

// ============================================================================
// SECTION 2 — PUBLIC API WRAPPER  (dgtl::api)
// ============================================================================

namespace dgtl {
namespace api {

/**
 * @class Context
 * @brief Public API handle for a DGTL compilation context.
 *
 * Owns a dgtl::core::DGTLContext instance exclusively through a unique_ptr.
 * Not copyable: each Context represents a unique compilation lifetime.  Move
 * semantics allow transferring ownership to higher-level session objects.
 *
 * Typical usage:
 * @code
 *   auto ctx = dgtl::api::Context::create(dgtl::api::hft_config());
 *   if (!ctx) { // handle creation failure }
 *   dgtl::api::JITSession jit(ctx);
 * @endcode
 */
class Context {
public:
    // ---- Factory ----

    /**
     * @brief Create a new compilation context with the given configuration.
     * @param config Configuration controlling execution mode and optimization.
     * @return A valid Context, or an invalid one if arena allocation failed.
     */
    [[nodiscard]] static Context create(const Config& config = default_config()) noexcept;

    // ---- Special member functions ----

    /** @brief Default constructor: creates an invalid (null) context. */
    Context() noexcept;

    /** @brief Destructor: releases internal DGTLContext. */
    ~Context() noexcept;

    /** @brief Move constructor: transfers ownership of the implementation. */
    Context(Context&& other) noexcept;

    /** @brief Move assignment: transfers ownership of the implementation. */
    Context& operator=(Context&& other) noexcept;

    /** @brief Deleted — contexts are non-copyable. */
    Context(const Context&) = delete;

    /** @brief Deleted — contexts are non-copyable. */
    Context& operator=(const Context&) = delete;

    // ---- Validity queries ----

    /**
     * @brief Contextually convert to bool to test validity.
     * @return true if this context holds a live DGTLContext.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /**
     * @brief Named validity check (equivalent to operator bool).
     */
    [[nodiscard]] bool is_valid() const noexcept;

    // ---- Configuration access ----

    /**
     * @brief Retrieve the configuration used to create this context.
     * @return Reference to Config (valid only when is_valid() == true).
     */
    [[nodiscard]] const Config& get_config() const noexcept;

    // ---- Internal access (friend classes only) ----

    /**
     * @brief Retrieve the internal DGTLContext pointer.
     *
     * Reserved for friend classes that implement pipeline stages.
     * Do not use from application code.
     */
    [[nodiscard]] dgtl::core::DGTLContext* get_impl() noexcept;

    /** @overload */
    [[nodiscard]] const dgtl::core::DGTLContext* get_impl() const noexcept;

private:
    friend class ExecutionEngine;
    friend class JITSession;
    friend class AOTSession;
    friend class HybridSession;
    friend class IRBuilder;

    std::unique_ptr<dgtl::core::DGTL_Arena>      arena_;
    std::unique_ptr<dgtl::core::DGTLContext>     impl_;
    Config                                        config_;

    explicit Context(std::unique_ptr<dgtl::core::DGTLContext> impl,
                     const Config& cfg) noexcept;
};

} // namespace api
} // namespace dgtl
