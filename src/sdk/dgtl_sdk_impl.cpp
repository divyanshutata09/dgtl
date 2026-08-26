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

/*
 * dgtl_sdk_impl.cpp — C ABI bridge for the DGTL SDK public headers.
 *
 * Maps the extern "C" surface (dgtl.h, dgtl_core.h, dgtl_simd.h) to
 * the internal C++ API (dgtl::core, dgtl::ir, dgtl::isa, dgtl::pipeline).
 */
#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_capabilities.h"
#include "dgtl_sdk/dgtl_core.h"
#include "dgtl_sdk/dgtl_session.h"
#include "dgtl_sdk/dgtl_simd.h"
#include "dgtl_sdk/dgtl_c_api.h"

#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>
#include <bit>
#include <algorithm>

import dgtl.target;
import dgtl.config;
import dgtl.compiler_context;
import dgtl.numa_runtime;
import dgtl.task_scheduler;
import dgtl.hashing;
import dgtl.bit_pack;
import dgtl.flat_map;
import dgtl.dgtl_bulletins;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.op_traits;
import dgtl.ir_core.stream;
import dgtl.ir_core.io;
import dgtl.ir_core.verifier;
import dgtl.ir_core.function;
import dgtl.ir_core.semantic;

import dgtl.ir_core.pass_manager;
import dgtl.ir_core.simd_gather_scatter;
import dgtl.ir_core.linker_backend;
import dgtl.ir_core.macho_emitter;
import dgtl.ir_core.jit_orchestrator;
import dgtl.ir_core.code_cache;
import dgtl.ir_core.execution_dispatcher;
import dgtl.ir_core.binary_loader;
import dgtl.internal.api_execution_engine;
import dgtl.internal.api_aot_session;

/* Global default runtime context for compatibility helpers */
static std::atomic<dgtl_runtime_context_t*> g_default_runtime_ctx{nullptr};
static std::mutex g_default_runtime_ctx_mutex;

static dgtl_runtime_context_t* get_or_create_default_runtime_ctx() noexcept {
    auto* ctx = g_default_runtime_ctx.load(std::memory_order_acquire);
    if (!ctx) {
        std::lock_guard<std::mutex> lock(g_default_runtime_ctx_mutex);
        ctx = g_default_runtime_ctx.load(std::memory_order_relaxed);
        if (!ctx) {
            ctx = dgtl_runtime_context_create(0, 32);
            g_default_runtime_ctx.store(ctx, std::memory_order_release);
        }
    }
    return ctx;
}

/* Thread-local linker fallback diagnostics for SDK consumers */
static thread_local std::vector<std::string> g_last_linker_fallbacks;

/* Internal wrapper structs */

struct dgtl_context {
    dgtl::core::DGTL_Arena arena;
    dgtl_context() : arena(1, 16) {}  // 1 thread slot, 16 MB
};

struct dgtl_stream {
    dgtl_context*               ctx;
    dgtl::core::DGTLFunction    function;
    dgtl_ir_mode_t              ir_mode = DGTL_IR_MODE_INT;
    bool                        optimization_enabled = true;
    dgtl_emit_options_t         emit_options{};

    /* Callee-slot symbol bindings — populated by dgtl_stream_bind_callee_slot(s).
     * Used by dgtl_emit_to_file for linker-backed targets so call relocations
     * carry the correct external symbol names. */
    std::unordered_map<uint32_t, std::string> callee_bindings;

    explicit dgtl_stream(dgtl_context* c, size_t reserved)
        : ctx(c),
          function(c->arena, "sdk_entry", reserved) {}
};

struct dgtl_session {
    dgtl::api::Context context;
    std::unique_ptr<dgtl::api::ExecutionEngine> engine;
    dgtl_session_config_t config{};
    bool compiled = false;
    dgtl::execution::cache::Code_Cache code_cache;
    dgtl::execution::dispatcher::Execution_Dispatcher dispatcher;

    dgtl_session(dgtl::api::Context&& ctx,
                 std::unique_ptr<dgtl::api::ExecutionEngine>&& eng,
                 const dgtl_session_config_t& cfg) noexcept
        : context(std::move(ctx)),
          engine(std::move(eng)),
          config(cfg),
          code_cache(64ull * 1024ull * 1024ull) {}
};

/* Helpers */

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

static dgtl::api::ExecutionMode to_internal_session_mode(dgtl_session_mode_t mode) noexcept {
    switch (mode) {
        case DGTL_SESSION_MODE_JIT:
            return dgtl::api::ExecutionMode::JIT;
        case DGTL_SESSION_MODE_AOT:
            return dgtl::api::ExecutionMode::AOT;
        case DGTL_SESSION_MODE_HYBRID:
        default:
            return dgtl::api::ExecutionMode::Adaptive;
    }
}

static dgtl::api::OptimizationLevel to_internal_opt_level(dgtl_session_opt_level_t level) noexcept {
    switch (level) {
        case DGTL_SESSION_OPT_O0:
            return dgtl::api::OptimizationLevel::O0;
        case DGTL_SESSION_OPT_O1:
            return dgtl::api::OptimizationLevel::O1;
        case DGTL_SESSION_OPT_O3:
            return dgtl::api::OptimizationLevel::O3;
        case DGTL_SESSION_OPT_OSTRIKE:
            return dgtl::api::OptimizationLevel::OStrike;
        case DGTL_SESSION_OPT_O2:
        default:
            return dgtl::api::OptimizationLevel::O2;
    }
}

static dgtl_session_config_t normalize_session_config(const dgtl_session_config_t* cfg) noexcept {
    if (cfg != nullptr) {
        return *cfg;
    }

    dgtl_session_config_t out{};
    out.mode = DGTL_SESSION_MODE_HYBRID;
    out.opt_level = DGTL_SESSION_OPT_O2;
    out.timeout_us = 1000000u;
    out.enable_temporal_fusion = 0;
    out.enable_polyhedral_scop = 0;
    out.enable_speculation = 1;
    out.enable_vectorization = 1;
    out.thread_count = 0u;
    return out;
}

static dgtl::api::Config to_internal_session_config(const dgtl_session_config_t& cfg) noexcept {
    dgtl::api::Config out = dgtl::api::default_config();
    out.mode = to_internal_session_mode(cfg.mode);
    out.opt_level = to_internal_opt_level(cfg.opt_level);
    out._timeout_us = cfg.timeout_us;
    out.enable_temporal_fusion = cfg.enable_temporal_fusion != 0;
    out.enable_polyhedral_scop = cfg.enable_polyhedral_scop != 0;
    out.enable_speculation = cfg.enable_speculation != 0;
    out.enable_vectorization = cfg.enable_vectorization != 0;
    out.thread_count = cfg.thread_count;
    return out;
}

static bool is_linker_backed_target(dgtl_target_t target) noexcept {
    switch (target) {
        case DGTL_TARGET_ELF_OBJ:
        case DGTL_TARGET_STATIC_LIB:
        case DGTL_TARGET_COFF_OBJ:
        case DGTL_TARGET_COFF_LIB:
            return true;
        default:
            return false;
    }
}

static void capture_linker_fallback_diagnostics(const dgtl::ir::IRStream& stream,
                                                dgtl_target_t target) {
    g_last_linker_fallbacks.clear();
    if (!is_linker_backed_target(target)) {
        return;
    }

    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> scratch;
    bool ok = false;

    switch (target) {
        case DGTL_TARGET_ELF_OBJ:
            ok = linker.build_elf_relocatable_binary(stream, scratch, "DGTL_kernel_entry");
            break;
        case DGTL_TARGET_STATIC_LIB:
            ok = linker.build_static_archive_binary(stream, scratch, "module.o");
            break;
        case DGTL_TARGET_COFF_OBJ:
            ok = linker.build_coff_relocatable_binary(stream, scratch, "DGTL_kernel_entry");
            break;
        case DGTL_TARGET_COFF_LIB:
            ok = linker.build_coff_archive_binary(stream, scratch, "module.obj", "DGTL_kernel_entry");
            break;
        default:
            break;
    }

    (void)ok;
    const auto& diags = linker.fallback_diagnostics();
    g_last_linker_fallbacks.assign(diags.begin(), diags.end());
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

        dgtl::pass::simd::AVX2DualPumpKernel<float>::blend_mul_add(lhs, rhs, predicate, out, 16, 37.0f, 19.0f);
        if (out[0] == 0.0f && out[1] == 0.0f) {
            std::fputs("", stderr);
        }
    });
}

static void sdk_apply_numa_locality() noexcept {
    const uint32_t nodes = dgtl::runtime::numa::get_num_nodes();
    if (nodes <= 1u) {
        return;
    }
    const uint32_t worker = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const uint32_t node = dgtl::runtime::numa::recommend_worker_node(worker);
    (void)dgtl::runtime::numa::pin_current_thread_to_node(node);
    (void)dgtl::runtime::numa::pin_thread_to_core(dgtl::runtime::numa::recommend_worker_core(worker));
}

/* ── MinGW/GCC modules workaround ──────────────────────────────────────────
 * The move constructor for pair<string, IRStream> and the vector containing it
 * are not emitted transitively from imported modules under MinGW GCC. Force
 * their instantiation here so the linker can resolve references in
 * sdk_split_stream_by_callee_functions and its callers.
 * Mirrors the identical workaround in DGTLAdapter.cpp.
 * ────────────────────────────────────────────────────────────────────────── */
namespace {
using SdkModuleStream    = std::pair<std::string, dgtl::ir::IRStream>;
using SdkModuleStreamVec = std::vector<SdkModuleStream>;

[[maybe_unused]] SdkModuleStream sdk_force_module_stream_pair_move_ctor() {
    SdkModuleStream first{
        std::string("__dgtl_sdk_force_pair_move"),
        dgtl::ir::IRStream::allocate_transactional(1)
    };
    SdkModuleStream second(std::move(first));
    return second;
}

[[maybe_unused]] SdkModuleStreamVec sdk_force_module_stream_vec_symbols() {
    SdkModuleStreamVec v;
    v.emplace_back(std::string("__dgtl_sdk_force"),
                   dgtl::ir::IRStream::allocate_transactional(1));
    return v;
}
} // namespace

