/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Frame Lowering & Local Variable Layout
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

export module dgtl.isa.spirv.frame_lowering;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::spirv {

struct SPIRVFramePlan {
    bool valid = true;
    std::uint32_t local_bytes = 0;
    std::uint32_t spill_bytes = 0;
    std::uint32_t outgoing_bytes = 0;
    std::uint32_t alignment_padding = 0;
    std::uint32_t total_frame_bytes = 0;
    std::vector<dgtl::machine::MachineRegister> saved_registers;
};

class SPIRVFrameCalculator {
public:
    static constexpr bool is_callee_saved(std::uint32_t /*id*/) noexcept {
        // SPIR-V shader functions allocate function-scoped OpVariable variables;
        // hardware registers and spills are managed by GPU driver compilers.
        return false;
    }

    [[nodiscard]] static SPIRVFramePlan compute_plan(
        std::uint32_t local_bytes,
        std::uint32_t spill_bytes,
        std::uint32_t outgoing_bytes,
        const std::vector<dgtl::machine::MachineRegister>& /*clobbered*/,
        bool /*has_calls*/) noexcept {

        SPIRVFramePlan plan{};

        const std::uint64_t raw =
            static_cast<std::uint64_t>(local_bytes) +
            static_cast<std::uint64_t>(spill_bytes) +
            static_cast<std::uint64_t>(outgoing_bytes);

        if (raw > std::numeric_limits<std::uint32_t>::max()) {
            plan.valid = false;
            return plan;
        }

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

        const SPIRVFramePlan plan =
            compute_plan(local_bytes, spill_bytes,
                         frame.outgoing_argument_bytes, callee_saved, frame.has_calls);
        if (!plan.valid) return false;

        if (static_cast<std::uint64_t>(used) > plan.total_frame_bytes) return false;

        frame.stack_alignment = 16u;
        frame.local_bytes = plan.local_bytes;
        frame.spill_bytes = plan.spill_bytes;
        frame.callee_saved_registers = plan.saved_registers;
        frame.callee_saved_bytes = 0u;
        frame.alignment_padding = plan.alignment_padding;
        frame.total_frame_bytes = plan.total_frame_bytes;

        function.properties().frame_lowered = true;
        function.set_phase(MachinePhase::FrameLowered);
        return true;
    }
};

} // namespace dgtl::isa::spirv
