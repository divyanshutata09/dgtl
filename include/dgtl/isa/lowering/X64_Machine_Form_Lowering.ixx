/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #undef CONST // Neutralize legacy Windows preprocessor macro
#endif

export module dgtl.ir_core.x64_lowering;

// Import base module and sibling lowering partitions
import dgtl.ir_core;
import dgtl.ir_core.lowering;
import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.legalizer;
import dgtl.ir_core.isel_gen;
import dgtl.target;
import dgtl.ir_core.x64_reg_map;

export namespace dgtl::isa::lowering {

namespace ir = dgtl::ir;

/**
 * @brief X64_Machine_Form_Lowering
 *
 * Handles machine-form lowering: routes selected instructions to target-specific
 * encoding based on generated semantic kind tables. Decouples instruction selection
 * from low-level x64 encoding.
 *
 * The lowering layer queries generated kind tables and dispatches to encoder methods,
 * leaving the emitter as a thin register-state plus byte-encoding shell only.
 */
class X64FormLowering {
public:
    using ByteStream = std::vector<uint8_t>;
    using Emitter = void*; // Forward ref; actual type is X64StrikeEmitter

    // Encoder method type (void (X64StrikeEmitter::*)(const ir::IRNode&, ByteStream&))
    // We use std::function to store these
    using EncoderFunc = std::function<void(const ir::IRNode&, ByteStream&)>;

    struct EncoderCallbacks {
        EncoderFunc add_i32;
        EncoderFunc sub_i32;
        EncoderFunc mul_i32;
        EncoderFunc div_i32;
        EncoderFunc mod_i32;
        EncoderFunc and_i32;
        EncoderFunc or_i32;
        EncoderFunc xor_i32;
        EncoderFunc shl_i32;
        EncoderFunc shr_i32;

        EncoderFunc add_i64;
        EncoderFunc sub_i64;
        EncoderFunc mul_i64;
        EncoderFunc div_i64;
        EncoderFunc mod_i64;
        EncoderFunc and_i64;
        EncoderFunc or_i64;
        EncoderFunc xor_i64;
        EncoderFunc shl_i64;
        EncoderFunc shr_i64;

        EncoderFunc add_f32;
        EncoderFunc sub_f32;
        EncoderFunc mul_f32;
        EncoderFunc div_f32;

        EncoderFunc add_f64;
        EncoderFunc sub_f64;
        EncoderFunc mul_f64;
        EncoderFunc div_f64;

        EncoderFunc cmp_i32;

        EncoderFunc vec_add_f32_512;
        EncoderFunc vec_mul_f32_512;

        std::function<void(const char*, uint32_t)> fail_emission;
    };

    explicit X64FormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

