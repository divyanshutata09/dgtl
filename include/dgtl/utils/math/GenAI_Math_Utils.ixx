/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   GenAI, LLM Activations, Online Softmax, RoPE, BF16/FP8 Quantization
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
#include <numeric>
#include <cstring>
#include <algorithm>
#include <limits>

export module dgtl.math.genai;

export namespace dgtl::math::genai {

// ============================================================================
// 1. NEURAL ACTIVATION FUNCTIONS
// ============================================================================

/**
 * @brief Numerically stable Sigmoid: 1 / (1 + exp(-x)) with [-88, 88] saturation.
 */
[[nodiscard]] inline double sigmoid(double x) noexcept {
    if (x > 88.0) return 1.0;
    if (x < -88.0) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
}

/**
 * @brief SiLU / Swish activation: x * sigmoid(x).
 */
[[nodiscard]] inline double silu(double x) noexcept {
    return x * sigmoid(x);
}

/**
 * @brief SwiGLU gated activation: SiLU(gate) * up.
 */
[[nodiscard]] inline double swiglu(double gate, double up) noexcept {
    return silu(gate) * up;
}

/**
 * @brief Fast GeLU approximation via Tanh (Hendrycks & Gimpel / GPT standard):
 * 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
 */
[[nodiscard]] inline double fast_gelu(double x) noexcept {
    constexpr double kSqrt2OverPi = 0.7978845608028654;
    constexpr double kCoeff       = 0.044715;
    const double inner = kSqrt2OverPi * (x + kCoeff * x * x * x);
    return 0.5 * x * (1.0 + std::tanh(inner));
}

[[nodiscard]] inline double gelu_fast(double x) noexcept {
    return fast_gelu(x);
}

/**
 * @brief Exact GeLU: 0.5 * x * (1 + erf(x / sqrt(2)))
 */
[[nodiscard]] inline double exact_gelu(double x) noexcept {
    constexpr double kInvSqrt2 = 0.7071067811865475;
    return 0.5 * x * (1.0 + std::erf(x * kInvSqrt2));
}

/**
 * @brief QuickGELU (used in CLIP / Vision Transformers): x * sigmoid(1.702 * x)
 */
[[nodiscard]] inline double quick_gelu(double x) noexcept {
    return x * sigmoid(1.702 * x);
}

/**
 * @brief Mish activation: x * tanh(softplus(x)) = x * tanh(ln(1 + exp(x)))
 */
[[nodiscard]] inline double mish(double x) noexcept {
    if (x > 20.0) return x; // Softplus(x) -> x for large x
    const double sp = std::log1p(std::exp(x));
    return x * std::tanh(sp);
}

// ============================================================================
// 2. NORMALIZATION KERNELS (RMSNorm & LayerNorm)
// ============================================================================

/**
 * @brief Root Mean Square Normalization (RMSNorm) with element-wise gain scaling.
 * RMS(x) = sqrt( 1/N * sum(x_i^2) + eps )
 * out_i = (x_i / RMS(x)) * gamma_i
 */
inline void rms_norm(
    const double* in, const double* gamma, double* out, std::size_t n, double eps = 1e-6) noexcept {
    if (n == 0) return;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum_sq += in[i] * in[i];
    }
    const double rms = std::sqrt(sum_sq / static_cast<double>(n) + eps);
    const double inv_rms = 1.0 / rms;

    for (std::size_t i = 0; i < n; ++i) {
        out[i] = in[i] * inv_rms * (gamma ? gamma[i] : 1.0);
    }
}

/**
 * @brief Standard Layer Normalization with affine transformation (gamma * norm + beta).
 */
inline void layer_norm(
    const double* in, const double* gamma, const double* beta, double* out, std::size_t n, double eps = 1e-5) noexcept {
    if (n == 0) return;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += in[i];
    const double mean = sum / static_cast<double>(n);

    double var_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double diff = in[i] - mean;
        var_sum += diff * diff;
    }
    const double inv_std = 1.0 / std::sqrt(var_sum / static_cast<double>(n) + eps);

    for (std::size_t i = 0; i < n; ++i) {
        const double normalized = (in[i] - mean) * inv_std;
        const double g = gamma ? gamma[i] : 1.0;
        const double b = beta ? beta[i] : 0.0;
        out[i] = normalized * g + b;
    }
}

// ============================================================================
// 3. ONLINE SOFTMAX & FLASHATTENTION-2 NUMERICS
// ============================================================================

