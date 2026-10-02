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
 */

module;

#include <cstdlib>

// Global Module Fragment: Capture lightweight templates and hardware hint pre-fetches
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#include <immintrin.h>
#endif

export module dgtl.compiler_context;

// Dependency Ordered Named Module Imports
import dgtl.api.config;
import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir.stream;

// ============================================================================
// SECTION 1 Ã¢â‚¬â€ INTERNAL COMPILER CONTEXT  (dgtl::core)
// ============================================================================

export namespace dgtl::core {

using dgtl::ir::OpCode;
using dgtl::ir::INVALID_SLOT;

// ----------------------------------------------------------------------------
// P0-001: POLYHEDRAL CONFIGURATION
// ----------------------------------------------------------------------------

enum class PolyhedralMode : std::uint8_t {
    off        = 0,
    safe       = 1,
    aggressive = 2,
};

enum class PolyhedralParallelPolicy : std::uint8_t {
    auto_policy = 0,
    off         = 1,
    on          = 2,
};

struct PolyhedralConfig {
    PolyhedralMode         mode            = PolyhedralMode::off;
    PolyhedralParallelPolicy parallel_policy = PolyhedralParallelPolicy::auto_policy;
    std::uint32_t          tile_size       = 0u;

    [[nodiscard]] constexpr bool is_enabled() const noexcept {
        return mode != PolyhedralMode::off;
    }

    [[nodiscard]] constexpr bool allow_aggressive_transforms() const noexcept {
        return mode == PolyhedralMode::aggressive;
    }
};

// ----------------------------------------------------------------------------
// PILLAR 2: THREAD-LOCAL BUFFER  (64-byte cache-line aligned)
// ----------------------------------------------------------------------------

// Canonical HIR representation: staging uses the same IRAtom/IRCore/IRNode types as IRStorage.
using HIRAtom = dgtl::ir::IRAtom;
using HIRCore = dgtl::ir::IRCore;
using HIRNode = dgtl::ir::IRNode;

struct alignas(64) ThreadLocalBuffer {
    HIRAtom* base_atom_ptr       = nullptr;
    HIRCore* base_core_ptr       = nullptr;
    HIRAtom* atom_ptr            = nullptr;
    HIRCore* core_ptr            = nullptr;
    std::size_t       remaining_primary   = 0u;

    HIRAtom* base_shadow_atom_ptr = nullptr;
    HIRCore* base_shadow_core_ptr = nullptr;
    HIRAtom* shadow_atom_ptr      = nullptr;
    HIRCore* shadow_core_ptr      = nullptr;
    std::size_t       remaining_shadow     = 0u;

    HIRAtom* checkpoint_atom              = nullptr;
    HIRCore* checkpoint_core              = nullptr;
    std::size_t       checkpoint_remaining_shadow  = 0u;

    bool in_strike = false;
};

inline thread_local ThreadLocalBuffer g_tls_buffer{};

// ----------------------------------------------------------------------------
// DGTL INTERNAL CONTEXT
// ----------------------------------------------------------------------------

class alignas(64) DGTLContext {
public:
    static constexpr std::size_t PageSizeNodes = 262144u;

    explicit DGTLContext(DGTL_Arena& arena) noexcept
        : global_arena_(arena), polyhedral_config_() {}

    DGTLContext(const DGTLContext&)            = delete;
    DGTLContext& operator=(const DGTLContext&) = delete;
    DGTLContext(DGTLContext&&)                 = delete;
    DGTLContext& operator=(DGTLContext&&)      = delete;

    [[nodiscard]] const PolyhedralConfig& get_polyhedral_config() const noexcept {
        return polyhedral_config_;
    }

    void set_polyhedral_config(const PolyhedralConfig& cfg) noexcept {
        polyhedral_config_ = cfg;
    }

    void emit_node_fast(OpCode   op,
                        std::uint16_t    flg,
                        std::uint32_t    res,
                        std::uint32_t    a,
                        std::uint32_t    b,
                        std::uint32_t    c    = INVALID_SLOT,
                        std::uint64_t    hash = 0ull) noexcept
    {
        auto& tls = g_tls_buffer;

        const bool page_empty = tls.in_strike
            ? (tls.remaining_shadow  == 0u)
            : (tls.remaining_primary == 0u);

        if (page_empty) [[unlikely]] {
            if (!refresh_thread_pages()) [[unlikely]] {
                return; 
            }
        }

        HIRAtom* atom;
        HIRCore* core;

        if (tls.in_strike) {
            atom = tls.shadow_atom_ptr++;
            core = tls.shadow_core_ptr++;
            --tls.remaining_shadow;
        } else {
            atom = tls.atom_ptr++;
            core = tls.core_ptr++;
            --tls.remaining_primary;
        }

        *atom = dgtl::ir::AtomSplitter::pack(op, flg);

        core->result_id   = res;
        core->op_a        = a;
        core->op_b        = b;
        core->op_c        = c;
        core->intent_hash = (hash != 0ull) ? hash : HIRNode::calculate_hash(op, res, a, b, c);
        core->logic_flags = 0u;
        core->aux_flags   = 0u;
        core->debug_id    = 0u;
        core->epoch       = 0ull;
    }

    [[nodiscard]] HIRNode* create_node_atomic() noexcept {
        temp_node_buffer_ = HIRNode{};
        temp_node_buffer_.result_id = INVALID_SLOT;
        temp_node_buffer_.op_a = INVALID_SLOT;
        temp_node_buffer_.op_b = INVALID_SLOT;
        temp_node_buffer_.op_c = INVALID_SLOT;
        return &temp_node_buffer_;
    }

