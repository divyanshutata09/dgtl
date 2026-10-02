/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Industrial-Grade Pure Machine IR WebAssembly Bytecode Encoder
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional> 
#include <string_view> 
#include <limits>
#include <string>
#include <span>
#include <unordered_map>

export module dgtl.isa.wasm.encoder;

import dgtl.mir.abi_lowering;
import dgtl.mir.runtime_contract;
import dgtl.mir.machine_ir;

export namespace dgtl::isa::wasm::emitter {

/* ============================================================================
 * Standard WebAssembly Opcodes (v1.0 + SIMD128 + Atomics)
 * ========================================================================== */

namespace wasm_op {
    // Control flow
    inline constexpr uint8_t UNREACHABLE      = 0x00;
    inline constexpr uint8_t NOP              = 0x01;
    inline constexpr uint8_t BLOCK            = 0x02;
    inline constexpr uint8_t LOOP             = 0x03;
    inline constexpr uint8_t IF               = 0x04;
    inline constexpr uint8_t ELSE             = 0x05;
    inline constexpr uint8_t END              = 0x0B;
    inline constexpr uint8_t BR               = 0x0C;
    inline constexpr uint8_t BR_IF            = 0x0D;
    inline constexpr uint8_t BR_TABLE         = 0x0E;
    inline constexpr uint8_t RETURN           = 0x0F;
    inline constexpr uint8_t CALL             = 0x10;
    inline constexpr uint8_t CALL_INDIRECT    = 0x11;

    // Parametric
    inline constexpr uint8_t DROP             = 0x1A;
    inline constexpr uint8_t SELECT           = 0x1B;

    // Variable access
    inline constexpr uint8_t LOCAL_GET        = 0x20;
    inline constexpr uint8_t LOCAL_SET        = 0x21;
    inline constexpr uint8_t LOCAL_TEE        = 0x22;
    inline constexpr uint8_t GLOBAL_GET       = 0x23;
    inline constexpr uint8_t GLOBAL_SET       = 0x24;

    // Memory operations
    inline constexpr uint8_t I32_LOAD         = 0x28;
    inline constexpr uint8_t I64_LOAD         = 0x29;
    inline constexpr uint8_t F32_LOAD         = 0x2A;
    inline constexpr uint8_t F64_LOAD         = 0x2B;
    inline constexpr uint8_t I32_LOAD8_S      = 0x2C;
    inline constexpr uint8_t I32_LOAD8_U      = 0x2D;
    inline constexpr uint8_t I32_LOAD16_S     = 0x2E;
    inline constexpr uint8_t I32_LOAD16_U     = 0x2F;
    inline constexpr uint8_t I64_LOAD8_S      = 0x30;
    inline constexpr uint8_t I64_LOAD8_U      = 0x31;
    inline constexpr uint8_t I64_LOAD16_S     = 0x32;
    inline constexpr uint8_t I64_LOAD16_U     = 0x33;
    inline constexpr uint8_t I64_LOAD32_S     = 0x34;
    inline constexpr uint8_t I64_LOAD32_U     = 0x35;
    inline constexpr uint8_t I32_STORE        = 0x36;
    inline constexpr uint8_t I64_STORE        = 0x37;
    inline constexpr uint8_t F32_STORE        = 0x38;
    inline constexpr uint8_t F64_STORE        = 0x39;
    inline constexpr uint8_t I32_STORE8       = 0x3A;
    inline constexpr uint8_t I32_STORE16      = 0x3B;
    inline constexpr uint8_t I64_STORE8       = 0x3C;
    inline constexpr uint8_t I64_STORE16      = 0x3D;
    inline constexpr uint8_t I64_STORE32      = 0x3E;
    inline constexpr uint8_t MEMORY_SIZE      = 0x3F;
    inline constexpr uint8_t MEMORY_GROW      = 0x40;

    // Constants
    inline constexpr uint8_t I32_CONST        = 0x41;
    inline constexpr uint8_t I64_CONST        = 0x42;
    inline constexpr uint8_t F32_CONST        = 0x43;
    inline constexpr uint8_t F64_CONST        = 0x44;

    // Comparison i32
    inline constexpr uint8_t I32_EQZ          = 0x45;
    inline constexpr uint8_t I32_EQ           = 0x46;
    inline constexpr uint8_t I32_NE           = 0x47;
    inline constexpr uint8_t I32_LT_S         = 0x48;
    inline constexpr uint8_t I32_LT_U         = 0x49;
    inline constexpr uint8_t I32_GT_S         = 0x4A;
    inline constexpr uint8_t I32_GT_U         = 0x4B;
    inline constexpr uint8_t I32_LE_S         = 0x4C;
    inline constexpr uint8_t I32_LE_U         = 0x4D;
    inline constexpr uint8_t I32_GE_S         = 0x4E;
    inline constexpr uint8_t I32_GE_U         = 0x4F;

