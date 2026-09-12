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

export module dgtl.backend.isa.win64_frame_lowering;

export namespace dgtl::backend::isa {

enum class X64Register : uint8_t {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15
};

struct FrameAllocationPlan {
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
            if (is_callee_saved(reg)) {
                if (std::find(plan.saved_non_volatile_regs.begin(), 
                              plan.saved_non_volatile_regs.end(), reg) == plan.saved_non_volatile_regs.end()) {
                    plan.saved_non_volatile_regs.push_back(reg);
                }
            }
        }

        // Sort to maintain deterministic prologue push ordering
        std::sort(plan.saved_non_volatile_regs.begin(), plan.saved_non_volatile_regs.end());

        const uint32_t callee_saved_bytes = static_cast<uint32_t>(plan.saved_non_volatile_regs.size()) * 8;
        
        // Outgoing argument space: Windows x64 requires a 32-byte shadow space if any call is made
        plan.shadow_space_bytes = makes_external_calls ? std::max<uint32_t>(32, max_outgoing_args_bytes) : 0;
        
        const uint32_t raw_locals = local_storage_bytes + spill_slot_bytes;
        plan.local_and_spill_offset = plan.shadow_space_bytes;

        const uint32_t raw_body = raw_locals + plan.shadow_space_bytes;

        // Mathematical Stack Alignment:
        // Initial state at function entry: (RSP % 16 == 8) due to return address pushed by CALL.
        // Pushing RBP: (RSP % 16 == 0).
        // Pushing callee-saved registers: RSP changes by (callee_saved_bytes).
        // Total bytes pushed on stack before 'sub rsp, total_stack_bytes':
        //   PushedBytes = 8 (RetAddr) + 8 (RBP) + callee_saved_bytes
        const uint32_t total_pushed_before_sub = 8 + 8 + callee_saved_bytes;
        
        // We need: (total_pushed_before_sub + raw_body + pad) % 16 == 0
        const uint32_t remainder = (total_pushed_before_sub + raw_body) % 16;
        if (remainder != 0) {
            plan.alignment_pad_bytes = 16 - remainder;
        } else {
            plan.alignment_pad_bytes = 0;
        }

        plan.total_stack_bytes = raw_body + plan.alignment_pad_bytes;
        return plan;
    }

    [[nodiscard]] static bool verify_invariants(
        const FrameAllocationPlan& plan,
        bool makes_external_calls
    ) noexcept {
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
};

} // namespace dgtl::backend::isa

/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Component:   Win64 Frame Lowering Engine
 *
 * Purpose:
 *   Industrial-grade Win64 ABI stack-frame layout calculation.
 *
 * Guarantees:
 *   - Windows x64 ABI compliant 16-byte stack alignment at call sites.
 *   - Correct 32-byte Win64 home/shadow-space handling.
 *   - Explicit outgoing stack-argument accounting.
 *   - Deterministic callee-save ordering.
 *   - Overflow-safe frame-size arithmetic.
 *   - Explicit frame-pointer policy.
 *   - Optional Win64 nonvolatile XMM register support.
 *   - Stack-probe detection for large allocations.
 *   - Validation of generated plans.
 *   - No undefined behavior from malformed register lists.
 *
 * Notes:
 *   This component calculates frame layout. It does not itself emit
 *   machine instructions or Windows unwind (.pdata/.xdata) records.
 *
 * ABI:
 *   Microsoft x64 / Win64 ABI.
 *
 * Language:
 *   C++26
 */

// module;

// #include <algorithm>
// #include <cstddef>
// #include <cstdint>
// #include <limits>
// #include <utility>
// #include <vector>

// export module dgtl.backend.isa.win64_frame_lowering;

// export namespace dgtl::backend::isa {

// /* ============================================================================
//  * Win64 register model
//  * ========================================================================== */

// enum class X64Register : uint8_t {
//     RAX = 0,
//     RCX,
//     RDX,
//     RBX,
//     RSP,
//     RBP,
//     RSI,
//     RDI,
//     R8,
//     R9,
//     R10,
//     R11,
//     R12,
//     R13,
//     R14,
//     R15
// };

// enum class X64VectorRegister : uint8_t {
//     XMM0 = 0,
//     XMM1,
//     XMM2,
//     XMM3,
//     XMM4,
//     XMM5,
//     XMM6,
//     XMM7,
//     XMM8,
//     XMM9,
//     XMM10,
//     XMM11,
//     XMM12,
//     XMM13,
//     XMM14,
//     XMM15
// };

