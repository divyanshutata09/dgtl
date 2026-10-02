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
 * DGTL HIR architectural contracts.
 *
 * This module contains phase/ownership/invalidation contracts only.
 * It deliberately contains no target opcode or machine representation.
 */

module;

#include <cstdint>
#include <string_view>

export module dgtl.ir_contracts;

export namespace dgtl::ir::contract {

enum class Phase : std::uint8_t {
    Construction,
    CanonicalHir,
    Analysis,
    Optimization,
    Legalization,
    CommonMir,
    TargetBackend,
    Emission
};

enum class VerificationLevel : std::uint8_t {
    Structural,
    Semantic,
    Ssa,
    PreLegalization,
    Full
};

enum class Preservation : std::uint32_t {
    None          = 0,
    CFG           = 1u << 0,
    Dominance     = 1u << 1,
    SSA           = 1u << 2,
    MemoryEffects = 1u << 3,
    Types         = 1u << 4,
    DebugInfo     = 1u << 5,
    Profiles      = 1u << 6,
    All           = 0xFFFFFFFFu
};

[[nodiscard]] constexpr Preservation operator|(
    Preservation a,
    Preservation b
) noexcept {
    return static_cast<Preservation>(
        static_cast<std::uint32_t>(a) |
        static_cast<std::uint32_t>(b)
    );
}

[[nodiscard]] constexpr Preservation operator&(
    Preservation a,
    Preservation b
) noexcept {
    return static_cast<Preservation>(
        static_cast<std::uint32_t>(a) &
        static_cast<std::uint32_t>(b)
    );
}

[[nodiscard]] constexpr bool preserves(
    Preservation set,
    Preservation bit
) noexcept {
    return (
        static_cast<std::uint32_t>(set) &
        static_cast<std::uint32_t>(bit)
    ) != 0;
}

struct PipelineStamp {
    Phase phase{Phase::Construction};
    std::uint64_t mutation_epoch{0};
    std::uint64_t semantic_hash{0};
};

struct TargetCapability {
    // Opaque capability identity: HIR can carry capability requirements without
    // importing a target backend's opcode/register model.
    std::uint32_t family{0};
    std::uint32_t features{0};
    std::uint64_t constraints{0};
};

inline constexpr std::string_view kHirMirAbiWall =
    "HIR -> legalization -> common MIR -> target backend";

} // namespace dgtl::ir::contract
// ```

// The resulting API is now consistent:

// ```text
// Preservation
//     ├── operator|    → combine preservation bits
//     ├── operator&    → test/intersect preservation bits
//     └── preserves()  → boolean bit test
// ```

