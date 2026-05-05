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

#include "dgtl/pass/ml/Tensor_Specializer.hpp"
#include <algorithm>
#include <numeric>

namespace dgtl {
namespace pass {
namespace ml {

void TensorSpecializer::execute(
    ir::IRStream& stream
) {
    stats = SpecializationStats();

    if (!runtime_profiling_enabled) {
        return;  // Cannot specialize without profiling data
    }

    // Analyze collected shape observations based on stream size
    uint32_t min_frequency = std::max(1u, static_cast<uint32_t>(stream.size()) / 10);
    uint32_t specialized_count = analyze_shapes(min_frequency);
    (void)specialized_count;  // Use result to satisfy [[nodiscard]]

    // Generate specializations for tensor operations in stream
    for (const auto& [op_id, observations] : shape_observations) {
        for (const auto& obs : observations) {
            if (obs.is_specialized) {
                stats.kernels_generated++;
            }
        }
    }
}

void TensorSpecializer::observe_shape(
    uint32_t op_id,
    const TensorShape& shape,
    uint64_t cycles
) noexcept {
    if (!runtime_profiling_enabled) {
        return;
    }

    // Find or create observation entry
    auto it = shape_observations.find(op_id);
    if (it == shape_observations.end()) {
        shape_observations[op_id] = std::vector<ShapeObservation>();
        it = shape_observations.find(op_id);
    }

    // Check if shape already observed
    for (auto& obs : it->second) {
        if (obs.shape == shape) {
            obs.occurrence_count++;
            obs.total_cycles += cycles;
            obs.cycles_per_element = static_cast<double>(obs.total_cycles) / 
                                     obs.shape.total_size();
            return;
        }
    }

    // New shape observation
    ShapeObservation obs;
    obs.shape = shape;
    obs.occurrence_count = 1;
    obs.total_cycles = cycles;
    obs.cycles_per_element = static_cast<double>(cycles) / shape.total_size();
    obs.is_specialized = false;

    it->second.push_back(obs);
    stats.shapes_observed++;
}

uint32_t TensorSpecializer::analyze_shapes(uint32_t min_frequency) noexcept {
    uint32_t specialized_count = 0;

    for (auto& [op_id, observations] : shape_observations) {
        // Sort by frequency
        std::sort(observations.begin(), observations.end(),
            [](const ShapeObservation& a, const ShapeObservation& b) {
                return a.occurrence_count > b.occurrence_count;
            });

        // Mark top shapes for specialization
        uint32_t specialization_count = 0;
        for (auto& obs : observations) {
            if (specialization_count >= MAX_SPECIALIZATIONS_PER_OP) {
                break;
            }

            if (obs.occurrence_count >= min_frequency) {
                obs.is_specialized = true;
                stats.shapes_common++;
                stats.kernels_generated++;
                specialization_count++;
                specialized_count++;
            }
        }
    }

    return specialized_count;
}

std::vector<TensorSpecializer::ShapeObservation> TensorSpecializer::get_common_shapes(
    uint32_t op_id,
    uint32_t limit
) const noexcept {
    auto it = shape_observations.find(op_id);
    if (it == shape_observations.end()) {
        return {};
    }

    std::vector<ShapeObservation> result;
    for (const auto& obs : it->second) {
        if (result.size() >= limit) {
            break;
        }
        result.push_back(obs);
    }

    return result;
}

uint32_t TensorSpecializer::emit_specialized_kernel(
    uint32_t op_id,
    const TensorShape& shape,
    ir::IRStream& stream
) noexcept {
    // Generate specialized kernel for specific shape
    // Validate op_id is in stream
    if (op_id >= stream.size()) {
        return 0;  // Invalid operation ID
    }

    // Check cache first
    uint32_t cached_id = get_cached_specialization(op_id, shape);
    if (cached_id != 0) {
        return cached_id;
    }

    // Generate new kernel (simplified - just return dummy ID)
    uint32_t kernel_id = op_id * 1000 + static_cast<uint32_t>(shape.dimensions[0]);

    // Cache it
    cache_specialization(op_id, shape, kernel_id);

    return kernel_id;
}

void TensorSpecializer::cache_specialization(
    uint32_t op_id,
    const TensorShape& shape,
    uint32_t kernel_id
) noexcept {
    auto key = std::make_pair(op_id, shape.dimensions);
    kernel_cache[key] = kernel_id;
    stats.kernels_cached++;

    // Track memory usage
    stats.total_specialization_memory += 1024 * 100;  // Assume ~100KB per kernel
}

uint32_t TensorSpecializer::get_cached_specialization(
    uint32_t op_id,
    const TensorShape& shape
) const noexcept {
    auto key = std::make_pair(op_id, shape.dimensions);
    auto it = kernel_cache.find(key);
    if (it != kernel_cache.end()) {
        return it->second;
    }
    return 0;
}

uint32_t TensorSpecializer::emit_generic_kernel(
    uint32_t op_id,
    ir::IRStream& stream
) noexcept {
    // Generate generic fallback kernel for any shape
    // Validate op_id is in stream
    if (op_id >= stream.size()) {
        return 0;  // Invalid operation ID
    }
    return op_id * 10000;  // Dummy ID
}

uint32_t TensorSpecializer::select_best_kernel(
    uint32_t op_id,
    const TensorShape& shape
) const noexcept {
    // Try to find specialized kernel
    uint32_t specialized = get_cached_specialization(op_id, shape);
    if (specialized != 0) {
        return specialized;
    }

    // Fall back to generic kernel
    return op_id * 10000;
}

double TensorSpecializer::estimate_specialization_speedup(
    const ShapeObservation& observation
) const noexcept {
    // Estimate speedup from specialization
    // Based on frequency and efficiency improvement potential

    if (observation.occurrence_count < 10) {
        return 1.0;  // Not frequent enough
    }

    // Higher frequency = more potential benefit from specialization
    double frequency_factor = std::log(observation.occurrence_count) / 
                             std::log(1000);  // Log scale
    
    double base_speedup = 1.05 + (frequency_factor * 0.15);  // 5-20% speedup
    return std::min(base_speedup, 1.25);  // Cap at 25%
}

} // namespace ml
} // namespace pass
} // namespace dgtl
