/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 4: IR STREAM CORE IMPLEMENTATION, iterator access, and hot path operations
 * ., transantion gaurd: it has three cpp files, IR_Stream_Core 
 * (lifecycle, allocation), 
 * IR_Stream_Operations (emit, access), and 
 * IR_Stream_Optimizations (passes)
 * 
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once


#include "IR_Types.hpp"
#include "IR_Nodes.hpp"
#include "IR_Traits.hpp"
#include "IR_Storage.hpp"
#include "dgtl/core/Arena.hpp"


namespace dgtl::ir {

// ======================================================
// 🔹 IR STREAM
// ======================================================
class alignas(64) IRStream {
private:
    size_t count = 0;
    size_t _capacity = 0;

    IRAtom* atoms = nullptr;
    IRCore* cores = nullptr;

    IRAtom* shadow_atoms = nullptr;
    IRCore* shadow_cores = nullptr;

    size_t shadow_count = 0;

    std::atomic<bool> in_transaction{false};

    // 🔥 FIX: track transaction state for joined buffer
    mutable bool joined_buffer_in_transaction = false;

    // AoS fallback buffer (optional)
    mutable IRNode* joined_buffer = nullptr;
    mutable bool joined_valid = false;

    // For external buffer ownership
    bool owns_aligned_alloc_storage = false;
    bool owns_joined_buffer = false;

public:
    IRStream() = default;
    ~IRStream();

    // ======================================================
    // 🔹 ARENA CONSTRUCTOR (✅ FIXED)
    // ======================================================
    IRStream(dgtl::core::DGTL_Arena& arena, size_t node_count);
    IRStream(IRNode* external_buffer, size_t node_count);
    IRStream(IRStream&& other) noexcept;
    IRStream& operator=(IRStream&& other) noexcept;

    // ======================================================
    // 🔹 BASICS
    // ======================================================
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] size_t capacity() const noexcept { return _capacity; }
    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] uint64_t get_version() const noexcept { return generate_logic_hash(); }

    void set_count(size_t v) noexcept;
    void reset() noexcept;

    // ======================================================
    // 🔹 ACCESS
    // ======================================================
    [[nodiscard]] IRNode get_node(size_t idx) const noexcept;
    [[nodiscard]] IRNode get_node_readonly(size_t idx) const noexcept { return get_node(idx); }

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
    void push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h = 0) noexcept;
    void push_back(const IRNode& node) noexcept;

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
    // 🔹 BUFFER ACCESS
    // ======================================================
    [[nodiscard]] IRNode* get_buffer_ptr() const noexcept;

    // ======================================================
    // 🔹 CLONING & COPYING
    // ======================================================
    static IRStream allocate_shadow(size_t size);
    void copy_from(const IRStream& other);
    IRStream clone() const;

    // ======================================================
    // 🔹 VALIDATION
    // ======================================================
    [[nodiscard]] bool validate_shadow_integrity() const noexcept;
    [[nodiscard]] uint64_t generate_logic_hash() const noexcept;

    // ======================================================
    // 🔹 OPTIMIZATION PASSES
    // ======================================================
    void push_shadow_fold(const IRNode& node) noexcept;
    void perform_dead_code_elimination() noexcept;
    void perform_gvn() noexcept;

    // ======================================================
    // 🔹 UTILITY
    // ======================================================
    void set_shadow_count(size_t val) noexcept;
    void update_shadow_size(size_t s) noexcept;
    [[nodiscard]] uint32_t intent_crc() const noexcept;
    [[nodiscard]] uint32_t get_max_id() const noexcept;

    // ======================================================
    // 🔹 EVOLUTION
    // ======================================================
    void evolve_node(IRNode* node);

    // ======================================================
    // 🔹 RAW ACCESS (for cost models)
    // ======================================================
    [[nodiscard]] const IRAtom* raw_atoms() const noexcept { return atoms; }
    [[nodiscard]] const IRCore* raw_cores() const noexcept { return cores; }
    [[nodiscard]] IRAtom* raw_atoms() noexcept { return in_transaction.load() ? shadow_atoms : atoms; }
    [[nodiscard]] IRCore* raw_cores() noexcept { return in_transaction.load() ? shadow_cores : cores; }

    // ======================================================
    // 🔹 TRANSACTION STATUS
    // ======================================================
    [[nodiscard]] bool is_active_transaction() const noexcept { return in_transaction.load(std::memory_order_acquire); }

    // ======================================================
    // 🔹 STATIC ALLOCATORS
    // ======================================================
    static IRStream allocate_transactional(size_t size);

    // ======================================================
    // 🔹 ITERATOR
    // ======================================================
    struct Iterator {
        const IRStream* stream;
        size_t index;

        bool operator!=(const Iterator& o) const { return index != o.index; }
        void operator++() { ++index; }

        IRNode operator*() const { return stream->get_node(index); }
    };

    Iterator end() const { return {this, count}; }
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