    inline void lower_integer_binary32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::integer_binary32_kind(selected.semantic_opcode)) {
            case generated::X64IntegerBinary32Kind::Add:
                callbacks_.add_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Sub:
                callbacks_.sub_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Mul:
                callbacks_.mul_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Div:
                callbacks_.div_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Mod:
                callbacks_.mod_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::And:
                callbacks_.and_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Or:
                callbacks_.or_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Xor:
                callbacks_.xor_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Shl:
                callbacks_.shl_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Shr:
                callbacks_.shr_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary32 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_integer_binary64(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::integer_binary64_kind(selected.semantic_opcode)) {
            case generated::X64IntegerBinary64Kind::Add:
                callbacks_.add_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Sub:
                callbacks_.sub_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Mul:
                callbacks_.mul_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Div:
                callbacks_.div_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Mod:
                callbacks_.mod_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::And:
                if (callbacks_.and_i64) callbacks_.and_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Or:
                if (callbacks_.or_i64) callbacks_.or_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Xor:
                if (callbacks_.xor_i64) callbacks_.xor_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Shl:
                if (callbacks_.shl_i64) callbacks_.shl_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Shr:
                if (callbacks_.shr_i64) callbacks_.shr_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary64 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_float_binary32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::float_binary32_kind(selected.semantic_opcode)) {
            case generated::X64FloatBinary32Kind::Add:
                callbacks_.add_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::Sub:
                if (callbacks_.sub_f32) callbacks_.sub_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::Mul:
                callbacks_.mul_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::Div:
                if (callbacks_.div_f32) callbacks_.div_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::FloatBinary32 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_float_binary64(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::float_binary64_kind(selected.semantic_opcode)) {
            case generated::X64FloatBinary64Kind::Add:
                if (callbacks_.add_f64) callbacks_.add_f64(selected.node, out);
                return;
            case generated::X64FloatBinary64Kind::Sub:
                if (callbacks_.sub_f64) callbacks_.sub_f64(selected.node, out);
                return;
            case generated::X64FloatBinary64Kind::Mul:
                if (callbacks_.mul_f64) callbacks_.mul_f64(selected.node, out);
                return;
            case generated::X64FloatBinary64Kind::Div:
                if (callbacks_.div_f64) callbacks_.div_f64(selected.node, out);
                return;
            case generated::X64FloatBinary64Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::FloatBinary64 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_compare32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::compare32_kind(selected.semantic_opcode)) {
            case generated::X64Compare32Kind::Any:
                callbacks_.cmp_i32(selected.node, out);
                return;
            case generated::X64Compare32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::Compare32 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_vector_binary512(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::vector_binary512_kind(selected.semantic_opcode)) {
            case generated::X64VectorBinary512Kind::Add:
                callbacks_.vec_add_f32_512(selected.node, out);
                return;
            case generated::X64VectorBinary512Kind::Mul:
                callbacks_.vec_mul_f32_512(selected.node, out);
                return;
            case generated::X64VectorBinary512Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::VectorBinary512 opcode",
                                         static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

private:
    EncoderCallbacks callbacks_;
};

} // namespace dgtl::isa::lowering

export namespace dgtl::isa {

class X64StrikeEmitter {
public:
    struct PendingBranchPatch {
        size_t displacement_offset = 0;
        size_t instruction_end_offset = 0;
        uint32_t target_label = ir::INVALID_SLOT;
    };

    X64StrikeEmitter()
                : target_machine_(core::ArchType::X86_64_AVX2),
                  selector_(lowering::make_builtin_instruction_selector_program()) {}

    void set_target_machine(const core::TargetMachine& target) {
        target_machine_ = target;
    }

    void register_runtime_function(const std::string& name, uintptr_t address) {
        runtime_functions_[name] = address;
    }

    void register_runtime_function_slot(uint32_t slot, uintptr_t address) {
        runtime_slots_[slot] = address;
    }

    [[nodiscard]] bool had_emission_error() const noexcept {
        return had_error_;
    }

    [[nodiscard]] const char* last_emission_error() const noexcept {
        return last_error_.c_str();
    }

    [[nodiscard]] ir::OpCode last_failed_opcode() const noexcept {
        return last_failed_opcode_;
    }

    [[nodiscard]] size_t get_label_offset(uint32_t label_id) const noexcept {
        auto it = label_offsets_.find(label_id);
        return it != label_offsets_.end() ? it->second : 0;
    }

    [[nodiscard]] bool process_stream(const ir::IRStream& stream, std::vector<uint8_t>& out) const {
        out.clear();
        label_offsets_.clear();
        had_error_ = false;
        last_error_.clear();
        last_failed_opcode_ = ir::OpCode::NOP;

        const auto legalized = legalizer_.legalize(stream, target_machine_);
        const ir::IRStream& machine_stream = legalized.stream;

        std::unordered_map<uint32_t, size_t> label_to_node_index;
        label_to_node_index.reserve(machine_stream.size() / 4 + 4);
        for (size_t i = 0; i < machine_stream.size(); ++i) {
            const ir::IRNode n = machine_stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                if (n.result_id != ir::INVALID_SLOT && n.result_id != 0u) {
                    label_to_node_index[n.result_id] = i;
                }
                const bool is_func_header = (n.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                if (!is_func_header) {
                    if (n.op_a != ir::INVALID_SLOT && n.op_a != 0u) {
                        label_to_node_index[n.op_a] = i;
                    }
                    if (n.op_b != ir::INVALID_SLOT && n.op_b != 0u) {
                        label_to_node_index[n.op_b] = i;
                    }
                }
            }
        }

        std::vector<PendingBranchPatch> branch_patches;
        branch_patches.reserve(machine_stream.size() / 8 + 4);
        
        std::vector<size_t> node_offsets(machine_stream.size(), 0);

        for (size_t i = 0; i < machine_stream.size(); ++i) {
            node_offsets[i] = out.size();
            const ir::IRNode node = machine_stream.get_node(i);
            if (node.opcode == ir::OpCode::LABEL) {
                size_t byte_offset = out.size();
                if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                    label_offsets_[node.result_id] = byte_offset;
                }
                const bool is_func_header = (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                if (!is_func_header) {
                    if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                        label_offsets_[node.op_a] = byte_offset;
                    }
                    if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                        label_offsets_[node.op_b] = byte_offset;
                    }
                }
            }
            const auto selected = selector_.select_instruction(machine_stream, i, node, target_machine_);

            switch (selected.form) {
                case lowering::MachineForm::Move:
                    if (node.opcode == ir::OpCode::SELECT || node.opcode == ir::OpCode::BOOL_SELECT) {
                        emit_select(selected, out);
                    } else if (node.opcode == ir::OpCode::GEP || node.opcode == ir::OpCode::LEA) {
                        emit_gep(selected, out);
                    } else if (node.opcode == ir::OpCode::STORE || node.opcode == ir::OpCode::VOLATILE_STORE) {
                        emit_store_stack(selected, out, machine_stream);
                    } else {
                        emit_move(selected, out);
                    }
                    break;
                case lowering::MachineForm::LoadImmediate:
                    if (node.opcode == ir::OpCode::CONST && (node.flags & ir::Flags::IS_SPEC_CONST)) {
                        emit_load_imm_f64(selected, out);
                    } else {
                        emit_load_imm(selected, out);
                    }
                    break;
                case lowering::MachineForm::LoadStack:
                    emit_load_stack(selected, out, machine_stream);
                    break;
                case lowering::MachineForm::StoreStack:
                    emit_store_stack(selected, out, machine_stream);
                    break;
                case lowering::MachineForm::IntegerBinary32:
                    emit_i32_binary(selected, out);
                    break;
                case lowering::MachineForm::IntegerBinary64:
                    emit_i64_binary(selected, out);
                    break;
                case lowering::MachineForm::FloatBinary32:
                    emit_f32_binary(selected, out);
                    break;
                case lowering::MachineForm::FloatBinary64:
                    emit_f64_binary(selected, out);
                    break;
                case lowering::MachineForm::FloatTernaryFma32:
                    emit_f32_fma(selected, out);
                    break;
                case lowering::MachineForm::Compare32:
                    emit_cmp_i32(selected, out);
                    break;
                case lowering::MachineForm::VectorBinary512:
                    emit_vec_binary512(selected, out);
                    break;
                case lowering::MachineForm::VectorGather512:
                    emit_vec_gather512(selected, out);
                    break;
                case lowering::MachineForm::VectorStore512:
                    emit_vec_store512(selected, out);
                    break;
                case lowering::MachineForm::Call:
                    emit_call_slot(selected, i, out, branch_patches, label_to_node_index, machine_stream);
                    break;
                case lowering::MachineForm::Goto:
                    emit_goto(selected, out, branch_patches, label_to_node_index);
                    break;
                case lowering::MachineForm::BranchCond:
                    emit_branch_cond(selected, out, branch_patches, label_to_node_index);
                    break;
                case lowering::MachineForm::BranchCompare:
                    emit_branch_compare_fused(selected, out, branch_patches, label_to_node_index);
                    break;
                case lowering::MachineForm::Syscall:
                    emit_syscall(selected, out);
                    break;
                case lowering::MachineForm::Return: {
                    const uint32_t frame_size = compute_frame_size(machine_stream);
                    // Move return value to RAX / XMM0 if present
                    if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                        const ir::TypeCode tc = machine_stream.get_result_type_code(node.op_a);
                        if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                            emit_movsd_xmm_from_stack(out, 0, vreg_offset(node.op_a));
                        } else {
                            emit_mov_reg_from_stack(out, static_cast<uint8_t>(exec::X64Reg::RAX), vreg_offset(node.op_a));
                        }
                    } else if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                        emit_mov_reg_from_stack(out, static_cast<uint8_t>(exec::X64Reg::RAX), vreg_offset(node.result_id));
                    }

#if defined(_WIN32)
                    emit_add_rsp(out, frame_size);
                    out.push_back(0x41); out.push_back(0x5F); // pop r15
                    out.push_back(0x41); out.push_back(0x5E); // pop r14
                    out.push_back(0x41); out.push_back(0x5D); // pop r13
                    out.push_back(0x41); out.push_back(0x5C); // pop r12
                    out.push_back(0x5F); // pop rdi
                    out.push_back(0x5E); // pop rsi
                    out.push_back(0x5D); // pop rbp
                    out.push_back(0x5B); // pop rbx
#endif
                    out.push_back(0xC3);
                    break;
                }
                case lowering::MachineForm::NoOp:
                    if (node.opcode == ir::OpCode::LABEL &&
                        (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE))) {
#if defined(_WIN32)
                        const uint32_t frame_size = compute_frame_size(machine_stream);
                        out.push_back(0x53); // push rbx
                        out.push_back(0x55); // push rbp
                        out.push_back(0x56); // push rsi
                        out.push_back(0x57); // push rdi
                        out.push_back(0x41); out.push_back(0x54); // push r12
                        out.push_back(0x41); out.push_back(0x55); // push r13
                        out.push_back(0x41); out.push_back(0x56); // push r14
                        out.push_back(0x41); out.push_back(0x57); // push r15
                        emit_sub_rsp(out, frame_size);
#endif
                        emit_function_prologue_params(node, i, out, machine_stream);
                    }
                    break;
                case lowering::MachineForm::Unhandled:
                default:
                    mark_error("Unhandled machine form selected by ISel tables", selected.node.opcode);
                    out.push_back(0x90); 
                    break;
            }
        }

        // --- BACKPATCH BRANCH JUMPS & CALLS ---
        for (const PendingBranchPatch& patch : branch_patches) {
            size_t target_node_index = machine_stream.size();
            const auto label_it = label_to_node_index.find(patch.target_label);
            if (label_it != label_to_node_index.end()) {
                target_node_index = label_it->second;
            } else {
                auto offset_it = label_offsets_.find(patch.target_label);
                if (offset_it != label_offsets_.end()) {
                    const int64_t target_offset = static_cast<int64_t>(offset_it->second);
                    const int64_t instr_end = static_cast<int64_t>(patch.instruction_end_offset);
                    const int32_t rel32 = static_cast<int32_t>(target_offset - instr_end);

                    out[patch.displacement_offset + 0] = static_cast<uint8_t>((rel32 >> 0) & 0xFF);
                    out[patch.displacement_offset + 1] = static_cast<uint8_t>((rel32 >> 8) & 0xFF);
                    out[patch.displacement_offset + 2] = static_cast<uint8_t>((rel32 >> 16) & 0xFF);
                    out[patch.displacement_offset + 3] = static_cast<uint8_t>((rel32 >> 24) & 0xFF);
                    continue;
                }

                for (size_t k = 0; k < machine_stream.size(); ++k) {
                    const auto& knode = machine_stream.get_node(k);
                    if (knode.opcode == ir::OpCode::LABEL) {
                        const bool knode_is_func = (knode.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) != 0;
                        if (knode.result_id == patch.target_label || (!knode_is_func && (knode.op_a == patch.target_label || knode.op_b == patch.target_label))) {
                            target_node_index = k;
                            break;
                        }
                    }
                }
            }

            if (target_node_index >= machine_stream.size()) {
                mark_error("Unresolved branch target label", ir::OpCode::GOTO);
                continue;
            }

            const int64_t target_offset = static_cast<int64_t>(node_offsets[target_node_index]);
            const int64_t instr_end = static_cast<int64_t>(patch.instruction_end_offset);
            const int64_t rel64 = target_offset - instr_end;
            if (rel64 < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
                rel64 > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
                mark_error("Branch target displacement out of range", ir::OpCode::GOTO);
                continue;
            }

            const int32_t rel32 = static_cast<int32_t>(rel64);
            if (patch.displacement_offset + 4 > out.size()) {
                mark_error("Invalid branch patch offset", ir::OpCode::GOTO);
                continue;
            }

            out[patch.displacement_offset + 0] = static_cast<uint8_t>((rel32 >> 0) & 0xFF);
            out[patch.displacement_offset + 1] = static_cast<uint8_t>((rel32 >> 8) & 0xFF);
            out[patch.displacement_offset + 2] = static_cast<uint8_t>((rel32 >> 16) & 0xFF);
            out[patch.displacement_offset + 3] = static_cast<uint8_t>((rel32 >> 24) & 0xFF);
        }

        if (out.empty() || out.back() != 0xC3) {
            out.push_back(0xC3);
        }
        return true;
    }