/**
 * @brief Online Softmax Accumulator state (Milakov & Gimelshein / Dao et al.)
 * Computes exact numerically stable softmax in a single pass without allocating
 * the full NxN attention matrix.
 */
struct OnlineSoftmaxState {
    double max_val = -std::numeric_limits<double>::infinity();
    double sum_exp = 0.0;

    /**
     * @brief Updates state with a new scalar score x.
     * Returns the un-normalized contribution exp(x - max_new).
     */
    double update(double x) noexcept {
        if (x > max_val) {
            sum_exp = sum_exp * std::exp(max_val - x) + 1.0;
            max_val = x;
            return 1.0;
        } else {
            const double term = std::exp(x - max_val);
            sum_exp += term;
            return term;
        }
    }

    /**
     * @brief Merges two partial online softmax states (used across blocks).
     */
    void merge(const OnlineSoftmaxState& other) noexcept {
        if (other.max_val <= -std::numeric_limits<double>::infinity()) return;
        if (max_val <= -std::numeric_limits<double>::infinity()) {
            *this = other;
            return;
        }
        const double new_max = std::max(max_val, other.max_val);
        const double scale_self = std::exp(max_val - new_max);
        const double scale_other = std::exp(other.max_val - new_max);
        max_val = new_max;
        sum_exp = sum_exp * scale_self + other.sum_exp * scale_other;
    }
};

// ============================================================================
// 4. ROTARY POSITION EMBEDDING (RoPE) & ALiBi
// ============================================================================

/**
 * @brief Applies Rotary Positional Embedding to a 2D query/key head vector.
 * [q_0, q_1] -> [q_0 * cos - q_1 * sin, q_0 * sin + q_1 * cos]
 */
inline void apply_rope_2d(double& x, double& y, double theta_pos) noexcept {
    const double cos_t = std::cos(theta_pos);
    const double sin_t = std::sin(theta_pos);
    const double x_rot = x * cos_t - y * sin_t;
    const double y_rot = x * sin_t + y * cos_t;
    x = x_rot;
    y = y_rot;
}

/**
 * @brief Computes standard Transformer ALiBi (Attention with Linear Biases) slope:
 * m_h = 2^(-8 * (h + 1) / H)
 */
[[nodiscard]] inline double alibi_slope(uint32_t head_index, uint32_t num_heads) noexcept {
    if (num_heads == 0) return 1.0;
    const double exponent = -8.0 * static_cast<double>(head_index + 1) / static_cast<double>(num_heads);
    return std::pow(2.0, exponent);
}

// ============================================================================
// 5. QUANTIZATION & FORMAT CONVERSIONS (BF16, FP8, INT8)
// ============================================================================

/**
 * @brief IEEE-754 FP32 to BF16 (Brain Floating Point: 1 sign, 8 exponent, 7 mantissa).
 * Implements round-to-nearest-even with tie breaking.
 */
[[nodiscard]] inline uint16_t f32_to_bf16(float f) noexcept {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));

    // Handle NaNs: preserve quiet NaN bit
    if ((u & 0x7F800000u) == 0x7F800000u) {
        const uint16_t nan_hi = static_cast<uint16_t>(u >> 16);
        return nan_hi | 0x0040u; // Ensure quiet NaN
    }

    // Round-to-nearest-even: add 0x7FFF + lsb of high half
    const uint32_t lsb = (u >> 16) & 1u;
    const uint32_t rounding_bias = 0x7FFFu + lsb;
    u += rounding_bias;

    return static_cast<uint16_t>(u >> 16);
}