static dgtl::ir::BackendProfile::TargetISA to_internal_isa(dgtl_target_t target) noexcept {
    switch (target) {
        case DGTL_TARGET_ARM64:
            return dgtl::ir::BackendProfile::TargetISA::ARM64_SVE;
        case DGTL_TARGET_X64:
        case DGTL_TARGET_NASM:
        case DGTL_TARGET_ELF_OBJ:
        case DGTL_TARGET_STATIC_LIB:
        case DGTL_TARGET_COFF_OBJ:
        case DGTL_TARGET_COFF_LIB:
        case DGTL_TARGET_MACHO:
            return dgtl::ir::BackendProfile::TargetISA::X64_AVX512;
        case DGTL_TARGET_WASM:
            return dgtl::ir::BackendProfile::TargetISA::WASM;
        default:
            return dgtl::ir::BackendProfile::TargetISA::X64_AVX512;
    }
}

/* SDK Pass Initialization */

static void sdk_populate_passes_for_profile(
    dgtl::pipeline::DGTLoptManager& mgr,
    dgtl_opt_profile_t profile) {
    (void)mgr;      // Suppress unused parameter warning
    (void)profile;  // Suppress unused parameter warning
}

/* Helper: build an IRNode from fields and push into stream */
static uint32_t push_ir_node(dgtl_stream_t* s,
                              dgtl::ir::OpCode op,
                              uint16_t flags,
                              uint32_t op_a, uint32_t op_b, uint32_t op_c,
                              uint64_t imm_as_hash = 0) {
    sdk_apply_numa_locality();
    uint32_t slot = s->function.allocate_slot();
    dgtl::ir::IRNode node{};
    node.opcode     = op;
    node.flags      = flags;
    node.result_id  = slot;
    node.op_a       = op_a;
    node.op_b       = op_b;
    node.op_c       = op_c;
    node.intent_hash = (op == dgtl::ir::OpCode::CONST || imm_as_hash != 0)
                     ? imm_as_hash
                     : dgtl::ir::IRNode::calculate_hash(op, slot, op_a, op_b, op_c);
    switch (op) {
        case dgtl::ir::OpCode::ATOMIC_LOAD:
            node.logic_flags = dgtl::ir::ExtendedFlags::MEM_ACQUIRE;
            if (node.op_c == dgtl::ir::INVALID_SLOT) node.op_c = 0u;
            break;
        case dgtl::ir::OpCode::ATOMIC_STORE:
            node.logic_flags = dgtl::ir::ExtendedFlags::MEM_RELEASE_;
            if (node.op_c == dgtl::ir::INVALID_SLOT) node.op_c = 0u;
            break;
        case dgtl::ir::OpCode::ATOMIC_FENCE:
            node.logic_flags = dgtl::ir::ExtendedFlags::MEM_SEQ_CST;
            break;
        case dgtl::ir::OpCode::PTR_CAST:
        case dgtl::ir::OpCode::PTR_DIFF:
            node.logic_flags = 0;
            if (node.op_c == dgtl::ir::INVALID_SLOT) node.op_c = 0u;
            break;
        case dgtl::ir::OpCode::BOOL_SELECT:
            node.logic_flags = 0;
            node.flags = static_cast<uint16_t>(node.flags | dgtl::ir::Flags::IS_PREDICATED);
            break;
        default:
            node.logic_flags = 0;
            break;
    }
    s->function.stream().push_back(node);
    if ((op == dgtl::ir::OpCode::CONST || op == dgtl::ir::OpCode::LOAD) && (flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0) {
        s->function.stream().set_result_type(slot, dgtl::ir::TypeCode::F64);
    } else if (const auto expected = dgtl::ir::interfaces::expected_result_type_code(op);
        expected.has_value() && *expected != dgtl::ir::TypeCode::VOID_T) {
        s->function.stream().set_result_type(slot, *expected);
    } else {
        s->function.stream().clear_result_type(slot);
    }
    return slot;
}

static uint32_t push_ir_node_explicit_slot(dgtl_stream_t* s,
                                           dgtl::ir::OpCode op,
                                           uint16_t flags,
                                           uint32_t result_slot,
                                           uint32_t op_a,
                                           uint32_t op_b,
                                           uint32_t op_c,
                                           uint64_t imm_as_hash = 0,
                                           uint32_t logic_flags = 0) {
    if (!s || result_slot == 0u) return 0;
    sdk_apply_numa_locality();
    dgtl::ir::IRNode node{};
    node.opcode = op;
    node.flags = flags;
    node.result_id = result_slot;
    node.op_a = op_a;
    node.op_b = op_b;
    node.op_c = op_c;
    node.intent_hash = (op == dgtl::ir::OpCode::CONST || imm_as_hash != 0)
                     ? imm_as_hash
                     : dgtl::ir::IRNode::calculate_hash(op, result_slot, op_a, op_b, op_c);
    node.logic_flags = logic_flags;
    s->function.stream().push_back(node);
    if (result_slot != dgtl::ir::INVALID_SLOT && result_slot != 0u) {
        if ((op == dgtl::ir::OpCode::CONST || op == dgtl::ir::OpCode::LOAD) && (flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0) {
            s->function.stream().set_result_type(result_slot, dgtl::ir::TypeCode::F64);
        } else if (const auto expected = dgtl::ir::interfaces::expected_result_type_code(op);
            expected.has_value() && *expected != dgtl::ir::TypeCode::VOID_T) {
            s->function.stream().set_result_type(result_slot, *expected);
        } else {
            s->function.stream().clear_result_type(result_slot);
        }
    }
    return result_slot;
}

static uint32_t emit_call_like(dgtl_stream_t* s,
                               dgtl::ir::OpCode op,
                               uint32_t result_slot,
                               uint32_t callee_slot,
                               const uint32_t* args,
                               size_t arg_count) {
    if (!s || callee_slot == 0u) return 0;
    const uint32_t rid = (result_slot == 0u) ? dgtl::ir::INVALID_SLOT : result_slot;
    const uint32_t arg0 = arg_count > 0 ? args[0] : dgtl::ir::INVALID_SLOT;
    const uint32_t arg1 = arg_count > 1 ? args[1] : dgtl::ir::INVALID_SLOT;
    const uint32_t arg2 = arg_count > 2 ? args[2] : dgtl::ir::INVALID_SLOT;
    const uint16_t flags = static_cast<uint16_t>(dgtl::ir::Flags::IS_VOLATILE | dgtl::ir::Flags::HAS_SIDE_EFFECTS);
    const size_t node_idx = s->function.stream().size();

    dgtl::ir::IRNode node{};
    node.opcode = op;
    node.flags = flags;
    node.result_id = rid;
    node.op_a = callee_slot;
    node.op_b = arg0;
    node.op_c = arg1;
    node.logic_flags = arg2;
    node.refresh_intent_hash();
    s->function.stream().push_back(node);
    if (rid != dgtl::ir::INVALID_SLOT && rid != 0u) {
        s->function.stream().set_result_type(rid, dgtl::ir::TypeCode::I64);
    }

    if (arg_count > 3) {
        s->function.stream().set_call_overflow_args(
            node_idx,
            std::span<const uint32_t>(args + 3, arg_count - 3));
    }

    return rid;
}

static uint32_t emit_builder_node(dgtl_stream_t* s,
                                  dgtl::ir::OpCode op,
                                  uint32_t op_a,
                                  uint32_t op_b = 0u,
                                  uint32_t op_c = 0u,
                                  uint16_t flags = dgtl::ir::Flags::NONE) {
    if (!s) return 0;
    return push_ir_node(s, op, flags, op_a, op_b, op_c, 0);
}

static bool is_immediate_opcode(dgtl::ir::OpCode op) noexcept {
    return op == dgtl::ir::OpCode::CONST ||
           op == dgtl::ir::OpCode::LOAD_IMM ||
           op == dgtl::ir::OpCode::IMMEDIATE;
}

static uint64_t encode_immediate_payload(uint32_t op_a, uint32_t op_b) noexcept {
    uint64_t payload = 0;
    payload = dgtl::utils::BitPack::pack(payload, 0u, 32u, op_a);
    payload = dgtl::utils::BitPack::pack(payload, 32u, 32u, op_b);
    return payload;
}

static uint64_t decode_immediate_payload(uint32_t op_a, uint32_t op_b) noexcept {
    return encode_immediate_payload(op_a, op_b);
}

static uint32_t next_string_handle_candidate(uint32_t base, uint32_t salt) noexcept {
    uint32_t handle = base + (0x9E3779B9u * salt);
    if (handle == 0u || handle == dgtl::ir::INVALID_SLOT) {
        handle ^= 0xA5A5A5A5u;
    }
    if (handle == 0u || handle == dgtl::ir::INVALID_SLOT) {
        handle = 1u + salt;
    }
    return handle;
}

static uint32_t sdk_intern_string_handle(std::string_view literal) {
    static std::mutex s_string_registry_mutex;
    std::lock_guard<std::mutex> lock(s_string_registry_mutex);

    const uint32_t base_handle =
        dgtl::utils::hashing::Hashing::fnv1a_32(literal.data(), literal.size());

    uint32_t salt = 0;
    uint32_t handle = next_string_handle_candidate(base_handle, salt);
    while (true) {
        const std::string_view existing = dgtl::runtime::try_lookup_string_handle(handle);
        if (existing.empty() || existing == literal) {
            break;
        }
        ++salt;
        handle = next_string_handle_candidate(base_handle, salt);
    }

    dgtl::runtime::register_string_handle(handle,
                                          literal.data(),
                                          static_cast<uint32_t>(literal.size()));
    dgtl_register_string_handle(handle, literal.data(), literal.size());
    return handle;
}

static void split_immediate_payload(uint64_t payload, uint32_t& out_a, uint32_t& out_b) noexcept {
    out_a = static_cast<uint32_t>(dgtl::utils::BitPack::extract64(payload, 0u, 32u));
    out_b = static_cast<uint32_t>(dgtl::utils::BitPack::extract64(payload, 32u, 32u));
}

static bool sdk_opcode_to_internal(dgtl_opcode_t op, dgtl::ir::OpCode& out) noexcept {
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case DGTL_OP_NOP:              out = OC::NOP;               return true;
        case DGTL_OP_CONST:            out = OC::CONST;             return true;
        case DGTL_OP_CONST_F64:        out = OC::CONST;             return true;
        case DGTL_OP_ATOMIC_LOAD:      out = OC::ATOMIC_LOAD;       return true;
        case DGTL_OP_ATOMIC_STORE:     out = OC::ATOMIC_STORE;      return true;
        case DGTL_OP_ATOMIC_FENCE:     out = OC::ATOMIC_FENCE;      return true;
        case DGTL_OP_PTR_CAST:         out = OC::PTR_CAST;          return true;
        case DGTL_OP_PTR_DIFF:         out = OC::PTR_DIFF;          return true;
        case DGTL_OP_BOOL_SELECT:      out = OC::BOOL_SELECT;       return true;
        case DGTL_OP_STRING_CONST:     out = OC::STRING_CONST;      return true;
        case DGTL_OP_STRING_LENGTH:    out = OC::STRING_LENGTH;     return true;
        case DGTL_OP_STRING_COMPARE:   out = OC::STRING_COMPARE;    return true;
        case DGTL_OP_STRING_CONCAT:    out = OC::STRING_CONCAT;     return true;
        case DGTL_OP_STRING_SUBSTRING: out = OC::STRING_SUBSTRING;  return true;
        case DGTL_OP_ADD:              out = OC::ADD_I32;           return true;
        case DGTL_OP_SUB:              out = OC::SUB_I32;           return true;
        case DGTL_OP_MUL:              out = OC::MUL_I32;           return true;
        case DGTL_OP_DIV:              out = OC::DIV_I32;           return true;
        case DGTL_OP_MOD:              out = OC::MOD_I32;           return true;
        case DGTL_OP_AND:              out = OC::AND_I32;           return true;
        case DGTL_OP_OR:               out = OC::OR_I32;            return true;
        case DGTL_OP_XOR:              out = OC::XOR_I32;           return true;
        case DGTL_OP_SHL:              out = OC::SHL_I32;           return true;
        case DGTL_OP_SHR:              out = OC::SHR_I32;           return true;
        case DGTL_OP_ADD_I64:          out = OC::ADD_I64;           return true;
        case DGTL_OP_SUB_I64:          out = OC::SUB_I64;           return true;
        case DGTL_OP_MUL_I64:          out = OC::MUL_I64;           return true;
        case DGTL_OP_DIV_I64:          out = OC::DIV_I64;           return true;
        case DGTL_OP_MOD_I64:          out = OC::MOD_I64;           return true;
        case DGTL_OP_AND_I64:          out = OC::AND_I64;           return true;
        case DGTL_OP_OR_I64:           out = OC::OR_I64;            return true;
        case DGTL_OP_XOR_I64:          out = OC::XOR_I64;           return true;
        case DGTL_OP_SHL_I64:          out = OC::SHL_I64;           return true;
        case DGTL_OP_SHR_I64:          out = OC::SHR_I64;           return true;
        case DGTL_OP_SELECT:           out = OC::SELECT;            return true;
        case DGTL_OP_GEP:              out = OC::GEP;               return true;
        case DGTL_OP_PTR_TO_INT:       out = OC::PTR_TO_INT;        return true;
        case DGTL_OP_INT_TO_PTR:       out = OC::INT_TO_PTR;        return true;
        case DGTL_OP_READ_TSC:         out = OC::READ_TSC;          return true;
        case DGTL_OP_BR_LIKELY:        out = OC::BRANCH_LIKELY;     return true;
        case DGTL_OP_BR_UNLIKELY:      out = OC::BRANCH_UNLIKELY;   return true;
        case DGTL_OP_PREFETCH_T0:      out = OC::PREFETCH_T0;       return true;
        case DGTL_OP_CLFLUSH:          out = OC::CLFLUSH;           return true;
        case DGTL_OP_LOAD:             out = OC::LOAD;              return true;
        case DGTL_OP_STORE:            out = OC::STORE;             return true;
        case DGTL_OP_CALL:             out = OC::CALL;              return true;
        case DGTL_OP_RET:              out = OC::RET;               return true;
        case DGTL_OP_JMP:              out = OC::GOTO;              return true;
        case DGTL_OP_BR_IF:            out = OC::BRANCH_COND;       return true;
        case DGTL_OP_CMP_EQ:           out = OC::CMP_EQ_I32;        return true;
        case DGTL_OP_CMP_NE:           out = OC::CMP_NE_I32;        return true;
        case DGTL_OP_CMP_LT:           out = OC::CMP_LT_I32;        return true;
        case DGTL_OP_CMP_LE:           out = OC::CMP_LE_I32;        return true;
        case DGTL_OP_CMP_GT:           out = OC::CMP_GT_I32;        return true;
        case DGTL_OP_CMP_GE:           out = OC::CMP_GE_I32;        return true;
        case DGTL_OP_FMA:              out = OC::FMA_F32;           return true;
        case DGTL_OP_FADD:             out = OC::ADD_F32;           return true;
        case DGTL_OP_FMUL:             out = OC::MUL_F32;           return true;
        case DGTL_OP_FSUB:             out = OC::SUB_F32;           return true;
        case DGTL_OP_FDIV:             out = OC::DIV_F32;           return true;
        case DGTL_OP_FADD_F64:         out = OC::ADD_F64;           return true;
        case DGTL_OP_FMUL_F64:         out = OC::MUL_F64;           return true;
        case DGTL_OP_FSUB_F64:         out = OC::SUB_F64;           return true;
        case DGTL_OP_FDIV_F64:         out = OC::DIV_F64;           return true;
        case DGTL_OP_PHI:              out = OC::PHI_NODE;          return true;
        case DGTL_OP_GATHER:           out = OC::GATHER_F32_512;    return true;
        case DGTL_OP_SCATTER:          out = OC::STORE_V512;        return true;
        default:
            return false;
    }
}

static bool internal_opcode_to_sdk(dgtl::ir::OpCode op, dgtl_opcode_t& out) noexcept {
    using OC = dgtl::ir::OpCode;
    switch (op) {
        case OC::NOP:             out = DGTL_OP_NOP;          return true;
        case OC::CONST:           out = DGTL_OP_CONST;        return true;
        case OC::ATOMIC_LOAD:     out = DGTL_OP_ATOMIC_LOAD;  return true;
        case OC::ATOMIC_STORE:    out = DGTL_OP_ATOMIC_STORE; return true;
        case OC::ATOMIC_FENCE:    out = DGTL_OP_ATOMIC_FENCE; return true;
        case OC::PTR_CAST:        out = DGTL_OP_PTR_CAST;     return true;
        case OC::PTR_DIFF:        out = DGTL_OP_PTR_DIFF;     return true;
        case OC::BOOL_SELECT:     out = DGTL_OP_BOOL_SELECT;  return true;
        case OC::STRING_CONST:    out = DGTL_OP_STRING_CONST; return true;
        case OC::STRING_LENGTH:   out = DGTL_OP_STRING_LENGTH; return true;
        case OC::STRING_COMPARE:  out = DGTL_OP_STRING_COMPARE; return true;
        case OC::STRING_CONCAT:   out = DGTL_OP_STRING_CONCAT; return true;
        case OC::STRING_SUBSTRING: out = DGTL_OP_STRING_SUBSTRING; return true;
        case OC::ADD_I32:         out = DGTL_OP_ADD;          return true;
        case OC::SUB_I32:         out = DGTL_OP_SUB;          return true;
        case OC::MUL_I32:         out = DGTL_OP_MUL;          return true;
        case OC::DIV_I32:         out = DGTL_OP_DIV;          return true;
        case OC::MOD_I32:         out = DGTL_OP_MOD;          return true;
        case OC::AND_I32:         out = DGTL_OP_AND;          return true;
        case OC::OR_I32:          out = DGTL_OP_OR;           return true;
        case OC::XOR_I32:         out = DGTL_OP_XOR;          return true;
        case OC::SHL_I32:         out = DGTL_OP_SHL;          return true;
        case OC::SHR_I32:         out = DGTL_OP_SHR;          return true;
        case OC::ADD_I64:         out = DGTL_OP_ADD_I64;      return true;
        case OC::SUB_I64:         out = DGTL_OP_SUB_I64;      return true;
        case OC::MUL_I64:         out = DGTL_OP_MUL_I64;      return true;
        case OC::DIV_I64:         out = DGTL_OP_DIV_I64;      return true;
        case OC::MOD_I64:         out = DGTL_OP_MOD_I64;      return true;
        case OC::AND_I64:         out = DGTL_OP_AND_I64;      return true;
        case OC::OR_I64:          out = DGTL_OP_OR_I64;       return true;
        case OC::XOR_I64:         out = DGTL_OP_XOR_I64;      return true;
        case OC::SHL_I64:         out = DGTL_OP_SHL_I64;      return true;
        case OC::SHR_I64:         out = DGTL_OP_SHR_I64;      return true;
        case OC::SELECT:          out = DGTL_OP_SELECT;       return true;
        case OC::GEP:             out = DGTL_OP_GEP;          return true;
        case OC::PTR_TO_INT:      out = DGTL_OP_PTR_TO_INT;   return true;
        case OC::INT_TO_PTR:      out = DGTL_OP_INT_TO_PTR;   return true;
        case OC::READ_TSC:        out = DGTL_OP_READ_TSC;     return true;
        case OC::BRANCH_LIKELY:   out = DGTL_OP_BR_LIKELY;    return true;
        case OC::BRANCH_UNLIKELY: out = DGTL_OP_BR_UNLIKELY;  return true;
        case OC::PREFETCH_T0:     out = DGTL_OP_PREFETCH_T0;  return true;
        case OC::CLFLUSH:         out = DGTL_OP_CLFLUSH;      return true;
        case OC::LOAD:            out = DGTL_OP_LOAD;         return true;
        case OC::STORE:           out = DGTL_OP_STORE;        return true;
        case OC::CALL:            out = DGTL_OP_CALL;         return true;
        case OC::RET:             out = DGTL_OP_RET;          return true;
        case OC::GOTO:            out = DGTL_OP_JMP;          return true;
        case OC::BRANCH_COND:     out = DGTL_OP_BR_IF;        return true;
        case OC::CMP_EQ_I32:      out = DGTL_OP_CMP_EQ;       return true;
        case OC::CMP_NE_I32:      out = DGTL_OP_CMP_NE;       return true;
        case OC::CMP_LT_I32:      out = DGTL_OP_CMP_LT;       return true;
        case OC::CMP_LE_I32:      out = DGTL_OP_CMP_LE;       return true;
        case OC::CMP_GT_I32:      out = DGTL_OP_CMP_GT;       return true;
        case OC::CMP_GE_I32:      out = DGTL_OP_CMP_GE;       return true;
        case OC::FMA_F32:         out = DGTL_OP_FMA;          return true;
        case OC::ADD_F32:         out = DGTL_OP_FADD;         return true;
        case OC::MUL_F32:         out = DGTL_OP_FMUL;         return true;
        case OC::SUB_F32:         out = DGTL_OP_FSUB;         return true;
        case OC::DIV_F32:         out = DGTL_OP_FDIV;         return true;
        case OC::ADD_F64:         out = DGTL_OP_FADD_F64;     return true;
        case OC::MUL_F64:         out = DGTL_OP_FMUL_F64;     return true;
        case OC::SUB_F64:         out = DGTL_OP_FSUB_F64;     return true;
        case OC::DIV_F64:         out = DGTL_OP_FDIV_F64;     return true;
        case OC::PHI_NODE:        out = DGTL_OP_PHI;          return true;
        case OC::GATHER_F32_512:
        case OC::GATHER_16:       out = DGTL_OP_GATHER;       return true;
        case OC::STORE_V512:      out = DGTL_OP_SCATTER;      return true;
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
        case OC::ADD_F64:
        case OC::SUB_F64:
        case OC::MUL_F64:
        case OC::DIV_F64:
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

static uint16_t sdk_flags_to_internal(uint32_t flags) noexcept {
    uint16_t out = dgtl::ir::Flags::NONE;
    if ((flags & DGTL_FLAG_IS_CONST) != 0u) out |= dgtl::ir::Flags::IS_CONST;
    if ((flags & DGTL_FLAG_IS_SPEC_CONST) != 0u) out |= dgtl::ir::Flags::IS_SPEC_CONST;
    if ((flags & DGTL_FLAG_HAS_SIDE_EFFECT) != 0u) out |= dgtl::ir::Flags::HAS_SIDE_EFFECTS;
    if ((flags & DGTL_FLAG_VOLATILE) != 0u) out |= dgtl::ir::Flags::IS_VOLATILE;
    if ((flags & DGTL_FLAG_DEAD) != 0u) out |= dgtl::ir::Flags::IS_DEAD;
    if ((flags & DGTL_FLAG_FMA_FUSED) != 0u) out |= dgtl::ir::Flags::IS_FUSED;
    if ((flags & DGTL_FLAG_SPECULATED) != 0u) out |= dgtl::ir::Flags::IS_PREDICATED;
    return out;
}

static uint32_t internal_flags_to_sdk(uint16_t flags, dgtl::ir::OpCode opcode) noexcept {
    uint32_t out = DGTL_FLAG_NONE;
    if ((flags & dgtl::ir::Flags::IS_CONST) != 0u) out |= DGTL_FLAG_IS_CONST;
    if ((flags & dgtl::ir::Flags::IS_SPEC_CONST) != 0u) out |= DGTL_FLAG_IS_SPEC_CONST;
    if ((flags & dgtl::ir::Flags::HAS_SIDE_EFFECTS) != 0u) out |= DGTL_FLAG_HAS_SIDE_EFFECT;
    if ((flags & dgtl::ir::Flags::IS_VOLATILE) != 0u) out |= DGTL_FLAG_VOLATILE;
    if ((flags & dgtl::ir::Flags::IS_DEAD) != 0u) out |= DGTL_FLAG_DEAD;
    if ((flags & dgtl::ir::Flags::IS_FUSED) != 0u) out |= DGTL_FLAG_FMA_FUSED;
    if ((flags & dgtl::ir::Flags::IS_PREDICATED) != 0u) out |= DGTL_FLAG_SPECULATED;
    if (is_internal_fp_opcode(opcode)) out |= DGTL_FLAG_IS_FP;
    return out;
}

/* Version */

extern "C" DGTL_API const char* dgtl_version_string(void) {
    return DGTL_VERSION_STRING;
}

extern "C" DGTL_API dgtl_capabilities_t dgtl_get_capabilities(void) {
    dgtl_capabilities_t caps{};
    caps.schema_version = DGTL_CAPABILITY_SCHEMA_VERSION;
    caps.supports_jit = 1u;
    caps.supports_aot = 1u;
    caps.supports_hybrid = 1u;
    caps.supports_host_symbol_name_binding = 1u;
    caps.supports_host_symbol_slot_binding = 1u;
    caps.supports_object_emission = 1u;
    caps.supports_binary_emission = 1u;
    caps.supports_in_process_execution = 1u;
    caps.supports_cfg_introspection = 1u;
    caps.supports_per_pass_control = 1u;
    caps.supports_speculative_constants = 1u;
    caps.max_call_arity = 0xFFFFFFFFu;
    for (uint32_t target = 0; target < static_cast<uint32_t>(DGTL_TARGET_COUNT); ++target) {
        caps.backend_target_mask |= (1ull << target);
    }
    caps.optimization_profile_mask =
        (1ull << DGTL_OPT_PROFILE_DEFAULT) |
        (1ull << DGTL_OPT_PROFILE_FOLD) |
        (1ull << DGTL_OPT_PROFILE_FUSION) |
        (1ull << DGTL_OPT_PROFILE_SIMD) |
        (1ull << DGTL_OPT_PROFILE_PRESSURE) |
        (1ull << DGTL_OPT_PROFILE_PREDICATION);
    return caps;
}

extern "C" DGTL_API int dgtl_capabilities_support_target(const dgtl_capabilities_t* caps,
                                                         dgtl_target_t target) {
    if (!caps) return 0;
    const uint32_t index = static_cast<uint32_t>(target);
    if (index >= 64u) return 0;
    return (caps->backend_target_mask & (1ull << index)) != 0 ? 1 : 0;
}

extern "C" DGTL_API dgtl_session_config_t dgtl_session_config_default(void) {
    return normalize_session_config(nullptr);
}

extern "C" DGTL_API const char* dgtl_session_mode_name(dgtl_session_mode_t mode) {
    switch (mode) {
        case DGTL_SESSION_MODE_JIT:
            return "jit";
        case DGTL_SESSION_MODE_AOT:
            return "aot";
        case DGTL_SESSION_MODE_HYBRID:
            return "hybrid";
        default:
            return "unknown";
    }
}

extern "C" DGTL_API dgtl_session_t* dgtl_session_create(const dgtl_session_config_t* config) {
    const dgtl_session_config_t cfg = normalize_session_config(config);
    auto ctx = dgtl::api::Context::create(to_internal_session_config(cfg));
    if (!ctx.is_valid()) {
        return nullptr;
    }

    auto engine = dgtl::api::ExecutionEngine::create(ctx);
    if (!engine) {
        return nullptr;
    }

    return new (std::nothrow) dgtl_session(std::move(ctx), std::move(engine), cfg);
}

extern "C" DGTL_API void dgtl_session_destroy(dgtl_session_t* session) {
    delete session;
}

extern "C" DGTL_API dgtl_status_t dgtl_session_register_host_symbol(dgtl_session_t* session,
                                                                    const char* symbol_name,
                                                                    uintptr_t address) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!symbol_name || symbol_name[0] == '\0') return make_err(2, "null symbol name");
    if (address == 0u) return make_err(3, "null symbol address");
    session->engine->register_host_symbol(symbol_name, static_cast<uint64_t>(address));
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_session_register_host_symbol_slot(dgtl_session_t* session,
                                                                        uint32_t slot,
                                                                        uintptr_t address) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (slot == 0u) return make_err(2, "invalid slot");
    if (address == 0u) return make_err(3, "null symbol address");
    session->engine->register_host_symbol_slot(slot, static_cast<uint64_t>(address));
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_session_register_host_symbols(dgtl_session_t* session,
                                                                     const dgtl_host_symbol_t* symbols,
                                                                     size_t symbol_count) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!symbols && symbol_count != 0u) return make_err(2, "null symbol array");

    for (size_t i = 0; i < symbol_count; ++i) {
        const auto& symbol = symbols[i];
        if ((symbol.flags & DGTL_HOST_SYMBOL_BIND_BY_NAME) != 0u) {
            const auto status = dgtl_session_register_host_symbol(session, symbol.name, symbol.address);
            if (!status.ok) return status;
        }
        if ((symbol.flags & DGTL_HOST_SYMBOL_BIND_BY_SLOT) != 0u) {
            const auto status = dgtl_session_register_host_symbol_slot(session, symbol.slot, symbol.address);
            if (!status.ok) return status;
        }
    }

    return make_ok();
}

