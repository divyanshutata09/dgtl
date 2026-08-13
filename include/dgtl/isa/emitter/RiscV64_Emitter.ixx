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

export module dgtl.ir_core.riscv64_emitter;

export namespace dgtl::isa::emitter {

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

} // namespace dgtl::isa::emitter

export namespace dgtl::isa {
using RiscV64GatherScatter = dgtl::isa::emitter::RiscV64GatherScatter;
}