private:
    static void append_u64(std::vector<uint8_t>& out, uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
        }
    }

    static void emit_modrm_reg_reg(std::vector<uint8_t>& out, uint8_t reg, uint8_t rm) {
        const uint8_t modrm = static_cast<uint8_t>(0xC0u | ((reg & 0x7u) << 3u) | (rm & 0x7u));
        out.push_back(modrm);
    }

    static void emit_mov_imm32_signext(std::vector<uint8_t>& out, uint8_t dst, uint32_t imm32) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0xC7); // MOV r/m64, imm32 (sign-extended)
        emit_modrm_reg_reg(out, 0x00, dst);
        out.push_back(static_cast<uint8_t>(imm32 & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 8u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 16u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 24u) & 0xFFu));
    }

    static void emit_prefetch_t0_from_base(std::vector<uint8_t>& out, uint8_t base) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x0F);
        out.push_back(0x18); // PREFETCHh m8

        const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
        if (base_lo == 0x4u) {
            out.push_back(0x0Cu); // mod=00, reg=001 (T0), rm=100 (SIB follows)
            out.push_back(static_cast<uint8_t>(0x20u | base_lo));
            return;
        }
        if (base_lo == 0x5u) {
            out.push_back(0x4Du); // mod=01, reg=001 (T0), rm=101
            out.push_back(0x00u); // disp8 = 0
            return;
        }

        out.push_back(static_cast<uint8_t>(0x08u | base_lo));
    }

    static void emit_clflush_from_base(std::vector<uint8_t>& out, uint8_t base) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x0F);
        out.push_back(0xAE); // CLFLUSH m8 with /7

        const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
        if (base_lo == 0x4u) {
            out.push_back(0x3Cu);
            out.push_back(static_cast<uint8_t>(0x20u | base_lo));
            return;
        }
        if (base_lo == 0x5u) {
            out.push_back(0x7Du);
            out.push_back(0x00u);
            return;
        }

        out.push_back(static_cast<uint8_t>(0x38u | base_lo));
    }

    static constexpr uint32_t MAX_FRAME_SLOTS = 16384;
    static constexpr uint32_t FIXED_FRAME_SIZE = 131112; // 8 mod 16, ensures (64 + 131112 + 8) % 16 == 0 for exact Win64 16-byte ABI alignment

    static uint32_t compute_frame_size(const ir::IRStream& /*stream*/) noexcept {
        return FIXED_FRAME_SIZE;
    }

    static int32_t vreg_offset(uint32_t vreg) noexcept {
        return static_cast<int32_t>(40 + (vreg % MAX_FRAME_SLOTS) * 8);
    }

    static void emit_sub_rsp(std::vector<uint8_t>& out, uint32_t amount) {
        if (amount == 0) return;
        if (amount <= 127) {
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xEC);
            out.push_back(static_cast<uint8_t>(amount & 0xFF));
        } else {
            out.push_back(0x48); out.push_back(0x81); out.push_back(0xEC);
            out.push_back(static_cast<uint8_t>((amount >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 24) & 0xFF));
        }
    }

    static void emit_add_rsp(std::vector<uint8_t>& out, uint32_t amount) {
        if (amount == 0) return;
        if (amount <= 127) {
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xC4);
            out.push_back(static_cast<uint8_t>(amount & 0xFF));
        } else {
            out.push_back(0x48); out.push_back(0x81); out.push_back(0xC4);
            out.push_back(static_cast<uint8_t>((amount >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 24) & 0xFF));
        }
    }

    static void emit_mov_reg_from_stack(std::vector<uint8_t>& out, uint8_t reg, int32_t offset) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(reg),
                                             /*x=*/false,
                                             /*b=*/false));
        out.push_back(0x8B); // MOV r64, [rsp + offset]
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_mov_stack_from_reg(std::vector<uint8_t>& out, int32_t offset, uint8_t reg) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(reg),
                                             /*x=*/false,
                                             /*b=*/false));
        out.push_back(0x89); // MOV [rsp + offset], r64
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movsd_xmm_from_stack(std::vector<uint8_t>& out, uint8_t xmm_reg, int32_t offset) {
        out.push_back(0xF2);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSD xmm, [rsp + offset]
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movsd_stack_from_xmm(std::vector<uint8_t>& out, int32_t offset, uint8_t xmm_reg) {
        out.push_back(0xF2);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x11); // MOVSD [rsp + offset], xmm
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static uint8_t map_reg(uint32_t vreg) noexcept {
        return static_cast<uint8_t>(exec::X64SinglePassRegisterMapper::map_gpr_branchless(vreg));
    }

    static uint64_t combine_u64_from_ops(const ir::IRNode& node) noexcept {
        return (static_cast<uint64_t>(node.op_b) << 32u) | static_cast<uint64_t>(node.op_a);
    }

    static uint64_t immediate_from_node(const ir::IRNode& node) noexcept {
        if (node.opcode == ir::OpCode::STRING_CONST) {
            return static_cast<uint64_t>(node.op_a);
        }
        if (node.opcode == ir::OpCode::CONST ||
            node.opcode == ir::OpCode::LOAD_IMM ||
            node.opcode == ir::OpCode::IMMEDIATE) {
            return combine_u64_from_ops(node);
        }
        uint64_t imm = node.intent_hash;
        uint64_t computed_node_hash = ir::IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c);
        if (imm == 0 || imm == computed_node_hash) {
            imm = combine_u64_from_ops(node);
        }
        return imm;
    }

    static void emit_load_imm(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t dst = map_reg(node.result_id);
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/(dst >= 8u)));
        out.push_back(static_cast<uint8_t>(0xB8u + (dst & 0x7u)));
        append_u64(out, immediate_from_node(node));
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), dst);
        }
    }

    static void emit_load_imm_f64(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint64_t imm = immediate_from_node(node);
        const uint8_t xmm_dst = map_xmm_reg(node.result_id);

        const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
        out.push_back(static_cast<uint8_t>(0xB8u + (rax & 0x7u))); // movabs rax, imm64
        append_u64(out, imm);

        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(xmm_dst),
                                             /*x=*/false,
                                             /*b=*/false));
        out.push_back(0x0F);
        out.push_back(0x6E); // MOVQ xmm, r/m64
        emit_modrm_reg_reg(out, xmm_dst, rax);

        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_movsd_stack_from_xmm(out, vreg_offset(node.result_id), xmm_dst);
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), rax);
        }
    }

    static void emit_movq_xmm_gpr(std::vector<uint8_t>& out, uint8_t xmm_dst, uint8_t gpr_src) {
        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(xmm_dst),
                                             /*x=*/false,
                                             /*b=*/requires_rex(gpr_src)));
        out.push_back(0x0F);
        out.push_back(0x6E); // MOVQ xmm, r/m64
        emit_modrm_reg_reg(out, xmm_dst, gpr_src);
    }

    static void emit_movq_gpr_xmm(std::vector<uint8_t>& out, uint8_t gpr_dst, uint8_t xmm_src) {
        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(xmm_src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(gpr_dst)));
        out.push_back(0x0F);
        out.push_back(0x7E); // MOVQ r/m64, xmm
        emit_modrm_reg_reg(out, xmm_src, gpr_dst);
    }

    void emit_function_prologue_params(const ir::IRNode& node, size_t node_index, std::vector<uint8_t>& out, const ir::IRStream& stream) const {
        const uint8_t gpr_params[4] = {
            static_cast<uint8_t>(exec::X64Reg::RCX),
            static_cast<uint8_t>(exec::X64Reg::RDX),
            static_cast<uint8_t>(exec::X64Reg::R8),
            static_cast<uint8_t>(exec::X64Reg::R9)
        };

        const uint32_t params[4] = {
            node.op_a,
            node.op_b,
            node.op_c,
            node.logic_flags
        };

        for (int i = 0; i < 4; ++i) {
            const uint32_t param_slot = params[i];
            if (param_slot == ir::INVALID_SLOT || param_slot == 0u) {
                continue;
            }

            const ir::TypeCode tc = stream.get_result_type_code(param_slot);
            const uint8_t src_gpr = gpr_params[i];
            const uint8_t src_xmm = static_cast<uint8_t>(i);

            // Always write GPR to the parameter's stack slot
            emit_mov_stack_from_reg(out, vreg_offset(param_slot), src_gpr);

            if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                const uint8_t dst_xmm = map_xmm_reg(param_slot);
                if (dst_xmm != src_xmm) {
                    emit_xmm_move_f64(out, dst_xmm, src_xmm);
                }
                emit_movsd_stack_from_xmm(out, vreg_offset(param_slot), src_xmm);
            } else {
                const uint8_t dst_gpr = map_reg(param_slot);
                if (dst_gpr != src_gpr) {
                    emit_reg_move(out, dst_gpr, src_gpr);
                }
            }
        }

        const auto overflow = stream.get_call_overflow_args(node_index);
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        for (size_t k = 0; k < overflow.size(); ++k) {
            const uint32_t param_slot = overflow[k];
            if (param_slot != ir::INVALID_SLOT && param_slot != 0u) {
                const int32_t incoming_disp = static_cast<int32_t>(FIXED_FRAME_SIZE + 104 + k * 8);
                emit_mov_reg_from_stack(out, r10, incoming_disp);
                emit_mov_stack_from_reg(out, vreg_offset(param_slot), r10);
            }
        }
    }

    static void emit_move(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        uint32_t dst_vreg = node.result_id;
        uint32_t src_vreg = node.op_a;
        if (node.opcode == ir::OpCode::STORE || node.opcode == ir::OpCode::VOLATILE_STORE) {
            dst_vreg = node.op_a;
            src_vreg = node.op_b;
        } else if (dst_vreg == ir::INVALID_SLOT && node.op_a != ir::INVALID_SLOT && node.op_b != ir::INVALID_SLOT) {
            dst_vreg = node.op_a;
            src_vreg = node.op_b;
        }
        if (dst_vreg == ir::INVALID_SLOT || src_vreg == ir::INVALID_SLOT) {
            return;
        }
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t dst = map_reg(dst_vreg);
        emit_mov_reg_from_stack(out, r10, vreg_offset(src_vreg));
        if (dst != r10) {
            emit_reg_move(out, dst, r10);
        }
        emit_mov_stack_from_reg(out, vreg_offset(dst_vreg), r10);
    }

    static constexpr bool requires_rex(uint8_t reg) noexcept {
        return reg >= 8u;
    }

    static uint8_t map_xmm_reg(uint32_t vreg) noexcept {
        return exec::X64SinglePassRegisterMapper::map_xmm_branchless(vreg);
    }

    static uint8_t map_zmm_reg(uint32_t vreg) noexcept {
        return exec::X64Encoding::map_DGTL_vec_to_phys(vreg);
    }

    static void emit_reg_move(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x89); // MOV r/m64, r64
        emit_modrm_reg_reg(out, src, dst);
    }

    static void emit_reg_move32(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/requires_rex(src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x89); // MOV r/m32, r32
        emit_modrm_reg_reg(out, src, dst);
    }

    static void emit_xmm_move(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(0xF3); // MOVSS prefix
        if (requires_rex(dst) || requires_rex(src)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src)));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSS xmm, xmm/m32
        emit_modrm_reg_reg(out, dst, src);
    }

    static void emit_xmm_move_f64(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(0xF2); // MOVSD prefix
        if (requires_rex(dst) || requires_rex(src)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src)));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSD xmm, xmm/m64
        emit_modrm_reg_reg(out, dst, src);
    }

    static void emit_zmm_move(std::vector<uint8_t>& out, uint32_t dst_vreg, uint32_t src_vreg) {
        const uint8_t dst = map_zmm_reg(dst_vreg);
        const uint8_t src = map_zmm_reg(src_vreg);
        out.push_back(0x62); // EVEX prefix byte 0
        emit_modrm_reg_reg(out, dst, src);
    }

    static void emit_vex3_prefix(std::vector<uint8_t>& out,
                                 uint8_t dest,
                                 uint8_t src1,
                                 uint8_t src2,
                                 uint8_t pp,
                                 uint8_t mmmmm,
                                 bool w,
                                 bool l) {
        const uint8_t r_bit = requires_rex(dest) ? 0u : 1u;
        const uint8_t x_bit = 1u;
        const uint8_t b_bit = requires_rex(src2) ? 0u : 1u;
        const uint8_t vvvv = static_cast<uint8_t>(src1 & 0x0Fu);

        out.push_back(0xC4);
        out.push_back(static_cast<uint8_t>((r_bit << 7) | (x_bit << 6) | (b_bit << 5) | (mmmmm & 0x1Fu)));
        out.push_back(static_cast<uint8_t>((w ? 0x80u : 0u) | ((~vvvv & 0x0Fu) << 3) | (l ? 0x04u : 0u) | (pp & 0x03u)));
    }

    static void emit_load_stack(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out, const ir::IRStream& stream) {
        const ir::IRNode& node = selected.node;
        const uint32_t src_vreg = node.op_a;
        const uint32_t dst_vreg = node.result_id;

        const ir::TypeCode tc = stream.get_result_type_code(src_vreg);
        bool is_float = (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32);
        if (!is_float && src_vreg < stream.size()) {
            const auto& src_node = stream.get_node(src_vreg);
            if (src_node.opcode == ir::OpCode::ADD_F64 || src_node.opcode == ir::OpCode::SUB_F64 ||
                src_node.opcode == ir::OpCode::MUL_F64 || src_node.opcode == ir::OpCode::DIV_F64 ||
                src_node.opcode == ir::OpCode::ADD_F32 || src_node.opcode == ir::OpCode::SUB_F32 ||
                src_node.opcode == ir::OpCode::MUL_F32 || src_node.opcode == ir::OpCode::DIV_F32 ||
                (src_node.opcode == ir::OpCode::CONST && (src_node.flags & ir::Flags::IS_SPEC_CONST))) {
                is_float = true;
            }
        }

        if (is_float || tc == ir::TypeCode::F64) {
            const uint8_t dst = map_xmm_reg(dst_vreg);
            if (src_vreg != ir::INVALID_SLOT && src_vreg != 0u) {
                emit_movsd_xmm_from_stack(out, dst, vreg_offset(src_vreg));
            }
            if (dst_vreg != ir::INVALID_SLOT && dst_vreg != 0u) {
                emit_movsd_stack_from_xmm(out, vreg_offset(dst_vreg), dst);
            }
            return;
        }

        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t dst = map_reg(dst_vreg);
        if (src_vreg != ir::INVALID_SLOT && src_vreg != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(src_vreg));
        }
        if (dst != r10) {
            emit_reg_move(out, dst, r10);
        }
        if (dst_vreg != ir::INVALID_SLOT && dst_vreg != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(dst_vreg), r10);
        }
    }

    static void emit_store_stack(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out, const ir::IRStream& stream) {
        const ir::IRNode& node = selected.node;
        uint32_t dest_vreg = node.op_a;
        uint32_t src_vreg = node.op_b;
        if (dest_vreg == ir::INVALID_SLOT && src_vreg != ir::INVALID_SLOT) {
            dest_vreg = node.op_b;
            src_vreg = node.op_a;
        }

        const ir::TypeCode tc = stream.get_result_type_code(src_vreg);
        bool is_float = (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32);
        if (!is_float && src_vreg < stream.size()) {
            const auto& src_node = stream.get_node(src_vreg);
            if (src_node.opcode == ir::OpCode::ADD_F64 || src_node.opcode == ir::OpCode::SUB_F64 ||
                src_node.opcode == ir::OpCode::MUL_F64 || src_node.opcode == ir::OpCode::DIV_F64 ||
                src_node.opcode == ir::OpCode::ADD_F32 || src_node.opcode == ir::OpCode::SUB_F32 ||
                src_node.opcode == ir::OpCode::MUL_F32 || src_node.opcode == ir::OpCode::DIV_F32 ||
                (src_node.opcode == ir::OpCode::CONST && (src_node.flags & ir::Flags::IS_SPEC_CONST))) {
                is_float = true;
            }
        }

        if (is_float || tc == ir::TypeCode::F64) {
            const uint8_t xmm_src = map_xmm_reg(src_vreg);
            if (src_vreg != ir::INVALID_SLOT && src_vreg != 0u) {
                emit_movsd_xmm_from_stack(out, xmm_src, vreg_offset(src_vreg));
            }
            if (dest_vreg != ir::INVALID_SLOT && dest_vreg != 0u) {
                emit_movsd_stack_from_xmm(out, vreg_offset(dest_vreg), xmm_src);
            }
            return;
        }

        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t src = map_reg(src_vreg);
        if (src_vreg != ir::INVALID_SLOT && src_vreg != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(src_vreg));
        }
        if (src != r10) {
            emit_reg_move(out, src, r10);
        }
        if (dest_vreg != ir::INVALID_SLOT && dest_vreg != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(dest_vreg), r10);
        }
    }

    static void emit_select(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        const uint8_t dst = map_reg(node.result_id);

        // Load cond (node.op_a)
        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_a));
        } else {
            out.push_back(exec::X64Encoding::rex(/*w=*/true, false, false, requires_rex(r10)));
            out.push_back(0x31); out.push_back(static_cast<uint8_t>(0xC0u | ((r10 & 7u) << 3u) | (r10 & 7u)));
        }

        // Load false_val (node.op_c) into r11 as default
        if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) {
            emit_mov_reg_from_stack(out, r11, vreg_offset(node.op_c));
        } else {
            out.push_back(exec::X64Encoding::rex(/*w=*/true, false, false, requires_rex(r11)));
            out.push_back(0x31); out.push_back(static_cast<uint8_t>(0xC0u | ((r11 & 7u) << 3u) | (r11 & 7u)));
        }

        // TEST r10, r10
        out.push_back(exec::X64Encoding::rex(/*w=*/true, requires_rex(r10), false, requires_rex(r10)));
        out.push_back(0x85);
        emit_modrm_reg_reg(out, r10, r10);

        // If true_val (node.op_b) is present:
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_b));
            // CMOVNZ r11, r10
            out.push_back(exec::X64Encoding::rex(/*w=*/true, requires_rex(r11), false, requires_rex(r10)));
            out.push_back(0x0F);
            out.push_back(0x45);
            emit_modrm_reg_reg(out, r11, r10);
        }

        if (dst != r11) {
            emit_reg_move(out, dst, r11);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), r11);
        }
    }

    static void emit_gep(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        const uint8_t dst = map_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_a));
        } else {
            out.push_back(exec::X64Encoding::rex(/*w=*/true, false, false, requires_rex(r10)));
            out.push_back(0x31); out.push_back(static_cast<uint8_t>(0xC0u | ((r10 & 7u) << 3u) | (r10 & 7u)));
        }

        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_mov_reg_from_stack(out, r11, vreg_offset(node.op_b));
            // ADD r10, r11 (base + offset)
            out.push_back(exec::X64Encoding::rex(/*w=*/true, requires_rex(r11), false, requires_rex(r10)));
            out.push_back(0x01);
            emit_modrm_reg_reg(out, r11, r10);
        }

        if (dst != r10) {
            emit_reg_move(out, dst, r10);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), r10);
        }
    }

    static void emit_i32_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        const uint8_t dst = map_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_a));
        }
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_mov_reg_from_stack(out, r11, vreg_offset(node.op_b));
        }

        switch (original_opcode) {
            case ir::OpCode::ADD_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x01); // ADD r/m32, r32
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::SUB_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x29); // SUB r/m32, r32
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::MUL_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r10), /*x=*/false, /*b=*/requires_rex(r11)));
                out.push_back(0x0F);
                out.push_back(0xAF); // IMUL r32, r/m32
                emit_modrm_reg_reg(out, r10, r11);
                break;
            case ir::OpCode::AND_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x21); // AND r/m32, r32
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::OR_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x09); // OR r/m32, r32
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::XOR_I32:
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x31); // XOR r/m32, r32
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::SHL_I32: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                emit_reg_move32(out, count_reg, r11);
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0xD3); // SHL r/m32, CL
                emit_modrm_reg_reg(out, 0x04, r10);
                break;
            }
            case ir::OpCode::SHR_I32: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                emit_reg_move32(out, count_reg, r11);
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0xD3); // SAR r/m32, CL
                emit_modrm_reg_reg(out, 0x07, r10);
                break;
            }
            case ir::OpCode::DIV_I32:
            case ir::OpCode::MOD_I32: {
                const uint8_t eax = static_cast<uint8_t>(exec::X64Reg::RAX);
                const uint8_t edx = static_cast<uint8_t>(exec::X64Reg::RDX);
                emit_reg_move32(out, eax, r10);
                out.push_back(0x99); // CDQ
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r11)));
                out.push_back(0xF7);
                emit_modrm_reg_reg(out, 0x07, r11); // IDIV r/m32

                if (original_opcode == ir::OpCode::DIV_I32) {
                    emit_reg_move32(out, r10, eax);
                } else {
                    emit_reg_move32(out, r10, edx);
                }
                break;
            }
            default:
                out.push_back(0x90);
                break;
        }

        if (dst != r10) {
            emit_reg_move32(out, dst, r10);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), r10);
        }
    }

    static void emit_i64_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        const uint8_t dst = map_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_a));
        }
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_mov_reg_from_stack(out, r11, vreg_offset(node.op_b));
        }

        switch (original_opcode) {
            case ir::OpCode::ADD_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x01); // ADD r/m64, r64
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::SUB_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x29); // SUB r/m64, r64
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::AND_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x21); // AND r/m64, r64
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::OR_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x09); // OR r/m64, r64
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::XOR_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r11), /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0x31); // XOR r/m64, r64
                emit_modrm_reg_reg(out, r11, r10);
                break;
            case ir::OpCode::SHL_I64: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                emit_reg_move(out, count_reg, r11);
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0xD3); // SHL r/m64, CL
                emit_modrm_reg_reg(out, 0x04, r10);
                break;
            }
            case ir::OpCode::SHR_I64: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                emit_reg_move(out, count_reg, r11);
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r10)));
                out.push_back(0xD3); // SAR r/m64, CL
                emit_modrm_reg_reg(out, 0x07, r10);
                break;
            }
            case ir::OpCode::MUL_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(r10), /*x=*/false, /*b=*/requires_rex(r11)));
                out.push_back(0x0F);
                out.push_back(0xAF); // IMUL r64, r/m64
                emit_modrm_reg_reg(out, r10, r11);
                break;
            case ir::OpCode::DIV_I64:
            case ir::OpCode::MOD_I64: {
                const uint8_t eax = static_cast<uint8_t>(exec::X64Reg::RAX);
                const uint8_t edx = static_cast<uint8_t>(exec::X64Reg::RDX);
                emit_reg_move(out, eax, r10);
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
                out.push_back(0x99); // CQO
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(r11)));
                out.push_back(0xF7);
                emit_modrm_reg_reg(out, 0x07, r11); // IDIV r/m64

                if (original_opcode == ir::OpCode::DIV_I64) {
                    emit_reg_move(out, r10, eax);
                } else {
                    emit_reg_move(out, r10, edx);
                }
                break;
            }
            default:
                out.push_back(0x90);
                break;
        }

        if (dst != r10) {
            emit_reg_move(out, dst, r10);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), r10);
        }
    }

    static void emit_f32_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t xmm4 = 4;
        const uint8_t xmm5 = 5;
        const uint8_t dst = map_xmm_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_movsd_xmm_from_stack(out, xmm4, vreg_offset(node.op_a));
        }
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_movsd_xmm_from_stack(out, xmm5, vreg_offset(node.op_b));
        }

        out.push_back(0xF3); // Scalar FP32 prefix for ADDSS/SUBSS/MULSS/DIVSS
        out.push_back(0x0F);
        switch (original_opcode) {
            case ir::OpCode::ADD_F32: out.push_back(0x58); break; // ADDSS
            case ir::OpCode::SUB_F32: out.push_back(0x5C); break; // SUBSS
            case ir::OpCode::MUL_F32: out.push_back(0x59); break; // MULSS
            case ir::OpCode::DIV_F32: out.push_back(0x5E); break; // DIVSS
            default:
                out.push_back(0x90);
                return;
        }
        emit_modrm_reg_reg(out, xmm4, xmm5);

        if (dst != xmm4) {
            emit_xmm_move(out, dst, xmm4);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_movsd_stack_from_xmm(out, vreg_offset(node.result_id), xmm4);
        }
    }

    static void emit_f64_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t xmm4 = 4;
        const uint8_t xmm5 = 5;
        const uint8_t dst = map_xmm_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_movsd_xmm_from_stack(out, xmm4, vreg_offset(node.op_a));
        }
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_movsd_xmm_from_stack(out, xmm5, vreg_offset(node.op_b));
        }

        out.push_back(0xF2); // Scalar FP64 prefix
        out.push_back(0x0F);
        switch (original_opcode) {
            case ir::OpCode::ADD_F64: out.push_back(0x58); break; // ADDSD
            case ir::OpCode::SUB_F64: out.push_back(0x5C); break; // SUBSD
            case ir::OpCode::MUL_F64: out.push_back(0x59); break; // MULSD
            case ir::OpCode::DIV_F64: out.push_back(0x5E); break; // DIVSD
            default:
                out.push_back(0x90);
                return;
        }
        emit_modrm_reg_reg(out, xmm4, xmm5);

        if (dst != xmm4) {
            emit_xmm_move_f64(out, dst, xmm4);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_movsd_stack_from_xmm(out, vreg_offset(node.result_id), xmm4);
        }
    }

    static void emit_f32_fma(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t src_a = map_xmm_reg(node.op_a);
        const uint8_t src_b = map_xmm_reg(node.op_b);
        const uint8_t src_c = map_xmm_reg(node.op_c);
        const uint8_t dst = map_xmm_reg(node.result_id);

        if (dst != src_a) {
            emit_xmm_move(out, dst, src_a);
        }

        emit_vex3_prefix(out,
                         dst,
                         src_c,
                         src_b,
                         /*pp=*/0x01,
                         /*mmmmm=*/0x02,
                         /*w=*/false,
                         /*l=*/false);
        out.push_back(0x9A); // VFMADD132SS
        emit_modrm_reg_reg(out, dst, src_b);
    }

    static void emit_cmp_i32(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        const uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        const uint8_t dst = map_reg(node.result_id);

        if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
            emit_mov_reg_from_stack(out, r10, vreg_offset(node.op_a));
        }
        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            emit_mov_reg_from_stack(out, r11, vreg_offset(node.op_b));
        }

        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(r11),
                                             /*x=*/false,
                                             /*b=*/requires_rex(r10)));
        out.push_back(0x39); // CMP r/m64, r64 (r10 vs r11)
        emit_modrm_reg_reg(out, r11, r10);

        uint8_t cond = 0;
        switch (original_opcode) {
            case ir::OpCode::CMP_EQ_I32: cond = 0x94; break;
            case ir::OpCode::CMP_NE_I32: cond = 0x95; break;
            case ir::OpCode::CMP_LT_I32: cond = 0x9C; break;
            case ir::OpCode::CMP_LE_I32: cond = 0x9E; break;
            case ir::OpCode::CMP_GT_I32: cond = 0x9F; break;
            case ir::OpCode::CMP_GE_I32: cond = 0x9D; break;
            default:
                cond = 0x9E;
                break;
        }

        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(r10)));
        out.push_back(0x0F);
        out.push_back(cond);
        emit_modrm_reg_reg(out, 0x00, r10);

        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/requires_rex(r10),
                                             /*x=*/false,
                                             /*b=*/requires_rex(r10)));
        out.push_back(0x0F);
        out.push_back(0xB6); // MOVZX r32, r/m8
        emit_modrm_reg_reg(out, r10, r10);

        if (dst != r10) {
            emit_reg_move(out, dst, r10);
        }
        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            emit_mov_stack_from_reg(out, vreg_offset(node.result_id), r10);
        }
    }

    static void emit_vec_binary512(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        emit_zmm_move(out, selected.node.result_id, selected.node.op_a);
    }

    static void emit_vec_gather512(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        emit_zmm_move(out, selected.node.result_id, selected.node.op_a);
    }

    static void emit_vec_store512(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        emit_zmm_move(out, selected.node.op_a, selected.node.op_b);
    }

    static void emit_goto(const lowering::SelectedInstruction& selected,
                          std::vector<uint8_t>& out,
                          std::vector<PendingBranchPatch>& patches,
                          const std::unordered_map<uint32_t, size_t>& /*label_to_node_index*/) {
        const ir::IRNode& node = selected.node;
        const uint32_t target_label = (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) ? node.op_a : node.result_id;

        out.push_back(0xE9); // JMP rel32
        const size_t disp_offset = out.size();
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        patches.push_back(PendingBranchPatch{disp_offset, out.size(), target_label});
    }

    static void emit_branch_cond(const lowering::SelectedInstruction& selected,
                                 std::vector<uint8_t>& out,
                                 std::vector<PendingBranchPatch>& patches,
                                 const std::unordered_map<uint32_t, size_t>& /*label_to_node_index*/) {
        const ir::IRNode& node = selected.node;
        const uint32_t cond_vreg = node.op_a;
        const uint32_t target_label = (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) ? node.op_b : node.result_id;

        const uint8_t cond = map_reg(cond_vreg);
        if (cond_vreg != ir::INVALID_SLOT && cond_vreg != 0u) {
            emit_mov_reg_from_stack(out, cond, vreg_offset(cond_vreg));
        }

        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(cond),
                                             /*x=*/false,
                                             /*b=*/requires_rex(cond)));
        out.push_back(0x85); // TEST r/m64, r64
        emit_modrm_reg_reg(out, cond, cond);

        out.push_back(0x0F);
        out.push_back(node.opcode == ir::OpCode::BRANCH_UNLIKELY ? 0x84 : 0x85); // JNZ / JZ rel32
        const size_t disp_offset = out.size();
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        patches.push_back(PendingBranchPatch{disp_offset, out.size(), target_label});
    }

    static void emit_branch_compare_fused(const lowering::SelectedInstruction& selected,
                                          std::vector<uint8_t>& out,
                                          std::vector<PendingBranchPatch>& patches,
                                          const std::unordered_map<uint32_t, size_t>& /*label_to_node_index*/) {
        const ir::IRNode& node = selected.node;
        const uint32_t lhs_vreg = node.op_a;
        const uint32_t rhs_vreg = node.op_b;
        const uint32_t target_label = node.op_c;

        const uint8_t lhs = map_reg(lhs_vreg);
        const uint8_t rhs = map_reg(rhs_vreg);

        if (lhs_vreg != ir::INVALID_SLOT && lhs_vreg != 0u) {
            emit_mov_reg_from_stack(out, lhs, vreg_offset(lhs_vreg));
        }
        if (rhs_vreg != ir::INVALID_SLOT && rhs_vreg != 0u) {
            emit_mov_reg_from_stack(out, rhs, vreg_offset(rhs_vreg));
        }

        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/requires_rex(rhs),
                                             /*x=*/false,
                                             /*b=*/requires_rex(lhs)));
        out.push_back(0x39); // CMP r/m32, r32
        emit_modrm_reg_reg(out, rhs, lhs);

        out.push_back(0x0F);
        out.push_back(0x85); // JNZ rel32
        const size_t disp_offset = out.size();
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        patches.push_back(PendingBranchPatch{disp_offset, out.size(), target_label});
    }

    static void emit_syscall(const lowering::SelectedInstruction&, std::vector<uint8_t>& out) {
        out.push_back(0x0F);
        out.push_back(0x05); // SYSCALL
    }

    [[nodiscard]] uintptr_t resolve_runtime_function(std::string_view name) const noexcept {
        const auto it = runtime_functions_.find(std::string(name));
        if (it == runtime_functions_.end()) {
            return 0;
        }
        return static_cast<uintptr_t>(it->second);
    }

    [[nodiscard]] static const char* runtime_symbol_for_opcode(ir::OpCode opcode) noexcept {
        return dgtl::isa::lowering::runtime_contract::runtime_symbol_for_opcode(opcode);
    }

    void emit_call_address(uintptr_t address, const ir::IRNode& node, size_t node_index, std::vector<uint8_t>& out, const ir::IRStream& stream) const {
#if defined(_WIN32)
        const auto overflow = stream.get_call_overflow_args(node_index);
        const size_t num_stack_args = (overflow.size() > 1) ? (overflow.size() - 1) : 0;
        const size_t stack_alloc = 32 + ((num_stack_args + 1) / 2) * 16;
        emit_sub_rsp(out, static_cast<uint32_t>(stack_alloc));

        for (size_t k = 1; k < overflow.size(); ++k) {
            if (overflow[k] != ir::INVALID_SLOT && overflow[k] != 0u) {
                const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
                emit_mov_reg_from_stack(out, r10, static_cast<int32_t>(vreg_offset(overflow[k]) + stack_alloc));
                emit_mov_stack_from_reg(out, static_cast<int32_t>(32 + (k - 1) * 8), r10);
            }
        }
#endif

        // movabs rax, imm64
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
        out.push_back(0xB8);
        append_u64(out, static_cast<uint64_t>(address));

        // call rax
        out.push_back(0xFF);
        out.push_back(0xD0);

#if defined(_WIN32)
        emit_add_rsp(out, static_cast<uint32_t>(stack_alloc));
#endif

        if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
            const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
            if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                const uint8_t dst_xmm = map_xmm_reg(node.result_id);
                if (dst_xmm != 0) {
                    emit_xmm_move_f64(out, dst_xmm, 0);
                }
                emit_movsd_stack_from_xmm(out, vreg_offset(node.result_id), 0);
                emit_movq_gpr_xmm(out, static_cast<uint8_t>(exec::X64Reg::RAX), 0);
                emit_mov_stack_from_reg(out, vreg_offset(node.result_id), static_cast<uint8_t>(exec::X64Reg::RAX));
            } else {
                const uint8_t dst = map_reg(node.result_id);
                if (dst != static_cast<uint8_t>(exec::X64Reg::RAX)) {
                    emit_reg_move(out, dst, static_cast<uint8_t>(exec::X64Reg::RAX));
                }
                emit_mov_stack_from_reg(out, vreg_offset(node.result_id), static_cast<uint8_t>(exec::X64Reg::RAX));
            }
        }
    }

    static bool emit_call_fastpath(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        switch (original_opcode) {
            case ir::OpCode::ACQUIRE_FENCE:
                out.push_back(0x0F); out.push_back(0xAE); out.push_back(0xE8); // LFENCE
                return true;
            case ir::OpCode::RELEASE_FENCE:
                out.push_back(0x0F); out.push_back(0xAE); out.push_back(0xF8); // SFENCE
                return true;
            case ir::OpCode::ATOMIC_FENCE:
            case ir::OpCode::MEMORY_FENCE:
            case ir::OpCode::BARRIER_SYNC:
            case ir::OpCode::SYNC_BARRIER:
                out.push_back(0x0F); out.push_back(0xAE); out.push_back(0xF0); // MFENCE
                return true;
            default:
                return false;
        }
    }

    void emit_call_slot(const lowering::SelectedInstruction& selected,
                        size_t node_index,
                        std::vector<uint8_t>& out,
                        std::vector<PendingBranchPatch>& patches,
                        const std::unordered_map<uint32_t, size_t>& label_to_node_index,
                        const ir::IRStream& stream) const {
        if (emit_call_fastpath(selected, out)) {
            return;
        }

        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);

        if (original_opcode == ir::OpCode::STRING_LENGTH ||
            original_opcode == ir::OpCode::STRING_COMPARE ||
            original_opcode == ir::OpCode::STRING_CONCAT ||
            original_opcode == ir::OpCode::STRING_SUBSTRING) {
            const char* symbol = nullptr;
            switch (original_opcode) {
                case ir::OpCode::STRING_LENGTH: symbol = "__dgtl_rt_string_length"; break;
                case ir::OpCode::STRING_COMPARE: symbol = "__dgtl_rt_string_compare"; break;
                case ir::OpCode::STRING_CONCAT: symbol = "__dgtl_rt_string_concat"; break;
                case ir::OpCode::STRING_SUBSTRING: symbol = "__dgtl_rt_string_substring"; break;
                default: break;
            }

            const uintptr_t target = (symbol != nullptr) ? resolve_runtime_function(symbol) : 0;
            if (target == 0) {
                mark_error("Missing runtime string intrinsic registration", original_opcode);
                out.push_back(0x90);
                return;
            }

            const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
            const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
            const uint8_t r8 = static_cast<uint8_t>(exec::X64Reg::R8);

            if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) emit_mov_reg_from_stack(out, rcx, vreg_offset(node.op_a));
            if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) emit_mov_reg_from_stack(out, rdx, vreg_offset(node.op_b));
            if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) emit_mov_reg_from_stack(out, r8, vreg_offset(node.op_c));

            emit_call_address(target, node, node_index, out, stream);
            return;
        }

        if (const char* symbol = runtime_symbol_for_opcode(original_opcode); symbol != nullptr) {
            const uintptr_t target = resolve_runtime_function(symbol);
            if (target == 0) {
                mark_error("Missing runtime intrinsic registration", original_opcode);
                out.push_back(0x90);
                return;
            }

            const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
            const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
            const uint8_t r8 = static_cast<uint8_t>(exec::X64Reg::R8);

            if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) emit_mov_reg_from_stack(out, rcx, vreg_offset(node.op_a));
            if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) emit_mov_reg_from_stack(out, rdx, vreg_offset(node.op_b));
            if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) emit_mov_reg_from_stack(out, r8, vreg_offset(node.op_c));

            emit_call_address(target, node, node_index, out, stream);
            return;
        }

        const auto it = runtime_slots_.find(node.op_a);
        if (it == runtime_slots_.end() || it->second == 0) {
            const uint32_t target_label = node.op_a;

#if defined(_WIN32)
            const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
            const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
            const uint8_t r8  = static_cast<uint8_t>(exec::X64Reg::R8);
            const uint8_t r9  = static_cast<uint8_t>(exec::X64Reg::R9);

            if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                const ir::TypeCode tc = stream.get_result_type_code(node.op_b);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movsd_xmm_from_stack(out, 0, vreg_offset(node.op_b));
                    emit_movq_gpr_xmm(out, rcx, 0);
                } else {
                    emit_mov_reg_from_stack(out, rcx, vreg_offset(node.op_b));
                    emit_movsd_xmm_from_stack(out, 0, vreg_offset(node.op_b));
                }
            }
            if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) {
                const ir::TypeCode tc = stream.get_result_type_code(node.op_c);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movsd_xmm_from_stack(out, 1, vreg_offset(node.op_c));
                    emit_movq_gpr_xmm(out, rdx, 1);
                } else {
                    emit_mov_reg_from_stack(out, rdx, vreg_offset(node.op_c));
                    emit_movsd_xmm_from_stack(out, 1, vreg_offset(node.op_c));
                }
            }
            if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
                const ir::TypeCode tc = stream.get_result_type_code(node.logic_flags);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movsd_xmm_from_stack(out, 2, vreg_offset(node.logic_flags));
                    emit_movq_gpr_xmm(out, r8, 2);
                } else {
                    emit_mov_reg_from_stack(out, r8, vreg_offset(node.logic_flags));
                    emit_movsd_xmm_from_stack(out, 2, vreg_offset(node.logic_flags));
                }
            }
            const auto overflow = stream.get_call_overflow_args(node_index);
            if (!overflow.empty() && overflow[0] != ir::INVALID_SLOT && overflow[0] != 0u) {
                const ir::TypeCode tc = stream.get_result_type_code(overflow[0]);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movsd_xmm_from_stack(out, 3, vreg_offset(overflow[0]));
                    emit_movq_gpr_xmm(out, r9, 3);
                } else {
                    emit_mov_reg_from_stack(out, r9, vreg_offset(overflow[0]));
                    emit_movsd_xmm_from_stack(out, 3, vreg_offset(overflow[0]));
                }
            } else {
                out.push_back(0x4D); out.push_back(0x31); out.push_back(0xC9); // xor r9, r9
            }
            const size_t num_stack_args = (overflow.size() > 1) ? (overflow.size() - 1) : 0;
            const size_t stack_alloc = 32 + ((num_stack_args + 1) / 2) * 16;
            emit_sub_rsp(out, static_cast<uint32_t>(stack_alloc));

            for (size_t k = 1; k < overflow.size(); ++k) {
                if (overflow[k] != ir::INVALID_SLOT && overflow[k] != 0u) {
                    const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
                    emit_mov_reg_from_stack(out, r10, static_cast<int32_t>(vreg_offset(overflow[k]) + stack_alloc));
                    emit_mov_stack_from_reg(out, static_cast<int32_t>(32 + (k - 1) * 8), r10);
                }
            }
