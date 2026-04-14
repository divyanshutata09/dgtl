#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"     
#include "dgtl/ir/base/IR_Stream.hpp" 
#include "dgtl/core/Arena.hpp" 

#include <unordered_set>
#include <unordered_map>



namespace dgtl::ir {

namespace {
constexpr bool is_commutative_opcode(const OpCode op) noexcept {
    return op == OpCode::ADD_I32 ||
           op == OpCode::MUL_I32 ||
           op == OpCode::AND_I32 ||
           op == OpCode::OR_I32 ||
           op == OpCode::XOR_I32 ||
           op == OpCode::ADD_F32 ||
           op == OpCode::MUL_F32;
}
} // namespace

// --- Optimization optes (From Files 4, 5) ---

void IRStream::push_shadow_fold(const IRNode& node) noexcept {
    IRNode folded = node.clone();
    folded.fold_if_possible();
    if (!in_transaction.load(std::memory_order_acquire)) begin_transaction();
    push_to_shadow(folded);
}

void IRStream::perform_dead_code_elimination() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) {
        rollback_transaction();
        return;
    }

    std::unordered_set<uint32_t> live_regs;
    for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];

        const bool is_terminator = IRTraits::is_terminator(atom.opcode);
        const bool is_essential = IRTraits::has_side_effects(atom.opcode, atom.flags) ||
                                  is_terminator ||
                                  (core.result_id != INVALID_SLOT && live_regs.count(core.result_id) != 0);

        if (!is_essential) {
            atom.flags |= Flags::IS_DEAD;
        } else {
            live_regs.erase(core.result_id);
            if (core.op_a != INVALID_SLOT) live_regs.insert(core.op_a);
            if (core.op_b != INVALID_SLOT) live_regs.insert(core.op_b);
            if (core.op_c != INVALID_SLOT) live_regs.insert(core.op_c);
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
                shadow_cores[write_idx].result_id, // Add this!
                shadow_cores[write_idx].op_a,
                shadow_cores[write_idx].op_b,
                shadow_cores[write_idx].op_c
            );
            write_idx++;
        }
    }
    shadow_count = write_idx;
    if (validate_shadow_integrity()) {
        commit_transaction();
    } else {
        rollback_transaction();
    }
}

