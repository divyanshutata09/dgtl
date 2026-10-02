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

module;

#include <cstdlib>

#include <array>
#include <cstdint>
#include <string>
#include <vector>
export module dgtl.isa.riscv64.riscv64_emitter;

export namespace dgtl::isa::riscv64::emitter {

class RiscV64GatherScatter {
public:
    struct Config {
        bool strict_validation = true;
        bool emit_comments = true;
        uint8_t loop_bound_temp_reg = 4; // t4 by default

        constexpr Config() noexcept = default;
    };

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size,
                            bool use_v_ext = false) noexcept {
        emit_gather(output, base_reg, indices_reg, data_reg, elem_size, use_v_ext, Config{});
    }

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size,
                            bool use_v_ext,
                            const Config& config) noexcept {
        if (config.strict_validation && !is_supported_elem_size(elem_size)) {
            if (config.emit_comments) {
                output.push_back("# DGTL RISC-V64: gather skipped (unsupported element size)");
            }
            return;
        }

        const std::array<const char*, 8> rv64_regs = {
            "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
        };

        const std::array<const char*, 32> rv64_v_regs = {
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
            "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
            "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
        };

        if (base_reg >= rv64_regs.size()) {
            return;
        }

        if (use_v_ext && indices_reg < rv64_v_regs.size() && data_reg < rv64_v_regs.size()) {
            if (config.emit_comments) {
                output.push_back("# RISC-V 64 gather with V-extension");
            }
            output.push_back("vsetvli t0, zero, e" + std::to_string(elem_size * 8) +
                             ", m1, ta, ma");

            const char* gather_op = (elem_size == 8) ? "vluxei64.v" : "vluxei32.v";
            output.push_back(std::string(gather_op) + " " + std::string(rv64_v_regs[data_reg]) +
                             ", (" + std::string(rv64_regs[base_reg]) + "), " +
                             std::string(rv64_v_regs[indices_reg]));
        } else {
            if (indices_reg >= rv64_regs.size() || data_reg >= rv64_regs.size()) {
                if (config.emit_comments) {
                    output.push_back("# DGTL RISC-V64: gather skipped (scalar regs out of range)");
                }
                return;
            }

            const std::string loop_label = ".L_dgtl_rv64_gather_" + std::to_string(output.size());
            const char* base = rv64_regs[base_reg];
            const char* idx_ptr = rv64_regs[indices_reg];
            const char* dst_acc = rv64_regs[data_reg];
            const char* bound_reg = t_reg_name(config.loop_bound_temp_reg);

            if (config.emit_comments) {
                output.push_back("# RISC-V 64 gather (scalar fallback)");
            }
            output.push_back("li t0, 0                # loop counter");
            output.push_back("li " + std::string(dst_acc) + ", 0         # clear output accumulator");

            output.push_back(loop_label + ":");
            output.push_back("lw t1, 0(" + std::string(idx_ptr) + ")      # load index[i]");
            if (const uint32_t shift = scale_shift_for_elem_size(elem_size); shift != 0) {
                output.push_back("slli t1, t1, " + std::to_string(shift));
            }
            output.push_back("add t2, " + std::string(base) + ", t1");

            if (elem_size == 8) {
                output.push_back("ld t3, 0(t2)            # load 64-bit element");
            } else if (elem_size == 4) {
                output.push_back("lw t3, 0(t2)            # load 32-bit element");
            } else if (elem_size == 2) {
                output.push_back("lh t3, 0(t2)            # load 16-bit element");
            } else {
                output.push_back("lb t3, 0(t2)            # load 8-bit element");
            }

            output.push_back("add " + std::string(dst_acc) +
                             ", " + std::string(dst_acc) + ", t3");
            output.push_back("addi t0, t0, 1");
            output.push_back("addi " + std::string(idx_ptr) + ", " + std::string(idx_ptr) + ", 4");
            output.push_back("blt t0, " + std::string(bound_reg) + ", " + loop_label);
        }
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_reg,
                             uint32_t indices_reg,
                             uint32_t data_reg,
                             uint8_t elem_size,
                             bool use_v_ext = false) noexcept {
        emit_scatter(output, base_reg, indices_reg, data_reg, elem_size, use_v_ext, Config{});
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_reg,
                             uint32_t indices_reg,
                             uint32_t data_reg,
                             uint8_t elem_size,
                             bool use_v_ext,
                             const Config& config) noexcept {
        if (config.strict_validation && !is_supported_elem_size(elem_size)) {
            if (config.emit_comments) {
                output.push_back("# DGTL RISC-V64: scatter skipped (unsupported element size)");
            }
            return;
        }

        const std::array<const char*, 8> rv64_regs = {
            "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
        };

        const std::array<const char*, 32> rv64_v_regs = {
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
            "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
            "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
        };

        if (base_reg >= rv64_regs.size()) {
            return;
        }

        if (use_v_ext && indices_reg < rv64_v_regs.size() && data_reg < rv64_v_regs.size()) {
            if (config.emit_comments) {
                output.push_back("# RISC-V 64 scatter with V-extension");
            }
            output.push_back("vsetvli t0, zero, e" + std::to_string(elem_size * 8) +
                             ", m1, ta, ma");

            const char* scatter_op = (elem_size == 8) ? "vsuxei64.v" : "vsuxei32.v";
            output.push_back(std::string(scatter_op) + " " + std::string(rv64_v_regs[data_reg]) +
                             ", (" + std::string(rv64_regs[base_reg]) + "), " +
                             std::string(rv64_v_regs[indices_reg]));
        } else {
            if (indices_reg >= rv64_regs.size() || data_reg >= rv64_regs.size()) {
                if (config.emit_comments) {
                    output.push_back("# DGTL RISC-V64: scatter skipped (scalar regs out of range)");
                }
                return;
            }

            const std::string loop_label = ".L_dgtl_rv64_scatter_" + std::to_string(output.size());
            const char* base = rv64_regs[base_reg];
            const char* idx_ptr = rv64_regs[indices_reg];
            const char* src_ptr = rv64_regs[data_reg];
            const char* bound_reg = t_reg_name(config.loop_bound_temp_reg);

            if (config.emit_comments) {
                output.push_back("# RISC-V 64 scatter (scalar fallback)");
            }
            output.push_back("li t0, 0                # loop counter");

            output.push_back(loop_label + ":");
            output.push_back("lw t1, 0(" + std::string(idx_ptr) + ")      # load index[i]");
            if (const uint32_t shift = scale_shift_for_elem_size(elem_size); shift != 0) {
                output.push_back("slli t1, t1, " + std::to_string(shift));
            }
            output.push_back("add t2, " + std::string(base) + ", t1");

            if (elem_size == 8) {
                output.push_back("ld t3, 0(" + std::string(src_ptr) + ")");
            } else if (elem_size == 4) {
                output.push_back("lw t3, 0(" + std::string(src_ptr) + ")");
            } else if (elem_size == 2) {
                output.push_back("lh t3, 0(" + std::string(src_ptr) + ")");
            } else {
                output.push_back("lb t3, 0(" + std::string(src_ptr) + ")");
            }

            if (elem_size == 8) {
                output.push_back("sd t3, 0(t2)            # store 64-bit element");
            } else if (elem_size == 4) {
                output.push_back("sw t3, 0(t2)            # store 32-bit element");
            } else if (elem_size == 2) {
                output.push_back("sh t3, 0(t2)            # store 16-bit element");
            } else {
                output.push_back("sb t3, 0(t2)            # store 8-bit element");
            }

            output.push_back("addi t0, t0, 1");
            output.push_back("addi " + std::string(idx_ptr) + ", " + std::string(idx_ptr) + ", 4");
            output.push_back("addi " + std::string(src_ptr) + ", " + std::string(src_ptr) + ", " + std::to_string(elem_size));
            output.push_back("blt t0, " + std::string(bound_reg) + ", " + loop_label);
        }
    }

