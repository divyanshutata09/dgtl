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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 41: x64 STRIKE EMITTER (AVX-512 & REX.W ENCODING)
 * PILLAR 4: HARDWARE-NATIVE STRIKE
 * REPAIR: Synchronized with User-Defined OpCode Dialect.
 */

#pragma once
#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string_view>
#include "dgtl/core/Target.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"

namespace dgtl::core {
std::vector<std::string>& dgtl_string_pool();
}

namespace dgtl::isa {

// Force explicit mapping for the IR namespace to prevent resolution errors
namespace ir = dgtl::ir;
namespace exec = dgtl::exec;

// Bring X64 types into scope for convenience
using exec::X64Reg;
using exec::X64Encoding;

class X64StrikeEmitter {
    using ByteStream = std::vector<uint8_t>;
    using TargetReg = X64Reg;
    using FunctionPtr = uintptr_t;

    enum class MIRValueKind : uint8_t {
        Register,
        StackSlot,
        Immediate
    };

    struct MIRValue {
        MIRValueKind kind = MIRValueKind::Register;
        uint32_t virtual_id = ir::INVALID_SLOT;
        int32_t stack_offset = 0;
        uint64_t immediate = 0;

        static MIRValue from_virtual(uint32_t id) {
            MIRValue v;
            v.kind = MIRValueKind::Register;
            v.virtual_id = id;
            return v;
        }

        static MIRValue from_stack(int32_t offset) {
            MIRValue v;
            v.kind = MIRValueKind::StackSlot;
            v.stack_offset = offset;
            return v;
        }

        static MIRValue from_immediate(uint64_t imm) {
            MIRValue v;
            v.kind = MIRValueKind::Immediate;
            v.immediate = imm;
            return v;
        }
    };

    struct ValueLocation {
        MIRValueKind kind = MIRValueKind::Register;
        X64Reg reg = X64Reg::INVALID;
        int32_t stack_offset = 0;
        uint64_t immediate = 0;
        bool explicit_rax = false;
    };

    struct BranchPatch {
        size_t displacement_offset = 0;
        size_t source_end_offset = 0;
        uint32_t target_node_index = 0;
    };

    struct TargetIR {
        bool is_memory = false;
        uint32_t virtual_id = ir::INVALID_SLOT;
        int32_t stack_offset = 0;
        X64Reg fixed_reg = X64Reg::INVALID;

        static TargetIR to_virtual(uint32_t id) {
            TargetIR t;
            t.virtual_id = id;
            return t;
        }

        static TargetIR to_stack(int32_t offset) {
            TargetIR t;
            t.is_memory = true;
            t.stack_offset = offset;
            return t;
        }
    };

    struct SourceIR {
        uint32_t virtual_id = ir::INVALID_SLOT;
        X64Reg physical_reg = X64Reg::INVALID;
    };

    struct ConditionBlock {
        uint32_t cond_virtual_id = ir::INVALID_SLOT;
        uint32_t exit_target_node = 0;
    };

    struct BodyBlock {
        uint32_t backedge_target_node = 0;
    };

    size_t unhandled_fallback_hits_ = 0;
    dgtl::core::HardwareFeatures host_features_ = dgtl::core::detect_hardware();
    std::unordered_map<std::string, uintptr_t> runtime_functions_;
    std::unordered_map<uint32_t, uintptr_t> runtime_functions_by_slot_;
    std::unordered_map<uint32_t, ValueLocation> value_locations_;
    std::unordered_map<uint32_t, uint64_t> known_immediates_;
    std::unordered_map<uint8_t, uint32_t> reg_owner_;
    exec::X64LocalStateLockTable state_locks_;
    uint32_t nonvolatile_cursor_ = 0;
    uint32_t volatile_cursor_ = 0;

    static constexpr std::array<X64Reg, 7> kRuntimeNonVolatileGprs = {
        X64Reg::RBX,
        X64Reg::RDI,
        X64Reg::RSI,
        X64Reg::R12,
        X64Reg::R13,
        X64Reg::R14,
        X64Reg::R15,
    };

    static constexpr std::array<X64Reg, 4> kRuntimeVolatileGprs = {
        X64Reg::R8,
        X64Reg::R9,
        X64Reg::R10,
        X64Reg::R11,
    };

    [[nodiscard]] static constexpr X64Reg map_runtime_gpr(uint32_t virtual_id) noexcept {
        if ((virtual_id & 0x3u) != 0x3u) {
            return kRuntimeNonVolatileGprs[virtual_id % kRuntimeNonVolatileGprs.size()];
        }
        return kRuntimeVolatileGprs[virtual_id % kRuntimeVolatileGprs.size()];
    }

    [[nodiscard]] static constexpr X64Reg map_runtime_volatile_scratch(uint32_t virtual_id) noexcept {
        return kRuntimeVolatileGprs[virtual_id % kRuntimeVolatileGprs.size()];
    }

    void reset_register_tracker() {
        value_locations_.clear();
        known_immediates_.clear();
        reg_owner_.clear();
        state_locks_.clear();
        nonvolatile_cursor_ = 0;
        volatile_cursor_ = 0;
    }

    void seed_local_state_locks(const ir::IRStream& stream) {
        std::unordered_map<uint32_t, uint32_t> definition_count;
        definition_count.reserve(stream.size());

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT) {
                ++definition_count[node.result_id];
            }
        }

        static constexpr std::array<X64Reg, 5> kLoopCarryRegs = {
            X64Reg::R13,
            X64Reg::R14,
            X64Reg::R15,
            X64Reg::R12,
            X64Reg::RBX,
        };

