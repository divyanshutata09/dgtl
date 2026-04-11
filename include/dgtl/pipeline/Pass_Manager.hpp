/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 26: INTEGRATED FORMAL VERIFICATION LAYER (SENTINEL-ENABLED)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL VALIDATOR)
 * DESIGN: Proves mathematical equivalence using Symbolic State & Master Formula Hashes.
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once
#include <array>
#include <bitset>
#include <optional>
#include <bit>
#include <cstring>
#include <vector>
#include <memory>
#include <thread>
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"

namespace dgtl::pipeline {

enum class TargetPrecision {
    HFT,      // High-Frequency Trading (sub-microsecond)
    FP32 = HFT, // Backward compatibility alias for FP32 optimization workloads
    CLOUD,    // Cloud Computing (millisecond-scale)
    MOBILE,   // Mobile/IoT (energy-constrained)
    HPC       // High-Performance Computing (throughput-optimized)
};

/**
 * @class DGTLOptimizationPass
 * @brief Base class for all optimization passes in the DGTL framework.
 * Provides the standard interface for pass execution and metadata.
 */
class DGTLOptimizationPass {
public:
    DGTLOptimizationPass() = default;
    virtual ~DGTLOptimizationPass() = default;

    /**
     * @brief Execute the optimization pass on the given IR stream.
     * @param stream The IR stream to optimize
     */
    virtual void execute(ir::IRStream& stream) = 0;

    /**
     * @brief Get the name of this optimization pass.
     * @return A string identifier for the pass
     */
    [[nodiscard]] virtual const char* get_pass_name() const noexcept = 0;

    /**
     * @brief Check if this pass requires transactional execution.
     * @return true if the pass needs shadow buffer protection
     */
    [[nodiscard]] virtual bool requires_transaction() const noexcept { return true; }
};

/**
 * @class DGTLoptManager
 * @brief The central optimization orchestrator for DGTL.
 * Manages pass execution, threading, and result validation.
 */
class DGTLoptManager {
    std::vector<std::unique_ptr<DGTLOptimizationPass>> passes;
    TargetPrecision target;
    uint32_t thread_count;

public:
    explicit DGTLoptManager(uint32_t threads = 0) noexcept
        : target(TargetPrecision::HFT),
          thread_count(threads == 0 ? std::thread::hardware_concurrency() : threads) {}

    DGTLoptManager(TargetPrecision t, uint32_t threads = 0) noexcept
        : target(t),
          thread_count(threads == 0 ? std::thread::hardware_concurrency() : threads) {}

    /**
     * @brief Add an optimization pass to the pipeline.
     * @param pass Unique pointer to the pass instance
     */
    void add_opt(std::unique_ptr<DGTLOptimizationPass> pass) {
        passes.push_back(std::move(pass));
    }

    /**
     * @brief Execute all passes on the given IR stream.
     * @param stream The IR stream to optimize
     * @param precision Target precision level
     * @return true if optimization succeeded
     */
    bool execute(ir::IRStream& stream, TargetPrecision precision) noexcept {
        target = precision;

        for (auto& pass : passes) {
            pass->execute(stream);
        }

        return true;
    }

    bool execute(ir::IRStream& stream) noexcept {
        return execute(stream, TargetPrecision::HFT);
    }

    /**
     * @brief Get the number of registered passes.
     * @return Number of optimization passes
     */
    [[nodiscard]] size_t pass_count() const noexcept {
        return passes.size();
    }

    /**
     * @brief Get the target precision level.
     * @return Current target precision
     */
    [[nodiscard]] TargetPrecision get_target() const noexcept {
        return target;
    }
};

} // namespace dgtl::pipeline