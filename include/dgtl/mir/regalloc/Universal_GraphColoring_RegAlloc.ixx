/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Universal Architecture-Independent Register Allocator
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
export module dgtl.mir.regalloc.universal;
import dgtl.mir.machine_ir;
export namespace dgtl::mir::regalloc {

using namespace dgtl::machine;

struct UniversalLiveInterval {
    MachineRegister reg{};
    std::size_t first = 0;
    std::size_t last = 0;
    std::uint32_t use_count = 0;
    bool fixed = false;
    MachineRegister fixed_target{};
};

class UniversalGraphColoringRegAlloc final : public MachineRegisterAllocator {
public:
    UniversalGraphColoringRegAlloc(
        MachineFunction& function,
        const TargetRegisterInfo& reg_info,
        const TargetInstructionInfo* instr_info = nullptr,
        const TargetFrameInfo* frame_info = nullptr,
        MachineSpiller* spiller = nullptr) noexcept
        : function_(function),
          reg_info_(reg_info),
          instr_info_(instr_info),
          frame_info_(frame_info),
          spiller_(spiller) {}

    bool run() override {
        if (!function_.is_phase_at_least(MachinePhase::PreRegAlloc)) {
            return false;
        }

        assignments_.clear();
        spills_.clear();
        spilled_virtuals_.clear();

        // Spilling mutates MIR. Rebuild liveness/interference and recolor until
        // all virtual registers are gone or the target spiller reports failure.
        for (std::size_t iteration = 0; iteration < kMaxAllocationIterations; ++iteration) {
            if (!function_.has_virtual_registers()) {
                function_.mark_registers_allocated();
                return function_.properties().registers_allocated;
            }

            AllocationState state;
            if (!build_interference_graph(state)) {
                return false;
            }

            if (!color_graph(state)) {
                return false;
            }

            if (state.spilled.empty()) {
                for (const auto& [v_id, p_reg] : assignments_) {
                    MachineRegister vreg{v_id, RegisterKind::Virtual, p_reg.reg_class};
                    if (!function_.replace_virtual_register(vreg, p_reg)) {
                        return false;
                    }
                }

                if (function_.has_virtual_registers()) {
                    return false;
                }

                function_.mark_registers_allocated();
                return function_.properties().registers_allocated;
            }

            if (spiller_ == nullptr || frame_info_ == nullptr) {
                return false;
            }

            for (const MachineRegister vreg : state.spilled) {
                const std::uint32_t size =
                    std::max<std::uint32_t>(1u, frame_info_->spill_size(vreg.reg_class));
                const std::uint32_t alignment =
                    std::max<std::uint32_t>(1u, frame_info_->spill_alignment(vreg.reg_class));

                const SpillRequest request{
                    vreg,
                    vreg.reg_class,
                    size,
                    alignment
                };
                const SpillResult result = spiller_->spill(function_, request);
                if (!result.spilled || result.slot == INVALID_STACK_SLOT) {
                    return false;
                }
                spills_[vreg.id] = result.slot;
                spilled_virtuals_.push_back(vreg);

                if (!spiller_->rewrite_for_spill(function_, vreg, result.slot)) {
                    return false;
                }
            }

            assignments_.clear();
        }

        return false;
    }

