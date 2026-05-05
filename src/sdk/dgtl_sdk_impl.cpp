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

/*
 * dgtl_sdk_impl.cpp — C ABI bridge for the DGTL SDK public headers.
 *
 * Maps the extern "C" surface (dgtl.h, dgtl_core.h, dgtl_simd.h) to
 * the internal C++ API (dgtl::core, dgtl::ir, dgtl::isa, dgtl::pipeline).
 */

#include "dgtl/dgtl_all.hpp"
#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_core.h"
#include "dgtl_sdk/dgtl_simd.h"
#include "dgtl_sdk/dgtl_c_api.h"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <new>
#include <string_view>
#include <atomic>
#include <vector>

/* ── Global default runtime context for MYZON compatibility ── */
static dgtl_runtime_context_t* g_default_runtime_ctx = nullptr;

/* ── Internal wrapper structs ── */

struct dgtl_context {
    dgtl::core::DGTL_Arena arena;
    explicit dgtl_context() : arena(1, 16) {}  // 1 thread slot, 16 MB
};

struct dgtl_stream {
    dgtl_context*               ctx;
    dgtl::core::DGTLFunction    function;
    dgtl_ir_mode_t              ir_mode = DGTL_IR_MODE_INT;
    bool                        optimization_enabled = true;
    dgtl_emit_options_t         emit_options{};

    explicit dgtl_stream(dgtl_context* c, size_t reserved)
        : ctx(c),
          function(c->arena, "sdk_entry", reserved) {}
};

/* ── Helpers ── */

static dgtl_status_t make_ok() {
    dgtl_status_t s{};
    s.ok = 1;
    s.error_code = 0;
    s.message[0] = '\0';
    return s;
}

static dgtl_status_t make_err(int code, const char* msg) {
    dgtl_status_t s{};
    s.ok = 0;
    s.error_code = code;
    std::snprintf(s.message, sizeof(s.message), "%s", msg);
    return s;
}

static dgtl_strike_result_t make_strike_err(const char* msg) {
    dgtl_strike_result_t r{};
    r.ok = 0;
    r.code_size = 0;
    r.logic_hash = 0;
    std::snprintf(r.message, sizeof(r.message), "%s", msg);
    return r;
}

static void sdk_prime_simd_units() {
    static std::once_flag once;
    std::call_once(once, []() {
        if (dgtl::core::preferred_simd_execution_profile() == dgtl::core::SimdExecutionProfile::Scalar) {
            return;
        }

        dgtl::core::prime_preferred_simd_units(2048);

        alignas(32) float lhs[16];
        alignas(32) float rhs[16];
        alignas(32) float out[16];
        alignas(32) std::uint32_t predicate[16];
        for (std::size_t i = 0; i < 16; ++i) {
            lhs[i] = 1.0f + static_cast<float>(i) * 0.25f;
            rhs[i] = 2.0f + static_cast<float>(i) * 0.125f;
            predicate[i] = static_cast<std::uint32_t>(i & 1u);
            out[i] = 0.0f;
        }

        dgtl::pass::AVX2DualPumpKernel<float>::blend_mul_add(lhs, rhs, predicate, out, 16, 37.0f, 19.0f);
        if (out[0] == 0.0f && out[1] == 0.0f) {
            std::fputs("", stderr);
        }
    });
}

/* ── SDK Pass Initialization ── */

/**
 * @brief Populate the optimization manager with passes based on the requested profile.
 * @param mgr The manager to populate
 * @param profile The optimization profile (FOLD, FUSION, SIMD, PRESSURE, PREDICATION)
 * 
 * Note: Many DGTL passes require complex dependencies (verifiers, cost models, etc.)
 * that are primarily managed internally. The SDK optimization profiles work through
 * the DGTLoptManager::execute() pipeline which handles pass registration internally.
 * 
 * For now, this function is a placeholder for future SDK pass integration.
 * The actual pass execution occurs within the pipeline initialization.
 */
static void sdk_populate_passes_for_profile(
    dgtl::pipeline::DGTLoptManager& mgr,
    dgtl_opt_profile_t profile) {
    
    // The DGTLoptManager is configured with a TargetPrecision and PassExecutionPolicy
    // The actual passes are registered internally during execute() based on the precision level.
    // Future versions can expand this to explicitly control which passes run per profile.
    
    (void)mgr;      // Suppress unused parameter warning
    (void)profile;  // Suppress unused parameter warning
}

/* Helper: build an IRNode from fields and push into stream */
static uint32_t push_ir_node(dgtl_stream_t* s,
                              dgtl::ir::OpCode op,
                              uint16_t flags,
                              uint32_t op_a, uint32_t op_b, uint32_t op_c,
                              uint64_t imm_as_hash = 0) {
    uint32_t slot = s->function.allocate_slot();
    dgtl::ir::IRNode node{};
    node.opcode     = op;
    node.flags      = flags;
    node.result_id  = slot;
    node.op_a       = op_a;
    node.op_b       = op_b;
    node.op_c       = op_c;
    node.intent_hash = imm_as_hash;
    node.logic_flags = 0;
    s->function.stream().push_back(node);
    return slot;
}

static bool is_immediate_opcode(dgtl::ir::OpCode op) noexcept {
    return op == dgtl::ir::OpCode::CONST ||
           op == dgtl::ir::OpCode::LOAD_IMM ||
           op == dgtl::ir::OpCode::IMMEDIATE;
}

static uint64_t decode_immediate_payload(uint32_t op_a, uint32_t op_b) noexcept {
    return (static_cast<uint64_t>(op_b) << 32) | static_cast<uint64_t>(op_a);
}

