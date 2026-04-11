/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * ENTRY POINT: The IRStream Orchestrator
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead SoA Orchestration)
 */

#pragma once
#include <atomic>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <algorithm>

// Internal Modularized Components
#include "IR_Types.hpp"
#include "IR_Traits.hpp"
#include "IR_Node.hpp"
#include "IR_Storage.hpp"

// Arena dependency (Core Logic)
#include "../core/Arena.hpp"

namespace dgtl::ir {

// --- FORWARD DECLARATION FOR RAII COMPLIANCE ---
class IRStream;

/**
 * @struct IRTransactionGuard
 * @brief RAII Guard for transactional safety. 
 * Automatically rolls back the stream if commit() is not called.
 */
struct IRTransactionGuard {
    IRStream& stream;
    size_t start_count;
    bool committed = false;

    explicit IRTransactionGuard(IRStream& s);
    void commit();
    ~IRTransactionGuard();
};

/**
 * @class IRStream
 * @brief High-Velocity SoA Stream with Double-Buffered Shadow Validation.
 * This class serves as the primary interface for BIR manipulation. It leverages
 * the SoA (Structure of Arrays) layout for cache-optimal throughput.
 */
class alignas(64) IRStream {
public:
    /**
     * @brief Rule of Five: Copy Constructor
     * Performs a deep copy of all internal SoA buffers.
     */
    IRStream(const IRStream& other) noexcept {
        count = other.count;
        shadow_count = other.shadow_count;
        _capacity = other._capacity;
        owns_buffer = other.owns_buffer;
        in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);
        
        if (other.atoms && atoms) std::memcpy(atoms, other.atoms, _capacity * sizeof(IRAtom));
        if (other.cores && cores) std::memcpy(cores, other.cores, _capacity * sizeof(IRCore));
        if (other.shadow_atoms && shadow_atoms) std::memcpy(shadow_atoms, other.shadow_atoms, _capacity * sizeof(IRAtom));
        if (other.shadow_cores && shadow_cores) std::memcpy(shadow_cores, other.shadow_cores, _capacity * sizeof(IRCore));
    }

    /**
     * @brief Rule of Five: Copy Assignment
     */
    IRStream& operator=(const IRStream& other) noexcept {
        if (this != &other) {
            count = other.count;
            shadow_count = other.shadow_count;
            _capacity = other._capacity;
            owns_buffer = other.owns_buffer;
            in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);
            if (other.atoms && atoms) std::memcpy(atoms, other.atoms, _capacity * sizeof(IRAtom));
            if (other.cores && cores) std::memcpy(cores, other.cores, _capacity * sizeof(IRCore));
        }
        return *this;
    }

    size_t count = 0;

    /**
     * @struct Iterator
     * @brief High-performance iterator for linear instruction scanning.
     */
    struct Iterator {
        const IRStream* stream;
        size_t index;
        bool operator!=(const Iterator& other) const { return index != other.index; }
        void operator++() { ++index; }
        IRNode operator*() const;
    };

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, count}; }

private:
    // Core SoA Buffers
    IRAtom* atoms         = nullptr;
    IRCore* cores         = nullptr;
    IRAtom* shadow_atoms  = nullptr;
    IRCore* shadow_cores  = nullptr;

    size_t shadow_count   = 0; 
    size_t _capacity      = 0; 
    std::atomic<bool> in_transaction{false};

    mutable IRNode* joined_buffer = nullptr;
    bool owns_buffer = false; 

