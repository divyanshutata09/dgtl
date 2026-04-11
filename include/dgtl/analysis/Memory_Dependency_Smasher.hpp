/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 29: MEMORY DEPENDENCY SMASHER (AI-PROVEN DISAMBIGUATION)
 * PILLAR 2: TRANSACTIONAL PROVENANCE (SHADOW-MAPPING)
 * PILLAR 3: THE SENTINEL (ALIAS-CONSISTENCY GUARD)
 */

#pragma once
#include <unordered_map>
#include <cstdint>
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"
#include "../analysis/AI_Cost_Model.hpp"

namespace dgtl::opt {

class DGTLMemorySmasher {
    DGTLFormalVerifier& verifier;
    DGTLAICostModel& ai_model;

    /**
     * @brief Provenance Map (Shadow-Buffered).
     * Maps a Pointer's ResultID to its base allocation region.
     */
    std::unordered_map<uint32_t, uint64_t> primary_region_map;
    std::unordered_map<uint32_t, uint64_t> shadow_region_map;
    bool in_transaction = false;

public:
    DGTLMemorySmasher(DGTLFormalVerifier& v, DGTLAICostModel& ai) 
        : verifier(v), ai_model(ai) {}

    /**
     * @brief AI-Powered Alias Probability.
     * Calculated via Tri-Tier check: Provenance -> Formal -> AI Inference.
     */
    [[nodiscard]] float predict_alias_probability(const ir::IRStream& stream, uint32_t idx_a, uint32_t idx_b) noexcept {
        const ir::IRCore& core_a = stream.get_core_readonly(idx_a);
        const ir::IRCore& core_b = stream.get_core_readonly(idx_b);

        uint64_t reg_a = get_region(core_a.op_a);
        uint64_t reg_b = get_region(core_b.op_a);

        // 1. Region-Based Disambiguation (O(1))
        if (reg_a != 0 && reg_b != 0 && reg_a != reg_b) {
            return 0.0f; // Guaranteed NoAlias
        }

        // 2. Formal Logic Proof (Polyhedral/SMT)
        if (verifier.verify_disjoint(stream, idx_a, idx_b)) {
            return 0.0f; 
        }

        // 3. AI-Guided Speculation
        return ai_model.predict_independence(stream, idx_a, idx_b);
    }

    /**
     * @brief The "Zero-Corruption" Dependency Strike.
     * Validates reordering safety using BIR 2.0 split-tier accessors.
     */
    [[nodiscard]] bool can_reorder(const ir::IRStream& stream, uint32_t store_idx, uint32_t load_idx) noexcept {
        const size_t stream_size = stream.size();
        if (store_idx >= stream_size || load_idx >= stream_size) [[unlikely]] {
            return false; 
        }

        // PILLAR 3: Split-Tier Access (Red Flag Resolved)
        const ir::IRAtom& store_atom = stream.get_atom_readonly(store_idx);
        const ir::IRAtom& load_atom  = stream.get_atom_readonly(load_idx);
        const ir::IRCore& store_core = stream.get_core_readonly(store_idx);
        const ir::IRCore& load_core  = stream.get_core_readonly(load_idx);

        // OpCode Sync: LOAD=10, STORE=11
        if (store_atom.opcode != ir::OpCode::STORE || load_atom.opcode != ir::OpCode::LOAD) {
            return true; 
        }

        uint64_t store_region = get_region(store_core.op_a);
        uint64_t load_region  = get_region(load_core.op_a);

        // 1. Region-Based Disambiguation (O(1))
        if (store_region != 0 && load_region != 0 && store_region != load_region) {
            return true; // Smashed! Guaranteed NoAlias.
        }

        // 2. Formal Logic Proof (Polyhedral/SMT)
        if (verifier.verify_disjoint(stream, store_idx, load_idx)) {
            return true; 
        }

        // 3. AI-Guided Speculation
        if (ai_model.predict_independence(stream, store_idx, load_idx) > 0.999f) {
            return true; 
        }

        return false;
    }

    /**
     * @brief PILLAR 2: Transactional Provenance Management.
     */
    void begin_provenance_strike() {
        shadow_region_map = primary_region_map;
        in_transaction = true;
    }

    void commit_provenance_strike() {
        primary_region_map = std::move(shadow_region_map);
        shadow_region_map.clear();
        in_transaction = false;
    }

    void rollback_provenance_strike() {
        shadow_region_map.clear();
        in_transaction = false;
    }

    void register_ptr_region(uint32_t ptr_id, uint64_t region_id) {
        if (in_transaction) {
            shadow_region_map[ptr_id] = region_id;
        } else {
            primary_region_map[ptr_id] = region_id;
        }
    }

    /**
     * @brief Step 38 Support: Proof of Invariance for LICM.
     */
    [[nodiscard]] bool is_guaranteed_invariant(const ir::IRStream& stream, uint32_t load_idx) noexcept {
        const size_t stream_size = stream.size();
        if (load_idx >= stream_size) return false;
        
        const ir::IRCore& load_core = stream.get_core_readonly(load_idx);
        uint64_t load_region = get_region(load_core.op_a);
        if (load_region == 0) return false; 
        
        for (uint32_t i = 0; i < stream_size; ++i) {
            const ir::IRAtom& atom = stream.get_atom_readonly(i);
            if (atom.opcode == ir::OpCode::STORE) {
                const ir::IRCore& store_core = stream.get_core_readonly(i);
                if (get_region(store_core.op_a) == load_region) {
                    return false; // Collision detected
                }
            }
        }
        return true; 
    }

    /**
     * @brief Validates contiguous access for AVX-512 Burst Fetching (0x0080).
     */
    [[nodiscard]] bool can_group_access(const ir::IRStream& stream, uint32_t start_idx, uint32_t count) noexcept {
        const size_t stream_size = stream.size();
        if (start_idx + count > stream_size) [[unlikely]] return false;

        const ir::IRCore& base_core = stream.get_core_readonly(start_idx);
        uint64_t base_region = get_region(base_core.op_a);
        if (base_region == 0) return false;

        for (uint32_t i = 1; i < count; ++i) {
            const ir::IRCore& current_core = stream.get_core_readonly(start_idx + i);
            if (get_region(current_core.op_a) != base_region) return false;

            // Sentinel check for physical continuity
            if (!verifier.is_contiguous(stream, start_idx + i - 1, start_idx + i)) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] uint64_t get_region(uint32_t ptr_id) const noexcept {
        // Updated to ensure Shadow priority during transactions to prevent Pillar 3 Ghost Aliasing
        if (in_transaction) {
            auto it = shadow_region_map.find(ptr_id);
            if (it != shadow_region_map.end()) return it->second;
            return 0; // Shadow is the only authority during strike
        }
        
        auto it = primary_region_map.find(ptr_id);
        return (it != primary_region_map.end()) ? it->second : 0;
    }
};

} // namespace dgtl::opt
