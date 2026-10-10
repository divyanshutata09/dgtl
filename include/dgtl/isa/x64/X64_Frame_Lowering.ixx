/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Win64 Frame Lowering Engine
 * Purpose:     Pure mathematical frame layout calculator and 16-byte alignment guarantee
 */

module;

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <string>
#include <limits>
export module dgtl.isa.lowering.x64.frame_lowering;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::lowering::x64 {

enum class X64Register : uint8_t {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15
};

struct FrameAllocationPlan {
    bool valid = true;
    uint32_t total_stack_bytes = 0;
    uint32_t local_and_spill_offset = 0;
    uint32_t shadow_space_bytes = 32;
    uint32_t alignment_pad_bytes = 0;
    std::vector<X64Register> saved_non_volatile_regs;
};

class Win64FrameCalculator {
public:
    static constexpr bool is_callee_saved(X64Register reg) noexcept {
        switch (reg) {
            case X64Register::RBX:
            case X64Register::RBP:
            case X64Register::RDI:
            case X64Register::RSI:
            case X64Register::R12:
            case X64Register::R13:
            case X64Register::R14:
            case X64Register::R15:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static FrameAllocationPlan compute_plan(
        uint32_t local_storage_bytes,
        uint32_t spill_slot_bytes,
        uint32_t max_outgoing_args_bytes,
        const std::vector<X64Register>& clobbered_registers,
        bool makes_external_calls
    ) noexcept {
        FrameAllocationPlan plan;

        // Filter and isolate strictly non-volatile registers that must be saved
        for (const auto reg : clobbered_registers) {
            // RBP is reserved by the fixed-frame-pointer policy below; it is
            // not represented in saved_non_volatile_regs because the prologue
            // owns its preservation explicitly.
            if (reg == X64Register::RBP) continue;
            if (is_callee_saved(reg)) {
                if (std::find(plan.saved_non_volatile_regs.begin(), 
                              plan.saved_non_volatile_regs.end(), reg) == plan.saved_non_volatile_regs.end()) {
                    plan.saved_non_volatile_regs.push_back(reg);
                }
            }
        }

        // Sort to maintain deterministic prologue push ordering
        std::sort(plan.saved_non_volatile_regs.begin(), plan.saved_non_volatile_regs.end());

        const std::uint64_t callee_saved_bytes =
            static_cast<std::uint64_t>(plan.saved_non_volatile_regs.size()) * 8ull;

        // Outgoing argument space: Windows x64 requires a 32-byte shadow space if any call is made
        plan.shadow_space_bytes = makes_external_calls ? std::max<uint32_t>(32, max_outgoing_args_bytes) : 0;
        
        const std::uint64_t raw_locals =
            static_cast<std::uint64_t>(local_storage_bytes) +
            static_cast<std::uint64_t>(spill_slot_bytes);
        if (raw_locals > UINT32_MAX) {
            plan.valid = false;
            return plan;
        }

        plan.local_and_spill_offset = plan.shadow_space_bytes;

        const std::uint64_t raw_body =
            raw_locals + static_cast<std::uint64_t>(plan.shadow_space_bytes);

        // Mathematical Stack Alignment:
        // Initial state at function entry: (RSP % 16 == 8) due to return address pushed by CALL.
        // Pushing RBP: (RSP % 16 == 0).
        // Pushing callee-saved registers: RSP changes by (callee_saved_bytes).
        // Total bytes pushed on stack before 'sub rsp, total_stack_bytes':
        //   PushedBytes = 8 (RetAddr) + 8 (RBP) + callee_saved_bytes
        const uint32_t total_pushed_before_sub = 8 + 8 + callee_saved_bytes;
        
        // We need: (total_pushed_before_sub + raw_body + pad) % 16 == 0
        const std::uint64_t alignment_total =
            static_cast<std::uint64_t>(total_pushed_before_sub) + raw_body;
        const std::uint32_t remainder =
            static_cast<std::uint32_t>(alignment_total % 16ull);
        plan.alignment_pad_bytes = remainder == 0 ? 0u : 16u - remainder;

        const std::uint64_t total_stack =
            raw_body + static_cast<std::uint64_t>(plan.alignment_pad_bytes);
        if (total_stack > UINT32_MAX) {
            plan.valid = false;
            return plan;
        }
        plan.total_stack_bytes = static_cast<uint32_t>(total_stack);
        return plan;
    }

    [[nodiscard]] static bool verify_invariants(
        const FrameAllocationPlan& plan,
        bool makes_external_calls
    ) noexcept {
        if (!plan.valid) return false;

        const uint32_t callee_saved_bytes = static_cast<uint32_t>(plan.saved_non_volatile_regs.size()) * 8;
        const uint32_t total_pushed = 8 /*RetAddr*/ + 8 /*RBP*/ + callee_saved_bytes;
        const uint32_t stack_depth_at_call = total_pushed + plan.total_stack_bytes;
        
        // 1. Stack must be 16-byte aligned before any CALL
        if ((stack_depth_at_call % 16) != 0) {
            return false;
        }
        
        // 2. If making external calls, shadow space must be at least 32 bytes
        if (makes_external_calls && plan.shadow_space_bytes < 32) {
            return false;
        }
        
        // 3. Total stack bytes must account for shadow space and alignment pad
        if (plan.total_stack_bytes < (plan.shadow_space_bytes + plan.alignment_pad_bytes)) {
            return false;
        }

        return true;
    }

/*
     * Apply the calculated Win64 frame contract directly to MIR.
     *
     * This pass consumes only MachineFrameInfo and physical MachineRegisters.
     * It does not inspect IR nodes or legacy virtual-value home offsets.
     */
    [[nodiscard]] static bool apply_machine_function(
        dgtl::machine::MachineFunction& function,
        const std::vector<dgtl::machine::MachineRegister>& callee_saved) noexcept {
        if (!function.is_phase_at_least(dgtl::machine::MachinePhase::PostRegAlloc) ||
            function.has_virtual_registers()) {
            return false;
        }

        std::vector<X64Register> clobbered;
        clobbered.reserve(callee_saved.size());
        for (const auto& reg : callee_saved) {
            if (!reg.is_physical() ||
                reg.reg_class != dgtl::machine::RegisterClassKind::General ||
                reg.id >= 16u) {
                return false;
            }
            const auto xreg = static_cast<X64Register>(reg.id);
            if (xreg == X64Register::RSP || xreg == X64Register::RBP ||
                xreg == X64Register::RAX || xreg == X64Register::RCX ||
                xreg == X64Register::RDX ||
                xreg == X64Register::R8 ||
                xreg == X64Register::R9 || xreg == X64Register::R10 ||
                xreg == X64Register::R11) {
                return false;
            }
            if (std::find(clobbered.begin(), clobbered.end(), xreg) == clobbered.end()) {
                clobbered.push_back(xreg);
            }
        }

        auto& frame = function.frame_info();
        std::uint32_t local_bytes = 0;
        std::uint32_t spill_bytes = 0;

        for (auto& slot : frame.slots) {
            const std::uint32_t size = slot.size_bytes;
            if (slot.kind == dgtl::machine::StackSlotKind::Spill) {
                if (spill_bytes > UINT32_MAX - size) return false;
                spill_bytes += size;
            } else {
                if (local_bytes > UINT32_MAX - size) return false;
                local_bytes += size;
            }
        }

        const FrameAllocationPlan plan = compute_plan(
            local_bytes,
            spill_bytes,
            frame.outgoing_argument_bytes,
            clobbered,
            frame.has_calls);

        if (!verify_invariants(plan, frame.has_calls)) {
            return false;
        }

        /*
         * Stack slots are addressed from RBP.  RBP itself and every
         * explicitly preserved non-volatile register occupy the positive
         * prologue depth below the incoming return address.  Start local
         * storage below that preserved-register area so a spill can never
         * alias a saved register.
         */
        std::int64_t used =
            8ll + static_cast<std::int64_t>(plan.saved_non_volatile_regs.size()) * 8ll;
        for (auto& slot : frame.slots) {
            const std::int64_t alignment =
                std::max<std::int64_t>(1, slot.alignment);
            const std::int64_t mask = alignment - 1;
            if (used > (std::numeric_limits<std::int64_t>::max() - mask)) return false;
            used = (used + mask) & ~mask;
            if (used > (std::numeric_limits<std::int64_t>::max() -
                        static_cast<std::int64_t>(slot.size_bytes))) return false;
            used += static_cast<std::int64_t>(slot.size_bytes);
            slot.offset = -used;
            slot.fixed_offset = true;
        }

        frame.stack_alignment = 16;
        frame.local_bytes = local_bytes;
        frame.spill_bytes = spill_bytes;
        frame.callee_saved_registers.clear();
        frame.callee_saved_registers.reserve(plan.saved_non_volatile_regs.size());
        for (const auto reg : plan.saved_non_volatile_regs) {
            frame.callee_saved_registers.push_back(
                dgtl::machine::MachineRegister{
                    static_cast<std::uint32_t>(reg),
                    dgtl::machine::RegisterKind::Physical,
                    dgtl::machine::RegisterClassKind::General});
        }
        frame.callee_saved_bytes =
            static_cast<std::uint32_t>(plan.saved_non_volatile_regs.size() * 8u);
        frame.alignment_padding = plan.alignment_pad_bytes;
        frame.total_frame_bytes = plan.total_stack_bytes;

        function.properties().frame_lowered = true;
        function.set_phase(dgtl::machine::MachinePhase::FrameLowered);
        return true;
    }

};

} // namespace dgtl::isa::lowering::x64