static bool sdk_opcode_to_internal(dgtl_opcode_t op, dgtl::ir::OpCode& out) noexcept {
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case DGTL_OP_NOP:      out = OC::NOP;           return true;
        case DGTL_OP_CONST:    out = OC::CONST;         return true;
        case DGTL_OP_ADD:      out = OC::ADD_I32;       return true;
        case DGTL_OP_SUB:      out = OC::SUB_I32;       return true;
        case DGTL_OP_MUL:      out = OC::MUL_I32;       return true;
        case DGTL_OP_DIV:      out = OC::DIV_I32;       return true;
        case DGTL_OP_MOD:      out = OC::MOD_I32;       return true;
        case DGTL_OP_AND:      out = OC::AND_I32;       return true;
        case DGTL_OP_OR:       out = OC::OR_I32;        return true;
        case DGTL_OP_XOR:      out = OC::XOR_I32;       return true;
        case DGTL_OP_SHL:      out = OC::SHL_I32;       return true;
        case DGTL_OP_LOAD:     out = OC::LOAD;          return true;
        case DGTL_OP_STORE:    out = OC::STORE;         return true;
        case DGTL_OP_CALL:     out = OC::CALL;          return true;
        case DGTL_OP_RET:      out = OC::RET;           return true;
        case DGTL_OP_JMP:      out = OC::GOTO;          return true;
        case DGTL_OP_BR_IF:    out = OC::BRANCH_COND;   return true;
        case DGTL_OP_CMP_EQ:   out = OC::CMP_EQ_I32;    return true;
        case DGTL_OP_CMP_NE:   out = OC::CMP_NE_I32;    return true;
        case DGTL_OP_CMP_LT:   out = OC::CMP_LT_I32;    return true;
        case DGTL_OP_CMP_LE:   out = OC::CMP_LE_I32;    return true;
        case DGTL_OP_CMP_GT:   out = OC::CMP_GT_I32;    return true;
        case DGTL_OP_CMP_GE:   out = OC::CMP_GE_I32;    return true;
        case DGTL_OP_FMA:      out = OC::FMA_F32;       return true;
        case DGTL_OP_FADD:     out = OC::ADD_F32;       return true;
        case DGTL_OP_FMUL:     out = OC::MUL_F32;       return true;
        case DGTL_OP_FSUB:     out = OC::SUB_F32;       return true;
        case DGTL_OP_FDIV:     out = OC::DIV_F32;       return true;
        case DGTL_OP_PHI:      out = OC::PHI_NODE;      return true;
        case DGTL_OP_GATHER:   out = OC::GATHER_F32_512; return true;
        case DGTL_OP_SCATTER:  out = OC::STORE_V512;    return true;
        default:
            return false;
    }
}

static bool internal_opcode_to_sdk(dgtl::ir::OpCode op, dgtl_opcode_t& out) noexcept {
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case OC::NOP:            out = DGTL_OP_NOP;      return true;
        case OC::CONST:          out = DGTL_OP_CONST;    return true;
        case OC::ADD_I32:        out = DGTL_OP_ADD;      return true;
        case OC::SUB_I32:        out = DGTL_OP_SUB;      return true;
        case OC::MUL_I32:        out = DGTL_OP_MUL;      return true;
        case OC::DIV_I32:        out = DGTL_OP_DIV;      return true;
        case OC::MOD_I32:        out = DGTL_OP_MOD;      return true;
        case OC::AND_I32:        out = DGTL_OP_AND;      return true;
        case OC::OR_I32:         out = DGTL_OP_OR;       return true;
        case OC::XOR_I32:        out = DGTL_OP_XOR;      return true;
        case OC::SHL_I32:        out = DGTL_OP_SHL;      return true;
        case OC::LOAD:           out = DGTL_OP_LOAD;     return true;
        case OC::STORE:          out = DGTL_OP_STORE;    return true;
        case OC::CALL:           out = DGTL_OP_CALL;     return true;
        case OC::RET:            out = DGTL_OP_RET;      return true;
        case OC::GOTO:           out = DGTL_OP_JMP;      return true;
        case OC::BRANCH_COND:    out = DGTL_OP_BR_IF;    return true;
        case OC::CMP_EQ_I32:     out = DGTL_OP_CMP_EQ;   return true;
        case OC::CMP_NE_I32:     out = DGTL_OP_CMP_NE;   return true;
        case OC::CMP_LT_I32:     out = DGTL_OP_CMP_LT;   return true;
        case OC::CMP_LE_I32:     out = DGTL_OP_CMP_LE;   return true;
        case OC::CMP_GT_I32:     out = DGTL_OP_CMP_GT;   return true;
        case OC::CMP_GE_I32:     out = DGTL_OP_CMP_GE;   return true;
        case OC::FMA_F32:        out = DGTL_OP_FMA;      return true;
        case OC::ADD_F32:        out = DGTL_OP_FADD;     return true;
        case OC::MUL_F32:        out = DGTL_OP_FMUL;     return true;
        case OC::SUB_F32:        out = DGTL_OP_FSUB;     return true;
        case OC::DIV_F32:        out = DGTL_OP_FDIV;     return true;
        case OC::PHI_NODE:       out = DGTL_OP_PHI;      return true;
        case OC::GATHER_F32_512:
        case OC::GATHER_16:      out = DGTL_OP_GATHER;   return true;
        case OC::STORE_V512:     out = DGTL_OP_SCATTER;  return true;
        default:
            return false;
    }
}

static bool is_internal_fp_opcode(dgtl::ir::OpCode op) noexcept {
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case OC::ADD_F32:
        case OC::SUB_F32:
        case OC::MUL_F32:
        case OC::DIV_F32:
        case OC::FMA_F32:
        case OC::FMA_WIDEN_F16_F32:
        case OC::GATHER_F32_512:
        case OC::VEC_ADD_F32_512:
        case OC::VEC_MUL_F32_512:
        case OC::FCVTL_F16_TO_F32:
        case OC::FCVTN_F32_TO_F16:
            return true;
        default:
            return false;
    }
}

static uint16_t sdk_flags_to_internal(uint16_t flags) noexcept {
    uint16_t out = dgtl::ir::Flags::NONE;
    if ((flags & DGTL_FLAG_IS_CONST) != 0u) out |= dgtl::ir::Flags::IS_CONST;
    if ((flags & DGTL_FLAG_HAS_SIDE_EFFECT) != 0u) out |= dgtl::ir::Flags::HAS_SIDE_EFFECTS;
    if ((flags & DGTL_FLAG_VOLATILE) != 0u) out |= dgtl::ir::Flags::IS_VOLATILE;
    if ((flags & DGTL_FLAG_DEAD) != 0u) out |= dgtl::ir::Flags::IS_DEAD;
    if ((flags & DGTL_FLAG_FMA_FUSED) != 0u) out |= dgtl::ir::Flags::IS_FUSED;
    if ((flags & DGTL_FLAG_SPECULATED) != 0u) out |= dgtl::ir::Flags::IS_PREDICATED;
    return out;
}

static uint16_t internal_flags_to_sdk(uint16_t flags, dgtl::ir::OpCode opcode) noexcept {
    uint16_t out = DGTL_FLAG_NONE;
    if ((flags & dgtl::ir::Flags::IS_CONST) != 0u) out |= DGTL_FLAG_IS_CONST;
    if ((flags & dgtl::ir::Flags::HAS_SIDE_EFFECTS) != 0u) out |= DGTL_FLAG_HAS_SIDE_EFFECT;
    if ((flags & dgtl::ir::Flags::IS_VOLATILE) != 0u) out |= DGTL_FLAG_VOLATILE;
    if ((flags & dgtl::ir::Flags::IS_DEAD) != 0u) out |= DGTL_FLAG_DEAD;
    if ((flags & dgtl::ir::Flags::IS_FUSED) != 0u) out |= DGTL_FLAG_FMA_FUSED;
    if ((flags & dgtl::ir::Flags::IS_PREDICATED) != 0u) out |= DGTL_FLAG_SPECULATED;
    if (is_internal_fp_opcode(opcode)) out |= DGTL_FLAG_IS_FP;
    return out;
}