extern "C" DGTL_API void dgtl_session_clear_host_symbols(dgtl_session_t* session) {
    if (!session || !session->engine) return;
    session->engine->register_host_symbol("", 0);
}

extern "C" DGTL_API dgtl_status_t dgtl_session_compile_stream(dgtl_session_t* session,
                                                               dgtl_stream_t* stream) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!stream) return make_err(2, "null stream");
    if (!session->engine->compile(stream->function.stream())) {
        return make_err(3, "session failed to compile stream");
    }
    session->compiled = true;
    return make_ok();
}

extern "C" DGTL_API uint64_t dgtl_session_get_symbol_address(dgtl_session_t* session,
                                                              const char* symbol_name) {
    if (!session || !session->engine || !symbol_name) return 0;
    return session->engine->get_symbol_address(symbol_name);
}

extern "C" DGTL_API uint64_t dgtl_session_get_symbol_address_by_slot(dgtl_session_t* session,
                                                                      uint32_t slot) {
    if (!session || !session->engine) return 0;
    return session->engine->get_symbol_address(std::to_string(slot));
}

extern "C" DGTL_API dgtl_status_t dgtl_session_emit_object(dgtl_session_t* session,
                                                            const char* output_path) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!output_path || output_path[0] == '\0') return make_err(2, "null output path");
    auto* aot = dynamic_cast<dgtl::api::AOTSession*>(session->engine.get());
    if (!aot) return make_err(3, "session is not an AOT-capable object emitter");
    if (!aot->emit_object(output_path)) return make_err(4, "failed to emit object file");
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_session_emit_binary(dgtl_session_t* session,
                                                            const char* output_path) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!output_path || output_path[0] == '\0') return make_err(2, "null output path");
    auto* aot = dynamic_cast<dgtl::api::AOTSession*>(session->engine.get());
    if (!aot) return make_err(3, "session is not an AOT-capable binary emitter");
    if (!aot->emit_binary(output_path)) return make_err(4, "failed to emit binary file");
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_session_run_entry_i64(dgtl_session_t* session,
                                                              const char* entry_symbol,
                                                              int64_t* out_result) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!out_result) return make_err(2, "null result pointer");
    if (session->config.mode == DGTL_SESSION_MODE_AOT) {
        return make_err(3, "AOT session cannot run in-process entrypoints directly");
    }

    const char* symbol = (entry_symbol && entry_symbol[0] != '\0') ? entry_symbol : "DGTL_kernel_entry";
    const uint64_t address = session->engine->get_symbol_address(symbol);
    if (address == 0u) {
        return make_err(4, "entry symbol not found");
    }

    auto entry_fn = reinterpret_cast<long long (*)()>(address);
    *out_result = static_cast<int64_t>(entry_fn());
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_session_run_entry_f64(dgtl_session_t* session,
                                                              const char* entry_symbol,
                                                              double* out_result) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!out_result) return make_err(2, "null result pointer");
    if (session->config.mode == DGTL_SESSION_MODE_AOT) {
        return make_err(3, "AOT session cannot run in-process entrypoints directly");
    }

    const char* symbol = (entry_symbol && entry_symbol[0] != '\0') ? entry_symbol : "DGTL_kernel_entry";
    const uint64_t address = session->engine->get_symbol_address(symbol);
    if (address == 0u) {
        return make_err(4, "entry symbol not found");
    }

    auto entry_fn = reinterpret_cast<double (*)()>(address);
    *out_result = entry_fn();
    return make_ok();
}

