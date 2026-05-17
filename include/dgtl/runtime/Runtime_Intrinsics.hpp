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
#include <string_view>
#include <utility>


namespace dgtl::runtime {

// Client may register runtime intrinsics via this extension point.
using RuntimeIntrinsicResolver = uintptr_t(*)(std::string_view name);
inline RuntimeIntrinsicResolver g_runtime_intrinsic_resolver = nullptr;

inline void register_runtime_intrinsic_resolver(RuntimeIntrinsicResolver resolver) {
    g_runtime_intrinsic_resolver = resolver;
}

[[nodiscard]] inline uintptr_t resolve_runtime_intrinsic_symbol(std::string_view callee) noexcept {
    if (g_runtime_intrinsic_resolver) {
        return g_runtime_intrinsic_resolver(callee);
    }
    return 0;
}

} // namespace dgtl::runtime
