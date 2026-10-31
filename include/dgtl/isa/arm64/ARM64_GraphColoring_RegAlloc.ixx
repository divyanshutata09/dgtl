/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ARM64 Graph Coloring Register Allocator (MIR-Native)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

export module dgtl.isa.arm64.arm64_graphcoloring_regalloc;

import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.mir.regalloc.universal;
import dgtl.isa.arm64.arm64_reg_map;

export namespace dgtl::isa::arm64::regalloc {

struct ARM64AllocationResult {
    bool success = true;
    std::vector<uint32_t> spilled_virtuals;
    std::vector<dgtl::machine::MachineRegister> physical_assignments;
};

/**
 * @class ARM64GraphColoringRegAlloc
 * @brief Tier-2 ARM64 Graph Coloring Register Allocator anchored on canonical MIR.
 * Bridges MIR register allocation with UniversalGraphColoringRegAlloc.
 */
class ARM64GraphColoringRegAlloc {
public:
    ARM64GraphColoringRegAlloc() = default;

    /**
     * @brief Allocate registers for a pure MachineFunction using UniversalGraphColoringRegAlloc.
     */
    [[nodiscard]] ARM64AllocationResult allocate(dgtl::machine::MachineFunction& function) {
        ARM64AllocationResult result;
        dgtl::isa::arm64::ARM64TargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(function, reg_info);
        result.success = allocator.run();
        for (const auto& spilled : allocator.spilled_virtuals()) {
            result.spilled_virtuals.push_back(spilled.id);
        }
        return result;
    }

    /**
     * @brief Analyze register pressure and spill candidates for Tier-2 machine function optimization.
     */
    [[nodiscard]] ARM64AllocationResult allocate_pressure(const dgtl::machine::MachineFunction& function) const {
        ARM64AllocationResult result;
        // ARM64 has 31 general-purpose registers (X0-X30, X18 reserved on Darwin/Windows, X29 FP, X30 LR)
        constexpr size_t kMaxAllocatableGPRs = 28;
        std::unordered_map<uint32_t, uint32_t> use_counts;

        for (const auto& instr : function.instructions()) {
            for (const auto& op : instr.operands) {
                if (op.kind == dgtl::machine::MachineOperandKind::Register && op.reg.valid()) {
                    use_counts[op.reg.id]++;
                }
            }
        }

        if (use_counts.size() > kMaxAllocatableGPRs) {
            std::vector<std::pair<uint32_t, uint32_t>> sorted_uses(use_counts.begin(), use_counts.end());
            std::sort(sorted_uses.begin(), sorted_uses.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
            const size_t num_spills = sorted_uses.size() - kMaxAllocatableGPRs;
            for (size_t i = 0; i < num_spills; ++i) {
                result.spilled_virtuals.push_back(sorted_uses[i].first);
            }
        }

        return result;
    }
};

} // namespace dgtl::isa::arm64::regalloc
