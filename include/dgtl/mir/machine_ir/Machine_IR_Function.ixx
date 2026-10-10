/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine Function, Basic Block CFG, Stack Frame & Constant Pool
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.mir.machine_ir:function;

import dgtl.lut_fsm;
import :types;
import :target;

export namespace dgtl::machine {

class MachineDominatorTree;
class MachineDominanceFrontier;
class MachineLoopInfo;

struct MachineBasicBlock {
    BlockId id = INVALID_BLOCK_ID;

    InstrId head_instr = INVALID_INSTR_ID;
    InstrId tail_instr = INVALID_INSTR_ID;
    std::size_t instr_count = 0;

    std::vector<InstrId> instructions;
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;

    std::vector<MachineRegister> live_ins;
    std::vector<MachineRegister> live_outs;
    std::vector<MachineRegister> formal_params;

    bool entry = false;
    bool exit = false;
    bool cold = false;
    bool address_taken = false;

    [[nodiscard]] bool valid() const noexcept {
        return id != INVALID_BLOCK_ID;
    }

    void add_instruction(InstrId instr) {
        instructions.push_back(instr);
    }

    void add_successor(BlockId b) {
        if (std::find(successors.begin(), successors.end(), b) == successors.end()) {
            successors.push_back(b);
        }
    }

    void remove_successor(BlockId b) {
        auto it = std::find(successors.begin(), successors.end(), b);
        if (it != successors.end()) {
            successors.erase(it);
        }
    }

    [[nodiscard]] bool has_successor(BlockId b) const noexcept {
        return std::find(successors.begin(), successors.end(), b) != successors.end();
    }

    void add_predecessor(BlockId b) {
        if (std::find(predecessors.begin(), predecessors.end(), b) == predecessors.end()) {
            predecessors.push_back(b);
        }
    }

    void remove_predecessor(BlockId b) {
        auto it = std::find(predecessors.begin(), predecessors.end(), b);
        if (it != predecessors.end()) {
            predecessors.erase(it);
        }
    }

    [[nodiscard]] bool has_predecessor(BlockId b) const noexcept {
        return std::find(predecessors.begin(), predecessors.end(), b) != predecessors.end();
    }
};

enum class StackSlotKind : std::uint8_t {
    Spill,
    Local,
    IncomingArgument,
    OutgoingArgument,
    CalleeSaved,
    VariableSized,
    Alignment,
    Temporary
};

struct MachineStackSlot {
    StackSlotId id = INVALID_STACK_SLOT;
    StackSlotKind kind = StackSlotKind::Temporary;

    std::uint32_t size_bytes = 0;
    std::uint32_t alignment = 1;

    std::int64_t offset = 0;
    bool fixed_offset = false;
    bool is_dead = false;
};

struct MachineFrameInfo {
    std::vector<MachineStackSlot> slots;

    std::uint32_t stack_alignment = 1;
    std::uint32_t outgoing_argument_bytes = 0;
    std::uint32_t local_bytes = 0;
    std::uint32_t spill_bytes = 0;
    std::uint32_t callee_saved_bytes = 0;
    std::vector<MachineRegister> callee_saved_registers;
    std::uint32_t alignment_padding = 0;
    std::uint32_t total_frame_bytes = 0;

    bool has_calls = false;
    bool has_variable_sized_objects = false;
    bool has_stack_protector = false;

