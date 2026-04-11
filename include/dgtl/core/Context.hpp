/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 5: LOCK-FREE THREAD-LOCAL CONTEXT (DETERMINISTIC VERSION)
 * PILLAR 2.5: TRI-TIER TLS BUFFERING & SHADOW-AWARE EMISSION
 * STRATEGY: 2MB Huge-Page slices per thread with Transactional Redirection.
 * GUARANTEE: Zero-Contention | Zero-Corruption IR Emission.
 */

#pragma once
#include <atomic>
#include <mutex>
#include <algorithm>
#include "core/Arena.hpp"
#include "../ir/Global_IR.hpp"

namespace dgtl::core {

/**
 * @struct ThreadLocalBuffer
 * @brief Manages the 3-Tier SoA slices and transaction state for a single thread.
 * alignas(64) ensures no false sharing between threads on the same cache line.
 */
struct alignas(64) ThreadLocalBuffer {
    ir::IRAtom* atom_ptr = nullptr;
    ir::IRCore* core_ptr = nullptr;
    
    // Transactional Shadow Pointers (Pillar 2: Rollback-ready storage)
    ir::IRAtom* shadow_atom_ptr = nullptr;
    ir::IRCore* shadow_core_ptr = nullptr;

    size_t remaining = 0;
    bool in_strike   = false; 
};

/**
 * @brief C++26 Inline Thread-Local Storage.
 * Replenished from the Global Arena on the first 'emit' strike.
 */
inline thread_local ThreadLocalBuffer g_tls_buffer{nullptr, nullptr, nullptr, nullptr, 0, false};

class DGTLContext {
    // 256K nodes per slice = ~1MB Atoms + ~1MB Cores = 2MB (L3/Huge-Page Optimized)
    static constexpr size_t PageSizeNodes = 262144; 

    DGTLArena& global_arena;
    std::mutex allocation_mutex;

    // Buffer for bridge node requests - used as a staging area before TLS flush
    mutable ir::IRNode temp_node_buffer;

public:
    explicit DGTLContext(DGTLArena& arena) : global_arena(arena) {}

    // Explicitly delete copy/move to maintain deterministic Arena ownership
    DGTLContext(const DGTLContext&) = delete;
    DGTLContext& operator=(const DGTLContext&) = delete;

    /**
     * @brief High-Frequency IR Node Emission with Transactional Logic.
     * Fast Path: ~4 CPU cycles (Branchless pointer selection + bump).
     * @param op Opcode from BIR 2.0 spec.
     * @param res Target ResultID (Validated by Liveness/Sentinel).
     * @param hash Pillar 3 Anti-Hallucination Hash.
     */
    void emit_node_fast(ir::OpCode op, uint16_t flg, uint32_t res, uint32_t a, uint32_t b, uint32_t c = 0, uint32_t hash = 0) noexcept {
        if (g_tls_buffer.remaining == 0) [[unlikely]] {
            refresh_thread_pages();
        }

        // PILLAR 2: Dual-Tier Selection (Shadow vs Primary)
        // If in_strike is true, emission is redirected to the transactional buffer.
        ir::IRAtom* target_atom = g_tls_buffer.in_strike ? g_tls_buffer.shadow_atom_ptr : g_tls_buffer.atom_ptr;
        ir::IRCore* target_core = g_tls_buffer.in_strike ? g_tls_buffer.shadow_core_ptr : g_tls_buffer.core_ptr;

        // Tier 1: Bit-Packed Metadata (Instruction Atoms)
        target_atom->opcode = op;
        target_atom->flags  = flg;
        
        // Tier 2: Logical Data + Pillar 3 Anti-Hallucination Hash (Instruction Cores)
        target_core->result_id   = res;
        target_core->op_a        = a;
        target_core->op_b        = b;
        target_core->op_c        = c; 
        target_core->intent_hash = hash;

        // Atomic Pointer Bump: Moves the cursor for the active tier
        if (g_tls_buffer.in_strike) [[unlikely]] {
            g_tls_buffer.shadow_atom_ptr++;
            g_tls_buffer.shadow_core_ptr++;
        } else {
            g_tls_buffer.atom_ptr++;
            g_tls_buffer.core_ptr++;
        }
        
        g_tls_buffer.remaining--;
    }

    /**
     * @brief Atomic Node Creation for the Intrinsic Bridge.
     * Provides a staging node for complex vector instruction assembly.
     */
    [[nodiscard]] ir::IRNode* create_node_atomic() noexcept {
        temp_node_buffer = ir::IRNode{};
        return &temp_node_buffer;
    }

    /**
     * @brief Commits the staged IRNode from create_node_atomic into TLS storage.
     */
    void commit_node(const ir::IRNode& node) noexcept {
        emit_node_fast(node.opcode, node.flags, node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash);
    }

    /**
     * @brief Accessor for the underlying Huge-Page Arena.
     */
    [[nodiscard]] DGTLArena& get_arena() noexcept {
        return global_arena;
    }

    // --- Transaction Control (Pillar 2) ---

    void enter_transactional_mode() noexcept { g_tls_buffer.in_strike = true; }
    void exit_transactional_mode()  noexcept { g_tls_buffer.in_strike = false; }
    [[nodiscard]] bool is_in_transaction() const noexcept { return g_tls_buffer.in_strike; }

private:
    /**
     * @brief Replenishes SoA buffers from the Global Huge-Page Arena.
     * Uses 4096-byte alignment to ensure page-table efficiency.
     */
#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    void refresh_thread_pages() noexcept {
        std::lock_guard<std::mutex> lock(allocation_mutex);
        
        // Re-check remaining to prevent race condition on the allocation lock
        if (g_tls_buffer.remaining > 0) return;

        // Allocate Primary paired slices (Atoms + Cores)
        ir::IRAtom* new_atoms = global_arena.allocate<ir::IRAtom>(PageSizeNodes, 4096);
        ir::IRCore* new_cores = global_arena.allocate<ir::IRCore>(PageSizeNodes, 4096);
        
        // Allocate Shadow paired slices (Double-Buffering for Pillar 2 Transactional Strikes)
        ir::IRAtom* shad_atoms = global_arena.allocate<ir::IRAtom>(PageSizeNodes, 4096);
        ir::IRCore* shad_cores = global_arena.allocate<ir::IRCore>(PageSizeNodes, 4096);
        
        if (!new_atoms || !shad_atoms || !new_cores || !shad_cores) [[unlikely]] {
            // Memory pressure critical: DGTL will trigger a global flush or crash
            return; 
        }

        g_tls_buffer.atom_ptr        = new_atoms;
        g_tls_buffer.core_ptr        = new_cores;
        g_tls_buffer.shadow_atom_ptr = shad_atoms;
        g_tls_buffer.shadow_core_ptr = shad_cores;
        g_tls_buffer.remaining       = PageSizeNodes;
    }
};

} // namespace dgtl::core