// /* ============================================================================
//  * ABI constants
//  * ========================================================================== */

// struct Win64Abi {
//     static constexpr uint32_t stack_alignment = 16;
//     static constexpr uint32_t shadow_space = 32;
//     static constexpr uint32_t register_width = 8;
//     static constexpr uint32_t vector_register_save_width = 16;

//     /*
//      * Windows x64 requires a stack probe for sufficiently large stack
//      * allocations.  The exact compiler/runtime strategy can vary, so this
//      * value is exposed as a backend policy rather than treated as an ABI
//      * constant.
//      */
//     static constexpr uint32_t default_stack_probe_threshold = 4096;

//     /*
//      * At normal function entry:
//      *
//      *     RSP % 16 == 8
//      *
//      * because CALL has pushed the return address.
//      *
//      * Immediately before another CALL:
//      *
//      *     RSP % 16 == 0
//      */
//     static constexpr uint32_t entry_stack_modulo = 8;
//     static constexpr uint32_t call_stack_modulo = 0;
// };

// /* ============================================================================
//  * Frame policy
//  * ========================================================================== */

// struct Win64FramePolicy {
//     /*
//      * Whether the generated prologue reserves a frame-pointer register.
//      *
//      * If true, RBP is assumed to be saved by the prologue and its 8-byte
//      * save slot is included in the frame calculation.
//      */
//     bool use_frame_pointer = false;

//     /*
//      * A function that performs calls must reserve the 32-byte Win64 home
//      * area for every call site.
//      */
//     bool reserve_shadow_space_for_calls = true;

//     /*
//      * Request stack-probe handling for large frames.
//      *
//      * The frame calculator does not emit __chkstk itself. It merely reports
//      * whether the final frame exceeds the configured threshold.
//      */
//     bool enable_stack_probing = true;

//     /*
//      * Backend-specific stack-probe threshold.
//      */
//     uint32_t stack_probe_threshold =
//         Win64Abi::default_stack_probe_threshold;

//     /*
//      * Win64 unwind information is required for non-leaf functions and for
//      * functions whose prologue modifies nonvolatile state.
//      */
//     bool require_unwind_metadata = true;
// };

// /* ============================================================================
//  * Frame layout
//  * ========================================================================== */

// struct FrameAllocationPlan {
//     /*
//      * Total bytes subtracted from RSP by the main frame allocation.
//      *
//      * This excludes:
//      *   - the caller's return address
//      *   - individual register PUSH operations
//      *
//      * It includes:
//      *   - shadow/home space
//      *   - outgoing stack arguments
//      *   - locals
//      *   - spills
//      *   - alignment padding
//      */
//     uint32_t total_stack_bytes = 0;

//     /*
//      * Offset, relative to the post-prologue RSP, where local/spill storage
//      * begins.
//      */
//     uint32_t local_and_spill_offset = 0;

//     /*
//      * Required Win64 home/shadow space.
//      */
//     uint32_t shadow_space_bytes = 0;

//     /*
//      * Space reserved for arguments that do not fit in the four integer/
//      * floating-point register argument positions.
//      *
//      * This is distinct from shadow space.
//      */
//     uint32_t outgoing_stack_arg_bytes = 0;

//     /*
//      * Local variables plus spill slots.
//      */
//     uint32_t local_and_spill_bytes = 0;

//     /*
//      * Bytes inserted solely to satisfy ABI stack alignment.
//      */
//     uint32_t alignment_pad_bytes = 0;

//     /*
//      * Number of bytes consumed by nonvolatile GPR saves.
//      */
//     uint32_t saved_gpr_bytes = 0;

//     /*
//      * Number of bytes consumed by nonvolatile XMM saves.
//      *
//      * XMM6-XMM15 are 16-byte nonvolatile registers on Win64.
//      */
//     uint32_t saved_xmm_bytes = 0;

//     /*
//      * Whether RBP is part of the saved-register set because the backend
//      * requested a frame pointer.
//      */
//     bool frame_pointer_saved = false;

//     /*
//      * Whether this function requires an actual CALL instruction or equivalent
//      * external call sequence.
//      */
//     bool makes_external_calls = false;

//     /*
//      * Whether the final frame is large enough to require stack probing under
//      * the configured backend policy.
//      */
//     bool requires_stack_probe = false;