    [[nodiscard]] StackSlotId create_slot(StackSlotKind kind,
                                          std::uint32_t size,
                                          std::uint32_t alignment) {
        const StackSlotId id = static_cast<StackSlotId>(slots.size());
        slots.push_back({
            id, kind, size, std::max<std::uint32_t>(alignment, 1u),
            0, false, false
        });
        return id;
    }
};

struct MachineProfile {
    std::uint32_t spill_count = 0;
    std::uint32_t reload_count = 0;
    std::uint32_t peak_register_pressure = 0;
    std::uint32_t estimated_cycle_cost = 0;
    std::uint32_t vector_lane_utilization = 0;
    std::uint32_t branch_mispredict_weight = 0;
    bool contains_atomic_fences = false;
    bool contains_accelerator_calls = false;
};

struct MachineConstantPoolEntry {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> data;
    std::uint32_t alignment = 4;
    std::uint32_t offset = 0;
    bool is_machine_specific = false;
};

class MachineConstantPool {
    std::vector<MachineConstantPoolEntry> entries_;
    std::uint32_t total_size_ = 0;
    std::uint32_t max_alignment_ = 1;

public:
    [[nodiscard]] std::uint32_t add_entry(std::span<const std::uint8_t> bytes, std::uint32_t alignment = 4) {
        for (const auto& entry : entries_) {
            if (entry.alignment == alignment && entry.data.size() == bytes.size()) {
                if (std::equal(entry.data.begin(), entry.data.end(), bytes.begin())) {
                    return entry.id;
                }
            }
        }
        const std::uint32_t id = static_cast<std::uint32_t>(entries_.size());
        MachineConstantPoolEntry entry{};
        entry.id = id;
        entry.data.assign(bytes.begin(), bytes.end());
        entry.alignment = std::max(alignment, 1u);
        max_alignment_ = std::max(max_alignment_, entry.alignment);
        
        const std::uint32_t rem = total_size_ % entry.alignment;
        if (rem != 0) {
            total_size_ += (entry.alignment - rem);
        }
        entry.offset = total_size_;
        total_size_ += static_cast<std::uint32_t>(entry.data.size());

        entries_.push_back(std::move(entry));
        return id;
    }

    [[nodiscard]] std::uint32_t add_float(float val) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&val);
        return add_entry(std::span<const std::uint8_t>(bytes, sizeof(float)), alignof(float));
    }

    [[nodiscard]] std::uint32_t add_double(double val) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&val);
        return add_entry(std::span<const std::uint8_t>(bytes, sizeof(double)), alignof(double));
    }

    [[nodiscard]] std::uint32_t add_uint64(std::uint64_t val) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&val);
        return add_entry(std::span<const std::uint8_t>(bytes, sizeof(std::uint64_t)), alignof(std::uint64_t));
    }

    [[nodiscard]] const std::vector<MachineConstantPoolEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] const MachineConstantPoolEntry* entry(std::uint32_t id) const noexcept {
        if (id >= entries_.size()) return nullptr;
        return &entries_[id];
    }

    [[nodiscard]] std::uint32_t total_size() const noexcept { return total_size_; }
    [[nodiscard]] std::uint32_t max_alignment() const noexcept { return max_alignment_; }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    void clear() noexcept { entries_.clear(); total_size_ = 0; max_alignment_ = 1; }
};

struct MachineJumpTableEntry {
    std::uint32_t id = 0;
    std::vector<BlockId> targets;
    std::uint32_t alignment = 8;
    std::uint32_t offset = 0;
};

class MachineJumpTableInfo {
    std::vector<MachineJumpTableEntry> entries_;

public:
    [[nodiscard]] std::uint32_t create_jump_table(std::vector<BlockId> targets, std::uint32_t alignment = 8) {
        const std::uint32_t id = static_cast<std::uint32_t>(entries_.size());
        MachineJumpTableEntry entry{};
        entry.id = id;
        entry.targets = std::move(targets);
        entry.alignment = std::max(alignment, 1u);
        entries_.push_back(std::move(entry));
        return id;
    }

    [[nodiscard]] const std::vector<MachineJumpTableEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] const MachineJumpTableEntry* entry(std::uint32_t id) const noexcept {
        if (id >= entries_.size()) return nullptr;
        return &entries_[id];
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    void clear() noexcept { entries_.clear(); }
};

enum class BlockBranchTopology : std::uint8_t {
    EmptyOrFallthrough = 0,
    UnconditionalBranch = 1,
    ConditionalBranchWithFallthrough = 2,
    ConditionalAndUnconditionalBranch = 3,
    Return = 4,
    SyscallOrTrap = 5,
    IndirectBranch = 6,
    Malformed = 7
};