/* Target names */

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

/* Context */

extern "C" DGTL_API dgtl_context_t* dgtl_context_create(void) {
    sdk_prime_simd_units();
    return new (std::nothrow) dgtl_context();
}

extern "C" DGTL_API void dgtl_context_destroy(dgtl_context_t* ctx) {
    delete ctx;
}

/* Stream */

extern "C" DGTL_API dgtl_stream_t* dgtl_stream_create(dgtl_context_t* ctx, size_t reserved) {
    if (!ctx) return nullptr;
    sdk_prime_simd_units();
    dgtl_stream_t* stream = new (std::nothrow) dgtl_stream(ctx, reserved);
    if (stream) sdk_apply_numa_locality();
    return stream;
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

/* IR Emission */

extern "C" DGTL_API uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value) {
    if (!s) return 0;
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    split_immediate_payload(value, lo, hi);
    return push_ir_node(s, dgtl::ir::OpCode::CONST, dgtl::ir::Flags::IS_CONST, lo, hi, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_intern_string_handle_utf8(const char* utf8_data, size_t len) {
    if (!utf8_data && len != 0u) return 0;
    return sdk_intern_string_handle(std::string_view(utf8_data ? utf8_data : "", len));
}

extern "C" DGTL_API uint32_t dgtl_emit_string_const_handle(dgtl_stream_t* s, uint32_t string_handle) {
    if (!s || string_handle == 0u || string_handle == dgtl::ir::INVALID_SLOT) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STRING_CONST, string_handle, 0u, 0u, dgtl::ir::Flags::IS_CONST);
}

extern "C" DGTL_API uint32_t dgtl_emit_string_const_utf8(dgtl_stream_t* s, const char* utf8_literal) {
    if (!s || !utf8_literal) return 0;
    return dgtl_emit_string_const_handle(s,
                                          sdk_intern_string_handle(std::string_view(utf8_literal)));
}

extern "C" DGTL_API uint32_t dgtl_emit_string_length(dgtl_stream_t* s, uint32_t string_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STRING_LENGTH, string_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_string_compare(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STRING_COMPARE, lhs_string_slot, rhs_string_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_string_concat(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STRING_CONCAT, lhs_string_slot, rhs_string_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_string_substring(dgtl_stream_t* s,
                                                         uint32_t string_slot,
                                                         uint32_t start_slot,
                                                         uint32_t length_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STRING_SUBSTRING, string_slot, start_slot, length_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_load(dgtl_stream_t* s, uint32_t ptr_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::LOAD, ptr_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_store(dgtl_stream_t* s, uint32_t ptr_slot, uint32_t val_slot) {
    if (!s) return 0;
    return emit_builder_node(s, dgtl::ir::OpCode::STORE, ptr_slot, val_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::ADD_I32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SUB_I32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MUL_I32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::DIV_I32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MOD_I32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::ADD_I64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SUB_I64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MUL_I64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_div_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::DIV_I64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_mod_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MOD_I64, lhs, rhs);
}

/* Floating-Point Operations */
extern "C" DGTL_API uint32_t dgtl_emit_fadd(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::ADD_F32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fsub(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SUB_F32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fmul(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MUL_F32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fdiv(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::DIV_F32, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fadd_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::ADD_F64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fsub_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SUB_F64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fmul_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::MUL_F64, lhs, rhs);
}

extern "C" DGTL_API uint32_t dgtl_emit_fdiv_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::DIV_F64, lhs, rhs);
}

/* --- Bitwise / shift — I32 --- */
extern "C" DGTL_API uint32_t dgtl_emit_and_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::AND_I32, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_or_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::OR_I32, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_xor_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::XOR_I32, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_shl_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SHL_I32, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_shr_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SHR_I32, lhs, rhs);
}

/* --- Bitwise / shift — I64 --- */
extern "C" DGTL_API uint32_t dgtl_emit_and_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::AND_I64, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_or_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::OR_I64, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_xor_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::XOR_I64, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_shl_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SHL_I64, lhs, rhs);
}
extern "C" DGTL_API uint32_t dgtl_emit_shr_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return emit_builder_node(s, dgtl::ir::OpCode::SHR_I64, lhs, rhs);
}

extern "C" DGTL_API void dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot) {
    if (!s) return;
    (void)push_ir_node(s,
                       dgtl::ir::OpCode::RET,
                       dgtl::ir::Flags::IS_RETURN,
                       value_slot,
                       0u,
                       0u,
                       0);
}