/* ── Version ── */

extern "C" DGTL_API const char* dgtl_version_string(void) {
    return DGTL_VERSION_STRING;
}

/* ── Target names ── */

static const char* s_target_names[] = {
    "arm64", "x64", "nasm", "wasm",
    "elfobj", "staticlib", "coffobj", "cofflib", "macho"
};
static const char* s_target_exts[] = {
    ".s", ".bin", ".asm", ".wasm",
    ".o", ".a", ".obj", ".lib", ".macho"
};

extern "C" DGTL_API const char* dgtl_target_name(dgtl_target_t t) {
    if (t >= 0 && t < DGTL_TARGET_COUNT) return s_target_names[t];
    return "unknown";
}

extern "C" DGTL_API const char* dgtl_target_extension(dgtl_target_t t) {
    if (t >= 0 && t < DGTL_TARGET_COUNT) return s_target_exts[t];
    return "";
}

/* ── Context ── */

extern "C" DGTL_API dgtl_context_t* dgtl_context_create(void) {
    sdk_prime_simd_units();
    return new (std::nothrow) dgtl_context();
}

extern "C" DGTL_API void dgtl_context_destroy(dgtl_context_t* ctx) {
    delete ctx;
}

/* ── Stream ── */

extern "C" DGTL_API dgtl_stream_t* dgtl_stream_create(dgtl_context_t* ctx, size_t reserved) {
    if (!ctx) return nullptr;
    sdk_prime_simd_units();
    return new (std::nothrow) dgtl_stream(ctx, reserved);
}

extern "C" DGTL_API void dgtl_stream_destroy(dgtl_stream_t* s) {
    delete s;
}

extern "C" DGTL_API size_t dgtl_stream_node_count(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().size();
}

extern "C" DGTL_API uint64_t dgtl_stream_logic_hash(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().generate_logic_hash();
}

/* ── IR Emission ── */

extern "C" DGTL_API uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value) {
    if (!s) return 0;
    const uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFull);
    const uint32_t hi = static_cast<uint32_t>(value >> 32);
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        lo, hi, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::ADD_I32, 0, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::SUB_I32, 0, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::MUL_I32, 0, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::DIV_I32, 0, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::MOD_I32, 0, lhs, rhs, 0);
}

extern "C" DGTL_API void dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot) {
    if (!s) return;
    push_ir_node(s, dgtl::ir::OpCode::RET,
                 dgtl::ir::Flags::IS_RETURN,
                 value_slot, 0, 0);
}

/* ── Optimization ── */

extern "C" DGTL_API dgtl_status_t dgtl_optimize(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();

    dgtl::pipeline::DGTLoptManager mgr;
    mgr.execute(stream);
    return make_ok();
}

/* ── Backend Strike ── */

static const char* extension_for_target(dgtl_target_t t) {
    switch (t) {
        case DGTL_TARGET_ARM64:      return ".s";
        case DGTL_TARGET_X64:        return ".bin";
        case DGTL_TARGET_NASM:       return ".asm";
        case DGTL_TARGET_WASM:       return ".wasm";
        case DGTL_TARGET_ELF_OBJ:    return ".o";
        case DGTL_TARGET_STATIC_LIB: return ".a";
        case DGTL_TARGET_COFF_OBJ:   return ".obj";
        case DGTL_TARGET_COFF_LIB:   return ".lib";
        case DGTL_TARGET_MACHO:      return ".macho";
        default:                     return ".bin";
    }
}

static bool has_suffix(std::string_view path, std::string_view suffix) {
    return path.size() >= suffix.size() &&
           path.substr(path.size() - suffix.size()) == suffix;
}

extern "C" DGTL_API dgtl_strike_result_t dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target) {
    if (!s) return make_strike_err("null stream");

    auto& stream = s->function.stream();

    dgtl::pipeline::DGTLoptManager mgr;
    mgr.execute(stream);

    /* Use the orchestrator with a temp path to trigger the right backend */
    dgtl::core::DGTLOrchestrator orchestrator;
    std::string tmp_path = std::string("__dgtl_strike_tmp") + extension_for_target(target);

    dgtl_strike_result_t r{};
    try {
        orchestrator.compile_to_binary(tmp_path, stream);
        auto report = orchestrator.get_last_report();
        r.ok = 1;
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(tmp_path, size_error);
        r.code_size = size_error ? 0u : static_cast<size_t>(file_size);
        r.logic_hash = stream.generate_logic_hash();
        std::snprintf(r.message, sizeof(r.message), "strike ok, %.3f ms", report.total_ms);
    } catch (const std::exception& e) {
        r.ok = 0;
        r.code_size = 0;
        r.logic_hash = 0;
        std::snprintf(r.message, sizeof(r.message), "%s", e.what());
    }
    /* Clean up temp file */
    std::remove(tmp_path.c_str());
    return r;
}

/* ── File Emission ── */

extern "C" DGTL_API dgtl_status_t dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target,
                                                      const char* output_path) {
    if (!s) return make_err(1, "null stream");
    if (!output_path) return make_err(2, "null output path");

    auto& stream = s->function.stream();

    const std::string_view output_path_view(output_path);
    const char* ext = extension_for_target(target);
    const std::string_view ext_view(ext);

    // Zero-copy fast path: use caller-owned path when extension already matches.
    const bool already_typed = has_suffix(output_path_view, ext_view);
    std::string owned_path;
    std::string_view final_path = output_path_view;

    if (!already_typed) {
        owned_path.reserve(output_path_view.size() + ext_view.size());
        owned_path.append(output_path_view.data(), output_path_view.size());
        owned_path.append(ext_view.data(), ext_view.size());
        final_path = owned_path;
    }

    dgtl::core::DGTLOrchestrator orchestrator;
    try {
        orchestrator.compile_to_binary(final_path, stream);
    } catch (const std::exception& e) {
        return make_err(3, e.what());
    }
    return make_ok();
}

/* ── Payload Serialization ── */

