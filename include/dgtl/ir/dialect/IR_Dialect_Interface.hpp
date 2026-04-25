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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 6: ZERO-COST STATIC DIALECT INTERFACE - VERSION 2.0
 * PILLAR 3: THE SENTINEL INTEGRATION (SHADOW-VALIDATION)
 * STRATEGY: C++26 Concept-based Invariant Guards for Custom Hardware.
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Static Dispatch)
 */

#pragma once
#include <concepts>
#include <string_view>
#include <type_traits>
#include <array>

// Internal logic only needs the basic types. 
#include "../base/IR_Nodes.hpp"

namespace dgtl::ir {
    // Forward declarations for cross-module visibility
    class IRStream;
    // IRAtom and IRCore are now defined in IR_Types.hpp
}

namespace dgtl::dialect {

namespace ir = dgtl::ir;

inline constexpr ir::OpCode kFastAddOpcode = static_cast<ir::OpCode>(0x8001);
inline constexpr ir::OpCode kFastFMAOpcode = static_cast<ir::OpCode>(0x8002);

/**
 * @concept DGTLOp
 * @brief Validates an operation's static structure against the Atom/Core duality.
 */
template <typename T>
concept DGTLOp = requires {
    { T::opcode } -> std::convertible_to<ir::OpCode>;
} && requires(const ir::IRAtom& atom, const ir::IRCore& core) {
    { T::verify_impl(atom, core) } -> std::same_as<bool>;
};

/**
 * @struct BaseOp
 * @brief CRTP Base for all DGTL Operations.
 * Zero-overhead dispatch to the derived implementation.
 */
template <typename Derived, ir::OpCode Op>
struct BaseOp {
    static constexpr ir::OpCode opcode = Op;

    [[nodiscard]] static inline bool verify(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        // If it matches our opcode, run the specific invariant check.
        if (atom.opcode == opcode) [[likely]] {
            return Derived::verify_impl(atom, core);
        }
        return true; // Pass-through for non-matching opcodes.
    }
};

/**
 * @struct MathDialect
 * @brief Hardware-Accelerated Vector Math Dialect.
 * Includes Anti-Hallucination checks for high-integrity compute.
 */
struct MathDialect {
    static constexpr std::string_view name = "DGTL.math";
    
    // FastAdd: Optimized 32-bit addition.
    struct FastAdd : BaseOp<FastAdd, kFastAddOpcode> {
        [[nodiscard]] static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
            // PILLAR 3: Sentinel Check - Ensure result and source are distinct/valid
            if (core.result_id == ir::INVALID_SLOT ||
                core.op_a == ir::INVALID_SLOT ||
                core.op_b == ir::INVALID_SLOT) [[unlikely]] {
                return false;
            }

            const uint64_t expected = ir::IRNode::calculate_hash(
                atom.opcode,
                core.result_id,
                core.op_a,
                core.op_b,
                core.op_c
            );
            return core.intent_hash == expected;
        }
    };
    
    // FastFMA: Fused Multiply-Add (Essential for HFT/ML workloads).
    struct FastFMA : BaseOp<FastFMA, kFastFMAOpcode> {
        [[nodiscard]] static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
            // All operands must be defined for FMA
            return (core.result_id != ir::INVALID_SLOT && 
                    core.op_a != ir::INVALID_SLOT && 
                    core.op_b != ir::INVALID_SLOT && 
                    core.op_c != ir::INVALID_SLOT);
        }
    };
};

/**
 * @class OpRegistry
 * @brief Orchestrates the "DGTL-Invariant" Guard across the IRStream.
 */
template <DGTLOp... Ops>
class OpRegistry {
public:
    /**
     * @brief High-Throughput Sentinel Verification.
     * Defined in the .cpp where IRStream methods (is_in_transaction, get_node) are visible.
     */
    [[nodiscard]] static bool verify_stream(ir::IRStream& stream) noexcept;

    /**
     * @brief Logic-level evolution. 
     * Applies dialect-specific transformations to nodes before they are committed.
     */
    static void evolve_stream(ir::IRStream& stream) noexcept;

private:
    /**
     * @brief O(1) Static Dispatch Logic via C++17 Fold Expression.
     */
    [[nodiscard]] static inline bool dispatch_verify(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        return (... && Ops::verify(atom, core));
    }
};

/**
 * @brief Standard Hardware Registry.
 */
using StandardMathRegistry = OpRegistry<MathDialect::FastAdd, MathDialect::FastFMA>;

} // namespace dgtl::dialect

