/* * DGTL-MLIR-X v1.0
 * STEP 5: LOCK-FREE THREAD-LOCAL CONTEXT
 * VERSION 4.2: BIR 2.1 PACKED ATOM + V4-ARENA INTEGRATED
 * PILLAR 2: TRUE TLS + SHADOW-BUFFERED TRANSACTIONS
 * FIX: Resolved shadow pointer naming collision in refresh logic.
 */

#pragma once
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include "dgtl/core/Arena.hpp"   // DGTL_ArenaV4
#include "dgtl/ir/base/Global_IR.hpp"

namespace dgtl::core {

// ================================
// 🔹 THREAD LOCAL BUFFER
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
// 🔹 CONTEXT
// ================================
class DGTLContext {
public:
    static constexpr size_t PageSizeNodes = 262144;

private:
    DGTL_Arena& global_arena;

    // Local buffer for atomic node construction before emission
    mutable ir::IRNode temp_node_buffer;

public:
    explicit DGTLContext(DGTL_Arena& arena) noexcept
        : global_arena(arena) {}

    DGTLContext(const DGTLContext&) = delete;
    DGTLContext& operator=(const DGTLContext&) = delete;

    // =========================================
    // 🔥 FAST EMIT
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

        // Pack opcode and flags into the uint32_t IRAtom (BIR 2.1 Standard)
        *atom = ir::AtomSplitter::pack(op, flg);

        core->result_id   = res;
        core->op_a        = a;
        core->op_b        = b;
        core->op_c        = c;
        core->intent_hash = hash;
    }

    // =========================================
    // 🔹 ATOMIC NODE API
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
    // 🔥 TRANSACTIONS
    // =========================================
    void enter_transactional_mode() noexcept {
        auto& tls = g_tls_buffer;
        tls.in_strike = true;
        tls.checkpoint_atom = tls.shadow_atom_ptr;
        tls.checkpoint_core = tls.shadow_core_ptr;
        tls.checkpoint_remaining_shadow = tls.remaining_shadow;
    }

    void commit_transaction() noexcept {
        g_tls_buffer.in_strike = false;
    }

    void rollback_transaction() noexcept {
        auto& tls = g_tls_buffer;
        if (tls.checkpoint_atom && tls.checkpoint_core) {
            tls.shadow_atom_ptr = tls.checkpoint_atom;
            tls.shadow_core_ptr = tls.checkpoint_core;
            tls.remaining_shadow = tls.checkpoint_remaining_shadow;
        }
        tls.in_strike = false;
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

            // ✅ FIX: Corrected variable name from base_shadow_shadow_atom_ptr
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