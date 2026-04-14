#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"     
#include "dgtl/ir/base/IR_Stream.hpp" 
#include "dgtl/core/Arena.hpp" 


    
 
#include <immintrin.h>
#include <cstring>
#include <thread>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <unordered_set>
#include <unordered_map>

#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace dgtl::ir {

/**
 * @brief Thread-Local Arena for Shadow Buffers
 * Prevents mutex contention across microservices in Project Titan.
 */
dgtl::core::DGTL_Arena local_transaction_arena(1);

// --- Internal Helper Functions (From Files 1, 2, 6) ---

static void* DGTL_aligned_alloc(size_t size, size_t alignment) {
    if (size == 0) size = alignment;
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

void DGTL_aligned_free(void* ptr) {
    if (!ptr) return;
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// 🔥 SAFE FALLBACK WRAPPER (ADDED, NOTHING REMOVED)
template<typename T>
static T* safe_allocate(dgtl::core::DGTL_Arena& arena, size_t count) {
    T* ptr = arena.allocate<T>(count);
    if (ptr) {
        return ptr;
    }

    ptr = new (std::nothrow) T[count];
    if (!ptr) {
        std::cerr << "[DGTL FATAL] Allocation failed\n";
        return nullptr;
    }
    return ptr;
}

// --- IRStream Implementation: Lifecycle (Merged from Files 1, 2, 6) ---

IRStream::IRStream(dgtl::core::DGTL_Arena& arena, size_t node_count)
    : count(0),
      _capacity((node_count > 0) ? node_count : 1024),
      atoms(nullptr),
      cores(nullptr),
      shadow_atoms(nullptr),
      shadow_cores(nullptr),
      shadow_count(0),
      in_transaction(false),
      joined_buffer_in_transaction(false),
      joined_buffer(nullptr),
      joined_valid(false),
      owns_aligned_alloc_storage(false),
      owns_joined_buffer(false)
{
    in_transaction.store(false, std::memory_order_release);
    atoms = safe_allocate<IRAtom>(arena, _capacity);
    cores = safe_allocate<IRCore>(arena, _capacity);
    shadow_atoms = safe_allocate<IRAtom>(arena, _capacity);
    shadow_cores = safe_allocate<IRCore>(arena, _capacity);
    joined_buffer = safe_allocate<IRNode>(arena, _capacity);

    if (atoms) std::memset(atoms, 0, _capacity * sizeof(IRAtom));
    if (cores) std::memset(cores, 0, _capacity * sizeof(IRCore));
    if (shadow_atoms) std::memset(shadow_atoms, 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::memset(shadow_cores, 0, _capacity * sizeof(IRCore));
    if (joined_buffer) std::memset(joined_buffer, 0, _capacity * sizeof(IRNode));
}

IRStream::IRStream(IRNode* external_buffer, size_t node_count)
    : count(external_buffer ? node_count : 0),
      _capacity((node_count > 0) ? node_count : 1024),
      atoms(nullptr),
      cores(nullptr),
      shadow_atoms(nullptr),
      shadow_cores(nullptr),
      shadow_count(count),
      in_transaction(false),
      joined_buffer_in_transaction(false),
      joined_buffer(external_buffer),
      joined_valid(false),
      owns_aligned_alloc_storage(false),
      owns_joined_buffer(false)
{
    in_transaction.store(false, std::memory_order_release);
    atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    if (!external_buffer) {
        joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64));
        owns_joined_buffer = true;
    }
    owns_aligned_alloc_storage = true;

    if (atoms) std::memset(atoms, 0, _capacity * sizeof(IRAtom));
    if (cores) std::memset(cores, 0, _capacity * sizeof(IRCore));
    if (shadow_atoms) std::memset(shadow_atoms, 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::memset(shadow_cores, 0, _capacity * sizeof(IRCore));
    if (!external_buffer && joined_buffer) std::memset(joined_buffer, 0, _capacity * sizeof(IRNode));

    if (external_buffer && atoms && cores) {
        for (size_t i = 0; i < count; ++i) {
            atoms[i].opcode = external_buffer[i].opcode;
            atoms[i].flags  = external_buffer[i].flags;
            cores[i].result_id = external_buffer[i].result_id;
            cores[i].op_a      = external_buffer[i].op_a;
            cores[i].op_b      = external_buffer[i].op_b;
            cores[i].op_c      = external_buffer[i].op_c;
            cores[i].intent_hash = external_buffer[i].intent_hash;
        }
    }
}

IRStream::~IRStream() {
    if (owns_aligned_alloc_storage) {
        DGTL_aligned_free(atoms);
        DGTL_aligned_free(cores);
        DGTL_aligned_free(shadow_atoms);
        DGTL_aligned_free(shadow_cores);
    }
    if (owns_joined_buffer) {
        DGTL_aligned_free(joined_buffer);
    }
}

IRStream::IRStream(IRStream&& other) noexcept
    : count(other.count),
      _capacity(other._capacity),
      atoms(other.atoms),
      cores(other.cores),
      shadow_atoms(other.shadow_atoms),
      shadow_cores(other.shadow_cores),
      shadow_count(other.shadow_count),
      in_transaction(other.in_transaction.load(std::memory_order_acquire)),
      joined_buffer_in_transaction(other.joined_buffer_in_transaction),
      joined_buffer(other.joined_buffer),
      joined_valid(other.joined_valid),
      owns_aligned_alloc_storage(other.owns_aligned_alloc_storage),
      owns_joined_buffer(other.owns_joined_buffer)
{
    other.count = 0;
    other._capacity = 0;
    other.atoms = nullptr;
    other.cores = nullptr;
    other.shadow_atoms = nullptr;
    other.shadow_cores = nullptr;
    other.shadow_count = 0;
    other.in_transaction.store(false, std::memory_order_release);
    other.joined_buffer_in_transaction = false;
    other.joined_buffer = nullptr;
    other.joined_valid = false;
    other.owns_aligned_alloc_storage = false;
    other.owns_joined_buffer = false;
}

IRStream& IRStream::operator=(IRStream&& other) noexcept {
    if (this != &other) {
        if (owns_aligned_alloc_storage) {
            DGTL_aligned_free(atoms);
            DGTL_aligned_free(cores);
            DGTL_aligned_free(shadow_atoms);
            DGTL_aligned_free(shadow_cores);
        }
        if (owns_joined_buffer) {
            DGTL_aligned_free(joined_buffer);
        }

        count = other.count;
        _capacity = other._capacity;
        atoms = other.atoms;
        cores = other.cores;
        shadow_atoms = other.shadow_atoms;
        shadow_cores = other.shadow_cores;
        shadow_count = other.shadow_count;
        in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);
        joined_buffer_in_transaction = other.joined_buffer_in_transaction;
        joined_buffer = other.joined_buffer;
        joined_valid = other.joined_valid;
        owns_aligned_alloc_storage = other.owns_aligned_alloc_storage;
        owns_joined_buffer = other.owns_joined_buffer;

        other.count = 0;
        other._capacity = 0;
        other.atoms = nullptr;
        other.cores = nullptr;
        other.shadow_atoms = nullptr;
        other.shadow_cores = nullptr;
        other.shadow_count = 0;
        other.in_transaction.store(false, std::memory_order_release);
        other.joined_buffer_in_transaction = false;
        other.joined_buffer = nullptr;
        other.joined_valid = false;
        other.owns_aligned_alloc_storage = false;
        other.owns_joined_buffer = false;
    }
    return *this;
}

// --- Move Semantics (From Files 1, 6) ---
// --- Allocation & Copying (From File 2, 4) ---

IRStream IRStream::allocate_transactional(size_t size) {
    return IRStream(local_transaction_arena, size);
}

IRStream IRStream::allocate_shadow(size_t size) {
    return IRStream(local_transaction_arena, size);
}

void IRStream::copy_from(const IRStream& other) {
    if (!atoms || !cores || !other.atoms || !other.cores) {
        return;
    }

    const bool src_in_transaction = other.in_transaction.load(std::memory_order_acquire);
    const IRAtom* src_atoms = src_in_transaction ? other.shadow_atoms : other.atoms;
    const IRCore* src_cores = src_in_transaction ? other.shadow_cores : other.cores;
    const size_t src_count = src_in_transaction ? other.shadow_count : other.count;

    if (!src_atoms || !src_cores) {
        count = 0;
        shadow_count = 0;
        joined_valid = false;
        return;
    }

    count = std::min(_capacity, src_count);
    shadow_count = count;
    std::memcpy(atoms, src_atoms, count * sizeof(IRAtom));
    std::memcpy(cores, src_cores, count * sizeof(IRCore));

    if (shadow_atoms && shadow_cores) {
        std::memcpy(shadow_atoms, atoms, count * sizeof(IRAtom));
        std::memcpy(shadow_cores, cores, count * sizeof(IRCore));
    }
    joined_valid = false;
}

IRStream IRStream::clone() const {
    bool src_in_trans = this->in_transaction.load(std::memory_order_acquire);
    size_t target_count = src_in_trans ? this->shadow_count : this->count;

    size_t new_capacity = std::max(this->_capacity, target_count);
    if (new_capacity == 0) new_capacity = 1024;

    IRStream cloned(nullptr, new_capacity);

    cloned.count = target_count;
    cloned.shadow_count = target_count;
    cloned.in_transaction.store(false, std::memory_order_release);

    const auto* src_atoms = src_in_trans ? this->shadow_atoms : this->atoms;
    const auto* src_cores = src_in_trans ? this->shadow_cores : this->cores;

    if (src_atoms && src_cores && target_count > 0 && cloned.atoms && cloned.cores) {
        size_t safe_count = std::min(target_count, cloned._capacity);
        std::memcpy(cloned.atoms, src_atoms, safe_count * sizeof(IRAtom));
        std::memcpy(cloned.cores, src_cores, safe_count * sizeof(IRCore));

        if (cloned.joined_buffer) {
            for (size_t i = 0; i < safe_count; ++i) {
                auto& dst = cloned.joined_buffer[i];
                const auto& a = cloned.atoms[i];
                const auto& c = cloned.cores[i];
                dst.opcode      = a.opcode;
                dst.flags       = a.flags;
                dst.result_id   = c.result_id;
                dst.op_a         = c.op_a;
                dst.op_b         = c.op_b;
                dst.op_c         = c.op_c;
                dst.intent_hash = c.intent_hash;
            }
        }
    }
    return cloned;
}

// --- Transaction Management & State (From Files 1, 6) ---

void IRStream::begin_transaction() noexcept {
    joined_valid = false;
    if (in_transaction.exchange(true, std::memory_order_acq_rel)) {
        shadow_count = std::min(count, _capacity);
        return;
    }
    shadow_count = std::min(count, _capacity);
    if (shadow_atoms && atoms && count > 0) {
        std::memcpy(shadow_atoms, atoms, count * sizeof(IRAtom));
        std::memcpy(shadow_cores, cores, count * sizeof(IRCore));
    }
}

void IRStream::commit_transaction() noexcept {
    if (!in_transaction.load(std::memory_order_acquire)) return;
    count = shadow_count;
    if (count > 0 && atoms && shadow_atoms && cores && shadow_cores) {
        std::memcpy(atoms, shadow_atoms, count * sizeof(IRAtom));
        std::memcpy(cores, shadow_cores, count * sizeof(IRCore));
    }
    in_transaction.store(false, std::memory_order_release);
    joined_valid = false;
}

void IRStream::abort_transaction() noexcept {
    in_transaction.store(false, std::memory_order_release);
    shadow_count = count;
    joined_valid = false;
}

void IRStream::rollback_transaction() noexcept {
    abort_transaction();
}

void IRStream::set_count(size_t val) noexcept {
    if (val <= _capacity) [[likely]] {
        count = val;
        shadow_count = val;
        joined_valid = false;
    }
}

bool IRStream::is_null() const noexcept {
    return atoms == nullptr || cores == nullptr;
}

void IRStream::prune_shadow_buffer() noexcept {
    shadow_count = count;
    if (shadow_atoms && shadow_cores) [[likely]] {
        size_t clear_size = (_capacity - count);
        if (clear_size > 0) {
            std::memset(shadow_atoms + count, 0, clear_size * sizeof(IRAtom));
            std::memset(shadow_cores + count, 0, clear_size * sizeof(IRCore));
        }
    }
}

void IRStream::push_full_to_shadow() noexcept {
    if (!shadow_atoms || !shadow_cores || !atoms || !cores) return;
    std::memcpy(shadow_atoms, atoms, count * sizeof(IRAtom));
    std::memcpy(shadow_cores, cores, count * sizeof(IRCore));
    shadow_count = count;
}

void IRStream::commit_from_shadow(IRStream& shadow_stream) noexcept {
    if (shadow_stream.is_null()) [[unlikely]] return;
    shadow_stream.commit_transaction();

    if (!atoms || !cores || !shadow_stream.atoms || !shadow_stream.cores) {
        return;
    }

    const size_t copy_count = std::min(_capacity, shadow_stream.count);
    if (copy_count > 0) {
        std::memcpy(atoms, shadow_stream.atoms, copy_count * sizeof(IRAtom));
        std::memcpy(cores, shadow_stream.cores, copy_count * sizeof(IRCore));
        if (shadow_atoms && shadow_cores) {
            std::memcpy(shadow_atoms, atoms, copy_count * sizeof(IRAtom));
            std::memcpy(shadow_cores, cores, copy_count * sizeof(IRCore));
        }
    }

    this->count = copy_count;
    this->shadow_count = copy_count;
    this->in_transaction.store(false, std::memory_order_release);
    this->joined_valid = false;
    shadow_stream.abort_transaction();
}

void IRStream::commit_from_shadow() noexcept {
    commit_transaction();
}

} // namespace dgtl::ir