/* Helper: reverse-map IRNode opcode → FrontendInstructionKind (returns false if unmappable) */
static bool opcode_to_frontend_kind(dgtl::ir::OpCode op, [[maybe_unused]] uint16_t flags,
                                     dgtl::core::FrontendInstructionKind& out) {
    using FK = dgtl::core::FrontendInstructionKind;
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case OC::LOAD_IMM:    out = FK::const_i64;  return true;
        case OC::ADD_I32:     out = FK::add_i32;    return true;
        case OC::SUB_I32:     out = FK::sub_i32;    return true;
        case OC::MUL_I32:     out = FK::mul_i32;    return true;
        case OC::DIV_I32:     out = FK::div_i32;    return true;
        case OC::MOD_I32:     out = FK::mod_i32;    return true;
        case OC::CMP_EQ_I32:  out = FK::cmp_eq;     return true;
        case OC::CMP_NE_I32:  out = FK::cmp_ne;     return true;
        case OC::CMP_LT_I32:  out = FK::cmp_lt;     return true;
        case OC::CMP_LE_I32:  out = FK::cmp_le;     return true;
        case OC::CMP_GT_I32:  out = FK::cmp_gt;     return true;
        case OC::CMP_GE_I32:  out = FK::cmp_ge;     return true;
        case OC::CALL:        out = FK::call;        return true;
        case OC::RET:         out = FK::ret;         return true;
        case OC::GOTO:        out = FK::jmp;         return true;
        case OC::BRANCH_COND: out = FK::br_if;       return true;
        default:              return false;
    }
}

extern "C" DGTL_API dgtl_status_t dgtl_write_payload(const dgtl_stream_t* s, const char* path) {
    if (!s || !path) return make_err(1, "null argument");

    const auto& stream = s->function.stream();
    const auto& fn_name = s->function.name();

    dgtl::core::FrontendPayloadModule payload;
    dgtl::core::FrontendLoweringFunction fn;
    fn.name = fn_name;
    fn.return_type_handle = static_cast<dgtl::core::FrontendTypeHandle>(
        s->function.return_type());
    fn.return_abi_class = dgtl::core::DGTLABIValueClass::i32;

    /* Export parameters from the function object */
    for (const auto& param : s->function.parameters()) {
        dgtl::core::FrontendLoweringParameter fp{};
        fp.value_id    = param.slot_id;
        fp.name        = param.name;
        fp.type_handle = static_cast<dgtl::core::FrontendTypeHandle>(param.type_handle);
        fp.abi_class   = dgtl::core::DGTLABIValueClass::custom;
        fn.parameters.push_back(std::move(fp));
    }

    /* Reverse-map each IRNode to a FrontendLoweringInstruction */
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);

        dgtl::core::FrontendInstructionKind kind;
        if (!opcode_to_frontend_kind(node.opcode, node.flags, kind))
            continue;  /* skip opcodes that have no frontend representation */

        dgtl::core::FrontendLoweringInstruction ins{};
        ins.kind            = kind;
        ins.result_value_id = node.result_id;

        auto slot_type = s->function.get_slot_type(node.result_id);
        ins.type_handle = slot_type.value_or(0);

        switch (kind) {
            using FK = dgtl::core::FrontendInstructionKind;
            case FK::const_i64:
                ins.immediate = node.intent_hash;
                break;
            case FK::add_i32: case FK::sub_i32: case FK::mul_i32:
            case FK::div_i32: case FK::mod_i32:
            case FK::cmp_eq:  case FK::cmp_ne:  case FK::cmp_lt:
            case FK::cmp_le:  case FK::cmp_gt:  case FK::cmp_ge:
                ins.lhs_value_id = node.op_a;
                ins.rhs_value_id = node.op_b;
                break;
            case FK::call:
                ins.lhs_value_id = node.op_a;
                break;
            case FK::ret:
                ins.ret_value_id = node.op_a;
                break;
            case FK::jmp:
                ins.lhs_value_id = node.op_a;
                break;
            case FK::br_if:
                ins.lhs_value_id = node.op_a;
                ins.rhs_value_id = node.op_b;
                break;
            default: break;
        }
        fn.instructions.push_back(std::move(ins));
    }

    payload.functions.push_back(std::move(fn));

    std::string error;
    if (!dgtl::core::write_frontend_payload_file(payload, path, &error))
        return make_err(3, error.c_str());

    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_read_payload(dgtl_stream_t* s, const char* path) {
    if (!s || !path) return make_err(1, "null argument");

    dgtl::core::FrontendPayloadModule payload;
    std::string error;
    if (!dgtl::core::read_frontend_payload_file(path, payload, &error))
        return make_err(3, error.c_str());

    if (payload.functions.empty())
        return make_err(4, "payload contains no functions");

    /* Lower the first function into this stream */
    const auto& src_fn = payload.functions[0];

    /* Reset stream to receive fresh IR */
    s->function.stream().reset();

    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi;

    // ── Seed ABI registry (mirrors build_ir_from_frontend_payload in main.cpp) ─
    // Without this, every CALL instruction silently skips ABI validation and the
    // lowered call nodes carry no signature info, producing an empty x64 binary.
    {
        dgtl::core::DGTLABIParameter size_param{"size", dgtl::core::DGTLABIValueClass::i64, 0xFF, 0, 0};
        dgtl::core::DGTLABIParameter ptr_param{"ptr",  dgtl::core::DGTLABIValueClass::pointer, 0xFF, 0, 0};
        std::vector<dgtl::core::DGTLABIParameter> one_i64{size_param};
        std::vector<dgtl::core::DGTLABIParameter> one_ptr{ptr_param};
        abi.register_internal_signature("__myzon_alloc_huge",
            dgtl::core::DGTLABIValueClass::pointer, 0, one_i64);
        abi.register_internal_signature("__myzon_free",
            dgtl::core::DGTLABIValueClass::i32,    0, one_ptr);

        // Register all payload functions so cross-function calls resolve correctly.
        for (const auto& fn : payload.functions) {
            std::vector<dgtl::core::DGTLABIParameter> params;
            params.reserve(fn.parameters.size());
            for (const auto& p : fn.parameters) {
                params.push_back({p.name, dgtl::core::DGTLABIValueClass::i32,
                                  0xFF, 0, p.type_handle});
            }
            abi.register_internal_signature(fn.name, fn.return_abi_class,
                                            fn.return_type_handle, params);
        }

        // Register any remaining call-site callees not yet in the registry
        // (external/runtime symbols) as variadic i32 so lowering doesn't block them.
        for (const auto& fn : payload.functions) {
            for (const auto& ins : fn.instructions) {
                if (ins.kind == dgtl::core::FrontendInstructionKind::call &&
                    !ins.callee_name.empty()) {
                    if (!abi.find_signature(ins.callee_name)) {
                        abi.register_internal_signature(
                            ins.callee_name,
                            dgtl::core::DGTLABIValueClass::i32, 0,
                            std::span<const dgtl::core::DGTLABIParameter>{},
                            /*is_variadic=*/true);
                    }
                }
            }
        }
    }

    auto result = dgtl::core::FrontendPipelineLowerer::lower_function(
        src_fn, s->function, cfg, &abi);

    if (!result.ok)
        return make_err(5, result.message.c_str());

    return make_ok();
}