private:
    [[nodiscard]] static constexpr bool is_supported_elem_size(uint8_t elem_size) noexcept {
        return elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8;
    }

    [[nodiscard]] static constexpr uint32_t scale_shift_for_elem_size(uint8_t elem_size) noexcept {
        return (elem_size == 8) ? 3u : (elem_size == 4) ? 2u : (elem_size == 2) ? 1u : 0u;
    }

    [[nodiscard]] static constexpr const char* t_reg_name(uint8_t reg_index) noexcept {
        switch (reg_index) {
            case 0: return "t0";
            case 1: return "t1";
            case 2: return "t2";
            case 3: return "t3";
            case 4: return "t4";
            case 5: return "t5";
            case 6: return "t6";
            default: return "t4";
        }
    }
};

} // namespace dgtl::isa::riscv64::riscv64::emitter

export namespace dgtl::isa::riscv64 {
using RiscV64GatherScatter = dgtl::isa::riscv64::emitter::RiscV64GatherScatter;
}
export namespace dgtl::isa {
using RiscV64GatherScatter = dgtl::isa::riscv64::emitter::RiscV64GatherScatter;
}


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

// module;

// #include <array>
// #include <cstdint>
// #include <string>
// #include <string_view>
// #include <vector>

// export module dgtl.ir_core.riscv64_emitter;

// export namespace dgtl::isa::riscv64::emitter {