//     /*
//      * Whether unwind metadata should be generated.
//      */
//     bool requires_unwind_metadata = false;

//     /*
//      * Deterministically ordered nonvolatile GPRs.
//      */
//     std::vector<X64Register> saved_non_volatile_regs;

//     /*
//      * Deterministically ordered nonvolatile XMM registers.
//      */
//     std::vector<X64VectorRegister> saved_non_volatile_xmm_regs;
// };

// /* ============================================================================
//  * Validation result
//  * ========================================================================== */

// enum class FrameValidationError : uint8_t {
//     none = 0,

//     invalid_stack_alignment,
//     shadow_space_missing,
//     outgoing_arguments_missing,
//     frame_size_inconsistent,
//     local_offset_inconsistent,
//     alignment_padding_inconsistent,

//     invalid_saved_gpr,
//     invalid_saved_xmm,

//     frame_pointer_not_saved,
//     unexpected_frame_pointer,

//     stack_probe_state_inconsistent,
//     unwind_metadata_state_inconsistent,

//     arithmetic_overflow
// };

// /* ============================================================================
//  * Frame calculator
//  * ========================================================================== */

// class Win64FrameCalculator {
// public:
//     static constexpr bool is_valid_gpr(X64Register reg) noexcept {
//         return static_cast<uint8_t>(reg) <=
//                static_cast<uint8_t>(X64Register::R15);
//     }

//     static constexpr bool is_valid_vector_register(
//         X64VectorRegister reg
//     ) noexcept {
//         return static_cast<uint8_t>(reg) <=
//                static_cast<uint8_t>(X64VectorRegister::XMM15);
//     }

//     /*
//      * Microsoft x64 nonvolatile GPRs:
//      *
//      *   RBX RBP RSI RDI R12 R13 R14 R15
//      *
//      * RSP is of course preserved, but it is the stack pointer and is not
//      * represented as a normal save/restore candidate.
//      */
//     static constexpr bool is_callee_saved(X64Register reg) noexcept {
//         switch (reg) {
//             case X64Register::RBX:
//             case X64Register::RBP:
//             case X64Register::RSI:
//             case X64Register::RDI:
//             case X64Register::R12:
//             case X64Register::R13:
//             case X64Register::R14:
//             case X64Register::R15:
//                 return true;

//             default:
//                 return false;
//         }
//     }

//     /*
//      * Win64 preserves XMM6-XMM15.
//      *
//      * XMM0-XMM5 are volatile.
//      */
//     static constexpr bool is_callee_saved(
//         X64VectorRegister reg
//     ) noexcept {
//         switch (reg) {
//             case X64VectorRegister::XMM6:
//             case X64VectorRegister::XMM7:
//             case X64VectorRegister::XMM8:
//             case X64VectorRegister::XMM9:
//             case X64VectorRegister::XMM10:
//             case X64VectorRegister::XMM11:
//             case X64VectorRegister::XMM12:
//             case X64VectorRegister::XMM13:
//             case X64VectorRegister::XMM14:
//             case X64VectorRegister::XMM15:
//                 return true;

//             default:
//                 return false;
//         }
//     }

//     static constexpr uint32_t gpr_save_size(
//         X64Register reg
//     ) noexcept {
//         return is_callee_saved(reg)
//             ? Win64Abi::register_width
//             : 0;
//     }

//     static constexpr uint32_t xmm_save_size(
//         X64VectorRegister reg
//     ) noexcept {
//         return is_callee_saved(reg)
//             ? Win64Abi::vector_register_save_width
//             : 0;
//     }

//     /*
//      * Align a value upward.
//      *
//      * Returns false if the operation would overflow uint32_t.
//      */
//     static constexpr bool align_up_checked(
//         uint32_t value,
//         uint32_t alignment,
//         uint32_t& result
//     ) noexcept {
//         if (alignment == 0) {
//             return false;
//         }

//         const uint32_t remainder = value % alignment;

//         if (remainder == 0) {
//             result = value;
//             return true;
//         }

//         const uint32_t adjustment = alignment - remainder;

//         if (value >
//             std::numeric_limits<uint32_t>::max() - adjustment) {
//             return false;
//         }

//         result = value + adjustment;
//         return true;
//     }

