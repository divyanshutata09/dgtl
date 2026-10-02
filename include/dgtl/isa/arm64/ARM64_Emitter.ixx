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

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 4: BACKEND EMISSION - ARM64 / AARCH64
 * FULL SYNCHRONIZATION: Aligned with 178 IR OpCodes, ISel DSL Bootstrap Rules & Precise ARM64 EABI Layout.
 */

module;

#include <cstdlib>
#include <cstring>

#include <cstdint>
#include <ostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <vector>
export module dgtl.isa.arm64.arm64_emitter;
import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;
import dgtl.mir.abi_lowering;

export namespace dgtl::isa::arm64 {

using namespace dgtl::machine;

class alignas(64) ARM64Emitter {
public:
    struct Config {
        bool use_neon         = true;
        bool position_indep  = true; 
        bool omit_fp         = true; 
        bool verbose_asm     = true;
        
        // Explicit constructor prevents MinGW "required before end of class" error
        constexpr Config() noexcept {} 
    };

    const dgtl::machine::MachineFunction* m_function{nullptr};

    ARM64Emitter() = default;
    explicit ARM64Emitter(const dgtl::machine::MachineFunction& function)
        : m_function(&function) {}
    template <typename StreamT, typename SymbolTableT>
    explicit ARM64Emitter(const StreamT&, const SymbolTableT&)
        : m_function(nullptr) {}

    /**
     * @brief Generates AArch64 Assembly.
     */
    void emit_assembly(std::ostream& out, const Config config = {}) const {
        if (m_function != nullptr) {
            (void)emit_mir(*m_function, out, config);
            return;
        }
        emit_header(out, config);
        emit_footer(out, config);
    }

    template <typename StreamT>
    void emit(StreamT& stream, std::ostream& out) const {
        if (m_function != nullptr) {
            (void)emit_mir(*m_function, out, {});
            return;
        }
        (void)stream;
        emit_assembly(out);
    }

    struct RuntimeBranchReloc {
        uint32_t text_offset{0};
        std::string symbol;
    };

    struct Arm64CodegenResult {
        std::vector<uint8_t> code;
        std::vector<RuntimeBranchReloc> runtime_relocs;
    };

    [[nodiscard]] static constexpr bool is_valid_gpr_slot(uint32_t slot) noexcept {
        return slot != 0xFFFFFFFFu && slot < 32;
    }

    static void append_u32le(std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    }

    static uint32_t encode_mov_x(uint32_t dst, uint32_t src) {
        return 0xAA0003E0u | ((src & 0x1Fu) << 16) | (dst & 0x1Fu);
    }

    static uint32_t arm64_bcond_code(dgtl::machine::ComparisonPredicate pred) noexcept {
        switch (pred) {
            case dgtl::machine::ComparisonPredicate::Equal:
                return 0x0u; // EQ
            case dgtl::machine::ComparisonPredicate::NotEqual:
                return 0x1u; // NE
            case dgtl::machine::ComparisonPredicate::SignedLessThan:
                return 0xBu; // LT
            case dgtl::machine::ComparisonPredicate::SignedLessEqual:
                return 0xDu; // LE
            case dgtl::machine::ComparisonPredicate::SignedGreaterThan:
                return 0xCu; // GT
            case dgtl::machine::ComparisonPredicate::SignedGreaterEqual:
                return 0xAu; // GE
            case dgtl::machine::ComparisonPredicate::UnsignedLessThan:
                return 0x3u; // CC / LO
            case dgtl::machine::ComparisonPredicate::UnsignedLessEqual:
                return 0x9u; // LS
            case dgtl::machine::ComparisonPredicate::UnsignedGreaterThan:
                return 0x8u; // HI
            case dgtl::machine::ComparisonPredicate::UnsignedGreaterEqual:
                return 0x2u; // CS / HS
            default:
                return 0x1u; // NE
        }
    }

