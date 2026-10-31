/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Advanced Linear & Polynomial Algebra, Matrix Decompositions & Number Theory
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>
#include <complex>
#include <algorithm>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <utility>

export module dgtl.math.algebra;

export namespace dgtl::math::algebra {

// ============================================================================
// 1. DYNAMIC MATRIX & VECTOR ALGEBRA
// ============================================================================

class Matrix {
public:
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    std::vector<double> data_{};

    constexpr Matrix() noexcept = default;
    Matrix(std::size_t rows, std::size_t cols, double init_val = 0.0)
        : rows_(rows), cols_(cols), data_(rows * cols, init_val) {}

    Matrix(std::size_t rows, std::size_t cols, std::initializer_list<double> list)
        : rows_(rows), cols_(cols), data_(list) {
        if (data_.size() != rows * cols) {
            data_.resize(rows * cols, 0.0);
        }
    }

    [[nodiscard]] static Matrix identity(std::size_t n) {
        Matrix I(n, n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            I(i, i) = 1.0;
        }
        return I;
    }

    [[nodiscard]] static Matrix zeros(std::size_t rows, std::size_t cols) {
        return Matrix(rows, cols, 0.0);
    }

    [[nodiscard]] static Matrix ones(std::size_t rows, std::size_t cols) {
        return Matrix(rows, cols, 1.0);
    }

    [[nodiscard]] constexpr std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] constexpr std::size_t cols() const noexcept { return cols_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] const double* data() const noexcept { return data_.data(); }
    [[nodiscard]] double* data() noexcept { return data_.data(); }

    [[nodiscard]] double& operator()(std::size_t r, std::size_t c) noexcept {
        return data_[r * cols_ + c];
    }
    [[nodiscard]] const double& operator()(std::size_t r, std::size_t c) const noexcept {
        return data_[r * cols_ + c];
    }

    Matrix operator+(const Matrix& o) const {
        if (rows_ != o.rows_ || cols_ != o.cols_) return *this;
        Matrix res(rows_, cols_);
        for (std::size_t i = 0; i < data_.size(); ++i) {
            res.data_[i] = data_[i] + o.data_[i];
        }
        return res;
    }

    Matrix operator-(const Matrix& o) const {
        if (rows_ != o.rows_ || cols_ != o.cols_) return *this;
        Matrix res(rows_, cols_);
        for (std::size_t i = 0; i < data_.size(); ++i) {
            res.data_[i] = data_[i] - o.data_[i];
        }
        return res;
    }

    Matrix operator*(double s) const noexcept {
        Matrix res(rows_, cols_);
        for (std::size_t i = 0; i < data_.size(); ++i) {
            res.data_[i] = data_[i] * s;
        }
        return res;
    }

    Matrix operator/(double s) const noexcept {
        const double inv_s = 1.0 / s;
        return (*this) * inv_s;
    }

    // Ultra-fast cache-tiled & vectorizable i-k-j matrix multiplication (Tile size B=32)
    Matrix operator*(const Matrix& o) const {
        if (cols_ != o.rows_) return Matrix{};
        const std::size_t M = rows_;
        const std::size_t K = cols_;
        const std::size_t N = o.cols_;
        Matrix res(M, N, 0.0);

        constexpr std::size_t BLOCK = 32;

        for (std::size_t i0 = 0; i0 < M; i0 += BLOCK) {
            const std::size_t i_max = std::min(i0 + BLOCK, M);
            for (std::size_t k0 = 0; k0 < K; k0 += BLOCK) {
                const std::size_t k_max = std::min(k0 + BLOCK, K);
                for (std::size_t j0 = 0; j0 < N; j0 += BLOCK) {
                    const std::size_t j_max = std::min(j0 + BLOCK, N);

                    for (std::size_t i = i0; i < i_max; ++i) {
                        for (std::size_t k = k0; k < k_max; ++k) {
                            const double aik = (*this)(i, k);
                            std::size_t j = j0;
                            for (; j + 3 < j_max; j += 4) {
                                res(i, j)     += aik * o(k, j);
                                res(i, j + 1) += aik * o(k, j + 1);
                                res(i, j + 2) += aik * o(k, j + 2);
                                res(i, j + 3) += aik * o(k, j + 3);
                            }
                            for (; j < j_max; ++j) {
                                res(i, j) += aik * o(k, j);
                            }
                        }
                    }
                }
            }
        }
        return res;
    }