//     /*
//      * Checked addition.
//      */
//     static constexpr bool add_checked(
//         uint32_t a,
//         uint32_t b,
//         uint32_t& result
//     ) noexcept {
//         if (a >
//             std::numeric_limits<uint32_t>::max() - b) {
//             return false;
//         }

//         result = a + b;
//         return true;
//     }

//     /*
//      * Checked multiplication.
//      */
//     static constexpr bool multiply_checked(
//         uint32_t a,
//         uint32_t b,
//         uint32_t& result
//     ) noexcept {
//         if (a != 0 &&
//             b > std::numeric_limits<uint32_t>::max() / a) {
//             return false;
//         }

//         result = a * b;
//         return true;
//     }

//     /*
//      * Normalizes a GPR save list:
//      *
//      *   - removes duplicates
//      *   - removes volatile registers
//      *   - removes RSP
//      *   - optionally removes RBP when frame-pointer mode is disabled
//      *   - sorts deterministically
//      */
//     static std::vector<X64Register> normalize_saved_registers(
//         const std::vector<X64Register>& requested,
//         bool use_frame_pointer
//     ) {
//         std::vector<X64Register> result;
//         result.reserve(requested.size());

//         for (const X64Register reg : requested) {
//             if (!is_valid_gpr(reg)) {
//                 continue;
//             }

//             if (!is_callee_saved(reg)) {
//                 continue;
//             }

//             if (reg == X64Register::RBP &&
//                 !use_frame_pointer) {
//                 continue;
//             }

//             if (std::find(
//                     result.begin(),
//                     result.end(),
//                     reg
//                 ) == result.end()) {
//                 result.push_back(reg);
//             }
//         }

//         std::sort(result.begin(), result.end());

//         return result;
//     }

//     /*
//      * Normalizes XMM save lists:
//      *
//      *   - removes duplicates
//      *   - removes volatile XMM registers
//      *   - sorts deterministically
//      */
//     static std::vector<X64VectorRegister>
//     normalize_saved_vector_registers(
//         const std::vector<X64VectorRegister>& requested
//     ) {
//         std::vector<X64VectorRegister> result;
//         result.reserve(requested.size());

//         for (const X64VectorRegister reg : requested) {
//             if (!is_valid_vector_register(reg)) {
//                 continue;
//             }

//             if (!is_callee_saved(reg)) {
//                 continue;
//             }

//             if (std::find(
//                     result.begin(),
//                     result.end(),
//                     reg
//                 ) == result.end()) {
//                 result.push_back(reg);
//             }
//         }

//         std::sort(result.begin(), result.end());

//         return result;
//     }

//     /*
//      * Primary frame-layout calculator.
//      *
//      * IMPORTANT:
//      *
//      * The returned total_stack_bytes represents the amount reserved by the
//      * frame allocation itself, not bytes consumed by PUSH operations.
//      *
//      * At function entry:
//      *
//      *     RSP % 16 == 8
//      *
//      * Before a CALL:
//      *
//      *     RSP % 16 == 0
//      *
//      * Therefore:
//      *
//      *   (8 + saved_bytes + allocated_frame_bytes) % 16 == 0
//      *
//      * must hold.
//      *
//      * No `noexcept` is intentionally used here because construction of the
//      * register vectors may allocate memory.
//      */
//     [[nodiscard]]
//     static FrameAllocationPlan compute_plan(
//         uint32_t local_storage_bytes,
//         uint32_t spill_slot_bytes,
//         uint32_t max_outgoing_args_bytes,
//         const std::vector<X64Register>& clobbered_registers,
//         bool makes_external_calls,
//         Win64FramePolicy policy = {}
//     ) {
//         FrameAllocationPlan plan;

//         plan.makes_external_calls = makes_external_calls;

//         /*
//          * Normalize GPR saves.
//          */
//         plan.saved_non_volatile_regs =
//             normalize_saved_registers(
//                 clobbered_registers,
//                 policy.use_frame_pointer
//             );

//         /*
//          * Frame pointer policy.
//          */
//         plan.frame_pointer_saved =
//             std::find(
//                 plan.saved_non_volatile_regs.begin(),
//                 plan.saved_non_volatile_regs.end(),
//                 X64Register::RBP
//             ) != plan.saved_non_volatile_regs.end();

//         /*
//          * A requested frame pointer must actually be saved.
//          */
//         if (policy.use_frame_pointer &&
//             !plan.frame_pointer_saved) {
//             plan.saved_non_volatile_regs.push_back(
//                 X64Register::RBP
//             );