#endif
            out.push_back(0xE8); // CALL rel32
            const size_t disp_offset = out.size();
            out.push_back(0x00);
            out.push_back(0x00);
            out.push_back(0x00);
            out.push_back(0x00);
            const size_t instr_end = out.size();
#if defined(_WIN32)
            emit_add_rsp(out, static_cast<uint32_t>(stack_alloc));
#endif
            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    const uint8_t dst_xmm = map_xmm_reg(node.result_id);
                    if (dst_xmm != 0) {
                        emit_xmm_move_f64(out, dst_xmm, 0);
                    }
                    emit_movsd_stack_from_xmm(out, vreg_offset(node.result_id), 0);
                    emit_movq_gpr_xmm(out, static_cast<uint8_t>(exec::X64Reg::RAX), 0);
                    emit_mov_stack_from_reg(out, vreg_offset(node.result_id), static_cast<uint8_t>(exec::X64Reg::RAX));
                } else {
                    const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
                    const uint8_t dst = map_reg(node.result_id);
                    if (dst != rax) emit_reg_move(out, dst, rax);
                    emit_mov_stack_from_reg(out, vreg_offset(node.result_id), rax);
                }
            }
            patches.push_back(PendingBranchPatch{disp_offset, instr_end, target_label});
            return;
        }

