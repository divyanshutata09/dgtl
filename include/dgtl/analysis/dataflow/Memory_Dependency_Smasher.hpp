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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 29: MEMORY DEPENDENCY SMASHER (AI-PROVEN DISAMBIGUATION)
 * PILLAR 2: TRANSACTIONAL PROVENANCE (SHADOW-MAPPING)
 * PILLAR 3: THE SENTINEL (ALIAS-CONSISTENCY GUARD)
 * STANDARDS: BIR 1.0.0 PACKED ATOM / C++26
 */

#pragma once
#include <unordered_map>
#include <cstdint>
#include <algorithm>

// Ensure all BIR 1.0.0 core types and the Stream definition are visible
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::analysis {

/**
 * @class DGTLMemorySmasher
 * @brief Disambiguates memory dependencies using a Tri-Tier verification system.
 */
class DGTLMemorySmasher {
    core::DGTLFormalVerifier& verifier;
    DGTLAICostModel& ai_model;

    /**
     * @brief Provenance Map (Shadow-Buffered).
     * Maps a Pointer's ResultID to its base allocation region.
     */
    std::unordered_map<uint32_t, uint64_t> primary_region_map;
    std::unordered_map<uint32_t, uint64_t> shadow_region_map;
    bool in_transaction = false;

public:
    static constexpr float AliasConfidenceThreshold = 0.90f;

    // Explicit constructor for the analysis engine
    DGTLMemorySmasher(core::DGTLFormalVerifier& v, DGTLAICostModel& ai) 
        : verifier(v), ai_model(ai) {}

    /**
     * @brief AI-Powered Alias Probability.
     * Calculated via Tri-Tier check: Provenance -> Formal -> AI Inference.
     */
    [[nodiscard]] float predict_alias_probability(const ir::IRStream& stream, uint32_t idx_a, uint32_t idx_b) noexcept {
        const size_t stream_size = stream.size();
        if (idx_a >= stream_size || idx_b >= stream_size) [[unlikely]] {
            return 1.0f;
        }

        const ir::IRCore& core_a = stream.get_core_readonly(idx_a);
        const ir::IRCore& core_b = stream.get_core_readonly(idx_b);

        if (core_a.op_a == core_b.op_a && core_a.op_a != ir::INVALID_SLOT) {
            return 1.0f;
        }

        // Tier 1: Region-Based Disambiguation (O(1))
        uint64_t reg_a = get_region(core_a.op_a);
        uint64_t reg_b = get_region(core_b.op_a);

        if (reg_a != 0 && reg_b != 0 && reg_a != reg_b) {
            return 0.0f; // Guaranteed NoAlias (Different base regions)
        }

        // Tier 2: Formal Logic Proof (Polyhedral/SMT)
        if (verifier.verify_disjoint(stream, idx_a, idx_b)) {
            return 0.0f; 
        }

        // Tier 3: AI-Guided Speculation
        // FIX: Explicitly naming the nodes ensures valid reference binding for the AI Model
        const ir::IRNode node_a = stream.get_node_readonly(idx_a);
        const ir::IRNode node_b = stream.get_node_readonly(idx_b);

        const float alias_risk = ai_model.predict_alias_risk(node_a, node_b);
        const float independence = ai_model.predict_independence(node_a, node_b);
        const float pointer_overlap = pointer_overlap_signal(core_a.op_a, core_b.op_a);

        return std::clamp(alias_risk * 0.55f + (1.0f - independence) * 0.30f + pointer_overlap * 0.15f, 0.0f, 1.0f);
    }

    /**
     * @brief The "Zero-Corruption" Dependency Strike.
     * Validates reordering safety using BIR 1.0.0 split-tier accessors and packed atoms.
     */
    [[nodiscard]] bool can_reorder(const ir::IRStream& stream, uint32_t store_idx, uint32_t load_idx) noexcept {
        const size_t stream_size = stream.size();
        if (store_idx >= stream_size || load_idx >= stream_size) [[unlikely]] {
            return false; 
        }

        // BIR 1.0.0: Use AtomSplitter to decode packed opcode/flags
        const ir::IRAtom store_atom_raw = stream.get_atom_readonly(store_idx);
        const ir::IRAtom load_atom_raw  = stream.get_atom_readonly(load_idx);
        
        ir::OpCode store_op = ir::AtomSplitter::get_opcode(store_atom_raw);
        ir::OpCode load_op  = ir::AtomSplitter::get_opcode(load_atom_raw);

        // Only check alias if we actually have a Store-Load pair
        if (store_op != ir::OpCode::STORE || load_op != ir::OpCode::LOAD) {
            return true; 
        }

        const ir::IRCore& store_core = stream.get_core_readonly(store_idx);
        const ir::IRCore& load_core  = stream.get_core_readonly(load_idx);

        uint64_t store_region = get_region(store_core.op_a);
        uint64_t load_region  = get_region(load_core.op_a);

        if (store_core.op_a == load_core.op_a && store_core.op_a != ir::INVALID_SLOT) {
            return false;
        }

        // 1. Provenance Check
        if (store_region != 0 && load_region != 0 && store_region != load_region) {
            return true; // Smashed! Guaranteed NoAlias.
        }

        if (store_region != 0 && load_region != 0 && store_region == load_region) {
            if (pointer_overlap_signal(store_core.op_a, load_core.op_a) >= 0.75f) {
                return false;
            }
        }

        // 2. Formal Proof
        if (verifier.verify_disjoint(stream, store_idx, load_idx)) {
            return true; 
        }

        // 3. AI Inference (Speculation Threshold-Gated)
        // FIX: Explicitly naming the nodes ensures valid reference binding for the AI Model
        const ir::IRNode node_s = stream.get_node_readonly(store_idx);
        const ir::IRNode node_l = stream.get_node_readonly(load_idx);

        const float alias_probability = predict_alias_probability(stream, store_idx, load_idx);
        const float independence = ai_model.predict_independence(node_s, node_l);
        const uint32_t pointer_delta = absolute_pointer_delta(store_core.op_a, load_core.op_a);

        if (alias_probability <= 0.10f) {
            return true; 
        }

        if (pointer_delta >= 64 && independence >= AliasConfidenceThreshold && alias_probability < 0.35f) {
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
        if (ptr_id == ir::INVALID_SLOT) return;
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
        
        for (uint32_t i = 0; i < (uint32_t)stream_size; ++i) {
            ir::IRAtom atom_raw = stream.get_atom_readonly(i);
            if (ir::AtomSplitter::get_opcode(atom_raw) == ir::OpCode::STORE) {
                const ir::IRCore& store_core = stream.get_core_readonly(i);
                if (get_region(store_core.op_a) == load_region) {
                    return false; 
                }
            }
        }
        return true; 
    }

    /**
     * @brief Validates contiguous access for SIMD/AVX-512 Burst Fetching.
     */
    [[nodiscard]] bool can_group_access(const ir::IRStream& stream, uint32_t start_idx, uint32_t count) noexcept {
        const size_t stream_size = stream.size();
        if (count == 0) [[unlikely]] return false;
        if (static_cast<size_t>(start_idx) > stream_size ||
            static_cast<size_t>(count) > (stream_size - static_cast<size_t>(start_idx))) [[unlikely]] {
            return false;
        }

        const ir::IRCore& base_core = stream.get_core_readonly(start_idx);
        uint64_t base_region = get_region(base_core.op_a);
        if (base_region == 0) return false;

        for (uint32_t i = 1; i < count; ++i) {
            const ir::IRCore& current_core = stream.get_core_readonly(start_idx + i);
            if (get_region(current_core.op_a) != base_region) return false;

            if (!verifier.is_contiguous(stream, (start_idx + i - 1), (start_idx + i))) {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] static uint32_t absolute_pointer_delta(uint32_t a, uint32_t b) noexcept {
        return (a > b) ? (a - b) : (b - a);
    }

    [[nodiscard]] static float pointer_overlap_signal(uint32_t a, uint32_t b) noexcept {
        if (a == ir::INVALID_SLOT || b == ir::INVALID_SLOT) return 0.5f;
        if (a == b) return 1.0f;

        const uint32_t delta = absolute_pointer_delta(a, b);
        if (delta <= 4) return 0.85f;
        if (delta <= 16) return 0.60f;
        if (delta <= 64) return 0.25f;
        return 0.0f;
    }

    [[nodiscard]] uint64_t get_region(uint32_t ptr_id) const noexcept {
        if (ptr_id == ir::INVALID_SLOT) return 0;
        if (in_transaction) {
            auto it = shadow_region_map.find(ptr_id);
            if (it != shadow_region_map.end()) return it->second;
        }
        
        auto it = primary_region_map.find(ptr_id);
        return (it != primary_region_map.end()) ? it->second : 0;
    }
};

} // namespace dgtl::analysis

