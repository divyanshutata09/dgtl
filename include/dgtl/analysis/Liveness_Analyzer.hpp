/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 9: ULTRA-FAST LIVENESS ANALYZER (V5)
 * PILLAR 3: THE SENTINEL INTEGRATION
 * ZERO-CORRUPTION: Added Invariant Verification and Dangling-ID Traps.
 */

#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>
#include "../ir/Global_IR.hpp"


#if defined(_MSC_VER) || defined(__MINGW32__)
    #include <malloc.h>
#else
    #include <stdlib.h>
#endif

namespace dgtl::core {

/**
 * @struct LivenessRange
 * @brief Represents the start and end indices of a ResultID's lifespan.
 * Aligned to 8 bytes for dense packing in the range table.
 */
struct alignas(8) LivenessRange {
    uint32_t start = 0xFFFFFFFF;
    uint32_t end   = 0;
};

/**
 * @class LivenessAnalyzer
 * @brief High-velocity liveness analysis with integrated invariant checking.
 */
class LivenessAnalyzer {
private:
    size_t max_results;
    LivenessRange* ranges;
    std::vector<uint32_t> active_ids;

public:
    /**
     * @brief Adaptive Constructor for the 32GB RAM spec.
     * Pillar 1: Aligns memory for potential SIMD-accelerated Sentinel scans.
     */
    explicit LivenessAnalyzer(int target_ram_gb = 32) {
        if (target_ram_gb <= 8)        max_results = 1024 * 512;
        else if (target_ram_gb <= 16)  max_results = 1024 * 1024;
        else if (target_ram_gb <= 32)  max_results = 1024 * 1024 * 2;
        else                           max_results = 1024 * 1024 * 8;

        size_t total_size = sizeof(LivenessRange) * max_results;
        
#if defined(_MSC_VER) || defined(__MINGW32__)
        ranges = static_cast<LivenessRange*>(_aligned_malloc(total_size, 64));
#else
        // Ensure size is a multiple of alignment for std::aligned_alloc
        if (total_size % 64 != 0) total_size += (64 - (total_size % 64));
        ranges = static_cast<LivenessRange*>(std::aligned_alloc(64, total_size));
#endif

        if (!ranges) [[unlikely]] std::abort();
        
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

    /**
     * @brief High-Velocity Analysis with Invariant Validation.
     * PILLAR 3: Implements reverse-opt analysis to detect "Dangling IDs" 
     * where an operand is used without a preceding definition.
     */
    void analyze_stream(const ir::IRStream& stream) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        // 1. Sparse Reset: Only clear IDs used in the previous opt to maintain O(N)
        for (uint32_t id : active_ids) {
            ranges[id] = LivenessRange{}; // Safe replacement for manual reset
        }
        active_ids.clear();

        const uint32_t count = static_cast<uint32_t>(stream.size());

        // 2. Reverse opt with Dangling-ID Detection
        for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
            const auto& node = stream.get_node(static_cast<size_t>(i));
            const uint32_t current_idx = static_cast<uint32_t>(i);

            // Track Uses (Operands)
            if (node.op_a != ir::INVALID_SLOT) track_and_mark_use(node.op_a, current_idx);
            if (node.op_b != ir::INVALID_SLOT) track_and_mark_use(node.op_b, current_idx);
            
            // op_c can be an operand or an immediate/offset based on context
            if (node.op_c != ir::INVALID_SLOT && !(node.flags & ir::Flags::SVE_ACTIVE)) {
                track_and_mark_use(node.op_c, current_idx);
            }

            // Track Definitions (Pillar 3.1: DGTL-Invariant Guard)
            if (node.result_id != 0) {
                if (node.result_id >= max_results) [[unlikely]] {
                    trigger_corruption_trap(node.result_id, "ID_OVERFLOW");
                }
                
                track_id(node.result_id);
                // In reverse opt, the first time we see the result_id is its definition.
                ranges[node.result_id].start = current_idx;
            }
        }

        // 3. Final Sentinel Check: Verify every tracked ID reached its definition
        verify_consistency();
    }

    [[nodiscard]] bool is_live_at(uint32_t result_id, uint32_t ins_idx) const noexcept {
        if (result_id >= max_results) [[unlikely]] return false;
        const auto& r = ranges[result_id];
        return (ins_idx >= r.start && ins_idx <= r.end);
    }

private:
    /**
     * @brief Full-memory sweep to initialize the range table.
     */
    void cold_reset() noexcept {
        // Replaced memset with std::fill to satisfy -Wclass-memaccess
        std::fill(ranges, ranges + max_results, LivenessRange{});
    }

    /**
     * @brief Registers an ID in the active set if not already present.
     */
    inline void track_id(uint32_t id) noexcept {
        if (id < max_results && ranges[id].start == 0xFFFFFFFF && ranges[id].end == 0) {
            active_ids.push_back(id);
        }
    }

    /**
     * @brief Marks the last usage index of an ID (first found in reverse opt).
     */
    inline void track_and_mark_use(uint32_t id, uint32_t idx) noexcept {
        if (id >= max_results) [[unlikely]] return;
        track_id(id);
        if (ranges[id].end == 0) ranges[id].end = idx;
    }

    /**
     * @brief THE SENTINEL TRAP.
     * Ensures every instruction referencing a result finds a definition.
     */
    void verify_consistency() {
        for (uint32_t id : active_ids) {
            // If start is still 0xFFFFFFFF after the opt, the ID was used but never defined.
            if (ranges[id].start == 0xFFFFFFFF) [[unlikely]] {
                trigger_corruption_trap(id, "DANGLING_ID_DETECTED");
            }
        }
    }

    /**
     * @brief Triggers a fatal halt on IR corruption.
     */
    [[noreturn]] void trigger_corruption_trap(uint32_t id, const char* msg) {
        std::cerr << "DGTL_CORRUPTION_TRAP: " << msg << " [ResultID: " << id << "]\n";
        std::abort();
    }
};

} // namespace dgtl::core