//             std::sort(
//                 plan.saved_non_volatile_regs.begin(),
//                 plan.saved_non_volatile_regs.end()
//             );

//             plan.frame_pointer_saved = true;
//         }

//         /*
//          * Calculate GPR save bytes.
//          */
//         uint32_t gpr_bytes = 0;

//         for (const X64Register reg :
//              plan.saved_non_volatile_regs) {

//             uint32_t bytes = gpr_save_size(reg);

//             if (!add_checked(
//                     gpr_bytes,
//                     bytes,
//                     gpr_bytes)) {
//                 /*
//                  * The input vector cannot realistically reach this condition
//                  * on a normal implementation, but keeping arithmetic checked
//                  * makes the calculator robust against future register-model
//                  * expansion.
//                  */
//                 return {};
//             }
//         }

//         plan.saved_gpr_bytes = gpr_bytes;

//         /*
//          * Shadow/home space.
//          *
//          * A function needs to reserve home space when it makes calls because
//          * the caller owns the 32-byte home area for each call.
//          *
//          * max_outgoing_args_bytes describes additional stack arguments and
//          * therefore does NOT replace the 32-byte home area.
//          */
//         if (makes_external_calls &&
//             policy.reserve_shadow_space_for_calls) {
//             plan.shadow_space_bytes = Win64Abi::shadow_space;
//         } else {
//             plan.shadow_space_bytes = 0;
//         }

//         /*
//          * Outgoing stack arguments are separate from shadow space.
//          *
//          * Align them to the ABI stack granularity so that the layout remains
//          * suitable for arbitrary backend call lowering.
//          */
//         uint32_t aligned_outgoing_args = 0;

//         if (!align_up_checked(
//                 max_outgoing_args_bytes,
//                 Win64Abi::stack_alignment,
//                 aligned_outgoing_args)) {
//             return {};
//         }

//         plan.outgoing_stack_arg_bytes =
//             aligned_outgoing_args;

//         /*
//          * Locals + spills.
//          */
//         if (!add_checked(
//                 local_storage_bytes,
//                 spill_slot_bytes,
//                 plan.local_and_spill_bytes)) {
//             return {};
//         }

//         /*
//          * Layout:
//          *
//          *   [shadow space]
//          *   [outgoing stack arguments]
//          *   [locals / spills]
//          *   [alignment padding]
//          *
//          * local_and_spill_offset points to the beginning of the local area.
//          */
//         uint32_t prefix = 0;

//         if (!add_checked(
//                 plan.shadow_space_bytes,
//                 plan.outgoing_stack_arg_bytes,
//                 prefix)) {
//             return {};
//         }

//         plan.local_and_spill_offset = prefix;

//         uint32_t raw_frame = 0;

//         if (!add_checked(
//                 prefix,
//                 plan.local_and_spill_bytes,
//                 raw_frame)) {
//             return {};
//         }

//         /*
//          * Saved register bytes alter the stack alignment because the saves
//          * occur before the main RSP subtraction in a conventional prologue.
//          *
//          * At entry:
//          *
//          *     RSP % 16 == 8
//          *
//          * After saves + frame allocation, before CALL:
//          *
//          *     (8 + saved_bytes + frame_bytes) % 16 == 0
//          */
//         uint32_t alignment_remainder =
//             (Win64Abi::entry_stack_modulo +
//              (plan.saved_gpr_bytes %
//               Win64Abi::stack_alignment) +
//              (raw_frame %
//               Win64Abi::stack_alignment))
//             % Win64Abi::stack_alignment;

//         if (alignment_remainder != 0) {
//             plan.alignment_pad_bytes =
//                 Win64Abi::stack_alignment -
//                 alignment_remainder;
//         } else {
//             plan.alignment_pad_bytes = 0;
//         }

//         /*
//          * Final frame allocation.
//          */
//         if (!add_checked(
//                 raw_frame,
//                 plan.alignment_pad_bytes,
//                 plan.total_stack_bytes)) {
//             return {};
//         }

//         /*
//          * Stack probing policy.
//          */
//         plan.requires_stack_probe =
//             policy.enable_stack_probing &&
//             plan.total_stack_bytes >=
//                 policy.stack_probe_threshold;

