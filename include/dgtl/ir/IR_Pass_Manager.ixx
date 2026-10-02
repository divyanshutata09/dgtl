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

module;

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.ir_pass_manager;

import dgtl.ir.stream;
import dgtl.ir.verifier;
import dgtl.ir_contracts;
import dgtl.ir_pass_contract;

export namespace dgtl::ir::pipeline {

struct PassExecutionRecord {
    std::string name;
    bool changed{false};
    bool verified{false};
    std::uint64_t before_hash{0};
    std::uint64_t after_hash{0};
};

class PassManager {
    struct RegisteredPass {
        PassContract contract{};
        std::function<bool(IRStream&)> execute{};
    };
    std::vector<RegisteredPass> passes_;
    std::vector<PassExecutionRecord> history_;
    contract::Phase phase_{contract::Phase::CanonicalHir};
    IRVerifier verifier_{};

public:
    void set_phase(contract::Phase p) noexcept { phase_=p; }
    [[nodiscard]] contract::Phase phase() const noexcept { return phase_; }

    bool register_pass(PassContract contract, std::function<bool(IRStream&)> fn) {
        if (contract.name.empty() || !fn) return false;
        if (static_cast<unsigned>(contract.minimum_phase) > static_cast<unsigned>(phase_)) return false;
        for (const auto& p : passes_) if (p.contract.name == contract.name) return false;
        passes_.push_back({contract,std::move(fn)}); return true;
    }

    void clear_history() noexcept { history_.clear(); }
    [[nodiscard]] const std::vector<PassExecutionRecord>& history() const noexcept { return history_; }

    bool run(IRStream& stream, bool verify_each_pass=true) {
        history_.clear();
        for (auto& p : passes_) {
            const auto before = stream.generate_logic_hash();
            bool changed=false;
            if (p.contract.transactional) {
                stream.begin_transaction();
                changed = p.execute(stream);
                if (verify_each_pass && !verifier_.is_valid(stream)) { stream.abort_transaction(); return false; }
                if (changed) stream.commit_transaction(); else stream.abort_transaction();
            } else {
                changed=p.execute(stream);
                if (verify_each_pass && !verifier_.is_valid(stream)) return false;
            }
            const auto after=stream.generate_logic_hash();
            history_.push_back({std::string(p.contract.name),changed,(!verify_each_pass || verifier_.is_valid(stream)),before,after});
        }
        return true;
    }
};
}