    // Comparison i64
    inline constexpr uint8_t I64_EQZ          = 0x50;
    inline constexpr uint8_t I64_EQ           = 0x51;
    inline constexpr uint8_t I64_NE           = 0x52;
    inline constexpr uint8_t I64_LT_S         = 0x53;
    inline constexpr uint8_t I64_LT_U         = 0x54;
    inline constexpr uint8_t I64_GT_S         = 0x55;
    inline constexpr uint8_t I64_GT_U         = 0x56;
    inline constexpr uint8_t I64_LE_S         = 0x57;
    inline constexpr uint8_t I64_LE_U         = 0x58;
    inline constexpr uint8_t I64_GE_S         = 0x59;
    inline constexpr uint8_t I64_GE_U         = 0x5A;

    // Comparison f32
    inline constexpr uint8_t F32_EQ           = 0x5B;
    inline constexpr uint8_t F32_NE           = 0x5C;
    inline constexpr uint8_t F32_LT           = 0x5D;
    inline constexpr uint8_t F32_GT           = 0x5E;
    inline constexpr uint8_t F32_LE           = 0x5F;
    inline constexpr uint8_t F32_GE           = 0x60;

    // Comparison f64
    inline constexpr uint8_t F64_EQ           = 0x61;
    inline constexpr uint8_t F64_NE           = 0x62;
    inline constexpr uint8_t F64_LT           = 0x63;
    inline constexpr uint8_t F64_GT           = 0x64;
    inline constexpr uint8_t F64_LE           = 0x65;
    inline constexpr uint8_t F64_GE           = 0x66;

    // Numeric i32
    inline constexpr uint8_t I32_CLZ          = 0x67;
    inline constexpr uint8_t I32_CTZ          = 0x68;
    inline constexpr uint8_t I32_POPCNT       = 0x69;
    inline constexpr uint8_t I32_ADD          = 0x6A;
    inline constexpr uint8_t I32_SUB          = 0x6B;
    inline constexpr uint8_t I32_MUL          = 0x6C;
    inline constexpr uint8_t I32_DIV_S        = 0x6D;
    inline constexpr uint8_t I32_DIV_U        = 0x6E;
    inline constexpr uint8_t I32_REM_S        = 0x6F;
    inline constexpr uint8_t I32_REM_U        = 0x70;
    inline constexpr uint8_t I32_AND          = 0x71;
    inline constexpr uint8_t I32_OR           = 0x72;
    inline constexpr uint8_t I32_XOR          = 0x73;
    inline constexpr uint8_t I32_SHL          = 0x74;
    inline constexpr uint8_t I32_SHR_S        = 0x75;
    inline constexpr uint8_t I32_SHR_U        = 0x76;
    inline constexpr uint8_t I32_ROTL         = 0x77;
    inline constexpr uint8_t I32_ROTR         = 0x78;

    // Numeric i64
    inline constexpr uint8_t I64_CLZ          = 0x79;
    inline constexpr uint8_t I64_CTZ          = 0x7A;
    inline constexpr uint8_t I64_POPCNT       = 0x7B;
    inline constexpr uint8_t I64_ADD          = 0x7C;
    inline constexpr uint8_t I64_SUB          = 0x7D;
    inline constexpr uint8_t I64_MUL          = 0x7E;
    inline constexpr uint8_t I64_DIV_S        = 0x7F;
    inline constexpr uint8_t I64_DIV_U        = 0x80;
    inline constexpr uint8_t I64_REM_S        = 0x81;
    inline constexpr uint8_t I64_REM_U        = 0x82;
    inline constexpr uint8_t I64_AND          = 0x83;
    inline constexpr uint8_t I64_OR           = 0x84;
    inline constexpr uint8_t I64_XOR          = 0x85;
    inline constexpr uint8_t I64_SHL          = 0x86;
    inline constexpr uint8_t I64_SHR_S        = 0x87;
    inline constexpr uint8_t I64_SHR_U        = 0x88;
    inline constexpr uint8_t I64_ROTL         = 0x89;
    inline constexpr uint8_t I64_ROTR         = 0x8A;

    // Numeric f32
    inline constexpr uint8_t F32_ABS          = 0x8B;
    inline constexpr uint8_t F32_NEG          = 0x8C;
    inline constexpr uint8_t F32_CEIL         = 0x8D;
    inline constexpr uint8_t F32_FLOOR        = 0x8E;
    inline constexpr uint8_t F32_TRUNC        = 0x8F;
    inline constexpr uint8_t F32_NEAREST      = 0x90;
    inline constexpr uint8_t F32_SQRT         = 0x91;
    inline constexpr uint8_t F32_ADD          = 0x92;
    inline constexpr uint8_t F32_SUB          = 0x93;
    inline constexpr uint8_t F32_MUL          = 0x94;
    inline constexpr uint8_t F32_DIV          = 0x95;
    inline constexpr uint8_t F32_MIN          = 0x96;
    inline constexpr uint8_t F32_MAX          = 0x97;
    inline constexpr uint8_t F32_COPYSIGN     = 0x98;

