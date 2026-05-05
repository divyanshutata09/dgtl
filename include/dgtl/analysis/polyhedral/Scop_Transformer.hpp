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
 * DGTL P0-003: SCoP Transformer
 *
 * ScopTransformer is the core polyhedral transform engine. It takes the
 * annotated ExecutionGraph produced by ScopDetector (P0-002) and the
 * PolyhedralConfig from the pipeline, applies legal affine transforms
 * (tiling, fusion, parallelization), and produces a PolyhedralSchedule
 * consumed by the GPU emitter.
 *
 * TRANSFORMATION PIPELINE:
 *
 *   ExecutionGraph (annotated)
 *         │
 *         ▼ [1] Profitability filter
 *   Candidate regions (ScopAffinity::AFFINE_CANDIDATE + can_tile/can_parallelize)
 *         │
 *         ▼ [2] Tile size selection (auto or user-specified)
 *   Tile descriptors per dimension
 *         │
 *         ▼ [3] Loop fusion (safe mode: only when deps allow; aggressive: across dep-free groups)
 *   Fused region groups
 *         │
 *         ▼ [4] Grid/block mapping
 *   KernelLaunchParams per TiledRegion
 *         │
 *         ▼
 *   PolyhedralSchedule
 *
 * LEGALITY INVARIANTS:
 *   - Only AFFINE_CANDIDATE regions with can_tile=true are tiled.
 *   - Fusion is gated by the dependency graph: two regions may fuse only when
 *     no cross-region use-def edge crosses the fusion boundary in a direction
 *     that would invert execution order.
 *   - Regions with irregular control flow (can_parallelize=false) are passed
 *     through unchanged.
 *   - Branch-target correctness: the IRNodes inside a TiledRegion are emitted
 *     in sequential order inside the loop body. Branch targets within a region
 *     continue to resolve via sequential node index, not modified.
 *
 * TILE SIZE POLICY:
 *   tile_size == 0 (auto):
 *     Select via cache-line heuristic:
 *       1-D loops → 256 (warp-aligned for NVIDIA, wavefront for AMD)
 *       2-D loops → 16×16 = 256 threads (standard matrix tile)
 *       deeper    → 8×8×... until product ≤ 1024 (hardware thread limit)
 *   tile_size > 0: use exactly as specified (user has hardware knowledge)
 *
 * SPEEDUP ESTIMATION:
 *   estimated_speedup = min(parallelism_factor, memory_factor)
 *   parallelism_factor = total_iters / block_dim_x  (Amdahl-bounded)
 *   memory_factor      = 1.0 + reuse_distance * 0.3  (cache-reuse benefit)
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <span>

#include "dgtl/core/Context.hpp"
#include "dgtl/graph/Execution_Graph.hpp"
#include "dgtl/analysis/polyhedral/Scop_Detector.hpp"
#include "dgtl/analysis/polyhedral/Polyhedral_Schedule.hpp"