    [[nodiscard]] std::optional<MachineRegister>
    assigned_register(MachineRegister virtual_reg) const noexcept override {
        const auto it = assignments_.find(virtual_reg.id);
        if (it != assignments_.end()) return it->second;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<StackSlotId>
    spill_slot(MachineRegister virtual_reg) const noexcept override {
        const auto it = spills_.find(virtual_reg.id);
        if (it != spills_.end()) return it->second;
        return std::nullopt;
    }

    [[nodiscard]] const std::vector<MachineRegister>& spilled_virtuals() const noexcept {
        return spilled_virtuals_;
    }

private:
    static constexpr std::size_t kMaxAllocationIterations = 8;

    struct AllocationState {
        std::unordered_map<std::uint32_t, MachineRegister> virtuals;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> edges;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> blocked_phys;
        std::unordered_map<std::uint32_t, MachineRegister> fixed;
        std::vector<MachineRegister> spilled;
    };

    static void add_edge(AllocationState& state,
                         std::uint32_t lhs,
                         std::uint32_t rhs) {
        if (lhs == rhs) return;
        state.edges[lhs].insert(rhs);
        state.edges[rhs].insert(lhs);
    }

    static void add_virtual_operand(const MachineOperand& op,
                                    std::unordered_set<std::uint32_t>& set) {
        if (op.kind == MachineOperandKind::Register && op.reg.is_virtual()) {
            set.insert(op.reg.id);
        } else if (op.kind == MachineOperandKind::Memory) {
            if (op.memory.base.is_virtual()) set.insert(op.memory.base.id);
            if (op.memory.index.is_virtual()) set.insert(op.memory.index.id);
        }
    }

    static void classify_operand(const MachineOperand& op,
                                 std::unordered_set<std::uint32_t>& uses,
                                 std::unordered_set<std::uint32_t>& defs) {
        auto classify_reg = [&](MachineRegister reg, OperandRole role) {
            if (!reg.is_virtual()) return;
            switch (role) {
                case OperandRole::Def:
                case OperandRole::ImplicitDef:
                case OperandRole::Dead:
                    defs.insert(reg.id);
                    break;
                case OperandRole::UseDef:
                    uses.insert(reg.id);
                    defs.insert(reg.id);
                    break;
                default:
                    uses.insert(reg.id);
                    break;
            }
        };

        if (op.kind == MachineOperandKind::Register) {
            classify_reg(op.reg, op.role);
        } else if (op.kind == MachineOperandKind::Memory) {
            // Address registers are reads unless a target explicitly models
            // them as a separate def operand.
            classify_reg(op.memory.base, OperandRole::Use);
            classify_reg(op.memory.index, OperandRole::Use);
        }
    }

    [[nodiscard]] bool build_interference_graph(AllocationState& state) {
        // Discover virtual registers and fixed-register constraints.
        for (const auto& block : function_.blocks()) {
            for (InstrId id : block.instructions) {
                const MachineInstruction* inst = function_.instruction(id);
                if (!inst) return false;

                for (std::size_t op_idx = 0; op_idx < inst->operands.size(); ++op_idx) {
                    const MachineOperand& op = inst->operands[op_idx];
                    if (op.kind == MachineOperandKind::Register && op.reg.is_virtual()) {
                        state.virtuals.emplace(op.reg.id, op.reg);
                        state.edges.try_emplace(op.reg.id);
                        if (instr_info_ && inst->has_target_opcode()) {
                            const auto constraint =
                                instr_info_->register_constraint(inst->opcode, op_idx);
                            if (constraint.kind == MachineRegisterConstraintKind::FixedRegister) {
                                if (!constraint.fixed_register.valid() ||
                                    !constraint.fixed_register.is_physical()) {
                                    return false;
                                }
                                const auto it = state.fixed.find(op.reg.id);
                                if (it != state.fixed.end() &&
                                    (it->second != constraint.fixed_register) &&
                                    reg_info_.aliases(it->second, constraint.fixed_register)) {
                                    return false;
                                }
                                state.fixed[op.reg.id] = constraint.fixed_register;
                            }
                        }
                    } else if (op.kind == MachineOperandKind::Memory) {
                        if (op.memory.base.is_virtual()) {
                            state.virtuals.emplace(op.memory.base.id, op.memory.base);
                            state.edges.try_emplace(op.memory.base.id);
                        }
                        if (op.memory.index.is_virtual()) {
                            state.virtuals.emplace(op.memory.index.id, op.memory.index);
                            state.edges.try_emplace(op.memory.index.id);
                        }
                    }
                }
            }
        }

        // Build block-level use/def sets.
        std::vector<std::unordered_set<std::uint32_t>> use(function_.blocks().size());
        std::vector<std::unordered_set<std::uint32_t>> def(function_.blocks().size());
        std::vector<std::unordered_set<std::uint32_t>> live_in(function_.blocks().size());
        std::vector<std::unordered_set<std::uint32_t>> live_out(function_.blocks().size());

        for (std::size_t b = 0; b < function_.blocks().size(); ++b) {
            for (InstrId id : function_.blocks()[b].instructions) {
                const auto* inst = function_.instruction(id);
                if (!inst) return false;

                std::unordered_set<std::uint32_t> uses;
                std::unordered_set<std::uint32_t> defs;
                for (const auto& op : inst->operands) {
                    classify_operand(op, uses, defs);
                }

                for (const auto v : uses) {
                    if (!def[b].contains(v)) use[b].insert(v);
                }
                def[b].insert(defs.begin(), defs.end());
            }
        }

        // Standard backward data-flow liveness over the actual MIR CFG.
        bool changed = true;
        for (std::size_t pass = 0; pass < function_.blocks().size() + 1u && changed; ++pass) {
            changed = false;
            for (std::size_t reverse = function_.blocks().size(); reverse-- > 0;) {
                const auto& block = function_.blocks()[reverse];
                std::unordered_set<std::uint32_t> new_out;
                for (BlockId succ : block.successors) {
                    if (succ >= live_in.size()) return false;
                    new_out.insert(live_in[succ].begin(), live_in[succ].end());
                }

                std::unordered_set<std::uint32_t> new_in = use[reverse];
                for (const auto v : new_out) {
                    if (!def[reverse].contains(v)) new_in.insert(v);
                }

                if (new_out != live_out[reverse] || new_in != live_in[reverse]) {
                    live_out[reverse] = std::move(new_out);
                    live_in[reverse] = std::move(new_in);
                    changed = true;
                }
            }
        }

        // Instruction-level interference and physical-register blocking.
        for (std::size_t b = 0; b < function_.blocks().size(); ++b) {
            std::unordered_set<std::uint32_t> live = live_out[b];

            for (std::size_t reverse = function_.blocks()[b].instructions.size(); reverse-- > 0;) {
                const InstrId id = function_.blocks()[b].instructions[reverse];
                const auto* inst = function_.instruction(id);
                if (!inst) return false;

                std::unordered_set<std::uint32_t> uses;
                std::unordered_set<std::uint32_t> defs;
                std::vector<MachineRegister> physical_operands;

                for (std::size_t op_idx = 0; op_idx < inst->operands.size(); ++op_idx) {
                    const auto& op = inst->operands[op_idx];
                    classify_operand(op, uses, defs);
                    if (op.kind == MachineOperandKind::Register && op.reg.is_physical()) {
                        physical_operands.push_back(op.reg);
                    } else if (op.kind == MachineOperandKind::Memory) {
                        if (op.memory.base.is_physical()) physical_operands.push_back(op.memory.base);
                        if (op.memory.index.is_physical()) physical_operands.push_back(op.memory.index);
                    }
                }

                std::unordered_set<std::uint32_t> live_here = live;
                live_here.insert(uses.begin(), uses.end());
                live_here.insert(defs.begin(), defs.end());

                for (const auto v : defs) {
                    for (const auto other : live) add_edge(state, v, other);
                    for (const auto other : defs) add_edge(state, v, other);
                }

                for (const auto v : live_here) {
                    for (const auto phys : physical_operands) {
                        state.blocked_phys[v].insert(phys.id);
                    }
                }

                for (const auto v : defs) live.erase(v);
                live.insert(uses.begin(), uses.end());
            }
        }

        return true;
    }

    [[nodiscard]] bool candidate_matches_class(const MachineRegister& virtual_reg,
                                                const MachineRegister& physical) const noexcept {
        if (virtual_reg.reg_class == physical.reg_class) return true;
        if (virtual_reg.reg_class == RegisterClassKind::General &&
            (physical.reg_class == RegisterClassKind::Integer ||
             physical.reg_class == RegisterClassKind::Address)) {
            return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<MachineRegister>
    candidates_for(const MachineRegister& virtual_reg) const {
        std::vector<MachineRegister> result;
        const std::size_t count = reg_info_.physical_register_count();
        for (std::uint32_t id = 0; id < count; ++id) {
            for (RegisterClassKind cls : {
                    RegisterClassKind::General,
                    RegisterClassKind::Integer,
                    RegisterClassKind::FloatingPoint,
                    RegisterClassKind::Vector,
                    RegisterClassKind::Predicate,
                    RegisterClassKind::Mask,
                    RegisterClassKind::Address,
                    RegisterClassKind::Special,
                    RegisterClassKind::Custom}) {
                MachineRegister candidate{id, RegisterKind::Physical, cls};
                if (!reg_info_.is_allocatable(candidate) ||
                    reg_info_.is_reserved(candidate) ||
                    !candidate_matches_class(virtual_reg, candidate)) {
                    continue;
                }
                if (reg_info_.register_class(candidate) != cls) {
                    continue;
                }
                if (std::find(result.begin(), result.end(), candidate) == result.end()) {
                    result.push_back(candidate);
                }
            }
        }
        return result;
    }

    [[nodiscard]] bool color_graph(AllocationState& state) {
        std::unordered_set<std::uint32_t> uncolored;
        for (const auto& [id, _] : state.virtuals) uncolored.insert(id);

        while (!uncolored.empty()) {
            std::uint32_t chosen = INVALID_REG;
            std::size_t best_saturation = 0;
            std::size_t best_degree = 0;

            for (const auto id : uncolored) {
                std::unordered_set<std::uint32_t> saturation;
                for (const auto neighbor : state.edges[id]) {
                    const auto assigned = assignments_.find(neighbor);
                    if (assigned != assignments_.end()) {
                        saturation.insert(assigned->second.id);
                    }
                }

                const std::size_t degree = state.edges[id].size();
                if (chosen == INVALID_REG ||
                    saturation.size() > best_saturation ||
                    (saturation.size() == best_saturation && degree > best_degree)) {
                    chosen = id;
                    best_saturation = saturation.size();
                    best_degree = degree;
                }
            }

            const MachineRegister vreg = state.virtuals.at(chosen);
            std::vector<MachineRegister> candidates = candidates_for(vreg);

            const auto fixed_it = state.fixed.find(chosen);
            if (fixed_it != state.fixed.end()) {
                candidates.erase(
                    std::remove_if(candidates.begin(), candidates.end(),
                                   [&](const MachineRegister& c) {
                                       return !(c == fixed_it->second);
                                   }),
                    candidates.end());
            }

            std::optional<MachineRegister> selected;
            for (const MachineRegister candidate : candidates) {
                bool conflict = false;
                const auto blocked = state.blocked_phys.find(chosen);
                if (blocked != state.blocked_phys.end() &&
                    blocked->second.contains(candidate.id)) {
                    conflict = true;
                }

                if (!conflict) {
                    for (const auto neighbor : state.edges[chosen]) {
                        const auto assigned = assignments_.find(neighbor);
                        if (assigned != assignments_.end() &&
                            (assigned->second == candidate ||
                             reg_info_.aliases(assigned->second, candidate))) {
                            conflict = true;
                            break;
                        }
                    }
                }

                if (!conflict) {
                    selected = candidate;
                    break;
                }
            }

            uncolored.erase(chosen);

            if (selected.has_value()) {
                assignments_[chosen] = *selected;
            } else {
                state.spilled.push_back(vreg);
            }
        }

        return true;
    }

    MachineFunction& function_;
    const TargetRegisterInfo& reg_info_;
    const TargetInstructionInfo* instr_info_;
    const TargetFrameInfo* frame_info_;
    MachineSpiller* spiller_;

    std::unordered_map<std::uint32_t, MachineRegister> assignments_;
    std::unordered_map<std::uint32_t, StackSlotId> spills_;
    std::vector<MachineRegister> spilled_virtuals_;
};

} // namespace dgtl::mir::regalloc

