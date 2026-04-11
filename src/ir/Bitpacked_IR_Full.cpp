#include "../../include/dgtl/ir/Global_IR.hpp"
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
thread_local dgtl::core::DGTLArena local_transaction_arena(1024 * 1024 * 64);

// From File 2: Extern declaration for linking across units
extern thread_local dgtl::core::DGTLArena local_transaction_arena;

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

// --- IRStream Iterator (From File 6) ---

IRNode IRStream::Iterator::operator*() const { 
    return stream->get_node(index); 
}

// --- IRStream Implementation: Lifecycle (Merged from Files 1, 2, 6) ---

IRStream::IRStream(dgtl::core::DGTLArena& arena, size_t node_count) {
    _capacity = (node_count > 0) ? node_count : 1024;
    count = 0;
    shadow_count = 0;
    in_transaction.store(false, std::memory_order_release); 

    // Logic from File 2 (Direct Arena Allocation)
    // Using a pointer-based check to satisfy G++ 15 while maintaining your logic branch
    dgtl::core::DGTLArena* arena_ptr = &arena;
    if (arena_ptr != nullptr) {
        atoms = arena.allocate<IRAtom>(_capacity);
        cores = arena.allocate<IRCore>(_capacity);
        shadow_atoms = arena.allocate<IRAtom>(_capacity);
        shadow_cores = arena.allocate<IRCore>(_capacity);
        joined_buffer = arena.allocate<IRNode>(_capacity);
        owns_buffer = true; 
    } 
    // Logic from File 1 & 6 (Aligned Memory Allocation)
    else {
        atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
        cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
        shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
        shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
        joined_buffer = static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64));
        owns_buffer = false; 
    }

    // Zero-initialization from File 1, 2, 6
    if (atoms) std::memset(atoms, 0, _capacity * sizeof(IRAtom));
    if (cores) std::memset(cores, 0, _capacity * sizeof(IRCore));
    if (shadow_atoms) std::memset(shadow_atoms, 0, _capacity * sizeof(IRAtom));
    if (shadow_cores) std::memset(shadow_cores, 0, _capacity * sizeof(IRCore));
    if (joined_buffer) std::memset(joined_buffer, 0, _capacity * sizeof(IRNode));
}

// Implementation for External Buffer matching Header Line 123
IRStream::IRStream(IRNode* external_buffer, size_t node_count) {
    _capacity = (node_count > 0) ? node_count : 1024;
    count = 0;
    shadow_count = 0;
    owns_buffer = false;
    in_transaction.store(false, std::memory_order_release);

    atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    shadow_atoms = static_cast<IRAtom*>(DGTL_aligned_alloc(_capacity * sizeof(IRAtom), 64));
    shadow_cores = static_cast<IRCore*>(DGTL_aligned_alloc(_capacity * sizeof(IRCore), 64));
    joined_buffer = external_buffer ? external_buffer : static_cast<IRNode*>(DGTL_aligned_alloc(_capacity * sizeof(IRNode), 64));

    if (atoms) std::memset(atoms, 0, _capacity * sizeof(IRAtom));
    if (cores) std::memset(cores, 0, _capacity * sizeof(IRCore));
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

// --- Move Semantics (From Files 1, 6) ---

IRStream::IRStream(IRStream&& other) noexcept 
    : count(other.count), atoms(other.atoms), cores(other.cores),
      shadow_atoms(other.shadow_atoms), shadow_cores(other.shadow_cores),
      shadow_count(other.shadow_count), _capacity(other._capacity),
      joined_buffer(other.joined_buffer), owns_buffer(other.owns_buffer)
{
    in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);
    other.atoms = nullptr; other.cores = nullptr; 
    other.shadow_atoms = nullptr; other.shadow_cores = nullptr;
    other.joined_buffer = nullptr;
    other.count = 0;
    other.shadow_count = 0;
    other._capacity = 0;
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

        count = other.count;
        atoms = other.atoms;
        cores = other.cores;
        shadow_atoms = other.shadow_atoms;
        shadow_cores = other.shadow_cores;
        shadow_count = other.shadow_count;
        _capacity = other._capacity;
        joined_buffer = other.joined_buffer;
        owns_buffer = other.owns_buffer;
        in_transaction.store(other.in_transaction.load(std::memory_order_acquire), std::memory_order_release);

        other.atoms = nullptr; other.cores = nullptr;
        other.shadow_atoms = nullptr; other.shadow_cores = nullptr;
        other.joined_buffer = nullptr;
        other.count = 0;
        other.shadow_count = 0;
        other._capacity = 0;
    }
    return *this;
}