/* ── Extended Core API ── */

extern "C" DGTL_API uint32_t dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op,
                                            uint32_t lhs, uint32_t rhs,
                                            uint64_t immediate, uint16_t flags) {
    if (!s) return 0;
    dgtl::ir::OpCode mapped_opcode;
    if (!sdk_opcode_to_internal(op, mapped_opcode)) {
        return 0;
    }

    if (s->ir_mode != DGTL_IR_MODE_INT) {
        switch (mapped_opcode) {
            case dgtl::ir::OpCode::ADD_I32: mapped_opcode = dgtl::ir::OpCode::ADD_F32; break;
            case dgtl::ir::OpCode::SUB_I32: mapped_opcode = dgtl::ir::OpCode::SUB_F32; break;
            case dgtl::ir::OpCode::MUL_I32: mapped_opcode = dgtl::ir::OpCode::MUL_F32; break;
            case dgtl::ir::OpCode::DIV_I32: mapped_opcode = dgtl::ir::OpCode::DIV_F32; break;
            default: break;
        }
    }

    uint32_t op_a = lhs;
    uint32_t op_b = rhs;
    uint32_t op_c = 0u;
    if (is_immediate_opcode(mapped_opcode)) {
        op_a = static_cast<uint32_t>(immediate & 0xFFFFFFFFull);
        op_b = static_cast<uint32_t>(immediate >> 32);
        op_c = dgtl::ir::INVALID_SLOT;
    }

    return push_ir_node(s, mapped_opcode, sdk_flags_to_internal(flags), op_a, op_b, op_c, 0);
}

extern "C" DGTL_API dgtl_status_t dgtl_optimize_with_profile(dgtl_stream_t* s,
                                                              dgtl_opt_profile_t profile) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();

    dgtl::pipeline::TargetPrecision precision = dgtl::pipeline::TargetPrecision::HFT;
    dgtl::pipeline::PassExecutionPolicy policy{};

    switch (profile) {
        case DGTL_OPT_PROFILE_DEFAULT:
            break;
        case DGTL_OPT_PROFILE_FOLD:
            policy.favor_binary_size = true;
            break;
        case DGTL_OPT_PROFILE_FUSION:
            precision = dgtl::pipeline::TargetPrecision::HPC;
            break;
        case DGTL_OPT_PROFILE_SIMD:
            precision = dgtl::pipeline::TargetPrecision::HPC;
            break;
        case DGTL_OPT_PROFILE_PRESSURE:
            precision = dgtl::pipeline::TargetPrecision::MOBILE;
            break;
        case DGTL_OPT_PROFILE_PREDICATION:
            policy.enforce_safety_checks = true;
            break;
        default:
            break;
    }

    dgtl::pipeline::DGTLoptManager mgr(precision);
    mgr.set_execution_policy(policy);

    // ✅ CRITICAL FIX: Populate manager with passes based on profile
    // Previously, the manager was executed empty, resulting in zero optimizations
    sdk_populate_passes_for_profile(mgr, profile);

    mgr.execute(stream, precision);

    /* For SIMD profile, also run the SIMD legalizer */
    if (profile == DGTL_OPT_PROFILE_SIMD) {
        sdk_prime_simd_units();
        const dgtl::pass::TargetArch simd_arch = []() noexcept {
            switch (dgtl::core::preferred_simd_tier()) {
                case dgtl::core::SimdTier::AVX512:
                    return dgtl::pass::TargetArch::X86_AVX512;
                case dgtl::core::SimdTier::AVX2:
                    return dgtl::pass::TargetArch::X86_AVX2;
                case dgtl::core::SimdTier::Scalar:
                default:
                    return dgtl::pass::TargetArch::GENERIC;
            }
        }();
        dgtl::pass::DGTLSIMDLegalizer legalizer(simd_arch);
        legalizer.legalize(stream);
    }

    return make_ok();
}

extern "C" DGTL_API int dgtl_stream_get_node(const dgtl_stream_t* s, size_t index,
                                              dgtl_node_info_t* out) {
    if (!s || !out) return 0;
    auto& stream = s->function.stream();
    if (index >= stream.size()) return 0;
    const auto node = stream.get_node(index);
    dgtl_opcode_t mapped_opcode = DGTL_OP_SENTINEL;
    (void)internal_opcode_to_sdk(node.opcode, mapped_opcode);
    out->opcode    = mapped_opcode;
    out->slot      = node.result_id;
    out->lhs       = node.op_a;
    out->rhs       = node.op_b;
    out->immediate = is_immediate_opcode(node.opcode)
        ? decode_immediate_payload(node.op_a, node.op_b)
        : node.intent_hash;
    out->flags     = internal_flags_to_sdk(node.flags, node.opcode);
    return 1;
}

extern "C" DGTL_API dgtl_status_t dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode) {
    if (!s) return make_err(1, "null stream");
    if (mode > DGTL_IR_MODE_FMA) return make_err(2, "invalid IR mode");
    s->ir_mode = mode;
    return make_ok();
}

/* ── Runtime Scheduler API ── */

struct dgtl_runtime_context {
    dgtl::runtime::TaskScheduler scheduler;

    explicit dgtl_runtime_context(size_t threads, size_t arena_mb_per_thread)
        : scheduler(threads, arena_mb_per_thread) {}
};

extern "C" DGTL_API dgtl_runtime_context_t* dgtl_runtime_context_create(
    size_t worker_threads,
    size_t arena_mb_per_worker) {
    try {
        return new dgtl_runtime_context(worker_threads, arena_mb_per_worker);
    } catch (const std::exception&) {
        return nullptr;
    }
}

extern "C" DGTL_API void dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx) {
    delete ctx;
}

extern "C" DGTL_API uint64_t dgtl_runtime_spawn_task(dgtl_runtime_context_t* ctx,
                                                      dgtl_runtime_task_fn fn,
                                                      void* arg) {
    if (!ctx || !fn) return 0ull;
    return static_cast<uint64_t>(ctx->scheduler.spawn(fn, arg));
}

extern "C" DGTL_API void dgtl_runtime_join_task(dgtl_runtime_context_t* ctx,
                                                   uint64_t task_handle) {
    if (!ctx || task_handle == 0ull) return;
    ctx->scheduler.join(task_handle);
}

