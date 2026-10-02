/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

export module dgtl.runtime.maths;

import dgtl.math_utils;

export extern "C" {

/* ─── TRANSCENDENTAL & MATHEMATICAL INTRINSICS (ZERO-OVERHEAD C-ABI HOOKS) ─── */

inline double __dgtl_rt_sin(double x) noexcept { return dgtl::utils::math::sin(x); }
inline double __dgtl_rt_cos(double x) noexcept { return dgtl::utils::math::cos(x); }
inline double __dgtl_rt_tan(double x) noexcept { return dgtl::utils::math::tan(x); }
inline double __dgtl_rt_asin(double x) noexcept { return dgtl::utils::math::asin(x); }
inline double __dgtl_rt_acos(double x) noexcept { return dgtl::utils::math::acos(x); }
inline double __dgtl_rt_atan(double x) noexcept { return dgtl::utils::math::atan(x); }
inline double __dgtl_rt_atan2(double y, double x) noexcept { return dgtl::utils::math::atan2(y, x); }
inline double __dgtl_rt_sinh(double x) noexcept { return dgtl::utils::math::sinh(x); }
inline double __dgtl_rt_cosh(double x) noexcept { return dgtl::utils::math::cosh(x); }
inline double __dgtl_rt_tanh(double x) noexcept { return dgtl::utils::math::tanh(x); }

inline double __dgtl_rt_log(double x) noexcept { return dgtl::utils::math::log(x); }
inline double __dgtl_rt_log2(double x) noexcept { return dgtl::utils::math::log2(x); }
inline double __dgtl_rt_log10(double x) noexcept { return dgtl::utils::math::log10(x); }
inline double __dgtl_rt_log1p(double x) noexcept { return dgtl::utils::math::log1p(x); }
inline double __dgtl_rt_exp(double x) noexcept { return dgtl::utils::math::exp(x); }
inline double __dgtl_rt_exp2(double x) noexcept { return dgtl::utils::math::exp2(x); }
inline double __dgtl_rt_exp10(double x) noexcept { return dgtl::utils::math::exp10(x); }
inline double __dgtl_rt_expm1(double x) noexcept { return dgtl::utils::math::expm1(x); }
inline double __dgtl_rt_pow(double x, double y) noexcept { return dgtl::utils::math::pow(x, y); }
inline double __dgtl_rt_cbrt(double x) noexcept { return dgtl::utils::math::cbrt(x); }
inline double __dgtl_rt_hypot(double x, double y) noexcept { return dgtl::utils::math::hypot(x, y); }

} // extern "C"
