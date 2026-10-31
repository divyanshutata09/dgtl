/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Calculus, Numerical Quadrature, Differentiation & ODE Solvers
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>
#include <algorithm>
#include <array>

export module dgtl.math.calculus;

export namespace dgtl::math::calculus {

// ============================================================================
// 1. NUMERICAL DIFFERENTIATION
// ============================================================================

/**
 * @brief Forward finite difference: f'(x) ~ (f(x + h) - f(x)) / h
 */
template <typename F>
[[nodiscard]] inline double diff_forward(F&& f, double x, double h = 1e-7) noexcept {
    return (f(x + h) - f(x)) / h;
}

/**
 * @brief Backward finite difference: f'(x) ~ (f(x) - f(x - h)) / h
 */
template <typename F>
[[nodiscard]] inline double diff_backward(F&& f, double x, double h = 1e-7) noexcept {
    return (f(x) - f(x - h)) / h;
}

/**
 * @brief Central finite difference (3-point, O(h^2)): f'(x) ~ (f(x + h) - f(x - h)) / (2h)
 */
template <typename F>
[[nodiscard]] inline double diff_central(F&& f, double x, double h = 1e-5) noexcept {
    return (f(x + h) - f(x - h)) / (2.0 * h);
}

/**
 * @brief 5-point central finite difference (O(h^4)):
 * f'(x) ~ (-f(x + 2h) + 8f(x + h) - 8f(x - h) + f(x - 2h)) / (12h)
 */
template <typename F>
[[nodiscard]] inline double diff_central_5pt(F&& f, double x, double h = 1e-3) noexcept {
    const double f_p2 = f(x + 2.0 * h);
    const double f_p1 = f(x + h);
    const double f_m1 = f(x - h);
    const double f_m2 = f(x - 2.0 * h);
    return (-f_p2 + 8.0 * f_p1 - 8.0 * f_m1 + f_m2) / (12.0 * h);
}

/**
 * @brief Richardson Extrapolation for numerical differentiation (O(h^6) accuracy).
 */
template <typename F>
[[nodiscard]] inline double diff_richardson(F&& f, double x, double h = 1e-2) noexcept {
    const double d1 = diff_central(f, x, h);
    const double d2 = diff_central(f, x, h / 2.0);
    const double d4 = diff_central(f, x, h / 4.0);

    const double r21 = (4.0 * d2 - d1) / 3.0;
    const double r22 = (4.0 * d4 - d2) / 3.0;

    return (16.0 * r22 - r21) / 15.0;
}

/**
 * @brief Second-order derivative f''(x) via central differences (O(h^2)):
 * f''(x) ~ (f(x + h) - 2f(x) + f(x - h)) / h^2
 */
template <typename F>
[[nodiscard]] inline double diff2_central(F&& f, double x, double h = 1e-4) noexcept {
    return (f(x + h) - 2.0 * f(x) + f(x - h)) / (h * h);
}

/**
 * @brief Gradient vector of a scalar function f: R^N -> R
 */
template <typename F>
[[nodiscard]] inline std::vector<double> gradient(F&& f, const std::vector<double>& x, double h = 1e-5) {
    const std::size_t n = x.size();
    std::vector<double> grad(n, 0.0);
    std::vector<double> x_perturbed = x;

    for (std::size_t i = 0; i < n; ++i) {
        const double orig = x[i];
        x_perturbed[i] = orig + h;
        const double f_plus = f(x_perturbed);
        x_perturbed[i] = orig - h;
        const double f_minus = f(x_perturbed);
        x_perturbed[i] = orig;

        grad[i] = (f_plus - f_minus) / (2.0 * h);
    }
    return grad;
}

/**
 * @brief Hessian matrix of a scalar function f: R^N -> R
 */
template <typename F>
[[nodiscard]] inline std::vector<std::vector<double>> hessian(F&& f, const std::vector<double>& x, double h = 1e-4) {
    const std::size_t n = x.size();
    std::vector<std::vector<double>> hess(n, std::vector<double>(n, 0.0));
    std::vector<double> x_mod = x;
    const double f_center = f(x);

    for (std::size_t i = 0; i < n; ++i) {
        // Diagonal: f''_ii
        x_mod[i] = x[i] + h;
        const double f_pi = f(x_mod);
        x_mod[i] = x[i] - h;
        const double f_mi = f(x_mod);
        x_mod[i] = x[i];
        hess[i][i] = (f_pi - 2.0 * f_center + f_mi) / (h * h);

        // Off-diagonal: f''_ij
        for (std::size_t j = i + 1; j < n; ++j) {
            x_mod[i] = x[i] + h;
            x_mod[j] = x[j] + h;
            const double f_pp = f(x_mod);

            x_mod[i] = x[i] + h;
            x_mod[j] = x[j] - h;
            const double f_pm = f(x_mod);

            x_mod[i] = x[i] - h;
            x_mod[j] = x[j] + h;
            const double f_mp = f(x_mod);

            x_mod[i] = x[i] - h;
            x_mod[j] = x[j] - h;
            const double f_mm = f(x_mod);

            x_mod[i] = x[i];
            x_mod[j] = x[j];

            const double d2 = (f_pp - f_pm - f_mp + f_mm) / (4.0 * h * h);
            hess[i][j] = d2;
            hess[j][i] = d2; // Symmetry
        }
    }
    return hess;
}

// ============================================================================
// 2. NUMERICAL QUADRATURE (INTEGRATION)
// ============================================================================

/**
 * @brief Trapezoidal rule over n subintervals.
 */
template <typename F>
[[nodiscard]] inline double integrate_trapezoid(F&& f, double a, double b, std::size_t n = 1000) noexcept {
    if (n == 0) return 0.0;
    const double h = (b - a) / static_cast<double>(n);
    double sum = 0.5 * (f(a) + f(b));
    for (std::size_t i = 1; i < n; ++i) {
        sum += f(a + static_cast<double>(i) * h);
    }
    return sum * h;
}

/**
 * @brief Composite Simpson's 1/3 Rule (O(h^4) accuracy).
 */
template <typename F>
[[nodiscard]] inline double integrate_simpson(F&& f, double a, double b, std::size_t n = 1000) noexcept {
    if (n == 0) return 0.0;
    if ((n & 1) != 0) ++n; // n must be even
    const double h = (b - a) / static_cast<double>(n);
    double sum = f(a) + f(b);

    for (std::size_t i = 1; i < n; ++i) {
        const double x = a + static_cast<double>(i) * h;
        sum += (i & 1) ? 4.0 * f(x) : 2.0 * f(x);
    }
    return sum * (h / 3.0);
}

/**
 * @brief Gauss-Legendre 5-Point Quadrature (Exact for polynomials up to degree 9).
 */
template <typename F>
[[nodiscard]] inline double integrate_gauss_legendre_5(F&& f, double a, double b) noexcept {
    // Roots of P_5(x)
    constexpr double x1 = 0.0;
    constexpr double x2 = 0.53846931010568309;
    constexpr double x3 = 0.90617984593866399;

    // Weights
    constexpr double w1 = 0.56888888888888889;
    constexpr double w2 = 0.47862867049936647;
    constexpr double w3 = 0.23692688505618908;

    const double mid = 0.5 * (a + b);
    const double half_len = 0.5 * (b - a);

    const double s = w1 * f(mid) +
                     w2 * (f(mid + half_len * x2) + f(mid - half_len * x2)) +
                     w3 * (f(mid + half_len * x3) + f(mid - half_len * x3));

    return s * half_len;
}

/**
 * @brief Gauss-Legendre 10-Point Quadrature (Exact for polynomials up to degree 19).
 */
template <typename F>
[[nodiscard]] inline double integrate_gauss_legendre_10(F&& f, double a, double b) noexcept {
    constexpr std::array<double, 5> x = {
        0.1488743389816312, 0.4333953941292472, 0.6794095682990244,
        0.8650633666889845, 0.9739065285171717
    };
    constexpr std::array<double, 5> w = {
        0.2955242247147529, 0.2692667193099963, 0.2190863625159820,
        0.1494513491505806, 0.0666713443086881
    };

    const double mid = 0.5 * (a + b);
    const double half_len = 0.5 * (b - a);
    double sum = 0.0;

    for (std::size_t i = 0; i < 5; ++i) {
        sum += w[i] * (f(mid + half_len * x[i]) + f(mid - half_len * x[i]));
    }
    return sum * half_len;
}

/**
 * @brief Adaptive Simpson's 3/8 Quadrature with recursive refinement.
 */
template <typename F>
[[nodiscard]] inline double integrate_adaptive_simpson(
    F&& f, double a, double b, double tol = 1e-8, std::size_t max_depth = 20) noexcept {

    auto simpson_step = [&](auto& self, double left, double right, double eps,
                            double whole, std::size_t depth) -> double {
        const double c = 0.5 * (left + right);
        const double h = (right - left) / 6.0;
        const double d = 0.5 * (left + c);
        const double e = 0.5 * (c + right);

        const double left_part = (c - left) / 6.0 * (f(left) + 4.0 * f(d) + f(c));
        const double right_part = (right - c) / 6.0 * (f(c) + 4.0 * f(e) + f(right));
        const double delta = left_part + right_part - whole;

        if (depth >= max_depth || std::fabs(delta) <= 15.0 * eps) {
            return left_part + right_part + delta / 15.0;
        }

        return self(self, left, c, 0.5 * eps, left_part, depth + 1) +
               self(self, c, right, 0.5 * eps, right_part, depth + 1);
    };

    const double initial_whole = (b - a) / 6.0 * (f(a) + 4.0 * f(0.5 * (a + b)) + f(b));
    return simpson_step(simpson_step, a, b, tol, initial_whole, 0);
}

// ============================================================================
// 3. ORDINARY DIFFERENTIAL EQUATIONS (ODE SOLVERS)
// ============================================================================

/**
 * @brief Classical Runge-Kutta 4th Order (RK4) single step.
 * Solves y' = f(t, y)
 */
template <typename F>
[[nodiscard]] inline double rk4_step(F&& f, double t, double y, double dt) noexcept {
    const double k1 = f(t, y);
    const double k2 = f(t + 0.5 * dt, y + 0.5 * dt * k1);
    const double k3 = f(t + 0.5 * dt, y + 0.5 * dt * k2);
    const double k4 = f(t + dt, y + dt * k3);
    return y + (dt / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

/**
 * @brief Multidimensional RK4 step for system of ODEs y' = f(t, y).
 */
template <typename F>
[[nodiscard]] inline std::vector<double> rk4_step_vec(
    F&& f, double t, const std::vector<double>& y, double dt) {
    const std::size_t n = y.size();
    const auto k1 = f(t, y);

    std::vector<double> y_temp(n);
    for (std::size_t i = 0; i < n; ++i) y_temp[i] = y[i] + 0.5 * dt * k1[i];
    const auto k2 = f(t + 0.5 * dt, y_temp);

    for (std::size_t i = 0; i < n; ++i) y_temp[i] = y[i] + 0.5 * dt * k2[i];
    const auto k3 = f(t + 0.5 * dt, y_temp);

    for (std::size_t i = 0; i < n; ++i) y_temp[i] = y[i] + dt * k3[i];
    const auto k4 = f(t + dt, y_temp);

    std::vector<double> y_next(n);
    for (std::size_t i = 0; i < n; ++i) {
        y_next[i] = y[i] + (dt / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    }
    return y_next;
}

/**
 * @brief Dormand-Prince (RK45) Adaptive Step-Size Integrator.
 */
struct RK45Result {
    double t_end = 0.0;
    double y_end = 0.0;
    std::size_t steps_accepted = 0;
    std::size_t steps_rejected = 0;
};

template <typename F>
[[nodiscard]] inline RK45Result rk45_integrate(
    F&& f, double t0, double t_target, double y0,
    double tol = 1e-6, double dt_initial = 1e-3) noexcept {

    double t = t0;
    double y = y0;
    double dt = dt_initial;
    std::size_t accepted = 0;
    std::size_t rejected = 0;

    // Dormand-Prince Butcher Tableau constants
    constexpr double c2 = 1.0/5.0, c3 = 3.0/10.0, c4 = 4.0/5.0, c5 = 8.0/9.0;
    constexpr double a21 = 1.0/5.0;
    constexpr double a31 = 3.0/40.0, a32 = 9.0/40.0;
    constexpr double a41 = 44.0/45.0, a42 = -56.0/15.0, a43 = 32.0/9.0;
    constexpr double a51 = 19372.0/6561.0, a52 = -25360.0/2187.0, a53 = 64448.0/6561.0, a54 = -212.0/729.0;
    constexpr double a61 = 9017.0/3168.0, a62 = -355.0/33.0, a63 = 46732.0/5247.0, a64 = 49.0/176.0, a65 = -5103.0/18656.0;

    // 5th order weights
    constexpr double b1 = 35.0/384.0, b3 = 500.0/1113.0, b4 = 125.0/192.0, b5 = -2187.0/6784.0, b6 = 11.0/84.0;
    // 4th order embedded weights difference (E_i = b_i - b_star_i)
    constexpr double e1 = 71.0/57600.0, e3 = -71.0/16695.0, e4 = 71.0/1920.0, e5 = -17253.0/339200.0, e6 = 22.0/525.0, e7 = -1.0/40.0;

    while (t < t_target) {
        if (t + dt > t_target) dt = t_target - t;

        const double k1 = f(t, y);
        const double k2 = f(t + c2 * dt, y + dt * a21 * k1);
        const double k3 = f(t + c3 * dt, y + dt * (a31 * k1 + a32 * k2));
        const double k4 = f(t + c4 * dt, y + dt * (a41 * k1 + a42 * k2 + a43 * k3));
        const double k5 = f(t + c5 * dt, y + dt * (a51 * k1 + a52 * k2 + a53 * k3 + a54 * k4));
        const double k6 = f(t + dt, y + dt * (a61 * k1 + a62 * k2 + a63 * k3 + a64 * k4 + a65 * k5));

        const double y5 = y + dt * (b1 * k1 + b3 * k3 + b4 * k4 + b5 * k5 + b6 * k6);
        const double k7 = f(t + dt, y5);

        // Truncation error estimate
        const double err = dt * std::fabs(e1 * k1 + e3 * k3 + e4 * k4 + e5 * k5 + e6 * k6 + e7 * k7);
        const double scale = tol * (1.0 + std::fabs(y));

        if (err <= scale) {
            // Accept step
            t += dt;
            y = y5;
            ++accepted;
            // Adaptive step enlargement
            const double factor = 0.9 * std::pow(scale / std::max(err, 1e-15), 0.2);
            dt = dt * std::clamp(factor, 0.2, 5.0);
        } else {
            // Reject step, shrink dt
            ++rejected;
            const double factor = 0.9 * std::pow(scale / err, 0.25);
            dt = dt * std::clamp(factor, 0.1, 0.5);
        }

        if (dt < 1e-14) break; // Avoid infinite loop on stiffness
    }

    return RK45Result{t, y, accepted, rejected};
}

// ============================================================================
// 4. MULTIVARIABLE CALCULUS & JACOBIAN MATRICES
// ============================================================================

template <typename F>
[[nodiscard]] inline double directional_derivative(
    F&& f, const std::vector<double>& x, const std::vector<double>& v, double h = 1e-5) {
    const auto grad = gradient(f, x, h);
    double sum = 0.0;
    const std::size_t n = std::min(grad.size(), v.size());
    for (std::size_t i = 0; i < n; ++i) sum += grad[i] * v[i];
    return sum;
}

template <typename F>
[[nodiscard]] inline std::vector<double> jacobian(
    F&& f, const std::vector<double>& x, std::size_t m, double h = 1e-5) {
    const std::size_t n = x.size();
    std::vector<double> J(m * n, 0.0);
    std::vector<double> x_plus = x;
    std::vector<double> x_minus = x;
    const double inv_2h = 0.5 / h;

    for (std::size_t j = 0; j < n; ++j) {
        x_plus[j] = x[j] + h;
        x_minus[j] = x[j] - h;
        const auto f_plus = f(x_plus);
        const auto f_minus = f(x_minus);
        for (std::size_t i = 0; i < m; ++i) {
            J[i * n + j] = (f_plus[i] - f_minus[i]) * inv_2h;
        }
        x_plus[j] = x[j];
        x_minus[j] = x[j];
    }
    return J;
}

// ============================================================================
// 5. PDE FINITE DIFFERENCE STENCILS (HEAT & LAPLACE EQUATIONS)
// ============================================================================

[[nodiscard]] inline std::vector<double> pde_heat_1d_step(
    const std::vector<double>& u, double alpha, double dt, double dx) {
    const std::size_t n = u.size();
    if (n < 3) return u;
    const double r = alpha * dt / (dx * dx);
    std::vector<double> u_next = u;
    for (std::size_t i = 1; i < n - 1; ++i) {
        u_next[i] = u[i] + r * (u[i + 1] - 2.0 * u[i] + u[i - 1]);
    }
    return u_next;
}

inline void pde_poisson_2d_relax(
    std::vector<double>& u, const std::vector<double>& source,
    std::size_t nx, std::size_t ny, double dx) {
    const double dx2 = dx * dx;
    for (std::size_t y = 1; y < ny - 1; ++y) {
        for (std::size_t x = 1; x < nx - 1; ++x) {
            const std::size_t idx = y * nx + x;
            const double sum_neighbors = u[idx + 1] + u[idx - 1] + u[idx + nx] + u[idx - nx];
            const double src = source.empty() ? 0.0 : source[idx];
            u[idx] = 0.25 * (sum_neighbors - dx2 * src);
        }
    }
}

} // namespace dgtl::math::calculus
