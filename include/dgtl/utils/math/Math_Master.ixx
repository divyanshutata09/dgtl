/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   DGTL Sovereign Master Mathematics Aggregate Module
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

export module dgtl.math;

// 1. Core IEEE-754 Transcendentals, Bitwise & Minimax Polynomials
export import dgtl.math_utils;

// 2. Numerical Differentiation, Quadrature & ODE Solvers
export import dgtl.math.calculus;

// 3. Statistical Distributions, Moments & PRNG
export import dgtl.math.probability;

// 4. Combinatorics, Partitions & Permutations
export import dgtl.math.combinatorics;

// 5. Complex Numbers, Transcendentals & Dual Autodiff
export import dgtl.math.complex;

// 6. 2D, 3D & 4D Geometry, Quaternions & Rays
export import dgtl.math.geometry;

// 7. Special Functions: erf, Gamma, Digamma, Bessel J/Y, Orthogonal Poly & FFT
export import dgtl.math.special;

// 8. GenAI & LLM Activations, Online Softmax, RoPE, BF16/FP8 Quantization
export import dgtl.math.genai;

// 9. Linear Algebra, Matrix Factorizations, Polynomials & Number Theory
export import dgtl.math.algebra;

