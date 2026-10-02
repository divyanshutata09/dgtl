/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Pure Machine IR Live Interval & Register Liveness Manager
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>

export module dgtl.mir.strike_live_manager;

import dgtl.mir.machine_ir;

export namespace dgtl::mir::regalloc {

using dgtl::machine::InstrId;
using dgtl::machine::RegId;
using dgtl::machine::BlockId;
using dgtl::machine::MachineFunction;
using dgtl::machine::MachineBasicBlock;
using dgtl::machine::MachineInstruction;
using dgtl::machine::MachineOperand;
using dgtl::machine::MachineRegister;
using dgtl::machine::INVALID_INSTR_ID;
using dgtl::machine::INVALID_REG;
using dgtl::machine::INVALID_BLOCK_ID;

struct LiveRangeSegment {
    InstrId start = INVALID_INSTR_ID;
    InstrId end = INVALID_INSTR_ID;

    [[nodiscard]] constexpr bool contains(InstrId point) const noexcept {
        return point >= start && point <= end;
    }

    [[nodiscard]] constexpr bool overlaps(const LiveRangeSegment& other) const noexcept {
        return !(end < other.start || other.end < start);
    }
};

struct LiveInterval {
    InstrId start_instr = INVALID_INSTR_ID;
    InstrId end_instr = INVALID_INSTR_ID;
    bool active = false;
    double spill_weight = 0.0;
    uint32_t max_loop_depth = 0;
    std::vector<LiveRangeSegment> segments;

    void add_segment(InstrId start, InstrId end) {
        if (start > end) return;
        for (auto& seg : segments) {
            if (seg.overlaps(LiveRangeSegment{start, end}) ||
                seg.end + 1 == start || end + 1 == seg.start) {
                seg.start = std::min(seg.start, start);
                seg.end = std::max(seg.end, end);
                start_instr = std::min(start_instr, seg.start);
                end_instr = std::max(end_instr, seg.end);
                return;
            }
        }
        segments.push_back(LiveRangeSegment{start, end});
        if (!active) {
            start_instr = start;
            end_instr = end;
            active = true;
        } else {
            start_instr = std::min(start_instr, start);
            end_instr = std::max(end_instr, end);
        }
    }

    [[nodiscard]] bool is_live_at(InstrId point) const noexcept {
        if (!active || point < start_instr || point > end_instr) {
            return false;
        }
        if (segments.empty()) {
            return true;
        }
        for (const auto& seg : segments) {
            if (seg.contains(point)) return true;
        }
        return false;
    }
};

class StrikeLiveManager {
    std::vector<LiveInterval> intervals_;

public:
    /**
     * @brief Computes register live intervals directly across the MachineFunction CFG.
     * Operates strictly on MachineOperand def/use roles with zero knowledge of frontend IR.
     */
    void compute(const MachineFunction& function) {
        intervals_.clear();
        const std::size_t vreg_count = function.virtual_register_count();
        intervals_.resize(vreg_count);

        const auto& loop_info = function.loop_info();

        // 1. Traverse all blocks and instructions to establish defs and uses
        for (const auto& bb : function.blocks()) {
            const uint32_t depth = loop_info.get_loop_depth(bb.id);
            const double depth_multiplier = std::pow(10.0, static_cast<double>(std::min(depth, 5u)));

            for (const InstrId inst_id : bb.instructions) {
                const auto* inst = function.instruction(inst_id);
                if (!inst) continue;

                for (const MachineOperand& op : inst->operands) {
                    if (!op.is_register() || !op.reg.valid()) continue;

                    const RegId r = op.reg.id;
                    if (r >= intervals_.size()) {
                        intervals_.resize(static_cast<std::size_t>(r) + 1);
                    }

                    auto& interval = intervals_[r];
                    interval.max_loop_depth = std::max(interval.max_loop_depth, depth);
                    interval.spill_weight += depth_multiplier;

                    if (op.is_def()) {
                        interval.add_segment(inst_id, inst_id);
                    } else if (op.is_use()) {
                        const InstrId start = (interval.start_instr != INVALID_INSTR_ID) ? interval.start_instr : inst_id;
                        interval.add_segment(start, inst_id);
                    }
                }
            }
        }

        // 2. Loop-carried live range extension (Natural loops)
        for (const auto& loop : loop_info.loops()) {
            if (loop.blocks.empty()) continue;

            InstrId loop_start = INVALID_INSTR_ID;
            InstrId loop_end = INVALID_INSTR_ID;

            for (const BlockId b : loop.blocks) {
                const auto* bb = function.block(b);
                if (!bb || bb->instructions.empty()) continue;

                const InstrId first = bb->instructions.front();
                const InstrId last = bb->instructions.back();

                loop_start = (loop_start == INVALID_INSTR_ID) ? first : std::min(loop_start, first);
                loop_end = (loop_end == INVALID_INSTR_ID) ? last : std::max(loop_end, last);
            }

            if (loop_start != INVALID_INSTR_ID && loop_end != INVALID_INSTR_ID) {
                for (auto& interval : intervals_) {
                    if (interval.active && interval.start_instr <= loop_end && interval.end_instr >= loop_start) {
                        interval.add_segment(loop_start, loop_end);
                        interval.max_loop_depth = std::max(interval.max_loop_depth, loop.depth);
                    }
                }
            }
        }

        // 3. Normalize spill weights by live interval lifetime span
        for (auto& interval : intervals_) {
            if (interval.active) {
                const uint32_t span = (interval.end_instr >= interval.start_instr)
                    ? (interval.end_instr - interval.start_instr + 1)
                    : 1;
                interval.spill_weight /= static_cast<double>(span);
            }
        }
    }

    [[nodiscard]] inline bool is_dead(RegId reg_id, InstrId current_instr) const noexcept {
        if (reg_id == INVALID_REG || static_cast<std::size_t>(reg_id) >= intervals_.size()) {
            return true;
        }
        const auto& interval = intervals_[reg_id];
        if (!interval.active) {
            return true;
        }
        return current_instr >= interval.end_instr || !interval.is_live_at(current_instr);
    }

    [[nodiscard]] inline const LiveInterval* get_interval(RegId reg_id) const noexcept {
        if (reg_id == INVALID_REG || static_cast<std::size_t>(reg_id) >= intervals_.size()) {
            return nullptr;
        }
        const auto& interval = intervals_[reg_id];
        return interval.active ? &interval : nullptr;
    }

    [[nodiscard]] inline double spill_weight(RegId reg_id) const noexcept {
        const auto* interval = get_interval(reg_id);
        return interval ? interval->spill_weight : 0.0;
    }
};

} // namespace dgtl::mir::regalloc