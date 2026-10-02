
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
 * DGTL pass contract.
 * Concrete passes implement this common protocol and are governed by the same contracts
 * when migrating them into the modern pipeline manager.
 */
module;
#include <cstdint>
#include <string_view>
export module dgtl.ir_pass_contract;
import dgtl.ir_contracts;

export namespace dgtl::ir::pipeline {

struct PassResult {
    bool changed{false};
    bool verified{false};
    std::uint64_t before_hash{0};
    std::uint64_t after_hash{0};
};

struct PassContract {
    std::string_view name{};
    contract::Phase minimum_phase{contract::Phase::CanonicalHir};
    contract::Preservation preserves{contract::Preservation::None};
    contract::Preservation invalidates{contract::Preservation::All};
    bool deterministic{true};
    bool transactional{true};
};

} // namespace dgtl::ir::pipeline


