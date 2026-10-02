/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm / AMDGPU Scratch & Private Memory Frame Lowering Engine
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

export module dgtl.isa.amdgpu.frame_lowering;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::amdgpu {

struct ROCmFramePlan {
    bool valid = true;
    std::uint32_t local_bytes = 0;
    std::uint32_t spill_bytes = 0;
    std::uint32_t outgoing_bytes = 0;
    std::uint32_t alignment_padding = 0;
    std::uint32_t total_frame_bytes = 0;
    std::vector<dgtl::machine::MachineRegister> saved_registers;
};

class ROCmFrameCalculator {
public:
    static constexpr bool is_callee_saved_sgpr(std::uint32_t id) noexcept {
        // AMDGPU ABI: s[30:31] link register, s[33..] callee-saved depending on calling convention
        return id >= 33u && id <= 47u;
    }

    [[nodiscard]] static ROCmFramePlan compute_plan(
        std::uint32_t local_bytes,
        std::uint32_t spill_bytes,
        std::uint32_t outgoing_bytes,
        const std::vector<dgtl::machine::MachineRegister>& clobbered,
        bool has_calls) noexcept {

        ROCmFramePlan plan{};

        for (const auto reg : clobbered) {
            if (!reg.is_physical()) {
                plan.valid = false;
                return plan;
            }

            const bool valid_sgpr =
                (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
                 reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
                 reg.reg_class == dgtl::machine::RegisterClassKind::Address) &&
                is_callee_saved_sgpr(reg.id);

            if (!valid_sgpr) continue;

            if (std::find_if(plan.saved_registers.begin(), plan.saved_registers.end(),
                             [&](const auto& x) noexcept { return x.id == reg.id; }) ==
                plan.saved_registers.end()) {
                plan.saved_registers.push_back(reg);
            }
        }

        if (has_calls) {
            // Save link register (s[30:31])
            dgtl::machine::MachineRegister lr_lo{};
            lr_lo.id = 30u;
            lr_lo.kind = dgtl::machine::RegisterKind::Physical;
            lr_lo.reg_class = dgtl::machine::RegisterClassKind::General;

            dgtl::machine::MachineRegister lr_hi{};
            lr_hi.id = 31u;
            lr_hi.kind = dgtl::machine::RegisterKind::Physical;
            lr_hi.reg_class = dgtl::machine::RegisterClassKind::General;

            if (std::find_if(plan.saved_registers.begin(), plan.saved_registers.end(),
                             [](const auto& x) noexcept { return x.id == 30u; }) == plan.saved_registers.end()) {
                plan.saved_registers.push_back(lr_lo);
            }
            if (std::find_if(plan.saved_registers.begin(), plan.saved_registers.end(),
                             [](const auto& x) noexcept { return x.id == 31u; }) == plan.saved_registers.end()) {
                plan.saved_registers.push_back(lr_hi);
            }
        }

        std::sort(plan.saved_registers.begin(), plan.saved_registers.end(),
                  [](const auto& a, const auto& b) noexcept { return a.id < b.id; });

        const std::uint64_t callee_saved_bytes =
            static_cast<std::uint64_t>(plan.saved_registers.size()) * 4ull;

        const std::uint64_t raw =
            static_cast<std::uint64_t>(local_bytes) +
            static_cast<std::uint64_t>(spill_bytes) +
            static_cast<std::uint64_t>(outgoing_bytes) +
            callee_saved_bytes;

        if (raw > std::numeric_limits<std::uint32_t>::max()) {
            plan.valid = false;
            return plan;
        }

        // AMDGPU scratch alignment: 16-byte
        const std::uint32_t remainder = static_cast<std::uint32_t>(raw % 16u);
        plan.alignment_padding = remainder == 0u ? 0u : 16u - remainder;

        const std::uint64_t total = raw + plan.alignment_padding;
        if (total > std::numeric_limits<std::uint32_t>::max()) {
            plan.valid = false;
            return plan;
        }

        plan.local_bytes = local_bytes;
        plan.spill_bytes = spill_bytes;
        plan.outgoing_bytes = outgoing_bytes;
        plan.total_frame_bytes = static_cast<std::uint32_t>(total);
        return plan;
    }

    [[nodiscard]] static bool apply_machine_function(
        dgtl::machine::MachineFunction& function,
        const std::vector<dgtl::machine::MachineRegister>& callee_saved) noexcept {

        using namespace dgtl::machine;

        if (!function.is_phase_at_least(MachinePhase::PostRegAlloc) ||
            function.has_virtual_registers()) {
            return false;
        }

        std::vector<MachineRegister> clobbered;
        clobbered.reserve(callee_saved.size());

        for (const auto reg : callee_saved) {
            if (!reg.is_physical()) return false;

            const bool valid_sgpr =
                (reg.reg_class == RegisterClassKind::General ||
                 reg.reg_class == RegisterClassKind::Integer ||
                 reg.reg_class == RegisterClassKind::Address) &&
                is_callee_saved_sgpr(reg.id);

            if (!valid_sgpr) continue;

            if (std::find_if(clobbered.begin(), clobbered.end(),
                             [&](const auto& x) { return x.id == reg.id; }) != clobbered.end()) {
                continue;
            }
            clobbered.push_back(reg);
        }

        auto& frame = function.frame_info();
        std::uint32_t local_bytes = 0;
        std::uint32_t spill_bytes = 0;

        std::int64_t used = 0;
        for (auto& slot : frame.slots) {
            if (slot.size_bytes == 0u || slot.alignment == 0u) return false;

            if (slot.kind == StackSlotKind::Spill) {
                if (spill_bytes > std::numeric_limits<std::uint32_t>::max() - slot.size_bytes)
                    return false;
                spill_bytes += slot.size_bytes;
            } else {
                if (local_bytes > std::numeric_limits<std::uint32_t>::max() - slot.size_bytes)
                    return false;
                local_bytes += slot.size_bytes;
            }

            const std::int64_t alignment = std::max<std::int64_t>(1, slot.alignment);
            const std::int64_t mask = alignment - 1;
            if (used > std::numeric_limits<std::int64_t>::max() - mask) return false;
            used = (used + mask) & ~mask;
            if (used > std::numeric_limits<std::int64_t>::max() -
                        static_cast<std::int64_t>(slot.size_bytes)) {
                return false;
            }
            slot.offset = used;
            used += slot.size_bytes;
            slot.fixed_offset = true;
        }

        const ROCmFramePlan plan =
            compute_plan(local_bytes, spill_bytes,
                         frame.outgoing_argument_bytes, clobbered, frame.has_calls);
        if (!plan.valid) return false;

        if (static_cast<std::uint64_t>(used) > plan.total_frame_bytes) return false;

        frame.stack_alignment = 16u;
        frame.local_bytes = plan.local_bytes;
        frame.spill_bytes = plan.spill_bytes;
        frame.callee_saved_registers = plan.saved_registers;
        frame.callee_saved_bytes =
            static_cast<std::uint32_t>(plan.saved_registers.size() * 4u);
        frame.alignment_padding = plan.alignment_padding;
        frame.total_frame_bytes = plan.total_frame_bytes;

        function.properties().frame_lowered = true;
        function.set_phase(MachinePhase::FrameLowered);
        return true;
    }
};

} // namespace dgtl::isa::amdgpu