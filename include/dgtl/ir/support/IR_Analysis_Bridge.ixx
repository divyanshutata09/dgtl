
/*
 * DGTL HIR analysis bridge.
 *
 * This module owns only HIR analysis lifecycle and structural safety checks.
 * Code generation, execution and target selection are downstream services and
 * therefore cannot be represented here.
 */

module;

#include <cstdint>
#include <cstddef>

export module dgtl.ir.bridge;

// -----------------------------------------------------------------------------
// IR dependencies
// -----------------------------------------------------------------------------

import dgtl.ir.stream;
import dgtl.ir.ssa;
import dgtl.ir_analysis;
import dgtl.ir_node;
import dgtl.ir_schema;

// -----------------------------------------------------------------------------
// Analysis interface dependency
//
// Analysis_Interface is owned by the IR analysis subsystem and is exported as:
//
//     module   : dgtl.ir.analysis.analysis_interface
//     namespace: dgtl::analysis
//
// Keep the namespace and module ownership aligned with the canonical
// analysis subsystem. Do not create an ir::analysis compatibility alias.
// -----------------------------------------------------------------------------

import dgtl.ir.analysis.analysis_interface;

// -----------------------------------------------------------------------------
// Structural validation helpers
// -----------------------------------------------------------------------------

export namespace dgtl::ir::bridge_validation {

[[nodiscard]]
inline bool is_stream_ready(
    const IRStream& stream,
    bool require_shadow_integrity = false
) noexcept
{
    if (stream.is_null() || stream.size() == 0) {
        return false;
    }

    if (require_shadow_integrity &&
        !stream.validate_shadow_integrity()) {
        return false;
    }

    return true;
}

[[nodiscard]]
inline uint32_t released_value_id(
    const IRNode& node
) noexcept
{
    return node.op_a != INVALID_SLOT
        ? node.op_a
        : node.result_id;
}

[[nodiscard]]
inline bool has_release_with_untracked_value(
    const IRStream& stream
) noexcept
{
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto& node = stream.get_node_readonly(i);

        if (node.opcode != OpCode::OWNERSHIP_RELEASE) {
            continue;
        }

        const uint32_t id = released_value_id(node);

        if (id == INVALID_SLOT ||
            stream.get_ownership_info(id) == nullptr) {
            return true;
        }
    }

    return false;
}

} // namespace dgtl::ir::bridge_validation

// -----------------------------------------------------------------------------
// IR analysis bridge
// -----------------------------------------------------------------------------

export namespace dgtl::ir {

class IR_Analysis_Bridge final {
public:

    [[nodiscard]]
    static bool analyze(
        IRStream& stream,
        const dgtl::analysis::Analysis_Interface* analyzer
    ) noexcept
    {
        if (analyzer == nullptr) {
            return false;
        }

        if (!bridge_validation::is_stream_ready(stream)) {
            return false;
        }

        try {
            /*
             * Generated result-type annotations are part of the canonical
             * IR/SSA contract and must be valid before downstream analysis
             * consumes the stream.
             */
            if (!dgtl::ir::ssa::
                    verify_generated_result_type_annotations(stream)
                        .empty()) {
                return false;
            }

            /*
             * DSL-originated IR has an additional ownership/lifetime
             * invariant. Other frontend languages are not forced through
             * this HIR-specific ownership check.
             */
            if (stream.get_frontend_language() ==
                    FrontendLanguage::DSL &&
                !verify_ownership_lifetimes(stream)) {
                return false;
            }

            return true;
        }
        catch (...) {
            /*
             * The bridge is a noexcept boundary. Analysis implementations
             * are not allowed to propagate exceptions through this ABI.
             */
            return false;
        }
    }

    /*
     * Analysis invalidation is intentionally a no-op at this bridge layer.
     *
     * Concrete analysis implementations own their cache/state lifecycle.
     * The bridge must not impose a global invalidation policy on them.
     */
    static void invalidate_analyses(
        const IRStream&,
        const dgtl::analysis::Analysis_Interface* = nullptr
    ) noexcept
    {
    }

    [[nodiscard]]
    static uint32_t select_analysis_tier(
        const IRStream& stream
    ) noexcept
    {
        const uint32_t complexity =
            stream.estimate_complexity_score();

        if (complexity < 100) {
            return 0u;
        }

        if (complexity < 500) {
            return 1u;
        }

        return 2u;
    }

private:

    [[nodiscard]]
    static bool verify_ownership_lifetimes(
        const IRStream& stream
    ) noexcept
    {
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node =
                stream.get_node_readonly(i);

            if (node.opcode != OpCode::OWNERSHIP_RELEASE) {
                continue;
            }

            const uint32_t id =
                bridge_validation::released_value_id(node);

            if (id == INVALID_SLOT) {
                return false;
            }

            const auto* info =
                stream.get_ownership_info(id);

            if (info == nullptr ||
                info->acquire_site == 0 ||
                info->release_site == 0) {
                return false;
            }
        }

        return true;
    }
};

} // namespace dgtl::ir

