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

#pragma once

#include <memory>
#include <vector>
#include <string>

namespace dgtl::pipeline {
    class DGTLOptimizationPass;
    class DGTLoptManager;
}

namespace dgtl {
namespace pass {

/**
 * @class Pass_Composer
 * @brief Composable builder for optimization passes.
 * Provides a fluent interface for collecting and orchestrating passes
 * before injecting them into a DGTLoptManager.
 */
class Pass_Composer {
public:
    Pass_Composer() = default;
    ~Pass_Composer() = default;

    Pass_Composer(const Pass_Composer&) = delete;
    Pass_Composer& operator=(const Pass_Composer&) = delete;

    /**
     * @brief Add a pass to the composition.
     * @param pass A unique_ptr<DGTLOptimizationPass> to add
     */
    void add_pass(std::unique_ptr<pipeline::DGTLOptimizationPass> pass) noexcept {
        if (pass) {
            passes_.push_back(std::move(pass));
        }
    }

    /**
     * @brief Inject all composed passes into a manager.
     * Transfers ownership of passes to the manager and clears internal state.
     * @param manager The DGTLoptManager to receive passes
     */
    void compose_into_manager(pipeline::DGTLoptManager& manager) noexcept;

    /**
     * @brief Get the number of passes currently composed.
     * @return Count of passes ready for injection
     */
    [[nodiscard]] size_t pass_count() const noexcept {
        return passes_.size();
    }

    /**
     * @brief Clear all accumulated passes.
     */
    void clear() noexcept {
        passes_.clear();
    }

private:
    std::vector<std::unique_ptr<pipeline::DGTLOptimizationPass>> passes_;
};

} // namespace pass
} // namespace dgtl
