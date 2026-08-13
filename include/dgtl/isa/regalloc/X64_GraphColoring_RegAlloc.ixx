/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

module;

#include <cstdlib>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.ir_core.x64_graphcoloring_regalloc;

import dgtl.ir_core;
import dgtl.ir_core.strike_live_manager;
import dgtl.ir_core.x64_reg_map;

export namespace dgtl::isa::regalloc {

struct GraphColoringAllocation {
    std::unordered_map<uint32_t, dgtl::exec::X64Reg> assigned_registers;
    std::vector<uint32_t> spilled_virtuals;
};

class X64GraphColoringRegAlloc {
public:
    [[nodiscard]] static GraphColoringAllocation allocate(const ir::IRStream& stream) {
        GraphColoringAllocation out{};
        if (stream.is_null() || stream.size() == 0) {
            return out;
        }

        // Build label map for loop-aware liveness intervals.
        std::unordered_map<uint32_t, size_t> label_map;
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);
            if (node.opcode == ir::OpCode::LABEL && node.op_a != ir::INVALID_SLOT) {
                label_map[node.op_a] = i;
            }
        }

        dgtl::analysis::LivenessAnalyzer live;
        live.analyze_stream(stream);

        constexpr std::array<dgtl::exec::X64Reg, 7> kPalette = {
            dgtl::exec::X64Reg::RAX,
            dgtl::exec::X64Reg::RCX,
            dgtl::exec::X64Reg::RDX,
            dgtl::exec::X64Reg::RSI,
            dgtl::exec::X64Reg::RDI,
            dgtl::exec::X64Reg::R8,
            dgtl::exec::X64Reg::R9
        };

        // Track all virtual IDs that are produced by IR nodes so immediates/labels are not
        // misclassified as register-valued uses during allocation.
        std::unordered_map<uint32_t, bool> produced_ids;
        produced_ids.reserve(stream.size());
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);
            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                produced_ids.emplace(node.result_id, true);
            }
        }

        std::array<bool, kPalette.size()> reg_used{};

        const auto is_value_slot = [&](uint32_t slot) noexcept {
            return slot != ir::INVALID_SLOT && slot != 0u && produced_ids.find(slot) != produced_ids.end();
        };

        const auto reclaim_dead_assignments = [&](size_t node_index) {
            for (size_t r = 0; r < kPalette.size(); ++r) {
                if (!reg_used[r]) {
                    continue;
                }

                const bool still_live_owner = std::any_of(out.assigned_registers.begin(), out.assigned_registers.end(),
                    [&](const auto& kv) noexcept {
                        return kv.second == kPalette[r] && live.is_live_at(kv.first, static_cast<uint32_t>(node_index));
                    });

                if (!still_live_owner) {
                    reg_used[r] = false;
                }
            }
        };

        const auto ensure_assignment = [&](uint32_t vid, size_t node_index) {
            if (!is_value_slot(vid)) {
                return;
            }
            if (out.assigned_registers.find(vid) != out.assigned_registers.end()) {
                return;
            }

            reclaim_dead_assignments(node_index);
            for (size_t r = 0; r < kPalette.size(); ++r) {
                if (!reg_used[r]) {
                    reg_used[r] = true;
                    out.assigned_registers.emplace(vid, kPalette[r]);
                    return;
                }
            }

            out.spilled_virtuals.push_back(vid);
        };

        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::LABEL || node.opcode == ir::OpCode::GOTO) {
                reclaim_dead_assignments(i);
                continue;
            }

            if (node.opcode == ir::OpCode::BRANCH_COND ||
                node.opcode == ir::OpCode::BRANCH_LIKELY ||
                node.opcode == ir::OpCode::BRANCH_UNLIKELY) {
                ensure_assignment(node.op_a, i);
                reclaim_dead_assignments(i);
                continue;
            }

            // Ensure operand values are assigned before definition point use.
            ensure_assignment(node.op_a, i);
            ensure_assignment(node.op_b, i);
            ensure_assignment(node.op_c, i);

            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                ensure_assignment(node.result_id, i);
            }

            reclaim_dead_assignments(i);
        }

        std::sort(out.spilled_virtuals.begin(), out.spilled_virtuals.end());
        out.spilled_virtuals.erase(
            std::unique(out.spilled_virtuals.begin(), out.spilled_virtuals.end()),
            out.spilled_virtuals.end());

        return out;
    }
};

} // namespace dgtl::isa::regalloc