void IRStream::perform_gvn() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) {
        rollback_transaction();
        return;
    }

    std::unordered_map<uint64_t, uint32_t> expression_map;
    std::unordered_map<uint32_t, uint32_t> register_remap;
    std::unordered_map<uint32_t, uint64_t> const_values;

    for (size_t i = 0; i < shadow_count; ++i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];
        if (core.op_a != INVALID_SLOT && register_remap.count(core.op_a)) core.op_a = register_remap[core.op_a];
        if (core.op_b != INVALID_SLOT && register_remap.count(core.op_b)) core.op_b = register_remap[core.op_b];
        if (core.op_c != INVALID_SLOT && register_remap.count(core.op_c)) core.op_c = register_remap[core.op_c];

        // Local constant folding for integer arithmetic.
        if (atom.opcode == OpCode::ADD_I32 || atom.opcode == OpCode::SUB_I32 ||
            atom.opcode == OpCode::MUL_I32 || atom.opcode == OpCode::DIV_I32 ||
            atom.opcode == OpCode::AND_I32 || atom.opcode == OpCode::OR_I32 ||
            atom.opcode == OpCode::XOR_I32 || atom.opcode == OpCode::SHL_I32) {
            const auto ita = const_values.find(core.op_a);
            const auto itb = const_values.find(core.op_b);
            if (ita != const_values.end() && itb != const_values.end()) {
                const uint32_t lhs = static_cast<uint32_t>(ita->second);
                const uint32_t rhs = static_cast<uint32_t>(itb->second);
                bool folded = true;
                uint32_t folded_val = 0;

                switch (atom.opcode) {
                    case OpCode::ADD_I32: folded_val = lhs + rhs; break;
                    case OpCode::SUB_I32: folded_val = lhs - rhs; break;
                    case OpCode::MUL_I32: folded_val = lhs * rhs; break;
                    case OpCode::DIV_I32:
                        if (rhs == 0) folded = false;
                        else folded_val = lhs / rhs;
                        break;
                    case OpCode::AND_I32: folded_val = lhs & rhs; break;
                    case OpCode::OR_I32: folded_val = lhs | rhs; break;
                    case OpCode::XOR_I32: folded_val = lhs ^ rhs; break;
                    case OpCode::SHL_I32: folded_val = lhs << (rhs & 31U); break;
                    default: folded = false; break;
                }

                if (folded) {
                    atom.opcode = OpCode::LOAD_IMM;
                    atom.flags = static_cast<uint16_t>(atom.flags | Flags::IS_CONST | Flags::IS_SPEC_CONST);
                    core.op_a = 0;
                    core.op_b = folded_val;
                    core.op_c = INVALID_SLOT;
                }
            }
        }

        if (is_commutative_opcode(atom.opcode) && core.op_a > core.op_b) {
            std::swap(core.op_a, core.op_b);
        }

        core.intent_hash = IRNode::calculate_hash(atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c);

        const bool is_const_like = atom.opcode == OpCode::LOAD_IMM || atom.opcode == OpCode::IMMEDIATE;
        if (is_const_like) {
            const uint64_t imm = (static_cast<uint64_t>(core.op_a) << 32) | core.op_b;
            const_values[core.result_id] = imm;
        }

        const bool is_blocking = IRTraits::has_side_effects(atom.opcode, atom.flags) ||
                                 IRTraits::is_terminator(atom.opcode) ||
                                 (atom.flags & Flags::IS_VOLATILE);

        if (!is_blocking) {
            const uint64_t semantic_hash = IRNode::calculate_semantic_hash(
                atom.opcode,
                core.op_a,
                core.op_b,
                core.op_c,
                atom.flags
            );

            auto it = expression_map.find(semantic_hash);
            if (it != expression_map.end()) {
                register_remap[core.result_id] = it->second;
                atom.flags |= Flags::IS_DEAD;
                continue;
            }
            expression_map[semantic_hash] = core.result_id;
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
                shadow_cores[write_idx].result_id,
                shadow_cores[write_idx].op_a,
                shadow_cores[write_idx].op_b,
                shadow_cores[write_idx].op_c
            );
            write_idx++;
        }
    }
    shadow_count = write_idx;
    if (validate_shadow_integrity()) {
        commit_transaction();
    } else {
        rollback_transaction();
    }
}

// Missing accessors required by header
void IRStream::set_shadow_count(size_t val) noexcept { shadow_count = val; }
void IRStream::update_shadow_size(size_t s) noexcept { shadow_count = s; }
uint32_t IRStream::intent_crc() const noexcept { return static_cast<uint32_t>(generate_logic_hash()); }
uint32_t IRStream::get_max_id() const noexcept {
    uint32_t max_id = 0;
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* c_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!c_ptr || current_count == 0) return 0;

    for (size_t i = 0; i < current_count; ++i) {
        uint32_t rid = c_ptr[i].result_id;
        if (rid != 0xFFFFFFFF && rid < 0x80000000U) {
            if (rid > max_id) max_id = rid;
        }
    }
    return max_id;
}

/**
 * @brief Evolution hook for IR self-specialization.
 * Allows the IR to mutate based on runtime feedback.
 */
void IRStream::evolve_node(IRNode* node) {
    if (!node) return;

    // Hook for IR evolution - in production this would integrate with IREvolver
    // For now, this is a placeholder for the self-evolving IR system
    // The evolution would be driven by:
    // - Runtime feedback from JIT execution
    // - Cost model predictions
    // - Workload pattern analysis

    // Example: Simple constant folding evolution
    if (node->opcode == OpCode::ADD_I32 && node->op_a == node->op_b) {
        // a + a -> 2 * a (potential for strength reduction)
        node->opcode = OpCode::MUL_I32;
        node->op_b = 2;
        node->refresh_intent_hash();
    }
}

} // namespace dgtl::ir