// /**
//  * RISC-V RV64/RVV gather/scatter assembly emitter.
//  *
//  * Semantics
//  * ---------
//  *
//  * `base_reg`
//  *     Base address of the indexed source/destination array.
//  *
//  * `indices_reg`
//  *     Pointer to an array of byte offsets.
//  *
//  * `data_reg`
//  *     Gather: destination pointer.
//  *     Scatter: source pointer.
//  *
//  * `length_reg`
//  *     Number of elements remaining.
//  *
//  * IMPORTANT:
//  *     indices are BYTE OFFSETS, not element indices.
//  *
//  * Example:
//  *
//  *     base = &array[0]
//  *     indices = [0, 16, 40]
//  *
//  *     gather -> array[0], array[16], array[40]
//  *
//  * This matches RVV indexed load/store semantics:
//  *
//  *     address[i] = base + index[i]
//  *
//  * RVV path:
//  *
//  *     vsetvli
//  *     vle<index_eew>.v
//  *     vluxei<index_eew>.v / vloxei<index_eew>.v
//  *     vse<data_eew>.v
//  *
//  * Scalar path:
//  *
//  *     load index
//  *     add base + index
//  *     load/store element
//  *
//  * Generated operations consume/update:
//  *
//  *     indices_reg
//  *     data_reg
//  *     length_reg
//  *
//  * `base_reg` remains unchanged.
//  *
//  * Scratch GPRs are clobbered.
//  */
// class RiscV64GatherScatter {
// public:
//     enum class MemoryOrder : uint8_t {
//         Unordered,
//         Ordered
//     };

//     enum class EmitResult : uint8_t {
//         Emitted,
//         Rejected
//     };

//     struct Config {
//         /*
//          * Emit explanatory assembly comments.
//          */
//         bool emit_comments = true;

//         /*
//          * Prefer RVV when requested.
//          */
//         bool use_rvv = true;

//         /*
//          * If RVV cannot be emitted for the requested widths,
//          * generate scalar code instead.
//          */
//         bool allow_scalar_fallback = true;

//         /*
//          * Maximum supported RVV SEW/EEW for the target.
//          *
//          * 32 -> Zve32* style target
//          * 64 -> RV64 + full V / Zve64* capable target
//          */
//         uint8_t max_rvv_eew = 64;

//         /*
//          * Scalar/RVV scratch GPRs.
//          *
//          * Defaults:
//          *   x5 = t0
//          *   x6 = t1
//          *   x7 = t2
//          *
//          * These must be caller-saved temporary registers.
//          */
//         uint8_t scratch0 = 5;
//         uint8_t scratch1 = 6;
//         uint8_t scratch2 = 7;

//         /*
//          * RVV temporary vector registers.
//          *
//          * v8 is the index vector.
//          * v16 is the data vector.
//          *
//          * They are intentionally separated because an index operand
//          * can have EMUL up to 8.
//          */
//         uint8_t index_vreg = 8;
//         uint8_t data_vreg = 16;

//         /*
//          * Default to unordered indexed operations because they
//          * generally provide the best implementation freedom.
//          *
//          * Use Ordered when element access ordering matters.
//          */
//         MemoryOrder memory_order = MemoryOrder::Unordered;
//     };

//     struct Operands {
//         uint8_t base_reg = 0;
//         uint8_t indices_reg = 0;
//         uint8_t data_reg = 0;
//         uint8_t length_reg = 0;

//         /*
//          * Size of each actual data element:
//          *
//          * 1, 2, 4, 8 bytes
//          */
//         uint8_t data_elem_size = 4;

//         /*
//          * Size of each index/offset:
//          *
//          * 1, 2, 4, 8 bytes
//          */
//         uint8_t index_elem_size = 4;
//     };

//     // ---------------------------------------------------------------------
//     // Public API
//     // ---------------------------------------------------------------------

//     static EmitResult emit_gather(
//         std::vector<std::string>& output,
//         const Operands& operands,
//         const Config& config = Config{}) {

//         if (!validate_common(output, operands, config, "gather")) {
//             return EmitResult::Rejected;
//         }

//         if (config.use_rvv &&
//             rvv_supported(operands, config)) {

//             emit_gather_rvv(output, operands, config);
//             return EmitResult::Emitted;
//         }

//         if (!config.allow_scalar_fallback) {
//             reject(
//                 output,
//                 config,
//                 "gather rejected: RVV unavailable and scalar fallback disabled");

//             return EmitResult::Rejected;
//         }

//         emit_gather_scalar(output, operands, config);
//         return EmitResult::Emitted;
//     }

//     static EmitResult emit_scatter(
//         std::vector<std::string>& output,
//         const Operands& operands,
//         const Config& config = Config{}) {

//         if (!validate_common(output, operands, config, "scatter")) {
//             return EmitResult::Rejected;
//         }

//         if (config.use_rvv &&
//             rvv_supported(operands, config)) {

//             emit_scatter_rvv(output, operands, config);
//             return EmitResult::Emitted;
//         }

//         if (!config.allow_scalar_fallback) {
//             reject(
//                 output,
//                 config,
//                 "scatter rejected: RVV unavailable and scalar fallback disabled");

//             return EmitResult::Rejected;
//         }

//         emit_scatter_scalar(output, operands, config);
//         return EmitResult::Emitted;
//     }

// private:
//     // ---------------------------------------------------------------------
//     // Register names
//     // ---------------------------------------------------------------------

//     static constexpr std::array<std::string_view, 32> GPR_NAMES = {
//         "zero", "ra", "sp", "gp",
//         "tp",
//         "t0", "t1", "t2",
//         "s0", "s1",
//         "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
//         "s2", "s3", "s4", "s5", "s6", "s7",
//         "s8", "s9", "s10", "s11",
//         "t3", "t4", "t5", "t6"
//     };

