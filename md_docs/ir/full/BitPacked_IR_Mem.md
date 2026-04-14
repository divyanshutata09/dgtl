#include "../../include/dgtl/ir/Global_IR.hpp"
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

// Forward declare the thread_local arena defined in BitPacked_IR_Sync.cpp
namespace dgtl::ir {
    // We need this to link with the static thread_local in the other translation unit
    // Note: In C++, thread_local objects can be externed to link across files.
    extern thread_local dgtl::core::DGTLArena local_transaction_arena;
}

namespace dgtl::ir {

// --- Internal Helper Functions ---

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

static void DGTL_aligned_free(void* ptr) {
    if (!ptr) return;
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// --- IRStream Implementation: Memory & Allocation ---

IRStream::IRStream(dgtl::core::DGTLArena& arena, size_t max_nodes) {
    _capacity = (max_nodes > 0) ? max_nodes : 1024;
    atoms = arena.allocate<IRAtom>(_capacity);
    cores = arena.allocate<IRCore>(_capacity);
    shadow_atoms = arena.allocate<IRAtom>(_capacity);
    shadow_cores = arena.allocate<IRCore>(_capacity);
    joined_buffer = arena.allocate<IRNode>(_capacity);
    owns_buffer = true; 
    
    count = 0;
    shadow_count = 0;
    in_transaction.store(false, std::memory_order_release);

    if (atoms) std::memset(atoms, 0, _capacity * sizeof(IRAtom));
    if (cores) std::memset(cores, 0, _capacity * sizeof(IRCore));
    if (shadow_atoms) std::memset(shadow_atoms, 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::memset(shadow_cores, 0, _capacity * sizeof(IRCore));
}

IRStream IRStream::allocate_transactional(size_t size) {
    // Linked to BitPacked_IR_Sync.cpp via the extern declaration above
    return IRStream(local_transaction_arena, size);
}

IRStream IRStream::allocate_shadow(size_t size) {
    static dgtl::core::DGTLArena tuner_arena(1024 * 1024 * 50); 
    return IRStream(tuner_arena, size);
}

void IRStream::copy_from(const IRStream& other) {
    if (!atoms || !other.atoms) return;
    count = std::min(_capacity, other.count);
    shadow_count = count;
    std::memcpy(atoms, other.atoms, count * sizeof(IRAtom));
    std::memcpy(cores, other.cores, count * sizeof(IRCore));
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
                dst.op_a        = c.op_a;
                dst.op_b        = c.op_b;
                dst.op_c        = c.op_c;
                dst.intent_hash = c.intent_hash;
            }
        }
    }
    return cloned;
}

} // namespace dgtl::ir