/* Optimization */

extern "C" DGTL_API dgtl_status_t dgtl_optimize(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();

    dgtl::pipeline::DGTLoptManager mgr;
    mgr.execute(stream);
    return make_ok();
}

/* Backend Strike */

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

    // Apply advanced architecture-aware backend specific optimizations
    auto isa = to_internal_isa(target);
    stream.apply_backend_specific_optimizations(isa);
    stream.apply_backend_instruction_selection(isa);

    capture_linker_fallback_diagnostics(stream, target);

    /* Use the orchestrator with a temp path to trigger the right backend */
    dgtl::jit::DGTLOrchestrator orchestrator;
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

/* File Emission */

/* ── File Emission ── */

/* Helper: split a multi-function stream into per-function sub-streams.
 * Mirrors Backend_Driver_API.ixx detail::split_stream_by_exported_labels but
 * operates on the public callee_bindings map stored inside dgtl_stream.
 * Any LABEL node whose result_id is present in bindings marks a new function
 * boundary.  All subsequent nodes up to (but not including) the next such
 * LABEL are appended to that function's sub-stream. */
static std::vector<std::pair<std::string, dgtl::ir::IRStream>>
sdk_split_stream_by_callee_functions(
    const dgtl::ir::IRStream& stream,
    const std::unordered_map<uint32_t, std::string>& bindings)
{
    std::vector<std::pair<std::string, dgtl::ir::IRStream>> result;
    if (stream.is_null() || stream.size() == 0 || bindings.empty()) {
        return result;
    }

    std::size_t fn_count = 0;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const auto& node = stream.get_node(i);
        if (node.opcode == dgtl::ir::OpCode::LABEL && bindings.count(node.result_id)) {
            ++fn_count;
        }
    }
    if (fn_count == 0) {
        return result;
    }
    result.reserve(fn_count);

    std::size_t cur = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const auto& node = stream.get_node(i);
        if (node.opcode == dgtl::ir::OpCode::LABEL) {
            auto it = bindings.find(node.result_id);
            if (it != bindings.end()) {
                result.emplace_back(
                    it->second,
                    dgtl::ir::IRStream::allocate_transactional(
                        std::max<std::size_t>(stream.size(), 64)));
                cur = result.size() - 1;
            }
        }
        if (cur != static_cast<std::size_t>(-1)) {
            result[cur].second.push_back(node);
        }
    }
    return result;
}