//         /*
//          * Unwind metadata is required whenever the function:
//          *
//          *   - calls another function,
//          *   - saves nonvolatile registers,
//          *   - allocates stack space,
//          *   - or explicitly uses a frame pointer.
//          */
//         plan.requires_unwind_metadata =
//             policy.require_unwind_metadata &&
//             (
//                 makes_external_calls ||
//                 plan.total_stack_bytes != 0 ||
//                 !plan.saved_non_volatile_regs.empty()
//             );

//         return plan;
//     }

//     /*
//      * Extended calculator supporting nonvolatile XMM registers.
//      *
//      * This is useful for floating-point/vector-heavy code generators.
//      */
//     [[nodiscard]]
//     static FrameAllocationPlan compute_plan(
//         uint32_t local_storage_bytes,
//         uint32_t spill_slot_bytes,
//         uint32_t max_outgoing_args_bytes,
//         const std::vector<X64Register>& clobbered_registers,
//         const std::vector<X64VectorRegister>& clobbered_xmm_registers,
//         bool makes_external_calls,
//         Win64FramePolicy policy = {}
//     ) {
//         FrameAllocationPlan plan =
//             compute_plan(
//                 local_storage_bytes,
//                 spill_slot_bytes,
//                 max_outgoing_args_bytes,
//                 clobbered_registers,
//                 makes_external_calls,
//                 policy
//             );

//         /*
//          * An empty plan can legitimately be a zero-sized frame, but the
//          * extended path cannot distinguish that from arithmetic failure.
//          * Recalculate XMM accounting only after validating the base plan.
//          */
//         plan.saved_non_volatile_xmm_regs =
//             normalize_saved_vector_registers(
//                 clobbered_xmm_registers
//             );

//         uint32_t xmm_bytes = 0;

//         for (const X64VectorRegister reg :
//              plan.saved_non_volatile_xmm_regs) {

//             uint32_t bytes = xmm_save_size(reg);

//             if (!add_checked(
//                     xmm_bytes,
//                     bytes,
//                     xmm_bytes)) {
//                 return {};
//             }
//         }

//         plan.saved_xmm_bytes = xmm_bytes;

//         /*
//          * Recalculate alignment because XMM saves consume 16-byte slots.
//          */
//         uint32_t raw_frame =
//             plan.shadow_space_bytes;

//         if (!add_checked(
//                 raw_frame,
//                 plan.outgoing_stack_arg_bytes,
//                 raw_frame)) {
//             return {};
//         }

//         if (!add_checked(
//                 raw_frame,
//                 plan.local_and_spill_bytes,
//                 raw_frame)) {
//             return {};
//         }

//         const uint32_t saved_bytes =
//             plan.saved_gpr_bytes +
//             plan.saved_xmm_bytes;

//         const uint32_t remainder =
//             (
//                 Win64Abi::entry_stack_modulo +
//                 (saved_bytes % Win64Abi::stack_alignment) +
//                 (raw_frame % Win64Abi::stack_alignment)
//             ) % Win64Abi::stack_alignment;

//         plan.alignment_pad_bytes =
//             remainder == 0
//                 ? 0
//                 : Win64Abi::stack_alignment - remainder;

//         if (!add_checked(
//                 raw_frame,
//                 plan.alignment_pad_bytes,
//                 plan.total_stack_bytes)) {
//             return {};
//         }

//         plan.requires_stack_probe =
//             policy.enable_stack_probing &&
//             plan.total_stack_bytes >=
//                 policy.stack_probe_threshold;

//         plan.requires_unwind_metadata =
//             policy.require_unwind_metadata &&
//             (
//                 makes_external_calls ||
//                 plan.total_stack_bytes != 0 ||
//                 !plan.saved_non_volatile_regs.empty() ||
//                 !plan.saved_non_volatile_xmm_regs.empty()
//             );

//         return plan;
//     }

//     /*
//      * Validates a generated plan against the Win64 ABI model.
//      *
//      * This function performs structural validation only. It cannot verify
//      * that a machine-code emitter actually emits the exact PUSH/SUB/MOV/XMM
//      * instructions represented by the plan.
//      */
//     [[nodiscard]]
//     static FrameValidationError validate(
//         const FrameAllocationPlan& plan,
//         bool makes_external_calls,
//         Win64FramePolicy policy = {}
//     ) noexcept {
//         /*
//          * 1. Validate saved GPRs.
//          */
//         for (const X64Register reg :
//              plan.saved_non_volatile_regs) {

