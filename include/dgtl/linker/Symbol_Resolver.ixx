/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

export module dgtl.linker.symbols;

import dgtl.mir;
import dgtl.object.symbol;
import dgtl.runtime.maths;
import dgtl.runtime.intrinsics;

export namespace dgtl::linker {

class SymbolResolver {
    std::unordered_map<std::string, uint64_t> m_;

public:
    SymbolResolver() {
        register_runtime_math_intrinsics();
    }

    bool define(std::string n, uint64_t v) {
        return m_.emplace(std::move(n), v).second;
    }

    void set(std::string n, uint64_t v) {
        m_[std::move(n)] = v;
    }

    bool resolve(std::string_view n, uint64_t& v) const noexcept {
        auto i = m_.find(std::string(n));
        if (i != m_.end()) {
            v = i->second;
            return true;
        }
        return resolve_builtin_runtime_symbol(n, v);
    }

    bool contains(std::string_view n) const {
        if (m_.contains(std::string(n))) return true;
        uint64_t dummy = 0;
        return resolve_builtin_runtime_symbol(n, dummy);
    }

    static uintptr_t resolve_builtin_runtime_symbol(std::string_view n) noexcept {
        uint64_t v = 0;
        if (resolve_builtin_runtime_symbol(n, v)) {
            return static_cast<uintptr_t>(v);
        }
        return 0;
    }