    [[nodiscard]] std::vector<double> operator*(const std::vector<double>& vec) const {
        if (cols_ != vec.size()) return {};
        std::vector<double> res(rows_, 0.0);
        for (std::size_t i = 0; i < rows_; ++i) {
            double sum = 0.0;
            for (std::size_t j = 0; j < cols_; ++j) {
                sum += (*this)(i, j) * vec[j];
            }
            res[i] = sum;
        }
        return res;
    }

    [[nodiscard]] Matrix transpose() const {
        Matrix res(cols_, rows_);
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t j = 0; j < cols_; ++j) {
                res(j, i) = (*this)(i, j);
            }
        }
        return res;
    }

    [[nodiscard]] double trace() const noexcept {
        const std::size_t n = std::min(rows_, cols_);
        double tr = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            tr += (*this)(i, i);
        }
        return tr;
    }

    [[nodiscard]] double frobenius_norm() const noexcept {
        double sum = 0.0;
        for (double v : data_) sum += v * v;
        return std::sqrt(sum);
    }
};

// ============================================================================
// 2. MATRIX FACTORIZATIONS (LU, QR, CHOLESKY, EIGEN, SVD)
// ============================================================================

struct LUResult {
    Matrix L;
    Matrix U;
    std::vector<std::size_t> P; // Row permutations
    int pivot_sign = 1;
    bool singular = false;
};

/**
 * @brief LU Decomposition with Doolittle algorithm and Partial Pivoting (PA = LU).
 */
[[nodiscard]] inline LUResult lu_decompose(const Matrix& A) {
    const std::size_t n = A.rows();
    if (n == 0 || n != A.cols()) return {};

    LUResult res;
    res.L = Matrix::identity(n);
    res.U = A;
    res.P.resize(n);
    std::iota(res.P.begin(), res.P.end(), 0);
    res.pivot_sign = 1;

    for (std::size_t k = 0; k < n; ++k) {
        // Find pivot
        double max_val = std::fabs(res.U(k, k));
        std::size_t pivot_idx = k;
        for (std::size_t i = k + 1; i < n; ++i) {
            const double val = std::fabs(res.U(i, k));
            if (val > max_val) {
                max_val = val;
                pivot_idx = i;
            }
        }

        if (max_val < 1e-15) {
            res.singular = true;
            return res;
        }

        if (pivot_idx != k) {
            // Swap rows in U
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(res.U(k, j), res.U(pivot_idx, j));
            }
            // Swap already computed elements in L
            for (std::size_t j = 0; j < k; ++j) {
                std::swap(res.L(k, j), res.L(pivot_idx, j));
            }
            std::swap(res.P[k], res.P[pivot_idx]);
            res.pivot_sign = -res.pivot_sign;
        }

        // Eliminate
        for (std::size_t i = k + 1; i < n; ++i) {
            const double factor = res.U(i, k) / res.U(k, k);
            res.L(i, k) = factor;
            res.U(i, k) = 0.0;
            for (std::size_t j = k + 1; j < n; ++j) {
                res.U(i, j) -= factor * res.U(k, j);
            }
        }
    }

    return res;
}

/**
 * @brief Solve Ax = b via forward & backward substitution on PA = LU.
 */
