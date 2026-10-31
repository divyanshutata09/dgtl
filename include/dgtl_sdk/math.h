/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Sovereign Mathematics & GenAI Intrinsic Layer
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_MATH_H
#define DGTL_SDK_MATH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. STANDARD IEEE-754 TRANSCENDENTALS & TRIGONOMETRY
 * ========================================================================== */

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_sin(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_sinf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_cos(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_cosf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_tan(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_tanf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_asin(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_acos(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_atan(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_atan2(double y, double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_sinh(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_cosh(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_tanh(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_asinh(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_acosh(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_atanh(double x) DGTL_NOEXCEPT;

/* ============================================================================
 * 2. EXPONENTIAL, LOGARITHMIC & ARITHMETIC PRIMITIVES
 * ========================================================================== */

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_log(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_logf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_log2(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_log10(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_log1p(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_exp(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_expf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_exp2(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_exp10(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_expm1(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_pow(double x, double y) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_powf(float x, float y) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_sqrt(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_sqrtf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float DGTL_CALL __dgtl_rt_fast_inv_sqrt(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float DGTL_CALL __dgtl_rt_fma_f32(float a, float b, float c) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_fma_f64(double a, double b, double c) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_cbrt(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_hypot(double x, double y) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_floor(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_ceil(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_round(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_trunc(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_abs(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_fmod(double x, double y) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_i64_pow(int64_t base, int64_t exp) DGTL_NOEXCEPT;

/* ============================================================================
 * 3. GENAI & LLM ACTIVATION FUNCTIONS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_relu(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_reluf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_sigmoid(double x) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_silu(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_swiglu(double gate, double up) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_gelu(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float  DGTL_CALL __dgtl_rt_geluf(float x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_exact_gelu(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_quick_gelu(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_mish(double x) DGTL_NOEXCEPT;

DGTL_API void DGTL_CALL __dgtl_rt_adamw_step(
    double* param, double* m, double* v, const double* grad,
    size_t n, double lr, double beta1, double beta2,
    double eps, double weight_decay, uint64_t step) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_conv1d(
    const double* in, size_t in_len,
    const double* kernel, size_t kernel_len,
    double* out, size_t out_len,
    size_t stride, size_t padding) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_max_pool2d(
    const double* in, size_t H, size_t W,
    size_t kernel_h, size_t kernel_w,
    size_t stride_h, size_t stride_w,
    double* out, size_t out_h, size_t out_w) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_linear_forward(
    const double* X, const double* W, const double* b,
    double* Y, size_t M, size_t K, size_t N) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_rms_norm(
    const double* in, const double* gamma, double* out, size_t n, double eps) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_flash_attention_2_block(
    const double* Q, const double* K, const double* V,
    double* O, size_t seq_len, size_t d_k, size_t d_v, double scale) DGTL_NOEXCEPT;

/* ============================================================================
 * 4. QUANTIZATION & FORMAT CONVERSIONS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API uint16_t DGTL_CALL __dgtl_rt_f32_to_bf16(float f) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float DGTL_CALL __dgtl_rt_bf16_to_f32(uint16_t bf) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API uint8_t DGTL_CALL __dgtl_rt_f32_to_fp8_e4m3(float f) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API float DGTL_CALL __dgtl_rt_fp8_e4m3_to_f32(uint8_t fp8) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int8_t DGTL_CALL __dgtl_rt_quantize_int8(double x, double scale) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_dequantize_int8(int8_t q, double scale) DGTL_NOEXCEPT;

/* ============================================================================
 * 5. SPECIAL FUNCTIONS & POLYNOMIALS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_erf(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_erfc(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_erfinv(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_gamma(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_log_gamma(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_digamma(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_j0(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_j1(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_lgamma(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_y0(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_y1(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_i0(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_bessel_i1(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_beta(double a, double b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_sinc(double x) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_regularized_gamma_p(double s, double x) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_qr_decompose(
    const double* A_data, double* Q_out, double* R_out, size_t m, size_t n) DGTL_NOEXCEPT;
DGTL_API void DGTL_CALL __dgtl_rt_fft2d(
    double* real, double* imag, size_t rows, size_t cols, bool invert) DGTL_NOEXCEPT;

/* ============================================================================
 * 6. PROBABILITY DISTRIBUTIONS & STATISTICS
 * ========================================================================== */

DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_normal_pdf(double x, double mu, double sigma) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_normal_cdf(double x, double mu, double sigma) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double DGTL_CALL __dgtl_rt_normal_quantile(double p) DGTL_NOEXCEPT;

/* ============================================================================
 * 7. SCALAR BITCAST DISPATCH (Dual Register x86-64 / Win64 Helpers)
 * ========================================================================== */

DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_sin_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_cos_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_tan_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_asin_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_acos_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_atan_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_atan2_bits(int64_t y_bits, int64_t x_bits) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_sinh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_cosh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_tanh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_asinh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_acosh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_atanh_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_exp_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_log_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_log10_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_log2_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_sqrt_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_cbrt_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_pow_bits(int64_t x_bits, int64_t y_bits) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_floor_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_ceil_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_round_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_trunc_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_abs_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_fmod_bits(int64_t x_bits, int64_t y_bits) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_hypot_bits(int64_t x_bits, int64_t y_bits) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_relu_bits(int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API double  DGTL_CALL __dgtl_rt_bits_to_f64(int64_t bits) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_to_bits(double d) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_add_bits(int64_t lhs, int64_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_sub_bits(int64_t lhs, int64_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_mul_bits(int64_t lhs, int64_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_div_bits(int64_t lhs, int64_t rhs) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_i64_to_f64_bits(int64_t value) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_f64_to_i64(int64_t bits) DGTL_NOEXCEPT;

/* ============================================================================
 * 8. NUMBER THEORY & MODULAR ALGEBRA
 * ========================================================================== */

DGTL_NODISCARD DGTL_API bool DGTL_CALL __dgtl_rt_is_prime(uint64_t n) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_gcd(int64_t a, int64_t b) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_mod_inverse(int64_t a, int64_t m) DGTL_NOEXCEPT;
DGTL_NODISCARD DGTL_API int64_t DGTL_CALL __dgtl_rt_mod_pow(int64_t base, int64_t exp, int64_t mod) DGTL_NOEXCEPT;

#ifdef __cplusplus
}

namespace dgtl::math {

    inline double sin(double x) noexcept { return __dgtl_rt_sin(x); }
    inline float  sin(float x) noexcept  { return __dgtl_rt_sinf(x); }
    inline double cos(double x) noexcept { return __dgtl_rt_cos(x); }
    inline float  cos(float x) noexcept  { return __dgtl_rt_cosf(x); }
    inline double tan(double x) noexcept { return __dgtl_rt_tan(x); }
    inline float  tan(float x) noexcept  { return __dgtl_rt_tanf(x); }
    inline double asin(double x) noexcept { return __dgtl_rt_asin(x); }
    inline double acos(double x) noexcept { return __dgtl_rt_acos(x); }
    inline double atan(double x) noexcept { return __dgtl_rt_atan(x); }
    inline double atan2(double y, double x) noexcept { return __dgtl_rt_atan2(y, x); }
    inline double sinh(double x) noexcept { return __dgtl_rt_sinh(x); }
    inline double cosh(double x) noexcept { return __dgtl_rt_cosh(x); }
    inline double tanh(double x) noexcept { return __dgtl_rt_tanh(x); }
    inline double asinh(double x) noexcept { return __dgtl_rt_asinh(x); }
    inline double acosh(double x) noexcept { return __dgtl_rt_acosh(x); }
    inline double atanh(double x) noexcept { return __dgtl_rt_atanh(x); }

    inline double log(double x) noexcept { return __dgtl_rt_log(x); }
    inline float  log(float x) noexcept  { return __dgtl_rt_logf(x); }
    inline double log2(double x) noexcept { return __dgtl_rt_log2(x); }
    inline double log10(double x) noexcept { return __dgtl_rt_log10(x); }
    inline double log1p(double x) noexcept { return __dgtl_rt_log1p(x); }
    inline double exp(double x) noexcept { return __dgtl_rt_exp(x); }
    inline float  exp(float x) noexcept  { return __dgtl_rt_expf(x); }
    inline double exp2(double x) noexcept { return __dgtl_rt_exp2(x); }
    inline double exp10(double x) noexcept { return __dgtl_rt_exp10(x); }
    inline double expm1(double x) noexcept { return __dgtl_rt_expm1(x); }
    inline double pow(double x, double y) noexcept { return __dgtl_rt_pow(x, y); }
    inline float  pow(float x, float y) noexcept   { return __dgtl_rt_powf(x, y); }
    inline double sqrt(double x) noexcept { return __dgtl_rt_sqrt(x); }
    inline float  sqrt(float x) noexcept  { return __dgtl_rt_sqrtf(x); }
    inline float fast_inv_sqrt(float x) noexcept { return __dgtl_rt_fast_inv_sqrt(x); }
    inline float fma(float a, float b, float c) noexcept { return __dgtl_rt_fma_f32(a, b, c); }
    inline double fma(double a, double b, double c) noexcept { return __dgtl_rt_fma_f64(a, b, c); }
    inline double cbrt(double x) noexcept { return __dgtl_rt_cbrt(x); }
    inline double hypot(double x, double y) noexcept { return __dgtl_rt_hypot(x, y); }
    inline double floor(double x) noexcept { return __dgtl_rt_floor(x); }
    inline double ceil(double x) noexcept { return __dgtl_rt_ceil(x); }
    inline double round(double x) noexcept { return __dgtl_rt_round(x); }
    inline double trunc(double x) noexcept { return __dgtl_rt_trunc(x); }
    inline double abs(double x) noexcept { return __dgtl_rt_abs(x); }
    inline double fmod(double x, double y) noexcept { return __dgtl_rt_fmod(x, y); }
    inline int64_t pow(int64_t base, int64_t exp) noexcept { return __dgtl_rt_i64_pow(base, exp); }

    inline double relu(double x) noexcept { return __dgtl_rt_relu(x); }
    inline float  relu(float x) noexcept  { return __dgtl_rt_reluf(x); }
    inline double sigmoid(double x) noexcept { return __dgtl_rt_sigmoid(x); }
    inline double silu(double x) noexcept { return __dgtl_rt_silu(x); }
    inline double swiglu(double gate, double up) noexcept { return __dgtl_rt_swiglu(gate, up); }
    inline double gelu(double x) noexcept { return __dgtl_rt_gelu(x); }
    inline float  gelu(float x) noexcept  { return __dgtl_rt_geluf(x); }
    inline double exact_gelu(double x) noexcept { return __dgtl_rt_exact_gelu(x); }
    inline double quick_gelu(double x) noexcept { return __dgtl_rt_quick_gelu(x); }
    inline double mish(double x) noexcept { return __dgtl_rt_mish(x); }

    inline void adamw_step(
        double* param, double* m, double* v, const double* grad,
        size_t n, double lr, double beta1 = 0.9, double beta2 = 0.999,
        double eps = 1e-8, double weight_decay = 0.01, uint64_t step = 1) noexcept {
        __dgtl_rt_adamw_step(param, m, v, grad, n, lr, beta1, beta2, eps, weight_decay, step);
    }
    inline void conv1d(
        const double* in, size_t in_len,
        const double* kernel, size_t kernel_len,
        double* out, size_t out_len,
        size_t stride = 1, size_t padding = 0) noexcept {
        __dgtl_rt_conv1d(in, in_len, kernel, kernel_len, out, out_len, stride, padding);
    }
    inline void max_pool2d(
        const double* in, size_t H, size_t W,
        size_t kernel_h, size_t kernel_w,
        size_t stride_h, size_t stride_w,
        double* out, size_t out_h, size_t out_w) noexcept {
        __dgtl_rt_max_pool2d(in, H, W, kernel_h, kernel_w, stride_h, stride_w, out, out_h, out_w);
    }
    inline void linear_forward(
        const double* X, const double* W, const double* b,
        double* Y, size_t M, size_t K, size_t N) noexcept {
        __dgtl_rt_linear_forward(X, W, b, Y, M, K, N);
    }
    inline void rms_norm(
        const double* in, const double* gamma, double* out, size_t n, double eps = 1e-5) noexcept {
        __dgtl_rt_rms_norm(in, gamma, out, n, eps);
    }
    inline void flash_attention_2_block(
        const double* Q, const double* K, const double* V,
        double* O, size_t seq_len, size_t d_k, size_t d_v, double scale) noexcept {
        __dgtl_rt_flash_attention_2_block(Q, K, V, O, seq_len, d_k, d_v, scale);
    }

    inline double erf(double x) noexcept { return __dgtl_rt_erf(x); }
    inline double erfc(double x) noexcept { return __dgtl_rt_erfc(x); }
    inline double erfinv(double x) noexcept { return __dgtl_rt_erfinv(x); }
    inline double gamma(double x) noexcept { return __dgtl_rt_gamma(x); }
    inline double lgamma(double x) noexcept { return __dgtl_rt_lgamma(x); }
    inline double digamma(double x) noexcept { return __dgtl_rt_digamma(x); }
    inline double bessel_j0(double x) noexcept { return __dgtl_rt_bessel_j0(x); }
    inline double bessel_j1(double x) noexcept { return __dgtl_rt_bessel_j1(x); }
    inline double bessel_y0(double x) noexcept { return __dgtl_rt_bessel_y0(x); }
    inline double bessel_y1(double x) noexcept { return __dgtl_rt_bessel_y1(x); }
    inline double bessel_i0(double x) noexcept { return __dgtl_rt_bessel_i0(x); }
    inline double bessel_i1(double x) noexcept { return __dgtl_rt_bessel_i1(x); }
    inline double beta(double a, double b) noexcept { return __dgtl_rt_beta(a, b); }
    inline double sinc(double x) noexcept { return __dgtl_rt_sinc(x); }
    inline double regularized_gamma_p(double s, double x) noexcept { return __dgtl_rt_regularized_gamma_p(s, x); }
    inline void qr_decompose(const double* A_data, double* Q_out, double* R_out, size_t m, size_t n) noexcept {
        __dgtl_rt_qr_decompose(A_data, Q_out, R_out, m, n);
    }
    inline void fft2d(double* real, double* imag, size_t rows, size_t cols, bool invert = false) noexcept {
        __dgtl_rt_fft2d(real, imag, rows, cols, invert);
    }

    inline double normal_pdf(double x, double mu = 0.0, double sigma = 1.0) noexcept { return __dgtl_rt_normal_pdf(x, mu, sigma); }
    inline double normal_cdf(double x, double mu = 0.0, double sigma = 1.0) noexcept { return __dgtl_rt_normal_cdf(x, mu, sigma); }
    inline double normal_quantile(double p) noexcept { return __dgtl_rt_normal_quantile(p); }

    inline uint16_t f32_to_bf16(float f) noexcept { return __dgtl_rt_f32_to_bf16(f); }
    inline float bf16_to_f32(uint16_t bf) noexcept { return __dgtl_rt_bf16_to_f32(bf); }
    inline uint8_t f32_to_fp8(float f) noexcept { return __dgtl_rt_f32_to_fp8_e4m3(f); }
    inline float fp8_to_f32(uint8_t fp8) noexcept { return __dgtl_rt_fp8_e4m3_to_f32(fp8); }
    inline int8_t quantize_int8(double x, double scale) noexcept { return __dgtl_rt_quantize_int8(x, scale); }
    inline double dequantize_int8(int8_t q, double scale) noexcept { return __dgtl_rt_dequantize_int8(q, scale); }

    inline bool is_prime(uint64_t n) noexcept { return __dgtl_rt_is_prime(n); }
    inline int64_t gcd(int64_t a, int64_t b) noexcept { return __dgtl_rt_gcd(a, b); }
    inline int64_t mod_inverse(int64_t a, int64_t m) noexcept { return __dgtl_rt_mod_inverse(a, m); }
    inline int64_t mod_pow(int64_t base, int64_t exp, int64_t mod) noexcept { return __dgtl_rt_mod_pow(base, exp, mod); }

    inline double  bits_to_f64(int64_t bits) noexcept { return __dgtl_rt_bits_to_f64(bits); }
    inline int64_t f64_to_bits(double d) noexcept { return __dgtl_rt_f64_to_bits(d); }
    inline int64_t f64_add_bits(int64_t lhs, int64_t rhs) noexcept { return __dgtl_rt_f64_add_bits(lhs, rhs); }
    inline int64_t f64_sub_bits(int64_t lhs, int64_t rhs) noexcept { return __dgtl_rt_f64_sub_bits(lhs, rhs); }
    inline int64_t f64_mul_bits(int64_t lhs, int64_t rhs) noexcept { return __dgtl_rt_f64_mul_bits(lhs, rhs); }
    inline int64_t f64_div_bits(int64_t lhs, int64_t rhs) noexcept { return __dgtl_rt_f64_div_bits(lhs, rhs); }
    inline int64_t i64_to_f64_bits(int64_t value) noexcept { return __dgtl_rt_i64_to_f64_bits(value); }
    inline int64_t f64_to_i64(int64_t bits) noexcept { return __dgtl_rt_f64_to_i64(bits); }

    inline int64_t f64_sin_bits(int64_t b) noexcept { return __dgtl_rt_f64_sin_bits(b); }
    inline int64_t f64_cos_bits(int64_t b) noexcept { return __dgtl_rt_f64_cos_bits(b); }
    inline int64_t f64_tan_bits(int64_t b) noexcept { return __dgtl_rt_f64_tan_bits(b); }
    inline int64_t f64_asin_bits(int64_t b) noexcept { return __dgtl_rt_f64_asin_bits(b); }
    inline int64_t f64_acos_bits(int64_t b) noexcept { return __dgtl_rt_f64_acos_bits(b); }
    inline int64_t f64_atan_bits(int64_t b) noexcept { return __dgtl_rt_f64_atan_bits(b); }
    inline int64_t f64_atan2_bits(int64_t y_bits, int64_t x_bits) noexcept { return __dgtl_rt_f64_atan2_bits(y_bits, x_bits); }
    inline int64_t f64_sinh_bits(int64_t b) noexcept { return __dgtl_rt_f64_sinh_bits(b); }
    inline int64_t f64_cosh_bits(int64_t b) noexcept { return __dgtl_rt_f64_cosh_bits(b); }
    inline int64_t f64_tanh_bits(int64_t b) noexcept { return __dgtl_rt_f64_tanh_bits(b); }
    inline int64_t f64_asinh_bits(int64_t b) noexcept { return __dgtl_rt_f64_asinh_bits(b); }
    inline int64_t f64_acosh_bits(int64_t b) noexcept { return __dgtl_rt_f64_acosh_bits(b); }
    inline int64_t f64_atanh_bits(int64_t b) noexcept { return __dgtl_rt_f64_atanh_bits(b); }
    inline int64_t f64_exp_bits(int64_t b) noexcept { return __dgtl_rt_f64_exp_bits(b); }
    inline int64_t f64_log_bits(int64_t b) noexcept { return __dgtl_rt_f64_log_bits(b); }
    inline int64_t f64_log10_bits(int64_t b) noexcept { return __dgtl_rt_f64_log10_bits(b); }
    inline int64_t f64_log2_bits(int64_t b) noexcept { return __dgtl_rt_f64_log2_bits(b); }
    inline int64_t f64_sqrt_bits(int64_t b) noexcept { return __dgtl_rt_f64_sqrt_bits(b); }
    inline int64_t f64_cbrt_bits(int64_t b) noexcept { return __dgtl_rt_f64_cbrt_bits(b); }
    inline int64_t f64_pow_bits(int64_t x_bits, int64_t y_bits) noexcept { return __dgtl_rt_f64_pow_bits(x_bits, y_bits); }
    inline int64_t f64_floor_bits(int64_t b) noexcept { return __dgtl_rt_f64_floor_bits(b); }
    inline int64_t f64_ceil_bits(int64_t b) noexcept { return __dgtl_rt_f64_ceil_bits(b); }
    inline int64_t f64_round_bits(int64_t b) noexcept { return __dgtl_rt_f64_round_bits(b); }
    inline int64_t f64_trunc_bits(int64_t b) noexcept { return __dgtl_rt_f64_trunc_bits(b); }
    inline int64_t f64_abs_bits(int64_t b) noexcept { return __dgtl_rt_f64_abs_bits(b); }
    inline int64_t f64_fmod_bits(int64_t x_bits, int64_t y_bits) noexcept { return __dgtl_rt_f64_fmod_bits(x_bits, y_bits); }
    inline int64_t f64_hypot_bits(int64_t x_bits, int64_t y_bits) noexcept { return __dgtl_rt_f64_hypot_bits(x_bits, y_bits); }

} // namespace dgtl::math

namespace dgtl::sdk {
    namespace math = dgtl::math;
}
#endif

#endif /* DGTL_SDK_MATH_H */