//     static constexpr std::array<std::string_view, 32> VREG_NAMES = {
//         "v0",  "v1",  "v2",  "v3",
//         "v4",  "v5",  "v6",  "v7",
//         "v8",  "v9",  "v10", "v11",
//         "v12", "v13", "v14", "v15",
//         "v16", "v17", "v18", "v19",
//         "v20", "v21", "v22", "v23",
//         "v24", "v25", "v26", "v27",
//         "v28", "v29", "v30", "v31"
//     };

//     [[nodiscard]]
//     static constexpr std::string_view gpr_name(uint8_t reg) noexcept {
//         return GPR_NAMES[reg];
//     }

//     [[nodiscard]]
//     static constexpr std::string_view vreg_name(uint8_t reg) noexcept {
//         return VREG_NAMES[reg];
//     }

//     // ---------------------------------------------------------------------
//     // Validation
//     // ---------------------------------------------------------------------

//     [[nodiscard]]
//     static constexpr bool valid_element_size(uint8_t size) noexcept {
//         return size == 1 ||
//                size == 2 ||
//                size == 4 ||
//                size == 8;
//     }

//     [[nodiscard]]
//     static constexpr bool valid_gpr(uint8_t reg) noexcept {
//         return reg < 32 && reg != 0;
//     }

//     [[nodiscard]]
//     static constexpr bool valid_vreg(uint8_t reg) noexcept {
//         return reg < 32;
//     }

//     /*
//      * Standard ABI caller-saved temporary GPRs:
//      *
//      * t0-t2 = x5-x7
//      * a0-a7 = x10-x17
//      * t3-t6 = x28-x31
//      *
//      * We only accept the t-register subset for scratch registers
//      * to avoid surprising the register allocator.
//      */
//     [[nodiscard]]
//     static constexpr bool valid_scratch_gpr(uint8_t reg) noexcept {
//         return reg == 5 ||
//                reg == 6 ||
//                reg == 7 ||
//                reg >= 28;
//     }

//     [[nodiscard]]
//     static constexpr bool distinct(
//         uint8_t a,
//         uint8_t b) noexcept {

//         return a != b;
//     }

//     [[nodiscard]]
//     static bool validate_common(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config,
//         std::string_view operation) {

//         if (!valid_element_size(op.data_elem_size)) {
//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: invalid data element size");

//             return false;
//         }

//         if (!valid_element_size(op.index_elem_size)) {
//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: invalid index element size");

//             return false;
//         }

//         if (!valid_gpr(op.base_reg) ||
//             !valid_gpr(op.indices_reg) ||
//             !valid_gpr(op.data_reg) ||
//             !valid_gpr(op.length_reg)) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: invalid GPR operand");

//             return false;
//         }

//         /*
//          * These registers are consumed/modified independently.
//          * Requiring distinct registers makes the generated code
//          * deterministic and avoids accidental self-clobbering.
//          */
//         if (!distinct(op.base_reg, op.indices_reg) ||
//             !distinct(op.base_reg, op.data_reg) ||
//             !distinct(op.base_reg, op.length_reg) ||
//             !distinct(op.indices_reg, op.data_reg) ||
//             !distinct(op.indices_reg, op.length_reg) ||
//             !distinct(op.data_reg, op.length_reg)) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: memory/length registers must be distinct");

//             return false;
//         }

//         if (!valid_scratch_gpr(config.scratch0) ||
//             !valid_scratch_gpr(config.scratch1) ||
//             !valid_scratch_gpr(config.scratch2)) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: invalid scratch register");

//             return false;
//         }

//         if (config.scratch0 == config.scratch1 ||
//             config.scratch0 == config.scratch2 ||
//             config.scratch1 == config.scratch2) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: scratch registers overlap");

//             return false;
//         }

//         if (is_input_register(op, config.scratch0) ||
//             is_input_register(op, config.scratch1) ||
//             is_input_register(op, config.scratch2)) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: scratch register aliases operand");

//             return false;
//         }

//         if (!valid_vreg(config.index_vreg) ||
//             !valid_vreg(config.data_vreg)) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: invalid vector register");

//             return false;
//         }

//         if (config.index_vreg == config.data_vreg) {
//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: vector registers overlap");

//             return false;
//         }

//         if (config.max_rvv_eew != 32 &&
//             config.max_rvv_eew != 64) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: max_rvv_eew must be 32 or 64");

//             return false;
//         }

//         if (!validate_vector_group_layout(
//                 output,
//                 op,
//                 config,
//                 operation)) {

//             return false;
//         }

//         return true;
//     }

//     [[nodiscard]]
//     static constexpr bool is_input_register(
//         const Operands& op,
//         uint8_t reg) noexcept {

//         return reg == op.base_reg ||
//                reg == op.indices_reg ||
//                reg == op.data_reg ||
//                reg == op.length_reg;
//     }

