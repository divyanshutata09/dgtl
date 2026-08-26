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

/*
 * DGTL Dataflow Analysis - Unified Liveness + Memory Dependency Module
 * Consolidates:
 * - Liveness_Analyzer.hpp
 * - Memory_Dependency_Smasher.hpp
 */

module;

#include <cstdlib>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_MSC_VER) || defined(__MINGW32__)
    #include <malloc.h>
#else
    #include <stdlib.h>
#endif

// FIXED: Bind to the correct intermediate macro layer space
export module dgtl.ir_core.liveness_memory_dependency_unified;

// FIXED CYCLIC LOOP: Import sibling leaves natively via short-form syntax

import dgtl.ir_core.stream;
import dgtl.ir_core.ai_cost_model;
import dgtl.ir_core.verified_formal;

export namespace dgtl::analysis {

struct alignas(8) LivenessRange {
    uint32_t start = ir::INVALID_SLOT;
    uint32_t end   = ir::INVALID_SLOT;
};

class LivenessAnalyzer {
private:
    size_t max_results;
    LivenessRange* ranges;
    std::vector<uint32_t> active_ids;
    bool corruption_detected = false;
    std::string corruption_reason;

public:
    LivenessAnalyzer(const LivenessAnalyzer&) = delete;
    LivenessAnalyzer& operator=(const LivenessAnalyzer&) = delete;
    LivenessAnalyzer(LivenessAnalyzer&&) = delete;
    LivenessAnalyzer& operator=(LivenessAnalyzer&&) = delete;

    explicit LivenessAnalyzer(int target_ram_gb = 32) {
        if (target_ram_gb <= 8)        max_results = 1024 * 512;
        else if (target_ram_gb <= 16)  max_results = 1024 * 1024;
        else if (target_ram_gb <= 32)  max_results = 1024 * 1024 * 2;
        else                           max_results = 1024 * 1024 * 8;

        size_t total_size = sizeof(LivenessRange) * max_results;

#if defined(_MSC_VER) || defined(__MINGW32__)
        ranges = static_cast<LivenessRange*>(_aligned_malloc(total_size, 64));
#else
        if (total_size % 64 != 0) total_size += (64 - (total_size % 64));
        ranges = static_cast<LivenessRange*>(std::aligned_alloc(64, total_size));
#endif

        if (!ranges) [[unlikely]] throw std::bad_alloc();

        active_ids.reserve(max_results / 128);
        cold_reset();
    }

    ~LivenessAnalyzer() {
#if defined(_MSC_VER) || defined(__MINGW32__)
        _aligned_free(ranges);
#else
        std::free(ranges);
#endif
    }

    void analyze_stream(const ir::IRStream& stream) {
        corruption_detected = false;
        corruption_reason.clear();

        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        for (uint32_t id : active_ids) {
            ranges[id] = LivenessRange{};
        }
        active_ids.clear();

        const uint32_t count = static_cast<uint32_t>(stream.size());

        for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
            const auto& node = stream.get_node(static_cast<size_t>(i));
            const uint32_t current_idx = static_cast<uint32_t>(i);

            const bool is_non_use_node = node.opcode == ir::OpCode::LOAD_IMM ||
                                         node.opcode == ir::OpCode::IMMEDIATE ||
                                         node.opcode == ir::OpCode::CONST ||
                                         node.opcode == ir::OpCode::STRING_CONST ||
                                         node.opcode == ir::OpCode::LABEL;
            if (!is_non_use_node) {
                if (node.op_a != ir::INVALID_SLOT) track_and_mark_use(node.op_a, current_idx);
                if (node.op_b != ir::INVALID_SLOT) track_and_mark_use(node.op_b, current_idx);

                if (node.op_c != ir::INVALID_SLOT && !(node.logic_flags & ir::HintFlags::SVEActive)) {
                    track_and_mark_use(node.op_c, current_idx);
                }
            }

            if (node.result_id != ir::INVALID_SLOT) {
                if (node.result_id >= max_results) [[unlikely]] {
                    trigger_corruption_trap(node.result_id, "ID_OVERFLOW");
                    return;
                }

                track_id(node.result_id);
                ranges[node.result_id].start = current_idx;
            }
        }

        verify_consistency();
    }

    [[nodiscard]] bool is_live_at(uint32_t result_id, uint32_t ins_idx) const noexcept {
        if (result_id >= max_results) [[unlikely]] return false;
        const auto& r = ranges[result_id];
        if (r.start == ir::INVALID_SLOT || r.end == ir::INVALID_SLOT) return false;
        return (ins_idx >= r.start && ins_idx <= r.end);
    }

