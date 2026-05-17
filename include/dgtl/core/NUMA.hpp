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

#include <cstdint>

namespace dgtl::core {

enum class NUMAPolicy : uint8_t {
    None = 0,
    PreferLocal = 1,
    StrictLocal = 2
};

// Future extension point for NUMA-aware arena placement and worker affinity.
class NUMAController {
public:
    NUMAController() = default;

    [[nodiscard]] static constexpr NUMAPolicy default_policy() noexcept {
        return NUMAPolicy::None;
    }

    // TODO: Implement socket discovery, node-local allocation, and policy enforcement.
    // TODO: Integrate with DGTL_Arena worker pinning once NUMA scheduler arrives.
};

} // namespace dgtl::core