//     /*
//      * Indexed vector instructions use:
//      *
//      *     data EMUL = LMUL
//      *     index EMUL = index_EEW / data_SEW * LMUL
//      *
//      * with LMUL=1 here.
//      *
//      * Therefore:
//      *
//      * data=64, index=32 -> mf2
//      * data=64, index=16 -> mf4
//      * data=64, index=8  -> mf8
//      * data=8,  index=64 -> m8
//      */
//     [[nodiscard]]
//     static constexpr uint8_t index_group_register_count(
//         uint8_t data_size,
//         uint8_t index_size) noexcept {

//         if (index_size >= data_size) {
//             return static_cast<uint8_t>(
//                 index_size / data_size);
//         }

//         /*
//          * Fractional LMUL still occupies a single architectural
//          * vector register as its lowest-numbered register.
//          */
//         return 1;
//     }

//     [[nodiscard]]
//     static bool validate_vector_group_layout(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config,
//         std::string_view operation) {

//         const uint8_t index_regs =
//             index_group_register_count(
//                 op.data_elem_size,
//                 op.index_elem_size);

//         /*
//          * Integer LMUL groups must start on an aligned register.
//          */
//         if (index_regs > 1 &&
//             (config.index_vreg % index_regs) != 0) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: index vector register is not aligned "
//                 "for its EMUL");

//             return false;
//         }

//         if (static_cast<uint16_t>(config.index_vreg) +
//             index_regs > 32) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: index vector register group exceeds v31");

//             return false;
//         }

//         /*
//          * Data vector is LMUL=1, so it occupies one register.
//          */
//         if (config.index_vreg <= config.data_vreg &&
//             static_cast<uint16_t>(config.data_vreg) <
//                 static_cast<uint16_t>(config.index_vreg) + index_regs) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: index/data vector groups overlap");

//             return false;
//         }

//         if (config.data_vreg <= config.index_vreg &&
//             config.index_vreg <
//                 static_cast<uint16_t>(config.data_vreg) + 1) {

//             reject(
//                 output,
//                 config,
//                 std::string(operation) +
//                 " rejected: index/data vector groups overlap");

//             return false;
//         }

//         return true;
//     }

//     [[nodiscard]]
//     static bool rvv_supported(
//         const Operands& op,
//         const Config& config) noexcept {

//         const uint8_t max_size =
//             op.data_elem_size > op.index_elem_size
//                 ? op.data_elem_size
//                 : op.index_elem_size;

//         return max_size <= (config.max_rvv_eew / 8);
//     }

//     // ---------------------------------------------------------------------
//     // Scalar instruction selection
//     // ---------------------------------------------------------------------

//     [[nodiscard]]
//     static constexpr std::string_view scalar_load(
//         uint8_t size) noexcept {

//         switch (size) {
//             case 1: return "lbu";
//             case 2: return "lhu";
//             case 4: return "lwu";
//             case 8: return "ld";
//             default: return "";
//         }
//     }

//     [[nodiscard]]
//     static constexpr std::string_view scalar_store(
//         uint8_t size) noexcept {

//         switch (size) {
//             case 1: return "sb";
//             case 2: return "sh";
//             case 4: return "sw";
//             case 8: return "sd";
//             default: return "";
//         }
//     }

//     // ---------------------------------------------------------------------
//     // RVV instruction selection
//     // ---------------------------------------------------------------------

//     [[nodiscard]]
//     static constexpr std::string_view rvv_load(
//         uint8_t size) noexcept {

//         switch (size) {
//             case 1: return "vle8.v";
//             case 2: return "vle16.v";
//             case 4: return "vle32.v";
//             case 8: return "vle64.v";
//             default: return "";
//         }
//     }

//     [[nodiscard]]
//     static constexpr std::string_view rvv_store(
//         uint8_t size) noexcept {

//         switch (size) {
//             case 1: return "vse8.v";
//             case 2: return "vse16.v";
//             case 4: return "vse32.v";
//             case 8: return "vse64.v";
//             default: return "";
//         }
//     }

//     [[nodiscard]]
//     static constexpr std::string_view rvv_gather(
//         uint8_t index_size,
//         MemoryOrder order) noexcept {

//         if (order == MemoryOrder::Ordered) {
//             switch (index_size) {
//                 case 1: return "vloxei8.v";
//                 case 2: return "vloxei16.v";
//                 case 4: return "vloxei32.v";
//                 case 8: return "vloxei64.v";
//                 default: return "";
//             }
//         }

//         switch (index_size) {
//             case 1: return "vluxei8.v";
//             case 2: return "vluxei16.v";
//             case 4: return "vluxei32.v";
//             case 8: return "vluxei64.v";
//             default: return "";
//         }
//     }

//     [[nodiscard]]
//     static constexpr std::string_view rvv_scatter(
//         uint8_t index_size,
//         MemoryOrder order) noexcept {

//         if (order == MemoryOrder::Ordered) {
//             switch (index_size) {
//                 case 1: return "vsoxei8.v";
//                 case 2: return "vsoxei16.v";
//                 case 4: return "vsoxei32.v";
//                 case 8: return "vsoxei64.v";
//                 default: return "";
//             }
//         }