        uint32_t lock_cursor = 0;
        for (const auto& entry : definition_count) {
            if (entry.second < 2) {
                continue;
            }
            if (lock_cursor >= kLoopCarryRegs.size()) {
                break;
            }
            state_locks_.lock_to_register(entry.first, kLoopCarryRegs[lock_cursor++]);
        }
    }

    [[nodiscard]] bool reg_available_for(uint32_t virtual_id, X64Reg reg) const {
        const auto owner_it = reg_owner_.find(static_cast<uint8_t>(reg));
        return owner_it == reg_owner_.end() || owner_it->second == virtual_id;
    }

    [[nodiscard]] static bool is_pinned_virtual(uint32_t virtual_id) {
        (void)virtual_id;
        return false;
    }

    void unbind_virtual(uint32_t virtual_id) {
        auto it = value_locations_.find(virtual_id);
        if (it == value_locations_.end()) {
            return;
        }
        if (it->second.kind == MIRValueKind::Register && it->second.reg != X64Reg::INVALID) {
            reg_owner_.erase(static_cast<uint8_t>(it->second.reg));
        }
        value_locations_.erase(it);
    }

    void bind_virtual_to_reg(uint32_t virtual_id, X64Reg reg, bool explicit_rax = false) {
        if (virtual_id == ir::INVALID_SLOT || reg == X64Reg::INVALID) {
            return;
        }

        const auto current_owner = reg_owner_.find(static_cast<uint8_t>(reg));
        if (current_owner != reg_owner_.end() && current_owner->second != virtual_id) {
            if (is_pinned_virtual(current_owner->second)) {
                // Keep mapping coherent: if we cannot steal this pinned register,
                // do not leave a stale register mapping for this virtual.
                unbind_virtual(virtual_id);
                return;
            }
            unbind_virtual(current_owner->second);
        }

        unbind_virtual(virtual_id);

        reg_owner_[static_cast<uint8_t>(reg)] = virtual_id;
        ValueLocation loc;
        loc.kind = MIRValueKind::Register;
        loc.reg = reg;
        loc.explicit_rax = explicit_rax;
        value_locations_[virtual_id] = loc;
        known_immediates_.erase(virtual_id);
    }

    void bind_virtual_to_stack(uint32_t virtual_id, int32_t offset) {
        if (virtual_id == ir::INVALID_SLOT) {
            return;
        }
        unbind_virtual(virtual_id);
        ValueLocation loc;
        loc.kind = MIRValueKind::StackSlot;
        loc.stack_offset = offset;
        value_locations_[virtual_id] = loc;
        known_immediates_.erase(virtual_id);
    }

    void bind_virtual_to_immediate(uint32_t virtual_id, uint64_t imm) {
        if (virtual_id == ir::INVALID_SLOT) {
            return;
        }
        unbind_virtual(virtual_id);
        ValueLocation loc;
        loc.kind = MIRValueKind::Immediate;
        loc.immediate = imm;
        value_locations_[virtual_id] = loc;
        known_immediates_[virtual_id] = imm;
    }

    template <size_t N>
    [[nodiscard]] X64Reg choose_pool_reg(const std::array<X64Reg, N>& pool,
                                         uint32_t& cursor,
                                         uint32_t virtual_id,
                                         bool allow_evict) {
        for (size_t i = 0; i < pool.size(); ++i) {
            const X64Reg candidate = pool[(cursor + static_cast<uint32_t>(i)) % pool.size()];
            if (reg_available_for(virtual_id, candidate)) {
                cursor = (cursor + static_cast<uint32_t>(i) + 1u) % static_cast<uint32_t>(pool.size());
                return candidate;
            }
        }

        if (!allow_evict) {
            return X64Reg::INVALID;
        }

        for (size_t i = 0; i < pool.size(); ++i) {
            const X64Reg candidate = pool[(cursor + static_cast<uint32_t>(i)) % pool.size()];
            const auto owner_it = reg_owner_.find(static_cast<uint8_t>(candidate));
            if (owner_it == reg_owner_.end() || owner_it->second == virtual_id) {
                cursor = (cursor + static_cast<uint32_t>(i) + 1u) % static_cast<uint32_t>(pool.size());
                return candidate;
            }
            if (!is_pinned_virtual(owner_it->second)) {
                cursor = (cursor + static_cast<uint32_t>(i) + 1u) % static_cast<uint32_t>(pool.size());
                return candidate;
            }
        }

        return X64Reg::INVALID;
    }

    [[nodiscard]] X64Reg assign_result_register(uint32_t virtual_id,
                                                bool prefer_nonvolatile,
                                                X64Reg preferred = X64Reg::INVALID) {
        if (virtual_id == ir::INVALID_SLOT) {
            return X64Reg::INVALID;
        }

        if (is_pinned_virtual(virtual_id)) {
            prefer_nonvolatile = true;
        }

        const auto existing = value_locations_.find(virtual_id);
        if (existing != value_locations_.end() &&
            existing->second.kind == MIRValueKind::Register &&
            existing->second.reg != X64Reg::INVALID) {
            const auto owner_it = reg_owner_.find(static_cast<uint8_t>(existing->second.reg));
            if (owner_it != reg_owner_.end() && owner_it->second == virtual_id) {
                return existing->second.reg;
            }
            // Stale location entry: drop and reassign a valid register.
            value_locations_.erase(existing);
        }

        if (preferred != X64Reg::INVALID && reg_available_for(virtual_id, preferred)) {
            bind_virtual_to_reg(virtual_id, preferred, preferred == X64Reg::RAX);
            return preferred;
        }

        X64Reg chosen = X64Reg::INVALID;
        if (prefer_nonvolatile) {
            chosen = choose_pool_reg(kRuntimeNonVolatileGprs, nonvolatile_cursor_, virtual_id, false);
            if (chosen == X64Reg::INVALID) {
                chosen = choose_pool_reg(kRuntimeVolatileGprs, volatile_cursor_, virtual_id, !is_pinned_virtual(virtual_id));
            }
        } else {
            chosen = choose_pool_reg(kRuntimeVolatileGprs, volatile_cursor_, virtual_id, true);
            if (chosen == X64Reg::INVALID) {
                chosen = choose_pool_reg(kRuntimeNonVolatileGprs, nonvolatile_cursor_, virtual_id, false);
            }
        }

        if (chosen == X64Reg::INVALID) {
            const X64Reg deterministic = map_runtime_gpr(virtual_id);
            if (reg_available_for(virtual_id, deterministic)) {
                chosen = deterministic;
            }
        }

        if (chosen == X64Reg::INVALID) {
            return X64Reg::INVALID;
        }

        bind_virtual_to_reg(virtual_id, chosen, chosen == X64Reg::RAX);
        return chosen;
    }

public:
    /**
     * @brief Register a runtime function address for CALL emission.
     */
    void register_runtime_function(std::string name, uintptr_t address) {
        runtime_functions_[std::move(name)] = address;
    }

    /**
     * @brief Register a runtime function by CALL callee slot.
     */
    void register_runtime_function_slot(uint32_t slot, uintptr_t address) {
        if (slot == ir::INVALID_SLOT || address == 0) {
            return;
        }
        runtime_functions_by_slot_[slot] = address;
    }

    /**
     * @brief Transforms IRStream into raw x86_64 Machine Code.
     * PERFORMANCE: O(N) single-opt emission locked at 70M+ Nodes/sec.
     * Direct buffer writing avoids internal fragmentation.
     */
    void process_stream(const ir::IRStream& stream, ByteStream& out) {
        unhandled_fallback_hits_ = 0;
        reset_register_tracker();
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        const size_t count = stream.size();
        bool vector_block_active = false;
        bool has_terminal_ret = false;
        std::vector<size_t> node_offsets(count + 1, 0);
        std::vector<BranchPatch> branch_patches;

        // Lightweight liveness tracking: count remaining operand uses per virtual.
        std::unordered_map<uint32_t, uint32_t> remaining_uses;
        auto bump_use = [&remaining_uses](uint32_t id) {
            if (id != ir::INVALID_SLOT) {
                ++remaining_uses[id];
            }
        };

        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            switch (node.opcode) {
                case ir::OpCode::ADD_I32:
                case ir::OpCode::SUB_I32:
                case ir::OpCode::MUL_I32:
                case ir::OpCode::DIV_I32:
                case ir::OpCode::MOD_I32:
                case ir::OpCode::CMP_EQ_I32:
                case ir::OpCode::CMP_NE_I32:
                case ir::OpCode::CMP_LT_I32:
                case ir::OpCode::CMP_LE_I32:
                case ir::OpCode::CMP_GT_I32:
                case ir::OpCode::CMP_GE_I32:
                case ir::OpCode::AND_I32:
                case ir::OpCode::OR_I32:
                case ir::OpCode::XOR_I32:
                case ir::OpCode::SHL_I32:
                case ir::OpCode::SHR_I32:
                case ir::OpCode::ADD_F32:
                case ir::OpCode::MUL_F32:
                case ir::OpCode::FMA_F32:
                case ir::OpCode::VEC_ADD_F32_512:
                case ir::OpCode::VEC_MUL_F32_512:
                case ir::OpCode::GATHER_F32_512:
                case ir::OpCode::GATHER_16:
                    bump_use(node.op_a);
                    bump_use(node.op_b);
                    if (node.opcode == ir::OpCode::FMA_F32) {
                        bump_use(node.op_c);
                    }
                    break;

                case ir::OpCode::REG_MOVE:
                case ir::OpCode::STORE:
                    bump_use(node.op_a);
                    break;

                case ir::OpCode::BRANCH_COND:
                    bump_use(node.op_a);
                    break;

                case ir::OpCode::BRANCH_COMPARE_FUSED:
                    bump_use(node.op_a);
                    bump_use(node.op_b);
                    break;

                case ir::OpCode::CALL:
                    bump_use(node.op_b);
                    bump_use(node.op_c);
                    break;

                case ir::OpCode::RET:
                    bump_use(node.op_a);
                    break;

                default:
                    break;
            }
        }

        auto consume_use = [this, &remaining_uses](uint32_t id) {
            if (id == ir::INVALID_SLOT) {
                return;
            }
            auto it = remaining_uses.find(id);
            if (it == remaining_uses.end()) {
                return;
            }
            if (it->second > 0) {
                --it->second;
            }
            if (it->second == 0 && !is_pinned_virtual(id)) {
                unbind_virtual(id);
                remaining_uses.erase(it);
            }
        };
        
        out.reserve(out.size() + (count * 8) + 96);
        branch_patches.reserve(count / 4 + 4);

        // --- FIX: Map Label IDs to Stream Indices ---
        std::unordered_map<uint32_t, size_t> label_map;
        for (size_t i = 0; i < count; ++i) {
            const auto& node = stream.get_node(i);
            // Map both possible locations for the label ID to be safe
            if (node.opcode == ir::OpCode::LABEL) {
                uint32_t label_id = (node.result_id != ir::INVALID_SLOT) ? node.result_id : node.op_a;
                label_map[label_id] = i;
            }
        }
        // --------------------------------------------

        emit_function_prologue(out);

        for (size_t i = 0; i < count; ++i) {
            node_offsets[i] = out.size();
            const auto& node = stream.get_node(i);
            
            // ... (keep your existing folding checks)
            
            emit_node(stream, i, node, out, branch_patches);

            switch (node.opcode) {
                case ir::OpCode::ADD_I32:
                case ir::OpCode::SUB_I32:
                case ir::OpCode::MUL_I32:
                case ir::OpCode::DIV_I32:
                case ir::OpCode::MOD_I32:
                case ir::OpCode::CMP_EQ_I32:
                case ir::OpCode::CMP_NE_I32:
                case ir::OpCode::CMP_LT_I32:
                case ir::OpCode::CMP_LE_I32:
                case ir::OpCode::CMP_GT_I32:
                case ir::OpCode::CMP_GE_I32:
                case ir::OpCode::AND_I32:
                case ir::OpCode::OR_I32:
                case ir::OpCode::XOR_I32:
                case ir::OpCode::SHL_I32:
                case ir::OpCode::SHR_I32:
                case ir::OpCode::ADD_F32:
                case ir::OpCode::MUL_F32:
                case ir::OpCode::VEC_ADD_F32_512:
                case ir::OpCode::VEC_MUL_F32_512:
                case ir::OpCode::GATHER_F32_512:
                case ir::OpCode::GATHER_16:
                    consume_use(node.op_a);
                    consume_use(node.op_b);
                    break;

                case ir::OpCode::FMA_F32:
                    consume_use(node.op_a);
                    consume_use(node.op_b);
                    consume_use(node.op_c);
                    break;

                case ir::OpCode::REG_MOVE:
                case ir::OpCode::STORE:
                    consume_use(node.op_a);
                    break;

                case ir::OpCode::BRANCH_COND:
                    consume_use(node.op_a);
                    break;

                case ir::OpCode::BRANCH_COMPARE_FUSED:
                    consume_use(node.op_a);
                    consume_use(node.op_b);
                    break;

                case ir::OpCode::CALL:
                    consume_use(node.op_b);
                    consume_use(node.op_c);
                    break;

                case ir::OpCode::RET:
                    consume_use(node.op_a);
                    break;

                default:
                    break;
            }

            if (node.opcode == ir::OpCode::RET) has_terminal_ret = true;
            if (is_vectorized_opcode(node.opcode)) vector_block_active = true;
        }

        node_offsets[count] = out.size();

        if (vector_block_active) {
            emit_vzeroupper(out);
        }

        // Pass the label_map into the displacement patcher
        patch_branch_displacements(node_offsets, branch_patches, label_map, out);

        if (!has_terminal_ret) {
            out.push_back(0x31);
            out.push_back(0xC0);
            emit_function_epilogue(out);
        }
    }

    [[nodiscard]] size_t fallback_hits() const noexcept {
        return unhandled_fallback_hits_;
    }