//             if (!is_valid_gpr(reg) ||
//                 !is_callee_saved(reg)) {
//                 return FrameValidationError::invalid_saved_gpr;
//             }

//             if (reg == X64Register::RBP &&
//                 !policy.use_frame_pointer) {
//                 return FrameValidationError::unexpected_frame_pointer;
//             }
//         }

//         /*
//          * 2. Validate saved XMM registers.
//          */
//         for (const X64VectorRegister reg :
//              plan.saved_non_volatile_xmm_regs) {

//             if (!is_valid_vector_register(reg) ||
//                 !is_callee_saved(reg)) {
//                 return FrameValidationError::invalid_saved_xmm;
//             }
//         }

//         /*
//          * 3. Validate frame-pointer state.
//          */
//         const bool has_rbp =
//             std::find(
//                 plan.saved_non_volatile_regs.begin(),
//                 plan.saved_non_volatile_regs.end(),
//                 X64Register::RBP
//             ) != plan.saved_non_volatile_regs.end();

//         if (policy.use_frame_pointer && !has_rbp) {
//             return FrameValidationError::frame_pointer_not_saved;
//         }

//         if (!policy.use_frame_pointer && has_rbp) {
//             return FrameValidationError::unexpected_frame_pointer;
//         }

//         /*
//          * 4. Validate shadow space.
//          */
//         if (makes_external_calls &&
//             policy.reserve_shadow_space_for_calls &&
//             plan.shadow_space_bytes <
//                 Win64Abi::shadow_space) {
//             return FrameValidationError::shadow_space_missing;
//         }

//         if (!makes_external_calls &&
//             plan.shadow_space_bytes != 0) {
//             /*
//              * Reserving shadow space for a leaf function is not technically
//              * forbidden, but it is unnecessary. We intentionally don't reject
//              * it because valid backend policies may over-allocate.
//              */
//         }

//         /*
//          * 5. Validate outgoing argument alignment.
//          */
//         if ((plan.outgoing_stack_arg_bytes %
//              Win64Abi::stack_alignment) != 0) {
//             return FrameValidationError::outgoing_arguments_missing;
//         }

//         /*
//          * 6. Validate local offset.
//          */
//         uint32_t expected_local_offset = 0;

//         if (!add_checked(
//                 plan.shadow_space_bytes,
//                 plan.outgoing_stack_arg_bytes,
//                 expected_local_offset)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         if (plan.local_and_spill_offset !=
//             expected_local_offset) {
//             return FrameValidationError::local_offset_inconsistent;
//         }

//         /*
//          * 7. Validate total frame size.
//          */
//         uint32_t expected_frame = 0;

//         if (!add_checked(
//                 expected_local_offset,
//                 plan.local_and_spill_bytes,
//                 expected_frame)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         if (!add_checked(
//                 expected_frame,
//                 plan.alignment_pad_bytes,
//                 expected_frame)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         if (plan.total_stack_bytes != expected_frame) {
//             return FrameValidationError::frame_size_inconsistent;
//         }

//         /*
//          * 8. Validate alignment at call sites.
//          *
//          * Function entry:
//          *
//          *     RSP % 16 == 8
//          *
//          * After saves and frame allocation:
//          *
//          *     RSP % 16 == 0
//          */
//         uint32_t saved_bytes = 0;

//         if (!multiply_checked(
//                 static_cast<uint32_t>(
//                     plan.saved_non_volatile_regs.size()
//                 ),
//                 Win64Abi::register_width,
//                 saved_bytes)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         uint32_t xmm_bytes = 0;

//         if (!multiply_checked(
//                 static_cast<uint32_t>(
//                     plan.saved_non_volatile_xmm_regs.size()
//                 ),
//                 Win64Abi::vector_register_save_width,
//                 xmm_bytes)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         if (!add_checked(
//                 saved_bytes,
//                 xmm_bytes,
//                 saved_bytes)) {
//             return FrameValidationError::arithmetic_overflow;
//         }

//         const uint32_t call_stack_depth =
//             Win64Abi::entry_stack_modulo +
//             (saved_bytes % Win64Abi::stack_alignment) +
//             (plan.total_stack_bytes %
//              Win64Abi::stack_alignment);

//         if ((call_stack_depth %
//              Win64Abi::stack_alignment) !=
//             Win64Abi::call_stack_modulo) {
//             return FrameValidationError::invalid_stack_alignment;
//         }

