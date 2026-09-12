#include "../../include/dgtl/ir/Global_IR.hpp"
#include <unordered_set>
#include <unordered_map>

namespace dgtl::ir {

// --- IRStream Implementation: Optimization optes ---

void IRStream::push_shadow_fold(const IRNode& node) noexcept {
    IRNode folded = node.clone();
    folded.fold_if_possible();
    push_to_shadow(folded);
}

void IRStream::perform_dead_code_elimination() noexcept {
    begin_transaction(); 
    if (!shadow_atoms || !shadow_cores) return;

    std::unordered_set<uint32_t> live_regs;
    
    // Backward opt to identify dead instructions
    for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];
        
        bool is_essential = IRTraits::has_side_effects(atom.opcode, atom.flags) || 
                           (core.result_id != INVALID_SLOT && live_regs.count(core.result_id));
        
        if (!is_essential) {
            atom.flags |= Flags::IS_DEAD;
        } else {
            // Instruction is live, mark its operands as live
            live_regs.erase(core.result_id);
            if (core.op_a != INVALID_SLOT) live_regs.insert(core.op_a);
            if (core.op_b != INVALID_SLOT) live_regs.insert(core.op_b);
            if (core.op_c != INVALID_SLOT) live_regs.insert(core.op_c);
        }
    }
    
    // Compact the buffer in-place
    size_t write_idx = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (!(shadow_atoms[i].flags & Flags::IS_DEAD)) {
            if (write_idx != i) {
                shadow_atoms[write_idx] = shadow_atoms[i];
                shadow_cores[write_idx] = shadow_cores[i];
            }
            
            // Re-sync hash for the new position/state
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

    std::unordered_map<uint32_t, uint32_t> expression_map; // hash -> original_result_id
    std::unordered_map<uint32_t, uint32_t> register_remap; // old_id -> new_id

    for (size_t i = 0; i < shadow_count; ++i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];

        // Apply existing remaps to operands
        if (core.op_a != INVALID_SLOT && register_remap.count(core.op_a)) core.op_a = register_remap[core.op_a];
        if (core.op_b != INVALID_SLOT && register_remap.count(core.op_b)) core.op_b = register_remap[core.op_b];
        if (core.op_c != INVALID_SLOT && register_remap.count(core.op_c)) core.op_c = register_remap[core.op_c];

        // Recalculate hash after operand remapping
        core.intent_hash = IRNode::calculate_hash(atom.opcode, core.op_a, core.op_b, core.op_c);

        if (!IRTraits::has_side_effects(atom.opcode, atom.flags)) {
            auto it = expression_map.find(core.intent_hash);
            if (it != expression_map.end()) {
                // Redundant expression found: map this result to the previous one
                register_remap[core.result_id] = it->second;
                atom.flags |= Flags::IS_DEAD;
                continue; 
            }
        }
        // New unique expression
        expression_map[core.intent_hash] = core.result_id;
    }

    // Compact the buffer after mapping
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

} // namespace dgtl::ir