[[nodiscard]] inline std::vector<double> solve_lu(const LUResult& lu, const std::vector<double>& b) {
    const std::size_t n = lu.L.rows();
    if (n == 0 || b.size() != n || lu.singular) return {};

    // Permute b -> Pb
    std::vector<double> pb(n);
    for (std::size_t i = 0; i < n; ++i) {
        pb[i] = b[lu.P[i]];
    }

    // Forward substitution: L y = Pb
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < i; ++j) {
            sum += lu.L(i, j) * y[j];
        }
        y[i] = pb[i] - sum; // L has 1.0 on diagonal
    }

    // Backward substitution: U x = y
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double sum = 0.0;
        for (std::size_t j = i + 1; j < n; ++j) {
            sum += lu.U(i, j) * x[j];
        }
        if (std::fabs(lu.U(i, i)) < 1e-15) return {};
        x[i] = (y[i] - sum) / lu.U(i, i);
    }

    return x;
}

/**
 * @brief Direct linear system solver Ax = b.
 */
[[nodiscard]] inline std::vector<double> solve_linear_system(const Matrix& A, const std::vector<double>& b) {
    const auto lu = lu_decompose(A);
    return solve_lu(lu, b);
}

/**
 * @brief Compute determinant via LU decomposition: det(A) = sgn(P) * prod(U_ii).
 */
[[nodiscard]] inline double determinant(const Matrix& A) {
    const std::size_t n = A.rows();
    if (n == 0 || n != A.cols()) return 0.0;
    const auto lu = lu_decompose(A);
    if (lu.singular) return 0.0;

    double det = static_cast<double>(lu.pivot_sign);
    for (std::size_t i = 0; i < n; ++i) {
        det *= lu.U(i, i);
    }
    return det;
}

/**
 * @brief Invert a non-singular matrix A via LU column-by-column back-substitution.
 */
[[nodiscard]] inline Matrix inverse(const Matrix& A) {
    const std::size_t n = A.rows();
    if (n == 0 || n != A.cols()) return {};
    const auto lu = lu_decompose(A);
    if (lu.singular) return {};

    Matrix inv(n, n);
    std::vector<double> e(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        e[j] = 1.0;
        const auto col = solve_lu(lu, e);
        if (col.empty()) return {};
        for (std::size_t i = 0; i < n; ++i) {
            inv(i, j) = col[i];
        }
        e[j] = 0.0;
    }
    return inv;
}

struct CholeskyResult {
    Matrix L;
    bool success = false;
};

/**
 * @brief Cholesky decomposition A = L L^T for symmetric positive-definite matrices.
 */
[[nodiscard]] inline CholeskyResult cholesky_decompose(const Matrix& A) {
    const std::size_t n = A.rows();
    if (n == 0 || n != A.cols()) return {};

    CholeskyResult res;
    res.L = Matrix::zeros(n, n);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < j; ++k) {
                sum += res.L(i, k) * res.L(j, k);
            }

            if (i == j) {
                const double diff = A(i, i) - sum;
                if (diff <= 1e-15) {
                    res.success = false;
                    return res; // Not positive-definite
                }
                res.L(i, i) = std::sqrt(diff);
            } else {
                res.L(i, j) = (A(i, j) - sum) / res.L(j, j);
            }
        }
    }

    res.success = true;
    return res;
}

struct QRResult {
    Matrix Q;
    Matrix R;
};

/**
 * @brief QR Decomposition via Householder Reflections (A = Q R).
 */