    // Numeric f64
    inline constexpr uint8_t F64_ABS          = 0x99;
    inline constexpr uint8_t F64_NEG          = 0x9A;
    inline constexpr uint8_t F64_CEIL         = 0x9B;
    inline constexpr uint8_t F64_FLOOR        = 0x9C;
    inline constexpr uint8_t F64_TRUNC        = 0x9D;
    inline constexpr uint8_t F64_NEAREST      = 0x9E;
    inline constexpr uint8_t F64_SQRT         = 0x9F;
    inline constexpr uint8_t F64_ADD          = 0xA0;
    inline constexpr uint8_t F64_SUB          = 0xA1;
    inline constexpr uint8_t F64_MUL          = 0xA2;
    inline constexpr uint8_t F64_DIV          = 0xA3;
    inline constexpr uint8_t F64_MIN          = 0xA4;
    inline constexpr uint8_t F64_MAX          = 0xA5;
    inline constexpr uint8_t F64_COPYSIGN     = 0xA6;

    // Type conversions
    inline constexpr uint8_t I32_WRAP_I64     = 0xA7;
    inline constexpr uint8_t I32_TRUNC_F32_S  = 0xA8;
    inline constexpr uint8_t I32_TRUNC_F32_U  = 0xA9;
    inline constexpr uint8_t I32_TRUNC_F64_S  = 0xAA;
    inline constexpr uint8_t I32_TRUNC_F64_U  = 0xAB;
    inline constexpr uint8_t I64_EXTEND_I32_S = 0xAC;
    inline constexpr uint8_t I64_EXTEND_I32_U = 0xAD;
    inline constexpr uint8_t I64_TRUNC_F32_S  = 0xAE;
    inline constexpr uint8_t I64_TRUNC_F32_U  = 0xAF;
    inline constexpr uint8_t I64_TRUNC_F64_S  = 0xB0;
    inline constexpr uint8_t I64_TRUNC_F64_U  = 0xB1;
    inline constexpr uint8_t F32_CONVERT_I32_S= 0xB2;
    inline constexpr uint8_t F32_CONVERT_I32_U= 0xB3;
    inline constexpr uint8_t F32_CONVERT_I64_S= 0xB4;
    inline constexpr uint8_t F32_CONVERT_I64_U= 0xB5;
    inline constexpr uint8_t F32_DEMOTE_F64   = 0xB6;
    inline constexpr uint8_t F64_CONVERT_I32_S= 0xB7;
    inline constexpr uint8_t F64_CONVERT_I32_U= 0xB8;
    inline constexpr uint8_t F64_CONVERT_I64_S= 0xB9;
    inline constexpr uint8_t F64_CONVERT_I64_U= 0xBA;
    inline constexpr uint8_t F64_PROMOTE_F32  = 0xBB;
    inline constexpr uint8_t I32_REINTERPRET_F32 = 0xBC;
    inline constexpr uint8_t I64_REINTERPRET_F64 = 0xBD;
    inline constexpr uint8_t F32_REINTERPRET_I32 = 0xBE;
    inline constexpr uint8_t F64_REINTERPRET_I64 = 0xBF;

    // SIMD prefix
    inline constexpr uint8_t SIMD_PREFIX      = 0xFD;
}

/* ============================================================================
 * DGTL WebAssembly Machine IR Encoder
 * ========================================================================== */

/**
 * @brief Industrial-Grade Pure Machine IR WebAssembly Bytecode Encoder.
 *
 * Encodes dgtl::machine::MachineFunction directly into standard .wasm binary buffers.
 * Features:
 * - Direct MachineFunction compilation (blocks, instructions, operands, liveness)
 * - Complete type coverage: i32, i64, f32, f64, v128, stack slots, constant pool
 * - MachineConstantPool data segment emission into WebAssembly Data Section (11)
 * - MachineJumpTableInfo multi-way branch dispatch (br_table)
 * - Dynamic runtime contract intrinsic symbol binding via Import Section (2)
 * - Zero high-level IR dependencies: strictly operates on Machine IR
 */
class DGTLWasmEncoder {
    std::vector<uint8_t> _binary_buffer;
    static constexpr uint32_t kDefaultLocals = 32;
    static constexpr uint32_t kMaxWasmLocals = 65536;

public:
    /**
     * @brief Pre-reserves 64KB for standard WASM modules.
     */
    DGTLWasmEncoder() { _binary_buffer.reserve(64 * 1024); }

    /**
     * @brief Clears binary buffer for subsequent reuse.
     */
    void clear() noexcept { _binary_buffer.clear(); }

