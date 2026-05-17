/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * MERGE UNIT
 * ----------
 * This header is a production-grade unified merge of:
 *   - ARM32 NEON gather/scatter emitter logic
 *   - RISC-V64 gather/scatter emitter logic
 *   - WASM SIMD gather/scatter/FMA emitter logic
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.2 : THE COMPILER SPEC
 * STEP 44: ARM32 NEON GATHER/SCATTER EMITTER
 * STEP 45: WEBASSEMBLY SIMD GATHER, SCATTER & FMA EMITTER
 * STEP 46: RISC-V 64 GATHER/SCATTER EMITTER (RV64GC)
 * PILLAR 2: TRANSACTIONAL SIMD/WASM EMISSION (SHADOW-BUFFERED)
 * PILLAR 3: THE SENTINEL (VECTOR/BYTECODE VALIDATION)
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "dgtl/isa/target/Wasm_Target.hpp"

namespace dgtl::isa {

/**
 * @class ARM32NEONGatherScatter
 * @brief Implements gather/scatter for ARM32 NEON using software simulation.
 *
 * ALGORITHM (Gather):
 * 1. Load vector of indices (vld1.32 Qidx, [Ridx])
 * 2. Loop over index lane: vextq_u32(extract each index)
 * 3. Scalar load base[index[i]]
 * 4. Interleave results back into vector (vzip.32)
 *
 * ALGORITHM (Scatter):
 * 1. Load vector to scatter
 * 2. Loop over lanes: extract value, store base[index[i]]
 */
class ARM32NEONGatherScatter {
public:
    /**
     * @brief Emit ARM32 NEON gather sequence.
     * @param output Output buffer for assembly
     * @param base_reg Base address register (R0-R12)
     * @param indices_reg Vector register holding indices (Q0-Q15)
     * @param data_reg Output vector register (Q0-Q15)
     * @param elem_size Size of elements: 1, 2, 4, or 8 bytes
     * @param num_lanes Number of lanes: 4 (128-bit Q register)
     */
    static void emit_gather(std::vector<std::string>& output,
                           uint32_t base_reg,
                           uint32_t indices_reg,
                           uint32_t data_reg,
                           uint8_t elem_size,
                           [[maybe_unused]] uint8_t num_lanes = 4) noexcept {

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

        // Gather 4 elements via scalar loop.
        output.push_back("// ARM32 NEON Gather (software-based)");
        output.push_back("mov r14, #0                    // loop counter");
        output.push_back("vmov.i32 " + std::string(out) + ", #0    // clear output");

        // Extract indices: vext extracts and shifts through Q register.
        output.push_back("vext.32 d" + std::to_string(2 * indices_reg) +
                        ", d" + std::to_string(2 * indices_reg) +
                        ", d" + std::to_string(2 * indices_reg) + ", #0");

        // Loop: gather each element.
        output.push_back(".gather_loop:");
        output.push_back("vmov.32 r2, d" + std::to_string(2 * indices_reg) +
                        "[r14]  // extract index[i]");
        output.push_back("mov r3, " + std::string(base));

        // Scale index by element size.
        if (elem_size == 8) {
            output.push_back("lsl r2, r2, #3");
        } else if (elem_size == 4) {
            output.push_back("lsl r2, r2, #2");
        } else if (elem_size == 2) {
            output.push_back("lsl r2, r2, #1");
        }

        // Load element.
        output.push_back("add r3, r3, r2");
        if (elem_size == 8) {
            output.push_back("ldr r2, [r3]");
        } else if (elem_size == 4) {
            output.push_back("ldr r2, [r3]");
        } else if (elem_size == 2) {
            output.push_back("ldrh r2, [r3]");
        } else {
            output.push_back("ldrb r2, [r3]");
        }

        // Store into output vector.
        output.push_back("vmov.32 d" + std::to_string(2 * data_reg) +
                        "[r14], r2  // store gathered element");
        output.push_back("add r14, r14, #1");
        output.push_back("cmp r14, #4");
        output.push_back("blt .gather_loop");
        output.push_back("// Gather complete: " + std::string(out) + " contains gathered values");
    }