struct MachineFunctionProperties {
    bool is_ssa = false;
    bool tracks_liveness = false;
    bool registers_allocated = false;
    bool frame_lowered = false;
    bool abi_lowered = false;
    bool contains_calls = false;
    bool contains_dynamic_alloca = false;
};

[[nodiscard]] constexpr dgtl::utils::BitMatrixLUT<8, 8> build_phase_transition_lut() noexcept {
    dgtl::utils::BitMatrixLUT<8, 8> lut{};
    for (std::size_t from = 0; from < 7; ++from) {
        for (std::size_t to = from + 1; to < 7; ++to) {
            lut.set(from, to, true);
        }
    }
    return lut;
}

inline constexpr auto kPhaseTransitionMatrix = build_phase_transition_lut();

class MachineFunction {
public:
    explicit MachineFunction(std::string_view name = {})
        : name_(name) {}

    [[nodiscard]] std::string_view name() const noexcept {
        return name_;
    }

    [[nodiscard]] MachinePhase phase() const noexcept {
        return phase_;
    }

    [[nodiscard]] static constexpr bool is_legal_phase_transition(MachinePhase from, MachinePhase to) noexcept {
        const auto f = static_cast<std::size_t>(from);
        const auto t = static_cast<std::size_t>(to);
        if (f >= 8 || t >= 8) return false;
        return kPhaseTransitionMatrix.get(f, t);
    }

    void set_phase(MachinePhase phase) noexcept {
        if (static_cast<std::uint8_t>(phase) >= static_cast<std::uint8_t>(phase_)) {
            phase_ = phase;
        }
    }

    [[nodiscard]] bool advance_phase(MachinePhase expected_current,
                                     MachinePhase next) noexcept {
        if (phase_ != expected_current) {
            return false;
        }
        if (!is_legal_phase_transition(expected_current, next)) {
            return false;
        }
        phase_ = next;
        return true;
    }

    [[nodiscard]] MachineFunctionProperties& properties() noexcept {
        return properties_;
    }

    [[nodiscard]] const MachineFunctionProperties&
    properties() const noexcept {
        return properties_;
    }

    void invalidate_analysis_caches() noexcept;

    MachineBasicBlock& create_block(bool entry = false) {
        invalidate_analysis_caches();
        if (blocks_.size() >= static_cast<std::size_t>(INVALID_BLOCK_ID)) {
            throw std::overflow_error("MachineFunction block id exhausted");
        }
        const BlockId id = static_cast<BlockId>(blocks_.size());
        blocks_.push_back({});
        blocks_.back().id = id;
        blocks_.back().entry = entry;
        if (entry && entry_block_ == INVALID_BLOCK_ID) {
            entry_block_ = id;
        }
        return blocks_.back();
    }

    MachineBasicBlock* block(BlockId id) noexcept {
        if (id >= blocks_.size()) return nullptr;
        return &blocks_[id];
    }

    const MachineBasicBlock* block(BlockId id) const noexcept {
        if (id >= blocks_.size()) return nullptr;
        return &blocks_[id];
    }

    bool connect_blocks(BlockId from, BlockId to) {
        MachineBasicBlock* source = block(from);
        MachineBasicBlock* target = block(to);
        if (source == nullptr || target == nullptr) {
            return false;
        }
        invalidate_analysis_caches();
        source->add_successor(to);
        target->add_predecessor(from);
        return true;
    }

    bool disconnect_blocks(BlockId from, BlockId to) {
        MachineBasicBlock* source = block(from);
        MachineBasicBlock* target = block(to);
        if (source == nullptr || target == nullptr) {
            return false;
        }
        invalidate_analysis_caches();
        source->remove_successor(to);
        target->remove_predecessor(from);
        return true;
    }

    [[nodiscard]] std::span<MachineBasicBlock> blocks() noexcept {
        return blocks_;
    }

    [[nodiscard]] std::span<const MachineBasicBlock> blocks() const noexcept {
        return blocks_;
    }

    MachineInstruction& create_instruction() {
        return create_instruction(INVALID_MACHINE_OPCODE);
    }

