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

#include <cstdlib>

#include <string_view>
#include <cstdint>

// FIXED FOR 3-TIER HIERARCHY: Belong to the core intermediate macro layer partition space
export module dgtl.ir.analysis.analysis_interface;

export namespace dgtl::ir::analysis {

/**
 * @class Analysis_Interface
 * @brief Abstract base class for all IR analysis passes.
 * * Defines the standard interface for analyses that can be run over IRStreams.
 * Analyses are lazily computed and cached; they invalidate on stream mutation.
 * All analyses must be deterministic and re-entrant.
 */
class Analysis_Interface {
public:
    virtual ~Analysis_Interface() = default;

    /**
     * @brief Initialize the analysis pass.
     * Called once before analyzing any streams.
     */
    virtual void initialize() noexcept = 0;

    /**
     * @brief Invalidate cached analysis results.
     * Called when the input stream is mutated.
     */
    virtual void invalidate() noexcept = 0;

    /**
     * @brief Get the human-readable name of this analysis.
     * @return Analysis name (must be a compile-time constant string)
     */
    [[nodiscard]] virtual std::string_view get_name() const noexcept = 0;

    /**
     * @brief Get the version number of this analysis.
     * Bumped when algorithm or output format changes.
     * @return Version number (e.g., 0x010000 for v1.0.0)
     */
    [[nodiscard]] virtual uint64_t get_version() const noexcept = 0;

    /**
     * @brief Check if the analysis results are currently valid.
     * @return true if results reflect current input state, false if stale
     */
    [[nodiscard]] virtual bool is_valid() const noexcept = 0;

    /**
     * @brief Check if this analysis is deterministic.
     * Deterministic analyses must produce identical output for identical input.
     * @return true if deterministic, false if result varies
     */
    [[nodiscard]] virtual bool is_deterministic() const noexcept {
        return true;
    }

    Analysis_Interface(const Analysis_Interface&) = delete;
    Analysis_Interface& operator=(const Analysis_Interface&) = delete;

protected:
    Analysis_Interface() = default;
};

} // namespace dgtl::ir::analysis

export namespace dgtl::analysis {
    using namespace dgtl::ir::analysis;
    using Analysis_Interface = dgtl::ir::analysis::Analysis_Interface;
}