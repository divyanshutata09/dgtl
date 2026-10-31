/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Complex Numbers, Complex Transcendentals & Dual Number Autodiff
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <concepts>
#include <numbers>
#include <algorithm>

export module dgtl.math.complex;

export namespace dgtl::math::complex {

// ============================================================================
// 1. COMPLEX NUMBER ARITHMETIC TEMPLATE
// ============================================================================

template <std::floating_point T = double>
struct Complex {
    T real = T(0);
    T imag = T(0);

    constexpr Complex() noexcept = default;
    constexpr Complex(T r, T i = T(0)) noexcept : real(r), imag(i) {}

    [[nodiscard]] static Complex from_polar(T r, T theta) noexcept {
        return Complex(r * std::cos(theta), r * std::sin(theta));
    }

    [[nodiscard]] constexpr T norm_sq() const noexcept {
        return real * real + imag * imag;
    }

    [[nodiscard]] T magnitude() const noexcept {
        return std::hypot(real, imag);
    }

    [[nodiscard]] T phase() const noexcept {
        return std::atan2(imag, real);
    }

    [[nodiscard]] constexpr Complex conjugate() const noexcept {
        return Complex(real, -imag);
    }

    constexpr Complex operator+(const Complex& o) const noexcept {
        return Complex(real + o.real, imag + o.imag);
    }

    constexpr Complex operator-(const Complex& o) const noexcept {
        return Complex(real - o.real, imag - o.imag);
    }

    constexpr Complex operator*(const Complex& o) const noexcept {
        return Complex(real * o.real - imag * o.imag, real * o.imag + imag * o.real);
    }

    constexpr Complex operator*(T s) const noexcept {
        return Complex(real * s, imag * s);
    }

    /**
     * @brief Robust Smith's complex division avoiding unnecessary intermediate overflow.
     */
    constexpr Complex operator/(const Complex& o) const noexcept {
        if (std::fabs(o.real) >= std::fabs(o.imag)) {
            const T r = o.imag / o.real;
            const T den = o.real + r * o.imag;
            return Complex((real + imag * r) / den, (imag - real * r) / den);
        } else {
            const T r = o.real / o.imag;
            const T den = o.imag + r * o.real;
            return Complex((real * r + imag) / den, (imag * r - real) / den);
        }
    }

    constexpr Complex operator/(T s) const noexcept {
        return Complex(real / s, imag / s);
    }

    constexpr Complex operator-() const noexcept {
        return Complex(-real, -imag);
    }

    constexpr bool operator==(const Complex& o) const noexcept = default;
};

// ============================================================================
// 2. COMPLEX TRANSCENDENTAL FUNCTIONS
// ============================================================================

template <std::floating_point T>
[[nodiscard]] inline Complex<T> exp(const Complex<T>& z) noexcept {
    const T exp_r = std::exp(z.real);
    return Complex<T>(exp_r * std::cos(z.imag), exp_r * std::sin(z.imag));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> log(const Complex<T>& z) noexcept {
    return Complex<T>(std::log(z.magnitude()), z.phase());
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> sqrt(const Complex<T>& z) noexcept {
    const T mag = z.magnitude();
    const T half_angle = 0.5 * z.phase();
    const T r = std::sqrt(mag);
    return Complex<T>(r * std::cos(half_angle), r * std::sin(half_angle));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> pow(const Complex<T>& base, const Complex<T>& exponent) noexcept {
    if (base.real == T(0) && base.imag == T(0)) {
        return (exponent.real == T(0) && exponent.imag == T(0)) ? Complex<T>(1, 0) : Complex<T>(0, 0);
    }
    return exp(exponent * log(base));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> sin(const Complex<T>& z) noexcept {
    // sin(x + iy) = sin(x)cosh(y) + i cos(x)sinh(y)
    return Complex<T>(std::sin(z.real) * std::cosh(z.imag),
                      std::cos(z.real) * std::sinh(z.imag));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> cos(const Complex<T>& z) noexcept {
    // cos(x + iy) = cos(x)cosh(y) - i sin(x)sinh(y)
    return Complex<T>(std::cos(z.real) * std::cosh(z.imag),
                      -std::sin(z.real) * std::sinh(z.imag));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> tan(const Complex<T>& z) noexcept {
    return sin(z) / cos(z);
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> sinh(const Complex<T>& z) noexcept {
    // sinh(x + iy) = sinh(x)cos(y) + i cosh(x)sin(y)
    return Complex<T>(std::sinh(z.real) * std::cos(z.imag),
                      std::cosh(z.real) * std::sin(z.imag));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> cosh(const Complex<T>& z) noexcept {
    // cosh(x + iy) = cosh(x)cos(y) + i sinh(x)sin(y)
    return Complex<T>(std::cosh(z.real) * std::cos(z.imag),
                      std::sinh(z.real) * std::sin(z.imag));
}

template <std::floating_point T>
[[nodiscard]] inline Complex<T> tanh(const Complex<T>& z) noexcept {
    return sinh(z) / cosh(z);
}

// ============================================================================
// 3. DUAL NUMBERS (FORWARD-MODE AUTOMATIC DIFFERENTIATION)
// Dual number z = val + eps * der, where eps^2 = 0
// Computes exact derivatives with ZERO truncation or discretization error.
// ============================================================================

template <std::floating_point T = double>
struct Dual {
    T val = T(0);
    T der = T(0);

    constexpr Dual() noexcept = default;
    constexpr Dual(T v, T d = T(0)) noexcept : val(v), der(d) {}

    constexpr Dual operator+(const Dual& o) const noexcept {
        return Dual(val + o.val, der + o.der);
    }
    constexpr Dual operator-(const Dual& o) const noexcept {
        return Dual(val - o.val, der - o.der);
    }
    constexpr Dual operator*(const Dual& o) const noexcept {
        // Product rule: (u * v)' = u'v + uv'
        return Dual(val * o.val, val * o.der + der * o.val);
    }
    constexpr Dual operator/(const Dual& o) const noexcept {
        // Quotient rule: (u / v)' = (u'v - uv') / v^2
        const T inv_v = T(1) / o.val;
        return Dual(val * inv_v, (der * o.val - val * o.der) * inv_v * inv_v);
    }

    constexpr Dual operator-() const noexcept {
        return Dual(-val, -der);
    }
};

template <std::floating_point T>
[[nodiscard]] inline Dual<T> sin(const Dual<T>& x) noexcept {
    return Dual<T>(std::sin(x.val), x.der * std::cos(x.val));
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> cos(const Dual<T>& x) noexcept {
    return Dual<T>(std::cos(x.val), -x.der * std::sin(x.val));
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> tan(const Dual<T>& x) noexcept {
    const T c = std::cos(x.val);
    return Dual<T>(std::tan(x.val), x.der / (c * c));
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> exp(const Dual<T>& x) noexcept {
    const T e = std::exp(x.val);
    return Dual<T>(e, x.der * e);
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> log(const Dual<T>& x) noexcept {
    return Dual<T>(std::log(x.val), x.der / x.val);
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> sqrt(const Dual<T>& x) noexcept {
    const T s = std::sqrt(x.val);
    return Dual<T>(s, x.der / (T(2) * s));
}

template <std::floating_point T>
[[nodiscard]] inline Dual<T> pow(const Dual<T>& x, T p) noexcept {
    return Dual<T>(std::pow(x.val, p), x.der * p * std::pow(x.val, p - T(1)));
}

} // namespace dgtl::math::complex