//         switch (index_size) {
//             case 1: return "vsuxei8.v";
//             case 2: return "vsuxei16.v";
//             case 4: return "vsuxei32.v";
//             case 8: return "vsuxei64.v";
//             default: return "";
//         }
//     }

//     [[nodiscard]]
//     static constexpr uint32_t log2_size(
//         uint8_t size) noexcept {

//         switch (size) {
//             case 1: return 0;
//             case 2: return 1;
//             case 4: return 2;
//             case 8: return 3;
//             default: return 0;
//         }
//     }

//     // ---------------------------------------------------------------------
//     // RVV gather
//     // ---------------------------------------------------------------------

//     static void emit_gather_rvv(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config) {

//         const auto base =
//             gpr_name(op.base_reg);

//         const auto indices =
//             gpr_name(op.indices_reg);

//         const auto destination =
//             gpr_name(op.data_reg);

//         const auto length =
//             gpr_name(op.length_reg);

//         const auto vl_tmp =
//             gpr_name(config.scratch0);

//         const auto address_tmp =
//             gpr_name(config.scratch1);

//         const auto index_v =
//             vreg_name(config.index_vreg);

//         const auto data_v =
//             vreg_name(config.data_vreg);

//         if (config.emit_comments) {
//             output.emplace_back(
//                 "# DGTL RISC-V64/RVV gather");

//             output.emplace_back(
//                 "# indices are byte offsets");

//             output.emplace_back(
//                 "# clobbers: " +
//                 std::string(vl_tmp) + ", " +
//                 std::string(address_tmp) + ", " +
//                 std::string(vreg_name(config.index_vreg)) + ", " +
//                 std::string(vreg_name(config.data_vreg)));
//         }

//         /*
//          * Zero-length fast path.
//          */
//         output.emplace_back(
//             "beqz " + std::string(length) + ", 2f");

//         /*
//          * Strip-mined VLA loop.
//          *
//          * vsetvli:
//          *
//          *   AVL = remaining element count
//          *   rd  = actual VL selected by hardware
//          *
//          * RVV explicitly supports this strip-mining model.
//          */
//         output.emplace_back("1:");

//         output.emplace_back(
//             "vsetvli " +
//             std::string(vl_tmp) + ", " +
//             std::string(length) + ", e" +
//             std::to_string(op.data_elem_size * 8) +
//             ", m1, ta, ma");

//         /*
//          * Load index vector.
//          *
//          * Because the indexed instruction encodes index EEW
//          * independently from data SEW, mixed-width indices are
//          * supported without changing vtype.
//          */
//         output.emplace_back(
//             std::string(rvv_load(op.index_elem_size)) +
//             " " +
//             std::string(index_v) +
//             ", (" +
//             std::string(indices) +
//             ")");

//         /*
//          * Gather:
//          *
//          * data[i] = *(base + indices[i])
//          */
//         output.emplace_back(
//             std::string(rvv_gather(
//                 op.index_elem_size,
//                 config.memory_order)) +
//             " " +
//             std::string(data_v) +
//             ", (" +
//             std::string(base) +
//             "), " +
//             std::string(index_v));

//         /*
//          * Store gathered values into destination.
//          */
//         output.emplace_back(
//             std::string(rvv_store(op.data_elem_size)) +
//             " " +
//             std::string(data_v) +
//             ", (" +
//             std::string(destination) +
//             ")");

//         /*
//          * indices += VL * sizeof(index)
//          */
//         emit_pointer_advance(
//             output,
//             indices,
//             vl_tmp,
//             address_tmp,
//             op.index_elem_size);

//         /*
//          * destination += VL * sizeof(data)
//          */
//         emit_pointer_advance(
//             output,
//             destination,
//             vl_tmp,
//             address_tmp,
//             op.data_elem_size);

//         /*
//          * remaining -= VL
//          */
//         output.emplace_back(
//             "sub " +
//             std::string(length) +
//             ", " +
//             std::string(length) +
//             ", " +
//             std::string(vl_tmp));

//         output.emplace_back(
//             "bnez " +
//             std::string(length) +
//             ", 1b");

//         output.emplace_back("2:");
//     }

//     // ---------------------------------------------------------------------
//     // RVV scatter
//     // ---------------------------------------------------------------------

//     static void emit_scatter_rvv(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config) {

//         const auto base =
//             gpr_name(op.base_reg);

//         const auto indices =
//             gpr_name(op.indices_reg);

//         const auto source =
//             gpr_name(op.data_reg);

//         const auto length =
//             gpr_name(op.length_reg);

//         const auto vl_tmp =
//             gpr_name(config.scratch0);

//         const auto address_tmp =
//             gpr_name(config.scratch1);

//         const auto index_v =
//             vreg_name(config.index_vreg);

//         const auto data_v =
//             vreg_name(config.data_vreg);

//         if (config.emit_comments) {
//             output.emplace_back(
//                 "# DGTL RISC-V64/RVV scatter");

//             output.emplace_back(
//                 "# indices are byte offsets");

