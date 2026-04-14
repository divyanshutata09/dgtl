#include "../../include/dgtl/ir/Global_IR.hpp"
#include <iostream>

namespace dgtl::ir {

// --- IRStream Implementation: Hot Path Operations ---

void IRStream::push_back(const IRNode& node) noexcept {
    // Generate or use existing hash for the node
    uint32_t final_hash = (node.intent_hash == 0) ? 
        IRNode::calculate_hash(node.opcode, node.op_a, node.op_b, node.op_c) : node.intent_hash;

    // Transaction-aware insertion
    if (in_transaction.load(std::memory_order_acquire)) { 
        uint32_t idx = reserve_shadow_slot();
        if (idx == INVALID_SLOT || !shadow_atoms || !shadow_cores) return;
        
        shadow_atoms[idx] = {node.opcode, node.flags};
        shadow_cores[idx] = {node.result_id, node.op_a, node.op_b, node.op_c, final_hash};
    }
    else {
        // [FIX] Double-check capacity floor and availability
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
    IRNode n{op, fl, res, a, b, c, sync_hash};
    push_back(n);
}

uint32_t IRStream::reserve_shadow_slot() noexcept {
    // [FIX] Added defensive capacity check to prevent memory corruption
    if (shadow_count >= _capacity || !shadow_atoms) [[unlikely]] {
        std::cerr << "[DGTL FATAL] IRStream Shadow Overflow at " << _capacity << "\n";
        return INVALID_SLOT;
    }
    uint32_t idx = static_cast<uint32_t>(shadow_count);
    shadow_count++; 
    return idx;
}

IRNode IRStream::get_node(size_t idx) const noexcept {
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* a = trans ? shadow_atoms : atoms;
    const auto* c = trans ? shadow_cores : cores;
    
    if (idx >= _capacity || !a || !c) return {}; 
    
    return {
        a[idx].opcode, 
        a[idx].flags, 
        c[idx].result_id, 
        c[idx].op_a, 
        c[idx].op_b, 
        c[idx].op_c, 
        c[idx].intent_hash
    };
}

const IRCore& IRStream::get_core_readonly(size_t idx) const noexcept {
    return in_transaction.load(std::memory_order_acquire) ? shadow_cores[idx] : cores[idx];
}

const IRAtom& IRStream::get_atom_readonly(size_t idx) const noexcept {
    return in_transaction.load(std::memory_order_acquire) ? shadow_atoms[idx] : atoms[idx];
}

IRNode IRStream::get_shadow_node_proxy(size_t idx) const noexcept {
    if (idx >= _capacity || !shadow_atoms || !shadow_cores) return {};
    return {
        shadow_atoms[idx].opcode, 
        shadow_atoms[idx].flags, 
        shadow_cores[idx].result_id, 
        shadow_cores[idx].op_a, 
        shadow_cores[idx].op_b, 
        shadow_cores[idx].op_c, 
        shadow_cores[idx].intent_hash
    };
}

void IRStream::push_to_shadow(const IRNode& node) noexcept {
    if (!in_transaction.load(std::memory_order_acquire)) {
        begin_transaction();
    }
    push_back(node);
}

} // namespace dgtl::ir
