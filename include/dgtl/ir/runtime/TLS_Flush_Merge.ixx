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

/*
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0
 * STEP 6: TLS -> IRStream FLUSH & MERGE LAYER
 * VERSION 3.4: LINEARIZED FLUSH ACCOUNTING + CLEAN HEADER SURFACE
 */

module;

#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <chrono>
#include <thread>
#include <string_view>

export module dgtl.ir_core.runtime;

// Import native compiler sub-systems and sibling module interfaces
import dgtl.ir_core.stream;
import dgtl.ir_core.ssa;
import dgtl.compiler_context;
import dgtl.sync;     // 🚀 Natively import your low-latency spin synchronization layer
import dgtl.flat_map; // 🚀 Natively import your high-performance flat mapping layer
import dgtl.hashing;  // 🚀 FIXED: Imported your custom hashing module for zero-overhead routing visibility

export namespace dgtl::runtime {

// 🚀 PRODUCTION-GRADE COMPILE FIREWALL: Structural static-sized container wrapper
// entirely prevents standard library dynamic template leak overhead within module interfaces.
struct VerificationErrorStack {
    std::string_view errors[32];
    size_t count = 0;

    void push(std::string_view err) noexcept {
        if (count < 32) {
            errors[count++] = err;
        }
    }
};

class TLSFlushMerge {
private:
    // 🚀 MODERNIZE: Replaced heavy kernel OS tracking primitives with FastSharedSpinLock
    mutable dgtl::utils::sync::FastSharedSpinLock merge_mutex;
    std::atomic<size_t> pending_flushes{0};  // Atomic counter for pending flush operations
    std::atomic<uint64_t> flush_epoch{0};    // Epoch counter for linearization

    [[nodiscard]] static size_t pending_primary_nodes(const ::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        if (!tls.base_atom_ptr || !tls.base_core_ptr || !tls.atom_ptr || !tls.core_ptr) {
            return 0;
        }
        if (tls.atom_ptr < tls.base_atom_ptr || tls.core_ptr < tls.base_core_ptr) {
            return 0;
        }

        const size_t atom_used = static_cast<size_t>(tls.atom_ptr - tls.base_atom_ptr);
        const size_t core_used = static_cast<size_t>(tls.core_ptr - tls.base_core_ptr);
        return std::min(atom_used, core_used);
    }

    [[nodiscard]] static size_t pending_shadow_nodes(const ::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        if (!tls.base_shadow_atom_ptr || !tls.base_shadow_core_ptr || !tls.shadow_atom_ptr || !tls.shadow_core_ptr) {
            return 0;
        }
        if (tls.shadow_atom_ptr < tls.base_shadow_atom_ptr || tls.shadow_core_ptr < tls.base_shadow_core_ptr) {
            return 0;
        }

        const size_t atom_used = static_cast<size_t>(tls.shadow_atom_ptr - tls.base_shadow_atom_ptr);
        const size_t core_used = static_cast<size_t>(tls.shadow_core_ptr - tls.base_shadow_core_ptr);
        return std::min(atom_used, core_used);
    }

    static void reset_primary(::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        tls.atom_ptr = tls.base_atom_ptr;
        tls.core_ptr = tls.base_core_ptr;
        tls.remaining_primary = ::dgtl::core::DGTLContext::PageSizeNodes;
    }

    static void reset_shadow(::dgtl::core::ThreadLocalBuffer& tls) noexcept {
        tls.shadow_atom_ptr = tls.base_shadow_atom_ptr;
        tls.shadow_core_ptr = tls.base_shadow_core_ptr;
        tls.remaining_shadow = ::dgtl::core::DGTLContext::PageSizeNodes;
    }

    class PendingFlushScope {
    public:
        explicit PendingFlushScope(TLSFlushMerge& owner) noexcept
            : owner_(owner) {
            owner_.pending_flushes.fetch_add(1, std::memory_order_acq_rel);
        }

        ~PendingFlushScope() {
            owner_.pending_flushes.fetch_sub(1, std::memory_order_acq_rel);
        }

        PendingFlushScope(const PendingFlushScope&) = delete;
        PendingFlushScope& operator=(const PendingFlushScope&) = delete;

    private:
        TLSFlushMerge& owner_;
    };

    static void append_nodes(
        ir::IRStream& global_stream,
        const ir::IRAtom* atoms,
        const ir::IRCore* cores,
        size_t used
    ) noexcept {
        for (size_t i = 0; i < used; ++i) {
            global_stream.push_node(atoms[i], cores[i]);
        }
    }

    static void apply_merge_numa_locality() noexcept {
        const uint32_t nodes = dgtl::runtime::numa::get_num_nodes();
        if (nodes <= 1u) {
            return;
        }
        const uint32_t worker = static_cast<uint32_t>(
            dgtl::utils::hash(std::this_thread::get_id()));
        const uint32_t node = dgtl::runtime::numa::recommend_worker_node(worker);
        (void)dgtl::runtime::numa::pin_current_thread_to_node(node);
        (void)dgtl::runtime::numa::pin_thread_to_core(
            dgtl::runtime::numa::recommend_worker_core(worker));
    }

public:
    TLSFlushMerge() = default;
    ~TLSFlushMerge() = default;
    TLSFlushMerge(const TLSFlushMerge&) = delete;
    TLSFlushMerge& operator=(const TLSFlushMerge&) = delete;