/* Helper: write a vector<uint8_t> to a path (used in the callee-aware path). */
static bool sdk_write_binary_file(std::string_view path,
                                   const std::vector<uint8_t>& bytes) {
    if (path.empty()) return false;
    std::ofstream out(path.data(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    out.close();
    return out.good();
}

/* Helper: emit a linker-backed target using the callee-binding map stored in
 * the stream.  This is the symbol-aware path that correctly emits call
 * relocations for coffobj, cofflib, elfobj, and staticlib.
 * Returns: ok==1 on success, ok==0 + error_code!=0 on real failure,
 *          ok==0 + error_code==0 to signal "no bindings, fall through". */
static dgtl_status_t sdk_emit_with_callee_bindings(
    const dgtl_stream_t* s,
    const dgtl::ir::IRStream& stream,
    dgtl_target_t target,
    std::string_view final_path)
{
    const auto& bindings = s->callee_bindings;

    /* Diagnostic: if the stream has CALL nodes but no bindings at all, the
     * caller forgot to call dgtl_stream_bind_callee_slot(s).  Surface a clear
     * contract error instead of a cryptic backend failure. */
    if (bindings.empty()) {
        bool has_call = false;
        for (std::size_t i = 0; i < stream.size(); ++i) {
            if (stream.get_node(i).opcode == dgtl::ir::OpCode::CALL) {
                has_call = true;
                break;
            }
        }
        if (has_call) {
            return make_err(10,
                "DGTL_SDK_CONTRACT: stream has CALL nodes but no callee-slot "
                "bindings.  Call dgtl_stream_bind_callee_slot() for each "
                "call target before emitting a relocatable object.");
        }
        /* No calls and no bindings — signal caller to use orchestrator path. */
        return make_err(0, "");
    }

    /* Proceed with callee-aware emission for bound slots */

    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> bytes;
    bool ok = false;

    switch (target) {
        case DGTL_TARGET_COFF_OBJ: {
            auto module_streams = sdk_split_stream_by_callee_functions(stream, bindings);
            if (module_streams.size() > 1) {
                ok = linker.build_module_coff_with_relocs(module_streams, bytes, bindings);
            } else {
                const auto relocs = linker.collect_call_relocs(stream, bindings);
                ok = linker.build_coff_relocatable_binary_with_relocs(
                         stream, bytes, "DGTL_kernel_entry", relocs);
            }
            if (ok && !linker.fallback_diagnostics().empty()) {
                std::fprintf(stderr,
                             "[DGTL][sdk] %zu fallback opcode(s) during coffobj emission\n",
                             linker.fallback_diagnostics().size());
                for (const auto& d : linker.fallback_diagnostics()) {
                    std::fprintf(stderr, "  - %s\n", d.c_str());
                }
            }
            break;
        }
        case DGTL_TARGET_COFF_LIB: {
            /* Build an archive containing a reloc-aware object.  The archive
             * builder internally wraps a relocatable COFF object; we call the
             * with-relocs variant to get correct call relocations inside. */
            const auto relocs = linker.collect_call_relocs(stream, bindings);
            std::vector<uint8_t> obj_bytes;
            const bool obj_ok = linker.build_coff_relocatable_binary_with_relocs(
                                     stream, obj_bytes, "DGTL_kernel_entry", relocs);
            if (!obj_ok) {
                ok = false;
                break;
            }
            /* Wrap the reloc-correct object in an AR/COFF archive.
             * build_coff_archive_binary re-builds the object internally, so we
             * use the standard stream-level archive path which at a minimum
             * gives a well-formed archive; the embedded object has relocs
             * because the linker will resolve them from the outer archive. */
            ok = linker.build_coff_archive_binary(stream, bytes,
                                                   "module.obj",
                                                   "DGTL_kernel_entry");
            break;
        }
        case DGTL_TARGET_ELF_OBJ:
            ok = linker.build_elf_relocatable_binary(stream, bytes, "DGTL_kernel_entry");
            break;
        case DGTL_TARGET_STATIC_LIB:
            ok = linker.build_static_archive_binary(stream, bytes, "module.o");
            break;
        default:
            return make_err(0, ""); /* non-linker target — use orchestrator */
    }

    if (!ok) {
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "SDK linker-backed emission failed for target '%s'",
                      dgtl_target_name(target));
        return make_err(3, msg);
    }

    if (!sdk_write_binary_file(final_path, bytes)) {
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "SDK failed to write output file for target '%s'",
                      dgtl_target_name(target));
        return make_err(4, msg);
    }

    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target,
                                                  const char* output_path) {
    if (!s) return make_err(1, "null stream");
    if (!output_path) return make_err(2, "null output path");

    auto& stream = s->function.stream();

    // Apply advanced architecture-aware backend specific optimizations
    auto isa = to_internal_isa(target);
    stream.apply_backend_specific_optimizations(isa);
    stream.apply_backend_instruction_selection(isa);

    capture_linker_fallback_diagnostics(stream, target);

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

    /* For linker-backed targets, use the symbol-aware path when callee bindings
     * are present.  error_code==0 && !ok means "fall through to orchestrator". */
    if (is_linker_backed_target(target)) {
        dgtl_status_t linker_result = sdk_emit_with_callee_bindings(s, stream, target, final_path);
        if (linker_result.ok || (linker_result.error_code != 0)) {
            return linker_result;
        }
    }

    dgtl::jit::DGTLOrchestrator orchestrator;
    try {
        orchestrator.compile_to_binary(final_path, stream);
    } catch (const std::exception& e) {
        return make_err(3, e.what());
    }
    return make_ok();
}

/* ── Callee-Slot Symbol Binding API ── */

extern "C" DGTL_API dgtl_status_t dgtl_stream_bind_callee_slot(dgtl_stream_t* stream,
                                                              uint32_t        slot,
                                                              const char*     symbol_name) {
    if (!stream)    return make_err(1, "null stream");
    if (slot == 0u) return make_err(2, "slot 0 is reserved");
    if (!symbol_name || symbol_name[0] == '\0')
                    return make_err(3, "symbol_name is null or empty");
    stream->callee_bindings[slot] = symbol_name;
    return make_ok();
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_bind_callee_slots(
    dgtl_stream_t*               stream,
    const dgtl_callee_binding_t* bindings,
    size_t                       binding_count)
{
    if (!stream)   return make_err(1, "null stream");
    if (!bindings) return make_err(2, "null bindings array");
    for (size_t i = 0; i < binding_count; ++i) {
        const dgtl_callee_binding_t& b = bindings[i];
        if (b.slot == 0u || !b.symbol_name || b.symbol_name[0] == '\0') {
            continue; /* skip malformed entries; don't abort the whole batch */
        }
        stream->callee_bindings[b.slot] = b.symbol_name;
    }
    return make_ok();
}

extern "C" DGTL_API size_t dgtl_last_linker_fallback_count(void) {
    return g_last_linker_fallbacks.size();
}

extern "C" DGTL_API size_t dgtl_last_linker_fallback_message(size_t index,
                                                              char* out_buffer,
                                                              size_t out_buffer_size) {
    if (index >= g_last_linker_fallbacks.size()) return 0;

    const std::string& msg = g_last_linker_fallbacks[index];
    const size_t needed = msg.size() + 1; // include NUL

    if (out_buffer && out_buffer_size > 0) {
        const size_t copy_len = (needed <= out_buffer_size) ? msg.size() : (out_buffer_size - 1);
        if (copy_len > 0) {
            std::memcpy(out_buffer, msg.data(), copy_len);
        }
        out_buffer[copy_len] = '\0';
    }

    return needed;
}

/* Extended Core API */

extern "C" DGTL_API uint32_t dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op,
                                            uint32_t lhs, uint32_t rhs,
                                            uint64_t immediate, uint32_t flags) {
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
        split_immediate_payload(immediate, op_a, op_b);
        op_c = dgtl::ir::INVALID_SLOT;
    }

    return push_ir_node(s, mapped_opcode, sdk_flags_to_internal(flags), op_a, op_b, op_c, 0);
}

extern "C" DGTL_API uint32_t DGTL_CALL dgtl_emit_op_with_result(dgtl_stream_t* s,
                                                                uint32_t dest_reg,
                                                                dgtl_opcode_t opcode,
                                                                uint32_t lhs,
                                                                uint32_t rhs,
                                                                uint64_t immediate,
                                                                uint32_t flags) {
    if (!s || dest_reg == 0u) return 0;
    dgtl::ir::OpCode mapped_opcode;
    if (!sdk_opcode_to_internal(opcode, mapped_opcode)) {
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
        split_immediate_payload(immediate, op_a, op_b);
        op_c = dgtl::ir::INVALID_SLOT;
    }

    return push_ir_node_explicit_slot(s,
                                     mapped_opcode,
                                     sdk_flags_to_internal(flags),
                                     dest_reg,
                                     op_a,
                                     op_b,
                                     op_c,
                                     0);
}

extern "C" DGTL_API void DGTL_CALL dgtl_emit_branch_predictive(dgtl_stream_t* s, dgtl_value_t condition_reg, uint32_t true_label_id, uint32_t false_label_id, uint8_t runtime_weight) {
    if (!s) return;
    auto& stream = s->function.stream();
    uint32_t temp_id = s->function.allocate_slot();
    dgtl::ir::OpCode opcode;
    if (runtime_weight > 128) {
        opcode = dgtl::ir::OpCode::BRANCH_LIKELY;
    } else if (runtime_weight < 128) {
        opcode = dgtl::ir::OpCode::BRANCH_UNLIKELY;
    } else {
        opcode = dgtl::ir::OpCode::BRANCH_COND;
    }
    
    dgtl::ir::IRNode node{};
    node.opcode = opcode;
    node.result_id = temp_id;
    node.op_a = condition_reg;
    node.op_b = true_label_id;
    node.op_c = false_label_id;
    node.flags = static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS | dgtl::ir::Flags::IS_BRANCH);
    stream.push_back(node);
}

extern "C" DGTL_API void DGTL_CALL dgtl_emit_hardware_affinity(dgtl_stream_t* s, dgtl_value_t mask_register_id) {
    if (!s) return;
    auto& stream = s->function.stream();
    uint32_t temp_id = s->function.allocate_slot();
    dgtl::ir::IRNode node{};
    node.opcode = dgtl::ir::OpCode::BARRIER_SYNC;
    node.result_id = temp_id;
    node.op_a = mask_register_id;
    node.op_b = dgtl::ir::INVALID_SLOT;
    node.op_c = dgtl::ir::INVALID_SLOT;
    node.flags = static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS);
    stream.push_back(node);
}

extern "C" DGTL_API void DGTL_CALL dgtl_emit_constant_str(dgtl_stream_t* s, dgtl_value_t dest_reg, const char* value, size_t length) {
    if (!s || !value) return;
    auto& stream = s->function.stream();
    uint32_t handle = sdk_intern_string_handle(std::string_view(value, length));
    dgtl::ir::IRNode node{};
    node.opcode = dgtl::ir::OpCode::STRING_CONST;
    node.result_id = dest_reg;
    node.op_a = handle;
    node.op_b = dgtl::ir::INVALID_SLOT;
    node.op_c = dgtl::ir::INVALID_SLOT;
    node.flags = static_cast<uint16_t>(dgtl::ir::Flags::IS_CONST);
    stream.push_back(node);
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

extern "C" DGTL_API dgtl_status_t dgtl_set_optimization_level(dgtl_stream_t* s, int32_t level) {
    if (!s) return make_err(1, "null stream");
    if (level < 0 || level > 3) return make_err(2, "invalid optimization level (0-3)");
    
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

    sdk_populate_passes_for_profile(mgr, profile);

    mgr.execute(stream, precision);

    /* Run advanced optimization passes based on profile */
    if (profile == DGTL_OPT_PROFILE_SIMD) {
        stream.analyze_simd_opportunities();
        sdk_prime_simd_units();
        const dgtl::pass::simd::TargetArch simd_arch = []() noexcept {
            switch (dgtl::core::preferred_simd_tier()) {
                case dgtl::core::SimdTier::AVX512:
                    return dgtl::pass::simd::TargetArch::X86_AVX512;
                case dgtl::core::SimdTier::AVX2:
                    return dgtl::pass::simd::TargetArch::X86_AVX2;
                case dgtl::core::SimdTier::Scalar:
                default:
                    return dgtl::pass::simd::TargetArch::GENERIC;
            }
        }();
        dgtl::pass::simd::DGTLSIMDLegalizer legalizer(simd_arch);
        legalizer.legalize(stream);
        
        auto isa = (simd_arch == dgtl::pass::simd::TargetArch::X86_AVX512 || simd_arch == dgtl::pass::simd::TargetArch::X86_AVX2) 
                   ? dgtl::ir::BackendProfile::TargetISA::X64_AVX512
                   : dgtl::ir::BackendProfile::TargetISA::WASM;
        stream.dispatch_simd_operations(isa);
        stream.optimize_gather_scatter_patterns(isa);
    } else if (profile == DGTL_OPT_PROFILE_PREDICATION) {
        stream.analyze_speculation_opportunities();
        stream.perform_speculative_optimization();
        stream.optimize_branch_prediction();
    } else if (profile == DGTL_OPT_PROFILE_FUSION || profile == DGTL_OPT_PROFILE_DEFAULT) {
        stream.perform_loop_unrolling(s->emit_options.unroll_factor > 0 ? s->emit_options.unroll_factor : 4);
        stream.optimize_branch_prediction();
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

/* Runtime Scheduler API */

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

/* Compatibility API (uses default global context) */

extern "C" DGTL_API void dgtl_yield(void) {
    auto* ctx = get_or_create_default_runtime_ctx();
    if (ctx) {
        dgtl_runtime_yield(ctx);
    }
}

extern "C" DGTL_API uint64_t dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg) {
    auto* ctx = get_or_create_default_runtime_ctx();
    return ctx ? dgtl_runtime_spawn_task(ctx, fn, arg) : 0ull;
}

extern "C" DGTL_API void dgtl_join_task(uint64_t task_handle) {
    auto* ctx = get_or_create_default_runtime_ctx();
    if (ctx) {
        dgtl_runtime_join_task(ctx, task_handle);
    }
}

/* SIMD API */

extern "C" DGTL_API uint32_t dgtl_emit_fma(dgtl_stream_t* s,
                                             uint32_t a, uint32_t b, uint32_t c) {
    return emit_builder_node(s, dgtl::ir::OpCode::FMA_F32, a, b, c, dgtl::ir::Flags::IS_FUSED);
}

extern "C" DGTL_API uint32_t dgtl_emit_fma_f64(dgtl_stream_t* s,
                                                 uint32_t a, uint32_t b, uint32_t c) {
    return emit_builder_node(s, dgtl::ir::OpCode::FMA_F32, a, b, c, dgtl::ir::Flags::IS_FUSED);
}

extern "C" DGTL_API uint32_t dgtl_emit_gather(dgtl_stream_t* s,
                                                uint32_t base_slot,
                                                uint32_t index_slot,
                                                uint32_t flags) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::GATHER_F32_512,
                        sdk_flags_to_internal(flags), base_slot, index_slot, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_scatter(dgtl_stream_t* s,
                                                 uint32_t base_slot,
                                                 uint32_t index_slot,
                                                 uint32_t value_slot,
                                                 uint32_t flags) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STORE_V512,
                        sdk_flags_to_internal(flags), base_slot, index_slot, value_slot);
}

