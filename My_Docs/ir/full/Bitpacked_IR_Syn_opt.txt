#include "../../include/dgtl/ir/Global_IR.hpp"
#include <immintrin.h> 
#include <thread>
#include <algorithm>
#include <atomic>
#include <unordered_set>
#include <unordered_map>

namespace dgtl::ir {

/**
 * @brief Thread-Local Arena for Shadow Buffers
 * Prevents mutex contention across microservices in Project Titan.
 */
thread_local dgtl::core::DGTLArena local_transaction_arena(1024 * 1024 * 64);

// --- SIMD JOINER LOGIC ---

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
            
            // Branchless NOP clearing
            const uint32_t mask = (atoms[idx].opcode == OpCode::NOP) ? 0 : 0xFFFFFFFF;
            
            dst[idx].op_a = cores[idx].op_a & mask;
            dst[idx].op_b = cores[idx].op_b & mask;
            dst[idx].op_c = cores[idx].op_c & mask;
            dst[idx].intent_hash = cores[idx].intent_hash;
        }
    }
#endif
    // Scalar Fallback / Epilogue
    for (; i < count; ++i) {
        dst[i].opcode = atoms[i].opcode;
        dst[i].flags = atoms[i].flags;
        dst[i].result_id = cores[i].result_id;
        if (atoms[i].opcode != OpCode::NOP) [[likely]] {
            dst[i].op_a = cores[i].op_a; 
            dst[i].op_b = cores[i].op_b; 
            dst[i].op_c = cores[i].op_c;
        } else {
            dst[i].op_a = dst[i].op_b = dst[i].op_c = 0;
        }
        dst[i].intent_hash = cores[i].intent_hash;
    }
}

// --- IRSTREAM SYNC & ALLOCATION ---

IRNode* IRStream::get_buffer_ptr() const noexcept {
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a = trans ? shadow_atoms : atoms;
    const auto* c = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;
    
    if (!joined_buffer || !a || !c || current_count == 0) return joined_buffer; 
    
    sync_joined_buffer_simd(joined_buffer, a, c, std::min(current_count, _capacity));
    return joined_buffer;
}

IRStream IRStream::allocate_transactional(size_t size) {
    return IRStream(local_transaction_arena, size);
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

// --- OPTIMIZATION optES ---

void IRStream::push_shadow_fold(const IRNode& node) noexcept {
    IRNode folded = node.clone();
    folded.fold_if_possible();
    if (!in_transaction.load(std::memory_order_acquire)) begin_transaction();
    push_back(folded);
}

void IRStream::perform_dead_code_elimination() noexcept {
    begin_transaction(); 
    if (!shadow_atoms || !shadow_cores) return;

    std::unordered_set<uint32_t> live_regs;
    for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
        bool is_essential = IRTraits::has_side_effects(shadow_atoms[i].opcode, shadow_atoms[i].flags) || 
                           (shadow_cores[i].result_id != INVALID_SLOT && live_regs.count(shadow_cores[i].result_id));
        
        if (!is_essential) {
            shadow_atoms[i].flags |= Flags::IS_DEAD;
        } else {
            live_regs.erase(shadow_cores[i].result_id);
            if (shadow_cores[i].op_a != INVALID_SLOT) live_regs.insert(shadow_cores[i].op_a);
            if (shadow_cores[i].op_b != INVALID_SLOT) live_regs.insert(shadow_cores[i].op_b);
            if (shadow_cores[i].op_c != INVALID_SLOT) live_regs.insert(shadow_cores[i].op_c);
        }
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

void IRStream::perform_gvn() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) return;

    std::unordered_map<uint32_t, uint32_t> expr_map; 
    std::unordered_map<uint32_t, uint32_t> remap; 

    for (size_t i = 0; i < shadow_count; ++i) {
        IRCore& core = shadow_cores[i];
        if (remap.count(core.op_a)) core.op_a = remap[core.op_a];
        if (remap.count(core.op_b)) core.op_b = remap[core.op_b];
        if (remap.count(core.op_c)) core.op_c = remap[core.op_c];

        core.intent_hash = IRNode::calculate_hash(shadow_atoms[i].opcode, core.op_a, core.op_b, core.op_c);

        if (!IRTraits::has_side_effects(shadow_atoms[i].opcode, shadow_atoms[i].flags)) {
            auto it = expr_map.find(core.intent_hash);
            if (it != expr_map.end()) {
                remap[core.result_id] = it->second;
                shadow_atoms[i].flags |= Flags::IS_DEAD;
                continue;
            }
        }
        expr_map[core.intent_hash] = core.result_id;
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

} // namespace dgtl::ir