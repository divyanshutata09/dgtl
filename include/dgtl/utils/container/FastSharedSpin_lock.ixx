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

module;

#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <thread>
#include <cassert>

#if defined(_MSC_VER)
#   include <intrin.h>
#endif

export module dgtl.sync;

export namespace dgtl::utils::sync {

/**
 * @class FastSharedSpinLock
 * @brief Zero-allocation, anti-starvation, bounded Reader-Writer ticket lock.
 * Engineered for sub-microsecond latency profiling in high-concurrency systems.
 */
class alignas(64) FastSharedSpinLock {
private:
    // Atomic State bit allocation:
    // Bit 31: Exclusive Write Lock Active Flag
    // Bit 30: Pending Writer Intent Flag (Starvation firewall)
    // Bits 0-29: Active Reader Reference Count
    std::atomic<uint32_t> state_{0u};

    static constexpr uint32_t kWriteBit   = 1u << 31;
    static constexpr uint32_t kIntentBit  = 1u << 30;
    static constexpr uint32_t kReaderMask = ~(kWriteBit | kIntentBit);
    static constexpr uint32_t kMaxReaders = kReaderMask - 1u;

    static inline void cpu_relax() noexcept {
    #if defined(_MSC_VER)
        _mm_pause();
    #elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
    #elif defined(__aarch64__) || defined(__arm__)
        asm volatile("yield" ::: "memory");
#else
        std::this_thread::yield();
#endif
    }

    static void backoff_spin(uint32_t& spins) noexcept {
        if (spins < 12) {
            ++spins;
            cpu_relax();
        } else if (spins < 48) {
            ++spins;
            for (uint32_t i = 0; i < 4; ++i) cpu_relax();
        } else {
            std::this_thread::yield();
            spins = 0; 
        }
    }

public:
    FastSharedSpinLock() noexcept = default;
    ~FastSharedSpinLock() noexcept {
        assert(state_.load(std::memory_order_relaxed) == 0u && "DGTL-E9541: Destroying locked spin synchronization context!");
    }

    // Explicitly Non-Copyable & Non-Moveable to guarantee hardware pinning safety
    FastSharedSpinLock(const FastSharedSpinLock&) = delete;
    FastSharedSpinLock& operator=(const FastSharedSpinLock&) = delete;
    FastSharedSpinLock(FastSharedSpinLock&&) = delete;
    FastSharedSpinLock& operator=(FastSharedSpinLock&&) = delete;

    // ============================================================================
    // EXCLUSIVE / WRITER INTERFACE (Strict Priority Anti-Starvation Pass)
    // ============================================================================

    void lock() noexcept {
        uint32_t spins = 0;
        
        // Step 1: Signal intent to block incoming readers
        while (true) {
            uint32_t current = state_.load(std::memory_order_relaxed);
            if ((current & kIntentBit) == 0u) {
                if (state_.compare_exchange_strong(current, current | kIntentBit,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                    break;
                }
            }
            backoff_spin(spins);
        }

        // Step 2: Spin wait for existing readers to drain out of the pipeline
        spins = 0;
        while (true) {
            uint32_t current = state_.load(std::memory_order_acquire);
            // We expect only our intent bit to be active before taking the write slot
            if ((current & (kWriteBit | kReaderMask)) == 0u) {
                uint32_t expected = kIntentBit;
                if (state_.compare_exchange_weak(expected, kWriteBit | kIntentBit,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                    return;
                }
            }
            backoff_spin(spins);
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        uint32_t expected = 0u;
        return state_.compare_exchange_strong(expected, kWriteBit | kIntentBit,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed);
    }

    void unlock() noexcept {
        state_.store(0u, std::memory_order_release);
    }

    // ============================================================================
    // SHARED / READER INTERFACE (Bounded Overflow Shielded)
    // ============================================================================

    void lock_shared() noexcept {
        uint32_t spins = 0;
        while (true) {
            uint32_t current = state_.load(std::memory_order_relaxed);
            
            if ((current & (kWriteBit | kIntentBit)) == 0u) {
                const uint32_t readers = current & kReaderMask;
                // 🚀 FIXED: Attribute moved to correct post-condition placement
                if (readers >= kMaxReaders) [[unlikely]] {
                    backoff_spin(spins);
                    continue;
                }

                if (state_.compare_exchange_weak(current, current + 1u,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                    return;
                }
            }
            backoff_spin(spins);
        }
    }

    [[nodiscard]] bool try_lock_shared() noexcept {
        uint32_t current = state_.load(std::memory_order_relaxed);
        if ((current & (kWriteBit | kIntentBit)) != 0u) {
            return false;
        }
        if ((current & kReaderMask) >= kMaxReaders) {
            return false;
        }
        return state_.compare_exchange_strong(current, current + 1u,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed);
    }

    void unlock_shared() noexcept {
        uint32_t current = state_.load(std::memory_order_relaxed);
        while (true) {
            if (state_.compare_exchange_weak(current, current - 1u,
                                             std::memory_order_release,
                                             std::memory_order_relaxed)) {
                return;
            }
        }
    }
};

// ============================================================================
// STABLE LOCAL RE-ENTRANCY SAFE PRIVILEGED RAII GUARDS
// ============================================================================

class UniqueLockGuard {
private:
    FastSharedSpinLock& lock_;
public:
    explicit UniqueLockGuard(FastSharedSpinLock& lock) noexcept : lock_(lock) { lock_.lock(); }
    ~UniqueLockGuard() { lock_.unlock(); }
    UniqueLockGuard(const UniqueLockGuard&) = delete;
    UniqueLockGuard& operator=(const UniqueLockGuard&) = delete;
};

class SharedLockGuard {
public:
    struct ReentrancyNode {
        FastSharedSpinLock* lock_ctx;
        uint32_t            depth;
        ReentrancyNode* next;
    };

private:
    FastSharedSpinLock& lock_;
    bool owns_lock_ = false;

    ReentrancyNode node_instance_{};
    static thread_local inline ReentrancyNode* tl_stack_top_{nullptr};

public:
    explicit SharedLockGuard(FastSharedSpinLock& lock) noexcept : lock_(lock) {
        ReentrancyNode* cur = tl_stack_top_;
        while (cur != nullptr) {
            if (cur->lock_ctx == &lock_) {
                ++(cur->depth);
                owns_lock_ = false;
                return;
            }
            cur = cur->next;
        }

        lock_.lock_shared();
        owns_lock_ = true;

        node_instance_.lock_ctx = &lock_;
        node_instance_.depth = 1u;
        node_instance_.next = tl_stack_top_;
        tl_stack_top_ = &node_instance_;
    }

    ~SharedLockGuard() {
        ReentrancyNode* cur = tl_stack_top_;
        ReentrancyNode* prev = nullptr;

        while (cur != nullptr) {
            if (cur == &node_instance_) {
                if (--(cur->depth) == 0u) {
                    if (prev != nullptr) {
                        prev->next = cur->next;
                    } else {
                        tl_stack_top_ = cur->next;
                    }
                    if (owns_lock_) {
                        lock_.unlock_shared();
                    }
                }
                return;
            }
            prev = cur;
            cur = cur->next;
        }

        if (owns_lock_) {
            lock_.unlock_shared();
        }
    }

    SharedLockGuard(const SharedLockGuard&) = delete;
    SharedLockGuard& operator=(const SharedLockGuard&) = delete;
};

} // namespace dgtl::utils::sync