    /**
     * @brief Encodes a MachineFunction into ARM64 binary machine code.
     */
    static Arm64CodegenResult generate_arm64_binary(const dgtl::machine::MachineFunction& function) {
        Arm64CodegenResult result;
        std::vector<uint8_t>& code = result.code;
        code.reserve(function.instructions().size() * 4 + 16);

        std::unordered_map<dgtl::machine::BlockId, size_t> block_byte_offsets;

        struct BranchPatch32 {
            size_t insn_byte_offset;
            dgtl::machine::BlockId target_block;
            bool is_cond;
            uint32_t cond_reg;
        };
        std::vector<BranchPatch32> patches;

        for (const auto& block : function.blocks()) {
            block_byte_offsets[block.id] = code.size();

            for (dgtl::machine::InstrId instruction_id : block.instructions) {
                const auto* instruction = function.instruction(instruction_id);
                if (!instruction) continue;

                if (instruction->form == dgtl::machine::MachineForm::Goto) {
                    if (!instruction->operands.empty() && instruction->operands[0].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                        patches.push_back({code.size(), instruction->operands[0].block, false, 0});
                        append_u32le(code, 0x14000000u); // B #0
                        continue;
                    }
                }
                if (instruction->form == dgtl::machine::MachineForm::BranchCond) {
                    if (instruction->operands.size() >= 2 && instruction->operands[1].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                        const uint32_t reg = instruction->operands[0].kind == dgtl::machine::MachineOperandKind::Register ? (instruction->operands[0].reg.id & 0x1Fu) : 0u;
                        patches.push_back({code.size(), instruction->operands[1].block, true, reg});
                        append_u32le(code, 0xB5000000u | reg); // CBNZ
                        continue;
                    }
                }
                if (instruction->form == dgtl::machine::MachineForm::BranchCompare) {
                    if (instruction->operands.size() >= 3 && instruction->operands[2].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                        const uint32_t rn = instruction->operands[0].kind == dgtl::machine::MachineOperandKind::Register ? (instruction->operands[0].reg.id & 0x1Fu) : 0u;
                        const uint32_t rm = instruction->operands[1].kind == dgtl::machine::MachineOperandKind::Register ? (instruction->operands[1].reg.id & 0x1Fu) : 0u;
                        append_u32le(code, 0xEB000000u | (rm << 16) | (rn << 5) | 0x1Fu); // CMP
                        const uint32_t cond = arm64_bcond_code(instruction->comparison_predicate());
                        patches.push_back({code.size(), instruction->operands[2].block, true, 0xFFu});
                        append_u32le(code, 0x54000000u | cond); // B.<cond> #0
                        continue;
                    }
                }

                if (const char* runtime_symbol = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(instruction->opcode)) {
                    const uint32_t bl_offset = static_cast<uint32_t>(code.size());
                    append_u32le(code, 0x94000000u);
                    result.runtime_relocs.push_back({bl_offset, runtime_symbol});
                    continue;
                }

                const uint32_t insn = encode_arm64_instruction(*instruction);
                append_u32le(code, insn);
            }
        }

        // Patch branches
        for (const auto& p : patches) {
            auto it = block_byte_offsets.find(p.target_block);
            const size_t target_byte = (it != block_byte_offsets.end()) ? it->second : code.size();
            const int64_t delta_bytes = static_cast<int64_t>(target_byte) - static_cast<int64_t>(p.insn_byte_offset);
            const int64_t delta_insns = delta_bytes / 4;

            uint32_t insn = 0;
            std::memcpy(&insn, &code[p.insn_byte_offset], 4);
            if (!p.is_cond) {
                if (delta_insns >= -(1 << 25) && delta_insns < (1 << 25)) {
                    insn = 0x14000000u | (static_cast<uint32_t>(delta_insns) & 0x3FFFFFFu);
                }
            } else if (p.cond_reg == 0xFFu) {
                if (delta_insns >= -(1 << 18) && delta_insns < (1 << 18)) {
                    insn = (insn & 0xFF00000Fu) | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5);
                }
            } else {
                if (delta_insns >= -(1 << 18) && delta_insns < (1 << 18)) {
                    insn = 0xB5000000u | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5) | (p.cond_reg & 0x1Fu);
                }
            }
            std::memcpy(&code[p.insn_byte_offset], &insn, 4);
        }

        return result;
    }

    /**
     * @brief Maps a single DGTL MIR MachineInstruction to its ARM64 binary encoding.
     */
    static uint32_t encode_arm64_instruction(const dgtl::machine::MachineInstruction& instruction) {
        using namespace dgtl::machine;
        uint32_t rd = 0, rn = 0, rm = 0;
        for (const auto& op : instruction.operands) {
            if (op.kind == MachineOperandKind::Register) {
                if (op.role == OperandRole::Def) {
                    rd = op.reg.id & 0x1F;
                } else if (rn == 0) {
                    rn = op.reg.id & 0x1F;
                } else {
                    rm = op.reg.id & 0x1F;
                }
            }
        }

        switch (instruction.form) {
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                const auto sem = instruction.semantic_kind;
                const auto op = instruction.opcode;
                if (sem == MachineSemanticKind::IntegerAdd || op == GM_IADD32 || op == GM_IADD64) {
                    return 0x0B000000u | (rm << 16) | (rn << 5) | rd; // ADD
                }
                if (sem == MachineSemanticKind::IntegerSub || op == GM_ISUB32 || op == GM_ISUB64) {
                    return 0x4B000000u | (rm << 16) | (rn << 5) | rd; // SUB
                }
                if (sem == MachineSemanticKind::IntegerMul || op == GM_IMUL32 || op == GM_IMUL64) {
                    return 0x1B007C00u | (rm << 16) | (rn << 5) | rd; // MADD
                }
                if (sem == MachineSemanticKind::IntegerAnd || op == GM_IAND32 || op == GM_IAND64) {
                    return 0x0A000000u | (rm << 16) | (rn << 5) | rd; // AND
                }
                if (sem == MachineSemanticKind::IntegerOr || op == GM_IOR32 || op == GM_IOR64) {
                    return 0x2A000000u | (rm << 16) | (rn << 5) | rd; // ORR
                }
                if (sem == MachineSemanticKind::IntegerXor || op == GM_IXOR32 || op == GM_IXOR64) {
                    return 0x4A000000u | (rm << 16) | (rn << 5) | rd; // EOR
                }
                return 0x0B000000u | (rm << 16) | (rn << 5) | rd;
            }
            case MachineForm::Move:
                return 0x2A0003E0u | (rn << 16) | rd; // MOV Wd, Wn
            case MachineForm::LoadImmediate: {
                uint32_t imm16 = 0;
                for (const auto& op : instruction.operands) {
                    if (op.kind == MachineOperandKind::Immediate) {
                        imm16 = static_cast<uint32_t>(op.immediate) & 0xFFFF;
                        break;
                    }
                }
                return 0x52800000u | (imm16 << 5) | rd; // MOVZ
            }
            case MachineForm::LoadStack: {
                uint32_t base = rn;
                uint32_t off12 = 0;
                for (const auto& op : instruction.operands) {
                    if (op.kind == MachineOperandKind::Immediate) {
                        off12 = (static_cast<uint32_t>(op.immediate) & 0xFFFu) >> 2;
                    }
                }
                return 0xB9400000u | (off12 << 10) | (base << 5) | rd; // LDR Wd, [Xn, #offset]
            }
            case MachineForm::StoreStack: {
                uint32_t val = (rd != 0) ? rd : rn;
                uint32_t base = (rn != val) ? rn : rm;
                uint32_t off12 = 0;
                for (const auto& op : instruction.operands) {
                    if (op.kind == MachineOperandKind::Immediate) {
                        off12 = (static_cast<uint32_t>(op.immediate) & 0xFFFu) >> 2;
                    }
                }
                return 0xB9000000u | (off12 << 10) | (base << 5) | val; // STR Wd, [Xn, #offset]
            }
            case MachineForm::Return:
                return 0xD65F03C0u; // RET
            case MachineForm::NoOp:
                return 0xD503201Fu; // NOP
            default:
                if (dgtl::mir::runtime_contract::has_runtime_symbol(instruction.opcode)) {
                    return 0xD4200000u; // BRK #0
                }
                return 0xD503201Fu; // NOP fallback
        }
    }



    /**
     * MIR-native AArch64 assembly emission.
     *
     * This is the authoritative backend emission boundary. The function must
     * already be post-register-allocation and frame-lowered. No IRNode/result
     * id is consulted here.
     */
    [[nodiscard]] bool emit_mir(
        const dgtl::machine::MachineFunction& function,
        std::ostream& out,
        const Config config = {}) const {

        using namespace dgtl::machine;
        (void)config;

        if (!function.is_phase_at_least(MachinePhase::FrameLowered) ||
            !function.properties().registers_allocated ||
            !function.properties().frame_lowered ||
            function.has_virtual_registers()) {
            return false;
        }

        const auto verify = MachineVerifier::verify(function);
        if (!verify.ok()) return false;

        const auto& frame = function.frame_info();
        if (frame.stack_alignment != 16u ||
            frame.callee_saved_bytes !=
                frame.callee_saved_registers.size() * 8u) {
            return false;
        }

        out << "// --- DGTL MIR-NATIVE AARCH64 BACKEND ---\n";
        out << ".arch armv8-a\n";
        out << ".text\n";
        out << ".align 2\n";
        out << ".global _DGTL_entry\n";
        out << "_DGTL_entry:\n";

        if (!emit_mir_prologue(function, out)) return false;

        for (const auto& block : function.blocks()) {
            out << ".Lbb" << block.id << ":\n";

            for (const InstrId instruction_id : block.instructions) {
                const auto* instruction = function.instruction(instruction_id);
                if (instruction == nullptr) return false;

                if (!emit_mir_instruction(function, *instruction, out))
                    return false;
            }
        }

        out << ".L_epilogue:\n";
        if (!emit_mir_epilogue(function, out)) return false;
        return true;
    }

