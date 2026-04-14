/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 18: TRANSACTIONAL DEAD CODE ELIMINATION (DCE-SOLVER)
 * STRATEGY: Reverse-opt Liveness + Transactional Shadow Compaction.
 * GUARANTEE: Zero-Corruption Strike via Pillar 3 Sentinel.
 */

#pragma once
#include <vector>
#include <bitset>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp" 
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::pass {

class DGTLDCESolver {
    // 64k IDs for local block liveness tracking
    static constexpr uint32_t MAX_TRACKED_IDS = 65536;

public:
    DGTLDCESolver() = default;

    /**
     * @brief Executes the DCE opt using the Tri-Tier Transactional Stream.
     */
    void execute(ir::IRStream& stream) {
        execute(stream, nullptr);
    }

    /**
     * @brief Executes DCE with optional language-level root retention.
     */
    void execute(ir::IRStream& stream, const pipeline::DGTLLanguageRootMarker* root_marker) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        // PILLAR 2: Shadow Buffer Allocation (Isolated Transaction Space)
        ir::IRStream shadow_stream = ir::IRStream::allocate_transactional(stream.capacity());
        
        if (shadow_stream.is_null()) return;

        // 1. Liveness Map (Bit-packed validation)
        std::bitset<MAX_TRACKED_IDS> live_map;
        mark_live_instructions(stream, live_map, root_marker);

        // 2. Transactional Pruning: Write only live nodes to the shadow
        // The shadow stream must be in a transaction state to accept pushes
        shadow_stream.begin_transaction();
        bool success = prune_to_shadow(stream, shadow_stream, live_map, root_marker);

        // PILLAR 3: THE SENTINEL
        // Validates shadow integrity (Hash/Stride/ResultID) before committing
        if (success && shadow_stream.validate_shadow_integrity()) {
            stream.commit_from_shadow(shadow_stream);
        } else {
            // Rollback: discard the shadow stream mutations
            shadow_stream.abort_transaction();
        }
    }

private:
    /**
     * @brief Reverse-opt to populate the liveness bitset.
     */
    void mark_live_instructions(
        const ir::IRStream& src,
        std::bitset<MAX_TRACKED_IDS>& live_map,
        const pipeline::DGTLLanguageRootMarker* root_marker
    ) {
        const size_t sz = src.size();
        for (int i = static_cast<int>(sz) - 1; i >= 0; --i) {
            // Synchronized with IRStream::get_node
            ir::IRNode node = src.get_node(i);
            
            bool is_essential = node.is_terminator() || 
                                (node.flags & ir::Flags::HAS_SIDE_EFFECTS) || 
                                (node.opcode == ir::OpCode::STORE) ||
                                (root_marker != nullptr && root_marker->is_root(node));

            // A node is live if it's essential OR its result is needed by a subsequent op
            // [DGTL FIX]: Added check to ensure result_id is non-zero before bitset testing
            bool needed_by_successor = (node.result_id != 0) && live_map.test(node.result_id % MAX_TRACKED_IDS);

            if (is_essential || needed_by_successor) {
                // If this node is live, its operands must also be live
                if (node.op_a != ir::INVALID_SLOT && node.op_a != 0) live_map.set(node.op_a % MAX_TRACKED_IDS);
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0) live_map.set(node.op_b % MAX_TRACKED_IDS);
                if (node.op_c != ir::INVALID_SLOT && node.op_c != 0) live_map.set(node.op_c % MAX_TRACKED_IDS);
                
                // If it was essential but not previously in live_map, ensure it's marked
                if (node.result_id != 0) {
                    live_map.set(node.result_id % MAX_TRACKED_IDS);
                }
            }
        }
    }

    /**
     * @brief Filters the stream into the shadow buffer with Hash Re-validation.
     */
    bool prune_to_shadow(
        const ir::IRStream& src,
        ir::IRStream& shadow,
        const std::bitset<MAX_TRACKED_IDS>& live_map,
        const pipeline::DGTLLanguageRootMarker* root_marker
    ) {
        for (size_t i = 0; i < src.size(); ++i) {
            ir::IRNode node = src.get_node(i);
            
            bool is_live = node.is_terminator() || 
                           (node.flags & ir::Flags::HAS_SIDE_EFFECTS) || 
                           (node.opcode == ir::OpCode::STORE) ||
                           (root_marker != nullptr && root_marker->is_root(node)) ||
                           (node.result_id != 0 && live_map.test(node.result_id % MAX_TRACKED_IDS));

            if (is_live) {
                // [DGTL SAFETY] Prevent pollution from uninitialized IDs
                if (node.result_id == 0 && !node.is_terminator() && !(node.flags & ir::Flags::HAS_SIDE_EFFECTS)) {
                    if (node.opcode != ir::OpCode::NOP) return false; 
                }

                // [PILLAR 3] REFRESH HASH
                // When moving nodes between streams/buffers, the intent_hash must be 
                // recalculated to ensure the Transactional Trap isn't tripped.
                node.intent_hash = ir::IRNode::calculate_hash(
                    node.opcode,
                    node.result_id,
                    node.op_a,
                    node.op_b,
                    node.op_c
                );
                
                shadow.push_back(node);
            }
        }
        return true;
    }
};

} // namespace dgtl::pass


