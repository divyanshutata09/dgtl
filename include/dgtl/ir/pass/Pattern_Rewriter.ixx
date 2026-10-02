
/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <string_view>

export module dgtl.ir.rewrite;

// Local core layer leaf sibling dependencies
import dgtl.ir.stream;
import dgtl.ir.gen;
import dgtl.ir.ssa;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_schema;
import dgtl.op_traits;

export namespace dgtl::ir::rewrite {

enum class AnalysisInvalidation : uint32_t {
    None = 0u,
    DominatorTree = 1u << 0,
    LoopInfo = 1u << 1,
    Liveness = 1u << 2,
    AliasAnalysis = 1u << 3,
    MemoryDependence = 1u << 4,
    ValueNumbering = 1u << 5,
    All = 0xFFFFFFFFu,
};

[[nodiscard]] constexpr AnalysisInvalidation operator|(
    AnalysisInvalidation lhs,
    AnalysisInvalidation rhs) noexcept
{
    return static_cast<AnalysisInvalidation>(
        static_cast<uint32_t>(lhs) |
        static_cast<uint32_t>(rhs)
    );
}

[[nodiscard]] constexpr AnalysisInvalidation operator&(
    AnalysisInvalidation lhs,
    AnalysisInvalidation rhs) noexcept
{
    return static_cast<AnalysisInvalidation>(
        static_cast<uint32_t>(lhs) &
        static_cast<uint32_t>(rhs)
    );
}

constexpr AnalysisInvalidation& operator|=(
    AnalysisInvalidation& lhs,
    AnalysisInvalidation rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

enum class MutationKind : uint8_t {
    Replace,
    Erase,
};

struct MutationRecord {
    MutationKind kind = MutationKind::Replace;
    uint32_t index = 0;
    dgtl::ir::IRNode before{};
    dgtl::ir::IRNode after{};
};

struct RewriteSummary {
    bool changed = false;
    bool structural_change = false;
    uint32_t mutation_count = 0;
    uint32_t invalidation_mask =
        static_cast<uint32_t>(AnalysisInvalidation::None);

    std::vector<uint32_t> touched_indices;
};

class PatternRewriter {
public:
    explicit PatternRewriter(dgtl::ir::IRStream& stream) noexcept
        : stream_(stream)
    {
    }

    ~PatternRewriter()
    {
        if (active_ && stream_.is_active_transaction()) {
            stream_.rollback_transaction();
        }
    }

    void begin()
    {
        if (active_) {
            return;
        }

        mutations_.clear();
        summary_ = RewriteSummary{};

        stream_.begin_transaction();
        active_ = true;
    }

    [[nodiscard]] bool is_active() const noexcept
    {
        return active_;
    }

    [[nodiscard]] bool replace_node(
        uint32_t idx,
        const dgtl::ir::IRNode& replacement)
    {
        if (!active_ || idx >= stream_.size()) {
            return false;
        }

        /*
         * The canonical HIR schema is the single semantic authority
         * for operation validity.
         *
         * dgtl.ir.gen provides generated data, while dgtl.ir_schema
         * provides the canonical access/validation layer.
         */
        if (!dgtl::ir::schema::is_canonical(replacement.opcode)) {
            return false;
        }

        const dgtl::ir::IRNode before =
            stream_.get_node_readonly(idx);

        write_shadow(idx, replacement);

        MutationRecord rec{};
        rec.kind = MutationKind::Replace;
        rec.index = idx;
        rec.before = before;
        rec.after = replacement;

        mutations_.push_back(rec);

        summary_.changed = true;
        ++summary_.mutation_count;
        summary_.touched_indices.push_back(idx);

        summary_.invalidation_mask |=
            static_cast<uint32_t>(
                AnalysisInvalidation::ValueNumbering
            );

        const bool operands_changed =
            before.op_a != replacement.op_a ||
            before.op_b != replacement.op_b ||
            before.op_c != replacement.op_c;

        if (before.opcode != replacement.opcode ||
            before.result_id != replacement.result_id)
        {
            summary_.structural_change = true;

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::DominatorTree
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::LoopInfo
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::Liveness
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::AliasAnalysis
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::MemoryDependence
                );
        }
        else if (operands_changed)
        {
            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::Liveness
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::AliasAnalysis
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::MemoryDependence
                );
        }

        return true;
    }