    void commit_node(const HIRNode& node) noexcept {
        emit_node_fast(node.opcode, node.flags,
                       node.result_id,
                       node.op_a, node.op_b, node.op_c,
                       node.intent_hash);
    }

    void enter_transactional_mode() noexcept {
        auto& tls = g_tls_buffer;

        if (tls.remaining_shadow == 0u && !refresh_thread_pages()) [[unlikely]] {
            tls.in_strike                    = false;
            tls.checkpoint_atom              = nullptr;
            tls.checkpoint_core              = nullptr;
            tls.checkpoint_remaining_shadow  = 0u;
            return;
        }

        tls.in_strike                   = true;
        tls.checkpoint_atom             = tls.shadow_atom_ptr;
        tls.checkpoint_core             = tls.shadow_core_ptr;
        tls.checkpoint_remaining_shadow = tls.remaining_shadow;
    }

    static void commit_transaction() noexcept {
        auto& tls    = g_tls_buffer;
        tls.in_strike    = false;
        tls.checkpoint_atom             = nullptr;
        tls.checkpoint_core             = nullptr;
        tls.checkpoint_remaining_shadow = 0u;
    }

    static void rollback_transaction() noexcept {
        auto& tls = g_tls_buffer;
        if (tls.checkpoint_atom && tls.checkpoint_core) [[likely]] {
            tls.shadow_atom_ptr  = tls.checkpoint_atom;
            tls.shadow_core_ptr  = tls.checkpoint_core;
            tls.remaining_shadow = tls.checkpoint_remaining_shadow;
        }
        tls.in_strike                   = false;
        tls.checkpoint_atom             = nullptr;
        tls.checkpoint_core             = nullptr;
        tls.checkpoint_remaining_shadow = 0u;
    }

    [[nodiscard]] static bool is_in_transaction() noexcept {
        return g_tls_buffer.in_strike;
    }

    [[nodiscard]] DGTL_Arena& get_arena() noexcept {
        return global_arena_;
    }

private:
    DGTL_Arena&      global_arena_;
    mutable HIRNode temp_node_buffer_{}; 
    PolyhedralConfig   polyhedral_config_{}; 

#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    [[nodiscard]] bool refresh_thread_pages() noexcept {
        auto& tls = g_tls_buffer;

        if (tls.remaining_primary == 0u) {
            tls.base_atom_ptr     = global_arena_.allocate<HIRAtom>(PageSizeNodes);
            tls.base_core_ptr     = global_arena_.allocate<HIRCore>(PageSizeNodes);
            tls.atom_ptr          = tls.base_atom_ptr;
            tls.core_ptr          = tls.base_core_ptr;
            tls.remaining_primary = PageSizeNodes;
        }

        if (tls.remaining_shadow == 0u) {
            tls.base_shadow_atom_ptr = global_arena_.allocate<HIRAtom>(PageSizeNodes);
            tls.base_shadow_core_ptr = global_arena_.allocate<HIRCore>(PageSizeNodes);
            tls.shadow_atom_ptr      = tls.base_shadow_atom_ptr;
            tls.shadow_core_ptr      = tls.base_shadow_core_ptr;
            tls.remaining_shadow     = PageSizeNodes;
        }

        if (!tls.atom_ptr || !tls.core_ptr || !tls.shadow_atom_ptr || !tls.shadow_core_ptr) [[unlikely]] {
            tls.remaining_primary = 0u;
            tls.remaining_shadow  = 0u;
            tls.in_strike         = false;
            return false;
        }

        return true;
    }
};

} // namespace dgtl::core

// ============================================================================
// SECTION 2 Ã¢â‚¬â€ PUBLIC API WRAPPER  (dgtl::api)
// ============================================================================

export namespace dgtl::api {

class Context {
public:
    [[nodiscard]] static Context create(const Config& config = default_config()) noexcept {
        auto arena = std::make_unique<dgtl::core::DGTL_Arena>(8u, 256u);
        auto impl = std::make_unique<dgtl::core::DGTLContext>(*arena);
        
        Context ctx;
        ctx.arena_ = std::move(arena);
        ctx.impl_ = std::move(impl);
        ctx.config_ = config;
        return ctx;
    }

    Context() noexcept : arena_(nullptr), impl_(nullptr), config_() {}
    ~Context() noexcept = default;

    Context(Context&& other) noexcept
        : arena_(std::move(other.arena_)),
          impl_(std::move(other.impl_)),
          config_(std::move(other.config_)) {}

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            arena_ = std::move(other.arena_);
            impl_ = std::move(other.impl_);
            config_ = std::move(other.config_);
        }
        return *this;
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr && arena_ != nullptr; }
    [[nodiscard]] bool is_valid() const noexcept { return impl_ != nullptr && arena_ != nullptr; }

    [[nodiscard]] const Config& get_config() const noexcept { return config_; }

    [[nodiscard]] dgtl::core::DGTLContext* get_impl() noexcept { return impl_.get(); }
    [[nodiscard]] const dgtl::core::DGTLContext* get_impl() const noexcept { return impl_.get(); }

private:
    std::unique_ptr<dgtl::core::DGTL_Arena>      arena_;
    std::unique_ptr<dgtl::core::DGTLContext>     impl_;
    Config                                       config_;

    explicit Context(std::unique_ptr<dgtl::core::DGTLContext> impl, const Config& cfg) noexcept
        : arena_(nullptr), impl_(std::move(impl)), config_(cfg) {}
};

} // namespace dgtl::api

