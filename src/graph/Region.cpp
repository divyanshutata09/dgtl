#include "dgtl/graph/Region.hpp"

#include "dgtl/ir/base/IR_Traits.hpp"

namespace {

inline bool is_valid_vreg(uint32_t id) {
    return id != dgtl::ir::INVALID_SLOT && id != 0;
}

} // namespace

namespace dgtl::graph {

void Region::add_node(const dgtl::ir::IRNode& node) {
    nodes.push_back(node);
}

void Region::add_dependency(Region* dep) {
    if (!dep || dep == this) {
        return;
    }

    for (Region* existing : dependencies) {
        if (existing == dep) {
            return;
        }
    }

    dependencies.push_back(dep);
    dep->dependents.insert(this);
}

bool Region::can_fuse_with(const Region& other) const {
    if (this == &other) {
        return false;
    }
    if (!validate() || !other.validate()) {
        return false;
    }
    if (nodes.empty() || other.nodes.empty()) {
        return false;
    }

    // Keep fusions bounded so scheduling remains stable and cache-friendly.
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
        if (is_valid_vreg(node.op_a) && !produced.contains(node.op_a)) {
            live_ins.insert(node.op_a);
        }
        if (is_valid_vreg(node.op_b) && !produced.contains(node.op_b)) {
            live_ins.insert(node.op_b);
        }
        if (is_valid_vreg(node.op_c) && !produced.contains(node.op_c)) {
            live_ins.insert(node.op_c);
        }
        if (is_valid_vreg(node.result_id)) {
            produced.insert(node.result_id);
        }
    }

    return live_ins;
}

std::unordered_set<uint32_t> Region::get_live_outs() const {
    std::unordered_set<uint32_t> produced;
    std::unordered_set<uint32_t> used;

    produced.reserve(nodes.size());
    used.reserve(nodes.size());

    for (const auto& node : nodes) {
        if (is_valid_vreg(node.result_id)) {
            produced.insert(node.result_id);
        }
        if (is_valid_vreg(node.op_a)) {
            used.insert(node.op_a);
        }
        if (is_valid_vreg(node.op_b)) {
            used.insert(node.op_b);
        }
        if (is_valid_vreg(node.op_c)) {
            used.insert(node.op_c);
        }
    }

    std::unordered_set<uint32_t> live_outs;
    live_outs.reserve(produced.size());
    for (uint32_t vreg : produced) {
        if (used.contains(vreg)) {
            live_outs.insert(vreg);
        }
    }

    return live_outs;
}

bool Region::validate() const {
    for (Region* dep : dependencies) {
        if (!dep || dep == this) {
            return false;
        }
    }

    for (const auto& node : nodes) {
        if (node.opcode == dgtl::ir::OpCode::NOP) {
            continue;
        }

        const bool has_side_effects = dgtl::ir::IRTraits::has_side_effects(node.opcode, node.flags);
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
        if ((node.flags & dgtl::ir::Flags::IS_SIMD_BURST) != 0 ||
            node.opcode == dgtl::ir::OpCode::VEC_ADD_F32_512 ||
            node.opcode == dgtl::ir::OpCode::VEC_MUL_F32_512 ||
            node.opcode == dgtl::ir::OpCode::GATHER_F32_512 ||
            node.opcode == dgtl::ir::OpCode::GATHER_16) {
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
        if (node.is_terminator() || node.opcode == dgtl::ir::OpCode::CALL || node.opcode == dgtl::ir::OpCode::SYSCALL) {
            ++ctrl;
        }
    }
    return static_cast<double>(ctrl) / static_cast<double>(nodes.size());
}

double Region::live_range_pressure() const {
    const auto ins = get_live_ins();
    const auto outs = get_live_outs();
    if (nodes.empty()) return 0.0;
    return static_cast<double>(ins.size() + outs.size()) / static_cast<double>(nodes.size());
}

} // namespace dgtl::graph

