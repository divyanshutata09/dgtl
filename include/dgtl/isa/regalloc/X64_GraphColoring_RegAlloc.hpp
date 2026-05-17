/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/regalloc/Strike_Live_Manager.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"

namespace dgtl::isa::regalloc {

struct GraphColoringAllocation {
    std::unordered_map<uint32_t, dgtl::exec::X64Reg> assigned_registers;
    std::vector<uint32_t> spilled_virtuals;
};

class X64GraphColoringRegAlloc {
public:
    [[nodiscard]] GraphColoringAllocation allocate(const ir::IRStream& stream) const {
        GraphColoringAllocation out{};
        if (stream.is_null() || stream.size() == 0) {
            return out;
        }

        // Build label map for loop-aware liveness intervals.
        std::unordered_map<uint32_t, size_t> label_map;
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);
            if (node.opcode == ir::OpCode::LABEL && node.result_id != ir::INVALID_SLOT) {
                label_map.emplace(node.result_id, i);
            }
        }

        StrikeLiveManager live;
        live.compute(stream, label_map);

        constexpr std::array<dgtl::exec::X64Reg, 9> kPalette = {
            dgtl::exec::X64Reg::RBX,
            dgtl::exec::X64Reg::R12,
            dgtl::exec::X64Reg::R13,
            dgtl::exec::X64Reg::R14,
            dgtl::exec::X64Reg::R15,
            dgtl::exec::X64Reg::R8,
            dgtl::exec::X64Reg::R9,
            dgtl::exec::X64Reg::R10,
            dgtl::exec::X64Reg::R11,
        };

        // Chaitin-Briggs style simplification approximation:
        // Use deterministic modulo mapping first, then mark values that exceed pressure as spills.
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);
            if (node.result_id == ir::INVALID_SLOT) {
                continue;
            }

            const auto it = out.assigned_registers.find(node.result_id);
            if (it == out.assigned_registers.end()) {
                const uint32_t vid = node.result_id;
                const size_t color_index = static_cast<size_t>(vid % kPalette.size());
                out.assigned_registers.emplace(vid, kPalette[color_index]);
            }

            // Lightweight pressure check: if too many live values remain, prefer spilling newer IDs.
            size_t live_count = 0;
            for (const auto& kv : out.assigned_registers) {
                if (!live.is_dead(kv.first, i)) {
                    ++live_count;
                }
            }

            if (live_count > kPalette.size()) {
                out.spilled_virtuals.push_back(node.result_id);
                out.assigned_registers.erase(node.result_id);
            }
        }

        std::sort(out.spilled_virtuals.begin(), out.spilled_virtuals.end());
        out.spilled_virtuals.erase(
            std::unique(out.spilled_virtuals.begin(), out.spilled_virtuals.end()),
            out.spilled_virtuals.end());

        return out;
    }
};

} // namespace dgtl::isa::regalloc