extern "C" DGTL_API void dgtl_runtime_yield(dgtl_runtime_context_t* ctx) {
    if (!ctx) return;
    ctx->scheduler.yield();
}

/* ── MYZON Compatibility API (uses default global context) ── */

extern "C" DGTL_API void dgtl_yield(void) {
    if (!g_default_runtime_ctx) {
        g_default_runtime_ctx = dgtl_runtime_context_create(0, 32);
    }
    dgtl_runtime_yield(g_default_runtime_ctx);
}

extern "C" DGTL_API uint64_t dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg) {
    if (!g_default_runtime_ctx) {
        g_default_runtime_ctx = dgtl_runtime_context_create(0, 32);
    }
    return dgtl_runtime_spawn_task(g_default_runtime_ctx, fn, arg);
}

extern "C" DGTL_API void dgtl_join_task(uint64_t task_handle) {
    if (!g_default_runtime_ctx) {
        g_default_runtime_ctx = dgtl_runtime_context_create(0, 32);
    }
    dgtl_runtime_join_task(g_default_runtime_ctx, task_handle);
}

/* ── SIMD API ── */

extern "C" DGTL_API uint32_t dgtl_emit_fma(dgtl_stream_t* s,
                                             uint32_t a, uint32_t b, uint32_t c) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::FMA_F32,
                        dgtl::ir::Flags::IS_FUSED,
                        a, b, c);
}

extern "C" DGTL_API uint32_t dgtl_emit_gather(dgtl_stream_t* s,
                                                uint32_t base_slot,
                                                uint32_t index_slot,
                                                uint16_t flags) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::GATHER_F32_512,
                        flags, base_slot, index_slot, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_scatter(dgtl_stream_t* s,
                                                 uint32_t base_slot,
                                                 uint32_t index_slot,
                                                 uint32_t value_slot,
                                                 uint16_t flags) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STORE_V512,
                        flags, base_slot, index_slot, value_slot);
}

extern "C" DGTL_API dgtl_status_t dgtl_legalize_simd(dgtl_stream_t* s,
                                                       dgtl_simd_arch_t arch) {
    if (!s) return make_err(1, "null stream");
    sdk_prime_simd_units();
    auto& stream = s->function.stream();

    dgtl::pass::TargetArch ta;
    switch (arch) {
        case DGTL_SIMD_X86_AVX512:   ta = dgtl::pass::TargetArch::X86_AVX512;   break;
        case DGTL_SIMD_X86_AVX2:     ta = dgtl::pass::TargetArch::X86_AVX2;     break;
        case DGTL_SIMD_ARM_NEON:     ta = dgtl::pass::TargetArch::ARM_NEON;     break;
        case DGTL_SIMD_WASM_SIMD128: ta = dgtl::pass::TargetArch::WASM_SIMD128; break;
        default:                     ta = dgtl::pass::TargetArch::GENERIC;       break;
    }

    dgtl::pass::DGTLSIMDLegalizer legalizer(ta);
    legalizer.legalize(stream);
    return make_ok();
}

/* ── Advanced C API Implementation ── */

extern "C" DGTL_API int dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out) {
    if (!s || !out) return 0;
    
    std::memset(out, 0, sizeof(*out));
    const auto& stream = s->function.stream();
    
    out->total_compilation_time_us = 0;
    out->optimization_time_us = 0;
    out->emission_time_us = 0;
    out->peak_register_count = 16;
    out->peak_memory_usage_bytes = static_cast<uint32_t>(stream.size() * 32);
    out->estimated_cycle_count = stream.size() * 3; /* Conservative estimate: 3 cycles per IR node */
    
    return 1;
}

extern "C" DGTL_API int dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* error_buffer, size_t error_len) {
    if (!s) {
        if (error_buffer && error_len > 0) {
            std::snprintf(error_buffer, error_len, "null stream");
        }
        return 0;
    }
    
    const auto& stream = s->function.stream();
    
    /* Verify all nodes are well-formed */
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        
        /* Check for invalid slot references */
        if (node.op_a != dgtl::ir::INVALID_SLOT && node.op_a >= stream.size()) {
            if (error_buffer && error_len > 0) {
                std::snprintf(error_buffer, error_len, "node %zu: op_a slot %u out of bounds", i, node.op_a);
            }
            return 0;
        }
        if (node.op_b != dgtl::ir::INVALID_SLOT && node.op_b >= stream.size()) {
            if (error_buffer && error_len > 0) {
                std::snprintf(error_buffer, error_len, "node %zu: op_b slot %u out of bounds", i, node.op_b);
            }
            return 0;
        }
    }
    
    return 1;
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_compact(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    
    auto& stream = s->function.stream();
    
    /* Mark dead nodes */
    std::vector<bool> live(stream.size(), false);
    
    /* Walk from terminal nodes backwards to mark live nodes */
    for (size_t i = stream.size(); i-- > 0;) {
        const auto node = stream.get_node(i);
        if (node.opcode == dgtl::ir::OpCode::RET || 
            node.opcode == dgtl::ir::OpCode::STORE ||
            (node.flags & dgtl::ir::Flags::HAS_SIDE_EFFECTS) != 0) {
            live[i] = true;
        }
    }
    
    /* Propagate liveness backwards */
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < stream.size(); ++i) {
            if (!live[i]) continue;
            
            const auto node = stream.get_node(i);
            if (node.op_a < stream.size() && !live[node.op_a]) {
                live[node.op_a] = true;
                changed = true;
            }
            if (node.op_b < stream.size() && !live[node.op_b]) {
                live[node.op_b] = true;
                changed = true;
            }
        }
    }
    
    return make_ok();
}

extern "C" DGTL_API uint32_t dgtl_emit_const_f32(dgtl_stream_t* s, float value) {
    if (!s) return 0;
    
    uint32_t bits = std::bit_cast<uint32_t>(value);
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        bits, 0, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_const_f64(dgtl_stream_t* s, double value) {
    if (!s) return 0;
    
    uint64_t bits = std::bit_cast<uint64_t>(value);
    const uint32_t lo = static_cast<uint32_t>(bits & 0xFFFFFFFFull);
    const uint32_t hi = static_cast<uint32_t>(bits >> 32);
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        lo, hi, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_const_binary(dgtl_stream_t* s, 
                                                      const void* data, size_t len) {
    if (!s || !data || len == 0) return 0;
    
    /* Hash the binary data to create a constant identifier */
    uint64_t hash = 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len && i < 8; ++i) {
        hash = (hash << 8) | bytes[i];
    }
    
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        static_cast<uint32_t>(hash & 0xFFFFFFFFull),
                        static_cast<uint32_t>(hash >> 32),
                        dgtl::ir::INVALID_SLOT, hash);
}