//             output.emplace_back(
//                 "# clobbers: " +
//                 std::string(vl_tmp) + ", " +
//                 std::string(address_tmp) + ", " +
//                 std::string(vreg_name(config.index_vreg)) + ", " +
//                 std::string(vreg_name(config.data_vreg)));
//         }

//         output.emplace_back(
//             "beqz " + std::string(length) + ", 2f");

//         output.emplace_back("1:");

//         /*
//          * Set VLA length based on the DATA element width.
//          */
//         output.emplace_back(
//             "vsetvli " +
//             std::string(vl_tmp) + ", " +
//             std::string(length) + ", e" +
//             std::to_string(op.data_elem_size * 8) +
//             ", m1, ta, ma");

//         /*
//          * Load byte offsets.
//          */
//         output.emplace_back(
//             std::string(rvv_load(op.index_elem_size)) +
//             " " +
//             std::string(index_v) +
//             ", (" +
//             std::string(indices) +
//             ")");

//         /*
//          * Load contiguous source data.
//          */
//         output.emplace_back(
//             std::string(rvv_load(op.data_elem_size)) +
//             " " +
//             std::string(data_v) +
//             ", (" +
//             std::string(source) +
//             ")");

//         /*
//          * base + offsets[i] -> destination addresses.
//          */
//         output.emplace_back(
//             std::string(rvv_scatter(
//                 op.index_elem_size,
//                 config.memory_order)) +
//             " " +
//             std::string(data_v) +
//             ", (" +
//             std::string(base) +
//             "), " +
//             std::string(index_v));

//         /*
//          * indices += VL * sizeof(index)
//          */
//         emit_pointer_advance(
//             output,
//             indices,
//             vl_tmp,
//             address_tmp,
//             op.index_elem_size);

//         /*
//          * source += VL * sizeof(data)
//          */
//         emit_pointer_advance(
//             output,
//             source,
//             vl_tmp,
//             address_tmp,
//             op.data_elem_size);

//         /*
//          * remaining -= VL
//          */
//         output.emplace_back(
//             "sub " +
//             std::string(length) +
//             ", " +
//             std::string(length) +
//             ", " +
//             std::string(vl_tmp));

//         output.emplace_back(
//             "bnez " +
//             std::string(length) +
//             ", 1b");

//         output.emplace_back("2:");
//     }

//     // ---------------------------------------------------------------------
//     // Scalar gather
//     // ---------------------------------------------------------------------

//     static void emit_gather_scalar(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config) {

//         const auto base =
//             gpr_name(op.base_reg);

//         const auto indices =
//             gpr_name(op.indices_reg);

//         const auto destination =
//             gpr_name(op.data_reg);

//         const auto length =
//             gpr_name(op.length_reg);

//         const auto index_tmp =
//             gpr_name(config.scratch0);

//         const auto address_tmp =
//             gpr_name(config.scratch1);

//         const auto value_tmp =
//             gpr_name(config.scratch2);

//         if (config.emit_comments) {
//             output.emplace_back(
//                 "# DGTL RISC-V64 scalar gather fallback");

//             output.emplace_back(
//                 "# indices are byte offsets");

//             output.emplace_back(
//                 "# clobbers: " +
//                 std::string(index_tmp) + ", " +
//                 std::string(address_tmp) + ", " +
//                 std::string(value_tmp));
//         }

//         output.emplace_back(
//             "beqz " +
//             std::string(length) +
//             ", 2f");

//         output.emplace_back("1:");

//         /*
//          * index_tmp = indices[i]
//          */
//         output.emplace_back(
//             std::string(scalar_load(op.index_elem_size)) +
//             " " +
//             std::string(index_tmp) +
//             ", 0(" +
//             std::string(indices) +
//             ")");

//         /*
//          * address_tmp = base + byte_offset
//          */
//         output.emplace_back(
//             "add " +
//             std::string(address_tmp) +
//             ", " +
//             std::string(base) +
//             ", " +
//             std::string(index_tmp));

//         /*
//          * value_tmp = *(base + index)
//          */
//         output.emplace_back(
//             std::string(scalar_load(op.data_elem_size)) +
//             " " +
//             std::string(value_tmp) +
//             ", 0(" +
//             std::string(address_tmp) +
//             ")");

//         /*
//          * destination[i] = value
//          */
//         output.emplace_back(
//             std::string(scalar_store(op.data_elem_size)) +
//             " " +
//             std::string(value_tmp) +
//             ", 0(" +
//             std::string(destination) +
//             ")");

//         emit_scalar_pointer_increment(
//             output,
//             indices,
//             op.index_elem_size);

//         emit_scalar_pointer_increment(
//             output,
//             destination,
//             op.data_elem_size);

//         output.emplace_back(
//             "addi " +
//             std::string(length) +
//             ", " +
//             std::string(length) +
//             ", -1");

//         output.emplace_back(
//             "bnez " +
//             std::string(length) +
//             ", 1b");

//         output.emplace_back("2:");
//     }

//     // ---------------------------------------------------------------------
//     // Scalar scatter
//     // ---------------------------------------------------------------------

