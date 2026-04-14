#include "../../include/dgtl/ir/Global_IR.hpp"
#include <cstring>
#include <atomic>
#include <iostream>

#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace dgtl::ir {

// --- Aligned Allocation Helpers ---
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

// --- Lifecycle ---
IRStream::IRStream(dgtl::core::DGTLArena& arena, size_t node_count) {
    _capacity = (node_count > 0) ? node_count : 1024;
    count = 0;
    shadow_count = 0;
    owns_buffer = false; 
    in_transaction.store(false, std::memory_order_release); 
    
    atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64));

    if (shadow_atoms) std::memset(shadow_atoms, 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::memset(shadow_cores, 0, _capacity * sizeof(IRCore));
    if (joined_buffer) std::memset(joined_buffer, 0, _capacity * sizeof(IRNode));
}

IRStream::~IRStream() {
    if (!owns_buffer) {
        DGTL_aligned_free(atoms);
        DGTL_aligned_free(cores);
    }
    DGTL_aligned_free(shadow_atoms);
    DGTL_aligned_free(shadow_cores);
    DGTL_aligned_free(joined_buffer);
}

// --- Move Semantics ---
IRStream::IRStream(IRStream&& other) noexcept 
    : count(other.count), atoms(other.atoms), cores(other.cores),
      shadow_atoms(other.shadow_atoms), shadow_cores(other.shadow_cores),
      shadow_count(other.shadow_count), _capacity(other._capacity),
      joined_buffer(other.joined_buffer), owns_buffer(other.owns_buffer) {
    in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);
    other.atoms = nullptr; other.cores = nullptr; 
    other.shadow_atoms = nullptr; other.shadow_cores = nullptr;
    other.joined_buffer = nullptr;
    other.count = 0; other.shadow_count = 0; other._capacity = 0;
}

IRStream& IRStream::operator=(IRStream&& other) noexcept {
    if (this != &other) {
        if (!owns_buffer) {
            DGTL_aligned_free(atoms);
            DGTL_aligned_free(cores);
        }
        DGTL_aligned_free(shadow_atoms);
        DGTL_aligned_free(shadow_cores);
        DGTL_aligned_free(joined_buffer);

        count = other.count; atoms = other.atoms; cores = other.cores;
        shadow_atoms = other.shadow_atoms; shadow_cores = other.shadow_cores;
        shadow_count = other.shadow_count; _capacity = other._capacity;
        joined_buffer = other.joined_buffer; owns_buffer = other.owns_buffer;
        in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);

        other.atoms = nullptr; other.cores = nullptr;
        other.shadow_atoms = nullptr; other.shadow_cores = nullptr;
        other.joined_buffer = nullptr;
        other.count = 0; other.shadow_count = 0; other._capacity = 0;
    }
    return *this;
}

// --- Transaction Management ---
void IRStream::begin_transaction() noexcept {
    if (in_transaction.exchange(true, std::memory_order_acq_rel)) {
        shadow_count = count; 
        return;
    }
    shadow_count = count; 
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
}

void IRStream::abort_transaction() noexcept { 
    shadow_count = count;
    in_transaction.store(false, std::memory_order_release); 
}

// Helper: Required by your emission logic
void IRStream::push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h) noexcept {
    if (count >= _capacity) return;
    bool trans = in_transaction.load(std::memory_order_acquire);
    size_t& idx = trans ? shadow_count : count;
    IRAtom* t_atoms = trans ? shadow_atoms : atoms;
    IRCore* t_cores = trans ? shadow_cores : cores;
    t_atoms[idx] = { op, fl };
    t_cores[idx] = { res, a, b, c, h };
    idx++;
}

} // namespace dgtl::ir