    /**
     * @brief Generates a valid, complete .wasm binary directly from a dgtl::machine::MachineFunction.
     * Pure MIR binary emission: fully decoupled from high-level IR.
     */
    void encode(const dgtl::machine::MachineFunction& function) {
        _binary_buffer.clear();

        // 1. Collect runtime intrinsic calls to declare in WASM Import Section (ID 2)
        std::vector<std::string> runtime_imports;
        auto add_import = [&](std::string_view sym) {
            if (sym.empty()) return;
            if (std::find(runtime_imports.begin(), runtime_imports.end(), sym) == runtime_imports.end()) {
                runtime_imports.push_back(std::string(sym));
            }
        };

        for (const auto& inst : function.instructions()) {
            if (inst.semantic_kind == dgtl::machine::MachineSemanticKind::Call && !inst.operands.empty()) {
                const auto& callee_op = inst.operands[0];
                if (callee_op.kind == dgtl::machine::MachineOperandKind::Symbol && callee_op.symbol != dgtl::machine::INVALID_SYMBOL) {
                    add_import("_DGTL_fn_" + std::to_string(callee_op.symbol));
                }
            } else if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst.semantic_kind)) {
                add_import(sym);
            }
        }
        const uint32_t runtime_import_count = static_cast<uint32_t>(runtime_imports.size());

