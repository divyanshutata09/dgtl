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

#include "dgtl/all.hpp"
#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_core.h"
#include "dgtl_sdk/dgtl_simd.h"

#include <cstring>
#include <cstdio>
#include <mutex>
#include <new>
#include <string_view>

/* ── Internal wrapper structs ── */

struct dgtl_context {
    dgtl::core::DGTL_Arena arena;
    explicit dgtl_context() : arena(1, 16) {}  // 1 thread slot, 16 MB
};

struct dgtl_stream {
    dgtl_context*               ctx;
    dgtl::core::DGTLFunction    function;
    dgtl_ir_mode_t              ir_mode = DGTL_IR_MODE_INT;

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
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        0, 0, 0, value);
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
        r.code_size = report.nodes_processed;
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
    return push_ir_node(s, static_cast<dgtl::ir::OpCode>(op), flags, lhs, rhs, 0, immediate);
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
    out->opcode    = static_cast<dgtl_opcode_t>(static_cast<uint16_t>(node.opcode));
    out->slot      = node.result_id;
    out->lhs       = node.op_a;
    out->rhs       = node.op_b;
    out->immediate = node.intent_hash;
    out->flags     = node.flags;
    return 1;
}

extern "C" DGTL_API dgtl_status_t dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode) {
    if (!s) return make_err(1, "null stream");
    if (mode > DGTL_IR_MODE_FMA) return make_err(2, "invalid IR mode");
    s->ir_mode = mode;
    return make_ok();
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