// --- Allocation & Copying (From File 2, 4) ---

IRStream IRStream::allocate_transactional(size_t size) {
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

    // Use the external_buffer constructor with nullptr to force aligned_alloc path
    // This removes the illegal null-reference dereference from the previous version
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

void IRStream::rollback_transaction() noexcept {
    abort_transaction();
}

void IRStream::set_count(size_t val) noexcept {
    if (val <= _capacity) [[likely]] {
        count = val;
        shadow_count = val;
    }
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
    std::swap(this->atoms, shadow_stream.atoms);
    std::swap(this->cores, shadow_stream.cores);
    this->count = shadow_stream.count;
    this->shadow_count = shadow_stream.count;
    this->_capacity = shadow_stream._capacity;
    this->in_transaction.store(false, std::memory_order_release);
    shadow_stream.abort_transaction();
}

void IRStream::commit_from_shadow() noexcept {
    commit_transaction();
}

// --- Hot Path Operations (From File 3) ---

void IRStream::push_back(const IRNode& node) noexcept {
    uint32_t final_hash = (node.intent_hash == 0) ? 
        IRNode::calculate_hash(node.opcode, node.op_a, node.op_b, node.op_c) : node.intent_hash;

    if (in_transaction.load(std::memory_order_acquire)) { 
        uint32_t idx = reserve_shadow_slot();
        if (idx == 0xFFFFFFFF || !shadow_atoms || !shadow_cores) return;
        
        shadow_atoms[idx] = {node.opcode, node.flags};
        shadow_cores[idx] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash};
    }
    else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] {
            std::cerr << "[DGTL FATAL] IRStream Capacity Overflow at " << _capacity << "\n";
            return;
        }
        atoms[count].opcode = node.opcode;
        atoms[count].flags = node.flags;
        cores[count] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash};
        count++;
        shadow_count = count;
    }
}

void IRStream::push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h) noexcept {
    uint32_t sync_hash = (h == 0) ? IRNode::calculate_hash(op, a, b, c) : h;
    if (count >= _capacity) return;

    bool trans = in_transaction.load(std::memory_order_acquire);
    size_t& idx = trans ? shadow_count : count;
    IRAtom* t_atoms = trans ? shadow_atoms : atoms;
    IRCore* t_cores = trans ? shadow_cores : cores;

    if (t_atoms && t_cores) {
        t_atoms[idx] = { op, fl };
        t_cores[idx] = { res, a, b, c, sync_hash };
        idx++;
        if (!trans) shadow_count = count;
    }
}

uint32_t IRStream::reserve_shadow_slot() noexcept {
    if (shadow_count >= _capacity || !shadow_atoms) [[unlikely]] {
        std::cerr << "[DGTL FATAL] IRStream Shadow Overflow at " << _capacity << "\n";
        return 0xFFFFFFFF;
    }
    uint32_t idx = static_cast<uint32_t>(shadow_count);
    shadow_count++; 
    return idx;
}

void IRStream::push_to_shadow(const IRNode& node) noexcept {
    if (!in_transaction.load(std::memory_order_acquire)) {
        begin_transaction();
    }
    push_back(node);
}

// --- Accessors (From File 3, 6) ---

