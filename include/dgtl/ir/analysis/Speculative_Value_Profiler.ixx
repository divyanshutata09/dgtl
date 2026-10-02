/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

module;

#include <cstdlib>

#include <atomic>
#include <stdexcept>
#include <vector>
#include <string>

export module dgtl.ir.analysis.speculative_value_profiler;

import dgtl.arena;
import dgtl.ir_node;
import dgtl.ir_enums;

import dgtl.ir.stream;
import dgtl.ir.analysis.cost_model;
import dgtl.jit.executor;

export namespace dgtl::ir::analysis {

class DGTLValueProfiler {
    dgtl::runtime::DGTLExecutor& executor_;
    AICostModel& ai_model;
    core::DGTL_Arena* global_arena = nullptr;

public:
    DGTLValueProfiler(dgtl::runtime::DGTLExecutor& j,
                      AICostModel& ai,
                      core::DGTL_Arena* arena = nullptr);

    ~DGTLValueProfiler();

    void execute(ir::IRStream& stream);

private:
    // FIXED: Template deduction bridge solves the incomplete type visibility error losslessly
    template <typename FeedbackT>
    [[nodiscard]] static bool should_attempt_specialization(
        float estimated_cost,
        const FeedbackT& feedback) noexcept;

    bool specialize_on_value(ir::IRStream& stream, uint32_t guard_idx);
    void optimize_with_constant_context(ir::IRStream& s);
};

} // namespace dgtl::ir::analysis

export namespace dgtl::analysis {
    using namespace dgtl::ir::analysis;
    using DGTLValueProfiler = dgtl::ir::analysis::DGTLValueProfiler;
    using ValueProfiler = dgtl::ir::analysis::DGTLValueProfiler;
}