    [[nodiscard]] bool erase_node(uint32_t idx)
    {
        if (!active_ || idx >= stream_.size()) {
            return false;
        }

        dgtl::ir::IRNode tombstone{};

        tombstone.opcode = dgtl::ir::OpCode::NOP;
        tombstone.flags = dgtl::ir::Flags::NONE;
        tombstone.result_id = dgtl::ir::INVALID_SLOT;
        tombstone.op_a = 0;
        tombstone.op_b = 0;
        tombstone.op_c = 0;
        tombstone.logic_flags = 0;

        tombstone.refresh_intent_hash();

        const bool ok = replace_node(idx, tombstone);

        if (ok && !mutations_.empty()) {
            mutations_.back().kind = MutationKind::Erase;

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::DominatorTree
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::LoopInfo
                );

            summary_.invalidation_mask |=
                static_cast<uint32_t>(
                    AnalysisInvalidation::Liveness
                );
        }

        return ok;
    }

    [[nodiscard]] bool commit(bool validate_shadow = true)
    {
        if (!active_) {
            return true;
        }

        bool ok = true;

        if (validate_shadow) {
            ok = stream_.validate_shadow_integrity();
        }

        if (ok) {
            stream_.commit_transaction();
        }
        else {
            stream_.rollback_transaction();
            mutations_.clear();
            summary_ = RewriteSummary{};
        }

        active_ = false;
        return ok;
    }

    void rollback() noexcept
    {
        if (!active_) {
            return;
        }

        if (stream_.is_active_transaction()) {
            stream_.rollback_transaction();
        }

        mutations_.clear();
        summary_ = RewriteSummary{};
        active_ = false;
    }

    [[nodiscard]] const std::vector<MutationRecord>&
    mutations() const noexcept
    {
        return mutations_;
    }

    [[nodiscard]] const RewriteSummary&
    summary() const noexcept
    {
        return summary_;
    }

    [[nodiscard]] uint32_t
    analysis_invalidation_mask() const noexcept
    {
        return summary_.invalidation_mask;
    }

    [[nodiscard]] RewriteSummary
    take_summary() noexcept
    {
        RewriteSummary out = std::move(summary_);
        summary_ = RewriteSummary{};
        return out;
    }

    [[nodiscard]] std::vector<std::string>
    verify_generated_result_types() const
    {
        const auto ssa_errors =
            dgtl::ir::ssa::verify_generated_result_type_annotations(
                stream_
            );

        std::vector<std::string> out;
        out.reserve(ssa_errors.count);

        for (size_t i = 0; i < ssa_errors.count; ++i) {
            out.emplace_back(ssa_errors.errors[i]);
        }

        return out;
    }

private:
    void write_shadow(
        uint32_t idx,
        const dgtl::ir::IRNode& node) noexcept
    {
        dgtl::ir::IRAtom& atom =
            stream_.get_shadow_atom(idx);

        dgtl::ir::IRCore& core =
            stream_.get_shadow_core(idx);

        atom.opcode = node.opcode;
        atom.flags = node.flags;

        core.result_id = node.result_id;
        core.op_a = node.op_a;
        core.op_b = node.op_b;
        core.op_c = node.op_c;
        core.intent_hash = node.intent_hash;
        core.logic_flags = node.logic_flags;

        core.aux_flags = 0;
        core.debug_id = 0;
        core.epoch = 0;
    }

    dgtl::ir::IRStream& stream_;

    bool active_ = false;

    std::vector<MutationRecord> mutations_;

    RewriteSummary summary_{};
};

} // namespace dgtl::ir::rewrite