    MachineInstruction& create_instruction(MachineOpcode opcode) {
        if (instructions_.size() >= static_cast<std::size_t>(INVALID_INSTR_ID)) {
            throw std::overflow_error("MachineFunction instruction id exhausted");
        }
        const InstrId id = static_cast<InstrId>(instructions_.size());
        instructions_.push_back({});
        instructions_.back().id = id;
        instructions_.back().opcode = opcode;
        if (is_target_opcode(opcode)) {
            (void)instructions_.back().assign_target_opcode_once(opcode);
        }
        return instructions_.back();
    }

    [[nodiscard]] bool insert_instruction_before(BlockId block_id, InstrId cursor_id, InstrId new_instr_id);
    void remove_instruction(InstrId instr_id);
    void splice_instructions(BlockId dest_block_id, InstrId cursor_id,
                             BlockId src_block_id, InstrId first_instr_id, InstrId last_instr_id);

    MachineInstruction* instruction(InstrId id) noexcept {
        if (id >= instructions_.size()) return nullptr;
        return &instructions_[id];
    }

    const MachineInstruction* instruction(InstrId id) const noexcept {
        if (id >= instructions_.size()) return nullptr;
        return &instructions_[id];
    }

    [[nodiscard]] std::span<MachineInstruction> instructions() noexcept {
        return instructions_;
    }

    [[nodiscard]] std::span<const MachineInstruction> instructions() const noexcept {
        return instructions_;
    }

    [[nodiscard]] std::size_t instruction_count() const noexcept {
        return instructions_.size();
    }

    MachineRegister create_virtual_register(
        RegisterClassKind reg_class = RegisterClassKind::General) {
        if (next_virtual_reg_ == INVALID_REG) {
            throw std::overflow_error("MachineFunction virtual register id exhausted");
        }
        const RegId id = next_virtual_reg_++;
        return MachineRegister{id, RegisterKind::Virtual, reg_class};
    }

    void add_stack_slot(MachineStackSlot slot) {
        if (slot.id == INVALID_STACK_SLOT) {
            slot.id = static_cast<StackSlotId>(frame_.slots.size());
        }
        frame_.slots.push_back(slot);
    }

    [[nodiscard]] MachineFrameInfo& frame_info() noexcept {
        return frame_;
    }

    [[nodiscard]] const MachineFrameInfo& frame_info() const noexcept {
        return frame_;
    }

    [[nodiscard]] BlockId entry_block() const noexcept {
        return entry_block_;
    }

    [[nodiscard]] BlockId block_for_label(std::uint32_t label_id) {
        auto it = label_to_block_.find(label_id);
        if (it != label_to_block_.end()) {
            return it->second;
        }
        MachineBasicBlock& bb = create_block(blocks_.empty());
        label_to_block_[label_id] = bb.id;
        return bb.id;
    }

    void map_label_to_block(std::uint32_t label_id, BlockId block_id) {
        label_to_block_[label_id] = block_id;
    }

