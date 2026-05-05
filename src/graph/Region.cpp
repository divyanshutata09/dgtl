/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/graph/Region.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"

namespace {

/**
 * @brief Checks if a register ID is valid within the DGTL virtual register space.
 * Slot 0 is reserved for NULL/Discard, and INVALID_SLOT is the BIR sentinel.
 */
inline bool is_valid_vreg(uint32_t id) {
    return id != dgtl::ir::INVALID_SLOT && id != 0;
}

} // namespace

namespace dgtl::graph {

void Region::add_node(const dgtl::ir::IRNode& node) {
    nodes.push_back(node);
}

void Region::add_dependency(Region* dep) {
    if (!dep || dep == this) return;

    // Check for existing dependency to prevent duplicates in the stream
    for (Region* existing : dependencies) {
        if (existing == dep) return;
    }

    dependencies.push_back(dep);
    dep->dependents.insert(this);
}

bool Region::can_fuse_with(const Region& other) const {
    if (this == &other) return false;
    if (nodes.empty() || other.nodes.empty()) return false;

    // BIR 1.0.0 Optimization: Keep fusions bounded for L1-instruction cache locality.
    // Over-fusing triggers spills in the register allocator.
    constexpr size_t kMaxFusedNodes = 128;
    if (nodes.size() + other.nodes.size() > kMaxFusedNodes) {
        return false;
    }

    return true;
}

std::unordered_set<uint32_t> Region::get_live_ins() const {
    std::unordered_set<uint32_t> produced;
    std::unordered_set<uint32_t> live_ins;

    produced.reserve(nodes.size());
    live_ins.reserve(nodes.size());

    for (const auto& node : nodes) {
        // If an operand is used before it is produced in this region, it's a live-in
        if (is_valid_vreg(node.op_a) && !produced.contains(node.op_a)) live_ins.insert(node.op_a);
        if (is_valid_vreg(node.op_b) && !produced.contains(node.op_b)) live_ins.insert(node.op_b);
        if (is_valid_vreg(node.op_c) && !produced.contains(node.op_c)) live_ins.insert(node.op_c);
        
        if (is_valid_vreg(node.result_id)) {
            produced.insert(node.result_id);
        }
    }

    return live_ins;
}

std::unordered_set<uint32_t> Region::get_live_outs() const {
    std::unordered_set<uint32_t> produced;
    std::unordered_set<uint32_t> used_internally;

    produced.reserve(nodes.size());
    used_internally.reserve(nodes.size());

    for (const auto& node : nodes) {
        if (is_valid_vreg(node.result_id)) produced.insert(node.result_id);
        if (is_valid_vreg(node.op_a)) used_internally.insert(node.op_a);
        if (is_valid_vreg(node.op_b)) used_internally.insert(node.op_b);
        if (is_valid_vreg(node.op_c)) used_internally.insert(node.op_c);
    }

    // Simplified logic: for now, assume all produced values are potential live-outs
    // unless they are strictly transient (to be refined by global DFA).
    return produced;
}

bool Region::validate() const {
    for (const auto& node : nodes) {
        if (node.opcode == dgtl::ir::OpCode::NOP) continue;

        // Ensure node is "useful": either has side effects, produces a value, or controls flow.
        const bool has_side_effects = node.has_side_effects();
        const bool produces_value = is_valid_vreg(node.result_id);
        
        if (!has_side_effects && !produces_value && !node.is_terminator()) {
            return false;
        }
    }
    return true;
}

size_t Region::node_count() const noexcept {
    return nodes.size();
}

double Region::simd_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t simd = 0;
    for (const auto& node : nodes) {
        // Checking for AVX-512 vector opcodes and SIMD burst flags
        if ((node.flags & dgtl::ir::Flags::IS_SIMD_BURST) != 0 ||
            static_cast<uint16_t>(node.opcode) >= static_cast<uint16_t>(dgtl::ir::OpCode::VEC_ADD_F32_512)) {
            ++simd;
        }
    }
    return static_cast<double>(simd) / static_cast<double>(nodes.size());
}

double Region::memory_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t mem = 0;
    for (const auto& node : nodes) {
        if (node.opcode == dgtl::ir::OpCode::LOAD || node.opcode == dgtl::ir::OpCode::STORE) {
            ++mem;
        }
    }
    return static_cast<double>(mem) / static_cast<double>(nodes.size());
}

double Region::control_density() const noexcept {
    if (nodes.empty()) return 0.0;
    size_t ctrl = 0;
    for (const auto& node : nodes) {
        // is_terminator handles RET/BRANCH/GOTO
        if (node.is_terminator() || node.opcode == dgtl::ir::OpCode::CALL) {
            ++ctrl;
        }
    }
    return static_cast<double>(ctrl) / static_cast<double>(nodes.size());
}

double Region::live_range_pressure() const {
    if (nodes.empty()) return 0.0;
    const auto ins = get_live_ins();
    const auto outs = get_live_outs();
    return static_cast<double>(ins.size() + outs.size()) / static_cast<double>(nodes.size());
}

} // namespace dgtl::graph
