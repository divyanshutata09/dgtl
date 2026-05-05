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
 * DGTL P0-003: Polyhedral Schedule Representation
 *
 * A PolyhedralSchedule is the output of ScopTransformer. It captures the
 * result of loop tiling, fusion, and parallelization decisions made on the
 * ExecutionGraph layer, and provides the GPU emitter with everything needed
 * to emit a structurally correct tiled, data-parallel kernel.
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. The schedule operates on the ExecutionGraph's Region layer, NOT on raw
 *    IRStream indices. This avoids the branch-target invalidation problem that
 *    makes direct IRStream mutation unsafe on frontend payload paths.
 *
 * 2. Each TiledRegion describes one GPU kernel body: the set of IR nodes from
 *    the source region, the tile sizes for each loop dimension, and the grid/block
 *    dimensions to use when launching.
 *
 * 3. Parallelism is modeled as an abstract IterationSpace. The emitter maps the
 *    outermost tile loop → grid dimension, and the innermost point loop →
 *    thread dimension, following standard GPGPU practice.
 *
 * 4. A PolyhedralSchedule carries a validity flag. If ScopTransformer could not
 *    legally tile any region (non-affine IR, data-dependent control, etc.), it
 *    produces an invalid schedule. The emitter then falls back to the existing
 *    scalar fuse_to_source() path transparently.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <span>

#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/graph/Region.hpp"

namespace dgtl::analysis::polyhedral {

// ============================================================
// Tile descriptor for a single loop dimension
// ============================================================

/**
 * @struct LoopDimension
 * @brief Describes one dimension of a tiled iteration space.
 *
 * For a loop:
 *   for (i = lower; i < upper; i += stride)
 *
 * After tiling by tile_size, the outer tile loop runs:
 *   for (tile_i = lower; tile_i < upper; tile_i += tile_size * stride)
 *
 * The inner point loop runs:
 *   for (point_i = tile_i; point_i < min(tile_i + tile_size * stride, upper); point_i += stride)
 */
struct LoopDimension {
    int64_t  lower      = 0;        ///< Affine lower bound
    int64_t  upper      = 0;        ///< Affine upper bound (exclusive)
    int64_t  stride     = 1;        ///< Loop stride (always positive in affine SCoPs)
    uint32_t tile_size  = 0;        ///< 0 = no tiling (scalar dimension)
    bool     parallelize = false;   ///< Outer tile loop maps to GPU grid/block dimension
    uint32_t induction_var_slot = 0; ///< IR vreg slot of the loop counter
};

// ============================================================
// Grid/block launch parameters
// ============================================================

/**
 * @struct KernelLaunchParams
 * @brief GPU launch grid dimensions derived from the tiling decision.
 *
 * For a 1-D tiled loop with N total iterations and tile_size T:
 *   grid_dim_x  = ceil(N / T)         (number of thread blocks)
 *   block_dim_x = T                   (threads per block)
 *
 * For 2-D (matrix-style):
 *   grid/block dimensions extend to Y.
 */
struct KernelLaunchParams {
    uint32_t grid_dim_x  = 1;
    uint32_t grid_dim_y  = 1;
    uint32_t block_dim_x = 256;    ///< Hardware default: 256 threads/block
    uint32_t block_dim_y = 1;
    uint32_t shared_mem_bytes = 0;
};

// ============================================================
// Tiled region — one GPU kernel body
// ============================================================

/**
 * @struct TiledRegion
 * @brief The tiled, GPU-ready description of one affine SCoP region.
 *
 * The emitter walks `nodes` in order, wrapping them with tile/point loop
 * boilerplate generated from `dims`. For GPU emission it maps:
 *   dims[outermost parallelize=true] → blockIdx.x
 *   dims[next parallelize=true]      → threadIdx.x
 */
struct TiledRegion {
    // Source identity
    size_t region_index = 0;          ///< Index into ExecutionGraph::regions
    uint32_t loop_depth = 0;          ///< Detected loop nesting depth (from ScopAnnotation)

    // Tiling geometry
    std::vector<LoopDimension> dims;  ///< One entry per loop dimension, outermost first

    // Nodes from the region in their original sequential order.
    // The emitter emits these inside the innermost point loop body.
    std::vector<dgtl::ir::IRNode> nodes;

    // GPU launch parameters derived from dims
    KernelLaunchParams launch;

    // Profitability metadata (informational; emitter may log)
    double estimated_speedup = 1.0;   ///< vs. scalar emission
    bool was_fused = false;           ///< True if this TiledRegion is a fusion of multiple regions
};

// ============================================================
// Top-level polyhedral schedule
// ============================================================

/**
 * @class PolyhedralSchedule
 * @brief Output of P0-003 ScopTransformer: a complete, emitter-ready schedule.
 *
 * VALIDITY CONTRACT:
 *   If is_valid() returns false, the GPU emitter MUST fall back to the raw
 *   fuse_to_source() path. No partial application is allowed.
 *
 * ORDERING:
 *   tiled_regions are in execution order (matching the original region
 *   scheduling order from ExecutionGraph::schedule()). Regions not selected
 *   for tiling appear as pass-through entries with empty dims (loop_depth=0).
 */
class PolyhedralSchedule {
public:
    PolyhedralSchedule() = default;

    // ---- Mutators used by ScopTransformer ----

    void add_tiled_region(TiledRegion region) {
        m_regions.push_back(std::move(region));
    }

    void set_valid(bool v) noexcept { m_valid = v; }

    void set_transform_summary(std::string summary) {
        m_transform_summary = std::move(summary);
    }

    // ---- Accessors used by the emitter ----

    [[nodiscard]] bool is_valid() const noexcept { return m_valid; }

    [[nodiscard]] bool has_any_tiled_regions() const noexcept {
        for (const auto& tr : m_regions) {
            if (!tr.dims.empty() && tr.loop_depth > 0) return true;
        }
        return false;
    }

    [[nodiscard]] std::span<const TiledRegion> tiled_regions() const noexcept {
        return std::span<const TiledRegion>(m_regions.data(), m_regions.size());
    }

    [[nodiscard]] const std::string& transform_summary() const noexcept {
        return m_transform_summary;
    }

    [[nodiscard]] size_t tiled_region_count() const noexcept {
        return m_regions.size();
    }

    // Total node count across all tiled regions (for emitter allocation hint)
    [[nodiscard]] size_t total_node_count() const noexcept {
        size_t n = 0;
        for (const auto& tr : m_regions) n += tr.nodes.size();
        return n;
    }

private:
    std::vector<TiledRegion> m_regions;
    bool m_valid = false;
    std::string m_transform_summary;
};

} // namespace dgtl::analysis::polyhedral
