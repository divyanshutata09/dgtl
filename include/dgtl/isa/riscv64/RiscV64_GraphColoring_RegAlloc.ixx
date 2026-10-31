/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   RISC-V 64 Graph Coloring Register Allocator (MIR-Native)
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

export module dgtl.isa.riscv64.riscv64_graphcoloring_regalloc;

import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.mir.regalloc.universal;
import dgtl.isa.riscv64.riscv64_reg_map;

export namespace dgtl::isa::riscv64::regalloc {

struct RiscV64AllocationResult {
    bool success = true;
    std::vector<uint32_t> spilled_virtuals;
    std::vector<dgtl::machine::MachineRegister> physical_assignments;
};

/**
 * @class RiscV64GraphColoringRegAlloc
 * @brief Tier-2 RISC-V 64 Graph Coloring Register Allocator anchored on canonical MIR.
 * Bridges MIR register allocation with UniversalGraphColoringRegAlloc.
 */
class RiscV64GraphColoringRegAlloc {
public:
    RiscV64GraphColoringRegAlloc() = default;

    /**
     * @brief Allocate registers for a pure MachineFunction using UniversalGraphColoringRegAlloc.
     */
    [[nodiscard]] RiscV64AllocationResult allocate(dgtl::machine::MachineFunction& function) {
        RiscV64AllocationResult result;
        dgtl::isa::riscv64::RiscV64TargetRegisterInfo reg_info{};
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
    [[nodiscard]] RiscV64AllocationResult allocate_pressure(const dgtl::machine::MachineFunction& function) const {
        RiscV64AllocationResult result;
        // RISC-V 64 has 32 GPRs (x0 zero, x1 ra, x2 sp, x3 gp, x4 tp, x8 s0 reserved -> ~26 allocatable)
        constexpr size_t kMaxAllocatableGPRs = 26;
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

} // namespace dgtl::isa::riscv64::regalloc
