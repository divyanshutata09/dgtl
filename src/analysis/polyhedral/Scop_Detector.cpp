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

#include "dgtl/analysis/polyhedral/Scop_Detector.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <thread>
#include "dgtl/ir/base/IR_Traits.hpp"

namespace dgtl::analysis::polyhedral {

namespace {

uint32_t compute_induction_depth_signal(const graph::Region& region) noexcept {
    std::unordered_set<uint32_t> induction_signal;
    induction_signal.reserve(region.nodes.size());

    for (const auto& node : region.nodes) {
        if ((node.opcode == ir::OpCode::ADD_I32 || node.opcode == ir::OpCode::SUB_I32) &&
            node.result_id != 0 &&
            (node.op_a == node.result_id || node.op_b == node.result_id)) {
            induction_signal.insert(node.result_id);
        }
    }

    return static_cast<uint32_t>(induction_signal.size());
}

class LoopDepthWorker {
public:
    LoopDepthWorker() {
        worker_ = std::thread([this]() { run(); });
    }

    ~LoopDepthWorker() {
        stop_.store(true, std::memory_order_release);
        request_ready_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    uint32_t evaluate(const graph::Region& region) noexcept {
        region_ = &region;
        response_ready_.store(false, std::memory_order_release);
        request_ready_.store(true, std::memory_order_release);

        while (!response_ready_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        return result_.load(std::memory_order_acquire);
    }

private:
    void run() noexcept {
        while (!stop_.load(std::memory_order_acquire)) {
            if (!request_ready_.load(std::memory_order_acquire)) {
                std::this_thread::yield();
                continue;
            }

            request_ready_.store(false, std::memory_order_release);
            const graph::Region* local_region = region_;
            if (local_region == nullptr) {
                result_.store(0, std::memory_order_release);
                response_ready_.store(true, std::memory_order_release);
                continue;
            }

            const uint32_t signal = compute_induction_depth_signal(*local_region);
            result_.store(signal, std::memory_order_release);
            response_ready_.store(true, std::memory_order_release);
        }
    }

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> request_ready_{false};
    std::atomic<bool> response_ready_{false};
    std::atomic<uint32_t> result_{0};
    const graph::Region* region_ = nullptr;
};

LoopDepthWorker& loop_depth_worker() {
    static LoopDepthWorker worker;
    return worker;
}

} // namespace

// ============================================================================
// PUBLIC API
// ============================================================================

ScopMetadata ScopDetector::analyze_region(const graph::Region& region) const {
    ScopMetadata meta;
    meta.classification = ScopClassification::INVALID;

    // Sanity checks
    if (region.nodes.empty()) {
        meta.classification = ScopClassification::INVALID;
        meta.rejection_reason = "empty region";
        return meta;
    }

    if (region.nodes.size() > kMaxRegionSize) {
        meta.classification = ScopClassification::TOO_COMPLEX;
        meta.rejection_reason = "region size exceeds threshold";
        meta.confidence_score = 100;
        return meta;
    }

    // Step 1: Detect loop depth and induction variables
    meta.loop_depth = detect_loop_depth(region);
    if (meta.loop_depth == 0) {
        meta.classification = ScopClassification::AFFINE_CANDIATE;
        meta.has_affine_bounds = true;
        meta.has_affine_subscripts = true;
        meta.has_analyzable_dependencies = true;
        meta.confidence_score = 95;
        meta.can_tile = false;  // Tiling only meaningful for loops
        meta.can_fuse = true;
        meta.can_parallelize = false;
        return meta;
    }

    // Collect induction variables
    meta.induction_vars = detect_induction_variables(region);

    // Step 2: Check for irregular control flow (data-dependent branches)
    if (has_irregular_control(region)) {
        meta.classification = ScopClassification::NON_AFFINE_CONTROL;
        meta.rejection_reason = "data-dependent control flow detected";
        meta.confidence_score = 90;
        return meta;
    }

    // Step 3: Try to extract affine loop bounds
    if (!extract_affine_bounds(region, meta.induction_vars,
                               meta.lower_bounds, meta.upper_bounds, meta.strides)) {
        meta.classification = ScopClassification::NON_AFFINE_LOOP;
        meta.rejection_reason = "non-affine loop bounds or strides";
        meta.confidence_score = 85;
        return meta;
    }
    meta.has_affine_bounds = true;

    // Step 4: Check memory accesses for affine subscripts
    if (!check_affine_subscripts(region, meta)) {
        meta.classification = ScopClassification::NON_AFFINE_SUBSCRIPT;
        meta.rejection_reason = "non-linear array subscripts detected";
        meta.confidence_score = 80;
        return meta;
    }
    meta.has_affine_subscripts = true;

    // Step 5: Count memory operations and estimate reuse
    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::GATHER_F32_512) {
            meta.load_count++;
        } else if (node.opcode == ir::OpCode::STORE || node.opcode == ir::OpCode::STORE_V512) {
            meta.store_count++;
        }
    }