#if defined(_WIN32)
        const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
        const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
        const uint8_t r8  = static_cast<uint8_t>(exec::X64Reg::R8);
        const uint8_t r9  = static_cast<uint8_t>(exec::X64Reg::R9);

        std::string callee_name;
        for (const auto& [name, addr] : runtime_functions_) {
            if (addr == it->second) {
                callee_name = name;
                break;
            }
        }

        const bool is_gpr_float_helper = (!callee_name.empty() && 
            (callee_name == "__myzon_async_push_f64_bits" ||
             callee_name == "__myzon_async_push_f32_bits" ||
             callee_name == "__myzon_async_push_c64_bits" ||
             callee_name == "__myzon_f64_add_bits" ||
             callee_name == "__myzon_f64_sub_bits" ||
             callee_name == "__myzon_f64_mul_bits" ||
             callee_name == "__myzon_f64_div_bits" ||
             callee_name == "__myzon_f64_pow_bits" ||
             callee_name == "__myzon_i64_to_f64_bits"));

        auto is_float_slot = [&](uint32_t slot) -> bool {
            if (slot == ir::INVALID_SLOT || slot == 0u) return false;
            const ir::TypeCode tc = stream.get_result_type_code(slot);
            if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) return true;
            if (slot < stream.size()) {
                const auto& src_node = stream.get_node(slot);
                if (src_node.opcode == ir::OpCode::ADD_F64 || src_node.opcode == ir::OpCode::SUB_F64 ||
                    src_node.opcode == ir::OpCode::MUL_F64 || src_node.opcode == ir::OpCode::DIV_F64 ||
                    src_node.opcode == ir::OpCode::ADD_F32 || src_node.opcode == ir::OpCode::SUB_F32 ||
                    src_node.opcode == ir::OpCode::MUL_F32 || src_node.opcode == ir::OpCode::DIV_F32 ||
                    (src_node.opcode == ir::OpCode::CONST && (src_node.flags & ir::Flags::IS_SPEC_CONST))) {
                    return true;
                }
            }
            return false;
        };

        if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
            if (is_gpr_float_helper && is_float_slot(node.op_b)) {
                emit_movsd_xmm_from_stack(out, 0, vreg_offset(node.op_b));
                emit_movq_gpr_xmm(out, rcx, 0);
            } else if (is_float_slot(node.op_b)) {
                emit_movsd_xmm_from_stack(out, 0, vreg_offset(node.op_b));
                emit_mov_reg_from_stack(out, rcx, vreg_offset(node.op_b));
            } else {
                emit_mov_reg_from_stack(out, rcx, vreg_offset(node.op_b));
            }
        } else {
            out.push_back(0x48); out.push_back(0x31); out.push_back(0xC9); // xor rcx, rcx
        }
        if (node.op_c != ir::INVALID_SLOT && node.op_c != 0u) {
            if (is_gpr_float_helper && is_float_slot(node.op_c)) {
                emit_movsd_xmm_from_stack(out, 1, vreg_offset(node.op_c));
                emit_movq_gpr_xmm(out, rdx, 1);
            } else if (is_float_slot(node.op_c)) {
                emit_movsd_xmm_from_stack(out, 1, vreg_offset(node.op_c));
                emit_mov_reg_from_stack(out, rdx, vreg_offset(node.op_c));
            } else {
                emit_mov_reg_from_stack(out, rdx, vreg_offset(node.op_c));
            }
        } else {
            out.push_back(0x48); out.push_back(0x31); out.push_back(0xD2); // xor rdx, rdx
        }
        if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
            if (is_gpr_float_helper && is_float_slot(node.logic_flags)) {
                emit_movsd_xmm_from_stack(out, 2, vreg_offset(node.logic_flags));
                emit_movq_gpr_xmm(out, r8, 2);
            } else if (is_float_slot(node.logic_flags)) {
                emit_movsd_xmm_from_stack(out, 2, vreg_offset(node.logic_flags));
                emit_mov_reg_from_stack(out, r8, vreg_offset(node.logic_flags));
            } else {
                emit_mov_reg_from_stack(out, r8, vreg_offset(node.logic_flags));
            }
        } else {
            out.push_back(0x4D); out.push_back(0x31); out.push_back(0xC0); // xor r8, r8
        }

        const auto overflow_args = stream.get_call_overflow_args(node_index);
        if (!overflow_args.empty() && overflow_args[0] != ir::INVALID_SLOT && overflow_args[0] != 0u) {
            if (is_gpr_float_helper && is_float_slot(overflow_args[0])) {
                emit_movsd_xmm_from_stack(out, 3, vreg_offset(overflow_args[0]));
                emit_movq_gpr_xmm(out, r9, 3);
            } else if (is_float_slot(overflow_args[0])) {
                emit_movsd_xmm_from_stack(out, 3, vreg_offset(overflow_args[0]));
                emit_mov_reg_from_stack(out, r9, vreg_offset(overflow_args[0]));
            } else {
                emit_mov_reg_from_stack(out, r9, vreg_offset(overflow_args[0]));
            }
        } else {
            out.push_back(0x4D); out.push_back(0x31); out.push_back(0xC9); // xor r9, r9
        }

        if (!is_gpr_float_helper && !callee_name.empty() && 
            (callee_name.find("push_f64") != std::string::npos || 
             callee_name.find("push_f32") != std::string::npos || 
             callee_name.find("push_float") != std::string::npos ||
             callee_name.find("_f64_") != std::string::npos ||
             callee_name.find("_f32_") != std::string::npos ||
             callee_name.find("_to_f64") != std::string::npos)) {
            out.push_back(0x66);
            out.push_back(0x48);
            out.push_back(0x0F);
            out.push_back(0x6E);
            out.push_back(0xC1); // movq xmm0, rcx
            if (callee_name.find("_add_") != std::string::npos ||
                callee_name.find("_sub_") != std::string::npos ||
                callee_name.find("_mul_") != std::string::npos ||
                callee_name.find("_div_") != std::string::npos ||
                callee_name.find("_pow_") != std::string::npos) {
                out.push_back(0x66);
                out.push_back(0x48);
                out.push_back(0x0F);
                out.push_back(0x6E);
                out.push_back(0xCA); // movq xmm1, rdx
            }
        }
#endif

        emit_call_address(static_cast<uintptr_t>(it->second), node, node_index, out, stream);
    }

    void mark_error(const char* msg, ir::OpCode opcode) const {
        if (!had_error_) [[unlikely]] {
            had_error_ = true;
            last_error_ = msg;
            last_failed_opcode_ = opcode;
        }
    }

private:
    core::TargetMachine target_machine_;
    mutable lowering::MachineLegalizer legalizer_{};
    lowering::InstructionSelectorProgram selector_;
    std::unordered_map<std::string, uintptr_t> runtime_functions_;
    std::unordered_map<uint32_t, uintptr_t> runtime_slots_;
    mutable std::unordered_map<uint32_t, size_t> label_offsets_;
    mutable bool had_error_ = false;
    mutable std::string last_error_;
    mutable ir::OpCode last_failed_opcode_ = ir::OpCode::NOP;
};

} // namespace dgtl::isa