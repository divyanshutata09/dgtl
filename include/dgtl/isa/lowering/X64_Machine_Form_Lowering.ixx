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

        EncoderFunc add_f32;
        EncoderFunc mul_f32;

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
            case generated::X64FloatBinary32Kind::Mul:
                callbacks_.mul_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::FloatBinary32 opcode",
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
                if (n.op_a != ir::INVALID_SLOT && n.op_a != 0u) {
                    label_to_node_index[n.op_a] = i;
                }
                if (n.op_b != ir::INVALID_SLOT && n.op_b != 0u) {
                    label_to_node_index[n.op_b] = i;
                }
                label_to_node_index[static_cast<uint32_t>(i)] = i;
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
                if (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) {
                    label_offsets_[node.op_a] = byte_offset;
                }
                if (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) {
                    label_offsets_[node.op_b] = byte_offset;
                }
            }
            const auto selected = selector_.select_instruction(machine_stream, i, node, target_machine_);

            switch (selected.form) {
                case lowering::MachineForm::Move:
                    emit_move(selected, out);
                    break;
                case lowering::MachineForm::LoadImmediate:
                    if (node.opcode == ir::OpCode::CONST && (node.flags & ir::Flags::IS_SPEC_CONST)) {
                        emit_load_imm_f64(selected, out);
                    } else {
                        emit_load_imm(selected, out);
                    }
                    break;
                case lowering::MachineForm::LoadStack:
                    emit_load_stack(selected, out);
                    break;
                case lowering::MachineForm::StoreStack:
                    emit_store_stack(selected, out);
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
                    emit_call_slot(selected, out, branch_patches, label_to_node_index, machine_stream);
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
                case lowering::MachineForm::Return:
                    out.push_back(0xC3);
                    break;
                case lowering::MachineForm::NoOp:
                    if (node.opcode == ir::OpCode::LABEL &&
                        (node.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE))) {
                        emit_function_prologue_params(node, out, machine_stream);
                    }
                    break;
                case lowering::MachineForm::Unhandled:
                default:
                    mark_error("Unhandled machine form selected by ISel tables", selected.node.opcode);
                    out.push_back(0x90); 
                    break;
            }
        }

        for (const PendingBranchPatch& patch : branch_patches) {
            size_t target_node_index = machine_stream.size();
            const auto label_it = label_to_node_index.find(patch.target_label);
            if (label_it != label_to_node_index.end()) {
                target_node_index = label_it->second;
            } else {
                for (size_t k = 0; k < machine_stream.size(); ++k) {
                    const auto& knode = machine_stream.get_node(k);
                    if (knode.opcode == ir::OpCode::LABEL) {
                        if (knode.result_id == patch.target_label || knode.op_a == patch.target_label || knode.op_b == patch.target_label) {
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

    void emit_function_prologue_params(const ir::IRNode& node, std::vector<uint8_t>& out, const ir::IRStream& stream) const {
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
            if (param_slot == ir::INVALID_SLOT || param_slot == 0u || param_slot == node.result_id) {
                continue;
            }

            const ir::TypeCode tc = stream.get_result_type_code(param_slot);
            if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                const uint8_t dst_xmm = map_xmm_reg(param_slot);
                const uint8_t src_xmm = static_cast<uint8_t>(i); // XMM0..XMM3 match parameter index i
                if (dst_xmm != src_xmm) {
                    emit_xmm_move_f64(out, dst_xmm, src_xmm);
                }
            } else {
                const uint8_t dst_gpr = map_reg(param_slot);
                const uint8_t src_gpr = gpr_params[i];
                if (dst_gpr != src_gpr) {
                    emit_reg_move(out, dst_gpr, src_gpr);
                }
            }
        }
    }

    static void emit_move(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t src = map_reg(node.op_a);
        const uint8_t dst = map_reg(node.result_id);

        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/(src >= 8u), /*x=*/false, /*b=*/(dst >= 8u)));
        out.push_back(0x89); // MOV r/m64, r64
        emit_modrm_reg_reg(out, src, dst);
    }

    static constexpr bool requires_rex(uint8_t reg) noexcept {
        return reg >= 8u;
    }

    static uint8_t map_xmm_reg(uint32_t vreg) noexcept {
        return static_cast<uint8_t>(exec::X64Encoding::map_DGTL_to_phys(vreg));
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

    static void emit_load_stack(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t dst = map_reg(node.result_id);
        const uint8_t base = map_reg(node.op_a);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(dst),
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x8B); // MOV r64, [base]
        const uint8_t base_lo = base & 0x7u;
        if (base_lo == 0x4u) { // RSP / R12
            out.push_back(static_cast<uint8_t>(0x44u | ((dst & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(0x00u);
        } else if (base_lo == 0x5u) { // RBP / R13
            out.push_back(static_cast<uint8_t>(0x45u | ((dst & 0x7u) << 3u)));
            out.push_back(0x00u);
        } else {
            out.push_back(static_cast<uint8_t>(((dst & 0x7u) << 3u) | base_lo));
        }
    }

    static void emit_store_stack(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        uint32_t base_vreg = node.op_a;
        uint32_t src_vreg = node.op_b;
        if (base_vreg == ir::INVALID_SLOT && src_vreg != ir::INVALID_SLOT) {
            base_vreg = node.op_b;
            src_vreg = node.op_a;
        }

        const uint8_t src = map_reg(src_vreg);
        const uint8_t base = map_reg(base_vreg);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x89); // MOV [base], src
        const uint8_t base_lo = base & 0x7u;
        if (base_lo == 0x4u) {
            out.push_back(static_cast<uint8_t>(0x44u | ((src & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(0x00u);
        } else if (base_lo == 0x5u) {
            out.push_back(static_cast<uint8_t>(0x45u | ((src & 0x7u) << 3u)));
            out.push_back(0x00u);
        } else {
            out.push_back(static_cast<uint8_t>(((src & 0x7u) << 3u) | base_lo));
        }
    }

    static void emit_i32_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t src_a = map_reg(node.op_a);
        const uint8_t src_b = map_reg(node.op_b);
        const uint8_t dst = map_reg(node.result_id);

        if (dst != src_a) {
            emit_reg_move32(out, dst, src_a);
        }

        const bool rex_r = requires_rex(src_b);
        const bool rex_b = requires_rex(dst);
        out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));

        switch (original_opcode) {
            case ir::OpCode::ADD_I32:
                out.push_back(0x01); // ADD r/m32, r32
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::SUB_I32:
                out.push_back(0x29); // SUB r/m32, r32
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::MUL_I32:
                out.push_back(0x0F);
                out.push_back(0xAF); // IMUL r32, r/m32
                emit_modrm_reg_reg(out, dst, src_b);
                break;
            case ir::OpCode::AND_I32:
                out.push_back(0x21); // AND r/m32, r32
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::OR_I32:
                out.push_back(0x09); // OR r/m32, r32
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::XOR_I32:
                out.push_back(0x31); // XOR r/m32, r32
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::SHL_I32: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                if (src_b != count_reg) {
                    emit_reg_move32(out, count_reg, src_b);
                }
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(dst)));
                out.push_back(0xD3); // SHL r/m32, CL
                emit_modrm_reg_reg(out, 0x04, dst);
                break;
            }
            case ir::OpCode::SHR_I32: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                if (src_b != count_reg) {
                    emit_reg_move32(out, count_reg, src_b);
                }
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(dst)));
                out.push_back(0xD3); // SHR r/m32, CL
                emit_modrm_reg_reg(out, 0x05, dst);
                break;
            }
            case ir::OpCode::DIV_I32:
            case ir::OpCode::MOD_I32: {
                const uint8_t eax = static_cast<uint8_t>(exec::X64Reg::RAX);
                const uint8_t edx = static_cast<uint8_t>(exec::X64Reg::RDX);
                const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
                uint8_t effective_divisor = src_b;
                if (src_b == eax || src_b == edx) {
                    emit_reg_move32(out, r10, src_b);
                    effective_divisor = r10;
                }
                if (src_a != eax) {
                    emit_reg_move32(out, eax, src_a);
                }
                out.push_back(0x99); // CDQ
                out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/false, /*x=*/false, /*b=*/requires_rex(effective_divisor)));
                out.push_back(0xF7);
                emit_modrm_reg_reg(out, 0x07, effective_divisor); // IDIV r/m32

                if (original_opcode == ir::OpCode::DIV_I32) {
                    if (dst != eax) {
                        emit_reg_move32(out, dst, eax);
                    }
                } else {
                    if (dst != edx) {
                        emit_reg_move32(out, dst, edx);
                    }
                }
                break;
            }
            default:
                out.push_back(0x90);
                break;
        }
    }

    static void emit_i64_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t src_a = map_reg(node.op_a);
        const uint8_t src_b = map_reg(node.op_b);
        const uint8_t dst = map_reg(node.result_id);

        if (dst != src_a) {
            emit_reg_move(out, dst, src_a);
        }

        const bool rex_r = requires_rex(src_b);
        const bool rex_b = requires_rex(dst);
        switch (original_opcode) {
            case ir::OpCode::ADD_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));
                out.push_back(0x01); // ADD r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::SUB_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));
                out.push_back(0x29); // SUB r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::AND_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));
                out.push_back(0x21); // AND r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::OR_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));
                out.push_back(0x09); // OR r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::XOR_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/rex_r, /*x=*/false, /*b=*/rex_b));
                out.push_back(0x31); // XOR r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                break;
            case ir::OpCode::SHL_I64: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                if (src_b != count_reg) {
                    emit_reg_move(out, count_reg, src_b);
                }
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(dst)));
                out.push_back(0xD3); // SHL r/m64, CL
                emit_modrm_reg_reg(out, 0x04, dst);
                break;
            }
            case ir::OpCode::SHR_I64: {
                const uint8_t count_reg = static_cast<uint8_t>(exec::X64Reg::RCX);
                if (src_b != count_reg) {
                    emit_reg_move(out, count_reg, src_b);
                }
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(dst)));
                out.push_back(0xD3); // SHR r/m64, CL
                emit_modrm_reg_reg(out, 0x05, dst);
                break;
            }
            case ir::OpCode::MUL_I64:
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/requires_rex(dst), /*x=*/false, /*b=*/rex_b));
                out.push_back(0x0F);
                out.push_back(0xAF); // IMUL r64, r/m64
                emit_modrm_reg_reg(out, dst, src_b);
                break;
            case ir::OpCode::DIV_I64:
            case ir::OpCode::MOD_I64: {
                const uint8_t eax = static_cast<uint8_t>(exec::X64Reg::RAX);
                const uint8_t edx = static_cast<uint8_t>(exec::X64Reg::RDX);
                const uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
                uint8_t effective_divisor = src_b;
                if (src_b == eax || src_b == edx) {
                    emit_reg_move(out, r10, src_b);
                    effective_divisor = r10;
                }
                if (src_a != eax) {
                    emit_reg_move(out, eax, src_a);
                }
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
                out.push_back(0x99); // CQO
                out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/requires_rex(effective_divisor)));
                out.push_back(0xF7);
                emit_modrm_reg_reg(out, 0x07, effective_divisor); // IDIV r/m64

                if (original_opcode == ir::OpCode::DIV_I64) {
                    if (dst != eax) {
                        emit_reg_move(out, dst, eax);
                    }
                } else {
                    if (dst != edx) {
                        emit_reg_move(out, dst, edx);
                    }
                }
                break;
            }
            default:
                out.push_back(0x90);
                break;
        }
    }

    static void emit_f32_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t src_a = map_xmm_reg(node.op_a);
        const uint8_t src_b = map_xmm_reg(node.op_b);
        const uint8_t dst = map_xmm_reg(node.result_id);

        if (dst != src_a) {
            emit_xmm_move(out, dst, src_a);
        }

        out.push_back(0xF3); // Scalar FP32 prefix for ADDSS/SUBSS/MULSS/DIVSS
        if (requires_rex(dst) || requires_rex(src_b)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src_b)));
        }
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
        emit_modrm_reg_reg(out, dst, src_b);
    }

    static void emit_f64_binary(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const auto original_opcode = static_cast<ir::OpCode>(selected.semantic_opcode);
        const uint8_t src_a = map_xmm_reg(node.op_a);
        const uint8_t src_b = map_xmm_reg(node.op_b);
        const uint8_t dst = map_xmm_reg(node.result_id);

        if (dst != src_a) {
            emit_xmm_move_f64(out, dst, src_a);
        }

        out.push_back(0xF2); // Scalar FP64 prefix
        if (requires_rex(dst) || requires_rex(src_b)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src_b)));
        }
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
        emit_modrm_reg_reg(out, dst, src_b);
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
        const uint8_t lhs = map_reg(node.op_a);
        const uint8_t rhs = map_reg(node.op_b);
        const uint8_t dst = map_reg(node.result_id);

        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(rhs),
                                             /*x=*/false,
                                             /*b=*/requires_rex(lhs)));
        out.push_back(0x39); // CMP r/m64, r64
        emit_modrm_reg_reg(out, rhs, lhs);

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
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x0F);
        out.push_back(cond);
        emit_modrm_reg_reg(out, 0x00, dst);

        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/requires_rex(dst),
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x0F);
        out.push_back(0xB6); // MOVZX r32, r/m8
        emit_modrm_reg_reg(out, dst, dst);
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

    struct PendingBranchPatch {
        size_t displacement_offset = 0;
        size_t instruction_end_offset = 0;
        uint32_t target_label = ir::INVALID_SLOT;
    };

    static void emit_goto(const lowering::SelectedInstruction& selected,
                          std::vector<uint8_t>& out,
                          std::vector<PendingBranchPatch>& patches,
                          const std::unordered_map<uint32_t, size_t>& label_to_node_index) {
        const ir::IRNode& node = selected.node;
        const auto target_opt = pick_known_label(label_to_node_index, {node.op_a, node.result_id, node.op_b});
        const uint32_t target_label = target_opt.value_or(node.op_a != ir::INVALID_SLOT && node.op_a != 0u ? node.op_a : node.result_id);

        out.push_back(0xE9); // JMP rel32
        const size_t disp_offset = out.size();
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0x00);
        patches.push_back(PendingBranchPatch{disp_offset, out.size(), target_label});
    }

    static std::optional<uint32_t> pick_known_label(const std::unordered_map<uint32_t, size_t>& label_to_node_index,
                                                    std::initializer_list<uint32_t> candidates) {
        for (const uint32_t candidate : candidates) {
            if (candidate == ir::INVALID_SLOT || candidate == 0u) {
                continue;
            }
            if (label_to_node_index.find(candidate) != label_to_node_index.end()) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    static void emit_branch_cond(const lowering::SelectedInstruction& selected,
                                 std::vector<uint8_t>& out,
                                 std::vector<PendingBranchPatch>& patches,
                                 const std::unordered_map<uint32_t, size_t>& label_to_node_index) {
        const ir::IRNode& node = selected.node;
        const uint32_t cond_vreg = node.op_a;
        const auto target_opt = pick_known_label(label_to_node_index, {node.op_b, node.result_id, node.op_a});
        const uint32_t target_label = target_opt.value_or(node.op_b != ir::INVALID_SLOT && node.op_b != 0u ? node.op_b : node.result_id);

        const uint8_t cond = map_reg(cond_vreg);

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

    static void emit_syscall(const lowering::SelectedInstruction& selected, std::vector<uint8_t>& out) {
        const ir::IRNode& node = selected.node;
        const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
        const uint8_t rdi = static_cast<uint8_t>(exec::X64Reg::RDI);
        const uint8_t rsi = static_cast<uint8_t>(exec::X64Reg::RSI);

        const uint8_t sysnum = map_reg(node.op_a);
        const uint8_t arg0 = map_reg(node.op_b);
        const uint8_t arg1 = map_reg(node.op_c);

        if (sysnum != rax) emit_reg_move(out, rax, sysnum);
        if (arg0 != rdi) emit_reg_move(out, rdi, arg0);
        if (arg1 != rsi) emit_reg_move(out, rsi, arg1);

        out.push_back(0x0F);
        out.push_back(0x05); // SYSCALL

        if (node.result_id != ir::INVALID_SLOT) {
            const uint8_t dst = map_reg(node.result_id);
            if (dst != rax) {
                emit_reg_move(out, dst, rax);
            }
        }
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

    void emit_call_address(uintptr_t address, const ir::IRNode& node, std::vector<uint8_t>& out, const ir::IRStream& stream) const {
#if defined(_WIN32)
        // Win64 requires 32-byte shadow space at each call site.
        out.push_back(0x48); out.push_back(0x83); out.push_back(0xEC); out.push_back(0x20); // sub rsp, 32
#endif

        // movabs rax, imm64
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
        out.push_back(0xB8);
        append_u64(out, static_cast<uint64_t>(address));

        // call rax
        out.push_back(0xFF);
        out.push_back(0xD0);

#if defined(_WIN32)
        out.push_back(0x48); out.push_back(0x83); out.push_back(0xC4); out.push_back(0x20); // add rsp, 32
#endif

        if (node.result_id != ir::INVALID_SLOT) {
            const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
            if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                const uint8_t dst_xmm = map_xmm_reg(node.result_id);
                emit_movq_xmm_gpr(out, dst_xmm, static_cast<uint8_t>(exec::X64Reg::RAX));
            } else {
                const uint8_t dst = map_reg(node.result_id);
                if (dst != static_cast<uint8_t>(exec::X64Reg::RAX)) {
                    emit_reg_move(out, dst, static_cast<uint8_t>(exec::X64Reg::RAX));
                }
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
            case ir::OpCode::LEA: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t dst = map_reg(node.result_id);
                const uint8_t base = map_reg(node.op_a);
                const uint8_t index = map_reg(node.op_b);

                uint8_t scale_bits = 0;
                switch (node.op_c) {
                    case 1u: scale_bits = 0; break;
                    case 2u: scale_bits = 1; break;
                    case 4u: scale_bits = 2; break;
                    case 8u: scale_bits = 3; break;
                    default:
                        return false;
                }

                const uint8_t index_lo = static_cast<uint8_t>(index & 0x7u);
                const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
                if (index_lo == 0x4u) {
                    return false;
                }

                const bool needs_disp8_zero = (base_lo == 0x5u);
                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(dst),
                                                     /*x=*/requires_rex(index),
                                                     /*b=*/requires_rex(base)));
                out.push_back(0x8D); // LEA r64, m

                const uint8_t mod = needs_disp8_zero ? 0x01u : 0x00u;
                const uint8_t modrm = static_cast<uint8_t>((mod << 6u) | ((dst & 0x7u) << 3u) | 0x04u);
                out.push_back(modrm);

                const uint8_t sib = static_cast<uint8_t>((scale_bits << 6u) | ((index & 0x7u) << 3u) | (base & 0x7u));
                out.push_back(sib);
                if (needs_disp8_zero) {
                    out.push_back(0x00u);
                }
                return true;
            }
            case ir::OpCode::GEP: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t dst = map_reg(node.result_id);
                const uint8_t base = map_reg(node.op_a);
                const uint8_t index = map_reg(node.op_b);
                const uint8_t index_lo = static_cast<uint8_t>(index & 0x7u);
                const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
                if (index_lo == 0x4u) {
                    return false;
                }

                const bool needs_disp8_zero = (base_lo == 0x5u);
                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(dst),
                                                     /*x=*/requires_rex(index),
                                                     /*b=*/requires_rex(base)));
                out.push_back(0x8D); // LEA r64, [base + index]

                const uint8_t mod = needs_disp8_zero ? 0x01u : 0x00u;
                const uint8_t modrm = static_cast<uint8_t>((mod << 6u) | ((dst & 0x7u) << 3u) | 0x04u);
                out.push_back(modrm);
                out.push_back(static_cast<uint8_t>(((index & 0x7u) << 3u) | (base & 0x7u)));
                if (needs_disp8_zero) {
                    out.push_back(0x00u);
                }
                return true;
            }
            case ir::OpCode::PREFETCH:
            case ir::OpCode::PREFETCH_T0:
                if (node.op_a == ir::INVALID_SLOT) {
                    return true;
                }
                emit_prefetch_t0_from_base(out, map_reg(node.op_a));
                return true;
            case ir::OpCode::CLFLUSH:
                if (node.op_a == ir::INVALID_SLOT) {
                    return true;
                }
                emit_clflush_from_base(out, map_reg(node.op_a));
                return true;
            case ir::OpCode::READ_TSC:
                out.push_back(0x0F);
                out.push_back(0x31); // RDTSC -> EDX:EAX
                out.push_back(0x48); out.push_back(0xC1); out.push_back(0xE2); out.push_back(0x20); // shl rdx, 32
                out.push_back(0x48); out.push_back(0x09); out.push_back(0xD0); // or rax, rdx
                if (node.result_id != ir::INVALID_SLOT) {
                    const uint8_t dst = map_reg(node.result_id);
                    const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
                    if (dst != rax) {
                        emit_reg_move(out, dst, rax);
                    }
                }
                return true;
            case ir::OpCode::PTR_DIFF: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t src_a = map_reg(node.op_a);
                const uint8_t src_b = map_reg(node.op_b);
                const uint8_t dst = map_reg(node.result_id);

                if (dst != src_a) {
                    emit_reg_move(out, dst, src_a);
                }

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(src_b),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(dst)));
                out.push_back(0x29); // SUB r/m64, r64
                emit_modrm_reg_reg(out, src_b, dst);
                return true;
            }
            case ir::OpCode::BOOL_SELECT:
            case ir::OpCode::SELECT: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t src_cond = map_reg(node.op_a);
                const uint8_t src_true = map_reg(node.op_b);
                const uint8_t src_false = map_reg(node.op_c);
                const uint8_t dst = map_reg(node.result_id);

                const bool use_true_initialized_plan = (dst == src_true && src_true != src_false);
                if (use_true_initialized_plan) {
                    if (dst != src_true) {
                        emit_reg_move(out, dst, src_true);
                    }
                } else {
                    if (dst != src_false) {
                        emit_reg_move(out, dst, src_false);
                    }
                }

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(src_cond),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_cond)));
                out.push_back(0x85);
                emit_modrm_reg_reg(out, src_cond, src_cond);

                if (use_true_initialized_plan) {
                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(dst),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_false)));
                    out.push_back(0x0F);
                    out.push_back(0x44); // CMOVE r64, r/m64
                    emit_modrm_reg_reg(out, dst, src_false);
                } else {
                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(dst),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_true)));
                    out.push_back(0x0F);
                    out.push_back(0x45); // CMOVNE r64, r/m64
                    emit_modrm_reg_reg(out, dst, src_true);
                }
                return true;
            }
            case ir::OpCode::PHI_NODE: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t src_a = map_reg(node.op_a);
                const uint8_t src_b = map_reg(node.op_b);
                const uint8_t src_pick = map_reg(node.op_c);
                const uint8_t dst = map_reg(node.result_id);

                const bool use_b_initialized_plan = (dst == src_b && src_b != src_a);
                if (use_b_initialized_plan) {
                    if (dst != src_b) {
                        emit_reg_move(out, dst, src_b);
                    }
                } else {
                    if (dst != src_a) {
                        emit_reg_move(out, dst, src_a);
                    }
                }

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(src_pick),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_pick)));
                out.push_back(0x85); // TEST r/m64, r64
                emit_modrm_reg_reg(out, src_pick, src_pick);

                if (use_b_initialized_plan) {
                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(dst),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_a)));
                    out.push_back(0x0F);
                    out.push_back(0x44); // CMOVE r64, r/m64
                    emit_modrm_reg_reg(out, dst, src_a);
                } else {
                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(dst),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_b)));
                    out.push_back(0x0F);
                    out.push_back(0x45); // CMOVNE r64, r/m64
                    emit_modrm_reg_reg(out, dst, src_b);
                }
                return true;
            }
            case ir::OpCode::GUARD_ALIAS: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t src_a = map_reg(node.op_a);
                const uint8_t src_b = map_reg(node.op_b);
                const uint8_t src_fail = map_reg(node.op_c);
                const uint8_t dst = map_reg(node.result_id);

                if (dst == src_fail && src_fail != src_a && src_fail != src_b) {
                    return false;
                }

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(src_b),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_a)));
                out.push_back(0x39); // CMP r/m64, r64
                emit_modrm_reg_reg(out, src_b, src_a);

                emit_mov_imm32_signext(out, dst, 1u);

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(dst),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_fail)));
                out.push_back(0x0F);
                out.push_back(0x45); // CMOVNE r64, r/m64
                emit_modrm_reg_reg(out, dst, src_fail);
                return true;
            }
            case ir::OpCode::GUARD_VALUE: {
                if (node.result_id == ir::INVALID_SLOT) {
                    return true;
                }

                const uint8_t src_value = map_reg(node.op_a);
                const uint8_t src_expected = map_reg(node.op_b);
                const uint8_t src_fail = map_reg(node.op_c);
                const uint8_t dst = map_reg(node.result_id);

                if (dst == src_value) {
                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(src_expected),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_value)));
                    out.push_back(0x39); // CMP r/m64, r64
                    emit_modrm_reg_reg(out, src_expected, src_value);

                    out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                         /*r=*/requires_rex(dst),
                                                         /*x=*/false,
                                                         /*b=*/requires_rex(src_fail)));
                    out.push_back(0x0F);
                    out.push_back(0x45); // CMOVNE r64, r/m64
                    emit_modrm_reg_reg(out, dst, src_fail);
                    return true;
                }

                if (dst != src_fail) {
                    emit_reg_move(out, dst, src_fail);
                }

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(src_expected),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_value)));
                out.push_back(0x39); // CMP r/m64, r64
                emit_modrm_reg_reg(out, src_expected, src_value);

                out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                                     /*r=*/requires_rex(dst),
                                                     /*x=*/false,
                                                     /*b=*/requires_rex(src_value)));
                out.push_back(0x0F);
                out.push_back(0x44); // CMOVE r64, r/m64
                emit_modrm_reg_reg(out, dst, src_value);
                return true;
            }
            default:
                return false;
        }
    }

    void emit_call_slot(const lowering::SelectedInstruction& selected,
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

            const uint8_t arg0 = map_reg(node.op_a);
            const uint8_t arg1 = map_reg(node.op_b);
            const uint8_t arg2 = map_reg(node.op_c);

            if (arg0 != rcx) emit_reg_move(out, rcx, arg0);
            if ((original_opcode == ir::OpCode::STRING_COMPARE ||
                 original_opcode == ir::OpCode::STRING_CONCAT ||
                 original_opcode == ir::OpCode::STRING_SUBSTRING) && arg1 != rdx) {
                emit_reg_move(out, rdx, arg1);
            }
            if (original_opcode == ir::OpCode::STRING_SUBSTRING && arg2 != r8) {
                emit_reg_move(out, r8, arg2);
            }

            emit_call_address(target, node, out, stream);
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

            const uint8_t arg0 = map_reg(node.op_a);
            const uint8_t arg1 = map_reg(node.op_b);
            const uint8_t arg2 = map_reg(node.op_c);

            if (arg0 != rcx) emit_reg_move(out, rcx, arg0);
            if (arg1 != rdx) emit_reg_move(out, rdx, arg1);
            if (arg2 != r8) emit_reg_move(out, r8, arg2);

            emit_call_address(target, node, out, stream);
            return;
        }

        const auto it = runtime_slots_.find(node.op_a);
        if (it == runtime_slots_.end()) {
            const auto target_opt = pick_known_label(label_to_node_index, {node.op_a, node.result_id, node.op_b});
            const uint32_t target_label = target_opt.value_or(node.op_a);

#if defined(_WIN32)
            const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
            const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
            const uint8_t r8  = static_cast<uint8_t>(exec::X64Reg::R8);

            if (node.op_b != ir::INVALID_SLOT) {
                const ir::TypeCode tc = stream.get_result_type_code(node.op_b);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movq_gpr_xmm(out, rcx, map_xmm_reg(node.op_b));
                } else {
                    const uint8_t arg0 = map_reg(node.op_b);
                    if (arg0 != rcx) emit_reg_move(out, rcx, arg0);
                }
            }
            if (node.op_c != ir::INVALID_SLOT) {
                const ir::TypeCode tc = stream.get_result_type_code(node.op_c);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movq_gpr_xmm(out, rdx, map_xmm_reg(node.op_c));
                } else {
                    const uint8_t arg1 = map_reg(node.op_c);
                    if (arg1 != rdx) emit_reg_move(out, rdx, arg1);
                }
            }
            if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
                const ir::TypeCode tc = stream.get_result_type_code(node.logic_flags);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    emit_movq_gpr_xmm(out, r8, map_xmm_reg(node.logic_flags));
                } else {
                    const uint8_t arg2 = map_reg(node.logic_flags);
                    if (arg2 != r8) emit_reg_move(out, r8, arg2);
                }
            }
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xEC); out.push_back(0x20); // sub rsp, 32
#endif
            out.push_back(0xE8); // CALL rel32
            const size_t disp_offset = out.size();
            out.push_back(0x00);
            out.push_back(0x00);
            out.push_back(0x00);
            out.push_back(0x00);
            const size_t instr_end = out.size();