[[nodiscard]] inline QRResult qr_decompose(const Matrix& A) {
    const std::size_t m = A.rows();
    const std::size_t n = A.cols();
    if (m == 0 || n == 0) return {};

    Matrix Q = Matrix::identity(m);
    Matrix R = A;
    std::vector<double> v(m, 0.0);

    const std::size_t k_max = std::min(m - 1, n);
    for (std::size_t k = 0; k < k_max; ++k) {
        // Construct Householder vector v for subcolumn R(k:m, k)
        double norm_sq = 0.0;
        for (std::size_t i = k; i < m; ++i) {
            norm_sq += R(i, k) * R(i, k);
        }
        const double norm = std::sqrt(norm_sq);
        if (norm < 1e-15) continue;

        const double alpha = (R(k, k) >= 0.0) ? -norm : norm;
        std::fill(v.begin(), v.end(), 0.0);
        for (std::size_t i = k; i < m; ++i) {
            v[i] = R(i, k);
        }
        v[k] -= alpha;

        double v_norm_sq = 0.0;
        for (std::size_t i = k; i < m; ++i) v_norm_sq += v[i] * v[i];
        if (v_norm_sq < 1e-15) continue;

        const double beta = 2.0 / v_norm_sq;

        // Apply Householder reflection H = I - beta * v * v^T to R: R = H * R
        for (std::size_t j = k; j < n; ++j) {
            double dot = 0.0;
            for (std::size_t i = k; i < m; ++i) dot += v[i] * R(i, j);
            const double factor = beta * dot;
            for (std::size_t i = k; i < m; ++i) R(i, j) -= factor * v[i];
        }

        // Apply H to Q: Q = Q * H
        for (std::size_t i = 0; i < m; ++i) {
            double dot = 0.0;
            for (std::size_t j = k; j < m; ++j) dot += Q(i, j) * v[j];
            const double factor = beta * dot;
            for (std::size_t j = k; j < m; ++j) Q(i, j) -= factor * v[j];
        }
    }

    return {Q, R};
}

struct EigenResult {
    std::vector<double> eigenvalues;
    Matrix eigenvectors;
};

/**
 * @brief Jacobi Eigenvalue Algorithm for Real Symmetric Matrices.
 * Computes all real eigenvalues and orthonormal eigenvectors with quadratic convergence.
 */
[[nodiscard]] inline EigenResult eigen_symmetric_jacobi(
    const Matrix& A, std::size_t max_iter = 100, double tol = 1e-14) {

    const std::size_t n = A.rows();
    if (n == 0 || n != A.cols()) return {};

    Matrix D = A;
    Matrix V = Matrix::identity(n);

    for (std::size_t iter = 0; iter < max_iter; ++iter) {
        double sum_offdiag = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                sum_offdiag += std::fabs(D(i, j));
            }
        }
        if (sum_offdiag < tol) break;

        const double thresh = (iter < 3) ? (0.2 * sum_offdiag / static_cast<double>(n * n)) : 0.0;

        for (std::size_t p = 0; p < n - 1; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double d_pq = D(p, q);
                if (std::fabs(d_pq) <= thresh) continue;

                const double diff = D(q, q) - D(p, p);
                double t;
                if (std::fabs(d_pq) < 1e-15 * std::fabs(diff)) {
                    t = d_pq / diff;
                } else {
                    const double theta = diff / (2.0 * d_pq);
                    t = 1.0 / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                    if (theta < 0.0) t = -t;
                }

                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                const double tau = s / (1.0 + c);

                D(p, q) = 0.0;
                D(q, p) = 0.0;
                D(p, p) -= t * d_pq;
                D(q, q) += t * d_pq;

                for (std::size_t i = 0; i < n; ++i) {
                    if (i != p && i != q) {
                        const double d_ip = D(i, p);
                        const double d_iq = D(i, q);
                        D(i, p) = d_ip - s * (d_iq + tau * d_ip);
                        D(p, i) = D(i, p);
                        D(i, q) = d_iq + s * (d_ip - tau * d_iq);
                        D(q, i) = D(i, q);
                    }
                }

                for (std::size_t i = 0; i < n; ++i) {
                    const double v_ip = V(i, p);
                    const double v_iq = V(i, q);
                    V(i, p) = v_ip - s * (v_iq + tau * v_ip);
                    V(i, q) = v_iq + s * (v_ip - tau * v_iq);
                }
            }
        }
    }

    std::vector<double> evals(n);
    for (std::size_t i = 0; i < n; ++i) {
        evals[i] = D(i, i);
    }

    return {evals, V};
}