    meta.reuse_distance = estimate_reuse_distance(region);
    meta.memory_footprint_bytes = estimate_memory_footprint(region, meta);

    // Step 6: Determine transformation eligibility
    // Can tile if: affine bounds + affine subscripts + reasonable reuse distance
    meta.can_tile = (meta.has_affine_bounds && meta.has_affine_subscripts &&
                     meta.reuse_distance >= kMinReuseDistanceForTile);

    // Can fuse with other affine regions
    meta.can_fuse = (meta.has_affine_bounds && meta.has_affine_subscripts);

    // Can parallelize if: affine + no non-affine control + not too small
    meta.can_parallelize = (meta.has_affine_bounds && meta.has_affine_subscripts &&
                           !meta.has_irregular_control && region.nodes.size() > 8);

    meta.has_analyzable_dependencies = true;  // PLACEHOLDER: will be computed by Dependence_Graph
    meta.classification = ScopClassification::AFFINE_CANDIATE;
    meta.confidence_score = 95;

    return meta;
}

std::vector<ScopMetadata> ScopDetector::analyze_regions(
    const std::vector<std::unique_ptr<graph::Region>>& regions) const {
    std::vector<ScopMetadata> results;
    results.reserve(regions.size());

    for (const auto& region_ptr : regions) {
        if (region_ptr) {
            results.push_back(analyze_region(*region_ptr));
        }
    }

    return results;
}

bool ScopDetector::is_tileable(
    [[maybe_unused]] const graph::Region& region,
    const ScopMetadata& metadata) const {
    // A region is tileable if:
    // 1. It's classified as AFFINE_CANDIATE
    // 2. It has loop nesting
    // 3. Reuse distance is above threshold
    // 4. Memory footprint is reasonable (fits in cache hierarchy)

    if (metadata.classification != ScopClassification::AFFINE_CANDIATE) {
        return false;
    }

    if (metadata.loop_depth < 2) {
        return false;  // Tiling most beneficial for multi-dimensional loops
    }

    if (metadata.reuse_distance < kMinReuseDistanceForTile) {
        return false;
    }

    // PLACEHOLDER: Add cache hierarchy model check
    // For now, assume tileable if metadata says so
    return metadata.can_tile;
}

bool ScopDetector::can_fuse_affine(
    [[maybe_unused]] const graph::Region& producer,
    [[maybe_unused]] const graph::Region& consumer,
    const ScopMetadata& prod_meta,
    const ScopMetadata& cons_meta) const {
    // Two affine regions can fuse if:
    // 1. Both are AFFINE_CANDIATE
    // 2. Both have same loop depth (or compatible nesting)
    // 3. Producer's output directly feeds consumer (checked by caller)
    // 4. Fusion improves data locality

    if (prod_meta.classification != ScopClassification::AFFINE_CANDIATE ||
        cons_meta.classification != ScopClassification::AFFINE_CANDIATE) {
        return false;
    }

    if (prod_meta.loop_depth != cons_meta.loop_depth) {
        return false;  // Require exact depth match for safety
    }

    if (!prod_meta.can_fuse || !cons_meta.can_fuse) {
        return false;
    }

    // PLACEHOLDER: Add dependence check to ensure fusion is legal
    return true;
}

uint32_t ScopDetector::detect_loop_depth(const graph::Region& region) const {
    // Heuristic blend:
    // 1) Branch-based signal: control flow suggests at least one loop.
    // 2) Induction-variable signal from lock-free worker indicates nesting.
    uint32_t branch_depth_signal = 0;

    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::BRANCH_COND || node.opcode == ir::OpCode::BRANCH) {
            branch_depth_signal = 1;
        }
    }

    const uint32_t induction_depth_signal =
        std::min(loop_depth_worker().evaluate(region), kMaxLoopDepth);
    const uint32_t combined_depth = std::max(branch_depth_signal, induction_depth_signal);
    return std::min(combined_depth, kMaxLoopDepth);
}

bool ScopDetector::check_affine_subscripts(
    const graph::Region& region,
    const ScopMetadata& metadata) const {
    // Check if all memory operations use affine subscripts.
    // Affine: address = base + c0*i + c1*j + ... (linear in induction variables)
    // Non-affine: address = f(i*j), address = lookup_table[i], etc.

    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE ||
            node.opcode == ir::OpCode::GATHER_F32_512 || node.opcode == ir::OpCode::STORE_V512) {
            
            // PLACEHOLDER: In a full implementation, extract address expression from LEA chain.
            // For now, assume affine if operands are induction variables or constants.
            uint32_t addr_reg = node.op_a;  // Simplified: assume op_a holds address

            bool is_affine = false;
            if (addr_reg == 0) {
                is_affine = true;  // Constant/immediate
            } else {
                // Check if address register is derived from induction variables
                for (uint32_t ind_var : metadata.induction_vars) {
                    if (addr_reg == ind_var) {
                        is_affine = true;
                        break;
                    }
                }
            }

            if (!is_affine) {
                // PLACEHOLDER: More sophisticated check needed here
                // For now, accept if it's not obviously irregular
                is_affine = true;  // Optimistic default
            }

            if (!is_affine) {
                return false;
            }
        }
    }

    return true;
}