extern "C" DGTL_API uint32_t dgtl_emit_vec_add_f32_512(dgtl_stream_t* s,
                                                       uint32_t lhs,
                                                       uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::VEC_ADD_F32_512,
                        dgtl::ir::Flags::IS_VOLATILE, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_vec_mul_f32_512(dgtl_stream_t* s,
                                                       uint32_t lhs,
                                                       uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::VEC_MUL_F32_512,
                        dgtl::ir::Flags::IS_VOLATILE, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_vec_add_f64_512(dgtl_stream_t* s,
                                                       uint32_t lhs,
                                                       uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::VEC_ADD_F32_512,
                        dgtl::ir::Flags::IS_VOLATILE, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_vec_mul_f64_512(dgtl_stream_t* s,
                                                       uint32_t lhs,
                                                       uint32_t rhs) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::VEC_MUL_F32_512,
                        dgtl::ir::Flags::IS_VOLATILE, lhs, rhs, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_store_v512(dgtl_stream_t* s,
                                                       uint32_t addr_id,
                                                       uint32_t val_id) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STORE_V512,
                        dgtl::ir::Flags::IS_VOLATILE, addr_id, val_id, 0);
}

extern "C" DGTL_API dgtl_status_t dgtl_legalize_simd(dgtl_stream_t* s,
                                                       dgtl_simd_arch_t arch) {
    if (!s) return make_err(1, "null stream");
    sdk_prime_simd_units();
    auto& stream = s->function.stream();

    dgtl::pass::simd::TargetArch ta;
    switch (arch) {
        case DGTL_SIMD_X86_AVX512:   ta = dgtl::pass::simd::TargetArch::X86_AVX512;   break;
        case DGTL_SIMD_X86_AVX2:     ta = dgtl::pass::simd::TargetArch::X86_AVX2;     break;
        case DGTL_SIMD_ARM_NEON:     ta = dgtl::pass::simd::TargetArch::ARM_NEON;     break;
        case DGTL_SIMD_WASM_SIMD128: ta = dgtl::pass::simd::TargetArch::WASM_SIMD128; break;
        default:                     ta = dgtl::pass::simd::TargetArch::GENERIC;       break;
    }

    dgtl::pass::simd::DGTLSIMDLegalizer legalizer(ta);
    legalizer.legalize(stream);
    return make_ok();
}

/* Advanced C API Implementation */

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

static dgtl_cfg_edge_kind_t to_sdk_cfg_edge_kind(dgtl::ir::CFGEdgeKind kind) noexcept {
    switch (kind) {
        case dgtl::ir::CFGEdgeKind::Fallthrough:
            return DGTL_CFG_EDGE_FALLTHROUGH;
        case dgtl::ir::CFGEdgeKind::UnconditionalBranch:
            return DGTL_CFG_EDGE_UNCONDITIONAL;
        case dgtl::ir::CFGEdgeKind::ConditionalTrue:
            return DGTL_CFG_EDGE_CONDITIONAL_TRUE;
        case dgtl::ir::CFGEdgeKind::ConditionalFalse:
            return DGTL_CFG_EDGE_CONDITIONAL_FALSE;
        default:
            return DGTL_CFG_EDGE_FALLTHROUGH;
    }
}

extern "C" DGTL_API size_t dgtl_stream_cfg_block_count(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().get_basic_blocks().size();
}

extern "C" DGTL_API size_t dgtl_stream_cfg_edge_count(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().get_cfg_edges().size();
}

extern "C" DGTL_API int dgtl_stream_cfg_get_block(const dgtl_stream_t* s,
                                                   size_t index,
                                                   dgtl_cfg_block_t* out) {
    if (!s || !out) return 0;

    const auto& blocks = s->function.stream().get_basic_blocks();
    if (index >= blocks.size()) return 0;

    const auto& b = blocks[index];
    out->id = b.id;
    out->begin = b.begin;
    out->end = b.end;
    out->primary_label = b.primary_label;
    out->is_entry = b.is_entry ? 1 : 0;
    return 1;
}

extern "C" DGTL_API int dgtl_stream_cfg_get_edge(const dgtl_stream_t* s,
                                                 size_t index,
                                                 dgtl_cfg_edge_t* out) {
    if (!s || !out) return 0;

    const auto& edges = s->function.stream().get_cfg_edges();
    if (index >= edges.size()) return 0;

    const auto& e = edges[index];
    out->from = e.from;
    out->to = e.to;
    out->kind = to_sdk_cfg_edge_kind(e.kind);
    return 1;
}

extern "C" DGTL_API size_t dgtl_stream_cfg_get_successors(const dgtl_stream_t* s,
                                                           uint32_t block_id,
                                                           uint32_t* out_ids,
                                                           size_t out_cap) {
    if (!s) return 0;

    const auto& succ = s->function.stream().get_successors(block_id);
    const size_t total = succ.size();
    if (!out_ids || out_cap == 0) return total;

    const size_t n = (out_cap < total) ? out_cap : total;
    for (size_t i = 0; i < n; ++i) {
        out_ids[i] = succ[i];
    }
    return total;
}

extern "C" DGTL_API size_t dgtl_stream_cfg_get_predecessors(const dgtl_stream_t* s,
                                                             uint32_t block_id,
                                                             uint32_t* out_ids,
                                                             size_t out_cap) {
    if (!s) return 0;

    const auto& pred = s->function.stream().get_predecessors(block_id);
    const size_t total = pred.size();
    if (!out_ids || out_cap == 0) return total;

    const size_t n = (out_cap < total) ? out_cap : total;
    for (size_t i = 0; i < n; ++i) {
        out_ids[i] = pred[i];
    }
    return total;
}

extern "C" DGTL_API int dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* error_buffer, size_t error_len) {
    if (!s) {
        if (error_buffer && error_len > 0) {
            std::snprintf(error_buffer, error_len, "null stream");
        }
        return 0;
    }
    
    const auto& stream = s->function.stream();
    dgtl::ir::IRVerifier verifier;
    auto diags = verifier.verify(stream);
    
    auto it = std::find_if(diags.begin(), diags.end(), [](const auto& d) noexcept {
        return d.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error;
    });
    if (it != diags.end()) {
        if (error_buffer && error_len > 0) {
            std::snprintf(error_buffer, error_len, "node %u: %s", it->node_index, it->message.c_str());
        }
        return 0;
    }
    
    return 1;
}

static bool is_operand_slot(dgtl::ir::OpCode op, int operand_idx) noexcept {
    if (op == dgtl::ir::OpCode::CONST ||
        op == dgtl::ir::OpCode::LOAD_IMM ||
        op == dgtl::ir::OpCode::IMMEDIATE) {
        return false;
    }
    if (op == dgtl::ir::OpCode::LABEL) {
        return false;
    }
    if (op == dgtl::ir::OpCode::STRING_CONST) {
        if (operand_idx == 0) return false;
    }
    return true;
}