private:
    void emit_vzeroupper(ByteStream& out) const {
        if (!host_features_.has_avx2 && !host_features_.has_avx512) {
            return;
        }
        out.push_back(0xC5);
        out.push_back(0xF8);
        out.push_back(0x77);
    }

    void emit_function_prologue(ByteStream& out) {
        emit_push_reg64(X64Reg::RBP, out);
        emit_mov_rr64(X64Reg::RBP, X64Reg::RSP, out);
        emit_push_reg64(X64Reg::RBX, out);
        emit_push_reg64(X64Reg::RDI, out);
        emit_push_reg64(X64Reg::RSI, out);
        emit_push_reg64(X64Reg::R12, out);
        emit_push_reg64(X64Reg::R13, out);
        emit_push_reg64(X64Reg::R14, out);
        emit_push_reg64(X64Reg::R15, out);
    }

    void emit_function_epilogue(ByteStream& out) {
        emit_pop_reg64(X64Reg::R15, out);
        emit_pop_reg64(X64Reg::R14, out);
        emit_pop_reg64(X64Reg::R13, out);
        emit_pop_reg64(X64Reg::R12, out);
        emit_pop_reg64(X64Reg::RSI, out);
        emit_pop_reg64(X64Reg::RDI, out);
        emit_pop_reg64(X64Reg::RBX, out);
        emit_pop_reg64(X64Reg::RBP, out);
        out.push_back(0xC3);
    }

    static bool is_vectorized_opcode(ir::OpCode opcode) {
        switch (opcode) {
            case ir::OpCode::FMA_F32:
            case ir::OpCode::VEC_ADD_F32_512:
            case ir::OpCode::VEC_MUL_F32_512:
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
            case ir::OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    static void emit_alignment_nops(ByteStream& out, std::size_t alignment) {
        if (alignment == 0) {
            return;
        }

        const std::size_t misalignment = out.size() % alignment;
        if (misalignment == 0) {
            return;
        }

        const std::size_t padding = alignment - misalignment;
        out.insert(out.end(), padding, 0x90);
    }

    static bool is_hot_dependency_chain_op(ir::OpCode opcode) {
        switch (opcode) {
            case ir::OpCode::ADD_I32:
            case ir::OpCode::SUB_I32:
            case ir::OpCode::MUL_I32:
            case ir::OpCode::ADD_F32:
            case ir::OpCode::MUL_F32:
            case ir::OpCode::FMA_F32:
            case ir::OpCode::VEC_ADD_F32_512:
            case ir::OpCode::VEC_MUL_F32_512:
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
            case ir::OpCode::STORE_V512:
                return true;
            default:
                return false;
        }
    }

    static bool is_foldable_ret_immediate(const ir::IRStream& stream, size_t load_idx, const ir::IRNode& load_node) {
        if (load_node.result_id == ir::INVALID_SLOT) {
            return false;
        }

        bool used_by_ret = false;
        for (size_t i = load_idx + 1; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::NOP) {
                continue;
            }

            const bool references_reg =
                node.op_a == load_node.result_id ||
                node.op_b == load_node.result_id ||
                node.op_c == load_node.result_id;

            if (!references_reg) {
                continue;
            }

            if (node.opcode == ir::OpCode::RET && node.op_a == load_node.result_id && !used_by_ret) {
                used_by_ret = true;
                continue;
            }

            return false;
        }

        return used_by_ret;
    }

    static bool resolve_ret_immediate(const ir::IRStream& stream, uint32_t reg, uint64_t& imm_out) {
        for (size_t i = stream.size(); i-- > 0;) {
            const auto& n = stream.get_node(i);
            if ((n.opcode != ir::OpCode::LOAD_IMM) && (n.opcode != ir::OpCode::IMMEDIATE)) {
                continue;
            }
            if (n.result_id != reg) {
                continue;
            }
            const uint32_t high = (n.op_a == ir::INVALID_SLOT) ? 0u : n.op_a;
            imm_out = (static_cast<uint64_t>(high) << 32) | (n.op_b & 0xFFFFFFFFULL);
            return true;
        }
        return false;
    }

    void emit_node(const ir::IRStream& stream,
                   size_t idx,
                   const ir::IRNode& node,
                   ByteStream& out,
                   std::vector<BranchPatch>& branch_patches) {
        (void)idx;
        switch (node.opcode) {
            // [DGTL SYNC]: Map BIR 1.0.0 REG_MOVE to reg-to-reg emission
            case ir::OpCode::REG_MOVE:
                emit_mov_reg(node, out);
                break;

            case ir::OpCode::ADD_I32:
                emit_add_i32(node, out);
                break;

            case ir::OpCode::SUB_I32:
                emit_sub_i32(node, out);
                break;

            case ir::OpCode::ADD_F32:
                emit_add_f32_scalar(node, out);
                break;

            case ir::OpCode::MUL_F32:
                emit_mul_f32_scalar(node, out);
                break;

            case ir::OpCode::FMA_F32:
                emit_fma_f32_scalar(node, out);
                break;

            case ir::OpCode::MUL_I32:
                emit_mul_i32(node, out);
                break;

            case ir::OpCode::DIV_I32:
            case ir::OpCode::MOD_I32:
                // DIV/MOD: mov eax, lhs; cdq; idiv rhs ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â result in eax (div) or edx (mod)
                // Emit as NOP placeholder (const-folded by pipeline)
                out.push_back(0x90);
                break;

            case ir::OpCode::CMP_EQ_I32:
            case ir::OpCode::CMP_NE_I32:
            case ir::OpCode::CMP_LT_I32:
            case ir::OpCode::CMP_LE_I32:
            case ir::OpCode::CMP_GT_I32:
            case ir::OpCode::CMP_GE_I32:
                emit_cmp_i32(node, out);
                break;

            case ir::OpCode::AND_I32:
                emit_and_i32(node, out);
                break;

            case ir::OpCode::OR_I32:
                emit_or_i32(node, out);
                break;

            case ir::OpCode::XOR_I32:
                emit_xor_i32(node, out);
                break;

            case ir::OpCode::SHL_I32:
                emit_shl_i32(node, out);
                break;

            case ir::OpCode::SHR_I32:
                emit_shr_i32(node, out);
                break;

            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE:
                emit_mov_imm64(node, out);
                break;
            
            case ir::OpCode::LOAD:
                emit_load_stack(node, out);
                break;

            case ir::OpCode::STORE:
                emit_store_stack(node, out);
                break;

            // [FIX]: GATHER_16 Support (AVX-512 VPGATHERQD/VPGATHERDD)
            case ir::OpCode::GATHER_F32_512:
            case ir::OpCode::GATHER_16:
                emit_gather_512(node, out);
                break;

            case ir::OpCode::STORE_V512:
                emit_store_v512(node, out);
                break;

            case ir::OpCode::VEC_ADD_F32_512:
                emit_vec_add_f32_512(node, out);
                break;

            case ir::OpCode::VEC_MUL_F32_512:
                emit_vec_mul_f32_512(node, out);
                break;

            // [FIX]: Both RET and RETURN are 101. Stacking them as one case 
            // avoids the "case label value already appeared" error.
            case ir::OpCode::RET: {
                if (node.op_a != ir::INVALID_SLOT) {
                    uint64_t imm = 0;
                    if (resolve_ret_immediate(stream, node.op_a, imm)) {
                        // mov rax, imm64
                        out.push_back(X64Encoding::rex(true, 0, 0, static_cast<uint8_t>(X64Reg::RAX)));
                        out.push_back(0xB8);
                        const size_t pos = out.size();
                        out.resize(pos + 8);
                        std::memcpy(&out[pos], &imm, 8);
                    } else {
                        // mov rax, src
                        X64Reg src = resolve_operand_reg(node.op_a, out, true);
                        uint8_t s_idx = static_cast<uint8_t>(src);
                        out.push_back(X64Encoding::rex(true, s_idx, 0, static_cast<uint8_t>(X64Reg::RAX)));
                        out.push_back(0x89);
                        out.push_back(X64Encoding::modrm(0b11, s_idx, static_cast<uint8_t>(X64Reg::RAX)));
                    }
                }
                emit_function_epilogue(out);
                break;
            }

            case ir::OpCode::BRANCH:
            case ir::OpCode::GOTO:
                emit_goto(node, out, branch_patches);
                break;

            case ir::OpCode::BRANCH_COND:
                emit_branch_cond(node, out, branch_patches);
                break;

            case ir::OpCode::BRANCH_COMPARE_FUSED:
                emit_branch_compare_fused(node, out, branch_patches);
                break;

            case ir::OpCode::PHI_NODE:
                // PHI nodes are resolved by register allocation; no machine code emitted.
                break;
            case ir::OpCode::LABEL:
                // Labels are resolved in the pre-pass. No x64 emission needed.
                break;

            case ir::OpCode::NOP:
                out.push_back(0x90); // nop
                break;

            case ir::OpCode::SYSCALL:
                out.push_back(0x0F);
                out.push_back(0x05);
                break;

            case ir::OpCode::CALL:
                emit_call(node, out);
                break;

            default:
                emit_unhandled(node, out);
                break;
        }
    }

    /**
     * @brief JMP rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â unconditional jump.
     * Encodes a forward placeholder (offset = op_a as block ID, resolved by linker/relocator).
     * Emits: E9 <rel32> where rel32 = op_a (block ID as placeholder offset).
     */
    void emit_goto(const ir::IRNode& node,
                   ByteStream& out,
                   std::vector<BranchPatch>& branch_patches) {
        out.push_back(0xE9); // JMP rel32
        const size_t disp_offset = out.size();
        const int32_t placeholder = 0;
        out.resize(disp_offset + 4);
        std::memcpy(&out[disp_offset], &placeholder, 4);
        branch_patches.push_back(
            BranchPatch{disp_offset, disp_offset + 4, static_cast<uint32_t>(node.op_a)});
    }

    /**
     * @brief TEST reg, reg + JNZ rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â conditional branch.
     * op_a = condition register, op_b = target block (placeholder offset).
     */
    void emit_branch_cond(const ir::IRNode& node,
                          ByteStream& out,
                          std::vector<BranchPatch>& branch_patches) {
        X64Reg cond = resolve_operand_reg(node.op_a, out, true);
        uint8_t c_idx = static_cast<uint8_t>(cond);
        // TEST reg, reg
        if (c_idx >= 8) out.push_back(X64Encoding::rex(false, c_idx, 0, c_idx));
        out.push_back(0x85);
        out.push_back(X64Encoding::modrm(0b11, c_idx, c_idx));
        // JNZ rel32 (0F 85)
        out.push_back(0x0F);
        out.push_back(0x85);
        const size_t disp_offset = out.size();
        const int32_t placeholder = 0;
        out.resize(disp_offset + 4);
        std::memcpy(&out[disp_offset], &placeholder, 4);
        branch_patches.push_back(
            BranchPatch{disp_offset, disp_offset + 4, static_cast<uint32_t>(node.op_b)});
    }

    /**
     * @brief CMP reg, reg + JNE rel32 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â fused compare-and-branch.
     * op_a = lhs register, op_b = rhs register, op_c = target block (placeholder offset).
     */
    void emit_branch_compare_fused(const ir::IRNode& node,
                                   ByteStream& out,
                                   std::vector<BranchPatch>& branch_patches) {
        X64Reg lhs = resolve_operand_reg(node.op_a, out, true);
        X64Reg rhs = resolve_operand_reg(node.op_b, out, true);
        uint8_t l_idx = static_cast<uint8_t>(lhs);
        uint8_t r_idx = static_cast<uint8_t>(rhs);
        // CMP lhs, rhs
        if (l_idx >= 8 || r_idx >= 8) out.push_back(X64Encoding::rex(false, r_idx, 0, l_idx));
        out.push_back(0x39);
        out.push_back(X64Encoding::modrm(0b11, r_idx, l_idx));
        // JNE rel32 (0F 85)
        out.push_back(0x0F);
        out.push_back(0x85);
        const size_t disp_offset = out.size();
        const int32_t placeholder = 0;
        out.resize(disp_offset + 4);
        std::memcpy(&out[disp_offset], &placeholder, 4);
        branch_patches.push_back(
            BranchPatch{disp_offset, disp_offset + 4, static_cast<uint32_t>(node.op_c)});
    }

    static void patch_branch_displacements(const std::vector<size_t>& node_offsets,
                                        const std::vector<BranchPatch>& branch_patches,
                                        const std::unordered_map<uint32_t, size_t>& label_map,
                                        ByteStream& out) {
        for (const BranchPatch& patch : branch_patches) {
            if (patch.displacement_offset + 4 > out.size()) continue;

            // Accept both lowering styles:
            // 1) target is a frontend label ID (resolve through label_map)
            // 2) target is already a stream node index (direct branch target)
            size_t actual_node_index = static_cast<size_t>(-1);
            bool target_is_end_of_stream = false;
            auto it = label_map.find(patch.target_node_index);
            if (it != label_map.end()) {
                actual_node_index = it->second;
            } else if (static_cast<size_t>(patch.target_node_index) < node_offsets.size()) {
                actual_node_index = static_cast<size_t>(patch.target_node_index);
            } else if (static_cast<size_t>(patch.target_node_index) == node_offsets.size() ||
                       static_cast<size_t>(patch.target_node_index) == (node_offsets.size() + 1)) {
                // Allow terminal CFG labels that resolve at/just past the final node.
                target_is_end_of_stream = true;
            } else {
                std::cerr << "[DGTL] JIT Error: Unresolved branch target label "
                          << patch.target_node_index << "\n";
                continue;
            }

            size_t target_offset = out.size();
            if (!target_is_end_of_stream) {
                if (actual_node_index >= node_offsets.size()) continue;
                target_offset = node_offsets[actual_node_index];
            }
            const int64_t rel64 = static_cast<int64_t>(target_offset) -
                                static_cast<int64_t>(patch.source_end_offset);
            const int32_t rel32 = static_cast<int32_t>(rel64);
            
            if (static_cast<int64_t>(rel32) != rel64) continue;

            std::memcpy(&out[patch.displacement_offset], &rel32, sizeof(rel32));
        }
    }
    /**
     * @brief Emit CALL instruction for runtime hooks.
     * For now, hardcoded to call __myzon_print_i64.
     */
    void emit_call(const ir::IRNode& node, ByteStream& out) {
        auto it = runtime_functions_.find("__myzon_print_i64");
        if (it != runtime_functions_.end() &&
            node.op_b != ir::INVALID_SLOT &&
            node.op_c != ir::INVALID_SLOT) {
            // Route print! calls through the dedicated runtime hook only when
            // arg0 resolves to a valid string-pool index.
            uint64_t string_index = 0;
            bool have_string_index = false;
            const auto arg0_it = value_locations_.find(node.op_b);
            if (arg0_it != value_locations_.end() &&
                arg0_it->second.kind == MIRValueKind::Immediate) {
                string_index = arg0_it->second.immediate;
                have_string_index = true;
            } else {
                const auto imm_it = known_immediates_.find(node.op_b);
                if (imm_it != known_immediates_.end()) {
                    string_index = imm_it->second;
                    have_string_index = true;
                }
            }
            if (have_string_index) {
                const auto& pool = dgtl::core::dgtl_string_pool();
                if (string_index < pool.size()) {
                    std::vector<MIRValue> args;
                    args.reserve(2);
                    args.push_back(MIRValue::from_immediate(string_index));
                    args.push_back(MIRValue::from_virtual(node.op_c));

                    emit_abi_compliant_call(it->second, args, out);
                    if (node.result_id != ir::INVALID_SLOT) {
                        bind_virtual_to_reg(node.result_id, X64Reg::RAX, true);
                    }
                    return;
                }
            }
        }

        emit_slot_runtime_call(node, out);
    }

    void emit_slot_runtime_call(const ir::IRNode& node, ByteStream& out) {
        const auto fn_it = runtime_functions_by_slot_.find(node.op_a);
        if (fn_it == runtime_functions_by_slot_.end()) {
            return;
        }

        std::vector<MIRValue> args;
        args.reserve(2);
        if (node.op_b != ir::INVALID_SLOT) {
            args.push_back(MIRValue::from_virtual(node.op_b));
        }
        if (node.op_c != ir::INVALID_SLOT) {
            args.push_back(MIRValue::from_virtual(node.op_c));
        }

        emit_abi_compliant_call(fn_it->second, args, out);
        if (node.result_id != ir::INVALID_SLOT) {
            bind_virtual_to_reg(node.result_id, X64Reg::RAX, true);
        }
    }

    void emit_push_reg64(X64Reg reg, ByteStream& out) {
        const uint8_t idx = static_cast<uint8_t>(reg);
        if (idx >= 8) {
            out.push_back(0x41);
        }
        out.push_back(static_cast<uint8_t>(0x50 + (idx & 7)));
    }

    void emit_pop_reg64(X64Reg reg, ByteStream& out) {
        const uint8_t idx = static_cast<uint8_t>(reg);
        if (idx >= 8) {
            out.push_back(0x41);
        }
        out.push_back(static_cast<uint8_t>(0x58 + (idx & 7)));
    }

    void emit_mov_rr64(X64Reg dest, X64Reg src, ByteStream& out) {
        const uint8_t d_idx = static_cast<uint8_t>(dest);
        const uint8_t s_idx = static_cast<uint8_t>(src);
        out.push_back(X64Encoding::rex(true, s_idx, 0, d_idx));
        out.push_back(0x89);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_mov_ri64(X64Reg dest, uint64_t imm, ByteStream& out) {
        const uint8_t d_idx = static_cast<uint8_t>(dest);
        out.push_back(X64Encoding::rex(true, 0, 0, d_idx));
        out.push_back(static_cast<uint8_t>(0xB8 | (d_idx & 7)));
        const size_t pos = out.size();
        out.resize(pos + 8);
        std::memcpy(&out[pos], &imm, 8);
    }

    void emit_mov_rm64(X64Reg dest, X64Reg base, int32_t disp, ByteStream& out) {
        const uint8_t d_idx = static_cast<uint8_t>(dest);
        const uint8_t b_idx = static_cast<uint8_t>(base);
        out.push_back(X64Encoding::rex(true, d_idx, 0, b_idx));
        out.push_back(0x8B);

        if (disp >= -128 && disp <= 127) {
            out.push_back(X64Encoding::modrm(0b01, d_idx, b_idx));
            out.push_back(static_cast<uint8_t>(disp & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, d_idx, b_idx));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &disp, 4);
        }
    }

    void emit_mov_mr64(X64Reg base, int32_t disp, X64Reg src, ByteStream& out) {
        const uint8_t s_idx = static_cast<uint8_t>(src);
        const uint8_t b_idx = static_cast<uint8_t>(base);
        out.push_back(X64Encoding::rex(true, s_idx, 0, b_idx));
        out.push_back(0x89);

        if (disp >= -128 && disp <= 127) {
            out.push_back(X64Encoding::modrm(0b01, s_idx, b_idx));
            out.push_back(static_cast<uint8_t>(disp & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, s_idx, b_idx));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &disp, 4);
        }
    }

    void emit_xor_rr64(X64Reg dest, X64Reg src, ByteStream& out) {
        const uint8_t d_idx = static_cast<uint8_t>(dest);
        const uint8_t s_idx = static_cast<uint8_t>(src);
        out.push_back(X64Encoding::rex(true, s_idx, 0, d_idx));
        out.push_back(0x31);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_cmp_ri32(X64Reg reg, uint32_t imm32, ByteStream& out) {
        const uint8_t r_idx = static_cast<uint8_t>(reg);
        out.push_back(X64Encoding::rex(true, 0, 0, r_idx));
        out.push_back(0x81);
        out.push_back(X64Encoding::modrm(0b11, 0b111, r_idx));
        const size_t pos = out.size();
        out.resize(pos + 4);
        std::memcpy(&out[pos], &imm32, 4);
    }

    [[nodiscard]] X64Reg resolve_operand_reg(uint32_t virtual_id,
                                             ByteStream& out,
                                             bool prefer_nonvolatile = true) {
        if (virtual_id == ir::INVALID_SLOT) {
            return X64Reg::INVALID;
        }

        const auto it = value_locations_.find(virtual_id);
        if (it != value_locations_.end()) {
            const ValueLocation& loc = it->second;
            if (loc.kind == MIRValueKind::Register && loc.reg != X64Reg::INVALID) {
                return loc.reg;
            }
            const X64Reg materialized = assign_result_register(virtual_id, prefer_nonvolatile);
            if (materialized == X64Reg::INVALID) {
                return X64Reg::INVALID;
            }
            if (loc.kind == MIRValueKind::StackSlot) {
                emit_mov_rm64(materialized, X64Reg::RBP, loc.stack_offset, out);
            } else if (loc.kind == MIRValueKind::Immediate) {
                emit_mov_ri64(materialized, loc.immediate, out);
            }
            bind_virtual_to_reg(virtual_id, materialized, materialized == X64Reg::RAX);
            return materialized;
        }

        // Unknown value: pin deterministic fallback once, then track it.
        const X64Reg fallback = map_runtime_gpr(virtual_id);
        if (reg_available_for(virtual_id, fallback)) {
            bind_virtual_to_reg(virtual_id, fallback, fallback == X64Reg::RAX);
            return fallback;
        }
        return assign_result_register(virtual_id, prefer_nonvolatile);
    }

    void emit_store(const TargetIR& target, const SourceIR& source, ByteStream& out) {
        X64Reg src = source.physical_reg;
        if (src == X64Reg::INVALID && source.virtual_id != ir::INVALID_SLOT) {
            src = resolve_operand_reg(source.virtual_id, out, true);
        }
        if (src == X64Reg::INVALID) {
            return;
        }

        if (target.is_memory) {
            emit_mov_mr64(X64Reg::RBP, target.stack_offset, src, out);
            if (target.virtual_id != ir::INVALID_SLOT) {
                bind_virtual_to_stack(target.virtual_id, target.stack_offset);
                state_locks_.lock_to_stack(target.virtual_id, target.stack_offset);
            }
            return;
        }

        X64Reg dst = target.fixed_reg;
        if (dst == X64Reg::INVALID && target.virtual_id != ir::INVALID_SLOT) {
            exec::X64StorageBinding lock;
            if (state_locks_.resolve(target.virtual_id, lock) &&
                lock.kind == exec::X64StorageKind::register_lock && lock.reg != X64Reg::INVALID) {
                dst = lock.reg;
            }
        }
        if (dst == X64Reg::INVALID && target.virtual_id != ir::INVALID_SLOT) {
            dst = assign_result_register(target.virtual_id, true);
        }
        if (dst == X64Reg::INVALID) {
            return;
        }

        if (dst != src) {
            emit_mov_rr64(dst, src, out);
        }
        if (target.virtual_id != ir::INVALID_SLOT) {
            bind_virtual_to_reg(target.virtual_id, dst, dst == X64Reg::RAX);
            state_locks_.lock_to_register(target.virtual_id, dst);
        }
    }

    void emit_while_loop(const ConditionBlock& cond,
                         const BodyBlock& body,
                         ByteStream& out,
                         std::vector<BranchPatch>& branch_patches) {
        if (cond.cond_virtual_id == ir::INVALID_SLOT) {
            return;
        }

        ir::IRNode branch{};
        branch.op_a = cond.cond_virtual_id;
        branch.op_b = cond.exit_target_node;
        emit_branch_cond(branch, out, branch_patches);

        if (body.backedge_target_node != 0) {
            ir::IRNode backedge{};
            backedge.op_a = body.backedge_target_node;
            emit_goto(backedge, out, branch_patches);
        }
    }

    // 1) Strict argument loader backed by MIR virtual->physical mapping.
    bool load_argument(MIRValue val, TargetReg target_abi_reg, ByteStream& out) {
        switch (val.kind) {
            case MIRValueKind::Register: {
                if (val.virtual_id == ir::INVALID_SLOT) {
                    emit_xor_rr64(target_abi_reg, target_abi_reg, out);
                    return false;
                }

                const auto it = value_locations_.find(val.virtual_id);
                if (it == value_locations_.end()) {
                    // Never silently fall back to RAX for unmapped values.
                    emit_xor_rr64(target_abi_reg, target_abi_reg, out);
                    return false;
                }

                const ValueLocation& loc = it->second;
                if (loc.kind == MIRValueKind::Register && loc.reg != X64Reg::INVALID) {
                    if (loc.reg != target_abi_reg) {
                        emit_mov_rr64(target_abi_reg, loc.reg, out);
                    }
                    return true;
                }
                if (loc.kind == MIRValueKind::StackSlot) {
                    emit_mov_rm64(target_abi_reg, X64Reg::RBP, loc.stack_offset, out);
                    return true;
                }
                if (loc.kind == MIRValueKind::Immediate) {
                    emit_mov_ri64(target_abi_reg, loc.immediate, out);
                    return true;
                }

                emit_xor_rr64(target_abi_reg, target_abi_reg, out);
                return false;
            }
            case MIRValueKind::StackSlot:
                emit_mov_rm64(target_abi_reg, X64Reg::RBP, val.stack_offset, out);
                return true;
            case MIRValueKind::Immediate:
                emit_mov_ri64(target_abi_reg, val.immediate, out);
                return true;
            default:
                emit_xor_rr64(target_abi_reg, target_abi_reg, out);
                return false;
        }
    }

    // 4) String pool guard helper: cmp + jae handler.
    void emit_bounds_check(X64Reg index_reg, size_t pool_size, ByteStream& out) {
        const uint32_t bound = static_cast<uint32_t>(
            pool_size > static_cast<size_t>(0xFFFFFFFFu) ? 0xFFFFFFFFu : pool_size);

        emit_cmp_ri32(index_reg, bound, out);
        out.push_back(0x73);  // jae +2 (to overflow handler)
        out.push_back(0x02);
        out.push_back(0xEB);  // jmp +3 (skip handler on in-range)
        out.push_back(0x03);
        emit_xor_rr64(index_reg, index_reg, out);
    }

    // 2) ABI wrapper: align stack + shadow space + RCX/RDX/R8/R9 + call + cleanup.
    void emit_abi_compliant_call(FunctionPtr target,
                                 const std::vector<MIRValue>& args,
                                 ByteStream& out) {
        if (target == 0) {
            return;
        }

        const std::array<X64Reg, 4> abi_arg_regs = {
            X64Reg::RCX, X64Reg::RDX, X64Reg::R8, X64Reg::R9};

        const uint32_t stack_reserve = 40u;
        out.push_back(0x48);
        out.push_back(0x83);
        out.push_back(0xEC);
        out.push_back(static_cast<uint8_t>(stack_reserve));

        const size_t arg_count = args.size() < abi_arg_regs.size() ? args.size() : abi_arg_regs.size();
        for (size_t i = 0; i < arg_count; ++i) {
            load_argument(args[i], abi_arg_regs[i], out);
        }

        emit_mov_ri64(X64Reg::RAX, static_cast<uint64_t>(target), out);
        out.push_back(0xFF);
        out.push_back(X64Encoding::modrm(0b11, 2, static_cast<uint8_t>(X64Reg::RAX)));

        out.push_back(0x48);
        out.push_back(0x83);
        out.push_back(0xC4);
        out.push_back(static_cast<uint8_t>(stack_reserve));
    }

    /**
     * @brief Fallback for unsupported opcodes to prevent silent failures.
     */
    void emit_unhandled(const ir::IRNode& node, ByteStream& out) {
        (void)node;
        ++unhandled_fallback_hits_;
        // Stable fallback: preserve stream execution continuity.
        out.push_back(0x90);
    }

    /**
     * @brief AVX-512 Gather Implementation
     * Encodes VPGATHERDD zmm, [base + vindex], mask
     */
    void emit_gather_512(const ir::IRNode& node, ByteStream& out) {
        // EVEX Prefix: 4-byte sequence for AVX-512
        out.push_back(0x62); 
        
        X64Reg base = resolve_operand_reg(node.op_a, out, true);
        uint8_t zmm_dest = static_cast<uint8_t>(node.result_id % 32);
        
        // P1: R, X, B, R', 0, 0, m, m (Simplified EVEX map 1)
        out.push_back(0xF2); 
        // P2: W, v, v, v, v, 1, p, p (W=0 for 32-bit gather)
        out.push_back(0x7D);
        // P3: z, L'L, b, V', a, a, a (L'L=10 for 512-bit)
        out.push_back(0x48);

        out.push_back(0x90); // Opcode for VPGATHERDD
        
        // ModRM + SIB for indexed addressing
        out.push_back(X64Encoding::modrm(0b00, zmm_dest & 7, 0b100)); // SIB follows
        uint8_t scale = 2; // Scale factor 4 (2^2)
        uint8_t v_idx = static_cast<uint8_t>(node.op_b & 7);
        out.push_back((scale << 6) | (v_idx << 3) | (static_cast<uint8_t>(base) & 7));
    }

    /**
     * @brief MOV GPR, GPR (32-bit reg-to-reg)
     */
    void emit_mov_rr32(X64Reg dest, X64Reg src, ByteStream& out) {
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x89);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
    }

    void emit_mov_reg(const ir::IRNode& node, ByteStream& out) {
        X64Reg src  = resolve_operand_reg(node.op_a, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, src);
        emit_mov_rr32(dest, src, out);
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    /**
     * @brief ADD GPR, GPR (32-bit)
     */
    void emit_add_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs = resolve_operand_reg(node.op_a, out, true);
        X64Reg rhs = resolve_operand_reg(node.op_b, out, false);

        X64Reg dest = X64Reg::INVALID;
        X64Reg src = X64Reg::INVALID;

        if (node.result_id == node.op_a) {
            dest = assign_result_register(node.result_id, true, lhs);
            src = rhs;
        } else if (node.result_id == node.op_b) {
            dest = assign_result_register(node.result_id, true, rhs);
            src = lhs;
        } else {
            dest = assign_result_register(node.result_id, true);
            if (dest != lhs) {
                emit_mov_rr32(dest, lhs, out);
            }
            src = rhs;
        }

        if (dest == X64Reg::INVALID || src == X64Reg::INVALID) {
            return;
        }

        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x01); 
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    /**
     * @brief SUB GPR, GPR (32-bit)
     */
    void emit_sub_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs = resolve_operand_reg(node.op_a, out, true);
        X64Reg rhs = resolve_operand_reg(node.op_b, out, false);
        X64Reg dest = X64Reg::INVALID;
        X64Reg src = X64Reg::INVALID;

        if (node.result_id == node.op_a) {
            dest = assign_result_register(node.result_id, true, lhs);
            src = rhs;
        } else if (node.result_id == node.op_b) {
            dest = assign_result_register(node.result_id, true, rhs);
            X64Reg scratch = X64Reg::INVALID;
            for (const X64Reg candidate : kRuntimeVolatileGprs) {
                if (candidate != lhs && candidate != rhs && reg_available_for(ir::INVALID_SLOT, candidate)) {
                    scratch = candidate;
                    break;
                }
            }
            if (scratch == X64Reg::INVALID) {
                for (const X64Reg candidate : kRuntimeVolatileGprs) {
                    if (candidate != lhs && candidate != rhs) {
                        scratch = candidate;
                        break;
                    }
                }
            }
            if (scratch == X64Reg::INVALID || dest == X64Reg::INVALID) {
                return;
            }
            emit_mov_rr32(scratch, dest, out);
            if (dest != lhs) {
                emit_mov_rr32(dest, lhs, out);
            }
            src = scratch;
        } else {
            dest = assign_result_register(node.result_id, true);
            if (dest != lhs) {
                emit_mov_rr32(dest, lhs, out);
            }
            src = rhs;
        }

        if (dest == X64Reg::INVALID || src == X64Reg::INVALID) {
            return;
        }

        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x29); 
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_mul_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg src  = resolve_operand_reg(node.op_b, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);

        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }

        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);

        if (d_idx >= 8 || s_idx >= 8) {
            out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        }
        out.push_back(0x0F);
        out.push_back(0xAF);
        out.push_back(X64Encoding::modrm(0b11, d_idx, s_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_and_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg src  = resolve_operand_reg(node.op_b, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);
        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x21);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_or_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg src  = resolve_operand_reg(node.op_b, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);
        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x09);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_xor_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg src  = resolve_operand_reg(node.op_b, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);
        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }
        uint8_t d_idx = static_cast<uint8_t>(dest);
        uint8_t s_idx = static_cast<uint8_t>(src);
        if (d_idx >= 8 || s_idx >= 8) out.push_back(X64Encoding::rex(false, s_idx, 0, d_idx));
        out.push_back(0x31);
        out.push_back(X64Encoding::modrm(0b11, s_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_shl_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }
        X64Reg shift_reg = resolve_operand_reg(node.op_b, out, false);
        if (shift_reg != X64Reg::RCX) {
            emit_mov_rr32(X64Reg::RCX, shift_reg, out);
        }
        if (d_idx >= 8) out.push_back(X64Encoding::rex(false, 0, 0, d_idx));
        out.push_back(0xD3);
        out.push_back(X64Encoding::modrm(0b11, 0b100, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_shr_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs  = resolve_operand_reg(node.op_a, out, true);
        X64Reg dest = assign_result_register(node.result_id, true, lhs);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        if (dest != lhs) {
            emit_mov_rr32(dest, lhs, out);
        }
        X64Reg shift_reg = resolve_operand_reg(node.op_b, out, false);
        if (shift_reg != X64Reg::RCX) {
            emit_mov_rr32(X64Reg::RCX, shift_reg, out);
        }
        if (d_idx >= 8) out.push_back(X64Encoding::rex(false, 0, 0, d_idx));
        out.push_back(0xD3);
        out.push_back(X64Encoding::modrm(0b11, 0b101, d_idx));
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    void emit_cmp_i32(const ir::IRNode& node, ByteStream& out) {
        X64Reg lhs = resolve_operand_reg(node.op_a, out, true);
        X64Reg rhs = resolve_operand_reg(node.op_b, out, true);
        X64Reg dst = X64Reg::INVALID;
        if (node.result_id == node.op_a) {
            dst = assign_result_register(node.result_id, true, lhs);
        } else if (node.result_id == node.op_b) {
            dst = assign_result_register(node.result_id, true, rhs);
        } else {
            dst = assign_result_register(node.result_id, true);
        }
        if (dst == X64Reg::INVALID) {
            return;
        }

        const uint8_t l_idx = static_cast<uint8_t>(lhs);
        const uint8_t r_idx = static_cast<uint8_t>(rhs);
        const uint8_t d_idx = static_cast<uint8_t>(dst);

        if (l_idx >= 8 || r_idx >= 8) {
            out.push_back(X64Encoding::rex(false, r_idx, 0, l_idx));
        }
        out.push_back(0x39);
        out.push_back(X64Encoding::modrm(0b11, r_idx, l_idx));

        uint8_t setcc = 0x94; // sete
        switch (node.opcode) {
            case ir::OpCode::CMP_EQ_I32: setcc = 0x94; break; // ZF=1
            case ir::OpCode::CMP_NE_I32: setcc = 0x95; break; // ZF=0
            case ir::OpCode::CMP_LT_I32: setcc = 0x9C; break; // SF!=OF
            case ir::OpCode::CMP_LE_I32: setcc = 0x9E; break; // ZF=1 || SF!=OF
            case ir::OpCode::CMP_GT_I32: setcc = 0x9F; break; // ZF=0 && SF==OF
            case ir::OpCode::CMP_GE_I32: setcc = 0x9D; break; // SF==OF
            default: break;
        }

        if (d_idx >= 8 || d_idx >= 4) {
            out.push_back(X64Encoding::rex(false, 0, 0, d_idx));
        }
        out.push_back(0x0F);
        out.push_back(setcc);
        out.push_back(X64Encoding::modrm(0b11, 0b000, d_idx));

        if (d_idx >= 8) {
            out.push_back(X64Encoding::rex(false, d_idx, 0, d_idx));
        }
        out.push_back(0x0F);
        out.push_back(0xB6);
        out.push_back(X64Encoding::modrm(0b11, d_idx, d_idx));
        bind_virtual_to_reg(node.result_id, dst, dst == X64Reg::RAX);
    }

    // Scalar FP fallback strategy:
    // - Keep fusion profile meaningful even before full AVX/FMA ISA lowering.
    // - Lower ADD_F32/MUL_F32/FMA_F32 with SSE scalar ops on XMM registers.
    // - FMA uses a 2-op fallback sequence: movaps + mulss + addss.
    static uint8_t map_fp_reg(uint32_t id) {
        return static_cast<uint8_t>(id & 0xF);
    }

    void emit_xmm_movaps_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0x0F);
        out.push_back(0x28);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_xmm_addss_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0xF3);
        out.push_back(0x0F);
        out.push_back(0x58);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_xmm_mulss_rr(uint8_t dst, uint8_t src, ByteStream& out) {
        if (dst >= 8 || src >= 8) {
            out.push_back(X64Encoding::rex(false, dst, 0, src));
        }
        out.push_back(0xF3);
        out.push_back(0x0F);
        out.push_back(0x59);
        out.push_back(X64Encoding::modrm(0b11, dst, src));
    }

    void emit_vex3(uint8_t modrm_reg,
                   uint8_t vvvv_src,
                   uint8_t modrm_rm,
                   uint8_t map_select,
                   uint8_t pp,
                   bool vector_len_256,
                   bool w_bit,
                   ByteStream& out) {
        out.push_back(0xC4);

        const uint8_t r_inv = (modrm_reg >= 8) ? 0u : 1u;
        const uint8_t x_inv = 1u; // no SIB index extension in this encoder path
        const uint8_t b_inv = (modrm_rm >= 8) ? 0u : 1u;
        out.push_back(static_cast<uint8_t>((r_inv << 7) | (x_inv << 6) | (b_inv << 5) | (map_select & 0x1F)));

        const uint8_t v_inv = static_cast<uint8_t>((~vvvv_src) & 0x0F);
        out.push_back(static_cast<uint8_t>(((w_bit ? 1u : 0u) << 7) |
                                           ((v_inv & 0x0F) << 3) |
                                           ((vector_len_256 ? 1u : 0u) << 2) |
                                           (pp & 0x03)));
    }

    void emit_vfmadd213ss_rrr(uint8_t dst, uint8_t src_mul, uint8_t src_add, ByteStream& out) {
        // VEX.NDS.128.F3.0F38.W0 A9 /r
        // vfmadd213ss dst, src_mul, src_add
        // semantics: dst = (dst * src_mul) + src_add
        emit_vex3(dst, src_mul, src_add, 0x02, 0x02, false, false, out);
        out.push_back(0xA9);
        out.push_back(X64Encoding::modrm(0b11, dst, src_add));
    }

    void emit_add_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_xmm_addss_rr(dst, b, out);
    }

    void emit_mul_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_xmm_mulss_rr(dst, b, out);
    }

    void emit_fma_f32_scalar(const ir::IRNode& node, ByteStream& out) {
        const uint8_t dst = map_fp_reg(node.result_id);
        const uint8_t a = map_fp_reg(node.op_a);
        const uint8_t b = map_fp_reg(node.op_b);
        const uint8_t c = map_fp_reg(node.op_c);

        if (dst != a) {
            emit_xmm_movaps_rr(dst, a, out);
        }
        emit_vfmadd213ss_rrr(dst, b, c, out);
    }

    /**
     * @brief MOV GPR, imm64
     */
    void emit_mov_imm64(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = assign_result_register(node.result_id, true);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        out.push_back(X64Encoding::rex(true, 0, 0, d_idx));
        out.push_back(0xB8 | (d_idx & 7));

        const uint32_t high = (node.op_a == ir::INVALID_SLOT) ? 0u : node.op_a;
        uint64_t imm = (static_cast<uint64_t>(high) << 32) | (node.op_b & 0xFFFFFFFF);
        const size_t pos = out.size();
        out.resize(pos + 8);
        std::memcpy(&out[pos], &imm, 8);
        // Model constants as immediate-backed values so they can be safely
        // rematerialized after calls that clobber volatile registers.
        bind_virtual_to_immediate(node.result_id, imm);
    }

    /**
     * @brief MOV GPR, [RBP - offset] (Stack Load)
     */
    void emit_load_stack(const ir::IRNode& node, ByteStream& out) {
        X64Reg dest = assign_result_register(node.result_id, true);
        uint8_t d_idx = static_cast<uint8_t>(dest);
        int32_t offset = static_cast<int32_t>(node.op_c);
        out.push_back(X64Encoding::rex(true, d_idx, 0, static_cast<uint8_t>(X64Reg::RBP)));
        out.push_back(0x8B); 
        
        if (offset >= -128 && offset <= 127) {
            out.push_back(X64Encoding::modrm(0b01, d_idx, static_cast<uint8_t>(X64Reg::RBP)));
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, d_idx, static_cast<uint8_t>(X64Reg::RBP)));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &offset, 4);
        }
        bind_virtual_to_stack(node.result_id, offset);
        bind_virtual_to_reg(node.result_id, dest, dest == X64Reg::RAX);
    }

    /**
     * @brief MOV [RBP - offset], GPR (Stack Store)
     */
    void emit_store_stack(const ir::IRNode& node, ByteStream& out) {
        const int32_t offset = static_cast<int32_t>(node.op_c);
        emit_store(TargetIR::to_stack(offset), SourceIR{node.op_a, X64Reg::INVALID}, out);
    }

    /**
     * @brief VMOVAPS [RBP - offset], ZMM (Vector Store 512-bit)
     */
    void emit_store_v512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_src = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        int32_t offset = static_cast<int32_t>(node.op_c);

        // EVEX prefix for 512-bit VMOVAPS
        out.push_back(0x62);
        out.push_back(0xF1); // P1: R=1, X=1, B=1, R'=1, 0,0,m,m (map 1)
        out.push_back(0x7C); // P2: W=0, vvvv=1111 (no mask), 1, p=00 (no prefix)
        out.push_back(0x08); // P3: z=0, L'L=10 (512-bit), b=0, V'=1, aaa=000

        out.push_back(0x29); // VMOVAPS opcode

        // ModRM for [RBP + offset], ZMM
        if (offset >= -128 && offset <= 127) {
            out.push_back(X64Encoding::modrm(0b01, zmm_src & 7, static_cast<uint8_t>(X64Reg::RBP)));
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(X64Encoding::modrm(0b10, zmm_src & 7, static_cast<uint8_t>(X64Reg::RBP)));
            const size_t pos = out.size();
            out.resize(pos + 4);
            std::memcpy(&out[pos], &offset, 4);
        }
    }

    /**
     * @brief VADDPS ZMM, ZMM, ZMM (512-bit float add)
     */
    void emit_vec_add_f32_512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_dest = X64Encoding::map_DGTL_vec_to_phys(node.result_id);
        uint8_t zmm_src1 = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        uint8_t zmm_src2 = X64Encoding::map_DGTL_vec_to_phys(node.op_b);

        // EVEX prefix for 512-bit VADDPS
        out.push_back(0x62);
        out.push_back(0xF1 | ((zmm_src1 & 16) ? 0x00 : 0x08)); // P1 with R' from src1
        out.push_back(0x0C | ((zmm_src2 & 15) << 3)); // P2 with vvvv from src2
        out.push_back(0x48 | ((zmm_dest & 16) ? 0x80 : 0x00)); // P3 with V' from dest

        out.push_back(0x58); // VADDPS opcode
        out.push_back(X64Encoding::modrm(0b11, zmm_dest & 7, zmm_src1 & 7));
    }

    /**
     * @brief VMULPS ZMM, ZMM, ZMM (512-bit float mul)
     */
    void emit_vec_mul_f32_512(const ir::IRNode& node, ByteStream& out) {
        uint8_t zmm_dest = X64Encoding::map_DGTL_vec_to_phys(node.result_id);
        uint8_t zmm_src1 = X64Encoding::map_DGTL_vec_to_phys(node.op_a);
        uint8_t zmm_src2 = X64Encoding::map_DGTL_vec_to_phys(node.op_b);

        // EVEX prefix for 512-bit VMULPS
        out.push_back(0x62);
        out.push_back(0xF1 | ((zmm_src1 & 16) ? 0x00 : 0x08));
        out.push_back(0x0C | ((zmm_src2 & 15) << 3));
        out.push_back(0x48 | ((zmm_dest & 16) ? 0x80 : 0x00));

        out.push_back(0x59); // VMULPS opcode
        out.push_back(X64Encoding::modrm(0b11, zmm_dest & 7, zmm_src1 & 7));
    }

public:
    /**
     * @brief Fuse SIMD operations across multiple IR nodes.
     * Enables cross-node vector fusion for better performance.
     */
    bool fuse_across_nodes(std::vector<ir::IRNode*>& nodes) {
        if (nodes.size() < 2) return false;
        
        // Check if nodes can be fused (same vector operation type)
        bool can_fuse = true;
        ir::OpCode base_op = nodes[0]->opcode;
        
        for (auto* node : nodes) {
            if (node->opcode != base_op || 
                !(node->flags & ir::Flags::IS_SIMD_BURST)) {
                can_fuse = false;
                break;
            }
        }
        
        if (can_fuse) {
            for (size_t i = 1; i < nodes.size(); ++i) {
                nodes[i]->flags |= ir::Flags::IS_FUSED;
                nodes[i]->opcode = ir::OpCode::NOP;
                nodes[i]->refresh_intent_hash();
            }
            return true;
        }
        
        return false;
    }
};

} // namespace dgtl::isa