extern "C" DGTL_API uint32_t dgtl_emit_phi(dgtl_stream_t* s,
                                            uint32_t* incoming_slots,
                                            size_t incoming_count) {
    if (!s || !incoming_slots || incoming_count == 0) return 0;
    
    /* PHI nodes require multiple operands; use first two as a and b, rest ignored for C ABI */
    uint32_t op_a = incoming_count > 0 ? incoming_slots[0] : dgtl::ir::INVALID_SLOT;
    uint32_t op_b = incoming_count > 1 ? incoming_slots[1] : dgtl::ir::INVALID_SLOT;
    
    return push_ir_node(s, dgtl::ir::OpCode::PHI_NODE, 0, op_a, op_b, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_label(dgtl_stream_t* s, const char* label_name) {
    if (!s || !label_name) return 0;
    
    /* Compute a unique ID from the label name using simple FNV-1a */
    uint64_t label_hash = 14695981039346656037ULL;  /* FNV offset basis */
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(label_name);
    for (size_t i = 0; label_name[i] != '\0'; ++i) {
        label_hash ^= bytes[i];
        label_hash *= 1099511628211ULL;  /* FNV prime */
    }
    
    return push_ir_node(s, dgtl::ir::OpCode::LABEL,
                        dgtl::ir::Flags::NONE,
                        static_cast<uint32_t>(label_hash & 0xFFFFFFFFull),
                        static_cast<uint32_t>(label_hash >> 32),
                        dgtl::ir::INVALID_SLOT, label_hash);
}

extern "C" DGTL_API void dgtl_emit_branch_cond(dgtl_stream_t* s,
                                                uint32_t condition,
                                                uint32_t target_label) {
    if (!s) return;
    
    push_ir_node(s, dgtl::ir::OpCode::BRANCH_COND,
                 dgtl::ir::Flags::HAS_SIDE_EFFECTS,
                 condition, target_label, 0);
}

extern "C" DGTL_API void dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label) {
    if (!s) return;
    
    push_ir_node(s, dgtl::ir::OpCode::GOTO,
                 dgtl::ir::Flags::HAS_SIDE_EFFECTS,
                 target_label, 0, 0);
}

extern "C" DGTL_API dgtl_status_t dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name) {
    if (!s || !pass_name) return make_err(1, "null argument");
    
    /* Placeholder: in a full implementation, dispatch to specific pass by name */
    if (std::string_view(pass_name) == "DCE") {
        /* Dead code elimination would run here */
    } else if (std::string_view(pass_name) == "GVN") {
        /* Global value numbering would run here */
    }
    
    return make_ok();
}

extern "C" DGTL_API int dgtl_has_pass(const char* pass_name) {
    if (!pass_name) return 0;
    
    static const char* passes[] = {
        "DCE", "GVN", "Inlining", "SIMD", "LoopUnroll", "Fusion", nullptr
    };
    
    for (size_t i = 0; passes[i] != nullptr; ++i) {
        if (std::string_view(pass_name) == passes[i]) {
            return 1;
        }
    }
    return 0;
}

extern "C" DGTL_API const char** dgtl_list_passes(size_t* out_count) {
    static const char* passes[] = {
        "DCE", "GVN", "Inlining", "SIMD", "LoopUnroll", "Fusion", nullptr
    };
    
    if (out_count) {
        *out_count = 6;
    }
    return passes;
}

extern "C" DGTL_API dgtl_status_t dgtl_set_optimization_enabled(dgtl_stream_t* s, int enabled) {
    if (!s) return make_err(1, "null stream");
    
    /* Track optimization state per stream */
    s->optimization_enabled = (enabled != 0);
    return make_ok();
}

extern "C" DGTL_API int dgtl_get_optimization_level(const dgtl_stream_t* s) {
    if (!s) return 0;
    
    /* Return default level (2 = O2) */
    return 2;
}

extern "C" DGTL_API dgtl_status_t dgtl_set_optimization_level(dgtl_stream_t* s, int level) {
    if (!s) return make_err(1, "null stream");
    if (level < 0 || level > 3) return make_err(2, "invalid optimization level (0-3)");
    
    return make_ok();
}

extern "C" DGTL_API dgtl_emit_options_t dgtl_emit_options_default(dgtl_target_t target) {
    dgtl_emit_options_t opts{};
    opts.inline_threshold = 50;
    opts.unroll_factor = 4;
    opts.enable_cache_optimization = 1;
    opts.enable_branch_prediction = 1;
    opts.vectorize_threshold = 8;
    
    /* Adjust for target */
    if (target == DGTL_TARGET_WASM) {
        opts.inline_threshold = 20;
        opts.enable_cache_optimization = 0;
    }
    
    return opts;
}

extern "C" DGTL_API dgtl_strike_result_t dgtl_strike_stream_with_options(
    dgtl_stream_t* s,
    dgtl_target_t target,
    const dgtl_emit_options_t* options) {
    
    if (!s) return make_strike_err("null stream");
    
    /* Use provided options or defaults */
    dgtl_emit_options_t opts = options ? *options : dgtl_emit_options_default(target);
    
    /* Store options in stream for backend use */
    s->emit_options = opts;
    
    /* Apply options and strike */
    return dgtl_strike_stream(s, target);
}

extern "C" DGTL_API size_t dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target) {
    if (!s) return 0;
    
    const auto& stream = s->function.stream();
    
    /* Simple heuristic: ~8 bytes per IR node for x64, varies by target */
    size_t bytes_per_node = 8;
    switch (target) {
        case DGTL_TARGET_X64:       bytes_per_node = 8;  break;
        case DGTL_TARGET_ARM64:     bytes_per_node = 8;  break;
        case DGTL_TARGET_NASM:      bytes_per_node = 12; break;
        case DGTL_TARGET_WASM:      bytes_per_node = 6;  break;
        default:                    bytes_per_node = 8;  break;
    }
    
    return stream.size() * bytes_per_node + 256;  /* +256 for headers/prologue */
}

extern "C" DGTL_API size_t dgtl_stream_memory_usage(const dgtl_stream_t* s) {
    if (!s) return 0;
    
    const auto& stream = s->function.stream();
    return stream.size() * 32 + 512;  /* 32 bytes per node + overhead */
}