[[nodiscard]] inline float bf16_to_f32(uint16_t bf) noexcept {
    const uint32_t u = static_cast<uint32_t>(bf) << 16;
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

/**
 * @brief FP32 to FP8 E4M3 (OCP / NVIDIA Hopper format: 1 sign, 4 exponent, 3 mantissa, bias 7).
 * Max finite value: 448.0.
 */
[[nodiscard]] inline uint8_t f32_to_fp8_e4m3(float f) noexcept {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    const uint32_t sign = (u >> 24) & 0x80u;
    const int32_t exp = static_cast<int32_t>((u >> 23) & 0xFFu) - 127;
    uint32_t mant = u & 0x007FFFFFu;

    if (exp > 8) {
        // Saturate to max representable FP8 E4M3 (0x7E / 448.0)
        return static_cast<uint8_t>(sign | 0x7Eu);
    }
    if (exp < -9) {
        // Underflow to zero
        return static_cast<uint8_t>(sign);
    }

    // Scale to FP8 E4M3 bias (7)
    int32_t fp8_exp = exp + 7;
    uint32_t fp8_mant = 0;

    if (fp8_exp <= 0) {
        // Subnormal
        mant |= 0x00800000u;
        const int32_t shift = 1 - fp8_exp + 20;
        fp8_mant = mant >> shift;
        fp8_exp = 0;
    } else {
        fp8_mant = (mant + 0x00080000u) >> 20; // Round
        if (fp8_mant > 7) {
            fp8_mant = 0;
            fp8_exp += 1;
        }
    }

    if (fp8_exp >= 15) {
        return static_cast<uint8_t>(sign | 0x7Eu);
    }
    return static_cast<uint8_t>(sign | (fp8_exp << 3) | (fp8_mant & 7u));
}

[[nodiscard]] inline float fp8_e4m3_to_f32(uint8_t fp8) noexcept {
    const uint32_t sign = (static_cast<uint32_t>(fp8) & 0x80u) << 24;
    const uint32_t exp = (fp8 >> 3) & 0x0Fu;
    const uint32_t mant = fp8 & 0x07u;

    if (exp == 0 && mant == 0) {
        float f = 0.0f;
        if (sign) f = -0.0f;
        return f;
    }

    uint32_t f32_u = 0;
    if (exp == 0) {
        // Subnormal in FP8
        float f = static_cast<float>(mant) * (1.0f / 1024.0f);
        if (sign) f = -f;
        return f;
    } else {
        const uint32_t f32_exp = (exp - 7 + 127) << 23;
        const uint32_t f32_mant = mant << 20;
        f32_u = sign | f32_exp | f32_mant;
    }

    float f;
    std::memcpy(&f, &f32_u, sizeof(f));
    return f;
}

/**
 * @brief Symmetric INT8 Quantization: q = clamp(round(x / scale), -128, 127).
 */
[[nodiscard]] inline int8_t quantize_int8(double x, double scale) noexcept {
    if (scale <= 1e-15) return 0;
    const double scaled = std::round(x / scale);
    return static_cast<int8_t>(std::clamp<double>(scaled, -128.0, 127.0));
}

[[nodiscard]] inline double dequantize_int8(int8_t q, double scale) noexcept {
    return static_cast<double>(q) * scale;
}

// ============================================================================
// 6. FLASHATTENTION-2 BLOCK & DEEP LEARNING COMPUTE KERNELS
// ============================================================================

/**
 * @brief FlashAttention-2 block-tiled scaled dot-product attention kernel.
 * Given query block Q (Br x d), key block K (Bc x d), and value block V (Bc x d),
 * updates output block O (Br x d) and online softmax state accumulator.
 * Scale is typically 1.0 / sqrt(d).
 */
inline void flash_attention_2_block(
    const double* Q, const double* K, const double* V,
    double* O, OnlineSoftmaxState* states,
    std::size_t Br, std::size_t Bc, std::size_t d, double scale) noexcept {

    std::vector<double> scores(Bc, 0.0);

    for (std::size_t i = 0; i < Br; ++i) {
        const double* q_row = Q + i * d;
        double* o_row = O + i * d;
        auto& state = states[i];

        double block_max = -std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < Bc; ++j) {
            const double* k_row = K + j * d;
            double dot = 0.0;
            std::size_t k = 0;
            for (; k + 3 < d; k += 4) {
                dot += q_row[k] * k_row[k]
                     + q_row[k + 1] * k_row[k + 1]
                     + q_row[k + 2] * k_row[k + 2]
                     + q_row[k + 3] * k_row[k + 3];
            }
            for (; k < d; ++k) dot += q_row[k] * k_row[k];
            scores[j] = dot * scale;
            if (scores[j] > block_max) block_max = scores[j];
        }

        const double old_max = state.max_val;
        const double new_max = std::max(old_max, block_max);
        const double alpha = (old_max > -std::numeric_limits<double>::infinity()) ? std::exp(old_max - new_max) : 0.0;

        double block_sum = 0.0;
        for (std::size_t j = 0; j < Bc; ++j) {
            scores[j] = std::exp(scores[j] - new_max);
            block_sum += scores[j];
        }

        for (std::size_t k = 0; k < d; ++k) {
            o_row[k] *= alpha;
            double pv = 0.0;
            for (std::size_t j = 0; j < Bc; ++j) {
                pv += scores[j] * V[j * d + k];
            }
            o_row[k] += pv;
        }

        state.max_val = new_max;
        state.sum_exp = state.sum_exp * alpha + block_sum;
    }
}

