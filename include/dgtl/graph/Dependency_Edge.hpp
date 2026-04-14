/* * DGTL Backend vNext - Dependency Edges
 * Typed dependency relationships between regions
 */

#pragma once
#include "Region.hpp"

namespace dgtl::graph {

/**
 * @enum DependencyType
 * @brief Types of dependencies between regions.
 */
enum class DependencyType {
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
    Region* from;
    Region* to;
    DependencyType type;
    double latency_penalty = 0.0;  // Estimated cycles of delay

    DependencyEdge(Region* f, Region* t, DependencyType dt)
        : from(f), to(t), type(dt) {}

    /**
     * @brief Check if this dependency can be relaxed.
     */
    bool can_relax() const;

    /**
     * @brief Get the strength/cost of this dependency.
     */
    double get_cost() const;
};

} // namespace dgtl::graph

