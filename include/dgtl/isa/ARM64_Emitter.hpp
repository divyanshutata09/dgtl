/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 4: BACKEND EMISSION - ARM64 / AARCH64
 * FINAL SYNC: GCC/MinGW Default Initializer Fix & Namespace Alignment
 */

#pragma once
#include <cstdint>
#include <ostream>
#include <iomanip>
#include <string>

#include "../ir/Global_IR.hpp"
#include "../core/Target.hpp"
#include "../core/Symbol_Table.hpp"

namespace dgtl::isa {

// Bring in IR types for local scope visibility
using namespace dgtl::ir;

class alignas(64) ARM64Emitter {
public:
    struct Config {
        bool use_neon       = true;
        bool position_indep  = true; 
        bool omit_fp        = true; 
        bool verbose_asm    = true;
        
        // Explicit constructor prevents MinGW "required before end of class" error
        constexpr Config() noexcept {} 
    };

    explicit ARM64Emitter(const IRStream& stream, const SymbolTable& symbols)
        : m_stream(stream), m_symbols(symbols) {}

    /**
     * @brief Generates AArch64 Assembly.
     * Fixed: Use brace-init for default to byopt GCC's incomplete type check on default params.
     */
    void emit_assembly(std::ostream& out, const Config config = {}) {
        if (m_stream.is_null() || m_stream.size() == 0) return;

        emit_header(out, config);

        for (size_t i = 0; i < m_stream.size(); ++i) {
            const auto& atom = m_stream.get_atom_readonly(i);
            const auto& core = m_stream.get_core_readonly(i);
            
            if (atom.opcode == OpCode::NOP) continue;

            out << "    "; 
            const bool is_simd = (atom.flags & Flags::IS_SIMD_BURST);

            switch (atom.opcode) {
                case OpCode::ADD_I32:   emit_arithmetic(out, "add", core); break;
                case OpCode::SUB_I32:   emit_arithmetic(out, "sub", core); break;
                case OpCode::MUL_I32:   emit_arithmetic(out, "mul", core); break;
                case OpCode::ADD_F32:   emit_arithmetic(out, "fadd", core, true); break;
                case OpCode::SUB_F32:   emit_arithmetic(out, "fsub", core, true); break;
                case OpCode::LOAD:      emit_load(out, core); break;
                case OpCode::STORE:     emit_store(out, core); break;
                case OpCode::RET:       emit_ret(out, core); break;
                case OpCode::VEC_ADD_F32_512: 
                    if (config.use_neon || is_simd) emit_neon_vec_op(out, "fadd", core);
                    break;
                default:
                    out << "// [DGTL-EMIT-ERR] Unhandled: " << static_cast<uint32_t>(atom.opcode);
                    break;
            }

            if (config.verbose_asm) {
                out << std::setw(28) << std::right << " // TR_HASH:0x" 
                    << std::hex << std::uppercase << core.intent_hash << std::dec << std::nouppercase;
            }
            out << "\n";
        }
        emit_footer(out, config);
    }

private:
    const IRStream& m_stream;
    const SymbolTable& m_symbols;

    void emit_header(std::ostream& out, const Config& config) {
        out << "// --- DGTL-MLIR-X ARM64 BACKEND ---\n";
        out << ".arch armv8-a\n.text\n.align 4\n.global _DGTL_entry\n\n_DGTL_entry:\n";
        if (!config.omit_fp) out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n";
    }

    void emit_footer(std::ostream& out, const Config& config) {
        if (!config.omit_fp) out << "    ldp x29, x30, [sp], #16\n";
        out << "    ret\n";
    }

    void emit_arithmetic(std::ostream& out, const char* instr, const IRCore& core, bool is_fp = false) {
        char prefix = is_fp ? 's' : 'x'; 
        out << instr << " " << prefix << core.op_c << ", " << prefix << core.op_a << ", " << prefix << core.op_b;
    }

    void emit_load(std::ostream& out, const IRCore& core) {
        out << "ldr x" << core.op_c << ", [x" << core.op_a << ", #" << core.op_b << "]";
    }

    void emit_store(std::ostream& out, const IRCore& core) {
        out << "str x" << core.op_a << ", [x" << core.op_b << ", #" << core.op_c << "]";
    }

    void emit_neon_vec_op(std::ostream& out, const char* instr, const IRCore& core) {
        out << instr << " v" << core.op_c << ".4s, v" << core.op_a << ".4s, v" << core.op_b << ".4s";
    }

    void emit_ret(std::ostream& out, const IRCore& core) {
        if (core.op_a != 0xFF && core.op_a != 0) {
            out << "mov x0, x" << core.op_a << "\n    ";
        }
    }
};

} // namespace dgtl::isa

// This alias is CRITICAL. main.cpp uses ir::ARM64Emitter.
namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}

namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}