/**
 * @brief AdamW optimizer decoupled weight decay update step.
 */
inline void adamw_step(
    double* param, double* m, double* v, const double* grad,
    std::size_t n, double lr, double beta1, double beta2,
    double eps, double weight_decay, uint64_t step) noexcept {

    if (n == 0 || lr <= 0.0) return;
    const double bias_correction1 = 1.0 - std::pow(beta1, static_cast<double>(step));
    const double bias_correction2 = 1.0 - std::pow(beta2, static_cast<double>(step));
    const double step_size = lr / bias_correction1;

    for (std::size_t i = 0; i < n; ++i) {
        const double g = grad[i];
        param[i] -= lr * weight_decay * param[i];

        m[i] = beta1 * m[i] + (1.0 - beta1) * g;
        v[i] = beta2 * v[i] + (1.0 - beta2) * g * g;

        const double denom = std::sqrt(v[i] / bias_correction2) + eps;
        param[i] -= step_size * (m[i] / denom);
    }
}

/**
 * @brief 1D sliding cross-correlation (convolution).
 */
inline void conv1d(
    const double* in, std::size_t in_len,
    const double* kernel, std::size_t kernel_len,
    double* out, std::size_t out_len,
    std::size_t stride = 1, std::size_t padding = 0) noexcept {

    for (std::size_t i = 0; i < out_len; ++i) {
        const int64_t in_start = static_cast<int64_t>(i * stride) - static_cast<int64_t>(padding);
        double sum = 0.0;
        for (std::size_t k = 0; k < kernel_len; ++k) {
            const int64_t in_idx = in_start + static_cast<int64_t>(k);
            if (in_idx >= 0 && in_idx < static_cast<int64_t>(in_len)) {
                sum += in[in_idx] * kernel[k];
            }
        }
        out[i] = sum;
    }
}

/**
 * @brief 2D spatial window max pooling.
 */
inline void max_pool2d(
    const double* in, std::size_t H, std::size_t W,
    std::size_t kernel_h, std::size_t kernel_w,
    std::size_t stride_h, std::size_t stride_w,
    double* out, std::size_t out_h, std::size_t out_w) noexcept {

    for (std::size_t oh = 0; oh < out_h; ++oh) {
        const std::size_t ih_start = oh * stride_h;
        for (std::size_t ow = 0; ow < out_w; ++ow) {
            const std::size_t iw_start = ow * stride_w;
            double max_v = -std::numeric_limits<double>::infinity();

            for (std::size_t kh = 0; kh < kernel_h && (ih_start + kh) < H; ++kh) {
                for (std::size_t kw = 0; kw < kernel_w && (iw_start + kw) < W; ++kw) {
                    const double v = in[(ih_start + kh) * W + (iw_start + kw)];
                    if (v > max_v) max_v = v;
                }
            }
            out[oh * out_w + ow] = (max_v == -std::numeric_limits<double>::infinity()) ? 0.0 : max_v;
        }
    }
}

/**
 * @brief Affine transformation forward pass: Y = X * W^T + b.
 * X is (M x K), W is (N x K), b is (N), Y is (M x N).
 */
inline void linear_forward(
    const double* X, const double* W, const double* b,
    double* Y, std::size_t M, std::size_t K, std::size_t N) noexcept {

    for (std::size_t i = 0; i < M; ++i) {
        const double* x_row = X + i * K;
        double* y_row = Y + i * N;
        for (std::size_t j = 0; j < N; ++j) {
            const double* w_row = W + j * K;
            double dot = b ? b[j] : 0.0;
            std::size_t k = 0;
            for (; k + 3 < K; k += 4) {
                dot += x_row[k] * w_row[k]
                     + x_row[k + 1] * w_row[k + 1]
                     + x_row[k + 2] * w_row[k + 2]
                     + x_row[k + 3] * w_row[k + 3];
            }
            for (; k < K; ++k) {
                dot += x_row[k] * w_row[k];
            }
            y_row[j] = dot;
        }
    }
}

} // namespace dgtl::math::genai