public:
    IRStream() = default;

    /**
     * @brief [PILLAR 3] Synchronized Constructor for External Buffers.
     */
    IRStream(IRNode* external_buffer, size_t node_count);

    ~IRStream();

    /**
     * @brief Rule of Five: Move Constructor
     */
    IRStream(IRStream&& other) noexcept;

    /**
     * @brief Rule of Five: Move Assignment
     * Syncs with .cpp implementation to prevent 0xC0000005 access violations.
     */
    IRStream& operator=(IRStream&& other) noexcept;

    /**
     * @brief Arena-backed Stream allocation (Pillar 1).
     */
    IRStream(dgtl::core::DGTLArena& arena, size_t max_nodes);

    static IRStream allocate_transactional(size_t size);
    static IRStream allocate_shadow(size_t size);

    void set_count(size_t val) noexcept;

    [[nodiscard]] size_t capacity() const noexcept { return _capacity; }
    size_t size() const noexcept { return count; }
    bool is_null() const noexcept { return atoms == nullptr; }

    // --- TRANSACTIONAL ENGINE ---
    [[nodiscard]] bool is_transactional() const noexcept { return in_transaction.load(std::memory_order_acquire); }
    bool is_active_transaction() const noexcept { return is_transactional(); }
    bool is_committed() const noexcept { return !is_transactional(); }

    void begin_transaction() noexcept;
    void begin_strike() noexcept { begin_transaction(); }

    /**
     * @brief Finalizes the transaction.
     * PILLAR 3: Validates the shadow buffer against the Sentinel before committing.
     * */
    void commit_transaction() noexcept;

    void commit_strike() noexcept { commit_transaction(); }

    void abort_transaction() noexcept;

    void release_transaction() noexcept { in_transaction.store(false, std::memory_order_release); }
    void rollback_transaction() noexcept;
    void rollback_strike() noexcept { rollback_transaction(); }

    // --- SHADOW MAPPINGS & DATA FLOW ---
    void prune_shadow_buffer() noexcept;
    void push_full_to_shadow() noexcept;

    /**
     * @brief PILLAR 2: Transactional Commitment from External Shadow.
     */
    void commit_from_shadow(IRStream& shadow_stream) noexcept;
    void commit_from_shadow() noexcept;

    void copy_from(const IRStream& other);

    // --- RAW BUFFER ACCESS ---
    [[nodiscard]] IRAtom* raw_atoms() const noexcept { return in_transaction.load(std::memory_order_acquire) ? shadow_atoms : atoms; }
    [[nodiscard]] IRCore* raw_cores() const noexcept { return in_transaction.load(std::memory_order_acquire) ? shadow_cores : cores; }

    [[nodiscard]] IRNode* get_buffer_ptr() const noexcept;

    // --- NODE ACCESSORS ---
    [[nodiscard]] IRNode get_node(size_t idx) const noexcept;
    [[nodiscard]] IRNode get_node_readonly(size_t idx) const noexcept { return get_node(idx); }

    [[nodiscard]] const IRCore& get_core_readonly(size_t idx) const noexcept;
    [[nodiscard]] const IRAtom& get_atom_readonly(size_t idx) const noexcept;

    [[nodiscard]] inline IRAtom& get_atom(size_t idx) noexcept {
        return in_transaction.load(std::memory_order_acquire) ? shadow_atoms[idx] : atoms[idx];
    }

    [[nodiscard]] inline IRCore& get_core(size_t idx) noexcept { 
        return in_transaction.load(std::memory_order_acquire) ? shadow_cores[idx] : cores[idx]; 
    }

    [[nodiscard]] IRNode get_shadow_node_proxy(size_t idx) const noexcept;

    // --- EMISSION ---
    void push_back(const IRNode& node) noexcept;
    void push_node(const IRNode& node) noexcept { push_back(node); }
    void push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h = 0) noexcept;

    // --- SHADOW SPECIFIC MUTATION ---
    [[nodiscard]] uint32_t reserve_shadow_slot() noexcept;
    void push_to_shadow(const IRNode& node) noexcept;
    void push_shadow_fold(const IRNode& node) noexcept;

    [[nodiscard]] inline IRAtom& get_shadow_atom(size_t idx) noexcept { return shadow_atoms[idx]; }
    [[nodiscard]] inline IRCore& get_shadow_core(size_t idx) noexcept { return shadow_cores[idx]; }
    
    void set_shadow_count(size_t val) noexcept;
    void update_shadow_size(size_t new_size) noexcept;

    // --- PERFORMANCE PASSES ---
    void perform_dead_code_elimination() noexcept;
    void perform_gvn() noexcept;

    [[nodiscard]] uint32_t intent_crc() const noexcept;
    [[nodiscard]] uint64_t generate_logic_hash() const noexcept;

    [[nodiscard]] bool validate_shadow_integrity() const noexcept;

    [[nodiscard]] uint32_t get_max_id() const noexcept;
    [[nodiscard]] IRStream clone() const;
};

// --- IMPLEMENTATION OF RAII GUARD ---
inline IRTransactionGuard::IRTransactionGuard(IRStream& s) : stream(s), start_count(s.count) {
    stream.begin_transaction();
}

inline void IRTransactionGuard::commit() {
    stream.commit_transaction();
    committed = true;
}

inline IRTransactionGuard::~IRTransactionGuard() {
    if (!committed) {
        stream.rollback_transaction();
        stream.set_count(start_count);
    }
}

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