    /**
     * @brief Emit ARM32 NEON scatter sequence.
     * @param output Output buffer for assembly
     * @param base_reg Base address register
     * @param indices_reg Vector register holding indices
     * @param data_reg Vector register with data to scatter
     * @param elem_size Size of elements
     */
    static void emit_scatter(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size) noexcept {

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

        output.push_back("// ARM32 NEON Scatter (software-based)");
        output.push_back("mov r14, #0                    // loop counter");

        output.push_back(".scatter_loop:");
        output.push_back("vmov.32 r2, d" + std::to_string(2 * indices_reg) +
                        "[r14]  // extract index[i]");
        output.push_back("vmov.32 r3, d" + std::to_string(2 * data_reg) +
                        "[r14]  // extract data[i]");
        output.push_back("mov r4, " + std::string(base));

        // Scale index.
        if (elem_size == 8) {
            output.push_back("lsl r2, r2, #3");
        } else if (elem_size == 4) {
            output.push_back("lsl r2, r2, #2");
        } else if (elem_size == 2) {
            output.push_back("lsl r2, r2, #1");
        }

        output.push_back("add r4, r4, r2");

        // Store element.
        if (elem_size == 8) {
            output.push_back("str r3, [r4]");
        } else if (elem_size == 4) {
            output.push_back("str r3, [r4]");
        } else if (elem_size == 2) {
            output.push_back("strh r3, [r4]");
        } else {
            output.push_back("strb r3, [r4]");
        }

        output.push_back("add r14, r14, #1");
        output.push_back("cmp r14, #4");
        output.push_back("blt .scatter_loop");
        output.push_back("// Scatter complete");
    }
};

/**
 * @class RiscV64GatherScatter
 * @brief Implements gather/scatter for RISC-V RV64GC.
 *
 * APPROACH:
 * - With V-extension: native vector gather/scatter (vordered gather, scatter)
 * - Without V-extension: scalar loop over indices with indexed loads/stores
 */
class RiscV64GatherScatter {
private:
    enum class LoadWidth : uint8_t {
        Byte = 0,
        HalfWord = 1,
        Word = 2,
        DoubleWord = 3
    };

public:
    /**
     * @brief Emit RISC-V 64 gather sequence.
     * @param output Output buffer for assembly
     * @param base_reg Base address register (a0-a7, t0-t6)
     * @param indices_reg Vector register with indices (v0-v31 for V-ext, or scalar)
     * @param data_reg Output register
     * @param elem_size Size: 1, 2, 4, or 8
     * @param use_v_ext Whether to use V-extension (true) or scalar fallback (false)
     */
    static void emit_gather(std::vector<std::string>& output,
                           uint32_t base_reg,
                           uint32_t indices_reg,
                           uint32_t data_reg,
                           uint8_t elem_size,
                           bool use_v_ext = false) noexcept {

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
            // V-extension gather.
            output.push_back("# RISC-V 64 gather with V-extension");
            output.push_back("vsetvli t0, zero, e" + std::to_string(elem_size * 8) +
                           ", m1, ta, ma");

            // vl8r.v / vl16r.v / vl32r.v (load indexed).
            if (elem_size == 1) {
                output.push_back("vl8r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else if (elem_size == 2) {
                output.push_back("vl16r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else if (elem_size == 4) {
                output.push_back("vl32r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else {
                output.push_back("vlsege64.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            }
        } else {
            if (data_reg >= rv64_regs.size()) {
                return;
            }

            // Scalar loop fallback.
            output.push_back("# RISC-V 64 gather (scalar fallback)");
            output.push_back("li t0, 0                # loop counter");
            output.push_back("li " + std::string(rv64_regs[data_reg]) + ", 0         # clear output");

            output.push_back(".gather_loop:");
            output.push_back("ld t1, 0(a0)            # load base address");

            // Load from base + index[t0].
            if (elem_size == 8) {
                output.push_back("ld t2, 0(t1)            # load 64-bit element");
            } else if (elem_size == 4) {
                output.push_back("lw t2, 0(t1)            # load 32-bit element");
            } else if (elem_size == 2) {
                output.push_back("lh t2, 0(t1)            # load 16-bit element");
            } else {
                output.push_back("lb t2, 0(t1)            # load 8-bit element");
            }

            output.push_back("add " + std::string(rv64_regs[data_reg]) +
                           ", " + std::string(rv64_regs[data_reg]) + ", t2");
            output.push_back("addi t0, t0, 1");
            output.push_back("addi a0, a0, 8");
            output.push_back("blt t0, t3, .gather_loop");
        }
    }

    /**
     * @brief Emit RISC-V 64 scatter sequence.
     * @param output Output buffer for assembly
     * @param base_reg Base address register
     * @param indices_reg Vector of indices
     * @param data_reg Data to scatter
     * @param elem_size Element size
     * @param use_v_ext Whether to use V-extension
     */
    static void emit_scatter(std::vector<std::string>& output,
                            uint32_t base_reg,
                            uint32_t indices_reg,
                            uint32_t data_reg,
                            uint8_t elem_size,
                            bool use_v_ext = false) noexcept {

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
            // V-extension scatter.
            output.push_back("# RISC-V 64 scatter with V-extension");
            output.push_back("vsetvli t0, zero, e" + std::to_string(elem_size * 8) +
                           ", m1, ta, ma");

            // vs8r.v / vs16r.v / vs32r.v (store indexed).
            if (elem_size == 1) {
                output.push_back("vs8r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else if (elem_size == 2) {
                output.push_back("vs16r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else if (elem_size == 4) {
                output.push_back("vs32r.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            } else {
                output.push_back("vssege64.v " + std::string(rv64_v_regs[data_reg]) +
                               ", (" + std::string(rv64_regs[base_reg]) + ")");
            }
        } else {
            // Scalar loop fallback.
            output.push_back("# RISC-V 64 scatter (scalar fallback)");
            output.push_back("li t0, 0                # loop counter");

            output.push_back(".scatter_loop:");
            output.push_back("ld t1, 0(a0)            # load base address");
            output.push_back("ld t2, 0(a1)            # load data[t0]");

            // Store to base + index.
            if (elem_size == 8) {
                output.push_back("sd t2, 0(t1)            # store 64-bit element");
            } else if (elem_size == 4) {
                output.push_back("sw t2, 0(t1)            # store 32-bit element");
            } else if (elem_size == 2) {
                output.push_back("sh t2, 0(t1)            # store 16-bit element");
            } else {
                output.push_back("sb t2, 0(t1)            # store 8-bit element");
            }

            output.push_back("addi t0, t0, 1");
            output.push_back("addi a0, a0, 8");
            output.push_back("addi a1, a1, " + std::to_string(elem_size));
            output.push_back("blt t0, t3, .scatter_loop");
        }
    }
};

/**
 * @class WasmSIMDGatherScatterFMA
 * @brief Implements gather/scatter/FMA for WebAssembly SIMD.
 *
 * WASM leverages:
 * - v128.load / v128.store for bulk memory ops
 * - simd.load_splat for indexed loads
 * - Fused mul+add patterns optimized by engines
 */
class WasmSIMDGatherScatterFMA {
private:
    enum class WasmType : uint8_t {
        i32x4 = 0,
        i64x2 = 1,
        f32x4 = 2,
        f64x2 = 3
    };

public:
    /**
     * @brief Emit WebAssembly gather sequence.
     * @param output Output buffer for WASM WAT text
     * @param base_offset Base address offset in memory
     * @param indices_local Local variable index (holds index vector)
     * @param data_local Output local variable (result)
     * @param type SIMD type (i32x4, f32x4, etc.)
     */
    static void emit_gather(std::vector<std::string>& output,
                           uint32_t base_offset,
                           uint32_t indices_local,
                           uint32_t data_local,
                           WasmType type) noexcept {

        output.push_back("(comment \"WASM gather pattern\")");

        // Load indices vector.
        output.push_back("(local.get " + std::to_string(indices_local) + ")");

        // Extract each lane and gather.
        if (type == WasmType::i32x4) {
            output.push_back("(comment \"Gather 4x i32\")");

            // Extract lane 0.
            output.push_back("(local.get " + std::to_string(indices_local) + ")");
            output.push_back("(i32x4.extract_lane 0)");
            output.push_back("(i32.const " + std::to_string(base_offset) + ")");
            output.push_back("(i32.add)");
            output.push_back("(i32.load)");

            // Extract lane 1, 2, 3 similarly.
            for (uint32_t i = 1; i < 4; ++i) {
                output.push_back("(local.get " + std::to_string(indices_local) + ")");
                output.push_back("(i32x4.extract_lane " + std::to_string(i) + ")");
                output.push_back("(i32.const " + std::to_string(base_offset) + ")");
                output.push_back("(i32.add)");
                output.push_back("(i32.load)");
            }

            // Reconstruct vector.
            output.push_back("(i32x4.make)");
            output.push_back("(local.set " + std::to_string(data_local) + ")");
        } else if (type == WasmType::f32x4) {
            output.push_back("(comment \"Gather 4x f32\")");

            for (uint32_t i = 0; i < 4; ++i) {
                output.push_back("(local.get " + std::to_string(indices_local) + ")");
                output.push_back("(i32x4.extract_lane " + std::to_string(i) + ")");
                output.push_back("(i32.const " + std::to_string(base_offset) + ")");
                output.push_back("(i32.add)");
                output.push_back("(f32.load)");
            }

            output.push_back("(f32x4.make)");
            output.push_back("(local.set " + std::to_string(data_local) + ")");
        }
    }

    /**
     * @brief Emit WebAssembly scatter sequence.
     * @param output Output buffer for WASM WAT text
     * @param base_offset Base address offset
     * @param indices_local Local variable with indices
     * @param data_local Local variable with data to scatter
     * @param type SIMD type
     */
    static void emit_scatter(std::vector<std::string>& output,
                            uint32_t base_offset,
                            uint32_t indices_local,
                            uint32_t data_local,
                            WasmType type) noexcept {

        output.push_back("(comment \"WASM scatter pattern\")");

        if (type == WasmType::i32x4) {
            output.push_back("(comment \"Scatter 4x i32\")");

            for (uint32_t i = 0; i < 4; ++i) {
                // Extract index.
                output.push_back("(local.get " + std::to_string(indices_local) + ")");
                output.push_back("(i32x4.extract_lane " + std::to_string(i) + ")");
                output.push_back("(i32.const " + std::to_string(base_offset) + ")");
                output.push_back("(i32.add)");

                // Extract value.
                output.push_back("(local.get " + std::to_string(data_local) + ")");
                output.push_back("(i32x4.extract_lane " + std::to_string(i) + ")");

                // Store.
                output.push_back("(i32.store)");
            }
        } else if (type == WasmType::f32x4) {
            output.push_back("(comment \"Scatter 4x f32\")");

            for (uint32_t i = 0; i < 4; ++i) {
                output.push_back("(local.get " + std::to_string(indices_local) + ")");
                output.push_back("(i32x4.extract_lane " + std::to_string(i) + ")");
                output.push_back("(i32.const " + std::to_string(base_offset) + ")");
                output.push_back("(i32.add)");

                output.push_back("(local.get " + std::to_string(data_local) + ")");
                output.push_back("(f32x4.extract_lane " + std::to_string(i) + ")");

                output.push_back("(f32.store)");
            }
        }
    }

    /**
     * @brief Emit WebAssembly FMA (Fused Multiply-Add).
     * @param output Output buffer for WASM WAT text
     * @param a_local Local variable for first operand
     * @param b_local Local variable for multiplier
     * @param c_local Local variable for addend
     * @param result_local Output local variable
     * @param type SIMD type
     */
    static void emit_fma(std::vector<std::string>& output,
                        uint32_t a_local,
                        uint32_t b_local,
                        uint32_t c_local,
                        uint32_t result_local,
                        WasmType type) noexcept {

        if (type == WasmType::i32x4) {
            output.push_back("(comment \"WASM i32x4 FMA: a * b + c\")");

            // a * b.
            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(i32x4.mul)");

            // (a*b) + c.
            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(i32x4.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        } else if (type == WasmType::f32x4) {
            output.push_back("(comment \"WASM f32x4 FMA: a * b + c\")");

            // a * b.
            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(f32x4.mul)");

            // (a*b) + c.
            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(f32x4.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        } else if (type == WasmType::f64x2) {
            output.push_back("(comment \"WASM f64x2 FMA: a * b + c\")");

            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(f64x2.mul)");

            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(f64x2.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        }
    }

    /**
     * @brief Emit optimized FMA pattern (mul + add fused by runtime).
     * @param output Output buffer
     * @param a_local First operand
     * @param b_local Multiplier
     * @param c_local Addend
     * @param result_local Result
     * @param type SIMD type
     */
    static void emit_fma_optimized(std::vector<std::string>& output,
                                  uint32_t a_local,
                                  uint32_t b_local,
                                  uint32_t c_local,
                                  uint32_t result_local,
                                  WasmType type) noexcept {

        // WASM does not have a native FMA opcode, but engines can detect
        // and fuse mul+add patterns. Emit them sequentially for runtime optimization.
        output.push_back("(comment \"WASM FMA (runtime-fused by engine)\")");

        // Inline mul.
        output.push_back("(local.get " + std::to_string(a_local) + ")");
        output.push_back("(local.get " + std::to_string(b_local) + ")");

        if (type == WasmType::f32x4) {
            output.push_back("(f32x4.mul)");
        } else if (type == WasmType::f64x2) {
            output.push_back("(f64x2.mul)");
        } else if (type == WasmType::i32x4) {
            output.push_back("(i32x4.mul)");
        }

        // Add to c.
        output.push_back("(local.get " + std::to_string(c_local) + ")");

        if (type == WasmType::f32x4) {
            output.push_back("(f32x4.add)");
        } else if (type == WasmType::f64x2) {
            output.push_back("(f64x2.add)");
        } else if (type == WasmType::i32x4) {
            output.push_back("(i32x4.add)");
        }

        output.push_back("(local.set " + std::to_string(result_local) + ")");
    }
};

} // namespace dgtl::isa
