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
 
/*
 * DGTL analysis infrastructure contract.
 * Analysis implementations are governed by this common analysis contract; this layer defines
 * explicit identity, dependency and invalidation semantics.
 */
module;
#include <cstdint>
#include <string_view>

export module dgtl.ir_analysis;

import dgtl.ir_contracts;

export namespace dgtl::ir::analysis {

enum class AnalysisKind : std::uint16_t {
    CFG,
    Dominance,
    LoopInfo,
    SSA,
    Alias,
    MemoryEffects,
    TypeInfo,
    Profile,
    TargetIndependentCost
};

struct AnalysisStamp {
    std::uint64_t ir_epoch{0};
    std::uint64_t semantic_hash{0};
    contract::Preservation dependencies{contract::Preservation::None};
};

struct AnalysisKey {
    AnalysisKind kind{};
    std::uint64_t function_id{0};
    friend constexpr bool operator==(const AnalysisKey&, const AnalysisKey&) = default;
};

struct AnalysisInvalidation {
    contract::Preservation changed{contract::Preservation::None};
    std::uint64_t new_epoch{0};
};

[[nodiscard]] constexpr bool invalidated(const AnalysisStamp& stamp,
                                         const AnalysisInvalidation& change) noexcept {
    return static_cast<std::uint32_t>(stamp.dependencies & change.changed) != 0;
}

} // namespace dgtl::ir::analysis


