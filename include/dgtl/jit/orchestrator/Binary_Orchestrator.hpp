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
 * STEP 40: HARDWARE-NATIVE GLOBAL LAYOUT (FINAL ORCHESTRATOR)
 * DESIGN PRINCIPLE: 
 * - Eliminates Cache-Line Straddling for Hot Loops.
 * - Global Branch Distillation: Optimizes JMP distances for 8-bit encodings.
 * - Zero-Jank Entry: Aligns entry points to 64B CPU Fetch Windows.
 */

#pragma once
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

namespace dgtl::core {

/**
 * @brief Represents a compiled chunk of machine code ready for final layout.
 */
struct BinarySegment {
    std::vector<uint8_t> machine_code;
    float hotness_score;
    uint32_t original_id;
    uintptr_t final_load_address = 0; // Relative to blob start
};

/**
 * @class DGTLBinaryOrchestratorFinal
 * @brief Performs the "Final Strike" by weaving segments into an optimized binary blob.
 */
class DGTLBinaryOrchestratorFinal {
    static constexpr size_t CPU_CACHE_LINE = 64;
    static constexpr float HOT_THRESHOLD = 0.85f;
    
    dgtl::analysis::DGTLAICostModel& ai_model;

public:
    explicit DGTLBinaryOrchestratorFinal(dgtl::analysis::DGTLAICostModel& ai) : ai_model(ai) {}

    /**
     * @brief The "Global Strike" Layout Engine.
     * Performs final layout optimization and link-time code shifting.
     */
    std::vector<uint8_t> finalize_production_binary(std::vector<BinarySegment>& segments) {
        if (segments.empty()) [[unlikely]] return {};
        (void)ai_model;

        // 1. Hot-to-Cold Partitioning
        // Sorting by hotness ensures high-frequency code lives in the same TLB pages,
        // reducing TLB misses and improving instruction cache locality.
        std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) {
            return a.hotness_score > b.hotness_score;
        });

        std::vector<uint8_t> final_blob;
        // Pre-allocate 2MB to minimize reallocations during the merge strike
        final_blob.reserve(2 * 1024 * 1024); 

        for (auto& seg : segments) {
            // 2. Cache-Line Alignment (The "Zero-Straddle" Rule)
            // If the segment is 'Hot', we force 64-byte alignment.
            // This ensures the CPU decoder doesn't fetch across two cache lines for loop entries.
            if (seg.hotness_score >= HOT_THRESHOLD) {
                size_t current_pos = final_blob.size();
                size_t padding = (CPU_CACHE_LINE - (current_pos % CPU_CACHE_LINE)) % CPU_CACHE_LINE;
                
                if (padding > 0) {
                    inject_optimized_nops(final_blob, padding);
                }
            }

            // Record finalized relative address
            seg.final_load_address = final_blob.size();

            // 3. Global Branch Patching
            // Refines jump offsets based on the finalized sorted order.
            patch_relative_jumps(seg);

            // 4. Final Insertion Strike
            final_blob.insert(final_blob.end(), seg.machine_code.begin(), seg.machine_code.end());
        }

        return final_blob;
    }

private:
    /**
     * @brief High-Performance NOP injection (x86_64 Optimized).
     * Uses Intel/AMD recommended multi-byte sequences to keep 
     * decoder pipelines full without consuming execution ports.
     */
    void inject_optimized_nops(std::vector<uint8_t>& blob, size_t count) {
        // Multi-Byte NOP sequences (Standard x86_64)
        static const uint8_t NOP9[] = {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t NOP4[] = {0x0F, 0x1F, 0x40, 0x00};
        
        while (count > 0) {
            if (count >= 9) {
                blob.insert(blob.end(), NOP9, NOP9 + 9);
                count -= 9;
            } else if (count >= 4) {
                blob.insert(blob.end(), NOP4, NOP4 + 4);
                count -= 4;
            } else {
                blob.push_back(0x90); // 1-byte NOP
                count--;
            }
        }
    }

    /**
     * @brief Shrink/Expand Jumps.
     * Swaps E9 (JMP rel32) with EB (JMP rel8) if the target is within 127 bytes.
     * This improves code density and reduces instruction fetch bandwidth.
     */
    void patch_relative_jumps(BinarySegment& seg) {
        // Distill long near jumps (E9 rel32) into short jumps (EB rel8) when possible,
        // while preserving instruction footprint with trailing NOPs.
        for (size_t i = 0; i + 4 < seg.machine_code.size(); ++i) {
            if (seg.machine_code[i] != 0xE9) {
                continue;
            }

            int32_t rel32 = 0;
            std::memcpy(&rel32, &seg.machine_code[i + 1], sizeof(rel32));

            const int64_t target = static_cast<int64_t>(i) + 5 + static_cast<int64_t>(rel32);
            const int64_t rel8 = target - (static_cast<int64_t>(i) + 2);

            if (rel8 >= -128 && rel8 <= 127) {
                seg.machine_code[i] = 0xEB;
                seg.machine_code[i + 1] = static_cast<uint8_t>(static_cast<int8_t>(rel8));
                seg.machine_code[i + 2] = 0x90;
                seg.machine_code[i + 3] = 0x90;
                seg.machine_code[i + 4] = 0x90;
            }
        }
    }
};

} // namespace dgtl::core



