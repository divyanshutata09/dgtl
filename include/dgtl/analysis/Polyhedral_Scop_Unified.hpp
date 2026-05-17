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
 * DGTL Polyhedral Analysis - Unified Module
 * Consolidates:
 *   - Scop_Detector.hpp
 *   - Polyhedral_Schedule.hpp
 *   - Scop_Transformer.hpp
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dgtl/core/Compiler_Context.hpp"
#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"
#include "dgtl/ir/base/IR_Node.hpp"

namespace dgtl::analysis::polyhedral {

namespace graph = dgtl::graph;

enum class ScopClassification : uint8_t {
    INVALID = 0,
    AFFINE_CANDIDATE = 1,
    AFFINE_CANDIATE = AFFINE_CANDIDATE,
    NON_AFFINE_LOOP = 2,
    NON_AFFINE_SUBSCRIPT = 3,
    NON_AFFINE_CONTROL = 4,
    IRREGULAR_MEMORY = 5,
    TOO_COMPLEX = 6,
};

struct ScopMetadata {
    ScopClassification classification = ScopClassification::INVALID;

    uint32_t loop_depth = 0;
    std::vector<uint32_t> induction_vars;
    std::vector<int64_t> lower_bounds;
    std::vector<int64_t> upper_bounds;
    std::vector<int64_t> strides;

    bool has_affine_bounds = false;
    bool has_affine_subscripts = false;
    bool has_analyzable_dependencies = false;
    bool has_irregular_control = false;

    uint32_t load_count = 0;
    uint32_t store_count = 0;
    double reuse_distance = 0.0;
    double memory_footprint_bytes = 0.0;

    bool can_tile = false;
    bool can_fuse = false;
    bool can_parallelize = false;

    uint32_t confidence_score = 0;
    std::string rejection_reason;
};

class ScopDetector {
public:
    [[nodiscard]] ScopMetadata analyze_region(const dgtl::graph::Region& region) const;

    [[nodiscard]] std::vector<ScopMetadata> analyze_regions(
        const std::vector<std::unique_ptr<dgtl::graph::Region>>& regions) const;

    [[nodiscard]] bool is_tileable(
        const dgtl::graph::Region& region,
        const ScopMetadata& metadata) const;

    [[nodiscard]] bool can_fuse_affine(
        const dgtl::graph::Region& producer,
        const dgtl::graph::Region& consumer,
        const ScopMetadata& prod_meta,
        const ScopMetadata& cons_meta) const;

    [[nodiscard]] uint32_t detect_loop_depth(const dgtl::graph::Region& region) const;

    [[nodiscard]] bool check_affine_subscripts(
        const dgtl::graph::Region& region,
        const ScopMetadata& metadata) const;

    [[nodiscard]] double estimate_reuse_distance(const dgtl::graph::Region& region) const;

private:
    static constexpr uint32_t kMaxLoopDepth = 8;
    static constexpr size_t kMaxRegionSize = 512;
    static constexpr double kMinReuseDistanceForTile = 1.5;

    [[nodiscard]] std::vector<uint32_t> detect_induction_variables(
        const dgtl::graph::Region& region) const;

    [[nodiscard]] bool is_induction_variable(
        const dgtl::graph::Region& region,
        uint32_t vreg_slot) const;

    [[nodiscard]] bool extract_affine_bounds(
        const dgtl::graph::Region& region,
        const std::vector<uint32_t>& induction_vars,
        std::vector<int64_t>& lower,
        std::vector<int64_t>& upper,
        std::vector<int64_t>& strides) const;

    [[nodiscard]] bool has_irregular_control(const dgtl::graph::Region& region) const;

    [[nodiscard]] double estimate_memory_footprint(
        const dgtl::graph::Region& region,
        const ScopMetadata& metadata) const;
};

struct LoopDimension {
    int64_t lower = 0;
    int64_t upper = 0;
    int64_t stride = 1;
    uint32_t tile_size = 0;
    bool parallelize = false;
    uint32_t induction_var_slot = 0;
};

struct KernelLaunchParams {
    uint32_t grid_dim_x = 1;
    uint32_t grid_dim_y = 1;
    uint32_t block_dim_x = 256;
    uint32_t block_dim_y = 1;
    uint32_t shared_mem_bytes = 0;
};

struct TiledRegion {
    size_t region_index = 0;
    uint32_t loop_depth = 0;
    std::vector<LoopDimension> dims;
    std::vector<dgtl::ir::IRNode> nodes;
    KernelLaunchParams launch;
    double estimated_speedup = 1.0;
    bool was_fused = false;
};

class PolyhedralSchedule {
public:
    PolyhedralSchedule() = default;

    void add_tiled_region(TiledRegion region) {
        m_regions.push_back(std::move(region));
    }

    void set_valid(bool v) noexcept { m_valid = v; }