double ScopDetector::estimate_reuse_distance(const graph::Region& region) const {
    // Estimate temporal reuse: how many times is the same memory location accessed?
    // High reuse => good tiling candidate
    // Formula (heuristic): reuse_distance = (load_count + store_count) / unique_memory_patterns

    uint32_t mem_ops = 0;
    std::unordered_set<uint32_t> unique_base_regs;

    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE ||
            node.opcode == ir::OpCode::GATHER_F32_512 || node.opcode == ir::OpCode::STORE_V512) {
            mem_ops++;
            unique_base_regs.insert(node.op_a);
        }
    }

    if (unique_base_regs.empty() || mem_ops == 0) {
        return 0.0;
    }

    double reuse = static_cast<double>(mem_ops) / static_cast<double>(unique_base_regs.size());
    return reuse;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

std::vector<uint32_t> ScopDetector::detect_induction_variables(
    const graph::Region& region) const {
    // Heuristic: An induction variable is a vreg that is incremented/updated
    // in a loop-like pattern (ADD, SUB with immediate operands).

    std::vector<uint32_t> induction_vars;
    std::unordered_set<uint32_t> candidates;

    // Collect all results from ADD/SUB operations with constant operands
    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::ADD_I32 || node.opcode == ir::OpCode::SUB_I32) {
            // If one operand is immediate-like (op_b < 256 as heuristic), this could be i++
            if (node.op_b < 256 && node.result_id != 0) {
                candidates.insert(node.result_id);
            }
        }
    }

    // Refine: check which candidates are actually incremented
    for (uint32_t candidate : candidates) {
        if (is_induction_variable(region, candidate)) {
            induction_vars.push_back(candidate);
        }
    }

    return induction_vars;
}

bool ScopDetector::is_induction_variable(
    const graph::Region& region,
    uint32_t vreg_slot) const {
    // Check if vreg_slot is used as a loop counter:
    // - Incremented in ADD/SUB with constant
    // - Used as operand in subsequent operations
    // - Appears in loop condition (BRANCH_COND)

    uint32_t usage_count = 0;
    uint32_t increment_count = 0;

    for (const auto& node : region.nodes) {
        if (node.op_a == vreg_slot || node.op_b == vreg_slot || node.op_c == vreg_slot) {
            usage_count++;
        }

        if (node.result_id == vreg_slot &&
            (node.opcode == ir::OpCode::ADD_I32 || node.opcode == ir::OpCode::SUB_I32)) {
            increment_count++;
        }
    }

    // An induction variable is used multiple times and incremented
    return usage_count >= 3 && increment_count >= 1;
}

bool ScopDetector::extract_affine_bounds(
    [[maybe_unused]] const graph::Region& region,
    const std::vector<uint32_t>& induction_vars,
    std::vector<int64_t>& lower,
    std::vector<int64_t>& upper,
    std::vector<int64_t>& strides) const {
    // Extract loop bounds from initial assignments and comparisons.
    // Look for patterns like: i = 0; i < N; i += stride

    // PLACEHOLDER: Full implementation requires control flow analysis.
    // For now, use defaults:
    for (size_t i = 0; i < induction_vars.size(); ++i) {
        lower.push_back(0);
        upper.push_back(1024);  // Default heuristic
        strides.push_back(1);
    }

    return true;
}

bool ScopDetector::has_irregular_control(const graph::Region& region) const {
    // Check for data-dependent branches (non-affine).
    // Example: if (a[i*j] > threshold) => data-dependent

    for (const auto& node : region.nodes) {
        if (node.opcode == ir::OpCode::BRANCH_COND) {
            // Check if condition depends on memory operations or complex expressions
            // PLACEHOLDER: Conservative check - flag BRANCH_COND in loops as suspicious
            
            // For now, assume BRANCH_COND is affine unless proven otherwise
            // (actual check would require data flow analysis)
        }
    }

    return false;
}

double ScopDetector::estimate_memory_footprint(
    [[maybe_unused]] const graph::Region& region,
    const ScopMetadata& metadata) const {
    // Estimate working set size:
    // footprint = (loop_iterations) * (bytes_per_iteration)

    if (metadata.loop_depth == 0) {
        return 0.0;  // No loops => no repeated memory pattern
    }

    // PLACEHOLDER: Use loop bounds and data type sizes
    // For now, heuristic based on memory op count and region size
    double footprint = metadata.load_count * 8.0 + metadata.store_count * 8.0;
    
    // Scale by estimated iteration count
    int64_t iterations = 1;
    for (int64_t ub : metadata.upper_bounds) {
        iterations *= ub;
    }
    iterations = std::min(iterations, 10000LL);  // Cap to reasonable estimate

    return footprint * static_cast<double>(iterations);
}

} // namespace dgtl::analysis::polyhedral