    [[nodiscard]] bool is_live_after(uint32_t result_id, uint32_t ins_idx) const noexcept {
        if (ins_idx == 0xFFFFFFFFu) return false;
        return is_live_at(result_id, ins_idx + 1u);
    }

    [[nodiscard]] bool has_corruption() const noexcept {
        return corruption_detected;
    }

    [[nodiscard]] const std::string& last_error() const noexcept {
        return corruption_reason;
    }

private:
    void cold_reset() noexcept {
        std::fill(ranges, ranges + max_results, LivenessRange{});
    }

    inline void track_id(uint32_t id) noexcept {
        if (id < max_results && ranges[id].start == ir::INVALID_SLOT && ranges[id].end == ir::INVALID_SLOT) {
            active_ids.push_back(id);
        }
    }

    inline void track_and_mark_use(uint32_t id, uint32_t idx) noexcept {
        if (id >= max_results) [[unlikely]] return;
        track_id(id);
        if (ranges[id].end == ir::INVALID_SLOT) ranges[id].end = idx;
    }

    void verify_consistency() {
        const auto it = std::find_if(active_ids.begin(), active_ids.end(), [this](uint32_t id) {
            return ranges[id].start == ir::INVALID_SLOT;
        });
        if (it != active_ids.end()) [[unlikely]] {
            trigger_corruption_trap(*it, "DANGLING_ID_DETECTED");
        }
    }

    void trigger_corruption_trap(uint32_t id, const char* msg) noexcept {
        corruption_detected = true;
        corruption_reason = std::string("DGTL_CORRUPTION_TRAP: ") + msg + " [ResultID: " + std::to_string(id) + "]";
    }
};

class DGTLMemorySmasher {
    core::DGTLFormalVerifier& verifier;
    DGTLAICostModel& ai_model;

    std::unordered_map<uint32_t, uint64_t> primary_region_map;
    std::unordered_map<uint32_t, uint64_t> shadow_region_map;
    bool in_transaction = false;

public:
    static constexpr float AliasConfidenceThreshold = 0.90f;

    DGTLMemorySmasher(core::DGTLFormalVerifier& v, DGTLAICostModel& ai)
        : verifier(v), ai_model(ai) {}

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

        uint64_t reg_a = get_region(core_a.op_a);
        uint64_t reg_b = get_region(core_b.op_a);

        if (reg_a != 0 && reg_b != 0 && reg_a != reg_b) {
            return 0.0f;
        }

        if (verifier.verify_disjoint(stream, idx_a, idx_b)) {
            return 0.0f;
        }

        const ir::IRNode node_a = stream.get_node_readonly(idx_a);
        const ir::IRNode node_b = stream.get_node_readonly(idx_b);

        const float alias_risk = ai_model.predict_alias_risk(node_a, node_b);
        const float independence = ai_model.predict_independence(node_a, node_b);
        const float pointer_overlap = pointer_overlap_signal(core_a.op_a, core_b.op_a);

        return std::clamp(alias_risk * 0.55f + (1.0f - independence) * 0.30f + pointer_overlap * 0.15f, 0.0f, 1.0f);
    }

    [[nodiscard]] bool can_reorder(const ir::IRStream& stream, uint32_t store_idx, uint32_t load_idx) noexcept {
        const size_t stream_size = stream.size();
        if (store_idx >= stream_size || load_idx >= stream_size) [[unlikely]] {
            return false;
        }

        const ir::IRAtom store_atom_raw = stream.get_atom_readonly(store_idx);
        const ir::IRAtom load_atom_raw  = stream.get_atom_readonly(load_idx);

        ir::OpCode store_op = ir::AtomSplitter::get_opcode(store_atom_raw);
        ir::OpCode load_op  = ir::AtomSplitter::get_opcode(load_atom_raw);

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

        if (store_region != 0 && load_region != 0 && store_region != load_region) {
            return true;
        }

        if (store_region != 0 && load_region != 0 && store_region == load_region) {
            if (pointer_overlap_signal(store_core.op_a, load_core.op_a) >= 0.75f) {
                return false;
            }
        }

        if (verifier.verify_disjoint(stream, store_idx, load_idx)) {
            return true;
        }

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

    [[nodiscard]] bool validate_shadow_maps() const noexcept {
        return !in_transaction || !shadow_region_map.empty() || primary_region_map.empty();
    }

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

    [[nodiscard]] bool is_guaranteed_invariant(const ir::IRStream& stream, uint32_t load_idx) noexcept {
        const size_t stream_size = stream.size();
        if (load_idx >= stream_size) return false;

        const ir::IRCore& load_core = stream.get_core_readonly(load_idx);
        uint64_t load_region = get_region(load_core.op_a);
        if (load_region == 0) return false;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
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