namespace dgtl::analysis::polyhedral {

// ============================================================
// Internal helpers (private to this header's impl)
// ============================================================

namespace detail {

// Hardware-tuned tile size selection.
// Returns tile_size per-dimension for loop_depth dimensions.
[[nodiscard]] inline std::vector<uint32_t> select_tile_sizes(
    uint32_t loop_depth,
    uint32_t user_tile_size) noexcept
{
    std::vector<uint32_t> tiles(loop_depth > 0 ? loop_depth : 1, 0u);

    if (user_tile_size > 0) {
        // User override: replicate across all dimensions.
        for (auto& t : tiles) t = user_tile_size;
        return tiles;
    }

    // Auto-selection: product of tile dimensions must not exceed 1024 (GPU thread limit).
    constexpr uint32_t kMaxThreadsPerBlock = 1024;
    constexpr uint32_t kWarpSize           = 32;   // NVIDIA warp / AMD wavefront base
    constexpr uint32_t kDefaultBlock1D     = 256;  // Saturates L1 TLB on most GPUs
    constexpr uint32_t kDefaultBlock2D     = 16;   // 16×16 = 256, standard matrix tile

    switch (loop_depth) {
        case 0:
        case 1:
            tiles[0] = kDefaultBlock1D;
            break;
        case 2:
            tiles[0] = kDefaultBlock2D;
            tiles[1] = kDefaultBlock2D;
            break;
        default: {
            // For depth > 2: choose equal sizes with product ≤ 1024.
            // Start from 8 and double until we exceed the limit.
            uint32_t per_dim = kWarpSize;
            uint32_t product = 1u;
            for (uint32_t d = 0; d < loop_depth; ++d) {
                product *= per_dim;
            }
            while (product > kMaxThreadsPerBlock && per_dim > 1) {
                per_dim /= 2;
                product = 1;
                for (uint32_t d = 0; d < loop_depth; ++d) product *= per_dim;
            }
            for (auto& t : tiles) t = per_dim;
            break;
        }
    }
    return tiles;
}

// Compute grid dimension given total extent, tile size, and stride.
[[nodiscard]] inline uint32_t compute_grid_dim(
    int64_t lower, int64_t upper, int64_t stride, uint32_t tile_size) noexcept
{
    if (tile_size == 0 || stride <= 0) return 1u;
    const int64_t total_iters = (upper > lower) ? ((upper - lower + stride - 1) / stride) : 0;
    if (total_iters <= 0) return 1u;
    const int64_t num_tiles = (total_iters + static_cast<int64_t>(tile_size) - 1)
                              / static_cast<int64_t>(tile_size);
    return static_cast<uint32_t>(std::max(int64_t{1}, num_tiles));
}

// Extract LoopDimensions for a region.
// Uses ScopMetadata bounds if available; otherwise synthesizes from loop_depth.
[[nodiscard]] inline std::vector<LoopDimension> build_loop_dimensions(
    const graph::Region& region,
    const ScopMetadata& meta,
    const std::vector<uint32_t>& tile_sizes)
{
    const uint32_t depth = meta.loop_depth > 0 ? meta.loop_depth : 1u;
    std::vector<LoopDimension> dims;
    dims.reserve(depth);

    for (uint32_t d = 0; d < depth; ++d) {
        LoopDimension dim;

        // Use extracted affine bounds if available; otherwise use heuristic defaults.
        if (d < meta.lower_bounds.size()) {
            dim.lower = meta.lower_bounds[d];
        }
        if (d < meta.upper_bounds.size()) {
            dim.upper = meta.upper_bounds[d];
        } else {
            // Estimate upper bound from region node count — conservative heuristic.
            dim.upper = static_cast<int64_t>(region.nodes.size());
        }
        if (d < meta.strides.size()) {
            dim.stride = meta.strides[d] > 0 ? meta.strides[d] : 1;
        }
        if (d < meta.induction_vars.size()) {
            dim.induction_var_slot = meta.induction_vars[d];
        }

        dim.tile_size   = (d < tile_sizes.size()) ? tile_sizes[d] : tile_sizes.back();
        dim.parallelize = meta.can_parallelize;

        dims.push_back(dim);
    }
    return dims;
}

// Compute KernelLaunchParams from finalized LoopDimensions.
[[nodiscard]] inline KernelLaunchParams build_launch_params(
    const std::vector<LoopDimension>& dims) noexcept
{
    KernelLaunchParams lp;

    if (dims.empty()) return lp;

    // Outermost parallel dimension → grid_dim_x, tile_size → block_dim_x
    const LoopDimension& d0 = dims[0];
    lp.block_dim_x = (d0.tile_size > 0) ? d0.tile_size : 256u;
    lp.grid_dim_x  = compute_grid_dim(d0.lower, d0.upper, d0.stride, lp.block_dim_x);

    // Second parallel dimension → grid_dim_y, block_dim_y
    if (dims.size() >= 2) {
        const LoopDimension& d1 = dims[1];
        lp.block_dim_y = (d1.tile_size > 0) ? d1.tile_size : 1u;
        lp.grid_dim_y  = compute_grid_dim(d1.lower, d1.upper, d1.stride, lp.block_dim_y);
    }

    // Shared memory: estimate based on tile volume × 4 bytes (int32).
    // Keep within 48 KB (safe for sm_80+).
    constexpr uint32_t kMaxSharedMem = 48 * 1024;
    const uint32_t tile_vol = lp.block_dim_x * lp.block_dim_y;
    lp.shared_mem_bytes = std::min(tile_vol * 4u, kMaxSharedMem);

    return lp;
}

// Profitability estimate: speedup from parallelizing this region.
[[nodiscard]] inline double estimate_speedup(
    const ScopMetadata& meta,
    const KernelLaunchParams& launch) noexcept
{
    // Parallelism factor: how many threads we launch vs. scalar.
    const double thread_count = static_cast<double>(launch.grid_dim_x)
                              * static_cast<double>(launch.grid_dim_y)
                              * static_cast<double>(launch.block_dim_x)
                              * static_cast<double>(launch.block_dim_y);
    const double parallelism_factor = std::min(thread_count, 32768.0);  // Bounded

    // Cache-reuse benefit based on reuse distance from ScopDetector.
    const double memory_factor = 1.0 + meta.reuse_distance * 0.3;

    return std::min(parallelism_factor, memory_factor * 4.0);
}

// Legality check for fusing two adjacent TiledRegions.
// Two regions can fuse when no output of region_a is an input of region_a's
// successor in a way that would be violated by merging their loop bodies.
// Conservatively: allow fusion only when neither region has backward-flowing
// dependencies and they share no inter-region def-use edges.
[[nodiscard]] inline bool can_fuse_regions(
    const graph::Region& a,
    const graph::Region& b) noexcept
{
    // Collect result_ids defined in a.
    std::unordered_set<uint32_t> defs_a;
    defs_a.reserve(a.nodes.size());
    for (const auto& n : a.nodes) {
        if (n.result_id != dgtl::ir::INVALID_SLOT) {
            defs_a.insert(n.result_id);
        }
    }

    // Check if b uses any value from a's outputs via op_a/op_b/op_c.
    // If it does, fusing changes execution order and may be illegal.
    for (const auto& n : b.nodes) {
        if (defs_a.contains(n.op_a) ||
            defs_a.contains(n.op_b) ||
            defs_a.contains(n.op_c)) {
            return false;
        }
    }

    return true;
}

} // namespace detail

// ============================================================
// ScopTransformer — P0-003 core engine
// ============================================================

/**
 * @class ScopTransformer
 * @brief Applies legal polyhedral transforms to an annotated ExecutionGraph.
 *
 * Usage:
 *   ScopTransformer transformer(config);
 *   auto schedule = transformer.transform(exec_graph, detector_metadata);
 *   if (schedule.is_valid()) { emitter.fuse_to_source_tiled(schedule); }
 */
class ScopTransformer {
public:
    explicit ScopTransformer(const core::PolyhedralConfig& config)
        : m_config(config) {}

