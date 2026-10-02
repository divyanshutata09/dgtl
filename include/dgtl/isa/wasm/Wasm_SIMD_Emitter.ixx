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

#include <cstdint>
#include <string>
#include <vector>
export module dgtl.isa.wasm.simd_emitter;

export namespace dgtl::isa::wasm::emitter {

class WasmSIMDGatherScatterFMA {
public:
    struct Config {
        bool emit_comments = true;

        constexpr Config() noexcept = default;
    };

    enum class WasmType : uint8_t {
        i32x4 = 0,
        i64x2 = 1,
        f32x4 = 2,
        f64x2 = 3
    };

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_offset,
                            uint32_t indices_local,
                            uint32_t data_local,
                            WasmType type) noexcept {
        emit_gather(output, base_offset, indices_local, data_local, type, Config{});
    }

    static void emit_gather(std::vector<std::string>& output,
                            uint32_t base_offset,
                            uint32_t indices_local,
                            uint32_t data_local,
                            WasmType type,
                            const Config& config) noexcept {

        if (config.emit_comments) {
            output.push_back("(comment \"WASM gather pattern\")");
            output.push_back("(comment \"Gather lanes via index vector\")");
        }

        const uint32_t lanes = lane_count(type);
        for (uint32_t i = 0; i < lanes; ++i) {
            output.push_back("(local.get " + std::to_string(indices_local) + ")");
            output.push_back("(" + std::string(extract_lane_op(type)) + " " + std::to_string(i) + ")");
            if (type == WasmType::i64x2 || type == WasmType::f64x2) {
                output.push_back("(i32.wrap_i64)");
            }
            output.push_back("(i32.const " + std::to_string(base_offset) + ")");
            output.push_back("(i32.add)");
            output.push_back("(" + std::string(scalar_load_op(type)) + ")");
        }

        output.push_back("(" + std::string(vector_make_op(type)) + ")");
        output.push_back("(local.set " + std::to_string(data_local) + ")");
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_offset,
                             uint32_t indices_local,
                             uint32_t data_local,
                             WasmType type) noexcept {
        emit_scatter(output, base_offset, indices_local, data_local, type, Config{});
    }

    static void emit_scatter(std::vector<std::string>& output,
                             uint32_t base_offset,
                             uint32_t indices_local,
                             uint32_t data_local,
                             WasmType type,
                             const Config& config) noexcept {

        if (config.emit_comments) {
            output.push_back("(comment \"WASM scatter pattern\")");
        }

        const uint32_t lanes = lane_count(type);
        for (uint32_t i = 0; i < lanes; ++i) {
            output.push_back("(local.get " + std::to_string(indices_local) + ")");
            output.push_back("(" + std::string(extract_lane_op(type)) + " " + std::to_string(i) + ")");
            if (type == WasmType::i64x2 || type == WasmType::f64x2) {
                output.push_back("(i32.wrap_i64)");
            }
            output.push_back("(i32.const " + std::to_string(base_offset) + ")");
            output.push_back("(i32.add)");

            output.push_back("(local.get " + std::to_string(data_local) + ")");
            output.push_back("(" + std::string(extract_lane_op(type)) + " " + std::to_string(i) + ")");
            output.push_back("(" + std::string(scalar_store_op(type)) + ")");
        }
    }

    static void emit_fma(std::vector<std::string>& output,
                          uint32_t a_local,
                          uint32_t b_local,
                          uint32_t c_local,
                          uint32_t result_local,
                          WasmType type) noexcept {
        emit_fma(output, a_local, b_local, c_local, result_local, type, Config{});
    }