//     static void emit_scatter_scalar(
//         std::vector<std::string>& output,
//         const Operands& op,
//         const Config& config) {

//         const auto base =
//             gpr_name(op.base_reg);

//         const auto indices =
//             gpr_name(op.indices_reg);

//         const auto source =
//             gpr_name(op.data_reg);

//         const auto length =
//             gpr_name(op.length_reg);

//         const auto index_tmp =
//             gpr_name(config.scratch0);

//         const auto address_tmp =
//             gpr_name(config.scratch1);

//         const auto value_tmp =
//             gpr_name(config.scratch2);

//         if (config.emit_comments) {
//             output.emplace_back(
//                 "# DGTL RISC-V64 scalar scatter fallback");

//             output.emplace_back(
//                 "# indices are byte offsets");

//             output.emplace_back(
//                 "# clobbers: " +
//                 std::string(index_tmp) + ", " +
//                 std::string(address_tmp) + ", " +
//                 std::string(value_tmp));
//         }

//         output.emplace_back(
//             "beqz " +
//             std::string(length) +
//             ", 2f");

//         output.emplace_back("1:");

//         /*
//          * index_tmp = indices[i]
//          */
//         output.emplace_back(
//             std::string(scalar_load(op.index_elem_size)) +
//             " " +
//             std::string(index_tmp) +
//             ", 0(" +
//             std::string(indices) +
//             ")");

//         /*
//          * address_tmp = base + byte_offset
//          */
//         output.emplace_back(
//             "add " +
//             std::string(address_tmp) +
//             ", " +
//             std::string(base) +
//             ", " +
//             std::string(index_tmp));

//         /*
//          * value_tmp = source[i]
//          */
//         output.emplace_back(
//             std::string(scalar_load(op.data_elem_size)) +
//             " " +
//             std::string(value_tmp) +
//             ", 0(" +
//             std::string(source) +
//             ")");

//         /*
//          * *(base + index) = value
//          */
//         output.emplace_back(
//             std::string(scalar_store(op.data_elem_size)) +
//             " " +
//             std::string(value_tmp) +
//             ", 0(" +
//             std::string(address_tmp));

//         emit_scalar_pointer_increment(
//             output,
//             indices,
//             op.index_elem_size);

//         emit_scalar_pointer_increment(
//             output,
//             source,
//             op.data_elem_size);

//         output.emplace_back(
//             "addi " +
//             std::string(length) +
//             ", " +
//             std::string(length) +
//             ", -1");

//         output.emplace_back(
//             "bnez " +
//             std::string(length) +
//             ", 1b");

//         output.emplace_back("2:");
//     }

//     // ---------------------------------------------------------------------
//     // Pointer arithmetic
//     // ---------------------------------------------------------------------

//     static void emit_scalar_pointer_increment(
//         std::vector<std::string>& output,
//         std::string_view reg,
//         uint8_t element_size) {

//         switch (element_size) {
//             case 1:
//                 output.emplace_back(
//                     "addi " +
//                     std::string(reg) +
//                     ", " +
//                     std::string(reg) +
//                     ", 1");
//                 break;

//             case 2:
//                 output.emplace_back(
//                     "addi " +
//                     std::string(reg) +
//                     ", " +
//                     std::string(reg) +
//                     ", 2");
//                 break;

//             case 4:
//                 output.emplace_back(
//                     "addi " +
//                     std::string(reg) +
//                     ", " +
//                     std::string(reg) +
//                     ", 4");
//                 break;

//             case 8:
//                 output.emplace_back(
//                     "addi " +
//                     std::string(reg) +
//                     ", " +
//                     std::string(reg) +
//                     ", 8");
//                 break;

//             default:
//                 break;
//         }
//     }

//     static void emit_pointer_advance(
//         std::vector<std::string>& output,
//         std::string_view pointer,
//         std::string_view vl,
//         std::string_view temporary,
//         uint8_t element_size) {

//         const uint32_t shift =
//             log2_size(element_size);

//         if (shift == 0) {

//             output.emplace_back(
//                 "add " +
//                 std::string(pointer) +
//                 ", " +
//                 std::string(pointer) +
//                 ", " +
//                 std::string(vl));

//             return;
//         }

//         output.emplace_back(
//             "slli " +
//             std::string(temporary) +
//             ", " +
//             std::string(vl) +
//             ", " +
//             std::to_string(shift));

//         output.emplace_back(
//             "add " +
//             std::string(pointer) +
//             ", " +
//             std::string(pointer) +
//             ", " +
//             std::string(temporary));
//     }

//     // ---------------------------------------------------------------------
//     // Diagnostics
//     // ---------------------------------------------------------------------

//     static void reject(
//         std::vector<std::string>& output,
//         const Config& config,
//         const std::string& message) {

//         if (config.emit_comments) {
//             output.push_back(
//                 "# DGTL ERROR: " + message);
//         }
//     }
// };

// export namespace dgtl::isa::riscv64 {

// using RiscV64GatherScatter =
//     dgtl::isa::emitter::RiscV64GatherScatter;

// } // namespace dgtl::isa::riscv64