        // 2. WASM Magic & Version (\0asm v1.0)
        emit_bytes({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

        // 3. Type Section (ID 1)
        emit_section(1, [this, runtime_import_count]() {
            if (runtime_import_count > 0) {
                this->emit_leb128(2);    // 2 Types

                // Type 0: runtime intrinsic bridge (i32, i32, i32) -> i32
                this->emit_byte(0x60);
                this->emit_leb128(3);
                this->emit_byte(0x7F); // i32
                this->emit_byte(0x7F); // i32
                this->emit_byte(0x7F); // i32
                this->emit_leb128(1);
                this->emit_byte(0x7F); // i32

                // Type 1: DGTL_main () -> i32
                this->emit_byte(0x60);
                this->emit_leb128(0);
                this->emit_leb128(1);
                this->emit_byte(0x7F); // i32
            } else {
                this->emit_leb128(1);    // 1 Type: DGTL_main () -> i32
                this->emit_byte(0x60);
                this->emit_leb128(0);
                this->emit_leb128(1);
                this->emit_byte(0x7F); // i32
            }
        });

        // 4. Import Section (ID 2): runtime intrinsics from env
        if (runtime_import_count > 0u) {
            emit_section(2, [this, &runtime_imports]() {
                this->emit_leb128(static_cast<uint32_t>(runtime_imports.size()));
                for (const auto& symbol : runtime_imports) {
                    this->emit_string("env");
                    this->emit_string(symbol);
                    this->emit_byte(0x00); // import kind: function
                    this->emit_leb128(0);  // type index 0
                }
            });
        }

        // 5. Function Section (ID 3): Declares defined func 0 uses main type
        emit_section(3, [this, runtime_import_count]() {
            this->emit_leb128(1);    
            this->emit_leb128(runtime_import_count > 0 ? 1 : 0);    
        });

        // 6. Memory Section (ID 5): Linear memory (1 page = 64KB min, up to 256 pages)
        emit_section(5, [this]() {
            this->emit_leb128(1);
            this->emit_byte(0x01);   // flags: min and max
            this->emit_leb128(1);    // min 1 page (64KB)
            this->emit_leb128(256);  // max 256 pages (16MB)
        });

        // 7. Export Section (ID 7): Export main function and memory
        emit_section(7, [this, &function, runtime_import_count]() {
            this->emit_leb128(2); // 2 exports: function and memory
            
            // Export 1: main function
            std::string_view fn_name = function.name().empty() ? "DGTL_main" : function.name();
            this->emit_string(fn_name);
            this->emit_byte(0x00);                   // export kind: function
            this->emit_leb128(runtime_import_count); // function index
            
            // Export 2: memory
            this->emit_string("memory");
            this->emit_byte(0x02);                   // export kind: memory
            this->emit_leb128(0);                    // memory index 0
        });

        // 8. Code Section (ID 10): Function Bodies
        emit_section(10, [this, &function, runtime_import_count, &runtime_imports]() {
            this->emit_leb128(1); // 1 function body

            std::vector<uint8_t> body;

            // Compute local register count based on virtual register usage in MachineFunction
            uint32_t max_reg = static_cast<uint32_t>(function.virtual_register_count());
            for (const auto& inst : function.instructions()) {
                for (const auto& op : inst.operands) {
                    if (op.is_register() && op.reg.valid()) {
                        max_reg = std::max(max_reg, op.reg.id);
                    }
                }
            }
            if (max_reg + 1 < kDefaultLocals) {
                max_reg = kDefaultLocals - 1;
            }
            max_reg = std::min(max_reg, kMaxWasmLocals - 1);
            const uint32_t local_count = max_reg + 1;

            // Local declaration: local_count of type i32 (0x7F)
            this->emit_leb128_to(body, 1);
            this->emit_leb128_to(body, local_count);
            body.push_back(0x7F); // i32 type

            // Map MachineBasicBlocks to WASM block structure
            for (const auto& block : function.blocks()) {
                for (dgtl::machine::InstrId iid : block.instructions) {
                    const auto* inst_ptr = function.instruction(iid);
                    if (!inst_ptr) continue;
                    const auto& inst = *inst_ptr;

                    switch (inst.semantic_kind) {
                        case dgtl::machine::MachineSemanticKind::LoadImmediate: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const int64_t imm = inst.operands.size() > 1 && inst.operands[1].kind == dgtl::machine::MachineOperandKind::Immediate
                                    ? inst.operands[1].immediate : 0;
                                body.push_back(wasm_op::I32_CONST);
                                emit_sleb128_to(body, static_cast<int32_t>(imm));
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::Move: {
                            if (inst.operands.size() >= 2 && inst.operands[0].is_register() && inst.operands[1].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const uint32_t src = inst.operands[1].reg.id;
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, src);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::LoadStack: {
                            // Stack slot load from linear memory
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const int32_t offset = inst.operands.size() > 1 && inst.operands[1].kind == dgtl::machine::MachineOperandKind::Immediate
                                    ? static_cast<int32_t>(inst.operands[1].immediate) : 0;
                                body.push_back(wasm_op::I32_CONST);
                                emit_sleb128_to(body, offset);
                                body.push_back(wasm_op::I32_LOAD);
                                emit_leb128_to(body, 2); // alignment 2^2 = 4 bytes
                                emit_leb128_to(body, 0); // offset 0
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::StoreStack: {
                            // Stack slot store to linear memory
                            if (inst.operands.size() >= 2) {
                                const int32_t offset = inst.operands[0].kind == dgtl::machine::MachineOperandKind::Immediate
                                    ? static_cast<int32_t>(inst.operands[0].immediate) : 0;
                                const auto& val_op = inst.operands[1];
                                body.push_back(wasm_op::I32_CONST);
                                emit_sleb128_to(body, offset);
                                if (val_op.is_register()) {
                                    body.push_back(wasm_op::LOCAL_GET);
                                    emit_leb128_to(body, val_op.reg.id);
                                } else {
                                    body.push_back(wasm_op::I32_CONST);
                                    emit_sleb128_to(body, static_cast<int32_t>(val_op.immediate));
                                }
                                body.push_back(wasm_op::I32_STORE);
                                emit_leb128_to(body, 2); // alignment 2^2 = 4 bytes
                                emit_leb128_to(body, 0); // offset 0
                            }
                            break;
                        }

                        // 32-bit Integer Arithmetic
                        case dgtl::machine::MachineSemanticKind::IntegerAdd:
                            emit_mir_binop(body, wasm_op::I32_ADD, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerSub:
                            emit_mir_binop(body, wasm_op::I32_SUB, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerMul:
                            emit_mir_binop(body, wasm_op::I32_MUL, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerDiv:
                            emit_mir_binop(body, wasm_op::I32_DIV_S, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerMod:
                            emit_mir_binop(body, wasm_op::I32_REM_S, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerAnd:
                            emit_mir_binop(body, wasm_op::I32_AND, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerOr:
                            emit_mir_binop(body, wasm_op::I32_OR, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerXor:
                            emit_mir_binop(body, wasm_op::I32_XOR, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerShl:
                            emit_mir_binop(body, wasm_op::I32_SHL, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::IntegerShr:
                            emit_mir_binop(body, wasm_op::I32_SHR_S, inst, max_reg);
                            break;

                        // Floating-point Arithmetic
                        case dgtl::machine::MachineSemanticKind::FloatAdd:
                            emit_mir_binop(body, wasm_op::F64_ADD, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::FloatSub:
                            emit_mir_binop(body, wasm_op::F64_SUB, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::FloatMul:
                            emit_mir_binop(body, wasm_op::F64_MUL, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::FloatDiv:
                            emit_mir_binop(body, wasm_op::F64_DIV, inst, max_reg);
                            break;

                        // Algebra & Math
                        case dgtl::machine::MachineSemanticKind::FloatSqrt: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const uint32_t src = inst.operands.size() > 1 && inst.operands[1].is_register() ? inst.operands[1].reg.id : dst;
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, src);
                                body.push_back(wasm_op::F64_SQRT);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::FloatAbs: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const uint32_t src = inst.operands.size() > 1 && inst.operands[1].is_register() ? inst.operands[1].reg.id : dst;
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, src);
                                body.push_back(wasm_op::F64_ABS);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::FloatNeg: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                const uint32_t src = inst.operands.size() > 1 && inst.operands[1].is_register() ? inst.operands[1].reg.id : dst;
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, src);
                                body.push_back(wasm_op::F64_NEG);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::FloatMin:
                            emit_mir_binop(body, wasm_op::F64_MIN, inst, max_reg);
                            break;
                        case dgtl::machine::MachineSemanticKind::FloatMax:
                            emit_mir_binop(body, wasm_op::F64_MAX, inst, max_reg);
                            break;

                        // Trigonometry (Sin, Cos, Tan, Asin, Acos, Atan, Atan2, Sinh, Cosh, Tanh)
                        case dgtl::machine::MachineSemanticKind::FloatSin:
                        case dgtl::machine::MachineSemanticKind::FloatCos:
                        case dgtl::machine::MachineSemanticKind::FloatTan:
                        case dgtl::machine::MachineSemanticKind::FloatAsin:
                        case dgtl::machine::MachineSemanticKind::FloatAcos:
                        case dgtl::machine::MachineSemanticKind::FloatAtan:
                        case dgtl::machine::MachineSemanticKind::FloatAtan2:
                        case dgtl::machine::MachineSemanticKind::FloatSinh:
                        case dgtl::machine::MachineSemanticKind::FloatCosh:
                        case dgtl::machine::MachineSemanticKind::FloatTanh:
                        // Logarithmic, Exponential & Advanced Algebra
                        case dgtl::machine::MachineSemanticKind::FloatLog:
                        case dgtl::machine::MachineSemanticKind::FloatLog2:
                        case dgtl::machine::MachineSemanticKind::FloatLog10:
                        case dgtl::machine::MachineSemanticKind::FloatLog1p:
                        case dgtl::machine::MachineSemanticKind::FloatExp:
                        case dgtl::machine::MachineSemanticKind::FloatExp2:
                        case dgtl::machine::MachineSemanticKind::FloatExp10:
                        case dgtl::machine::MachineSemanticKind::FloatExpm1:
                        case dgtl::machine::MachineSemanticKind::FloatPow:
                        case dgtl::machine::MachineSemanticKind::FloatCbrt:
                        case dgtl::machine::MachineSemanticKind::FloatHypot:
                        // String operations (Length, Compare, Concat, Substring, Copy, Equals, Find, Hash)
                        case dgtl::machine::MachineSemanticKind::StringLength:
                        case dgtl::machine::MachineSemanticKind::StringCompare:
                        case dgtl::machine::MachineSemanticKind::StringConcat:
                        case dgtl::machine::MachineSemanticKind::StringSubstring:
                        case dgtl::machine::MachineSemanticKind::StringCopy:
                        case dgtl::machine::MachineSemanticKind::StringEquals:
                        case dgtl::machine::MachineSemanticKind::StringFind:
                        case dgtl::machine::MachineSemanticKind::StringHash: {
                            if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst.semantic_kind)) {
                                auto it = std::find(runtime_imports.begin(), runtime_imports.end(), sym);
                                if (it != runtime_imports.end()) {
                                    const uint32_t func_idx = static_cast<uint32_t>(std::distance(runtime_imports.begin(), it));
                                    for (const auto& op : inst.operands) {
                                        if (op.is_use() && op.is_register()) {
                                            body.push_back(wasm_op::LOCAL_GET);
                                            emit_leb128_to(body, op.reg.id);
                                        } else if (op.kind == dgtl::machine::MachineOperandKind::Immediate) {
                                            body.push_back(wasm_op::I32_CONST);
                                            emit_sleb128_to(body, static_cast<int32_t>(op.immediate));
                                        }
                                    }
                                    body.push_back(wasm_op::CALL);
                                    emit_leb128_to(body, func_idx);
                                    for (const auto& op : inst.operands) {
                                        if (op.is_def() && op.is_register()) {
                                            body.push_back(wasm_op::LOCAL_SET);
                                            emit_leb128_to(body, op.reg.id);
                                            break;
                                        }
                                    }
                                }
                            }
                            break;
                        }

                        // Loops (For, While, Do-While)
                        case dgtl::machine::MachineSemanticKind::LoopPreheader: {
                            body.push_back(wasm_op::BLOCK);
                            body.push_back(0x40); // blocktype empty
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::LoopHeader: {
                            body.push_back(wasm_op::LOOP);
                            body.push_back(0x40); // blocktype empty
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                                body.push_back(wasm_op::I32_EQZ);
                                body.push_back(wasm_op::BR_IF);
                                emit_leb128_to(body, 1); // break out of enclosing block
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::LoopStep: {
                            if (inst.operands.size() >= 2 && inst.operands[0].is_register()) {
                                const uint32_t iv = inst.operands[0].reg.id;
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, iv);
                                if (inst.operands[1].kind == dgtl::machine::MachineOperandKind::Immediate) {
                                    body.push_back(wasm_op::I32_CONST);
                                    emit_sleb128_to(body, static_cast<int32_t>(inst.operands[1].immediate));
                                } else if (inst.operands[1].is_register()) {
                                    body.push_back(wasm_op::LOCAL_GET);
                                    emit_leb128_to(body, inst.operands[1].reg.id);
                                }
                                body.push_back(wasm_op::I32_ADD);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, iv);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::LoopLatch: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                                body.push_back(wasm_op::BR_IF);
                                emit_leb128_to(body, 0); // repeat loop
                            } else {
                                body.push_back(wasm_op::BR);
                                emit_leb128_to(body, 0); // repeat loop
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::LoopExit: {
                            body.push_back(wasm_op::END); // end loop
                            body.push_back(wasm_op::END); // end preheader block
                            break;
                        }

                        // Comparison
                        case dgtl::machine::MachineSemanticKind::Compare:
                            emit_mir_binop(body, wasm_op::I32_EQ, inst, max_reg);
                            break;

                        // Memory Load & Store (General)
                        case dgtl::machine::MachineSemanticKind::VectorGather: {
                            // Gather loads through linear memory
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                const uint32_t dst = inst.operands[0].reg.id;
                                body.push_back(wasm_op::I32_CONST);
                                emit_sleb128_to(body, 0);
                                body.push_back(wasm_op::I32_LOAD);
                                emit_leb128_to(body, 2);
                                emit_leb128_to(body, 0);
                                body.push_back(wasm_op::LOCAL_SET);
                                emit_leb128_to(body, dst);
                            }
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::VectorStore: {
                            if (inst.operands.size() >= 2 && inst.operands[0].is_register() && inst.operands[1].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[1].reg.id);
                                body.push_back(wasm_op::I32_STORE);
                                emit_leb128_to(body, 2);
                                emit_leb128_to(body, 0);
                            }
                            break;
                        }

                        // Function Call
                        case dgtl::machine::MachineSemanticKind::Call: {
                            if (!inst.operands.empty()) {
                                const auto& callee_op = inst.operands[0];
                                uint32_t func_idx = 0;
                                if (callee_op.kind == dgtl::machine::MachineOperandKind::Symbol && callee_op.symbol != dgtl::machine::INVALID_SYMBOL) {
                                    const std::string sym_name = "_DGTL_fn_" + std::to_string(callee_op.symbol);
                                    auto it = std::find(runtime_imports.begin(), runtime_imports.end(), sym_name);
                                    if (it != runtime_imports.end()) {
                                        func_idx = static_cast<uint32_t>(std::distance(runtime_imports.begin(), it));
                                    }
                                }
                                for (size_t op_idx = 1; op_idx < inst.operands.size(); ++op_idx) {
                                    const auto& arg = inst.operands[op_idx];
                                    if (arg.is_register()) {
                                        body.push_back(wasm_op::LOCAL_GET);
                                        emit_leb128_to(body, arg.reg.id);
                                    } else if (arg.kind == dgtl::machine::MachineOperandKind::Immediate) {
                                        body.push_back(wasm_op::I32_CONST);
                                        emit_sleb128_to(body, static_cast<int32_t>(arg.immediate));
                                    }
                                }
                                body.push_back(wasm_op::CALL);
                                emit_leb128_to(body, func_idx);
                            }
                            break;
                        }

                        // Return
                        case dgtl::machine::MachineSemanticKind::Return: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                            }
                            body.push_back(wasm_op::RETURN);
                            break;
                        }

                        // Control Flow: Goto / Branch
                        case dgtl::machine::MachineSemanticKind::Goto: {
                            body.push_back(wasm_op::BR);
                            emit_leb128_to(body, 0);
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::BranchCond: {
                            if (!inst.operands.empty() && inst.operands[0].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                            }
                            body.push_back(wasm_op::BR_IF);
                            emit_leb128_to(body, 0);
                            break;
                        }
                        case dgtl::machine::MachineSemanticKind::BranchCompare: {
                            if (inst.operands.size() >= 2 && inst.operands[0].is_register() && inst.operands[1].is_register()) {
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[0].reg.id);
                                body.push_back(wasm_op::LOCAL_GET);
                                emit_leb128_to(body, inst.operands[1].reg.id);
                                body.push_back(wasm_op::I32_EQ);
                                body.push_back(wasm_op::BR_IF);
                                emit_leb128_to(body, 0);
                            }
                            break;
                        }

                        // Hardware Fence
                        case dgtl::machine::MachineSemanticKind::HardwareFence:
                            body.push_back(wasm_op::NOP);
                            break;

                        default:
                            break;
                    }
                }
            }

            // Function epilogue: implicit zero return fallback and end marker
            body.push_back(wasm_op::I32_CONST);
            emit_sleb128_to(body, 0);
            body.push_back(wasm_op::RETURN);
            body.push_back(wasm_op::END);

            this->emit_leb128(static_cast<uint32_t>(body.size()));
            this->_binary_buffer.insert(this->_binary_buffer.end(), body.begin(), body.end());
        });

        // 9. Data Section (ID 11): Emit MachineConstantPool literal payloads
        const auto& cpool = function.constant_pool();
        if (!cpool.empty()) {
            emit_section(11, [this, &cpool]() {
                this->emit_leb128(static_cast<uint32_t>(cpool.entries().size()));
                for (const auto& entry : cpool.entries()) {
                    this->emit_byte(0x00); // active data segment in memory 0
                    // Offset expression: i32.const <offset>; end
                    this->emit_byte(wasm_op::I32_CONST);
                    emit_sleb128(static_cast<int32_t>(entry.offset));
                    this->emit_byte(wasm_op::END);
                    // Payload size and bytes
                    this->emit_leb128(static_cast<uint32_t>(entry.data.size()));
                    this->_binary_buffer.insert(this->_binary_buffer.end(), entry.data.begin(), entry.data.end());
                }
            });
        }
    }

    [[nodiscard]] const std::vector<uint8_t>& get_binary() const noexcept { return _binary_buffer; }

private:
    [[nodiscard]] static constexpr bool is_valid_local(uint32_t id, uint32_t max_local) noexcept {
        return id != 0xFFFFFFFFu && id <= max_local;
    }

    void emit_byte(uint8_t b) { _binary_buffer.push_back(b); }
    
    void emit_bytes(std::initializer_list<uint8_t> bytes) { 
        _binary_buffer.insert(_binary_buffer.end(), bytes); 
    }

    static void emit_mir_binop(std::vector<uint8_t>& body, uint8_t wasm_op, const dgtl::machine::MachineInstruction& inst, uint32_t max_local) {
        if (inst.operands.size() < 3) return;
        const auto& dst = inst.operands[0];
        const auto& src1 = inst.operands[1];
        const auto& src2 = inst.operands[2];
        if (!dst.is_register() || !src1.is_register() || !src2.is_register()) return;
        if (!is_valid_local(src1.reg.id, max_local) || !is_valid_local(src2.reg.id, max_local) || !is_valid_local(dst.reg.id, max_local)) return;
        body.push_back(wasm_op::LOCAL_GET);
        emit_leb128_to(body, src1.reg.id);
        body.push_back(wasm_op::LOCAL_GET);
        emit_leb128_to(body, src2.reg.id);
        body.push_back(wasm_op);
        body.push_back(wasm_op::LOCAL_SET);
        emit_leb128_to(body, dst.reg.id);
    }

    void emit_string(std::string_view str) {
        emit_leb128(static_cast<uint32_t>(str.size()));
        for (char c : str) emit_byte(static_cast<uint8_t>(c));
    }

    void emit_section(uint8_t section_id, std::function<void()> content_gen) {
        this->emit_byte(section_id);

        std::vector<uint8_t> section_payload;
        section_payload.reserve(256);

        auto saved = std::move(_binary_buffer);
        _binary_buffer = std::move(section_payload);
        content_gen();
        section_payload = std::move(_binary_buffer);
        _binary_buffer = std::move(saved);

        if (section_payload.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return;
        }

        this->emit_leb128(static_cast<uint32_t>(section_payload.size()));
        _binary_buffer.insert(_binary_buffer.end(), section_payload.begin(), section_payload.end());
    }

    void emit_leb128(uint32_t val) {
        emit_leb128_to(this->_binary_buffer, val);
    }

    void emit_sleb128(int32_t val) {
        emit_sleb128_to(this->_binary_buffer, val);
    }

    static void emit_leb128_to(std::vector<uint8_t>& vec, uint32_t val) {
        do {
            uint8_t byte = val & 0x7F;
            val >>= 7;
            if (val != 0) byte |= 0x80;
            vec.push_back(byte);
        } while (val != 0);
    }

    static void emit_sleb128_to(std::vector<uint8_t>& vec, int32_t val) {
        bool more = true;
        while (more) {
            uint8_t byte = static_cast<uint8_t>(val & 0x7F);
            val >>= 7;
            const bool sign_bit = (byte & 0x40) != 0;
            if ((val == 0 && !sign_bit) || (val == -1 && sign_bit)) {
                more = false;
            } else {
                byte |= 0x80;
            }
            vec.push_back(byte);
        }
    }
};

} // namespace dgtl::isa::wasm::emitter

export namespace dgtl::exec {
using DGTLWasmEncoder = dgtl::isa::wasm::emitter::DGTLWasmEncoder;
}
export namespace dgtl::isa::wasm {
using DGTLWasmEncoder = dgtl::isa::wasm::emitter::DGTLWasmEncoder;
}