struct SVDResult {
    Matrix U;
    std::vector<double> S; // Singular values
    Matrix Vt;
};

/**
 * @brief Singular Value Decomposition (SVD): A = U S Vt via symmetric eigen on A^T A.
 */
[[nodiscard]] inline SVDResult svd_decompose(const Matrix& A) {
    const std::size_t m = A.rows();
    const std::size_t n = A.cols();
    if (m == 0 || n == 0) return {};

    const Matrix At = A.transpose();
    const Matrix AtA = At * A;
    const auto eigen = eigen_symmetric_jacobi(AtA);

    // Singular values are sqrt(max(0, eigenvalue))
    std::vector<std::pair<double, std::size_t>> sorted_s;
    for (std::size_t i = 0; i < n; ++i) {
        const double lambda = std::max(0.0, eigen.eigenvalues[i]);
        sorted_s.emplace_back(std::sqrt(lambda), i);
    }
    std::sort(sorted_s.begin(), sorted_s.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    std::vector<double> S(n);
    Matrix V(n, n);
    for (std::size_t j = 0; j < n; ++j) {
        S[j] = sorted_s[j].first;
        const std::size_t col_idx = sorted_s[j].second;
        for (std::size_t i = 0; i < n; ++i) {
            V(i, j) = eigen.eigenvectors(i, col_idx);
        }
    }

    // U = A * V * S^-1
    Matrix U = Matrix::zeros(m, n);
    for (std::size_t j = 0; j < n; ++j) {
        if (S[j] > 1e-15) {
            const double inv_s = 1.0 / S[j];
            for (std::size_t i = 0; i < m; ++i) {
                double sum = 0.0;
                for (std::size_t k = 0; k < n; ++k) sum += A(i, k) * V(k, j);
                U(i, j) = sum * inv_s;
            }
        }
    }

    return {U, S, V.transpose()};
}

/**
 * @brief Condition number kappa(A) = sigma_max / sigma_min.
 */
[[nodiscard]] inline double condition_number(const Matrix& A) {
    const auto svd = svd_decompose(A);
    if (svd.S.empty() || svd.S.back() < 1e-15) return std::numeric_limits<double>::infinity();
    return svd.S.front() / svd.S.back();
}

// ============================================================================
// 3. POLYNOMIAL ALGEBRA & ROOT FINDING
// ============================================================================

class Polynomial {
public:
    // Coefficients c_0 + c_1*x + c_2*x^2 + ... + c_n*x^n
    std::vector<double> coeffs_{0.0};

    Polynomial() = default;
    explicit Polynomial(std::vector<double> coeffs) : coeffs_(std::move(coeffs)) {
        trim();
    }
    Polynomial(std::initializer_list<double> init) : coeffs_(init) {
        trim();
    }

    void trim() noexcept {
        while (coeffs_.size() > 1 && std::fabs(coeffs_.back()) < 1e-15) {
            coeffs_.pop_back();
        }
        if (coeffs_.empty()) coeffs_.push_back(0.0);
    }

    [[nodiscard]] std::size_t degree() const noexcept {
        return coeffs_.empty() ? 0 : coeffs_.size() - 1;
    }

    // Horner's method evaluation
    [[nodiscard]] double eval(double x) const noexcept {
        if (coeffs_.empty()) return 0.0;
        double val = coeffs_.back();
        for (std::size_t i = coeffs_.size() - 1; i-- > 0;) {
            val = val * x + coeffs_[i];
        }
        return val;
    }

    [[nodiscard]] Polynomial derivative() const {
        if (coeffs_.size() <= 1) return Polynomial{{0.0}};
        std::vector<double> d(coeffs_.size() - 1);
        for (std::size_t i = 1; i < coeffs_.size(); ++i) {
            d[i - 1] = coeffs_[i] * static_cast<double>(i);
        }
        return Polynomial(d);
    }

    [[nodiscard]] Polynomial integral(double c = 0.0) const {
        std::vector<double> r(coeffs_.size() + 1);
        r[0] = c;
        for (std::size_t i = 0; i < coeffs_.size(); ++i) {
            r[i + 1] = coeffs_[i] / static_cast<double>(i + 1);
        }
        return Polynomial(r);
    }

    Polynomial operator+(const Polynomial& o) const {
        const std::size_t n = std::max(coeffs_.size(), o.coeffs_.size());
        std::vector<double> r(n, 0.0);
        for (std::size_t i = 0; i < coeffs_.size(); ++i) r[i] += coeffs_[i];
        for (std::size_t i = 0; i < o.coeffs_.size(); ++i) r[i] += o.coeffs_[i];
        return Polynomial(r);
    }

    Polynomial operator-(const Polynomial& o) const {
        const std::size_t n = std::max(coeffs_.size(), o.coeffs_.size());
        std::vector<double> r(n, 0.0);
        for (std::size_t i = 0; i < coeffs_.size(); ++i) r[i] += coeffs_[i];
        for (std::size_t i = 0; i < o.coeffs_.size(); ++i) r[i] -= o.coeffs_[i];
        return Polynomial(r);
    }

    // Polynomial multiplication via discrete convolution
    Polynomial operator*(const Polynomial& o) const {
        if (coeffs_.empty() || o.coeffs_.empty()) return Polynomial{{0.0}};
        std::vector<double> r(coeffs_.size() + o.coeffs_.size() - 1, 0.0);
        for (std::size_t i = 0; i < coeffs_.size(); ++i) {
            for (std::size_t j = 0; j < o.coeffs_.size(); ++j) {
                r[i + j] += coeffs_[i] * o.coeffs_[j];
            }
        }
        return Polynomial(r);
    }
};

/**
 * @brief Numerically stable quadratic root solver: a*x^2 + b*x + c = 0
 * Uses Citardauq / Muller formula to eliminate catastrophic cancellation.
 */
[[nodiscard]] inline std::pair<std::complex<double>, std::complex<double>> solve_quadratic(
    double a, double b, double c) noexcept {

    if (std::fabs(a) < 1e-15) {
        if (std::fabs(b) < 1e-15) return {0.0, 0.0};
        return {-c / b, -c / b};
    }

    const double disc = b * b - 4.0 * a * c;
    if (disc >= 0.0) {
        const double q = -0.5 * (b + (b >= 0.0 ? 1.0 : -1.0) * std::sqrt(disc));
        return {q / a, c / q};
    } else {
        const double real_part = -b / (2.0 * a);
        const double imag_part = std::sqrt(-disc) / (2.0 * a);
        return {
            std::complex<double>(real_part, imag_part),
            std::complex<double>(real_part, -imag_part)
        };
    }
}

/**
 * @brief Cardano's cubic equation solver: a*x^3 + b*x^2 + c*x + d = 0.
 */
[[nodiscard]] inline std::array<std::complex<double>, 3> solve_cubic(
    double a, double b, double c, double d) noexcept {

    if (std::fabs(a) < 1e-15) {
        const auto q = solve_quadratic(b, c, d);
        return {q.first, q.second, 0.0};
    }

    // Depressed cubic t^3 + p*t + q = 0 via x = t - b/(3a)
    const double inv_a = 1.0 / a;
    b *= inv_a; c *= inv_a; d *= inv_a;

    const double p = c - (b * b) / 3.0;
    const double q = (2.0 * b * b * b) / 27.0 - (b * c) / 3.0 + d;
    const double delta = (q * q) / 4.0 + (p * p * p) / 27.0;
    const double shift = b / 3.0;

    if (delta > 1e-15) {
        // One real root, two complex conjugate roots
        const double sqrt_delta = std::sqrt(delta);
        const double u = std::cbrt(-q / 2.0 + sqrt_delta);
        const double v = std::cbrt(-q / 2.0 - sqrt_delta);
        const double r1 = u + v - shift;
        const double real_part = -0.5 * (u + v) - shift;
        const double imag_part = 0.5 * std::sqrt(3.0) * (u - v);
        return {
            std::complex<double>(r1, 0.0),
            std::complex<double>(real_part, imag_part),
            std::complex<double>(real_part, -imag_part)
        };
    } else {
        // Three real roots (Viète's trigonometric method)
        const double r = std::sqrt(-p * p * p / 27.0);
        const double phi = std::acos(std::clamp(-q / (2.0 * r), -1.0, 1.0));
        constexpr double two_pi_over_3 = 2.0943951023931954923;
        const double m = 2.0 * std::cbrt(r);

        return {
            std::complex<double>(m * std::cos(phi / 3.0) - shift, 0.0),
            std::complex<double>(m * std::cos((phi + 2.0 * std::numbers::pi) / 3.0) - shift, 0.0),
            std::complex<double>(m * std::cos((phi + 4.0 * std::numbers::pi) / 3.0) - shift, 0.0)
        };
    }
}

// ============================================================================
// 4. NUMBER THEORY & MODULAR ALGEBRA
// ============================================================================

/**
 * @brief Extended Euclidean Algorithm: computes gcd(a, b) and Bezout coefficients x, y.
 * a * x + b * y = gcd(a, b)
 */
[[nodiscard]] inline int64_t extended_gcd(int64_t a, int64_t b, int64_t& x, int64_t& y) noexcept {
    if (b == 0) {
        x = 1;
        y = 0;
        return a;
    }
    int64_t x1 = 0, y1 = 0;
    const int64_t g = extended_gcd(b, a % b, x1, y1);
    x = y1;
    y = x1 - (a / b) * y1;
    return g;
}

/**
 * @brief Modular inverse of a modulo m: a * inv == 1 (mod m). Returns 0 if no inverse exists.
 */
[[nodiscard]] inline int64_t mod_inverse(int64_t a, int64_t m) noexcept {
    int64_t x = 0, y = 0;
    const int64_t g = extended_gcd(a, m, x, y);
    if (g != 1 && g != -1) return 0;
    return (x % m + m) % m;
}

/**
 * @brief Fast modular exponentiation: (base^exp) % mod.
 */
[[nodiscard]] inline int64_t mod_pow(int64_t base, int64_t exp, int64_t mod) noexcept {
    if (mod <= 0) return 0;
    int64_t result = 1;
    base %= mod;
    if (base < 0) base += mod;
    while (exp > 0) {
        if (exp & 1) result = static_cast<int64_t>((static_cast<__int128>(result) * base) % mod);
        base = static_cast<int64_t>((static_cast<__int128>(base) * base) % mod);
        exp >>= 1;
    }
    return result;
}

/**
 * @brief Deterministic Miller-Rabin primality test for 64-bit integers.
 * Proved deterministic for all n < 2^64 using standard test bases.
 */
[[nodiscard]] inline bool is_prime_miller_rabin(uint64_t n) noexcept {
    if (n < 2) return false;
    if (n == 2 || n == 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;

    // Write n - 1 = d * 2^s
    uint64_t d = n - 1;
    uint32_t s = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        ++s;
    }

    constexpr std::array<uint64_t, 12> bases = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37
    };

    for (uint64_t a : bases) {
        if (n <= a) break;
        uint64_t x = static_cast<uint64_t>(mod_pow(static_cast<int64_t>(a), static_cast<int64_t>(d), static_cast<int64_t>(n)));
        if (x == 1 || x == n - 1) continue;

        bool composite = true;
        for (uint32_t r = 1; r < s; ++r) {
            x = static_cast<uint64_t>((static_cast<__int128>(x) * x) % n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

} // namespace dgtl::math::algebra