    static void emit_fma(std::vector<std::string>& output,
                          uint32_t a_local,
                          uint32_t b_local,
                          uint32_t c_local,
                          uint32_t result_local,
                          WasmType type,
                          const Config& config) noexcept {

        if (type == WasmType::i32x4) {
            if (config.emit_comments) {
                output.push_back("(comment \"WASM i32x4 FMA: a * b + c\")");
            }

            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(i32x4.mul)");

            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(i32x4.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        } else if (type == WasmType::f32x4) {
            if (config.emit_comments) {
                output.push_back("(comment \"WASM f32x4 FMA: a * b + c\")");
            }

            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(f32x4.mul)");

            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(f32x4.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        } else if (type == WasmType::f64x2) {
            if (config.emit_comments) {
                output.push_back("(comment \"WASM f64x2 FMA: a * b + c\")");
            }

            output.push_back("(local.get " + std::to_string(a_local) + ")");
            output.push_back("(local.get " + std::to_string(b_local) + ")");
            output.push_back("(f64x2.mul)");

            output.push_back("(local.get " + std::to_string(c_local) + ")");
            output.push_back("(f64x2.add)");

            output.push_back("(local.set " + std::to_string(result_local) + ")");
        } else {
            if (config.emit_comments) {
                output.push_back("(comment \"WASM FMA skipped for unsupported SIMD type\")");
            }
        }
    }

    static void emit_fma_optimized(std::vector<std::string>& output,
                                   uint32_t a_local,
                                   uint32_t b_local,
                                   uint32_t c_local,
                                   uint32_t result_local,
                                   WasmType type) noexcept {
        emit_fma_optimized(output, a_local, b_local, c_local, result_local, type, Config{});
    }

    static void emit_fma_optimized(std::vector<std::string>& output,
                                   uint32_t a_local,
                                   uint32_t b_local,
                                   uint32_t c_local,
                                   uint32_t result_local,
                                   WasmType type,
                                   const Config& config) noexcept {

        if (config.emit_comments) {
            output.push_back("(comment \"WASM FMA (runtime-fused by engine)\")");
        }

        output.push_back("(local.get " + std::to_string(a_local) + ")");
        output.push_back("(local.get " + std::to_string(b_local) + ")");

        if (type == WasmType::f32x4) {
            output.push_back("(f32x4.mul)");
        } else if (type == WasmType::f64x2) {
            output.push_back("(f64x2.mul)");
        } else if (type == WasmType::i32x4) {
            output.push_back("(i32x4.mul)");
        } else {
            if (config.emit_comments) {
                output.push_back("(comment \"WASM FMA optimized path skipped for unsupported SIMD type\")");
            }
            return;
        }

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

private:
    [[nodiscard]] static constexpr uint32_t lane_count(WasmType type) noexcept {
        return (type == WasmType::i64x2 || type == WasmType::f64x2) ? 2u : 4u;
    }

    [[nodiscard]] static constexpr const char* extract_lane_op(WasmType type) noexcept {
        switch (type) {
            case WasmType::i32x4: return "i32x4.extract_lane";
            case WasmType::i64x2: return "i64x2.extract_lane";
            case WasmType::f32x4: return "f32x4.extract_lane";
            case WasmType::f64x2: return "f64x2.extract_lane";
            default: return "i32x4.extract_lane";
        }
    }

    [[nodiscard]] static constexpr const char* scalar_load_op(WasmType type) noexcept {
        switch (type) {
            case WasmType::i32x4: return "i32.load";
            case WasmType::i64x2: return "i64.load";
            case WasmType::f32x4: return "f32.load";
            case WasmType::f64x2: return "f64.load";
            default: return "i32.load";
        }
    }

    [[nodiscard]] static constexpr const char* scalar_store_op(WasmType type) noexcept {
        switch (type) {
            case WasmType::i32x4: return "i32.store";
            case WasmType::i64x2: return "i64.store";
            case WasmType::f32x4: return "f32.store";
            case WasmType::f64x2: return "f64.store";
            default: return "i32.store";
        }
    }

    [[nodiscard]] static constexpr const char* vector_make_op(WasmType type) noexcept {
        switch (type) {
            case WasmType::i32x4: return "i32x4.make";
            case WasmType::i64x2: return "i64x2.make";
            case WasmType::f32x4: return "f32x4.make";
            case WasmType::f64x2: return "f64x2.make";
            default: return "i32x4.make";
        }
    }
};

} // namespace dgtl::isa::wasm::wasm::emitter

export namespace dgtl::isa::wasm {
using WasmSIMDGatherScatterFMA = dgtl::isa::wasm::emitter::WasmSIMDGatherScatterFMA;
}
export namespace dgtl::isa {
using WasmSIMDGatherScatterFMA = dgtl::isa::wasm::emitter::WasmSIMDGatherScatterFMA;
}