    [[nodiscard]] std::optional<BlockId> find_block_for_label(std::uint32_t label_id) const noexcept {
        auto it = label_to_block_.find(label_id);
        if (it != label_to_block_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void add_param(MachineRegister reg) {
        params_.push_back(reg);
    }

    [[nodiscard]] std::span<const MachineRegister> params() const noexcept {
        return params_;
    }

    [[nodiscard]] std::size_t virtual_register_count() const noexcept {
        return next_virtual_reg_;
    }

    [[nodiscard]] bool assign_target_opcode(
        InstrId instruction_id, MachineOpcode target_opcode) noexcept {
        MachineInstruction* inst = instruction(instruction_id);
        return inst != nullptr && inst->assign_target_opcode_once(target_opcode);
    }

    [[nodiscard]] bool has_target_opcodes() const noexcept {
        for (const MachineInstruction& inst : instructions_) {
            if (inst.parent_block == INVALID_BLOCK_ID) continue;
            if (!inst.has_target_opcode()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool is_phase_at_least(MachinePhase phase) const noexcept {
        return static_cast<std::uint8_t>(phase_) >= static_cast<std::uint8_t>(phase);
    }

    [[nodiscard]] bool has_virtual_registers() const noexcept {
        for (const MachineInstruction& inst : instructions_) {
            if (inst.parent_block == INVALID_BLOCK_ID) continue;
            for (const MachineOperand& op : inst.operands) {
                if (op.kind == MachineOperandKind::Register && op.reg.is_virtual()) {
                    return true;
                }
                if (op.kind == MachineOperandKind::Memory) {
                    if (op.memory.base.is_virtual() || op.memory.index.is_virtual()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool replace_virtual_register(
        MachineRegister virtual_reg,
        MachineRegister physical_reg) noexcept {
        if (!virtual_reg.is_virtual() || !physical_reg.is_physical()) {
            return false;
        }

        bool replaced = false;
        // Identity of a virtual register is its id; the same vreg can be
        // referenced with different (compatible) classes, e.g. a value defined
        // as Address and used as a General memory base. Keep each operand's
        // own class so class-sensitive consumers still see the right kind.
        const auto rewrite = [&](MachineRegister& reg) noexcept {
            if (reg.is_virtual() && reg.id == virtual_reg.id) {
                const RegisterClassKind cls = reg.reg_class;
                reg = physical_reg;
                reg.reg_class = cls;
                replaced = true;
            }
        };
        for (MachineInstruction& inst : instructions_) {
            if (inst.parent_block == INVALID_BLOCK_ID) continue;
            for (MachineOperand& op : inst.operands) {
                if (op.kind == MachineOperandKind::Register) {
                    rewrite(op.reg);
                } else if (op.kind == MachineOperandKind::Memory) {
                    rewrite(op.memory.base);
                    rewrite(op.memory.index);
                }
            }
        }
        return replaced;
    }

    [[nodiscard]] bool mark_target_lowered() noexcept {
        if (!has_target_opcodes()) {
            return false;
        }
        if (static_cast<std::uint8_t>(phase_) < static_cast<std::uint8_t>(MachinePhase::PreRegAlloc)) {
            phase_ = MachinePhase::PreRegAlloc;
        }
        return true;
    }

    [[nodiscard]] bool mark_frame_lowered() noexcept {
        if (!properties_.registers_allocated || has_virtual_registers()) {
            return false;
        }
        properties_.frame_lowered = true;
        phase_ = MachinePhase::FrameLowered;
        return true;
    }

    [[nodiscard]] bool mark_abi_lowered() noexcept {
        if (!properties_.frame_lowered) {
            return false;
        }
        properties_.abi_lowered = true;
        phase_ = MachinePhase::ABILowered;
        return true;
    }

    [[nodiscard]] bool mark_emission_ready() noexcept {
        if (!properties_.abi_lowered || has_virtual_registers() || !has_target_opcodes()) {
            return false;
        }
        phase_ = MachinePhase::EmissionReady;
        return true;
    }

    void mark_registers_allocated() noexcept {
        properties_.registers_allocated = has_target_opcodes() && !has_virtual_registers();
        if (properties_.registers_allocated && is_phase_at_least(MachinePhase::PreRegAlloc)) {
            phase_ = MachinePhase::PostRegAlloc;
        }
    }

    [[nodiscard]] MachineConstantPool& constant_pool() noexcept {
        return constant_pool_;
    }

    [[nodiscard]] const MachineConstantPool& constant_pool() const noexcept {
        return constant_pool_;
    }

    [[nodiscard]] MachineJumpTableInfo& jump_table_info() noexcept {
        return jump_table_info_;
    }

    [[nodiscard]] const MachineJumpTableInfo& jump_table_info() const noexcept {
        return jump_table_info_;
    }

    [[nodiscard]] MachineProfile& profile() noexcept {
        return profile_;
    }

    [[nodiscard]] const MachineProfile& profile() const noexcept {
        return profile_;
    }

    void recompute_dominators() const;
    [[nodiscard]] const MachineDominatorTree& dominators() const;
    [[nodiscard]] bool dominates(BlockId a, BlockId b) const;
    [[nodiscard]] bool strictly_dominates(BlockId a, BlockId b) const;
    [[nodiscard]] bool instruction_dominates(InstrId a, InstrId b) const;
    [[nodiscard]] bool dominates(const MachineInstruction& a, const MachineInstruction& b) const;
    [[nodiscard]] BlockId nearest_common_dominator(BlockId a, BlockId b) const;

    void recompute_dominance_frontier() const;
    [[nodiscard]] const MachineDominanceFrontier& dominance_frontier() const;

    void recompute_loop_info() const;
    [[nodiscard]] MachineLoopInfo& loop_info() noexcept;
    [[nodiscard]] const MachineLoopInfo& loop_info() const noexcept;

    [[nodiscard]] std::vector<BlockId> traversal_preorder() const;
    [[nodiscard]] std::vector<BlockId> traversal_postorder() const;
    [[nodiscard]] std::vector<BlockId> traversal_reverse_postorder() const;
    [[nodiscard]] std::vector<BlockId> traversal_breadth_first() const;

    [[nodiscard]] bool verify_cfg_integrity(std::string* out_error = nullptr) const;
    [[nodiscard]] bool verify_ssa_dominance(std::string* out_error = nullptr) const;
    [[nodiscard]] BlockBranchTopology block_branch_topology(BlockId block_id) const noexcept;

private:
    std::string name_;
    MachinePhase phase_ = MachinePhase::Selected;
    MachineFunctionProperties properties_{};

    std::vector<MachineBasicBlock> blocks_;
    std::vector<MachineInstruction> instructions_;

    MachineFrameInfo frame_{};
    MachineConstantPool constant_pool_{};
    MachineJumpTableInfo jump_table_info_{};
    MachineProfile profile_{};

    mutable bool dom_tree_valid_ = false;
    mutable void* dom_tree_cache_ptr_ = nullptr;
    mutable bool dom_frontier_valid_ = false;
    mutable void* dom_frontier_cache_ptr_ = nullptr;
    mutable bool loop_info_valid_ = false;
    mutable void* loop_info_cache_ptr_ = nullptr;

    RegId next_virtual_reg_ = 0;
    BlockId entry_block_ = INVALID_BLOCK_ID;
    std::unordered_map<std::uint32_t, BlockId> label_to_block_;
    std::vector<MachineRegister> params_;
};

/* ============================================================================
 * MachineSelection Bridge & Builder
 * ========================================================================== */

enum class MachineSelectionOperandKind : std::uint8_t {
    Invalid,
    Value,
    Immediate,
    FloatingImmediate,
    Memory,
    BasicBlock,
    Symbol,
    StackSlot,
    Register
};

struct MachineSelectionOperand {
    MachineSelectionOperandKind kind = MachineSelectionOperandKind::Invalid;
    std::uint32_t value = INVALID_MACHINE_ID;
    std::int64_t immediate = 0;
    double floating_immediate = 0.0;
    MachineMemoryOperand memory = {};
    BlockId block = INVALID_BLOCK_ID;
    SymbolId symbol = INVALID_SYMBOL;
    StackSlotId stack_slot = INVALID_STACK_SLOT;
    MachineRegister reg = {};
    RegisterClassKind reg_class = RegisterClassKind::Invalid;
    OperandRole role = OperandRole::Use;

    [[nodiscard]] static MachineSelectionOperand value_ref(
        std::uint32_t id,
        OperandRole role = OperandRole::Use) noexcept {
        MachineSelectionOperand operand;
        operand.kind = MachineSelectionOperandKind::Value;
        operand.value = id;
        operand.role = role;
        return operand;
    }

    [[nodiscard]] static MachineSelectionOperand imm(
        std::int64_t v) noexcept {
        MachineSelectionOperand operand;
        operand.kind = MachineSelectionOperandKind::Immediate;
        operand.immediate = v;
        return operand;
    }

    [[nodiscard]] static MachineSelectionOperand block_ref(
        BlockId id) noexcept {
        MachineSelectionOperand operand;
        operand.kind = MachineSelectionOperandKind::BasicBlock;
        operand.block = id;
        return operand;
    }

    [[nodiscard]] static MachineSelectionOperand memory_ref(
        const MachineMemoryOperand& mem,
        OperandRole role = OperandRole::Use) noexcept {
        MachineSelectionOperand operand;
        operand.kind = MachineSelectionOperandKind::Memory;
        operand.memory = mem;
        operand.role = role;
        return operand;
    }

    [[nodiscard]] static MachineSelectionOperand symbol_ref(
        SymbolId sym,
        OperandRole role = OperandRole::Use) noexcept {
        MachineSelectionOperand operand;
        operand.kind = MachineSelectionOperandKind::Symbol;
        operand.symbol = sym;
        operand.role = role;
        return operand;
    }
};

struct MachineSelection {
    MachineForm form = MachineForm::Unhandled;
    MachineSemanticKind semantic_kind = MachineSemanticKind::None;
    MachineOpcode opcode = GM_INVALID;
    std::uint32_t semantic_provenance = 0;
    std::uint32_t debug_id = 0;
    std::uint32_t result_value = INVALID_MACHINE_ID;
    RegisterClassKind result_class = RegisterClassKind::General;

    std::vector<MachineSelectionOperand> operands;

    MachineInstrFlag flags = MachineInstrFlag::None;

    MachineTrait traits = MachineTrait::None;
    MachineDomain domains = MachineDomain::None;
    MachineAttribute attributes = MachineAttribute::None;
    std::uint64_t intent_hash = 0ULL;
    std::uint32_t epoch = 0;
    std::uint16_t latency_cycles = 1;
    std::uint16_t throughput_reciprocal = 1;
};

class MachineFunctionBuilder {
public:
    explicit MachineFunctionBuilder(MachineFunction& function)
        : function_(function) {}

    [[nodiscard]] MachineRegister value_register(
        std::uint32_t value_id,
        RegisterClassKind reg_class = RegisterClassKind::General) {
        if (value_id == INVALID_MACHINE_ID) {
            return {};
        }
        MachineRegister& cached = value_registers_[value_id];
        if (!cached.valid()) {
            cached = function_.create_virtual_register(reg_class);
        }
        return cached;
    }

    [[nodiscard]] MachineInstruction& append(
        BlockId block,
        const MachineSelection& selection,
        RegisterClassKind result_class = RegisterClassKind::General) {
        MachineBasicBlock* bb = function_.block(block);
        if (bb == nullptr) {
            throw std::out_of_range("invalid MachineFunction block");
        }

        if (selection.form == MachineForm::Unhandled) {
            throw std::invalid_argument("cannot append unhandled MachineForm");
        }

        MachineInstruction& instruction =
            function_.create_instruction();
        instruction.form = selection.form;
        instruction.semantic_kind = selection.semantic_kind;
        instruction.opcode = selection.opcode != GM_INVALID ? selection.opcode : GM_NOOP;
        instruction.semantic_provenance = selection.semantic_provenance;
        instruction.debug_id = selection.debug_id;
        instruction.flags = selection.flags;
        instruction.traits = selection.traits;
        instruction.domains = selection.domains;
        instruction.attributes = selection.attributes;
        instruction.intent_hash = selection.intent_hash;
        instruction.epoch = selection.epoch;
        instruction.latency_cycles = selection.latency_cycles;
        instruction.throughput_reciprocal = selection.throughput_reciprocal;
        instruction.parent_block = block;

        const RegisterClassKind effective_result_class =
            selection.result_class == RegisterClassKind::Invalid
                ? result_class
                : selection.result_class;

        if (selection.result_value != INVALID_MACHINE_ID) {
            instruction.add_operand(
                MachineOperand::def(
                    value_register(selection.result_value, effective_result_class)));
        }

        if (!selection.operands.empty()) {
            for (const MachineSelectionOperand& source : selection.operands) {
                switch (source.kind) {
                case MachineSelectionOperandKind::Value:
                    if (source.value != INVALID_MACHINE_ID) {
                        const RegisterClassKind operand_class =
                            source.reg_class == RegisterClassKind::Invalid
                                ? RegisterClassKind::General
                                : source.reg_class;
                        MachineRegister value_reg =
                            value_register(source.value, operand_class);
                        if (source.role == OperandRole::Def) {
                            instruction.add_operand(MachineOperand::def(value_reg));
                        } else {
                            MachineOperand operand = MachineOperand::use(value_reg);
                            operand.role = source.role;
                            instruction.add_operand(std::move(operand));
                        }
                    }
                    break;
                case MachineSelectionOperandKind::Immediate:
                    instruction.add_operand(MachineOperand::imm(source.immediate));
                    break;
                case MachineSelectionOperandKind::FloatingImmediate: {
                    MachineOperand operand;
                    operand.kind = MachineOperandKind::FloatingImmediate;
                    operand.role = source.role;
                    operand.floating_immediate = source.floating_immediate;
                    instruction.add_operand(std::move(operand));
                    break;
                }
                case MachineSelectionOperandKind::Memory: {
                    MachineOperand operand;
                    operand.kind = MachineOperandKind::Memory;
                    operand.role = source.role;
                    operand.memory = source.memory;
                    if (operand.memory.base.valid() && operand.memory.base.is_virtual()) {
                        operand.memory.base = value_register(operand.memory.base.id, RegisterClassKind::General);
                    }
                    if (operand.memory.index.valid() && operand.memory.index.is_virtual()) {
                        operand.memory.index = value_register(operand.memory.index.id, RegisterClassKind::General);
                    }
                    instruction.add_operand(std::move(operand));
                    break;
                }
                case MachineSelectionOperandKind::BasicBlock:
                    instruction.add_operand(MachineOperand::block_ref(source.block));
                    break;
                case MachineSelectionOperandKind::Symbol: {
                    MachineOperand operand;
                    operand.kind = MachineOperandKind::Symbol;
                    operand.role = source.role;
                    operand.symbol = source.symbol;
                    instruction.add_operand(std::move(operand));
                    break;
                }
                case MachineSelectionOperandKind::StackSlot:
                    instruction.add_operand(MachineOperand::stack_ref(source.stack_slot));
                    break;
                case MachineSelectionOperandKind::Register: {
                    MachineOperand operand;
                    operand.kind = MachineOperandKind::Register;
                    operand.role = source.role;
                    operand.reg = source.reg;
                    instruction.add_operand(std::move(operand));
                    break;
                }
                case MachineSelectionOperandKind::Invalid:
                    throw std::invalid_argument("invalid MachineSelection operand");
                }
            }
        }

        if (!function_.insert_instruction_before(block, INVALID_INSTR_ID, instruction.id)) {
            throw std::logic_error("failed to append MachineFunction instruction to its block");
        }

        if (instruction.has_flag(MachineInstrFlag::Call) || instruction.form == MachineForm::Call) {
            function_.properties().contains_calls = true;
            function_.frame_info().has_calls = true;
        }
        return instruction;
    }

    [[nodiscard]] MachineBasicBlock& create_block(bool entry = false) {
        return function_.create_block(entry);
    }

private:
    MachineFunction& function_;
    std::unordered_map<std::uint32_t, MachineRegister> value_registers_;
};

enum class EmissionKind : std::uint8_t {
    Assembly,
    Object,
    MachineCode,
    Intermediate
};

struct EmissionResult {
    bool success = false;
    std::vector<std::uint8_t> bytes;
    std::string_view diagnostic = {};
};

class MachineEmitter {
public:
    virtual ~MachineEmitter() = default;

    [[nodiscard]] virtual EmissionResult
    emit(const MachineFunction& function) = 0;

    [[nodiscard]] virtual EmissionKind kind() const noexcept = 0;
};

struct MIR {
    MachineFunction function;

    explicit MIR(std::string_view name = {})
        : function(name) {}

    [[nodiscard]] MachineFunction& machine_function() noexcept {
        return function;
    }

    [[nodiscard]] const MachineFunction&
    machine_function() const noexcept {
        return function;
    }
};

} // namespace dgtl::machine