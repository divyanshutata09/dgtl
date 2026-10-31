/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Unified C-ABI Runtime Mathematics & Intrinsic Exports
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <complex>

export module dgtl.runtime.maths;

import dgtl.math_utils;
import dgtl.math.calculus;
import dgtl.math.probability;
import dgtl.math.combinatorics;
import dgtl.math.complex;
import dgtl.math.geometry;
import dgtl.math.special;
import dgtl.math.genai;
import dgtl.math.algebra;

namespace detail {
    inline double bits_to_f64(int64_t bits) noexcept {
        double d = 0.0;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    inline int64_t f64_to_bits(double d) noexcept {
        int64_t bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        return bits;
    }
}

export extern "C" {

/* ─── 1. STANDARD IEEE-754 TRANSCENDENTALS & TRIGONOMETRY ─── */

double __dgtl_rt_sin(double x) noexcept { return dgtl::utils::math::sin(x); }
float  __dgtl_rt_sinf(float x) noexcept  { return std::sin(x); }
double __dgtl_rt_cos(double x) noexcept { return dgtl::utils::math::cos(x); }
float  __dgtl_rt_cosf(float x) noexcept  { return std::cos(x); }
double __dgtl_rt_tan(double x) noexcept { return dgtl::utils::math::tan(x); }
float  __dgtl_rt_tanf(float x) noexcept  { return std::tan(x); }
double __dgtl_rt_asin(double x) noexcept { return dgtl::utils::math::asin(x); }
double __dgtl_rt_acos(double x) noexcept { return dgtl::utils::math::acos(x); }
double __dgtl_rt_atan(double x) noexcept { return dgtl::utils::math::atan(x); }
double __dgtl_rt_atan2(double y, double x) noexcept { return dgtl::utils::math::atan2(y, x); }
double __dgtl_rt_sinh(double x) noexcept { return dgtl::utils::math::sinh(x); }
double __dgtl_rt_cosh(double x) noexcept { return dgtl::utils::math::cosh(x); }
double __dgtl_rt_tanh(double x) noexcept { return dgtl::utils::math::tanh(x); }
double __dgtl_rt_asinh(double x) noexcept { return std::asinh(x); }
double __dgtl_rt_acosh(double x) noexcept { return std::acosh(x); }
double __dgtl_rt_atanh(double x) noexcept { return std::atanh(x); }

/* ─── 2. EXPONENTIAL, LOGARITHMIC & POWER ─── */

double __dgtl_rt_log(double x) noexcept { return dgtl::utils::math::log(x); }
float  __dgtl_rt_logf(float x) noexcept  { return std::log(x); }
double __dgtl_rt_log2(double x) noexcept { return dgtl::utils::math::log2(x); }
double __dgtl_rt_log10(double x) noexcept { return dgtl::utils::math::log10(x); }
double __dgtl_rt_log1p(double x) noexcept { return dgtl::utils::math::log1p(x); }
double __dgtl_rt_exp(double x) noexcept { return dgtl::utils::math::exp(x); }
float  __dgtl_rt_expf(float x) noexcept  { return std::exp(x); }
double __dgtl_rt_exp2(double x) noexcept { return dgtl::utils::math::exp2(x); }
double __dgtl_rt_exp10(double x) noexcept { return dgtl::utils::math::exp10(x); }
double __dgtl_rt_expm1(double x) noexcept { return dgtl::utils::math::expm1(x); }
double __dgtl_rt_pow(double x, double y) noexcept { return dgtl::utils::math::pow(x, y); }
float  __dgtl_rt_powf(float x, float y) noexcept   { return std::pow(x, y); }
double __dgtl_rt_sqrt(double x) noexcept { return dgtl::utils::math::sqrt(x); }
float  __dgtl_rt_sqrtf(float x) noexcept  { return std::sqrt(x); }
float __dgtl_rt_fast_inv_sqrt(float x) noexcept { return dgtl::utils::math::fast_inv_sqrt(x); }
float __dgtl_rt_fma_f32(float a, float b, float c) noexcept { return std::fma(a, b, c); }
double __dgtl_rt_fma_f64(double a, double b, double c) noexcept { return std::fma(a, b, c); }
double __dgtl_rt_cbrt(double x) noexcept { return dgtl::utils::math::cbrt(x); }
double __dgtl_rt_hypot(double x, double y) noexcept { return dgtl::utils::math::hypot(x, y); }
double __dgtl_rt_floor(double x) noexcept { return std::floor(x); }
double __dgtl_rt_ceil(double x) noexcept { return std::ceil(x); }
double __dgtl_rt_round(double x) noexcept { return std::round(x); }
double __dgtl_rt_trunc(double x) noexcept { return std::trunc(x); }
double __dgtl_rt_abs(double x) noexcept { return std::fabs(x); }
double __dgtl_rt_fmod(double x, double y) noexcept { return std::fmod(x, y); }

/* ─── 3. GENAI / TRANSFORMER ACTIVATION FUNCTIONS ─── */

double __dgtl_rt_relu(double x) noexcept { return (x > 0.0) ? x : 0.0; }
float  __dgtl_rt_reluf(float x) noexcept { return (x > 0.0f) ? x : 0.0f; }
double __dgtl_rt_sigmoid(double x) noexcept { return dgtl::math::genai::sigmoid(x); }
double __dgtl_rt_silu(double x) noexcept { return dgtl::math::genai::silu(x); }
double __dgtl_rt_swiglu(double gate, double up) noexcept { return dgtl::math::genai::swiglu(gate, up); }
double __dgtl_rt_gelu(double x) noexcept { return dgtl::math::genai::fast_gelu(x); }
float  __dgtl_rt_geluf(float x) noexcept { return static_cast<float>(dgtl::math::genai::fast_gelu(x)); }
double __dgtl_rt_exact_gelu(double x) noexcept { return dgtl::math::genai::exact_gelu(x); }
double __dgtl_rt_quick_gelu(double x) noexcept { return dgtl::math::genai::quick_gelu(x); }
double __dgtl_rt_mish(double x) noexcept { return dgtl::math::genai::mish(x); }

void __dgtl_rt_adamw_step(
    double* param, double* m, double* v, const double* grad,
    size_t n, double lr, double beta1, double beta2,
    double eps, double weight_decay, uint64_t step) noexcept {
    dgtl::math::genai::adamw_step(param, m, v, grad, n, lr, beta1, beta2, eps, weight_decay, step);
}

void __dgtl_rt_conv1d(
    const double* in, size_t in_len,
    const double* kernel, size_t kernel_len,
    double* out, size_t out_len,
    size_t stride, size_t padding) noexcept {
    dgtl::math::genai::conv1d(in, in_len, kernel, kernel_len, out, out_len, stride, padding);
}

void __dgtl_rt_max_pool2d(
    const double* in, size_t H, size_t W,
    size_t kernel_h, size_t kernel_w,
    size_t stride_h, size_t stride_w,
    double* out, size_t out_h, size_t out_w) noexcept {
    dgtl::math::genai::max_pool2d(in, H, W, kernel_h, kernel_w, stride_h, stride_w, out, out_h, out_w);
}

void __dgtl_rt_linear_forward(
    const double* X, const double* W, const double* b,
    double* Y, size_t M, size_t K, size_t N) noexcept {
    dgtl::math::genai::linear_forward(X, W, b, Y, M, K, N);
}

void __dgtl_rt_rms_norm(
    const double* in, const double* gamma, double* out, size_t n, double eps) noexcept {
    dgtl::math::genai::rms_norm(in, gamma, out, n, eps);
}

void __dgtl_rt_flash_attention_2_block(
    const double* Q, const double* K, const double* V,
    double* O, size_t seq_len, size_t d_k, size_t d_v, double scale) noexcept {
    for (size_t i = 0; i < seq_len; ++i) {
        dgtl::math::genai::OnlineSoftmaxState st;
        std::vector<double> scores(seq_len);
        for (size_t j = 0; j < seq_len; ++j) {
            double dot = 0.0;
            for (size_t d = 0; d < d_k; ++d) {
                dot += Q[i * d_k + d] * K[j * d_k + d];
            }
            dot *= scale;
            scores[j] = dot;
            st.update(dot);
        }
        for (size_t dv = 0; dv < d_v; ++dv) {
            double sum_out = 0.0;
            for (size_t j = 0; j < seq_len; ++j) {
                double p = (st.sum_exp > 0.0) ? (std::exp(scores[j] - st.max_val) / st.sum_exp) : 0.0;
                sum_out += p * V[j * d_v + dv];
            }
            O[i * d_v + dv] = sum_out;
        }
    }
}

/* ─── 4. QUANTIZATION & FORMAT CONVERSIONS ─── */

uint16_t __dgtl_rt_f32_to_bf16(float f) noexcept { return dgtl::math::genai::f32_to_bf16(f); }
float __dgtl_rt_bf16_to_f32(uint16_t bf) noexcept { return dgtl::math::genai::bf16_to_f32(bf); }
uint8_t __dgtl_rt_f32_to_fp8_e4m3(float f) noexcept { return dgtl::math::genai::f32_to_fp8_e4m3(f); }
float __dgtl_rt_fp8_e4m3_to_f32(uint8_t fp8) noexcept { return dgtl::math::genai::fp8_e4m3_to_f32(fp8); }
int8_t __dgtl_rt_quantize_int8(double x, double scale) noexcept { return dgtl::math::genai::quantize_int8(x, scale); }
double __dgtl_rt_dequantize_int8(int8_t q, double scale) noexcept { return dgtl::math::genai::dequantize_int8(q, scale); }

/* ─── 5. SPECIAL FUNCTIONS (erf, Gamma, Bessel) ─── */

double __dgtl_rt_erf(double x) noexcept { return dgtl::math::special::erf(x); }
double __dgtl_rt_erfc(double x) noexcept { return dgtl::math::special::erfc(x); }
double __dgtl_rt_erfinv(double x) noexcept { return dgtl::math::special::erfinv(x); }
double __dgtl_rt_gamma(double x) noexcept { return dgtl::math::special::gamma(x); }
double __dgtl_rt_log_gamma(double x) noexcept { return dgtl::math::special::log_gamma(x); }
double __dgtl_rt_lgamma(double x) noexcept { return dgtl::math::special::lgamma(x); }
double __dgtl_rt_digamma(double x) noexcept { return dgtl::math::special::digamma(x); }
double __dgtl_rt_bessel_j0(double x) noexcept { return dgtl::math::special::bessel_j0(x); }
double __dgtl_rt_bessel_j1(double x) noexcept { return dgtl::math::special::bessel_j1(x); }
double __dgtl_rt_bessel_y0(double x) noexcept { return dgtl::math::special::bessel_y0(x); }
double __dgtl_rt_bessel_y1(double x) noexcept { return dgtl::math::special::bessel_y1(x); }
double __dgtl_rt_bessel_i0(double x) noexcept { return dgtl::math::special::bessel_i0(x); }
double __dgtl_rt_bessel_i1(double x) noexcept { return dgtl::math::special::bessel_i1(x); }
double __dgtl_rt_beta(double a, double b) noexcept { return dgtl::math::special::beta(a, b); }
double __dgtl_rt_sinc(double x) noexcept { return dgtl::math::special::sinc(x); }
double __dgtl_rt_regularized_gamma_p(double s, double x) noexcept { return dgtl::math::special::regularized_gamma_p(s, x); }

void __dgtl_rt_qr_decompose(
    const double* A_data, double* Q_out, double* R_out, size_t m, size_t n) noexcept {
    if (!A_data || !Q_out || !R_out || m == 0 || n == 0) return;
    dgtl::math::algebra::Matrix A(m, n);
    std::memcpy(A.data(), A_data, m * n * sizeof(double));
    auto res = dgtl::math::algebra::qr_decompose(A);
    std::memcpy(Q_out, res.Q.data(), m * m * sizeof(double));
    std::memcpy(R_out, res.R.data(), m * n * sizeof(double));
}

void __dgtl_rt_fft2d(
    double* real, double* imag, size_t rows, size_t cols, bool invert) noexcept {
    if (!real || !imag || rows == 0 || cols == 0) return;
    std::vector<std::complex<double>> data(rows * cols);
    for (size_t i = 0; i < rows * cols; ++i) {
        data[i] = std::complex<double>(real[i], imag[i]);
    }
    dgtl::math::special::fft2d(data, rows, cols, invert);
    for (size_t i = 0; i < rows * cols; ++i) {
        real[i] = data[i].real();
        imag[i] = data[i].imag();
    }
}

/* ─── 6. PROBABILITY DISTRIBUTIONS ─── */

double __dgtl_rt_normal_pdf(double x, double mu, double sigma) noexcept {
    return dgtl::math::probability::normal_pdf(x, mu, sigma);
}
double __dgtl_rt_normal_cdf(double x, double mu, double sigma) noexcept {
    return dgtl::math::probability::normal_cdf(x, mu, sigma);
}
double __dgtl_rt_normal_quantile(double p) noexcept {
    return dgtl::math::probability::normal_quantile(p);
}

/* ─── 7. SOVEREIGN DGTL RUNTIME SCALAR BITCAST EXPORTS ─── */

int64_t __dgtl_rt_f64_sin_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_sin(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_cos_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_cos(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_tan_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_tan(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_asin_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_asin(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_acos_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_acos(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_atan_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_atan(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_atan2_bits(int64_t y_bits, int64_t x_bits) noexcept {
    return detail::f64_to_bits(__dgtl_rt_atan2(detail::bits_to_f64(y_bits), detail::bits_to_f64(x_bits)));
}
int64_t __dgtl_rt_f64_sinh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_sinh(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_cosh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_cosh(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_tanh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_tanh(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_asinh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_asinh(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_acosh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_acosh(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_atanh_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_atanh(detail::bits_to_f64(b))); }

int64_t __dgtl_rt_f64_exp_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_exp(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_log_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_log(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_log10_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_log10(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_log2_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_log2(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_sqrt_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_sqrt(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_cbrt_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_cbrt(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_pow_bits(int64_t x_bits, int64_t y_bits) noexcept {
    return detail::f64_to_bits(__dgtl_rt_pow(detail::bits_to_f64(x_bits), detail::bits_to_f64(y_bits)));
}
int64_t __dgtl_rt_f64_floor_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_floor(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_ceil_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_ceil(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_round_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_round(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_trunc_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_trunc(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_abs_bits(int64_t b) noexcept { return detail::f64_to_bits(__dgtl_rt_abs(detail::bits_to_f64(b))); }
int64_t __dgtl_rt_f64_fmod_bits(int64_t x_bits, int64_t y_bits) noexcept {
    return detail::f64_to_bits(__dgtl_rt_fmod(detail::bits_to_f64(x_bits), detail::bits_to_f64(y_bits)));
}
int64_t __dgtl_rt_f64_hypot_bits(int64_t x_bits, int64_t y_bits) noexcept {
    return detail::f64_to_bits(__dgtl_rt_hypot(detail::bits_to_f64(x_bits), detail::bits_to_f64(y_bits)));
}
int64_t __dgtl_rt_f64_relu_bits(int64_t b) noexcept {
    return detail::f64_to_bits(__dgtl_rt_relu(detail::bits_to_f64(b)));
}


double __dgtl_rt_bits_to_f64(int64_t bits) noexcept {
    return detail::bits_to_f64(bits);
}

int64_t __dgtl_rt_f64_to_bits(double d) noexcept {
    return detail::f64_to_bits(d);
}

int64_t __dgtl_rt_f64_add_bits(int64_t lhs, int64_t rhs) noexcept {
    return detail::f64_to_bits(detail::bits_to_f64(lhs) + detail::bits_to_f64(rhs));
}

int64_t __dgtl_rt_f64_sub_bits(int64_t lhs, int64_t rhs) noexcept {
    return detail::f64_to_bits(detail::bits_to_f64(lhs) - detail::bits_to_f64(rhs));
}

int64_t __dgtl_rt_f64_mul_bits(int64_t lhs, int64_t rhs) noexcept {
    return detail::f64_to_bits(detail::bits_to_f64(lhs) * detail::bits_to_f64(rhs));
}

int64_t __dgtl_rt_f64_div_bits(int64_t lhs, int64_t rhs) noexcept {
    double r = detail::bits_to_f64(rhs);
    return detail::f64_to_bits(r == 0.0 ? 0.0 : (detail::bits_to_f64(lhs) / r));
}

int64_t __dgtl_rt_i64_to_f64_bits(int64_t value) noexcept {
    return detail::f64_to_bits(static_cast<double>(value));
}

int64_t __dgtl_rt_f64_to_i64(int64_t bits) noexcept {
    double d = detail::bits_to_f64(bits);
    return (std::isnan(d) || std::isinf(d)) ? 0 : static_cast<int64_t>(d);
}

int64_t __dgtl_rt_i64_pow(int64_t base, int64_t exp) noexcept {
    if (exp < 0) return 0;
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

/* ─── 8. NUMBER THEORY & MODULAR ALGEBRA ─── */

bool __dgtl_rt_is_prime(uint64_t n) noexcept {
    return dgtl::math::algebra::is_prime_miller_rabin(n);
}

int64_t __dgtl_rt_gcd(int64_t a, int64_t b) noexcept {
    int64_t x = 0, y = 0;
    return dgtl::math::algebra::extended_gcd(a, b, x, y);
}

int64_t __dgtl_rt_mod_inverse(int64_t a, int64_t m) noexcept {
    return dgtl::math::algebra::mod_inverse(a, m);
}

int64_t __dgtl_rt_mod_pow(int64_t base, int64_t exp, int64_t mod) noexcept {
    return dgtl::math::algebra::mod_pow(base, exp, mod);
}

} // extern "C"