    static bool resolve_builtin_runtime_symbol(std::string_view n, uint64_t& v) noexcept {
        if (!n.starts_with("__dgtl_rt_")) return false;
        #define DGTL_RESOLVE_RT(sym) if (n == #sym) { v = reinterpret_cast<uint64_t>(&sym); return true; }
        DGTL_RESOLVE_RT(__dgtl_rt_sin);
        DGTL_RESOLVE_RT(__dgtl_rt_cos);
        DGTL_RESOLVE_RT(__dgtl_rt_tan);
        DGTL_RESOLVE_RT(__dgtl_rt_asin);
        DGTL_RESOLVE_RT(__dgtl_rt_acos);
        DGTL_RESOLVE_RT(__dgtl_rt_atan);
        DGTL_RESOLVE_RT(__dgtl_rt_atan2);
        DGTL_RESOLVE_RT(__dgtl_rt_sinh);
        DGTL_RESOLVE_RT(__dgtl_rt_cosh);
        DGTL_RESOLVE_RT(__dgtl_rt_tanh);
        DGTL_RESOLVE_RT(__dgtl_rt_asinh);
        DGTL_RESOLVE_RT(__dgtl_rt_acosh);
        DGTL_RESOLVE_RT(__dgtl_rt_atanh);
        DGTL_RESOLVE_RT(__dgtl_rt_log);
        DGTL_RESOLVE_RT(__dgtl_rt_log2);
        DGTL_RESOLVE_RT(__dgtl_rt_log10);
        DGTL_RESOLVE_RT(__dgtl_rt_log1p);
        DGTL_RESOLVE_RT(__dgtl_rt_exp);
        DGTL_RESOLVE_RT(__dgtl_rt_exp2);
        DGTL_RESOLVE_RT(__dgtl_rt_exp10);
        DGTL_RESOLVE_RT(__dgtl_rt_expm1);
        DGTL_RESOLVE_RT(__dgtl_rt_pow);
        DGTL_RESOLVE_RT(__dgtl_rt_sqrt);
        DGTL_RESOLVE_RT(__dgtl_rt_fast_inv_sqrt);
        DGTL_RESOLVE_RT(__dgtl_rt_fma_f32);
        DGTL_RESOLVE_RT(__dgtl_rt_fma_f64);
        DGTL_RESOLVE_RT(__dgtl_rt_cbrt);
        DGTL_RESOLVE_RT(__dgtl_rt_hypot);
        DGTL_RESOLVE_RT(__dgtl_rt_sigmoid);
        DGTL_RESOLVE_RT(__dgtl_rt_silu);
        DGTL_RESOLVE_RT(__dgtl_rt_swiglu);
        DGTL_RESOLVE_RT(__dgtl_rt_gelu);
        DGTL_RESOLVE_RT(__dgtl_rt_geluf);
        DGTL_RESOLVE_RT(__dgtl_rt_rms_norm);
        DGTL_RESOLVE_RT(__dgtl_rt_adamw_step);
        DGTL_RESOLVE_RT(__dgtl_rt_conv1d);
        DGTL_RESOLVE_RT(__dgtl_rt_max_pool2d);
        DGTL_RESOLVE_RT(__dgtl_rt_linear_forward);
        DGTL_RESOLVE_RT(__dgtl_rt_flash_attention_2_block);
        DGTL_RESOLVE_RT(__dgtl_rt_f32_to_bf16);
        DGTL_RESOLVE_RT(__dgtl_rt_bf16_to_f32);
        DGTL_RESOLVE_RT(__dgtl_rt_f32_to_fp8_e4m3);
        DGTL_RESOLVE_RT(__dgtl_rt_fp8_e4m3_to_f32);
        DGTL_RESOLVE_RT(__dgtl_rt_erf);
        DGTL_RESOLVE_RT(__dgtl_rt_erfc);
        DGTL_RESOLVE_RT(__dgtl_rt_erfinv);
        DGTL_RESOLVE_RT(__dgtl_rt_gamma);
        DGTL_RESOLVE_RT(__dgtl_rt_log_gamma);
        DGTL_RESOLVE_RT(__dgtl_rt_lgamma);
        DGTL_RESOLVE_RT(__dgtl_rt_digamma);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_j0);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_j1);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_y0);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_y1);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_i0);
        DGTL_RESOLVE_RT(__dgtl_rt_bessel_i1);
        DGTL_RESOLVE_RT(__dgtl_rt_beta);
        DGTL_RESOLVE_RT(__dgtl_rt_sinc);
        DGTL_RESOLVE_RT(__dgtl_rt_regularized_gamma_p);
        DGTL_RESOLVE_RT(__dgtl_rt_normal_pdf);
        DGTL_RESOLVE_RT(__dgtl_rt_normal_cdf);
        DGTL_RESOLVE_RT(__dgtl_rt_normal_quantile);
        DGTL_RESOLVE_RT(__dgtl_rt_qr_decompose);
        DGTL_RESOLVE_RT(__dgtl_rt_fft2d);
        DGTL_RESOLVE_RT(__dgtl_rt_is_prime);
        DGTL_RESOLVE_RT(__dgtl_rt_gcd);
        DGTL_RESOLVE_RT(__dgtl_rt_mod_inverse);
        DGTL_RESOLVE_RT(__dgtl_rt_mod_pow);
        DGTL_RESOLVE_RT(__dgtl_rt_fast_inv_sqrt_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_vec3_cross);
        DGTL_RESOLVE_RT(__dgtl_rt_vec4_dot);
        DGTL_RESOLVE_RT(__dgtl_rt_mat4_mul);
        DGTL_RESOLVE_RT(__dgtl_rt_quat_slerp);
        DGTL_RESOLVE_RT(__dgtl_rt_ray_aabb_isect);
        DGTL_RESOLVE_RT(__dgtl_rt_exact_gelu);
        DGTL_RESOLVE_RT(__dgtl_rt_quick_gelu);
        DGTL_RESOLVE_RT(__dgtl_rt_mish);
        DGTL_RESOLVE_RT(__dgtl_rt_floor);
        DGTL_RESOLVE_RT(__dgtl_rt_ceil);
        DGTL_RESOLVE_RT(__dgtl_rt_round);
        DGTL_RESOLVE_RT(__dgtl_rt_trunc);
        DGTL_RESOLVE_RT(__dgtl_rt_abs);
        DGTL_RESOLVE_RT(__dgtl_rt_fmod);
        DGTL_RESOLVE_RT(__dgtl_rt_sinf);
        DGTL_RESOLVE_RT(__dgtl_rt_cosf);
        DGTL_RESOLVE_RT(__dgtl_rt_tanf);
        DGTL_RESOLVE_RT(__dgtl_rt_logf);
        DGTL_RESOLVE_RT(__dgtl_rt_expf);
        DGTL_RESOLVE_RT(__dgtl_rt_powf);
        DGTL_RESOLVE_RT(__dgtl_rt_sqrtf);
        DGTL_RESOLVE_RT(__dgtl_rt_relu);
        DGTL_RESOLVE_RT(__dgtl_rt_reluf);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_to_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_bits_to_f64);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_add_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_sub_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_mul_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_div_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_cbrt_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_relu_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_i64_to_f64_bits);
        DGTL_RESOLVE_RT(__dgtl_rt_f64_to_i64);
        DGTL_RESOLVE_RT(__dgtl_rt_i64_pow);
        #undef DGTL_RESOLVE_RT
        return false;
    }

    void register_runtime_math_intrinsics() {
        #define DGTL_REG_RT(sym) set(#sym, reinterpret_cast<uint64_t>(&sym))
        DGTL_REG_RT(__dgtl_rt_sin);
        DGTL_REG_RT(__dgtl_rt_cos);
        DGTL_REG_RT(__dgtl_rt_tan);
        DGTL_REG_RT(__dgtl_rt_asin);
        DGTL_REG_RT(__dgtl_rt_acos);
        DGTL_REG_RT(__dgtl_rt_atan);
        DGTL_REG_RT(__dgtl_rt_atan2);
        DGTL_REG_RT(__dgtl_rt_sinh);
        DGTL_REG_RT(__dgtl_rt_cosh);
        DGTL_REG_RT(__dgtl_rt_tanh);
        DGTL_REG_RT(__dgtl_rt_asinh);
        DGTL_REG_RT(__dgtl_rt_acosh);
        DGTL_REG_RT(__dgtl_rt_atanh);
        DGTL_REG_RT(__dgtl_rt_log);
        DGTL_REG_RT(__dgtl_rt_log2);
        DGTL_REG_RT(__dgtl_rt_log10);
        DGTL_REG_RT(__dgtl_rt_log1p);
        DGTL_REG_RT(__dgtl_rt_exp);
        DGTL_REG_RT(__dgtl_rt_exp2);
        DGTL_REG_RT(__dgtl_rt_exp10);
        DGTL_REG_RT(__dgtl_rt_expm1);
        DGTL_REG_RT(__dgtl_rt_pow);
        DGTL_REG_RT(__dgtl_rt_sqrt);
        DGTL_REG_RT(__dgtl_rt_fast_inv_sqrt);
        DGTL_REG_RT(__dgtl_rt_fma_f32);
        DGTL_REG_RT(__dgtl_rt_fma_f64);
        DGTL_REG_RT(__dgtl_rt_cbrt);
        DGTL_REG_RT(__dgtl_rt_hypot);
        DGTL_REG_RT(__dgtl_rt_sigmoid);
        DGTL_REG_RT(__dgtl_rt_silu);
        DGTL_REG_RT(__dgtl_rt_swiglu);
        DGTL_REG_RT(__dgtl_rt_gelu);
        DGTL_REG_RT(__dgtl_rt_geluf);
        DGTL_REG_RT(__dgtl_rt_rms_norm);
        DGTL_REG_RT(__dgtl_rt_adamw_step);
        DGTL_REG_RT(__dgtl_rt_conv1d);
        DGTL_REG_RT(__dgtl_rt_max_pool2d);
        DGTL_REG_RT(__dgtl_rt_linear_forward);
        DGTL_REG_RT(__dgtl_rt_flash_attention_2_block);
        DGTL_REG_RT(__dgtl_rt_f32_to_bf16);
        DGTL_REG_RT(__dgtl_rt_bf16_to_f32);
        DGTL_REG_RT(__dgtl_rt_f32_to_fp8_e4m3);
        DGTL_REG_RT(__dgtl_rt_fp8_e4m3_to_f32);
        DGTL_REG_RT(__dgtl_rt_erf);
        DGTL_REG_RT(__dgtl_rt_erfc);
        DGTL_REG_RT(__dgtl_rt_erfinv);
        DGTL_REG_RT(__dgtl_rt_gamma);
        DGTL_REG_RT(__dgtl_rt_log_gamma);
        DGTL_REG_RT(__dgtl_rt_lgamma);
        DGTL_REG_RT(__dgtl_rt_digamma);
        DGTL_REG_RT(__dgtl_rt_bessel_j0);
        DGTL_REG_RT(__dgtl_rt_bessel_j1);
        DGTL_REG_RT(__dgtl_rt_bessel_y0);
        DGTL_REG_RT(__dgtl_rt_bessel_y1);
        DGTL_REG_RT(__dgtl_rt_bessel_i0);
        DGTL_REG_RT(__dgtl_rt_bessel_i1);
        DGTL_REG_RT(__dgtl_rt_beta);
        DGTL_REG_RT(__dgtl_rt_sinc);
        DGTL_REG_RT(__dgtl_rt_regularized_gamma_p);
        DGTL_REG_RT(__dgtl_rt_normal_pdf);
        DGTL_REG_RT(__dgtl_rt_normal_cdf);
        DGTL_REG_RT(__dgtl_rt_normal_quantile);
        DGTL_REG_RT(__dgtl_rt_qr_decompose);
        DGTL_REG_RT(__dgtl_rt_fft2d);
        DGTL_REG_RT(__dgtl_rt_is_prime);
        DGTL_REG_RT(__dgtl_rt_gcd);
        DGTL_REG_RT(__dgtl_rt_mod_inverse);
        DGTL_REG_RT(__dgtl_rt_mod_pow);
        DGTL_REG_RT(__dgtl_rt_fast_inv_sqrt_bits);
        DGTL_REG_RT(__dgtl_rt_vec3_cross);
        DGTL_REG_RT(__dgtl_rt_vec4_dot);
        DGTL_REG_RT(__dgtl_rt_mat4_mul);
        DGTL_REG_RT(__dgtl_rt_quat_slerp);
        DGTL_REG_RT(__dgtl_rt_ray_aabb_isect);
        DGTL_REG_RT(__dgtl_rt_exact_gelu);
        DGTL_REG_RT(__dgtl_rt_quick_gelu);
        DGTL_REG_RT(__dgtl_rt_mish);
        DGTL_REG_RT(__dgtl_rt_floor);
        DGTL_REG_RT(__dgtl_rt_ceil);
        DGTL_REG_RT(__dgtl_rt_round);
        DGTL_REG_RT(__dgtl_rt_trunc);
        DGTL_REG_RT(__dgtl_rt_abs);
        DGTL_REG_RT(__dgtl_rt_fmod);
        DGTL_REG_RT(__dgtl_rt_sinf);
        DGTL_REG_RT(__dgtl_rt_cosf);
        DGTL_REG_RT(__dgtl_rt_tanf);
        DGTL_REG_RT(__dgtl_rt_logf);
        DGTL_REG_RT(__dgtl_rt_expf);
        DGTL_REG_RT(__dgtl_rt_powf);
        DGTL_REG_RT(__dgtl_rt_sqrtf);
        DGTL_REG_RT(__dgtl_rt_relu);
        DGTL_REG_RT(__dgtl_rt_reluf);
        DGTL_REG_RT(__dgtl_rt_f64_to_bits);
        DGTL_REG_RT(__dgtl_rt_bits_to_f64);
        DGTL_REG_RT(__dgtl_rt_f64_add_bits);
        DGTL_REG_RT(__dgtl_rt_f64_sub_bits);
        DGTL_REG_RT(__dgtl_rt_f64_mul_bits);
        DGTL_REG_RT(__dgtl_rt_f64_div_bits);
        DGTL_REG_RT(__dgtl_rt_f64_cbrt_bits);
        DGTL_REG_RT(__dgtl_rt_f64_relu_bits);
        DGTL_REG_RT(__dgtl_rt_i64_to_f64_bits);
        DGTL_REG_RT(__dgtl_rt_f64_to_i64);
        DGTL_REG_RT(__dgtl_rt_i64_pow);
        #undef DGTL_REG_RT
    }

    void import_machine_function(const machine::MachineFunction& fn, uint64_t address) {
        set(std::string(fn.name()), address);
    }

    void import_symbol(const dgtl::object::Symbol& sym, uint64_t section_base = 0) {
        if (!sym.name.empty() && sym.is_defined()) {
            set(sym.name, section_base + sym.value);
        }
    }

    [[nodiscard]] size_t size() const noexcept { return m_.size(); }
    [[nodiscard]] const std::unordered_map<std::string, uint64_t>& all() const noexcept { return m_; }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
}


