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

/* * DGTL Backend vNext - Dependency Edges
 * Typed dependency relationships between regions
 */

#pragma once
#include <algorithm>
#include <cstdint>
#include "Region.hpp"

namespace dgtl::graph {

/**
 * @enum DependencyType
 * @brief Types of dependencies between regions.
 */
enum class DependencyType : uint8_t {
    DATA,           // Data dependency (RAW, WAR, WAW)
    CONTROL,        // Control flow dependency
    MEMORY,         // Memory ordering dependency
    REGISTER,       // Register pressure dependency
    SPECULATIVE     // Speculative execution dependency
};

/**
 * @class DependencyEdge
 * @brief Represents a typed dependency between two regions.
 */
class DependencyEdge {
public:
    Region* from = nullptr;
    Region* to = nullptr;
    DependencyType type;
    double latency_penalty = 0.0;  // Estimated cycles of delay

    DependencyEdge(Region* f, Region* t, DependencyType dt) noexcept
        : from(f), to(t), type(dt) {}

    /**
     * @brief Check if this dependency can be relaxed.
     */
    [[nodiscard]] bool can_relax() const noexcept {
        if (!from || !to || from == to) {
            return false;
        }

        switch (type) {
            case DependencyType::CONTROL:
            case DependencyType::MEMORY:
                return false;
            case DependencyType::SPECULATIVE:
            case DependencyType::REGISTER:
                return true;
            case DependencyType::DATA:
            default:
                return latency_penalty < 1.0;
        }
    }

    /**
     * @brief Get the strength/cost of this dependency.
     */
    [[nodiscard]] double get_cost() const noexcept {
        const double base = [&]() noexcept {
            switch (type) {
                case DependencyType::DATA: return 1.0;
                case DependencyType::CONTROL: return 2.0;
                case DependencyType::MEMORY: return 2.5;
                case DependencyType::REGISTER: return 0.75;
                case DependencyType::SPECULATIVE: return 0.5;
                default: return 1.0;
            }
        }();

        return std::max(0.0, base + latency_penalty);
    }
};

} // namespace dgtl::graph

