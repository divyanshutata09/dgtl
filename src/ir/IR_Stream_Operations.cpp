#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"     
#include "dgtl/ir/base/IR_Stream.hpp" 
#include "dgtl/core/Arena.hpp" 

#include <iostream>


namespace dgtl::ir {

// From File 2: Extern declaration for linking across units
extern dgtl::core::DGTL_Arena local_transaction_arena;

// --- Hot Path Operations (From File 3) ---

void IRStream::push_back(const IRNode& node) noexcept {
    const uint64_t final_hash = (node.intent_hash == 0)
        ? IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c)
        : node.intent_hash;

    if (in_transaction.load(std::memory_order_acquire)) {
        uint32_t idx = reserve_shadow_slot();
        if (idx == 0xFFFFFFFF || !shadow_atoms || !shadow_cores) return;

        shadow_atoms[idx] = {node.opcode, node.flags};
        shadow_cores[idx] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash, 0, 0};
    }
    else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] {
            std::cerr << "[DGTL FATAL] IRStream Capacity Overflow at " << _capacity << "\n";
            return;
        }
        atoms[count].opcode = node.opcode;
        atoms[count].flags = node.flags;
        cores[count] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash, 0, 0};
        count++;
        shadow_count = count;
        joined_valid = false;
    }
}

void IRStream::push_node(OpCode op, uint16_t fl, uint32_t res, uint32_t a, uint32_t b, uint32_t c, uint32_t h) noexcept {
    const uint64_t sync_hash = (h == 0)
        ? IRNode::calculate_hash(op, res, a, b, c)
        : static_cast<uint64_t>(h);
    bool trans = in_transaction.load(std::memory_order_acquire);

    if (trans) {
        if (shadow_count >= _capacity || !shadow_atoms || !shadow_cores) [[unlikely]] return;
        shadow_atoms[shadow_count] = { op, fl };
        shadow_cores[shadow_count] = { res, a, b, c, sync_hash, fl, 0 };
        shadow_count++;
    } else {
        if (count >= _capacity || !atoms || !cores) [[unlikely]] return;
        atoms[count] = { op, fl };
        cores[count] = { res, a, b, c, sync_hash, fl, 0 };
        count++;
        shadow_count = count;
        joined_valid = false;
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
    const size_t limit = trans ? shadow_count : count;

    if (idx >= limit || !a || !c) return {};
    return { a[idx].opcode, a[idx].flags, c[idx].result_id, c[idx].op_a, c[idx].op_b, c[idx].op_c, c[idx].intent_hash, 0u };
}

IRNode& IRStream::get_node_ref(size_t idx) noexcept {
    static IRNode fallback{};

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const size_t current_count = trans ? shadow_count : count;
    if (idx >= current_count || !joined_buffer) {
        return fallback;
    }

    if (!joined_valid) {
        for (size_t i = 0; i < current_count; ++i) {
            joined_buffer[i] = get_node(i);
        }
        joined_valid = true;
        joined_buffer_in_transaction = trans;
    }
    return joined_buffer[idx];
}

const IRCore& IRStream::get_core_readonly(size_t idx) const noexcept {
    bool trans = in_transaction.load(std::memory_order_acquire);
    if (idx >= (trans ? shadow_count : count)) {
        static const IRCore null_core = {0u, 0u, 0u, 0u, 0ull, 0u, 0u};
        return null_core;
    }
    return trans ? shadow_cores[idx] : cores[idx];
}

const IRAtom& IRStream::get_atom_readonly(size_t idx) const noexcept {
    static const IRAtom null_atom{OpCode::NOP, Flags::NONE};

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const size_t limit = trans ? shadow_count : count;
    if (idx >= limit) {
        return null_atom;
    }

    return trans ? shadow_atoms[idx] : atoms[idx];
}

IRNode IRStream::get_shadow_node_proxy(size_t idx) const noexcept {
    if (idx >= _capacity || !shadow_atoms || !shadow_cores) return {};
    return { shadow_atoms[idx].opcode, shadow_atoms[idx].flags, shadow_cores[idx].result_id,
             shadow_cores[idx].op_a, shadow_cores[idx].op_b, shadow_cores[idx].op_c, shadow_cores[idx].intent_hash, 0u };
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
    if (!joined_valid || joined_buffer_in_transaction != trans) {
        sync_joined_buffer_simd(joined_buffer, a, c, std::min(current_count, _capacity));
        joined_valid = true;
        joined_buffer_in_transaction = trans;
    }
    return joined_buffer;
}

bool IRStream::validate_shadow_integrity() const noexcept {
    if (!in_transaction.load(std::memory_order_acquire)) return true;
    for (size_t i = 0; i < shadow_count; ++i) {
        const uint64_t expected = IRNode::calculate_hash(
            shadow_atoms[i].opcode,
            shadow_cores[i].result_id,
            shadow_cores[i].op_a,
            shadow_cores[i].op_b,
            shadow_cores[i].op_c
        );
        if (shadow_cores[i].intent_hash != expected) return false;
    }
    return true;
}

uint64_t IRStream::generate_logic_hash() const noexcept {
    constexpr uint64_t seed = 0x243F6A8885A308D3ULL;
    uint64_t h = seed;

    const bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a_ptr = trans ? shadow_atoms : atoms;
    const auto* c_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!a_ptr || !c_ptr || current_count == 0) {
        return h;
    }

    for (size_t i = 0; i < current_count; ++i) {
        const uint64_t node_hash = IRNode::calculate_hash(
            a_ptr[i].opcode,
            c_ptr[i].result_id,
            c_ptr[i].op_a,
            c_ptr[i].op_b,
            c_ptr[i].op_c
        ) ^ (static_cast<uint64_t>(a_ptr[i].flags) << 48)
          ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL);

        h ^= node_hash + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    }
    return h;
}
} // namespace dgtl::ir