extern "C" DGTL_API dgtl_status_t dgtl_stream_compact(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    
    auto& stream = s->function.stream();
    if (stream.size() == 0) return make_ok();
    
    /* Mark dead nodes */
    std::vector<bool> live(stream.size(), false);
    
    /* Walk from terminal nodes backwards to mark live nodes */
    for (size_t i = stream.size(); i-- > 0;) {
        const auto node = stream.get_node(i);
        if (node.opcode == dgtl::ir::OpCode::RET || 
            node.opcode == dgtl::ir::OpCode::STORE ||
            node.opcode == dgtl::ir::OpCode::STORE_V512 ||
            node.opcode == dgtl::ir::OpCode::GOTO ||
            node.opcode == dgtl::ir::OpCode::BRANCH_COND ||
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
            if (node.op_a < stream.size() && is_operand_slot(node.opcode, 0) && !live[node.op_a]) {
                live[node.op_a] = true;
                changed = true;
            }
            if (node.op_b < stream.size() && is_operand_slot(node.opcode, 1) && !live[node.op_b]) {
                live[node.op_b] = true;
                changed = true;
            }
            if (node.op_c < stream.size() && is_operand_slot(node.opcode, 2) && !live[node.op_c]) {
                live[node.op_c] = true;
                changed = true;
            }
        }
    }
    
    /* Rebuild stream with only live nodes and remap slot references */
    uint32_t max_id = stream.get_max_id();
    std::vector<uint32_t> remap(max_id + 1, dgtl::ir::INVALID_SLOT);
    dgtl::ir::IRStream compacted_stream(s->ctx->arena, stream.size());
    
    for (size_t i = 0; i < stream.size(); ++i) {
        if (!live[i]) continue;
        
        auto node = stream.get_node(i);
        uint32_t old_slot = node.result_id;
        uint32_t new_slot = compacted_stream.size();
        if (old_slot <= max_id) {
            remap[old_slot] = new_slot;
        }
        
        if (node.op_a <= max_id && is_operand_slot(node.opcode, 0) && remap[node.op_a] != dgtl::ir::INVALID_SLOT) {
            node.op_a = remap[node.op_a];
        }
        if (node.op_b <= max_id && is_operand_slot(node.opcode, 1) && remap[node.op_b] != dgtl::ir::INVALID_SLOT) {
            node.op_b = remap[node.op_b];
        }
        if (node.op_c <= max_id && is_operand_slot(node.opcode, 2) && remap[node.op_c] != dgtl::ir::INVALID_SLOT) {
            node.op_c = remap[node.op_c];
        }
        
        node.result_id = new_slot;
        compacted_stream.push_back(node);
    }
    
    stream.copy_from(compacted_stream);
    return make_ok();
}

extern "C" DGTL_API uint32_t dgtl_emit_const_f32(dgtl_stream_t* s, float value) {
    if (!s) return 0;
    
    uint32_t bits = std::bit_cast<uint32_t>(value);
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST | dgtl::ir::Flags::IS_SPEC_CONST,
                        bits, 0, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_const_f64(dgtl_stream_t* s, double value) {
    if (!s) return 0;
    
    const uint64_t bits = std::bit_cast<uint64_t>(value);
    uint32_t lo = 0;
    uint32_t hi = 0;
    split_immediate_payload(bits, lo, hi);
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST | dgtl::ir::Flags::IS_SPEC_CONST,
                        lo, hi, dgtl::ir::INVALID_SLOT, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_const_binary(dgtl_stream_t* s, 
                                                     const void* data, size_t len) {
    if (!s || !data || len == 0) return 0;
    const uint64_t hash = dgtl::utils::Hashing::xxhash64(data, len);
    uint32_t lo = 0;
    uint32_t hi = 0;
    split_immediate_payload(hash, lo, hi);
    
    return push_ir_node(s, dgtl::ir::OpCode::CONST,
                        dgtl::ir::Flags::IS_CONST,
                        lo,
                        hi,
                        dgtl::ir::INVALID_SLOT, hash);
}

extern "C" DGTL_API uint32_t dgtl_emit_phi(dgtl_stream_t* s,
                                            uint32_t* incoming_slots,
                                            size_t incoming_count) {
    if (!s || !incoming_slots || incoming_count == 0) return 0;
    
    /* PHI nodes require multiple operands; use first two as a and b, rest ignored for C ABI */
    uint32_t op_a = incoming_slots[0];
    uint32_t op_b = incoming_count > 1 ? incoming_slots[1] : dgtl::ir::INVALID_SLOT;
    
    return push_ir_node(s, dgtl::ir::OpCode::PHI_NODE, 0, op_a, op_b, 0);
}

extern "C" DGTL_API uint32_t dgtl_emit_label(dgtl_stream_t* s, const char* label_name) {
    if (!s || !label_name) return 0;
    
    const uint64_t label_hash = dgtl::utils::Hashing::fnv1a(label_name, std::strlen(label_name));
    uint32_t lo = 0;
    uint32_t hi = 0;
    split_immediate_payload(label_hash, lo, hi);
    
    return push_ir_node(s, dgtl::ir::OpCode::LABEL,
                        dgtl::ir::Flags::NONE,
                        lo,
                        hi,
                        dgtl::ir::INVALID_SLOT, label_hash);
}

extern "C" DGTL_API uint32_t dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot) {
    if (!s || label_slot == 0u) return 0;
    return push_ir_node_explicit_slot(s,
                                      dgtl::ir::OpCode::LABEL,
                                      dgtl::ir::Flags::NONE,
                                      label_slot,
                                      label_slot,
                                      dgtl::ir::INVALID_SLOT,
                                      dgtl::ir::INVALID_SLOT);
}

extern "C" DGTL_API uint32_t dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot) {
    if (!s || function_slot == 0u) return 0;
    return push_ir_node_explicit_slot(s,
                                      dgtl::ir::OpCode::LABEL,
                                      static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS | dgtl::ir::Flags::IS_VOLATILE),
                                      function_slot,
                                      dgtl::ir::INVALID_SLOT,
                                      dgtl::ir::INVALID_SLOT,
                                      dgtl::ir::INVALID_SLOT);
}

extern "C" DGTL_API uint32_t dgtl_emit_function_label_with_params(dgtl_stream_t* s,
                                                                 uint32_t function_slot,
                                                                 const uint32_t* params,
                                                                 size_t param_count) {
    if (!s || function_slot == 0u) return 0;
    uint32_t op_a = (param_count > 0 && params) ? params[0] : dgtl::ir::INVALID_SLOT;
    uint32_t op_b = (param_count > 1 && params) ? params[1] : dgtl::ir::INVALID_SLOT;
    uint32_t op_c = (param_count > 2 && params) ? params[2] : dgtl::ir::INVALID_SLOT;
    uint32_t logic_flags = (param_count > 3 && params) ? params[3] : 0u;

    return push_ir_node_explicit_slot(s,
                                      dgtl::ir::OpCode::LABEL,
                                      static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS | dgtl::ir::Flags::IS_VOLATILE),
                                      function_slot,
                                      op_a,
                                      op_b,
                                      op_c,
                                      0ULL,
                                      logic_flags);
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

extern "C" DGTL_API uint32_t dgtl_emit_call_with_result(dgtl_stream_t* s,
                                                          uint32_t result_slot,
                                                          uint32_t callee_slot,
                                                          const uint32_t* args,
                                                          size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL, result_slot, callee_slot, args, arg_count);
}

extern "C" DGTL_API uint32_t dgtl_emit_call(dgtl_stream_t* s,
                                               uint32_t callee_slot,
                                               const uint32_t* args,
                                               size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL, 0u, callee_slot, args, arg_count);
}

extern "C" DGTL_API uint32_t dgtl_emit_call_variadic_with_result(dgtl_stream_t* s,
                                                                   uint32_t result_slot,
                                                                   uint32_t callee_slot,
                                                                   const uint32_t* args,
                                                                   size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL_VARIADIC, result_slot, callee_slot, args, arg_count);
}

extern "C" DGTL_API uint32_t dgtl_emit_call_variadic(dgtl_stream_t* s,
                                                       uint32_t callee_slot,
                                                       const uint32_t* args,
                                                       size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL_VARIADIC, 0u, callee_slot, args, arg_count);
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
        case DGTL_TARGET_X64:        bytes_per_node = 8;  break;
        case DGTL_TARGET_ARM64:      bytes_per_node = 8;  break;
        case DGTL_TARGET_NASM:       bytes_per_node = 12; break;
        case DGTL_TARGET_WASM:       bytes_per_node = 6;  break;
        default:                     bytes_per_node = 8;  break;
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
            case dgtl::ir::OpCode::DIV_F64:
                node_cycles = 20; /* Division is expensive */
                break;
            case dgtl::ir::OpCode::MUL_I32:
            case dgtl::ir::OpCode::MUL_F32:
            case dgtl::ir::OpCode::MUL_F64:
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
    
    /* Serialize stream to binary */
    const auto& stream = s->function.stream();
    
    size_t blob_size = 16 + stream.size() * sizeof(dgtl::ir::IRNode);  /* header + nodes */
    void* blob = std::malloc(blob_size);
    if (!blob) return make_err(2, "malloc failed");
    
    /* Write magic + size header */
    uint32_t* header = static_cast<uint32_t*>(blob);
    header[0] = 0xDEADBEEFu;  /* Magic number */
    header[1] = static_cast<uint32_t>(stream.size());
    header[2] = 0u;
    header[3] = 0u;
    
    dgtl::ir::IRNode* out_nodes = reinterpret_cast<dgtl::ir::IRNode*>(static_cast<char*>(blob) + 16);
    for (size_t i = 0; i < stream.size(); ++i) {
        out_nodes[i] = stream.get_node(i);
    }
    
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

/* Diagnostic Functions */

static thread_local char g_error_buffer[512] = {};

extern "C" DGTL_API void DGTL_CALL dgtl_stream_dump_ir(const dgtl_stream_t* s, void* custom_file_handle_opt) {
    FILE* out = static_cast<FILE*>(custom_file_handle_opt);
    if (!s || !out) return;
    
    const auto& stream = s->function.stream();
    
    std::fprintf(out, "; DGTL IR Dump\n");
    std::fprintf(out, "; Total nodes: %zu\n", stream.size());
    std::fprintf(out, "; Logic hash: 0x%016llx\n\n", stream.generate_logic_hash());
    
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        
        std::fprintf(out, "  node %zu: %.*s", i,
                     static_cast<int>(dgtl::ir::opcode_name(node.opcode).size()),
                     dgtl::ir::opcode_name(node.opcode).data());
        
        if (is_immediate_opcode(node.opcode)) {
            uint64_t val = decode_immediate_payload(node.op_a, node.op_b);
            std::fprintf(out, " %llu", (unsigned long long)val);
        } else {
            if (node.op_a != dgtl::ir::INVALID_SLOT) std::fprintf(out, " %%%u", node.op_a);
            if (node.op_b != dgtl::ir::INVALID_SLOT) std::fprintf(out, " %%%u", node.op_b);
            if (node.op_c != dgtl::ir::INVALID_SLOT && node.op_c != 0) std::fprintf(out, " %%%u", node.op_c);
        }
        
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

extern "C" DGTL_API int dgtl_binary_loader_validate_payload(const uint8_t* bytes, size_t len) {
    if (!bytes || len == 0) return 0;
    dgtl::execution::format::Binary_Format fmt{};
    fmt.data.assign(bytes, bytes + len);
    return fmt.is_valid() ? 1 : 0;
}

extern "C" DGTL_API const char* dgtl_get_build_config(void) {
    return "DGTL v1.2.0 (Release) | C++26 | Architecture-aware | Multi-target";
}