IRNode IRStream::get_node(size_t idx) const noexcept {
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a = trans ? shadow_atoms : atoms;
    const auto* c = trans ? shadow_cores : cores;
    if (idx >= _capacity || !a || !c) return {}; 
    return { a[idx].opcode, a[idx].flags, c[idx].result_id, c[idx].op_a, c[idx].op_b, c[idx].op_c, c[idx].intent_hash };
}

const IRCore& IRStream::get_core_readonly(size_t idx) const noexcept {
    return in_transaction.load(std::memory_order_acquire) ? shadow_cores[idx] : cores[idx];
}

const IRAtom& IRStream::get_atom_readonly(size_t idx) const noexcept {
    return in_transaction.load(std::memory_order_acquire) ? shadow_atoms[idx] : atoms[idx];
}

IRNode IRStream::get_shadow_node_proxy(size_t idx) const noexcept {
    if (idx >= _capacity || !shadow_atoms || !shadow_cores) return {};
    return { shadow_atoms[idx].opcode, shadow_atoms[idx].flags, shadow_cores[idx].result_id, 
             shadow_cores[idx].op_a, shadow_cores[idx].op_b, shadow_cores[idx].op_c, shadow_cores[idx].intent_hash };
}

// --- Sync & Hash Logic (From File 4) ---

inline void sync_joined_buffer_simd(IRNode* __restrict dst, 
                               const IRAtom* __restrict atoms, 
                               const IRCore* __restrict cores, 
                               size_t count) noexcept {
    size_t i = 0;
#ifdef __AVX512F__
    for (; i + 8 <= count; i += 8) {
        #pragma GCC unroll 8
        for(size_t j = 0; j < 8; ++j) {
            const size_t idx = i + j;
            dst[idx].opcode = atoms[idx].opcode;
            dst[idx].flags = atoms[idx].flags;
            dst[idx].result_id = cores[idx].result_id;
            const uint32_t mask = (atoms[idx].opcode == OpCode::NOP) ? 0 : 0xFFFFFFFF;
            dst[idx].op_a = cores[idx].op_a & mask;
            dst[idx].op_b = cores[idx].op_b & mask;
            dst[idx].op_c = cores[idx].op_c & mask;
            dst[idx].intent_hash = cores[idx].intent_hash;
        }
    }
#endif
    for (; i < count; ++i) {
        dst[i].opcode = atoms[i].opcode;
        dst[i].flags = atoms[i].flags;
        dst[i].result_id = cores[i].result_id;
        if (atoms[i].opcode != OpCode::NOP) [[likely]] {
            dst[i].op_a = cores[i].op_a; dst[i].op_b = cores[i].op_b; dst[i].op_c = cores[i].op_c;
        } else {
            dst[i].op_a = dst[i].op_b = dst[i].op_c = 0;
        }
        dst[i].intent_hash = cores[i].intent_hash;
    }
}

IRNode* IRStream::get_buffer_ptr() const noexcept {
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a = trans ? shadow_atoms : atoms;
    const auto* c = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;
    if (!joined_buffer || !a || !c || current_count == 0) return joined_buffer; 
    sync_joined_buffer_simd(joined_buffer, a, c, std::min(current_count, _capacity));
    return joined_buffer;
}

bool IRStream::validate_shadow_integrity() const noexcept {
    if (!in_transaction.load(std::memory_order_acquire)) return true;
    for (size_t i = 0; i < shadow_count; ++i) {
        uint32_t expected = IRNode::calculate_hash(shadow_atoms[i].opcode, shadow_cores[i].op_a, shadow_cores[i].op_b, shadow_cores[i].op_c);
        if (shadow_cores[i].intent_hash != expected) return false; 
    }
    return true;
}

uint64_t IRStream::generate_logic_hash() const noexcept {
    uint64_t h = 0x1337;
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* c_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;
    if (!c_ptr || current_count == 0) return h;
    for(size_t i = 0; i < current_count; ++i) h ^= c_ptr[i].intent_hash ^ (h << 13);
    return h;
}