#if defined(_WIN32)
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xC4); out.push_back(0x20); // add rsp, 32
#endif
            if (node.result_id != ir::INVALID_SLOT) {
                const ir::TypeCode tc = stream.get_result_type_code(node.result_id);
                if (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32) {
                    const uint8_t dst_xmm = map_xmm_reg(node.result_id);
                    emit_movq_xmm_gpr(out, dst_xmm, static_cast<uint8_t>(exec::X64Reg::RAX));
                } else {
                    const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
                    const uint8_t dst = map_reg(node.result_id);
                    if (dst != rax) emit_reg_move(out, dst, rax);
                }
            }
            patches.push_back(PendingBranchPatch{disp_offset, instr_end, target_label});
            return;
        }

#if defined(_WIN32)
        const uint8_t rcx = static_cast<uint8_t>(exec::X64Reg::RCX);
        const uint8_t rdx = static_cast<uint8_t>(exec::X64Reg::RDX);
        const uint8_t r8  = static_cast<uint8_t>(exec::X64Reg::R8);

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

        if (node.op_b != ir::INVALID_SLOT) {
            const ir::TypeCode tc = stream.get_result_type_code(node.op_b);
            if (is_gpr_float_helper && (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32)) {
                emit_movq_gpr_xmm(out, rcx, map_xmm_reg(node.op_b));
            } else {
                const uint8_t arg0 = map_reg(node.op_b);
                if (arg0 != rcx) emit_reg_move(out, rcx, arg0);
            }
        } else {
            out.push_back(0x48); out.push_back(0x31); out.push_back(0xC9); // xor rcx, rcx
        }
        if (node.op_c != ir::INVALID_SLOT) {
            const ir::TypeCode tc = stream.get_result_type_code(node.op_c);
            if (is_gpr_float_helper && (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32)) {
                emit_movq_gpr_xmm(out, rdx, map_xmm_reg(node.op_c));
            } else {
                const uint8_t arg1 = map_reg(node.op_c);
                if (arg1 != rdx) emit_reg_move(out, rdx, arg1);
            }
        } else {
            out.push_back(0x48); out.push_back(0x31); out.push_back(0xD2); // xor rdx, rdx
        }
        if (node.logic_flags != 0 && node.logic_flags != ir::INVALID_SLOT) {
            const ir::TypeCode tc = stream.get_result_type_code(node.logic_flags);
            if (is_gpr_float_helper && (tc == ir::TypeCode::F64 || tc == ir::TypeCode::F32)) {
                emit_movq_gpr_xmm(out, r8, map_xmm_reg(node.logic_flags));
            } else {
                const uint8_t arg2 = map_reg(node.logic_flags);
                if (arg2 != r8) emit_reg_move(out, r8, arg2);
            }
        } else {
            out.push_back(0x4D); out.push_back(0x31); out.push_back(0xC0); // xor r8, r8
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

        emit_call_address(static_cast<uintptr_t>(it->second), node, out, stream);
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