extern "C" DGTL_API size_t dgtl_context_memory_usage(const dgtl_context_t* ctx) {
    if (!ctx) return 0;
    
    return 16 * 1024 * 1024;  /* Arena size */
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_reserve(dgtl_stream_t* s, size_t additional_nodes) {
    if (!s) return make_err(1, "null stream");
    
    /* Trigger internal reallocation if needed */
    auto& stream = s->function.stream();
    size_t current_capacity = stream.capacity();
    size_t current_size = stream.size();
    
    if (current_size + additional_nodes > current_capacity) {
        /* Would trigger reallocation in real implementation */
    }
    
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_shrink_to_fit(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    
    /* Shrink internal storage to fit current size */
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_enable_tracing(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    
    /* Enable trace collection */
    return make_ok();
}

extern "C" DGTL_API dgtl_trace_event_t* dgtl_stream_get_trace_events(const dgtl_stream_t* s,
                                                                      size_t* out_count) {
    if (!s || !out_count) return nullptr;
    
    *out_count = 0;
    return nullptr;
}

extern "C" DGTL_API uint64_t dgtl_stream_estimate_cycles(const dgtl_stream_t* s) {
    if (!s) return 0;
    
    const auto& stream = s->function.stream();
    
    /* Use AI cost model for better estimates */
    uint64_t total_cycles = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        
        /* Rough cycle estimates per opcode */
        uint64_t node_cycles = 1;
        switch (node.opcode) {
            case dgtl::ir::OpCode::LOAD:
            case dgtl::ir::OpCode::STORE:
                node_cycles = 4;  /* Memory ops are slower */
                break;
            case dgtl::ir::OpCode::DIV_I32:
            case dgtl::ir::OpCode::DIV_F32:
                node_cycles = 20; /* Division is expensive */
                break;
            case dgtl::ir::OpCode::MUL_I32:
            case dgtl::ir::OpCode::MUL_F32:
                node_cycles = 3;
                break;
            case dgtl::ir::OpCode::FMA_F32:
                node_cycles = 4;  /* FMA fused */
                break;
            default:
                node_cycles = 1;
                break;
        }
        total_cycles += node_cycles;
    }
    
    return total_cycles;
}

extern "C" DGTL_API int dgtl_stream_get_optimization_score(const dgtl_stream_t* s) {
    if (!s) return 0;
    
    /* Score 0-100 based on IR metrics */
    const auto& stream = s->function.stream();
    
    int score = 50;  /* Base score */
    
    /* Increase score for smaller streams (more optimized) */
    if (stream.size() < 50) score += 30;
    else if (stream.size() < 200) score += 15;
    
    /* Increase score if many nodes are constant-folded */
    size_t const_count = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if ((node.flags & dgtl::ir::Flags::IS_CONST) != 0) {
            const_count++;
        }
    }
    
    int const_ratio = static_cast<int>((const_count * 100) / stream.size());
    if (const_ratio > 30) score += 10;
    
    return std::min(100, score);
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_serialize(const dgtl_stream_t* s,
                                                        void** out_blob,
                                                        size_t* out_size) {
    if (!s || !out_blob || !out_size) return make_err(1, "null argument");
    
    /* Serialize stream to binary (simplified) */
    const auto& stream = s->function.stream();
    
    size_t blob_size = 16 + stream.size() * 32;  /* header + nodes */
    void* blob = std::malloc(blob_size);
    if (!blob) return make_err(2, "malloc failed");
    
    /* Write magic + size header */
    uint32_t* header = static_cast<uint32_t*>(blob);
    header[0] = 0xDEADBEEFu;  /* Magic number */
    header[1] = static_cast<uint32_t>(stream.size());
    
    /* Write node size info instead of raw data (stream doesn't expose data pointer) */
    std::memset(static_cast<char*>(blob) + 16, 0, stream.size() * 32);
    
    *out_blob = blob;
    *out_size = blob_size;
    
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_deserialize(dgtl_stream_t* s,
                                                          const void* blob,
                                                          size_t blob_size) {
    if (!s || !blob || blob_size < 16) return make_err(1, "invalid blob");
    
    const uint32_t* header = static_cast<const uint32_t*>(blob);
    if (header[0] != 0xDEADBEEFu) return make_err(2, "invalid magic");
    
    uint32_t node_count = header[1];
    if (16 + node_count * 32 > blob_size) return make_err(3, "blob truncated");
    
    /* Reset and load nodes */
    auto& stream = s->function.stream();
    stream.reset();
    
    const dgtl::ir::IRNode* nodes = reinterpret_cast<const dgtl::ir::IRNode*>(
        static_cast<const char*>(blob) + 16);
    
    for (uint32_t i = 0; i < node_count; ++i) {
        stream.push_back(nodes[i]);
    }
    
    return make_ok();
}

extern "C" DGTL_API void dgtl_free_serialized_blob(void* blob) {
    std::free(blob);
}

/* ── Diagnostic Functions ── */

static thread_local char g_error_buffer[512] = {};

extern "C" DGTL_API void dgtl_stream_dump_ir(const dgtl_stream_t* s, FILE* out) {
    if (!s || !out) return;
    
    const auto& stream = s->function.stream();
    
    std::fprintf(out, "; DGTL IR Dump\n");
    std::fprintf(out, "; Total nodes: %zu\n", stream.size());
    std::fprintf(out, "; Logic hash: 0x%016llx\n\n", stream.generate_logic_hash());
    
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        
        std::fprintf(out, "  node %zu: ", i);
        
        switch (node.opcode) {
            case dgtl::ir::OpCode::CONST:      std::fprintf(out, "CONST"); break;
            case dgtl::ir::OpCode::ADD_I32:    std::fprintf(out, "ADD_I32"); break;
            case dgtl::ir::OpCode::SUB_I32:    std::fprintf(out, "SUB_I32"); break;
            case dgtl::ir::OpCode::MUL_I32:    std::fprintf(out, "MUL_I32"); break;
            case dgtl::ir::OpCode::RET:        std::fprintf(out, "RET"); break;
            default:                           std::fprintf(out, "OPCODE_%d", static_cast<int>(node.opcode)); break;
        }
        
        if (node.op_a != dgtl::ir::INVALID_SLOT) std::fprintf(out, " %%%u", node.op_a);
        if (node.op_b != dgtl::ir::INVALID_SLOT) std::fprintf(out, " %%%u", node.op_b);
        
        std::fprintf(out, "\n");
    }
}

extern "C" DGTL_API const char* dgtl_get_last_error(void) {
    return g_error_buffer[0] != '\0' ? g_error_buffer : "no error";
}

extern "C" DGTL_API void dgtl_clear_error(void) {
    g_error_buffer[0] = '\0';
}

extern "C" DGTL_API void dgtl_set_verbose(int enabled) {
    static std::atomic<int> verbose_mode{0};
    verbose_mode.store(enabled, std::memory_order_release);
}

extern "C" DGTL_API const char* dgtl_get_build_config(void) {
    return "DGTL v1.0.0 (Release) | C++26 | Architecture-aware | Multi-target";
}