// --- Optimization Passes (From Files 4, 5) ---

void IRStream::push_shadow_fold(const IRNode& node) noexcept {
    IRNode folded = node.clone();
    folded.fold_if_possible();
    if (!in_transaction.load(std::memory_order_acquire)) begin_transaction();
    push_to_shadow(folded);
}

void IRStream::perform_dead_code_elimination() noexcept {
    begin_transaction(); 
    if (!shadow_atoms || !shadow_cores) return;

    std::unordered_set<uint32_t> live_regs;
    for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];
        
        bool is_essential = IRTraits::has_side_effects(atom.opcode, atom.flags) || 
                           (core.result_id != 0xFFFFFFFF && live_regs.count(core.result_id));
        
        if (!is_essential) {
            atom.flags |= Flags::IS_DEAD;
        } else {
            live_regs.erase(core.result_id);
            if (core.op_a != 0xFFFFFFFF) live_regs.insert(core.op_a);
            if (core.op_b != 0xFFFFFFFF) live_regs.insert(core.op_b);
            if (core.op_c != 0xFFFFFFFF) live_regs.insert(core.op_c);
        }
    }
    
    size_t write_idx = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (!(shadow_atoms[i].flags & Flags::IS_DEAD)) {
            if (write_idx != i) {
                shadow_atoms[write_idx] = shadow_atoms[i];
                shadow_cores[write_idx] = shadow_cores[i];
            }
            shadow_cores[write_idx].intent_hash = IRNode::calculate_hash(
                shadow_atoms[write_idx].opcode,
                shadow_cores[write_idx].op_a,
                shadow_cores[write_idx].op_b,
                shadow_cores[write_idx].op_c
            );
            write_idx++;
        }
    }
    shadow_count = write_idx;
    commit_transaction(); 
}

void IRStream::perform_gvn() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) return;

    std::unordered_map<uint32_t, uint32_t> expression_map; 
    std::unordered_map<uint32_t, uint32_t> register_remap; 

    for (size_t i = 0; i < shadow_count; ++i) {
        IRCore& core = shadow_cores[i];
        if (core.op_a != 0xFFFFFFFF && register_remap.count(core.op_a)) core.op_a = register_remap[core.op_a];
        if (core.op_b != 0xFFFFFFFF && register_remap.count(core.op_b)) core.op_b = register_remap[core.op_b];
        if (core.op_c != 0xFFFFFFFF && register_remap.count(core.op_c)) core.op_c = register_remap[core.op_c];

        core.intent_hash = IRNode::calculate_hash(shadow_atoms[i].opcode, core.op_a, core.op_b, core.op_c);

        if (!IRTraits::has_side_effects(shadow_atoms[i].opcode, shadow_atoms[i].flags)) {
            auto it = expression_map.find(core.intent_hash);
            if (it != expression_map.end()) {
                register_remap[core.result_id] = it->second;
                shadow_atoms[i].flags |= Flags::IS_DEAD;
                continue; 
            }
        }
        expression_map[core.intent_hash] = core.result_id;
    }

    size_t write_idx = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (!(shadow_atoms[i].flags & Flags::IS_DEAD)) {
            if (write_idx != i) {
                shadow_atoms[write_idx] = shadow_atoms[i];
                shadow_cores[write_idx] = shadow_cores[i];
            }
            write_idx++;
        }
    }
    shadow_count = write_idx;
    commit_transaction(); 
}

// Missing accessors required by header
void IRStream::set_shadow_count(size_t val) noexcept { shadow_count = val; }
void IRStream::update_shadow_size(size_t s) noexcept { shadow_count = s; }
uint32_t IRStream::intent_crc() const noexcept { return static_cast<uint32_t>(generate_logic_hash()); }
uint32_t IRStream::get_max_id() const noexcept { return 0; }

} // namespace dgtl::ir