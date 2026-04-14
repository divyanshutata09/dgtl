/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 34: SPATIAL-TEMPORAL STORE FUSER
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED FUSION)
 * PILLAR 3: THE SENTINEL (CRC-32 INTENT VALIDATION)
 * DESIGN: Fuses scalar stores into SIMD writes with zero-corruption rollback.
 */

#pragma once
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/core/Verified_Formal.hpp"

namespace dgtl::pass {

/**
 * @class DGTLRedundantStoreFuser
 * @brief Uses the MemorySmasher to safely fuse adjacent scalar stores into SIMD bursts.
 */
class DGTLRedundantStoreFuser {
    // Corrected: Fully qualified namespace for the analyzer
    dgtl::analysis::DGTLMemorySmasher& smasher;

public:
    // Corrected: Fully qualified namespace in constructor
    explicit DGTLRedundantStoreFuser(dgtl::analysis::DGTLMemorySmasher& s) : smasher(s) {}

    /**
     * @brief Transactional Memory Strike.
     * Consolidates scalar writes into SIMD bursts via Shadow-Tier mutation.
     */
    void fuse_stores(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        if (stream_size < 2) return;

        // PILLAR 2: Open a Transactional Tier
        stream.begin_transaction();

        // Step 1: Baseline sync of Primary to Shadow
        for (uint32_t i = 0; i < (uint32_t)stream_size; ++i) {
            sync_node_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            ir::IRNode node = stream.get_node_readonly(i);

            // OpCode Sync: STORE. Volatile Check via Flags.
            if (node.opcode == ir::OpCode::STORE && !(node.flags & ir::Flags::IS_VOLATILE)) {
                std::vector<uint32_t> fuse_candidates = find_adjacent_stores(stream, i);

                // Threshold: 4 nodes total (Head + 3 targets) = 128-bit/512-bit target
                if (fuse_candidates.size() >= 3) { 
                    if (is_safe_to_fuse(stream, i, fuse_candidates.back())) {
                        // PILLAR 3: Validated Synthesis in Shadow Tier
                        synthesize_wide_store(stream, i, fuse_candidates);
                        i = fuse_candidates.back(); // Jump past fused block
                    }
                }
            }
        }

        // Sentinel Integrity Check
        if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
            return;
        }

        stream.commit_transaction();
    }

private:
    void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        
        s_atom.opcode = node.opcode;
        s_atom.flags = node.flags;
        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;
        s_core.intent_hash = node.intent_hash;
    }

    std::vector<uint32_t> find_adjacent_stores(const ir::IRStream& s, uint32_t start_idx) {
        std::vector<uint32_t> candidates;
        const uint32_t limit = std::min(start_idx + 16, static_cast<uint32_t>(s.size()));
        
        for (uint32_t j = start_idx + 1; j < limit; ++j) {
            ir::IRNode next = s.get_node_readonly(j);
            
            if (next.opcode == ir::OpCode::STORE) {
                // Smasher check for spatial continuity
                if (smasher.can_group_access(s, start_idx + (uint32_t)candidates.size(), 2)) {
                    candidates.push_back(j);
                } else {
                    break;
                }
            } else if (next.opcode == ir::OpCode::LOAD) {
                if (!smasher.can_reorder(s, start_idx, j)) break;
            } else if (next.flags & ir::Flags::HAS_SIDE_EFFECTS) {
                break; 
            }
        }
        return candidates;
    }

    bool is_safe_to_fuse(const ir::IRStream& s, uint32_t start, uint32_t end) noexcept {
        for (uint32_t i = start; i <= end; ++i) {
            ir::IRNode n = s.get_node_readonly(i);
            if (n.is_terminator() || (n.flags & ir::Flags::HAS_SIDE_EFFECTS)) {
                return false;
            }
        }
        return true;
    }

    void synthesize_wide_store(ir::IRStream& stream, uint32_t start_idx, const std::vector<uint32_t>& targets) {
        ir::IRAtom& h_atom = stream.get_shadow_atom(start_idx);
        ir::IRCore& h_core = stream.get_shadow_core(start_idx);
        
        // Upgrade to SIMD Store (AVX-512 target)
        h_atom.opcode = ir::OpCode::STORE_V512; 
        h_atom.flags |= (ir::Flags::IS_MACRO_FUSED | ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT);
        
        // Refresh Hash via Sentinel formula
        h_core.intent_hash = core::DGTLFormalVerifier::compute_master_hash_bits(
            h_atom.opcode, h_core.op_a, h_core.op_b, h_core.op_c
        );

        // NOP redundant scalar stores in the shadow tier
        for (uint32_t target_idx : targets) {
            ir::IRAtom& r_atom = stream.get_shadow_atom(target_idx);
            ir::IRCore& r_core = stream.get_shadow_core(target_idx);
            
            r_atom.opcode = ir::OpCode::NOP;
            r_atom.flags = ir::Flags::NONE;
            r_core.result_id = ir::INVALID_SLOT;
            r_core.op_a = 0; r_core.op_b = 0; r_core.op_c = 0;
            
            r_core.intent_hash = core::DGTLFormalVerifier::compute_master_hash_bits(
                ir::OpCode::NOP, 0, 0, 0
            );
        }
    }
};

} // namespace dgtl::pass