    void set_transform_summary(std::string summary) {
        m_transform_summary = std::move(summary);
    }

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

namespace detail {

[[nodiscard]] inline std::vector<uint32_t> select_tile_sizes(
    uint32_t loop_depth,
    uint32_t user_tile_size) noexcept
{
    std::vector<uint32_t> tiles(loop_depth > 0 ? loop_depth : 1, 0u);

    if (user_tile_size > 0) {
        for (auto& t : tiles) t = user_tile_size;
        return tiles;
    }

    constexpr uint32_t kMaxThreadsPerBlock = 1024;
    constexpr uint32_t kWarpSize = 32;
    constexpr uint32_t kDefaultBlock1D = 256;
    constexpr uint32_t kDefaultBlock2D = 16;

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

        if (d < meta.lower_bounds.size()) {
            dim.lower = meta.lower_bounds[d];
        }
        if (d < meta.upper_bounds.size()) {
            dim.upper = meta.upper_bounds[d];
        } else {
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

[[nodiscard]] inline KernelLaunchParams build_launch_params(
    const std::vector<LoopDimension>& dims) noexcept
{
    KernelLaunchParams lp;

    if (dims.empty()) return lp;

    const LoopDimension& d0 = dims[0];
    lp.block_dim_x = (d0.tile_size > 0) ? d0.tile_size : 256u;
    lp.grid_dim_x  = compute_grid_dim(d0.lower, d0.upper, d0.stride, lp.block_dim_x);

    if (dims.size() >= 2) {
        const LoopDimension& d1 = dims[1];
        lp.block_dim_y = (d1.tile_size > 0) ? d1.tile_size : 1u;
        lp.grid_dim_y  = compute_grid_dim(d1.lower, d1.upper, d1.stride, lp.block_dim_y);
    }

    constexpr uint32_t kMaxSharedMem = 48 * 1024;
    const uint32_t tile_vol = lp.block_dim_x * lp.block_dim_y;
    lp.shared_mem_bytes = std::min(tile_vol * 4u, kMaxSharedMem);

    return lp;
}

[[nodiscard]] inline double estimate_speedup(
    const ScopMetadata& meta,
    const KernelLaunchParams& launch) noexcept
{
    const double thread_count = static_cast<double>(launch.grid_dim_x)
                              * static_cast<double>(launch.grid_dim_y)
                              * static_cast<double>(launch.block_dim_x)
                              * static_cast<double>(launch.block_dim_y);
    const double parallelism_factor = std::min(thread_count, 32768.0);
    const double memory_factor = 1.0 + meta.reuse_distance * 0.3;

    return std::min(parallelism_factor, memory_factor * 4.0);
}

[[nodiscard]] inline bool can_fuse_regions(
    const graph::Region& a,
    const graph::Region& b) noexcept
{
    std::unordered_set<uint32_t> defs_a;
    defs_a.reserve(a.nodes.size());
    for (const auto& n : a.nodes) {
        if (n.result_id != dgtl::ir::INVALID_SLOT) {
            defs_a.insert(n.result_id);
        }
    }

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

class ScopTransformer {
public:
    explicit ScopTransformer(const core::PolyhedralConfig& config)
        : m_config(config) {}

    [[nodiscard]] PolyhedralSchedule transform(
        const graph::ExecutionGraph& graph,
        const std::vector<ScopMetadata>& metadata) const
    {
        PolyhedralSchedule schedule;

        if (!m_config.is_enabled() || graph.regions.empty()) {
            return schedule;
        }

        std::ostringstream summary;
        summary << "P0-003 transforms: mode="
                << (m_config.allow_aggressive_transforms() ? "aggressive" : "safe")
                << " tile=" << m_config.tile_size;

        size_t tiled_count = 0;
        size_t fused_count = 0;
        size_t passthrough_count = 0;

        const size_t num_regions = graph.regions.size();
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
                TiledRegion tr;
                tr.region_index = i;
                tr.loop_depth = 0;
                tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                tr.estimated_speedup = 1.0;
                schedule.add_tiled_region(std::move(tr));
                passthrough_count++;
                continue;
            }

            const std::vector<uint32_t> tile_sizes =
                detail::select_tile_sizes(meta.loop_depth, m_config.tile_size);

            std::vector<LoopDimension> dims =
                detail::build_loop_dimensions(*region, meta, tile_sizes);

            KernelLaunchParams launch = detail::build_launch_params(dims);
            const double speedup = detail::estimate_speedup(meta, launch);

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
                        TiledRegion tr;
                        tr.region_index = i;
                        tr.loop_depth = std::max(meta.loop_depth, next_meta.loop_depth);
                        tr.dims = dims;
                        tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                        tr.nodes.insert(tr.nodes.end(),
                                        next->nodes.begin(), next->nodes.end());
                        tr.launch = launch;
                        tr.estimated_speedup = speedup * 1.1;
                        tr.was_fused = true;

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
                tr.region_index = i;
                tr.loop_depth = meta.loop_depth;
                tr.dims = std::move(dims);
                tr.nodes.assign(region->nodes.begin(), region->nodes.end());
                tr.launch = launch;
                tr.estimated_speedup = speedup;
                tr.was_fused = false;

                schedule.add_tiled_region(std::move(tr));
                tiled_count++;
            }
        }

        summary << " tiled=" << tiled_count
                << " fused=" << fused_count
                << " passthrough=" << passthrough_count;

        schedule.set_transform_summary(summary.str());
        schedule.set_valid(tiled_count > 0);

        return schedule;
    }

private:
    core::PolyhedralConfig m_config;
};

} // namespace dgtl::analysis::polyhedral
