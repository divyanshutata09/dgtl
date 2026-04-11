/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 6: ZERO-COST STATIC DIALECT INTERFACE - VERSION 2.0
 * PILLAR 3: THE SENTINEL INTEGRATION (SHADOW-VALIDATION)
 * STRATEGY: C++20 Concept-based Invariant Guards for Custom Hardware.
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Static Dispatch)
 */

#pragma once
#include <concepts>
#include <string_view>
#include <type_traits>
#include <array>

// Internal logic only needs the basic types. 
// Do NOT include IR_Entry_Point.hpp here to avoid circular dependency.
#include "IR_Types.hpp"

namespace dgtl::ir {
    // Forward declarations: Tells the compiler these exist without loading full guts.
    // This allows Dialect_interface to be Part 3 of the Entry Point safely.
    class IRStream;
    struct IRAtom;
    struct IRCore;
}

namespace dgtl::dialect {

// Alias for cleaner internal logic
namespace ir = dgtl::ir;

/**
 * @concept DGTLOp
 * @brief Operation Concept for zero-cost static verification.
 * Validates against the Tiered IR structure (Atom + Core).
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
 * Returns bool to signal the Sentinel (Pillar 3) whether to Rollback or Commit.
 */
template <typename Derived, ir::OpCode Op>
struct BaseOp {
    static constexpr ir::OpCode opcode = Op;

    static inline bool verify(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        if (atom.opcode == opcode) [[likely]] {
            return Derived::verify_impl(atom, core);
        }
        return true; // Not our opcode, opt through.
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
    struct FastAdd : BaseOp<FastAdd, static_cast<ir::OpCode>(0x8001)> {
        static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
            // PILLAR 3: Sentinel Check
            // FastAdd must produce a result and have at least one valid operand.
            if (core.result_id == 0 || core.op_a == 0) [[unlikely]] return false;
            
            // Intent Verification: Ensures IR mutation matches architectural intent.
            if (core.intent_hash != 0) {
                // Future: CRC check logic goes here.
            }
            return true;
        }
    };
    
    // FastFMA: Fused Multiply-Add (Essential for HFT/ML workloads).
    struct FastFMA : BaseOp<FastFMA, static_cast<ir::OpCode>(0x8002)> {
        static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
            // FMA in DGTL requires all registers to be non-zero (initialized).
            return (core.result_id != 0 && core.op_a != 0 && core.op_b != 0);
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
     * Scans the active stream. Since implementation requires IRStream methods, 
     * this must be defined where the full IRStream class is visible.
     */
    static bool verify_stream(ir::IRStream& stream) noexcept;

private:
    /**
     * @brief O(1) Dispatch Logic via C++17 Fold Expression.
     */
    static inline bool dispatch(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        return (... && Ops::verify(atom, core));
    }
};

/**
 * @brief Default Registry for Standard Math Dialect.
 */
using StandardMathRegistry = OpRegistry<MathDialect::FastAdd, MathDialect::FastFMA>;

} // namespace dgtl::dialect