//         /*
//          * 9. Validate stack-probe state.
//          */
//         const bool expected_probe =
//             policy.enable_stack_probing &&
//             plan.total_stack_bytes >=
//                 policy.stack_probe_threshold;

//         if (plan.requires_stack_probe != expected_probe) {
//             return FrameValidationError::stack_probe_state_inconsistent;
//         }

//         /*
//          * 10. Validate unwind state.
//          */
//         const bool expected_unwind =
//             policy.require_unwind_metadata &&
//             (
//                 makes_external_calls ||
//                 plan.total_stack_bytes != 0 ||
//                 !plan.saved_non_volatile_regs.empty() ||
//                 !plan.saved_non_volatile_xmm_regs.empty()
//             );

//         if (plan.requires_unwind_metadata !=
//             expected_unwind) {
//             return FrameValidationError::
//                 unwind_metadata_state_inconsistent;
//         }

//         return FrameValidationError::none;
//     }

//     [[nodiscard]]
//     static bool verify_invariants(
//         const FrameAllocationPlan& plan,
//         bool makes_external_calls,
//         Win64FramePolicy policy = {}
//     ) noexcept {
//         return validate(
//             plan,
//             makes_external_calls,
//             policy
//         ) == FrameValidationError::none;
//     }

//     /*
//      * Convenience helper for determining whether a function is a true leaf.
//      */
//     [[nodiscard]]
//     static constexpr bool is_leaf_function(
//         bool makes_external_calls
//     ) noexcept {
//         return !makes_external_calls;
//     }

//     /*
//      * Returns the final RSP alignment at a call site.
//      *
//      * 0 means correctly aligned for CALL.
//      */
//     [[nodiscard]]
//     static constexpr uint32_t call_site_rsp_modulo(
//         uint32_t total_stack_bytes,
//         uint32_t saved_gpr_bytes = 0,
//         uint32_t saved_xmm_bytes = 0
//     ) noexcept {
//         const uint32_t saved =
//             (saved_gpr_bytes +
//              saved_xmm_bytes) %
//             Win64Abi::stack_alignment;

//         return (
//             Win64Abi::entry_stack_modulo +
//             saved +
//             (total_stack_bytes %
//              Win64Abi::stack_alignment)
//         ) % Win64Abi::stack_alignment;
//     }
// };

// /* ============================================================================
//  * ABI layout documentation
//  * ========================================================================== */

// /*
//  * Canonical non-leaf Win64 layout represented by this calculator:
//  *
//  *
//  *                 HIGH ADDRESSES
//  *
//  *     +-------------------------------+
//  *     | caller-owned stack arguments  |
//  *     +-------------------------------+
//  *     | 32-byte home/shadow space     |
//  *     +-------------------------------+
//  *     | local variables / spills      |
//  *     +-------------------------------+
//  *     | alignment padding             |
//  *     +-------------------------------+
//  *     | saved XMM6-XMM15 (if needed)  |
//  *     +-------------------------------+
//  *     | saved RBX/RBP/... (if needed) |
//  *     +-------------------------------+
//  *     | return address                |
//  *     +-------------------------------+
//  *
//  *                 LOW ADDRESSES
//  *
//  *
//  * The exact physical ordering of save slots is an emitter concern. The
//  * calculator deliberately describes byte requirements and alignment rather
//  * than prescribing one particular prologue instruction sequence.
//  *
//  *
//  * Example:
//  *
//  *     local storage          = 24
//  *     spills                 = 16
//  *     outgoing stack args    = 40
//  *     external calls         = true
//  *     saved registers        = RBX, R12
//  *
//  *     shadow space            = 32
//  *     outgoing args           = 48   (aligned from 40)
//  *     locals/spills           = 40
//  *     raw frame               = 120
//  *     saved GPRs              = 16
//  *
//  *     Entry RSP modulo 16     = 8
//  *
//  *     8 + 16 + 120 = 144
//  *     144 % 16 = 0
//  *
//  *     Therefore:
//  *
//  *         alignment padding = 0
//  *         total frame       = 120 bytes
//  *
//  *
//  * The key distinction is that the Win64 32-byte home area is NOT the same
//  * thing as outgoing stack arguments.
//  */

// /* ============================================================================
//  * Backward-compatible aliases
//  * ========================================================================== */

// using Win64FrameLayout = FrameAllocationPlan;
// using Win64FrameLowering = Win64FrameCalculator;

// } // namespace dgtl::backend::isa
