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
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module dgtl.ir_core.arm32neon_emitter;

export namespace dgtl::isa::emitter {

class ARM32NEONGatherScatter {
public:
    struct Config {
        bool strict_validation = true;
        bool emit_comments = true;

        constexpr Config() noexcept = default;
    };

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size,
                            [[maybe_unused]] uint8_t num_lanes = 4) noexcept {
        emit_gather(output, base_reg, indices_reg, data_reg, elem_size, num_lanes, Config{});
    }

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size,
                            [[maybe_unused]] uint8_t num_lanes,
                            const Config& config) noexcept {
        if (config.strict_validation && !is_supported_elem_size(elem_size)) {
            if (config.emit_comments) {
                output.push_back("// DGTL ARM32NEON: gather skipped (unsupported element size)");
            }
            return;
        }

        const uint32_t lanes = normalize_lanes(num_lanes);
        if (lanes == 0) {
            if (config.emit_comments) {
                output.push_back("// DGTL ARM32NEON: gather skipped (invalid lane count)");
            }
            return;
        }

        const std::array<const char*, 13> arm32_regs = {
            "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
            "r8", "r9", "r10", "r11", "r12"
        };

        const std::array<const char*, 16> neon_regs = {
            "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7",
            "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"
        };

        if (base_reg >= arm32_regs.size() || indices_reg >= neon_regs.size() ||
            data_reg >= neon_regs.size()) {
            return;
        }

        const char* base = arm32_regs[base_reg];
        const char* out = neon_regs[data_reg];
        const std::string loop_label = ".L_dgtl_arm32neon_gather_" + std::to_string(output.size());

        if (config.emit_comments) {
            output.push_back("// ARM32 NEON Gather (software-based)");
        }
        output.push_back("mov r14, #0                    // loop counter");
        output.push_back("vmov.i32 " + std::string(out) + ", #0    // clear output");

        output.push_back("vext.32 d" + std::to_string(2 * indices_reg) +
                         ", d" + std::to_string(2 * indices_reg) +
                         ", d" + std::to_string(2 * indices_reg) + ", #0");

        output.push_back(loop_label + ":");
        output.push_back("vmov.32 r2, d" + std::to_string(2 * indices_reg) +
                         "[r14]  // extract index[i]");
        output.push_back("mov r3, " + std::string(base));

        if (const uint32_t shift = scale_shift_for_elem_size(elem_size); shift != 0) {
            output.push_back("lsl r2, r2, #" + std::to_string(shift));
        }

        output.push_back("add r3, r3, r2");
        if (elem_size == 8 || elem_size == 4) {
            output.push_back("ldr r2, [r3]");
        } else if (elem_size == 2) {
            output.push_back("ldrh r2, [r3]");
        } else {
            output.push_back("ldrb r2, [r3]");
        }

        output.push_back("vmov.32 d" + std::to_string(2 * data_reg) +
                         "[r14], r2  // store gathered element");
        output.push_back("add r14, r14, #1");
        output.push_back("cmp r14, #" + std::to_string(lanes));
        output.push_back("blt " + loop_label);
        if (config.emit_comments) {
            output.push_back("// Gather complete: " + std::string(out) + " contains gathered values");
        }
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_reg,
                             uint32_t indices_reg,
                             uint32_t data_reg,
                             uint8_t elem_size) noexcept {
        emit_scatter(output, base_reg, indices_reg, data_reg, elem_size, Config{});
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_reg,
                             uint32_t indices_reg,
                             uint32_t data_reg,
                             uint8_t elem_size,
                             const Config& config) noexcept {
        if (config.strict_validation && !is_supported_elem_size(elem_size)) {
            if (config.emit_comments) {
                output.push_back("// DGTL ARM32NEON: scatter skipped (unsupported element size)");
            }
            return;
        }

        const std::array<const char*, 13> arm32_regs = {
            "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
            "r8", "r9", "r10", "r11", "r12"
        };

        const std::array<const char*, 16> neon_regs = {
            "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7",
            "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"
        };

        if (base_reg >= arm32_regs.size() || indices_reg >= neon_regs.size() ||
            data_reg >= neon_regs.size()) {
            return;
        }

        const char* base = arm32_regs[base_reg];
        const std::string loop_label = ".L_dgtl_arm32neon_scatter_" + std::to_string(output.size());

        if (config.emit_comments) {
            output.push_back("// ARM32 NEON Scatter (software-based)");
        }
        output.push_back("mov r14, #0                    // loop counter");

        output.push_back(loop_label + ":");
        output.push_back("vmov.32 r2, d" + std::to_string(2 * indices_reg) +
                         "[r14]  // extract index[i]");
        output.push_back("vmov.32 r3, d" + std::to_string(2 * data_reg) +
                         "[r14]  // extract data[i]");
        output.push_back("mov r4, " + std::string(base));

        if (const uint32_t shift = scale_shift_for_elem_size(elem_size); shift != 0) {
            output.push_back("lsl r2, r2, #" + std::to_string(shift));
        }

        output.push_back("add r4, r4, r2");

        if (elem_size == 8 || elem_size == 4) {
            output.push_back("str r3, [r4]");
        } else if (elem_size == 2) {
            output.push_back("strh r3, [r4]");
        } else {
            output.push_back("strb r3, [r4]");
        }

        output.push_back("add r14, r14, #1");
        output.push_back("cmp r14, #4");
        output.push_back("blt " + loop_label);
        if (config.emit_comments) {
            output.push_back("// Scatter complete");
        }
    }

private:
    [[nodiscard]] static constexpr bool is_supported_elem_size(uint8_t elem_size) noexcept {
        return elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8;
    }

    [[nodiscard]] static constexpr uint32_t scale_shift_for_elem_size(uint8_t elem_size) noexcept {
        return (elem_size == 8) ? 3u : (elem_size == 4) ? 2u : (elem_size == 2) ? 1u : 0u;
    }

    [[nodiscard]] static constexpr uint32_t normalize_lanes(uint8_t requested_lanes) noexcept {
        return (requested_lanes == 0) ? 0u : (requested_lanes > 4 ? 4u : requested_lanes);
    }
};

} // namespace dgtl::isa::emitter

export namespace dgtl::isa {
using ARM32NEONGatherScatter = dgtl::isa::emitter::ARM32NEONGatherScatter;
}