    /**
     * @brief Flush PRIMARY buffer (normal execution)
     */
    void flush_primary(ir::IRStream& global_stream) noexcept {
        apply_merge_numa_locality();
        auto& tls = ::dgtl::core::g_tls_buffer;

        if (!tls.base_atom_ptr || !tls.base_core_ptr ||
            !tls.atom_ptr || !tls.core_ptr) {
            return;
        }

        const size_t used = pending_primary_nodes(tls);

        if (used == 0) {
            reset_primary(tls);
            return;
        }

        PendingFlushScope pending_scope(*this);
        dgtl::utils::sync::UniqueLockGuard lock(merge_mutex);

        append_nodes(
            global_stream, 
            reinterpret_cast<const ir::IRAtom*>(tls.base_atom_ptr), 
            reinterpret_cast<const ir::IRCore*>(tls.base_core_ptr), 
            used
        );
        flush_epoch.fetch_add(1, std::memory_order_release);

        reset_primary(tls);
    }

    /**
     * @brief Flush SHADOW buffer (after successful transaction)
     */
    void flush_shadow(ir::IRStream& global_stream) noexcept {
        apply_merge_numa_locality();
        auto& tls = ::dgtl::core::g_tls_buffer;

        if (!tls.base_shadow_atom_ptr || !tls.base_shadow_core_ptr ||
            !tls.shadow_atom_ptr || !tls.shadow_core_ptr) {
            return;
        }

        const size_t used = pending_shadow_nodes(tls);

        if (used == 0) {
            reset_shadow(tls);
            return;
        }

        PendingFlushScope pending_scope(*this);
        dgtl::utils::sync::UniqueLockGuard lock(merge_mutex);

        append_nodes(
            global_stream, 
            reinterpret_cast<const ir::IRAtom*>(tls.base_shadow_atom_ptr), 
            reinterpret_cast<const ir::IRCore*>(tls.base_shadow_core_ptr), 
            used
        );
        flush_epoch.fetch_add(1, std::memory_order_release);

        reset_shadow(tls);
    }    
    
    /**
     * @brief Take an atomic snapshot of pending flushes (lock-free)
     * @return Number of outstanding flush operations
     */
    [[nodiscard]] size_t pending_flush_count() const noexcept {
        return pending_flushes.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get current epoch counter for linearization point detection
     * @return Current flush epoch value
     */
    [[nodiscard]] uint64_t get_flush_epoch() const noexcept {
        return flush_epoch.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Wait until all pending flushes complete (busy-wait with backoff)
     * @param timeout_ns Timeout in nanoseconds (0 = infinite)
     * @return true if all flushes completed, false if timeout
     */
    [[nodiscard]] bool wait_pending_flushes(uint64_t timeout_ns = 0) noexcept {
        const auto start = std::chrono::steady_clock::now();
        uint32_t spin_count = 0;
        
        while (pending_flushes.load(std::memory_order_acquire) > 0) {
            if (timeout_ns > 0) {
                const auto elapsed = std::chrono::steady_clock::now() - start;
                if (std::chrono::nanoseconds(elapsed).count() > static_cast<int64_t>(timeout_ns)) {
                    return false;
                }
            }

            if (spin_count < 64) {
                ++spin_count;
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
        return true;
    }
    
    /**
     * @brief Concurrent-safe merge with a single linearization point.
     */
    void merge_concurrent(ir::IRStream& global_stream) noexcept {
        apply_merge_numa_locality();
        auto& tls = ::dgtl::core::g_tls_buffer;

        const size_t primary_used = pending_primary_nodes(tls);
        const size_t shadow_used = pending_shadow_nodes(tls);

        if (primary_used == 0 && shadow_used == 0) {
            reset_primary(tls);
            reset_shadow(tls);
            return;
        }

        PendingFlushScope pending_scope(*this);
        dgtl::utils::sync::UniqueLockGuard lock(merge_mutex);

        if (primary_used > 0) {
            append_nodes(
                global_stream, 
                reinterpret_cast<const ir::IRAtom*>(tls.base_atom_ptr), 
                reinterpret_cast<const ir::IRCore*>(tls.base_core_ptr), 
                primary_used
            );
        }
        if (shadow_used > 0) {
            append_nodes(
                global_stream, 
                reinterpret_cast<const ir::IRAtom*>(tls.base_shadow_atom_ptr), 
                reinterpret_cast<const ir::IRCore*>(tls.base_shadow_core_ptr), 
                shadow_used
            );
        }

        flush_epoch.fetch_add(1, std::memory_order_release);
        reset_primary(tls);
        reset_shadow(tls);
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    /**
     * @brief Update epoch counter (called after each merge)
     */
    void advance_epoch() noexcept {
        flush_epoch.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] VerificationErrorStack
    verify_generated_result_type_annotations(const ir::IRStream& stream) const {
        VerificationErrorStack error_stack;
        auto raw_errors = dgtl::ir::ssa::verify_generated_result_type_annotations(stream);
        for (size_t i = 0; i < raw_errors.count; ++i) {
            error_stack.push(raw_errors.errors[i]);
        }
        return error_stack;
    }
};

} // namespace dgtl::runtime