    /**
     * @brief Apply polyhedral transforms to an annotated ExecutionGraph.
     *
     * @param graph    The ExecutionGraph built and annotated by P0-002.
     * @param metadata Per-region ScopMetadata from ScopDetector (parallel to graph.regions).
     * @return         A PolyhedralSchedule ready for the GPU emitter.
     *                 If is_valid() is false, the emitter falls back to scalar.
     */
    [[nodiscard]] PolyhedralSchedule transform(
        const graph::ExecutionGraph& graph,
        const std::vector<ScopMetadata>& metadata) const
    {
        PolyhedralSchedule schedule;

        if (!m_config.is_enabled() || graph.regions.empty()) {
            return schedule;  // is_valid() == false → emitter uses scalar path
        }

        std::ostringstream summary;
        summary << "P0-003 transforms: mode="
                << (m_config.allow_aggressive_transforms() ? "aggressive" : "safe")
                << " tile=" << m_config.tile_size;

        size_t tiled_count    = 0;
        size_t fused_count    = 0;
        size_t passthrough_count = 0;

        const size_t num_regions = graph.regions.size();

        // Track which regions have been consumed by fusion.
        std::vector<bool> consumed(num_regions, false);

        for (size_t i = 0; i < num_regions; ++i) {
            if (consumed[i]) continue;

            const graph::Region* region = graph.regions[i].get();
            if (!region) continue;

            const ScopMetadata& meta = (i < metadata.size()) ? metadata[i] : ScopMetadata{};

            const bool is_candidate =
                region->scop_annotation.affinity == graph::ScopAffinity::AFFINE_CANDIDATE
                && region->scop_annotation.can_tile
                && region->scop_annotation.can_parallelize
                && meta.classification == ScopClassification::AFFINE_CANDIDATE;

            if (!is_candidate) {
                // Pass-through region: added to schedule without tiling.
                TiledRegion tr;
                tr.region_index = i;
                tr.loop_depth   = 0;
                tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                tr.estimated_speedup = 1.0;
                schedule.add_tiled_region(std::move(tr));
                passthrough_count++;
                continue;
            }

            // --- Tile this region ---

            const std::vector<uint32_t> tile_sizes =
                detail::select_tile_sizes(meta.loop_depth, m_config.tile_size);

            std::vector<LoopDimension> dims =
                detail::build_loop_dimensions(*region, meta, tile_sizes);

            KernelLaunchParams launch = detail::build_launch_params(dims);
            const double speedup = detail::estimate_speedup(meta, launch);

            // --- Aggressive mode: try to fuse with next candidate region ---
            bool fused = false;
            if (m_config.allow_aggressive_transforms() && i + 1 < num_regions) {
                const graph::Region* next = graph.regions[i + 1].get();
                if (next) {
                    const ScopMetadata& next_meta = ((i + 1) < metadata.size())
                        ? metadata[i + 1] : ScopMetadata{};

                    const bool next_candidate =
                        next->scop_annotation.affinity == graph::ScopAffinity::AFFINE_CANDIDATE
                        && next->scop_annotation.can_tile
                        && next->scop_annotation.can_parallelize
                        && next_meta.classification == ScopClassification::AFFINE_CANDIDATE;

                    if (next_candidate && detail::can_fuse_regions(*region, *next)) {
                        // Build a fused TiledRegion containing both regions' nodes.
                        TiledRegion tr;
                        tr.region_index     = i;
                        tr.loop_depth       = std::max(meta.loop_depth, next_meta.loop_depth);
                        tr.dims             = dims;
                        tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                        tr.nodes.insert(tr.nodes.end(),
                                        next->nodes.begin(), next->nodes.end());
                        tr.launch           = launch;
                        tr.estimated_speedup = speedup * 1.1; // fusion locality bonus
                        tr.was_fused        = true;

                        consumed[i + 1] = true;
                        schedule.add_tiled_region(std::move(tr));
                        fused = true;
                        fused_count++;
                        tiled_count++;
                    }
                }
            }

            if (!fused) {
                TiledRegion tr;
                tr.region_index     = i;
                tr.loop_depth       = meta.loop_depth;
                tr.dims             = std::move(dims);
                tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                tr.launch           = launch;
                tr.estimated_speedup = speedup;
                tr.was_fused        = false;

                schedule.add_tiled_region(std::move(tr));
                tiled_count++;
            }
        }

        summary << " tiled=" << tiled_count
                << " fused=" << fused_count
                << " passthrough=" << passthrough_count;

        schedule.set_transform_summary(summary.str());

        // The schedule is valid if at least one region was tiled for parallelism.
        schedule.set_valid(tiled_count > 0);

        return schedule;
    }

private:
    core::PolyhedralConfig m_config;
};

} // namespace dgtl::analysis::polyhedral