private:

    static std::string mir_gpr(std::uint32_t id, bool is32 = false) {
        if (id < 31u) {
            if (!is32) return "x" + std::to_string(id);
            return "w" + std::to_string(id);
        }
        if (id == 31u) return "sp";
        return {};
    }

    static std::string mir_simd(std::uint32_t id,
                                std::uint32_t width_bits,
                                bool vector = false) {
        if (id < 32u || id >= 64u) return {};
        const std::uint32_t n = id - 32u;
        if (vector) return "v" + std::to_string(n);
        return (width_bits == 32u ? "s" : "d") + std::to_string(n);
    }

    static std::string mir_reg(
        const dgtl::machine::MachineRegister& reg,
        std::uint32_t width_bits = 64u) {

        using namespace dgtl::machine;

        if (!reg.valid() || reg.is_virtual()) return {};

        switch (reg.reg_class) {
            case RegisterClassKind::General:
            case RegisterClassKind::Integer:
            case RegisterClassKind::Address:
                return mir_gpr(reg.id, width_bits == 32u);

            case RegisterClassKind::FloatingPoint:
                return mir_simd(reg.id, width_bits, false);

            case RegisterClassKind::Vector:
                return mir_simd(reg.id, 128u, true);

            default:
                return {};
        }
    }

    static std::string mir_memory(
        const dgtl::machine::MachineMemoryOperand& memory) {

        std::string text = "[";
        const std::string base = mir_reg(memory.base, 64u);
        if (base.empty()) return {};

        text += base;

        if (memory.index.valid()) {
            if (memory.displacement != 0) return {};
            const std::string index = mir_reg(memory.index, 64u);
            if (index.empty()) return {};
            text += ", " + index;
            if (memory.scale != 1) {
                if (memory.scale != 2 &&
                    memory.scale != 4 &&
                    memory.scale != 8) {
                    return {};
                }
                text += ", lsl #" + std::to_string(
                    memory.scale == 2 ? 1 :
                    memory.scale == 4 ? 2 : 3);
            }
        } else if (memory.displacement != 0) {
            text += ", #" + std::to_string(memory.displacement);
        }

        text += "]";
        return text;
    }

    static std::string mir_operand(
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineOperand& operand,
        std::uint32_t width_bits = 64u) {

        using namespace dgtl::machine;

        switch (operand.kind) {
            case MachineOperandKind::Register:
                return mir_reg(operand.reg,
                               operand.subregister != 0
                                   ? operand.subregister
                                   : width_bits);

            case MachineOperandKind::Immediate:
                return "#" + std::to_string(operand.immediate);

            case MachineOperandKind::FloatingImmediate:
                return std::to_string(operand.floating_immediate);

            case MachineOperandKind::Memory:
                return mir_memory(operand.memory);

            case MachineOperandKind::BasicBlock:
                return ".Lbb" + std::to_string(operand.block);

            case MachineOperandKind::Symbol:
                return "_DGTL_fn_" + std::to_string(operand.symbol);

            case MachineOperandKind::StackSlot:
                if (operand.stack_slot < function.frame_info().slots.size()) {
                    const auto& slot =
                        function.frame_info().slots[operand.stack_slot];
                    return "[sp, #" + std::to_string(slot.offset) + "]";
                }
                return {};

            default:
                return {};
        }
    }

    static const dgtl::machine::MachineOperand* mir_def(
        const dgtl::machine::MachineInstruction& instruction) {

        for (const auto& operand : instruction.operands) {
            if (operand.kind == dgtl::machine::MachineOperandKind::Register &&
                operand.role == dgtl::machine::OperandRole::Def) {
                return &operand;
            }
        }
        return nullptr;
    }

    static std::vector<const dgtl::machine::MachineOperand*> mir_uses(
        const dgtl::machine::MachineInstruction& instruction) {

        std::vector<const dgtl::machine::MachineOperand*> result;
        for (const auto& operand : instruction.operands) {
            if (operand.role == dgtl::machine::OperandRole::Def)
                continue;
            if (operand.role == dgtl::machine::OperandRole::Debug)
                continue;
            result.push_back(&operand);
        }
        return result;
    }

    static void emit_mov_imm64(
        std::ostream& out,
        std::uint32_t dst,
        std::uint64_t value) {

        out << "    movz x" << dst << ", #" << (value & 0xFFFFu);

        for (unsigned shift = 16u; shift < 64u; shift += 16u) {
            const std::uint64_t part = (value >> shift) & 0xFFFFu;
            if (part != 0u) {
                out << "\n    movk x" << dst
                    << ", #" << part
                    << ", lsl #" << shift;
            }
        }
        out << "\n";
    }

    static bool emit_mir_prologue(
        const dgtl::machine::MachineFunction& function,
        std::ostream& out) {

        const auto& frame = function.frame_info();

        // Canonical frame record. This also preserves LR across BL.
        out << "    stp x29, x30, [sp, #-16]!\n";
        out << "    mov x29, sp\n";

        // Save the exact physical non-volatile set selected by frame lowering.
        // AAPCS64 scalar FP preservation covers the low 64 bits of V8-V15;
        // vector V8-V15 are deliberately not allocated by the current backend.
        std::vector<const MachineRegister*> gprs;
        std::vector<const MachineRegister*> fps;
        for (const auto& reg : frame.callee_saved_registers) {
            if (reg.reg_class == RegisterClassKind::FloatingPoint) {
                fps.push_back(&reg);
            } else {
                gprs.push_back(&reg);
            }
        }

        auto save_gpr_group = [&](const std::vector<const MachineRegister*>& regs) -> bool {
            std::size_t i = 0;
            for (; i + 1u < regs.size(); i += 2u) {
                if (regs[i]->id > 30u || regs[i + 1u]->id > 30u) return false;
                out << "    stp x" << regs[i]->id << ", x"
                    << regs[i + 1u]->id << ", [sp, #-16]!\n";
            }
            if (i < regs.size()) {
                if (regs[i]->id > 30u) return false;
                out << "    str x" << regs[i]->id
                    << ", [sp, #-16]!\n";
            }
            return true;
        };

        auto save_fp_group = [&](const std::vector<const MachineRegister*>& regs) -> bool {
            std::size_t i = 0;
            for (; i + 1u < regs.size(); i += 2u) {
                if (regs[i]->id < 40u || regs[i]->id >= 48u ||
                    regs[i + 1u]->id < 40u || regs[i + 1u]->id >= 48u) {
                    return false;
                }
                out << "    stp d" << (regs[i]->id - 32u) << ", d"
                    << (regs[i + 1u]->id - 32u)
                    << ", [sp, #-16]!\n";
            }
            if (i < regs.size()) {
                if (regs[i]->id < 40u || regs[i]->id >= 48u) return false;
                out << "    str d" << (regs[i]->id - 32u)
                    << ", [sp, #-16]!\n";
            }
            return true;
        };

        if (!save_gpr_group(gprs) || !save_fp_group(fps))
            return false;

        if (frame.total_frame_bytes != 0u) {
            out << "    sub sp, sp, #"
                << frame.total_frame_bytes << "\n";
        }

        if (frame.has_calls && frame.outgoing_argument_bytes != 0u &&
            frame.outgoing_argument_bytes > frame.total_frame_bytes) {
            return false;
        }

        return true;
    }

    static bool emit_mir_epilogue(
        const dgtl::machine::MachineFunction& function,
        std::ostream& out) {

        const auto& frame = function.frame_info();

        if (frame.total_frame_bytes != 0u) {
            out << "    add sp, sp, #"
                << frame.total_frame_bytes << "\n";
        }

        const auto& saved = frame.callee_saved_registers;

        std::vector<const MachineRegister*> gprs;
        std::vector<const MachineRegister*> fps;
        for (const auto& reg : saved) {
            if (reg.reg_class == RegisterClassKind::FloatingPoint)
                fps.push_back(&reg);
            else
                gprs.push_back(&reg);
        }

        auto restore_fp_group =
            [&](const std::vector<const MachineRegister*>& regs) -> bool {
                if ((regs.size() & 1u) != 0u) {
                    const auto& reg = *regs.back();
                    if (reg.id < 40u || reg.id >= 48u) return false;
                    out << "    ldr d" << (reg.id - 32u)
                        << ", [sp], #16\n";
                }

                const std::size_t paired =
                    regs.size() & ~std::size_t(1u);
                for (std::size_t i = paired; i > 0u; i -= 2u) {
                    const auto& a = *regs[i - 2u];
                    const auto& b = *regs[i - 1u];
                    if (a.id < 40u || a.id >= 48u ||
                        b.id < 40u || b.id >= 48u) return false;
                    out << "    ldp d" << (a.id - 32u) << ", d"
                        << (b.id - 32u) << ", [sp], #16\n";
                }
                return true;
            };

        auto restore_gpr_group =
            [&](const std::vector<const MachineRegister*>& regs) -> bool {
                if ((regs.size() & 1u) != 0u) {
                    const auto& reg = *regs.back();
                    if (reg.id > 30u) return false;
                    out << "    ldr x" << reg.id
                        << ", [sp], #16\n";
                }

                const std::size_t paired =
                    regs.size() & ~std::size_t(1u);
                for (std::size_t i = paired; i > 0u; i -= 2u) {
                    const auto& a = *regs[i - 2u];
                    const auto& b = *regs[i - 1u];
                    if (a.id > 30u || b.id > 30u) return false;
                    out << "    ldp x" << a.id << ", x" << b.id
                        << ", [sp], #16\n";
                }
                return true;
            };

        if (!restore_fp_group(fps) || !restore_gpr_group(gprs))
            return false;

        out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
        return true;
    }

    static bool emit_mir_call(
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction,
        std::ostream& out) {

        using namespace dgtl::machine;

        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);

        const MachineOperand* symbol = nullptr;
        std::vector<const MachineOperand*> args;

        for (const auto* operand : uses) {
            if (operand->kind == MachineOperandKind::Symbol && symbol == nullptr) {
                symbol = operand;
            } else if (operand->kind == MachineOperandKind::Register) {
                args.push_back(operand);
            }
        }

        if (symbol == nullptr) return false;

        std::uint32_t next_gpr = 0;
        std::uint32_t next_fp = 0;

        for (const auto* arg : args) {
            if (arg->reg.reg_class == RegisterClassKind::FloatingPoint) {
                if (next_fp >= 8u) return false;

                const std::uint32_t width =
                    arg->subregister == 32u ? 32u : 64u;
                out << "    fmov "
                    << (width == 32u ? "s" : "d")
                    << next_fp << ", "
                    << mir_reg(arg->reg, width) << "\n";
                ++next_fp;
            } else if (arg->reg.reg_class == RegisterClassKind::Vector) {
                if (next_fp >= 8u) return false;
                out << "    mov v" << next_fp << ".16b, "
                    << mir_reg(arg->reg, 128u) << ".16b\n";
                ++next_fp;
            } else {
                if (next_gpr >= 8u) return false;
                out << "    mov x" << next_gpr << ", "
                    << mir_reg(arg->reg, 64u) << "\n";
                ++next_gpr;
            }
        }

        out << "    bl "
            << mir_operand(function, *symbol) << "\n";

        if (def != nullptr) {
            if (def->reg.reg_class == RegisterClassKind::FloatingPoint) {
                const std::uint32_t width =
                    def->subregister == 32u ? 32u : 64u;
                out << "    fmov "
                    << mir_reg(def->reg, width) << ", "
                    << (width == 32u ? "s0" : "d0") << "\n";
            } else if (def->reg.reg_class == RegisterClassKind::Vector) {
                out << "    mov "
                    << mir_reg(def->reg, 128u) << ".16b, v0.16b\n";
            } else {
                out << "    mov "
                    << mir_reg(def->reg, 64u) << ", x0\n";
            }
        }

        return true;
    }

    static void emit_arithmetic(
        std::ostream& out,
        const char* instr,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction,
        bool is_fp = false,
        bool is_64 = true) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        const std::uint32_t width = is_64 ? 64u : 32u;
        if (is_fp) {
            char prefix = (width == 32u) ? 's' : 'd';
            out << "    " << instr << " "
                << prefix << (def->reg.id >= 32u ? def->reg.id - 32u : def->reg.id) << ", "
                << mir_operand(function, *uses[0], width) << ", "
                << mir_operand(function, *uses[1], width) << "\n";
        } else {
            out << "    " << instr << " "
                << mir_reg(def->reg, width) << ", "
                << mir_operand(function, *uses[0], width) << ", "
                << mir_operand(function, *uses[1], width) << "\n";
        }
    }

    static void emit_arithmetic_f64(
        std::ostream& out,
        const char* instr,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        emit_arithmetic(out, instr, function, instruction, true, true);
    }

    static void emit_move(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.empty()) return;
        const auto& src = *uses[0];
        if (def->reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
            const std::uint32_t width = def->subregister == 32u ? 32u : 64u;
            out << "    fmov " << mir_reg(def->reg, width) << ", "
                << mir_operand(function, src, width) << "\n";
        } else if (def->reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            out << "    mov " << mir_reg(def->reg, 128u) << ".16b, "
                << mir_operand(function, src, 128u) << ".16b\n";
        } else {
            out << "    mov " << mir_reg(def->reg, 64u) << ", "
                << mir_operand(function, src, 64u) << "\n";
        }
    }

    static void emit_shift_left(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        out << "    lslv " << mir_reg(def->reg, 64u) << ", "
            << mir_operand(function, *uses[0], 64u) << ", "
            << mir_operand(function, *uses[1], 64u) << "\n";
    }

    static void emit_shift_right(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        out << "    lsrv " << mir_reg(def->reg, 64u) << ", "
            << mir_operand(function, *uses[0], 64u) << ", "
            << mir_operand(function, *uses[1], 64u) << "\n";
    }

    static void emit_fma(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        out << "    fmadd " << mir_reg(def->reg, 32u) << ", "
            << mir_operand(function, *uses[0], 32u) << ", "
            << mir_operand(function, *uses[1], 32u) << ", "
            << mir_operand(function, *uses[2], 32u) << "\n";
    }

    static void emit_fast_fma_f64(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        out << "    fmadd " << mir_reg(def->reg, 64u) << ", "
            << mir_operand(function, *uses[0], 64u) << ", "
            << mir_operand(function, *uses[1], 64u) << ", "
            << mir_operand(function, *uses[2], 64u) << "\n";
    }

    static void emit_simd_complex_fma(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        out << "    fmla " << mir_reg(def->reg, 128u) << ".4s, "
            << mir_operand(function, *uses[0], 128u) << ".4s, "
            << mir_operand(function, *uses[1], 128u) << ".4s\n";
    }

    static void emit_fma_widen(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        out << "    fcvtl " << mir_reg(def->reg, 128u) << ".4s, "
            << mir_operand(function, *uses[0], 64u) << ".4h\n";
        out << "    fmadd " << mir_reg(def->reg, 32u) << ", "
            << mir_reg(def->reg, 32u) << ", "
            << mir_operand(function, *uses[1], 32u) << ", "
            << mir_operand(function, *uses[2], 32u) << "\n";
    }

    static void emit_load_imm(
        std::ostream& out,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.empty()) return;
        if (uses[0]->kind == dgtl::machine::MachineOperandKind::Immediate) {
            emit_mov_imm64(out, def->reg.id, static_cast<std::uint64_t>(uses[0]->immediate));
        }
    }

    static void emit_load(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.empty()) return;
        const auto address = mir_operand(function, *uses.back());
        if (address.empty()) return;
        const std::uint32_t width = def->subregister == 32u ? 32u : 64u;
        out << "    ldr " << mir_reg(def->reg, width) << ", " << address << "\n";
    }

    static void emit_store(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto uses = mir_uses(instruction);
        if (uses.size() < 2u) return;
        const auto* value = uses[0];
        const auto addr = mir_operand(function, *uses[1]);
        if (addr.empty()) return;
        const std::uint32_t width = value->subregister == 32u ? 32u : 64u;
        out << "    str " << mir_reg(value->reg, width) << ", " << addr << "\n";
    }

    static void emit_neon_vec_op(
        std::ostream& out,
        const char* instr,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        out << "    " << instr << " "
            << mir_reg(def->reg, 128u) << ".4s, "
            << mir_operand(function, *uses[0], 128u) << ".4s, "
            << mir_operand(function, *uses[1], 128u) << ".4s\n";
    }

    static void emit_fcvtl(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.empty()) return;
        out << "    fcvtl " << mir_reg(def->reg, 128u) << ".4s, "
            << mir_operand(function, *uses[0], 64u) << ".4h\n";
    }

    static void emit_fcvtn(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.empty()) return;
        out << "    fcvtn " << mir_reg(def->reg, 64u) << ".4h, "
            << mir_operand(function, *uses[0], 128u) << ".4s\n";
    }

    static void emit_bsl_predicate(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        out << "    mov " << mir_reg(def->reg, 128u) << ".16b, "
            << mir_operand(function, *uses[0], 128u) << ".16b\n";
        out << "    bsl " << mir_reg(def->reg, 128u) << ".16b, "
            << mir_operand(function, *uses[1], 128u) << ".16b, "
            << mir_operand(function, *uses[2], 128u) << ".16b\n";
    }

    static void emit_branch(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto uses = mir_uses(instruction);
        if (uses.empty() || uses[0]->kind != dgtl::machine::MachineOperandKind::BasicBlock) return;
        out << "    b " << mir_operand(function, *uses[0]) << "\n";
    }

    static void emit_branch_cond(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto uses = mir_uses(instruction);
        if (uses.size() < 2u || uses[1]->kind != dgtl::machine::MachineOperandKind::BasicBlock) return;
        out << "    cbnz " << mir_operand(function, *uses[0], 64u) << ", "
            << mir_operand(function, *uses[1]) << "\n";
        if (uses.size() >= 3u && uses[2]->kind == dgtl::machine::MachineOperandKind::BasicBlock) {
            out << "    b " << mir_operand(function, *uses[2]) << "\n";
        }
    }

    static const char* arm64_condition_string(dgtl::machine::ComparisonPredicate pred) noexcept {
        switch (pred) {
            case dgtl::machine::ComparisonPredicate::Equal:
                return "eq";
            case dgtl::machine::ComparisonPredicate::NotEqual:
                return "ne";
            case dgtl::machine::ComparisonPredicate::SignedLessThan:
                return "lt";
            case dgtl::machine::ComparisonPredicate::SignedLessEqual:
                return "le";
            case dgtl::machine::ComparisonPredicate::SignedGreaterThan:
                return "gt";
            case dgtl::machine::ComparisonPredicate::SignedGreaterEqual:
                return "ge";
            case dgtl::machine::ComparisonPredicate::UnsignedLessThan:
                return "lo";
            case dgtl::machine::ComparisonPredicate::UnsignedLessEqual:
                return "ls";
            case dgtl::machine::ComparisonPredicate::UnsignedGreaterThan:
                return "hi";
            case dgtl::machine::ComparisonPredicate::UnsignedGreaterEqual:
                return "hs";
            case dgtl::machine::ComparisonPredicate::Ordered:
                return "vc";
            case dgtl::machine::ComparisonPredicate::Unordered:
                return "vs";
            default:
                return "ne";
        }
    }

    static void emit_branch_compare_fused(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto uses = mir_uses(instruction);
        if (uses.size() < 3u) return;
        const char* cond = arm64_condition_string(instruction.comparison_predicate());
        out << "    cmp " << mir_operand(function, *uses[0], 64u) << ", "
            << mir_operand(function, *uses[1], 64u) << "\n";
        out << "    b." << cond << " " << mir_operand(function, *uses[2]) << "\n";
    }

    static void emit_phi_comment(
        std::ostream& out,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        out << "    // PHI";
        if (def) out << " x" << def->reg.id << " <-";
        for (const auto* use : uses) {
            if (use->kind == dgtl::machine::MachineOperandKind::Register)
                out << " x" << use->reg.id;
        }
        out << "\n";
    }

    static void emit_mod_i32(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        const auto a = mir_operand(function, *uses[0], 64u);
        const auto b = mir_operand(function, *uses[1], 64u);
        out << "    sdiv x16, " << a << ", " << b << "\n";
        out << "    msub " << mir_reg(def->reg, 64u) << ", x16, " << b << ", " << a << "\n";
    }

    static void emit_cmp_set(
        std::ostream& out,
        const char* cond,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 2u) return;
        out << "    cmp " << mir_operand(function, *uses[0], 32u) << ", "
            << mir_operand(function, *uses[1], 32u) << "\n";
        out << "    cset " << mir_reg(def->reg, 32u) << ", " << cond << "\n";
    }

    static void emit_syscall(std::ostream& out) {
        out << "    svc #0\n";
    }

    static void emit_bitfield_insert(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        const uint64_t lsb = uses[1]->kind == dgtl::machine::MachineOperandKind::Immediate ? (uses[1]->immediate & 63U) : 0;
        const uint64_t width = uses[2]->kind == dgtl::machine::MachineOperandKind::Immediate ? ((uses[2]->immediate & 63U) + 1U) : 1;
        out << "    bfi " << mir_reg(def->reg, 64u) << ", "
            << mir_operand(function, *uses[0], 64u) << ", #"
            << lsb << ", #" << width << "\n";
    }

    static void emit_bitfield_extract(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        if (!def || uses.size() < 3u) return;
        const uint64_t lsb = uses[1]->kind == dgtl::machine::MachineOperandKind::Immediate ? (uses[1]->immediate & 63U) : 0;
        const uint64_t width = uses[2]->kind == dgtl::machine::MachineOperandKind::Immediate ? ((uses[2]->immediate & 63U) + 1U) : 1;
        out << "    ubfx " << mir_reg(def->reg, 64u) << ", "
            << mir_operand(function, *uses[0], 64u) << ", #"
            << lsb << ", #" << width << "\n";
    }

    static void emit_fallback(std::ostream& out, dgtl::machine::MachineOpcode opcode) {
        out << "    nop // [DGTL-EMIT-FALLBACK] MachineOpcode=#" << static_cast<uint32_t>(opcode) << "\n";
    }

    static const char* runtime_symbol_for_opcode(dgtl::machine::MachineOpcode opcode) {
        return dgtl::mir::runtime_contract::runtime_symbol_for_opcode(opcode);
    }

    static bool emit_runtime_fastpath(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        const auto semantic_kind = instruction.semantic_kind;
        const auto opcode = instruction.opcode;

        if (opcode == dgtl::machine::GM_ACQUIRE_FENCE) {
            out << "    dmb ishld\n";
            return true;
        }
        if (semantic_kind == dgtl::machine::MachineSemanticKind::HardwareFence ||
            semantic_kind == dgtl::machine::MachineSemanticKind::ConcurrentSync ||
            opcode == dgtl::machine::GM_RELEASE_FENCE || opcode == dgtl::machine::GM_ATOMIC_FENCE ||
            opcode == dgtl::machine::GM_FENCE || opcode == dgtl::machine::GM_BARRIER_SYNC || opcode == dgtl::machine::GM_SYNC_BARRIER) {
            out << "    dmb ish\n";
            return true;
        }
        if (opcode == dgtl::machine::GM_PREFETCH || opcode == dgtl::machine::GM_PREFETCH_T0) {
            if (uses.empty()) return false;
            out << "    prfm pldl1keep, [" << mir_operand(function, *uses[0]) << "]\n";
            return true;
        }
        if (opcode == dgtl::machine::GM_READ_TSC) {
            if (!def) return false;
            out << "    mrs " << mir_reg(def->reg, 64u) << ", cntvct_el0\n";
            return true;
        }
        if (opcode == dgtl::machine::GM_PTR_DIFF) {
            if (!def || uses.size() < 2u) return false;
            out << "    sub " << mir_reg(def->reg, 64u) << ", "
                << mir_operand(function, *uses[0], 64u) << ", "
                << mir_operand(function, *uses[1], 64u) << "\n";
            return true;
        }
        if (opcode == dgtl::machine::GM_GEP || opcode == dgtl::machine::GM_LEA) {
            if (!def || uses.size() < 2u) return false;
            out << "    add " << mir_reg(def->reg, 64u) << ", "
                << mir_operand(function, *uses[0], 64u) << ", "
                << mir_operand(function, *uses[1], 64u) << "\n";
            return true;
        }
        return false;
    }

    static void emit_runtime_call(
        std::ostream& out,
        const char* symbol,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        for (std::size_t i = 0; i < std::min<std::size_t>(uses.size(), 3u); ++i) {
            out << "    mov x" << i << ", " << mir_operand(function, *uses[i], 64u) << "\n";
        }
        out << "    bl " << symbol << "\n";
        if (def) {
            out << "    mov " << mir_reg(def->reg, 64u) << ", x0\n";
        }
    }

    static void emit_ret(
        std::ostream& out,
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction) {
        const auto uses = mir_uses(instruction);
        if (!uses.empty()) {
            if (uses[0]->kind == dgtl::machine::MachineOperandKind::Register) {
                const auto& value = *uses[0];
                if (value.reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
                    const std::uint32_t width = value.subregister == 32u ? 32u : 64u;
                    out << "    fmov " << (width == 32u ? "s0" : "d0") << ", "
                        << mir_reg(value.reg, width) << "\n";
                } else if (value.reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
                    out << "    mov v0.16b, " << mir_reg(value.reg, 128u) << ".16b\n";
                } else {
                    out << "    mov x0, " << mir_reg(value.reg, 64u) << "\n";
                }
            } else if (uses[0]->kind == dgtl::machine::MachineOperandKind::Immediate) {
                emit_mov_imm64(out, 0u, static_cast<std::uint64_t>(uses[0]->immediate));
            }
        }
        out << "    b .L_epilogue\n";
    }

    static bool emit_mir_instruction(
        const dgtl::machine::MachineFunction& function,
        const dgtl::machine::MachineInstruction& instruction,
        std::ostream& out) {

        using namespace dgtl::machine;

        const auto* def = mir_def(instruction);
        const auto uses = mir_uses(instruction);
        const auto semantic_kind = instruction.semantic_kind;
        const auto opcode = instruction.opcode;

        switch (instruction.form) {
            case MachineForm::NoOp:
                if (emit_runtime_fastpath(out, function, instruction)) {
                    return true;
                }
                return true;

            case MachineForm::Move: {
                if (emit_runtime_fastpath(out, function, instruction)) {
                    return true;
                }
                emit_move(out, function, instruction);
                return true;
            }

            case MachineForm::LoadImmediate: {
                if (!def || uses.empty()) return false;
                emit_load_imm(out, instruction);
                if (def->reg.reg_class == RegisterClassKind::FloatingPoint) {
                    const std::uint32_t width = def->subregister == 32u ? 32u : 64u;
                    out << "    fmov " << mir_reg(def->reg, width) << ", x16\n";
                }
                return true;
            }

            case MachineForm::LoadStack: {
                emit_load(out, function, instruction);
                return true;
            }

            case MachineForm::StoreStack: {
                if (opcode == GM_ATOMIC_STORE) return false;
                emit_store(out, function, instruction);
                return true;
            }

            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                const bool is32 = (instruction.form == MachineForm::IntegerBinary32);
                if (semantic_kind == MachineSemanticKind::IntegerMod || opcode == GM_IREM32 || opcode == GM_IREM64) {
                    emit_mod_i32(out, function, instruction);
                    return true;
                }
                if (semantic_kind == MachineSemanticKind::IntegerShl || opcode == GM_ISHL32 || opcode == GM_ISHL64) {
                    emit_shift_left(out, function, instruction);
                    return true;
                }
                if (semantic_kind == MachineSemanticKind::IntegerShr || opcode == GM_ISHR32 || opcode == GM_ISHR64) {
                    emit_shift_right(out, function, instruction);
                    return true;
                }
                const char* mnemonic = "add";
                if (semantic_kind == MachineSemanticKind::IntegerSub || opcode == GM_ISUB32 || opcode == GM_ISUB64) mnemonic = "sub";
                else if (semantic_kind == MachineSemanticKind::IntegerMul || opcode == GM_IMUL32 || opcode == GM_IMUL64) mnemonic = "mul";
                else if (semantic_kind == MachineSemanticKind::IntegerDiv || opcode == GM_IDIV32 || opcode == GM_IDIV64) mnemonic = "sdiv";
                else if (semantic_kind == MachineSemanticKind::IntegerAnd || opcode == GM_IAND32 || opcode == GM_IAND64) mnemonic = "and";
                else if (semantic_kind == MachineSemanticKind::IntegerOr  || opcode == GM_IOR32  || opcode == GM_IOR64)  mnemonic = "orr";
                else if (semantic_kind == MachineSemanticKind::IntegerXor || opcode == GM_IXOR32 || opcode == GM_IXOR64) mnemonic = "eor";
                emit_arithmetic(out, mnemonic, function, instruction, false, !is32);
                return true;
            }

            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64: {
                const bool is32 = (instruction.form == MachineForm::FloatBinary32);
                const char* mnemonic = "fadd";
                if (semantic_kind == MachineSemanticKind::FloatSub || opcode == GM_FSUB32 || opcode == GM_FSUB64) mnemonic = "fsub";
                else if (semantic_kind == MachineSemanticKind::FloatMul || opcode == GM_FMUL32 || opcode == GM_FMUL64) mnemonic = "fmul";
                else if (semantic_kind == MachineSemanticKind::FloatDiv || opcode == GM_FDIV32 || opcode == GM_FDIV64) mnemonic = "fdiv";
                if (is32) {
                    emit_arithmetic(out, mnemonic, function, instruction, true, false);
                } else {
                    emit_arithmetic_f64(out, mnemonic, function, instruction);
                }
                return true;
            }

            case MachineForm::FloatTernaryFma32: {
                emit_fma(out, function, instruction);
                return true;
            }

            case MachineForm::Compare32: {
                const char* condition = arm64_condition_string(instruction.comparison_predicate());
                emit_cmp_set(out, condition, function, instruction);
                return true;
            }

            case MachineForm::VectorBinary512: {
                emit_neon_vec_op(out, "fadd", function, instruction);
                return true;
            }

            case MachineForm::VectorStore512: {
                if (uses.size() < 2u) return false;
                const auto& value = *uses[0];
                const auto address = mir_operand(function, *uses[1]);
                if (value.kind != MachineOperandKind::Register ||
                    value.reg.reg_class != RegisterClassKind::Vector ||
                    address.empty()) return false;
                out << "    str q" << (value.reg.id - 32u) << ", " << address << "\n";
                return true;
            }

            case MachineForm::Goto: {
                emit_branch(out, function, instruction);
                return true;
            }

            case MachineForm::BranchCond: {
                emit_branch_cond(out, function, instruction);
                return true;
            }

            case MachineForm::BranchCompare: {
                emit_branch_compare_fused(out, function, instruction);
                return true;
            }

            case MachineForm::Return: {
                emit_ret(out, function, instruction);
                return true;
            }

            case MachineForm::Call:
                return emit_mir_call(function, instruction, out);

            case MachineForm::Syscall:
                emit_syscall(out);
                return true;

            case MachineForm::HardwareFence:
            case MachineForm::ConcurrentSync:
                out << "    dmb ish\n";
                return true;

            case MachineForm::AtomicOp:
                return false;

            case MachineForm::VectorGather512:
            case MachineForm::TensorOp:
            case MachineForm::Unhandled:
            default:
                if (emit_runtime_fastpath(out, function, instruction)) {
                    return true;
                }
                if (const char* sym = runtime_symbol_for_opcode(opcode)) {
                    emit_runtime_call(out, sym, function, instruction);
                    return true;
                }
                emit_fallback(out, opcode);
                return false;
        }
    }

    static void emit_header(std::ostream& out, const Config& config) {
        out << "// --- DGTL ARM64 BACKEND ---\n";
        out << ".arch armv8-a\n.text\n.align 4\n.global _DGTL_entry\n\n_DGTL_entry:\n";
        if (!config.omit_fp) out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n";
    }

    static void emit_footer(std::ostream& out, const Config& config) {
        out << ".L_epilogue:\n";
        if (!config.omit_fp) out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }
};

} // namespace dgtl::isa::arm64

// This alias is CRITICAL. main.cpp uses ir::ARM64Emitter.
export namespace dgtl::isa {
    using ARM64Emitter = dgtl::isa::arm64::ARM64Emitter;
}
export namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}
