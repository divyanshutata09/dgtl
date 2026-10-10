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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>
#include <string>
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
        failure_reason_.clear();
        if (!function_.is_phase_at_least(MachinePhase::PreRegAlloc)) {
            failure_reason_ = "function has not reached the PreRegAlloc phase";
            return false;
        }

        assignments_.clear();
        spills_.clear();
        spilled_virtuals_.clear();
        spill_temporaries_.clear();

        // Spilling mutates MIR. Rebuild liveness/interference and recolor until
        // all virtual registers are gone or the target spiller reports failure.
        for (std::size_t iteration = 0; iteration < kMaxAllocationIterations; ++iteration) {
            if (!function_.has_virtual_registers()) {
                ensure_target_opcodes();
                function_.mark_registers_allocated();
                if (!function_.properties().registers_allocated) {
                    failure_reason_ = "failed to mark registers allocated (missing target opcodes)";
                    return false;
                }
                return true;
            }

            AllocationState state;
            if (!build_interference_graph(state)) {
                if (failure_reason_.empty()) {
                    failure_reason_ = "could not build the interference graph (invalid instruction, "
                                      "successor block or conflicting fixed-register constraint)";
                }
                return false;
            }

            if (!color_graph(state)) {
                if (failure_reason_.empty()) failure_reason_ = "graph coloring failed";
                return false;
            }

            if (state.spilled.empty()) {
                for (const auto& [v_id, p_reg] : assignments_) {
                    MachineRegister vreg{v_id, RegisterKind::Virtual, p_reg.reg_class};
                    if (!function_.replace_virtual_register(vreg, p_reg)) {
                        failure_reason_ = "failed to rewrite v" + std::to_string(v_id) +
                                          " to physical r" + std::to_string(p_reg.id);
                        return false;
                    }
                }

                if (function_.has_virtual_registers()) {
                    failure_reason_ = "virtual registers remain after rewriting (register class mismatch "
                                      "between definition and use of the same virtual register)";
                    return false;
                }

                ensure_target_opcodes();
                function_.mark_registers_allocated();
                if (!function_.properties().registers_allocated) {
                    failure_reason_ = "failed to mark registers allocated (missing target opcodes or virtual registers remaining)";
                    return false;
                }
                return true;
            }

            if (spiller_ == nullptr || frame_info_ == nullptr) {
                failure_reason_ = std::to_string(state.spilled.size()) +
                                  " virtual register(s) could not be colored and no spiller is available; " +
                                  first_spill_detail_;
                return false;
            }

            std::unordered_set<std::uint32_t> existing_vregs;
            for (const auto& [vid, _] : state.virtuals) {
                existing_vregs.insert(vid);
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
                    failure_reason_ = "spiller rejected v" + std::to_string(vreg.id);
                    return false;
                }
                spills_[vreg.id] = result.slot;
                spilled_virtuals_.push_back(vreg);

                if (!spiller_->rewrite_for_spill(function_, vreg, result.slot)) {
                    failure_reason_ = "spill rewrite failed for v" + std::to_string(vreg.id);
                    return false;
                }
            }

            for (std::size_t i = 0; i < function_.instructions().size(); ++i) {
                const auto* inst = function_.instruction(static_cast<InstrId>(i));
                if (!inst || inst->parent_block == dgtl::machine::INVALID_BLOCK_ID) continue;
                for (const auto& op : inst->operands) {
                    if (op.kind == MachineOperandKind::Register && op.reg.is_virtual()) {
                        if (!existing_vregs.contains(op.reg.id)) {
                            spill_temporaries_.insert(op.reg.id);
                        }
                    } else if (op.kind == MachineOperandKind::Memory) {
                        if (op.memory.base.is_virtual() && !existing_vregs.contains(op.memory.base.id)) {
                            spill_temporaries_.insert(op.memory.base.id);
                        }
                        if (op.memory.index.is_virtual() && !existing_vregs.contains(op.memory.index.id)) {
                            spill_temporaries_.insert(op.memory.index.id);
                        }
                    }
                }
            }

            assignments_.clear();
        }

        failure_reason_ = "allocation did not converge after " +
                          std::to_string(kMaxAllocationIterations) + " spill iterations";
        return false;
    }

    [[nodiscard]] const std::string& failure_reason() const noexcept {
        return failure_reason_;
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
    static constexpr std::size_t kMaxAllocationIterations = 16;

    void ensure_target_opcodes() {
        if (instr_info_ == nullptr) return;
        for (std::size_t i = 0; i < function_.instructions().size(); ++i) {
            auto* inst = function_.instruction(static_cast<InstrId>(i));
            if (!inst || inst->parent_block == dgtl::machine::INVALID_BLOCK_ID) continue;
            if (!inst->has_target_opcode()) {
                MachineOpcode target_op = instr_info_->opcode_for(inst->form, inst->semantic_kind);
                if (target_op == dgtl::machine::INVALID_MACHINE_OPCODE) {
                    if (inst->form == MachineForm::Move) target_op = instr_info_->opcode_for(MachineForm::Move, MachineSemanticKind::Move);
                    else if (inst->form == MachineForm::StoreStack) target_op = instr_info_->opcode_for(MachineForm::StoreStack, MachineSemanticKind::StoreStack);
                    else if (inst->form == MachineForm::LoadStack) target_op = instr_info_->opcode_for(MachineForm::LoadStack, MachineSemanticKind::LoadStack);
                    else if (inst->form == MachineForm::Return) target_op = instr_info_->opcode_for(MachineForm::Return, MachineSemanticKind::Return);
                }
                if (target_op != dgtl::machine::INVALID_MACHINE_OPCODE) {
                    (void)inst->assign_target_opcode_once(target_op);
                }
            }
        }
    }

    struct AllocationState {
        std::unordered_map<std::uint32_t, MachineRegister> virtuals;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> edges;
        std::unordered_map<std::uint32_t, std::vector<MachineRegister>> blocked_phys;
        std::unordered_map<std::uint32_t, MachineRegister> fixed;
        std::unordered_map<std::uint32_t, std::size_t> use_def_counts;
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

        const std::size_t num_blocks = function_.blocks().size();
        if (num_blocks == 0) return true;

        std::uint32_t max_vid = 0;
        for (const auto& [vid, _] : state.virtuals) {
            max_vid = std::max(max_vid, vid);
        }
        const std::size_t num_words = state.virtuals.empty() ? 0 : ((static_cast<std::size_t>(max_vid) + 64u) / 64u);

        // Build block-level use/def sets as flat bitset vectors.
        std::vector<std::uint64_t> use(num_blocks * num_words, 0ULL);
        std::vector<std::uint64_t> def(num_blocks * num_words, 0ULL);
        std::vector<std::uint64_t> live_in(num_blocks * num_words, 0ULL);
        std::vector<std::uint64_t> live_out(num_blocks * num_words, 0ULL);

        auto set_bit = [&](std::vector<std::uint64_t>& table, std::size_t b, std::uint32_t v) noexcept {
            if (num_words == 0) return;
            const std::size_t word_idx = b * num_words + (v / 64u);
            table[word_idx] |= (1ULL << (v % 64u));
        };
        auto test_bit = [&](const std::vector<std::uint64_t>& table, std::size_t b, std::uint32_t v) noexcept -> bool {
            if (num_words == 0) return false;
            const std::size_t word_idx = b * num_words + (v / 64u);
            return (table[word_idx] & (1ULL << (v % 64u))) != 0ULL;
        };

        for (std::size_t b = 0; b < num_blocks; ++b) {
            for (InstrId id : function_.blocks()[b].instructions) {
                const auto* inst = function_.instruction(id);
                if (!inst) return false;

                std::unordered_set<std::uint32_t> uses;
                std::unordered_set<std::uint32_t> defs;
                for (const auto& op : inst->operands) {
                    classify_operand(op, uses, defs);
                }

                for (const auto v : uses) {
                    if (!test_bit(def, b, v)) set_bit(use, b, v);
                    state.use_def_counts[v] += 1;
                }
                for (const auto v : defs) {
                    state.use_def_counts[v] += 1;
                    set_bit(def, b, v);
                }
            }
        }

        // Standard backward data-flow liveness over the actual MIR CFG using bitset operations.
        if (num_words > 0) {
            std::vector<std::uint64_t> new_out(num_words, 0ULL);
            std::vector<std::uint64_t> new_in(num_words, 0ULL);

            bool changed = true;
            for (std::size_t pass = 0; pass < num_blocks + 1u && changed; ++pass) {
                changed = false;
                for (std::size_t reverse = num_blocks; reverse-- > 0;) {
                    const auto& block = function_.blocks()[reverse];
                    const std::size_t b_offset = reverse * num_words;

                    std::fill(new_out.begin(), new_out.end(), 0ULL);
                    for (BlockId succ : block.successors) {
                        if (succ >= num_blocks) return false;
                        const std::size_t succ_offset = succ * num_words;
                        for (std::size_t w = 0; w < num_words; ++w) {
                            new_out[w] |= live_in[succ_offset + w];
                        }
                    }

                    for (std::size_t w = 0; w < num_words; ++w) {
                        new_in[w] = use[b_offset + w] | (new_out[w] & ~def[b_offset + w]);
                    }

                    bool block_changed = false;
                    for (std::size_t w = 0; w < num_words; ++w) {
                        if (live_out[b_offset + w] != new_out[w] || live_in[b_offset + w] != new_in[w]) {
                            block_changed = true;
                            break;
                        }
                    }
                    if (block_changed) {
                        std::copy(new_out.begin(), new_out.end(), live_out.begin() + b_offset);
                        std::copy(new_in.begin(), new_in.end(), live_in.begin() + b_offset);
                        changed = true;
                    }
                }
            }
        }

        // Instruction-level interference and physical-register blocking.
        for (std::size_t b = 0; b < num_blocks; ++b) {
            std::unordered_set<std::uint32_t> live;
            if (num_words > 0) {
                const std::size_t b_offset = b * num_words;
                for (std::size_t w = 0; w < num_words; ++w) {
                    std::uint64_t word = live_out[b_offset + w];
                    while (word != 0ULL) {
                        const unsigned ctz = std::countr_zero(word);
                        const std::uint32_t v = static_cast<std::uint32_t>(w * 64u + ctz);
                        live.insert(v);
                        word &= (word - 1ULL);
                    }
                }
            }

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

                const bool is_move = (inst->form == dgtl::machine::MachineForm::Move ||
                                      inst->opcode == dgtl::machine::GM_COPY);
                for (const auto v : live_here) {
                    for (const auto phys : physical_operands) {
                        // If this is a move/copy instruction and v is an operand of it,
                        // v is being transferred to/from phys and must NOT be blocked by phys.
                        if (is_move && (uses.contains(v) || defs.contains(v))) {
                            continue;
                        }
                        // If v is a use that is killed at this instruction (!live.contains(v))
                        // and phys is a def, v can share or coalesce with phys without conflict.
                        if (uses.contains(v) && !live.contains(v)) {
                            bool phys_is_def = false;
                            for (const auto& op : inst->operands) {
                                if (op.is_def() && op.kind == MachineOperandKind::Register && op.reg == phys) {
                                    phys_is_def = true;
                                    break;
                                }
                            }
                            if (phys_is_def) continue;
                        }
                        auto& blocked = state.blocked_phys[v];
                        if (std::find(blocked.begin(), blocked.end(), phys) == blocked.end()) {
                            blocked.push_back(phys);
                        }
                    }
                }

                const bool is_call_instr = (inst->form == dgtl::machine::MachineForm::Call ||
                                            inst->has_flag(dgtl::machine::MachineInstrFlag::Call) ||
                                            inst->opcode == dgtl::machine::GM_CALL ||
                                            inst->opcode == dgtl::machine::GM_CALL_VARIADIC);
                if (is_call_instr) {
                    const std::size_t phys_count = reg_info_.physical_register_count();
                    for (const auto v : live) {
                        const auto vit = state.virtuals.find(v);
                        if (vit == state.virtuals.end()) continue;
                        auto& blocked = state.blocked_phys[v];
                        for (std::uint32_t p = 0; p < phys_count; ++p) {
                            dgtl::machine::MachineRegister phys_reg{p, dgtl::machine::RegisterKind::Physical, vit->second.reg_class};
                            if (reg_info_.is_caller_saved(phys_reg)) {
                                if (std::find(blocked.begin(), blocked.end(), phys_reg) == blocked.end()) {
                                    blocked.push_back(phys_reg);
                                }
                            }
                        }
                    }
                }

                for (const auto v : defs) live.erase(v);
                live.insert(uses.begin(), uses.end());
            }
        }

        return true;
    }

    [[nodiscard]] static constexpr bool is_gpr_family(RegisterClassKind cls) noexcept {
        return cls == RegisterClassKind::General ||
               cls == RegisterClassKind::Integer ||
               cls == RegisterClassKind::Address;
    }

    [[nodiscard]] bool candidate_matches_class(const MachineRegister& virtual_reg,
                                                const MachineRegister& physical) const noexcept {
        if (virtual_reg.reg_class == physical.reg_class) return true;
        // General, Integer and Address share one physical GPR file.
        return is_gpr_family(virtual_reg.reg_class) && is_gpr_family(physical.reg_class);
    }

    [[nodiscard]] std::vector<MachineRegister>
    candidates_for(const MachineRegister& virtual_reg) const {
        std::vector<MachineRegister> result;
        const std::size_t count = reg_info_.physical_register_count();
        const bool gpr_vreg = is_gpr_family(virtual_reg.reg_class);
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
                const RegisterClassKind target_cls = reg_info_.register_class(candidate);
                const bool class_ok = (target_cls == cls) ||
                    (gpr_vreg && is_gpr_family(cls) && is_gpr_family(target_cls));
                if (!class_ok) {
                    continue;
                }
                // Keep the virtual register's own class on the assignment so
                // downstream consumers (emitters, ABI) still see e.g. Address.
                if (gpr_vreg) candidate.reg_class = virtual_reg.reg_class;
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
            bool chosen_is_spill_temp = false;

            for (const auto id : uncolored) {
                const bool is_spill_temp = spill_temporaries_.contains(id);
                std::unordered_set<std::uint32_t> saturation;
                for (const auto neighbor : state.edges[id]) {
                    const auto assigned = assignments_.find(neighbor);
                    if (assigned != assignments_.end()) {
                        saturation.insert(assigned->second.id);
                    }
                }

                const std::size_t degree = state.edges[id].size();
                bool better = false;
                if (chosen == INVALID_REG) {
                    better = true;
                } else if (is_spill_temp != chosen_is_spill_temp) {
                    better = is_spill_temp; // Always prioritize spill temporaries
                } else if (saturation.size() > best_saturation) {
                    better = true;
                } else if (saturation.size() == best_saturation && degree > best_degree) {
                    better = true;
                }

                if (better) {
                    chosen = id;
                    best_saturation = saturation.size();
                    best_degree = degree;
                    chosen_is_spill_temp = is_spill_temp;
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
                    std::any_of(blocked->second.begin(), blocked->second.end(),
                                [&](const MachineRegister& phys) {
                                    return phys == candidate || reg_info_.aliases(phys, candidate);
                                })) {
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
            } else if (spill_temporaries_.contains(chosen)) {
                // Neighbor Eviction for spill temporary:
                // Find a candidate register whose conflicting neighbors are not spill temporaries,
                // and evict the neighbor with the lowest Chaitin-Briggs spill cost.
                std::optional<MachineRegister> best_candidate;
                std::vector<std::uint32_t> best_neighbors_to_evict;
                double min_eviction_cost = std::numeric_limits<double>::infinity();

                const auto blocked = state.blocked_phys.find(chosen);
                for (const MachineRegister candidate : candidates) {
                    // Cannot evict if candidate is blocked by fixed physical registers
                    if (blocked != state.blocked_phys.end() &&
                        std::any_of(blocked->second.begin(), blocked->second.end(),
                                    [&](const MachineRegister& phys) {
                                        return phys == candidate || reg_info_.aliases(phys, candidate);
                                    })) {
                        continue;
                    }

                    // Find all neighbors occupying candidate
                    std::vector<std::uint32_t> conflicting_neighbors;
                    bool has_unskippable_conflict = false;
                    for (const auto neighbor : state.edges[chosen]) {
                        const auto assigned = assignments_.find(neighbor);
                        if (assigned != assignments_.end() &&
                            (assigned->second == candidate || reg_info_.aliases(assigned->second, candidate))) {
                            if (spill_temporaries_.contains(neighbor)) {
                                has_unskippable_conflict = true;
                                break;
                            }
                            conflicting_neighbors.push_back(neighbor);
                        }
                    }

                    if (has_unskippable_conflict || conflicting_neighbors.empty()) {
                        continue;
                    }

                    // Compute total Chaitin-Briggs spill cost for conflicting neighbors
                    double total_cost = 0.0;
                    for (const auto n : conflicting_neighbors) {
                        const auto it_cnt = state.use_def_counts.find(n);
                        const double cnt = static_cast<double>(it_cnt != state.use_def_counts.end() ? it_cnt->second : 1u);
                        const double deg = static_cast<double>(state.edges[n].size() + 1);
                        total_cost += (cnt / deg);
                    }

                    if (total_cost < min_eviction_cost) {
                        min_eviction_cost = total_cost;
                        best_candidate = candidate;
                        best_neighbors_to_evict = std::move(conflicting_neighbors);
                    }
                }

                if (best_candidate.has_value()) {
                    for (const auto n : best_neighbors_to_evict) {
                        assignments_.erase(n);
                        const auto v_it = state.virtuals.find(n);
                        if (v_it != state.virtuals.end() &&
                            std::none_of(state.spilled.begin(), state.spilled.end(), [&](MachineRegister r) { return r.id == n; })) {
                            state.spilled.push_back(v_it->second);
                        }
                    }
                    assignments_[chosen] = *best_candidate;
                } else {
                    failure_reason_ = "spill temporary v" + std::to_string(chosen) +
                                      " could not be colored (blocked by fixed registers or register class mismatch)";
                    return false;
                }
            } else {
                if (state.spilled.empty()) {
                    std::size_t colored_neighbors = 0;
                    for (const auto neighbor : state.edges[chosen]) {
                        if (assignments_.contains(neighbor)) ++colored_neighbors;
                    }
                    const auto blocked = state.blocked_phys.find(chosen);
                    first_spill_detail_ =
                        "first uncolorable: v" + std::to_string(chosen) +
                        " class=" + std::to_string(static_cast<unsigned>(vreg.reg_class)) +
                        " candidates=" + std::to_string(candidates.size()) +
                        " interfering=" + std::to_string(state.edges[chosen].size()) +
                        " colored_neighbors=" + std::to_string(colored_neighbors) +
                        " blocked_phys=" + std::to_string(
                            blocked != state.blocked_phys.end() ? blocked->second.size() : 0u) +
                        (fixed_it != state.fixed.end()
                            ? " fixed=r" + std::to_string(fixed_it->second.id)
                            : std::string{});
                }
                state.spilled.push_back(vreg);
            }
        }

        if (!state.spilled.empty()) {
            std::sort(state.spilled.begin(), state.spilled.end(), [&](MachineRegister a, MachineRegister b) {
                const auto it_a = state.use_def_counts.find(a.id);
                const auto it_b = state.use_def_counts.find(b.id);
                const double count_a = static_cast<double>(it_a != state.use_def_counts.end() ? it_a->second : 1u);
                const double count_b = static_cast<double>(it_b != state.use_def_counts.end() ? it_b->second : 1u);
                const double degree_a = static_cast<double>(state.edges[a.id].size() + 1);
                const double degree_b = static_cast<double>(state.edges[b.id].size() + 1);
                return (count_a / degree_a) < (count_b / degree_b);
            });
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
    std::unordered_set<std::uint32_t> spill_temporaries_;
    std::string failure_reason_;
    std::string first_spill_detail_;
};

} // namespace dgtl::mir::regalloc

