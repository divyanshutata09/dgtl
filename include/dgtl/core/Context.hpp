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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0
 * STEP 5: LOCK-FREE THREAD-LOCAL CONTEXT
 * VERSION 4.2: BIR 1.0.0 PACKED ATOM + V4-ARENA INTEGRATED
 * PILLAR 2: TRUE TLS + SHADOW-BUFFERED TRANSACTIONS
 * Extension: P0-001 Polyhedral Configuration
 */

#pragma once
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <cstdint>
#include "dgtl/core/Arena.hpp"   // DGTL_ArenaV4
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::core {

// ================================
// ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¹ POLYHEDRAL CONFIGURATION (P0-001)
// ================================

/**
 * @enum PolyhedralMode
 * @brief Control level for affine SCoP detection and transformation.
 */
enum class PolyhedralMode : uint8_t {
    off = 0,        // Disable all polyhedral analysis and transforms
    safe = 1,       // Enable safe transforms with strict legality checking
    aggressive = 2, // Enable aggressive transforms with profitability gates
};

/**
 * @enum PolyhedralParallelPolicy
 * @brief Parallel lowering strategy for affine loop nests.
 */
enum class PolyhedralParallelPolicy : uint8_t {
    auto_policy = 0, // Auto-decide based on loop depth and region size
    off = 1,         // Disable parallel lowering
    on = 2,          // Force parallel lowering when legal
};

/**
 * @struct PolyhedralConfig
 * @brief Runtime configuration for polyhedral parallelization features.
 * Populated from CLI flags and passed through the optimization pipeline.
 */
struct PolyhedralConfig {
    PolyhedralMode mode = PolyhedralMode::off;
    PolyhedralParallelPolicy parallel_policy = PolyhedralParallelPolicy::auto_policy;
    uint32_t tile_size = 0;  // 0 means auto-select via heuristic
    
    bool is_enabled() const noexcept {
        return mode != PolyhedralMode::off;
    }
    
    bool allow_aggressive_transforms() const noexcept {
        return mode == PolyhedralMode::aggressive;
    }
};

// ================================
// ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¹ THREAD LOCAL BUFFER
// ================================
struct alignas(64) ThreadLocalBuffer {
    // ===== PRIMARY (Tier 1 & 2) =====
    ir::IRAtom* base_atom_ptr = nullptr;
    ir::IRCore* base_core_ptr = nullptr;

    ir::IRAtom* atom_ptr = nullptr;
    ir::IRCore* core_ptr = nullptr;

    size_t remaining_primary = 0;

    // ===== SHADOW (Tier 1 & 2) =====
    ir::IRAtom* base_shadow_atom_ptr = nullptr;
    ir::IRCore* base_shadow_core_ptr = nullptr;

    ir::IRAtom* shadow_atom_ptr = nullptr;
    ir::IRCore* shadow_core_ptr = nullptr;

    size_t remaining_shadow = 0;

    // ===== CHECKPOINT =====
    ir::IRAtom* checkpoint_atom = nullptr;
    ir::IRCore* checkpoint_core = nullptr;
    size_t checkpoint_remaining_shadow = 0;

    bool in_strike = false;
};

inline thread_local ThreadLocalBuffer g_tls_buffer{};

// ================================
// ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¹ CONTEXT
// ================================
class DGTLContext {
public:
    static constexpr size_t PageSizeNodes = 262144;

private:
    DGTL_Arena& global_arena;

    // Local buffer for atomic node construction before emission
    mutable ir::IRNode temp_node_buffer;

    // === P0-001: POLYHEDRAL CONFIGURATION ===
    PolyhedralConfig polyhedral_config;

public:
    explicit DGTLContext(DGTL_Arena& arena) noexcept
        : global_arena(arena), polyhedral_config() {}

    DGTLContext(const DGTLContext&) = delete;
    DGTLContext& operator=(const DGTLContext&) = delete;

    // =========================================
    // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¹ POLYHEDRAL CONFIGURATION ACCESS (P0-001)
    // =========================================
    [[nodiscard]] const PolyhedralConfig& get_polyhedral_config() const noexcept {
        return polyhedral_config;
    }

    void set_polyhedral_config(const PolyhedralConfig& config) noexcept {
        polyhedral_config = config;
    }

    // =========================================
    // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¥ FAST EMIT
    // =========================================
    void emit_node_fast(ir::OpCode op, uint16_t flg,
                        uint32_t res, uint32_t a, uint32_t b,
                        uint32_t c = 0, uint64_t hash = 0) noexcept
    {
        auto& tls = g_tls_buffer;

        if ((!tls.in_strike && tls.remaining_primary == 0) ||
            (tls.in_strike && tls.remaining_shadow == 0)) [[unlikely]]
        {
            if (!refresh_thread_pages()) {
                return;
            }
        }

        ir::IRAtom* atom;
        ir::IRCore* core;

        if (tls.in_strike) {
            atom = tls.shadow_atom_ptr++;
            core = tls.shadow_core_ptr++;
            tls.remaining_shadow--;
        } else {
            atom = tls.atom_ptr++;
            core = tls.core_ptr++;
            tls.remaining_primary--;
        }

        // Pack opcode and flags into the uint32_t IRAtom (BIR 1.0.0 Standard)
        *atom = ir::AtomSplitter::pack(op, flg);

        core->result_id   = res;
        core->op_a        = a;
        core->op_b        = b;
        core->op_c        = c;
        core->intent_hash = (hash != 0)
            ? hash
            : ir::IRNode::calculate_hash(op, res, a, b, c);
    }

    // =========================================
    // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¹ ATOMIC NODE API
    // =========================================
    [[nodiscard]] ir::IRNode* create_node_atomic() noexcept {
        temp_node_buffer = ir::IRNode{};
        return &temp_node_buffer;
    }

    void commit_node(const ir::IRNode& node) noexcept {
        emit_node_fast(node.opcode, node.flags,
                       node.result_id,
                       node.op_a, node.op_b, node.op_c,
                       node.intent_hash);
    }

    // =========================================
    // ÃƒÂ°Ã…Â¸Ã¢â‚¬ÂÃ‚Â¥ TRANSACTIONS
    // =========================================
    void enter_transactional_mode() noexcept {
        auto& tls = g_tls_buffer;

        // Ensure shadow pages exist before checkpointing so rollback always has a valid restore point.
        if (tls.remaining_shadow == 0 && !refresh_thread_pages()) {
            tls.in_strike = false;
            tls.checkpoint_atom = nullptr;
            tls.checkpoint_core = nullptr;
            tls.checkpoint_remaining_shadow = 0;
            return;
        }

        tls.in_strike = true;
        tls.checkpoint_atom = tls.shadow_atom_ptr;
        tls.checkpoint_core = tls.shadow_core_ptr;
        tls.checkpoint_remaining_shadow = tls.remaining_shadow;
    }

    void commit_transaction() noexcept {
        auto& tls = g_tls_buffer;
        tls.in_strike = false;
        tls.checkpoint_atom = nullptr;
        tls.checkpoint_core = nullptr;
        tls.checkpoint_remaining_shadow = 0;
    }

    void rollback_transaction() noexcept {
        auto& tls = g_tls_buffer;
        if (tls.checkpoint_atom && tls.checkpoint_core) {
            tls.shadow_atom_ptr = tls.checkpoint_atom;
            tls.shadow_core_ptr = tls.checkpoint_core;
            tls.remaining_shadow = tls.checkpoint_remaining_shadow;
        }
        tls.in_strike = false;
        tls.checkpoint_atom = nullptr;
        tls.checkpoint_core = nullptr;
        tls.checkpoint_remaining_shadow = 0;
    }

    [[nodiscard]] bool is_in_transaction() const noexcept {
        return g_tls_buffer.in_strike;
    }

    [[nodiscard]] DGTL_Arena& get_arena() noexcept { 
        return global_arena;
    }

private:

#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    [[nodiscard]] bool refresh_thread_pages() noexcept {
        auto& tls = g_tls_buffer;

        // ===== PRIMARY =====
        if (tls.remaining_primary == 0) {
            tls.base_atom_ptr = global_arena.allocate<ir::IRAtom>(PageSizeNodes);
            tls.base_core_ptr = global_arena.allocate<ir::IRCore>(PageSizeNodes);

            tls.atom_ptr = tls.base_atom_ptr;
            tls.core_ptr = tls.base_core_ptr;

            tls.remaining_primary = PageSizeNodes;
        }

        // ===== SHADOW =====
        if (tls.remaining_shadow == 0) {
            tls.base_shadow_atom_ptr = global_arena.allocate<ir::IRAtom>(PageSizeNodes);
            tls.base_shadow_core_ptr = global_arena.allocate<ir::IRCore>(PageSizeNodes);

            // ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ FIX: Corrected variable name from base_shadow_shadow_atom_ptr
            tls.shadow_atom_ptr = tls.base_shadow_atom_ptr; 
            tls.shadow_core_ptr = tls.base_shadow_core_ptr;

            tls.remaining_shadow = PageSizeNodes;
        }

        if (!tls.atom_ptr || !tls.core_ptr ||
            !tls.shadow_atom_ptr || !tls.shadow_core_ptr)
        {
            tls.remaining_primary = 0;
            tls.remaining_shadow = 0;
            tls.in_strike = false;
            return false;
        }

        return true;
    }
};

} // namespace dgtl::core

