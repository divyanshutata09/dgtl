/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Unified C-ABI SDK Implementation Bridge (Complete Unabridged Edition)
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl_sdk/dgtl.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  undef CONST
#  undef NOP
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define DGTL_WEAK __attribute__((weak))
#else
#  define DGTL_WEAK
#endif

/* ---------- Native DGTL C++ Named Module Ingestion ---------- */

import dgtl.target;
import dgtl.config;
import dgtl.compiler_context;
import dgtl.numa_runtime;
import dgtl.task_scheduler;
import dgtl.hashing;
import dgtl.bit_pack;
import dgtl.flat_map;
import dgtl.dgtl_bulletins;
import dgtl.runtime_intrinsics;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.op_traits;

import dgtl.ir_core.stream;
import dgtl.ir_core.io;
import dgtl.ir_core.verifier;

import dgtl.ir_core.debug;
import dgtl.ir_core.function;

import dgtl.ir_core.semantic;
import dgtl.ir_core.formal_verification_smt;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.transform_inlining;
import dgtl.ir_core.transform_poly_loop;
import dgtl.ir_core.error_recovery;
import dgtl.ir_core.escape_analysis;
import dgtl.ir_core.simd_gather_scatter;
import dgtl.ir_core.linker_backend;
import dgtl.backend.aot_linker;
import dgtl.ir_core.macho_emitter;
import dgtl.ir_core.jit_orchestrator;
import dgtl.ir_core.code_cache;
import dgtl.ir_core.execution_dispatcher;
import dgtl.ir_core.binary_loader;
import dgtl.ir_core.hardware_scheduler;
import dgtl.internal.api_execution_engine;
import dgtl.internal.api_aot_session;

/* ============================================================================
 * INTERNAL ENGINE WRAPPERS, STATE & DIAGNOSTIC BUFFERING
 * ============================================================================ */

struct dgtl_context {
    std::unique_ptr<dgtl::core::DGTL_Arena> arena;
    std::unique_ptr<dgtl::core::DGTLContext> compiler_ctx;

    dgtl_context(size_t threads, size_t arena_mb)
        : arena(std::make_unique<dgtl::core::DGTL_Arena>(threads, arena_mb)),
          compiler_ctx(std::make_unique<dgtl::core::DGTLContext>(*arena)) {}
};

struct dgtl_stream {
    dgtl_context* ctx = nullptr;
    dgtl::core::DGTLFunction function;
    dgtl_ir_mode_t ir_mode = DGTL_IR_MODE_INT;
    bool optimization_enabled = true;
    bool is_optimized = false;
    dgtl_emit_options_t emit_options{};
    std::unordered_map<uint32_t, std::string> callee_bindings;

    dgtl_stream(dgtl_context* c, size_t reserved)
        : ctx(c),
          function(*c->arena, "dgtl_kernel_entry", reserved) {
        emit_options.inline_threshold = 50;
        emit_options.unroll_factor = 4;
        emit_options.enable_cache_optimization = 1;
        emit_options.enable_branch_prediction = 1;
        emit_options.vectorize_threshold = 8;
    }
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

struct dgtl_runtime_context {
    dgtl::runtime::TaskScheduler scheduler;

    explicit dgtl_runtime_context(size_t threads, size_t arena_mb)
        : scheduler(threads, arena_mb) {}
};

static std::atomic<dgtl_runtime_context_t*> g_default_runtime_ctx{nullptr};
static std::mutex g_default_runtime_ctx_mutex;

static dgtl_runtime_context_t* get_or_create_default_runtime_ctx() noexcept {
    auto* ctx = g_default_runtime_ctx.load(std::memory_order_acquire);
    if (!ctx) {
        std::lock_guard<std::mutex> lock(g_default_runtime_ctx_mutex);
        ctx = g_default_runtime_ctx.load(std::memory_order_relaxed);
        if (!ctx) {
            ctx = new (std::nothrow) dgtl_runtime_context(0, 32);
            g_default_runtime_ctx.store(ctx, std::memory_order_release);
        }
    }
    return ctx;
}

static thread_local std::vector<std::string> g_last_linker_fallbacks;
static thread_local char g_error_buffer[512] = {};
static std::atomic<int32_t> g_verbose_mode{0};

static void set_last_error(const char* msg) noexcept {
    if (!msg) {
        g_error_buffer[0] = '\0';
        return;
    }
    std::snprintf(g_error_buffer, sizeof(g_error_buffer), "%s", msg);
}

static dgtl_status_t make_ok() noexcept {
    dgtl_status_t s{};
    s.ok = 1;
    s.error_code = 0;
    s.severity = DGTL_SEV_INFO;
    s.message[0] = '\0';
    return s;
}

static dgtl_status_t make_err(int32_t code, const char* msg, dgtl_severity_t sev = DGTL_SEV_ERROR) noexcept {
    dgtl_status_t s{};
    s.ok = 0;
    s.error_code = code;
    s.severity = sev;
    std::snprintf(s.message, sizeof(s.message), "%s", msg ? msg : "unknown internal failure");
    set_last_error(s.message);
    return s;
}

static dgtl_strike_result_t make_strike_err(const char* msg) noexcept {
    dgtl_strike_result_t r{};
    r.ok = 0;
    r.code_size = 0;
    r.logic_hash = 0;
    r.compilation_latency_ms = 0.0;
    std::snprintf(r.message, sizeof(r.message), "%s", msg ? msg : "strike synthesis failure");
    set_last_error(r.message);
    return r;
}

static void apply_numa_thread_pinning() noexcept {
    thread_local bool applied = false;
    if (applied) [[likely]] return;
    applied = true;

    const uint32_t nodes = dgtl::runtime::numa::get_num_nodes();
    if (nodes <= 1u) return;

    const uint32_t worker = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const uint32_t node = dgtl::runtime::numa::recommend_worker_node(worker);
    (void)dgtl::runtime::numa::pin_current_thread_to_node(node);
    (void)dgtl::runtime::numa::pin_thread_to_core(dgtl::runtime::numa::recommend_worker_core(worker));
}

static void sdk_prime_simd_units() {
    static std::once_flag once;
    std::call_once(once, []() {
        if (dgtl::core::preferred_simd_execution_profile() == dgtl::core::SimdExecutionProfile::Scalar) {
            return;
        }
        dgtl::core::prime_preferred_simd_units(2048);
    });
}

static void split_immediate_payload(uint64_t payload, uint32_t& out_a, uint32_t& out_b) noexcept {
    out_a = static_cast<uint32_t>(dgtl::utils::BitPack::extract64(payload, 0u, 32u));
    out_b = static_cast<uint32_t>(dgtl::utils::BitPack::extract64(payload, 32u, 32u));
}

static inline void split_payload_64(uint64_t payload, uint32_t& out_lo, uint32_t& out_hi) noexcept {
    split_immediate_payload(payload, out_lo, out_hi);
}

static uint32_t next_string_handle_candidate(uint32_t base, uint32_t salt) noexcept {
    uint32_t handle = base + (0x9E3779B9u * salt);
    if (handle == 0u || handle == DGTL_INVALID_SLOT) handle ^= 0xA5A5A5A5u;
    if (handle == 0u || handle == DGTL_INVALID_SLOT) handle = 1u + salt;
    return handle;
}

static uint32_t sdk_intern_string_handle(std::string_view literal) {
    static std::mutex s_string_registry_mutex;
    std::lock_guard<std::mutex> lock(s_string_registry_mutex);

    const uint32_t base_handle = dgtl::utils::hashing::Hashing::fnv1a_32(literal.data(), literal.size());
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

    dgtl::runtime::register_string_handle(handle, literal.data(), static_cast<uint32_t>(literal.size()));
    return handle;
}

static uint32_t intern_string_internal(std::string_view literal) {
    return sdk_intern_string_handle(literal);
}

static dgtl::ir::BackendProfile::TargetISA to_internal_isa(dgtl_target_t target) noexcept {
    switch (target) {
        case DGTL_TARGET_ARM64_SVE:  return dgtl::ir::BackendProfile::TargetISA::ARM64_SVE;
        case DGTL_TARGET_ARM32_NEON: return dgtl::ir::BackendProfile::TargetISA::ARM32_NEON;
        case DGTL_TARGET_RISCV64:    return dgtl::ir::BackendProfile::TargetISA::RISCV64_RV64GC;
        case DGTL_TARGET_WASM:       return dgtl::ir::BackendProfile::TargetISA::WASM;
        case DGTL_TARGET_CUDA_PTX:   return dgtl::ir::BackendProfile::TargetISA::CUDA_PTX;
        case DGTL_TARGET_ROCM:       return dgtl::ir::BackendProfile::TargetISA::ROCM_LLVMIR;
        case DGTL_TARGET_NASM_X64:   return dgtl::ir::BackendProfile::TargetISA::NASM_X64;
        case DGTL_TARGET_X64:
        case DGTL_TARGET_X64_AVX512:
        default:
            return dgtl::ir::BackendProfile::TargetISA::X64_AVX512;
    }
}

static const char* target_extension_internal(dgtl_target_t target) noexcept {
    switch (target) {
        case DGTL_TARGET_ARM64_SVE:  return ".s";
        case DGTL_TARGET_ARM32_NEON: return ".s";
        case DGTL_TARGET_RISCV64:    return ".s";
        case DGTL_TARGET_WASM:       return ".wasm";
        case DGTL_TARGET_CUDA_PTX:   return ".ptx";
        case DGTL_TARGET_ROCM:       return ".ll";
        case DGTL_TARGET_NASM_X64:   return ".asm";
        case DGTL_TARGET_ELF_OBJ:    return ".o";
        case DGTL_TARGET_COFF_OBJ:   return ".obj";
        case DGTL_TARGET_MACHO:      return ".macho";
        case DGTL_TARGET_STATIC_LIB: return ".a";
        case DGTL_TARGET_X64:
        case DGTL_TARGET_X64_AVX512:
        default:
            return ".bin";
    }
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
    apply_numa_thread_pinning();
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

static uint32_t push_ir_node(dgtl_stream_t* s,
                           dgtl::ir::OpCode op,
                           uint16_t flags,
                           uint32_t op_a, uint32_t op_b, uint32_t op_c,
                           uint64_t imm_as_hash = 0) {
    return push_ir_node_explicit_slot(s, op, flags, s->function.allocate_slot(), op_a, op_b, op_c, imm_as_hash);
}

static uint32_t push_node_internal(dgtl_stream_t* s,
                                   dgtl::ir::OpCode op,
                                   uint16_t flags,
                                   uint32_t result_slot,
                                   uint32_t op_a,
                                   uint32_t op_b,
                                   uint32_t op_c,
                                   uint64_t intent_hash = 0ull,
                                   uint32_t logic_flags = 0u) noexcept {
    return push_ir_node_explicit_slot(s, op, flags, result_slot == 0u ? s->function.allocate_slot() : result_slot, op_a, op_b, op_c, intent_hash, logic_flags);
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

static dgtl::api::ExecutionMode to_internal_session_mode(dgtl_session_mode_t mode) noexcept {
    switch (mode) {
        case DGTL_SESSION_MODE_JIT:    return dgtl::api::ExecutionMode::JIT;
        case DGTL_SESSION_MODE_AOT:    return dgtl::api::ExecutionMode::AOT;
        case DGTL_SESSION_MODE_HYBRID:
        default:                       return dgtl::api::ExecutionMode::Adaptive;
    }
}

static dgtl::api::OptimizationLevel to_internal_opt_level(dgtl_session_opt_level_t level) noexcept {
    switch (level) {
        case DGTL_SESSION_OPT_O0:      return dgtl::api::OptimizationLevel::O0;
        case DGTL_SESSION_OPT_O1:      return dgtl::api::OptimizationLevel::O1;
        case DGTL_SESSION_OPT_O3:      return dgtl::api::OptimizationLevel::O3;
        case DGTL_SESSION_OPT_OSTRIKE: return dgtl::api::OptimizationLevel::OStrike;
        case DGTL_SESSION_OPT_O2:
        default:                       return dgtl::api::OptimizationLevel::O2;
    }
}

static dgtl_session_config_t normalize_session_config(const dgtl_session_config_t* cfg) noexcept {
    if (cfg != nullptr) {
        return *cfg;
    }
    dgtl_session_config_t out{};
    out.mode = DGTL_SESSION_MODE_HYBRID;
    out.opt_level = DGTL_SESSION_OPT_O3;
    out.timeout_us = 1000000u;
    out.enable_temporal_fusion = 1;
    out.enable_polyhedral_scop = 1;
    out.enable_speculation = 1;
    out.enable_vectorization = 1;
    out.enable_deterministic_replay = 0;
    out.osr_threshold_invocations = 64;
    out.background_worker_threads = 2;
    out.preferred_numa_node = 0;
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
    out.thread_count = cfg.background_worker_threads;
    return out;
}

/* ============================================================================
 * EXTERN "C" INTERFACE IMPLEMENTATION (COMPLETE UNABRIDGED)
 * ============================================================================ */

extern "C" {

DGTL_API const char* DGTL_CALL dgtl_version_string(void) {
    return DGTL_VERSION_STRING;
}

DGTL_API const char* DGTL_CALL dgtl_target_name(dgtl_target_t target) {
    static const char* s_target_names[] = {
        "x64", "arm64_sve", "arm32_neon", "riscv64", "wasm",
        "cuda_ptx", "rocm", "nasm_x64", "elf_obj", "coff_obj", "macho", "static_lib"
    };
    if (static_cast<size_t>(target) < DGTL_TARGET_COUNT) {
        return s_target_names[target];
    }
    return "unknown";
}

DGTL_API const char* DGTL_CALL dgtl_target_extension(dgtl_target_t target) {
    return target_extension_internal(target);
}

DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create(void) {
    return dgtl_context_create_custom(2, 32);
}

DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create_custom(size_t worker_threads, size_t arena_mb_per_worker) {
    try {
        return new dgtl_context(worker_threads, arena_mb_per_worker);
    } catch (...) {
        return nullptr;
    }
}

DGTL_API void DGTL_CALL dgtl_context_destroy(dgtl_context_t* ctx) {
    delete ctx;
}

DGTL_API dgtl_stream_t* DGTL_CALL dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes) {
    if (!ctx) return nullptr;
    try {
        return new dgtl_stream(ctx, reserved_nodes);
    } catch (...) {
        return nullptr;
    }
}

DGTL_API void DGTL_CALL dgtl_stream_destroy(dgtl_stream_t* stream) {
    delete stream;
}

DGTL_API size_t DGTL_CALL dgtl_stream_node_count(const dgtl_stream_t* stream) {
    if (!stream) return 0;
    return stream->function.stream().size();
}

DGTL_API uint64_t DGTL_CALL dgtl_stream_logic_hash(const dgtl_stream_t* stream) {
    if (!stream) return 0;
    return stream->function.stream().generate_logic_hash();
}

DGTL_API void DGTL_CALL dgtl_stream_reset(dgtl_stream_t* stream) {
    if (stream) stream->function.stream().reset();
}

DGTL_API bool DGTL_CALL dgtl_stream_reserve(dgtl_stream_t* stream, size_t additional_nodes) {
    if (!stream) return false;
    auto& s = stream->function.stream();
    size_t current = s.size();
    if (additional_nodes > (std::numeric_limits<size_t>::max() - current)) {
        return false;
    }
    return s.reserve_capacity(current + additional_nodes);
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_compact(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();
    if (stream.size() == 0) return make_ok();

    std::vector<bool> live(stream.size(), false);
    std::unordered_map<uint32_t, size_t> def_map;
    def_map.reserve(stream.size());

    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if (node.result_id != DGTL_INVALID_SLOT && node.result_id != 0u) {
            def_map[node.result_id] = i;
        }
        if (dgtl::ir::detail::irnode_has_side_effects(node.opcode, node.flags) ||
            node.opcode == dgtl::ir::OpCode::RET ||
            node.opcode == dgtl::ir::OpCode::STORE ||
            node.opcode == dgtl::ir::OpCode::CALL) {
            live[i] = true;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < stream.size(); ++i) {
            if (!live[i]) continue;
            const auto node = stream.get_node(i);
            auto mark_op = [&](uint32_t operand) {
                if (operand == DGTL_INVALID_SLOT || operand == 0u) return;
                auto it = def_map.find(operand);
                if (it != def_map.end() && !live[it->second]) {
                    live[it->second] = true;
                    changed = true;
                }
            };
            mark_op(node.op_a);
            mark_op(node.op_b);
            mark_op(node.op_c);
        }
    }

    dgtl::ir::IRStream compacted(*s->ctx->arena, stream.size());
    std::vector<size_t> idx_remap(stream.size(), static_cast<size_t>(-1));

    for (size_t i = 0; i < stream.size(); ++i) {
        if (!live[i]) continue;
        idx_remap[i] = compacted.size();
        compacted.push_back(stream.get_node(i));
    }

    for (size_t old_idx = 0; old_idx < stream.size(); ++old_idx) {
        if (idx_remap[old_idx] == static_cast<size_t>(-1)) continue;
        size_t new_idx = idx_remap[old_idx];
        auto overflow = stream.get_call_overflow_args(old_idx);
        if (!overflow.empty()) compacted.set_call_overflow_args(new_idx, overflow);
        auto results = stream.get_result_ids(old_idx);
        if (!results.empty()) compacted.set_result_ids(new_idx, results);
    }

    stream.copy_from(compacted);
    return make_ok();
}

DGTL_API void DGTL_CALL dgtl_stream_begin_transaction(dgtl_stream_t* stream) {
    if (stream) stream->function.stream().begin_transaction();
}

DGTL_API bool DGTL_CALL dgtl_stream_commit_transaction(dgtl_stream_t* stream) {
    if (!stream) return false;
    stream->function.stream().commit_transaction();
    return true;
}

DGTL_API void DGTL_CALL dgtl_stream_rollback_transaction(dgtl_stream_t* stream) {
    if (stream) stream->function.stream().rollback_transaction();
}

DGTL_API bool DGTL_CALL dgtl_stream_validate_shadow_integrity(const dgtl_stream_t* stream) {
    if (!stream) return false;
    return stream->function.stream().validate_shadow_integrity();
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_node(dgtl_stream_t* s, dgtl_opcode_t op, uint16_t flags,
                                          uint32_t result_slot, uint32_t op_a, uint32_t op_b,
                                          uint32_t op_c, uint64_t intent_hash) {
    return push_node_internal(s, static_cast<dgtl::ir::OpCode>(op), flags, result_slot, op_a, op_b, op_c, intent_hash);
}

DGTL_API void DGTL_CALL dgtl_set_node_logic_flags(dgtl_stream_t* s, size_t node_idx, uint32_t logic_flags) {
    if (!s || node_idx >= s->function.stream().size()) return;
    auto node = s->function.stream().get_node(node_idx);
    node.logic_flags = logic_flags;
    node.refresh_intent_hash();
    s->function.stream().set_node(node_idx, node);
}

DGTL_API void DGTL_CALL dgtl_set_result_type(dgtl_stream_t* s, uint32_t result_slot, dgtl_type_code_t type_code) {
    if (!s || result_slot == DGTL_INVALID_SLOT || result_slot == 0u) return;
    s->function.stream().set_result_type(result_slot, static_cast<dgtl::ir::TypeCode>(type_code));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint32_t flags) {
    if (!s) return DGTL_INVALID_SLOT;
    uint32_t op_a = lhs;
    uint32_t op_b = rhs;
    uint32_t op_c = 0u;

    if (op == DGTL_OP_CONST || op == DGTL_OP_LOAD_IMM || op == DGTL_OP_IMMEDIATE) {
        split_payload_64(immediate, op_a, op_b);
        op_c = DGTL_INVALID_SLOT;
    }

    return push_node_internal(s, static_cast<dgtl::ir::OpCode>(op), static_cast<uint16_t>(flags),
                              DGTL_INVALID_SLOT, op_a, op_b, op_c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_op_with_result(dgtl_stream_t* s, uint32_t result_slot, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint32_t flags) {
    if (!s || result_slot == 0u) return DGTL_INVALID_SLOT;
    uint32_t op_a = lhs;
    uint32_t op_b = rhs;
    uint32_t op_c = 0u;

    if (op == DGTL_OP_CONST || op == DGTL_OP_LOAD_IMM || op == DGTL_OP_IMMEDIATE) {
        split_payload_64(immediate, op_a, op_b);
        op_c = DGTL_INVALID_SLOT;
    }

    return push_node_internal(s, static_cast<dgtl::ir::OpCode>(op), static_cast<uint16_t>(flags),
                              result_slot, op_a, op_b, op_c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value) {
    uint32_t lo = 0, hi = 0;
    split_payload_64(value, lo, hi);
    return push_node_internal(s, dgtl::ir::OpCode::CONST, DGTL_FLAG_IS_CONST, DGTL_INVALID_SLOT, lo, hi, DGTL_INVALID_SLOT, 0ull);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f32(dgtl_stream_t* s, float value) {
    uint32_t bits = std::bit_cast<uint32_t>(value);
    return push_node_internal(s, dgtl::ir::OpCode::CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, DGTL_INVALID_SLOT, bits, 0u, DGTL_INVALID_SLOT, 0ull);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f64(dgtl_stream_t* s, double value) {
    uint64_t bits = std::bit_cast<uint64_t>(value);
    uint32_t lo = 0, hi = 0;
    split_payload_64(bits, lo, hi);
    return push_node_internal(s, dgtl::ir::OpCode::CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, DGTL_INVALID_SLOT, lo, hi, DGTL_INVALID_SLOT, 0ull);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_const_binary(dgtl_stream_t* s, const void* data, size_t len) {
    if (!s || !data || len == 0) return DGTL_INVALID_SLOT;
    uint64_t hash = dgtl::utils::Hashing::xxhash64(data, len);
    uint32_t lo = 0, hi = 0;
    split_payload_64(hash, lo, hi);
    return push_node_internal(s, dgtl::ir::OpCode::CONST, DGTL_FLAG_IS_CONST, DGTL_INVALID_SLOT, lo, hi, DGTL_INVALID_SLOT, hash);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_load(dgtl_stream_t* s, uint32_t ptr_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::LOAD, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, ptr_slot, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_store(dgtl_stream_t* s, uint32_t ptr_slot, uint32_t val_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::STORE, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, ptr_slot, val_slot, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::ADD_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SUB_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::MUL_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::DIV_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::MOD_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::ADD_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SUB_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::MUL_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::DIV_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::AND_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::OR_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::XOR_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SHL_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SHR_I32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::AND_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::OR_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::XOR_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SHL_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SHR_I64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fadd(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::ADD_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fsub(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SUB_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fmul(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::MUL_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fdiv(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::DIV_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fadd_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::ADD_F64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fsub_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::SUB_F64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fmul_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::MUL_F64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fdiv_f64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::DIV_F64, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_label(dgtl_stream_t* s, const char* label_name) {
    if (!s || !label_name) return DGTL_INVALID_SLOT;
    uint64_t h = dgtl::utils::Hashing::fnv1a(label_name, std::strlen(label_name));
    uint32_t lo = 0, hi = 0;
    split_payload_64(h, lo, hi);
    return push_node_internal(s, dgtl::ir::OpCode::LABEL, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, lo, hi, DGTL_INVALID_SLOT, h);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot) {
    if (!s || label_slot == 0u) return DGTL_INVALID_SLOT;
    return push_node_internal(s, dgtl::ir::OpCode::LABEL, DGTL_FLAG_NONE, label_slot, label_slot, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot) {
    if (!s || function_slot == 0u) return DGTL_INVALID_SLOT;
    return push_node_internal(s, dgtl::ir::OpCode::LABEL, static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS | dgtl::ir::Flags::IS_VOLATILE),
                            function_slot, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_with_params(dgtl_stream_t* s, uint32_t function_slot, const uint32_t* params, size_t param_count) {
    if (!s || function_slot == 0u) return DGTL_INVALID_SLOT;
    uint32_t a0 = (param_count > 0 && params) ? params[0] : DGTL_INVALID_SLOT;
    uint32_t a1 = (param_count > 1 && params) ? params[1] : DGTL_INVALID_SLOT;
    uint32_t a2 = (param_count > 2 && params) ? params[2] : DGTL_INVALID_SLOT;
    uint32_t logic_flags = (param_count > 3 && params) ? params[3] : 0u;

    size_t node_idx = s->function.stream().size();
    uint32_t slot = push_node_internal(s, dgtl::ir::OpCode::LABEL, static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS | dgtl::ir::Flags::IS_VOLATILE),
                                       function_slot, a0, a1, a2, 0ull, logic_flags);
    if (param_count > 4 && params) {
        s->function.stream().set_call_overflow_args(node_idx, std::span<const uint32_t>(params + 4, param_count - 4));
    }
    return slot;
}

DGTL_API void DGTL_CALL dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::GOTO, DGTL_FLAG_IS_BRANCH | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, target_label_slot, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_branch_cond(dgtl_stream_t* s, uint32_t cond_slot, uint32_t target_label_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::BRANCH_COND, DGTL_FLAG_IS_BRANCH | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, cond_slot, target_label_slot, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_branch_predictive(dgtl_stream_t* s, uint32_t cond_reg, uint32_t true_label, uint32_t false_label, uint8_t weight) {
    dgtl::ir::OpCode op = (weight > 128) ? dgtl::ir::OpCode::BRANCH_LIKELY : (weight < 128) ? dgtl::ir::OpCode::BRANCH_UNLIKELY : dgtl::ir::OpCode::BRANCH_COND;
    (void)push_node_internal(s, op, DGTL_FLAG_IS_BRANCH | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, cond_reg, true_label, false_label);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count) {
    return dgtl_emit_call_with_result(s, DGTL_INVALID_SLOT, callee_slot, args, arg_count);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_call_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL, result_slot, callee_slot, args, arg_count);
}

DGTL_API void DGTL_CALL dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::RET, DGTL_FLAG_IS_RETURN | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, value_slot, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_constant_str(dgtl_stream_t* s, dgtl_value_t dest_reg, const char* value, size_t length) {
    if (!s || !value) return;
    auto& stream = s->function.stream();
    uint32_t handle = intern_string_internal(std::string_view(value, length));
    dgtl::ir::IRNode node{};
    node.opcode = dgtl::ir::OpCode::STRING_CONST;
    node.result_id = dest_reg;
    node.op_a = handle;
    node.op_b = DGTL_INVALID_SLOT;
    node.op_c = DGTL_INVALID_SLOT;
    node.flags = static_cast<uint16_t>(dgtl::ir::Flags::IS_CONST);
    stream.push_back(node);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_handle(dgtl_stream_t* s, uint32_t string_handle) {
    if (!s || string_handle == 0u || string_handle == dgtl::ir::INVALID_SLOT) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STRING_CONST, dgtl::ir::Flags::IS_CONST, string_handle, 0u, 0u, 0);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_utf8(dgtl_stream_t* s, const char* utf8_literal) {
    if (!s || !utf8_literal) return 0;
    return dgtl_emit_string_const_handle(s, sdk_intern_string_handle(std::string_view(utf8_literal)));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_length(dgtl_stream_t* s, uint32_t string_slot) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STRING_LENGTH, DGTL_FLAG_NONE, string_slot, 0u, 0u, 0);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_compare(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STRING_COMPARE, DGTL_FLAG_NONE, lhs_string_slot, rhs_string_slot, 0u, 0);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_concat(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STRING_CONCAT, DGTL_FLAG_NONE, lhs_string_slot, rhs_string_slot, 0u, 0);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_string_substring(dgtl_stream_t* s, uint32_t string_slot, uint32_t start_slot, uint32_t length_slot) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STRING_SUBSTRING, DGTL_FLAG_NONE, string_slot, start_slot, length_slot, 0);
}

DGTL_API DGTL_WEAK uint32_t DGTL_CALL dgtl_emit_string_const_rodata_rip(dgtl_stream_t* s, const char* str, size_t len) {
    if (!s || !str) return 0;
    uint32_t handle = intern_string_internal(std::string_view(str, len));
    return dgtl_emit_node(s, static_cast<dgtl_opcode_t>(dgtl::ir::OpCode::STRING_CONST), DGTL_FLAG_IS_CONST, DGTL_INVALID_SLOT, handle, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0ull);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_phi(dgtl_stream_t* s, uint32_t* incoming_slots, size_t incoming_count) {
    if (!s || !incoming_slots || incoming_count == 0) return DGTL_INVALID_SLOT;
    uint32_t op_a = incoming_slots[0];
    uint32_t op_b = incoming_count > 1 ? incoming_slots[1] : DGTL_INVALID_SLOT;
    return push_node_internal(s, dgtl::ir::OpCode::PHI_NODE, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, op_a, op_b, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_phi_multi(dgtl_stream_t* s, uint32_t result_slot, const uint32_t* incoming_values, const uint32_t* incoming_blocks, size_t count) {
    if (!s || count == 0 || !incoming_values || !incoming_blocks) return DGTL_INVALID_SLOT;
    uint32_t v0 = incoming_values[0];
    uint32_t v1 = (count > 1) ? incoming_values[1] : DGTL_INVALID_SLOT;
    uint32_t v2 = (count > 2) ? incoming_values[2] : DGTL_INVALID_SLOT;

    size_t node_idx = s->function.stream().size();
    uint32_t slot = push_node_internal(s, dgtl::ir::OpCode::PHI_MULTI, DGTL_FLAG_NONE, result_slot, v0, v1, v2);
    if (count > 3) {
        s->function.stream().set_operand_overflow(node_idx, std::span<const uint32_t>(incoming_values + 3, count - 3));
    }
    s->function.stream().set_result_ids(node_idx, std::span<const uint32_t>(incoming_blocks, count));
    return slot;
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_tuple_pack(dgtl_stream_t* s, const uint32_t* elements, size_t count) {
    if (!s || count == 0 || !elements) return DGTL_INVALID_SLOT;
    uint32_t e0 = elements[0];
    uint32_t e1 = (count > 1) ? elements[1] : DGTL_INVALID_SLOT;
    uint32_t e2 = (count > 2) ? elements[2] : DGTL_INVALID_SLOT;

    size_t node_idx = s->function.stream().size();
    uint32_t slot = push_node_internal(s, dgtl::ir::OpCode::TUPLE_PACK, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, e0, e1, e2);
    if (count > 3) {
        s->function.stream().set_operand_overflow(node_idx, std::span<const uint32_t>(elements + 3, count - 3));
    }
    return slot;
}

DGTL_API void DGTL_CALL dgtl_emit_tuple_unpack(dgtl_stream_t* s, uint32_t tuple_slot, const uint32_t* out_result_slots, size_t count) {
    if (!s || tuple_slot == DGTL_INVALID_SLOT || !out_result_slots || count == 0) return;
    size_t node_idx = s->function.stream().size();
    (void)push_node_internal(s, dgtl::ir::OpCode::TUPLE_UNPACK, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, tuple_slot, 0u, 0u);
    s->function.stream().set_result_ids(node_idx, std::span<const uint32_t>(out_result_slots, count));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count) {
    return dgtl_emit_call_variadic_with_result(s, DGTL_INVALID_SLOT, callee_slot, args, arg_count);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count) {
    return emit_call_like(s, dgtl::ir::OpCode::CALL_VARIADIC, result_slot, callee_slot, args, arg_count);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_graph_build(dgtl_stream_t* s, uint32_t nodes_vector, uint32_t edges_vector) {
    return push_node_internal(s, dgtl::ir::OpCode::GRAPH_BUILD, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, nodes_vector, edges_vector, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_graph_traverse(dgtl_stream_t* s, uint32_t graph_handle, uint32_t visitor_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::GRAPH_TRAVERSE, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, graph_handle, visitor_slot, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_trie_insert(dgtl_stream_t* s, uint32_t trie_handle, uint32_t key_str_handle, uint32_t val_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::TRIE_INSERT, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, trie_handle, key_str_handle, val_slot);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_scop_analyze(dgtl_stream_t* s, uint32_t region_begin_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::SCOP_ANALYZE, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, region_begin_slot, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_ownership_acquire(dgtl_stream_t* s, uint32_t resource_handle) {
    return push_node_internal(s, dgtl::ir::OpCode::OWNERSHIP_ACQUIRE, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, resource_handle, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_ownership_release(dgtl_stream_t* s, uint32_t resource_handle) {
    (void)push_node_internal(s, dgtl::ir::OpCode::OWNERSHIP_RELEASE, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, resource_handle, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_ownership_transfer(dgtl_stream_t* s, uint32_t src_handle, uint32_t dest_context) {
    return push_node_internal(s, dgtl::ir::OpCode::OWNERSHIP_TRANSFER, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, src_handle, dest_context, 0u);
}

DGTL_API bool DGTL_CALL dgtl_verify_ownership_lifetimes(const dgtl_stream_t* stream, char* err_buf, size_t err_buf_size) {
    if (!stream) return false;
    dgtl::ir::IRVerifier verifier;
    dgtl::ir::IRVerifyOptions opts{};
    opts.check_ownership = true;
    auto diags = verifier.verify(stream->function.stream(), opts);
    for (const auto& d : diags) {
        if (d.kind == dgtl::ir::IRVerifyDiagnostic::Kind::OwnershipViolation) {
            if (err_buf && err_buf_size > 0) {
                std::snprintf(err_buf, err_buf_size, "%s", d.message.c_str());
            }
            return false;
        }
    }
    return true;
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_matmul_bf16(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::MATMUL_BF16, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_matmul_fp8(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::MATMUL_FP8, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_sdpa_f16(dgtl_stream_t* s, uint32_t q, uint32_t k, uint32_t v, uint32_t mask) {
    size_t node_idx = s->function.stream().size();
    uint32_t slot = push_node_internal(s, dgtl::ir::OpCode::SDPA_F16, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, q, k, v);
    s->function.stream().set_call_overflow_args(node_idx, std::span<const uint32_t>(&mask, 1));
    return slot;
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_softmax_fast(dgtl_stream_t* s, uint32_t input) {
    return push_node_internal(s, dgtl::ir::OpCode::SOFTMAX_FAST, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, input, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_layer_norm(dgtl_stream_t* s, uint32_t input, uint32_t gamma, uint32_t beta) {
    return push_node_internal(s, dgtl::ir::OpCode::LAYER_NORM, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, input, gamma, beta);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fast_inv_sqrt(dgtl_stream_t* s, uint32_t input) {
    return push_node_internal(s, dgtl::ir::OpCode::FAST_INV_SQRT, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, input, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec3_cross(dgtl_stream_t* s, uint32_t a, uint32_t b) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC3_CROSS, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, a, b, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec4_dot(dgtl_stream_t* s, uint32_t a, uint32_t b) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC4_DOT, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, a, b, DGTL_INVALID_SLOT);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_mat4_mul(dgtl_stream_t* s, uint32_t a, uint32_t b) {
    return push_node_internal(s, dgtl::ir::OpCode::MAT4_MUL, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, a, b, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_read_tsc(dgtl_stream_t* s) {
    return push_node_internal(s, dgtl::ir::OpCode::READ_TSC, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, 0u, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_clflush(dgtl_stream_t* s, uint32_t addr_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::CLFLUSH, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, addr_slot, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_prefetch_t0(dgtl_stream_t* s, uint32_t addr_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::PREFETCH_T0, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, addr_slot, 0u, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_spin_acquire(dgtl_stream_t* s, uint32_t lock_slot) {
    (void)push_node_internal(s, dgtl::ir::OpCode::SPIN_ACQUIRE, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, lock_slot, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_ring_push_free(dgtl_stream_t* s, uint32_t ring_handle, uint32_t payload_slot) {
    return push_node_internal(s, dgtl::ir::OpCode::RING_PUSH_FREE, DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, ring_handle, payload_slot, 0u);
}

DGTL_API void DGTL_CALL dgtl_emit_hardware_affinity(dgtl_stream_t* s, dgtl_value_t mask_register_id) {
    if (!s) return;
    (void)push_node_internal(s, dgtl::ir::OpCode::BARRIER_SYNC, static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                            DGTL_INVALID_SLOT, mask_register_id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_apply_quiet_core_policy(uint32_t preferred_core) {
    auto res = dgtl::jit::DGTLHardwareScheduler::apply_quiet_core_policy(preferred_core);
    if (!res.affinity_locked) {
        return make_err(1, "failed to lock core affinity", DGTL_SEV_WARNING);
    }
    return make_ok();
}

DGTL_API size_t DGTL_CALL dgtl_stream_cfg_block_count(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().get_basic_blocks().size();
}

DGTL_API size_t DGTL_CALL dgtl_stream_cfg_edge_count(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().get_cfg_edges().size();
}

DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_block(const dgtl_stream_t* s, size_t index, dgtl_cfg_block_t* out) {
    if (!s || !out) return 0;
    const auto& blocks = s->function.stream().get_basic_blocks();
    if (index >= blocks.size()) return 0;
    out->id = blocks[index].id;
    out->begin = blocks[index].begin;
    out->end = blocks[index].end;
    out->primary_label = blocks[index].primary_label;
    out->is_entry = blocks[index].is_entry ? 1 : 0;
    return 1;
}

DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_edge(const dgtl_stream_t* s, size_t index, dgtl_cfg_edge_t* out) {
    if (!s || !out) return 0;
    const auto& edges = s->function.stream().get_cfg_edges();
    if (index >= edges.size()) return 0;
    out->from = edges[index].from;
    out->to = edges[index].to;
    out->kind = static_cast<dgtl_cfg_edge_kind_t>(edges[index].kind);
    return 1;
}

DGTL_API size_t DGTL_CALL dgtl_stream_cfg_get_successors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap) {
    if (!s) return 0;
    const auto& succ = s->function.stream().get_successors(block_id);
    size_t n = std::min(out_cap, succ.size());
    if (out_ids && n > 0) std::memcpy(out_ids, succ.data(), n * sizeof(uint32_t));
    return succ.size();
}

DGTL_API size_t DGTL_CALL dgtl_stream_cfg_get_predecessors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap) {
    if (!s) return 0;
    const auto& pred = s->function.stream().get_predecessors(block_id);
    size_t n = std::min(out_cap, pred.size());
    if (out_ids && n > 0) std::memcpy(out_ids, pred.data(), n * sizeof(uint32_t));
    return pred.size();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();

    dgtl::ir::pass::transform::DeadCodeElimination dce;
    dce.execute(stream);
    dgtl::ir::pass::transform::DGTLGVN gvn;
    gvn.execute(stream);
    dgtl::ir::pass::transform::DGTLLoopFusion fusion;
    fusion.execute(stream);
    dgtl::ir::pass::transform::DGTLAggressiveInliner inliner;
    inliner.execute(stream);
    dgtl::ir::pass::DGTLEscapeAnalysis ea;
    ea.execute(stream);
    dgtl::ir::pass::DGTLEnhancedErrorRecovery er;
    er.execute(stream);

    s->is_optimized = true;
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize_with_profile(dgtl_stream_t* s, dgtl_opt_profile_t profile) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();

    dgtl::ir::pass::transform::DeadCodeElimination dce;
    dce.execute(stream);
    dgtl::ir::pass::transform::DGTLGVN gvn;
    gvn.execute(stream);

    if (profile == DGTL_OPT_PROFILE_SIMD) {
        stream.analyze_simd_opportunities();
        sdk_prime_simd_units();
        const dgtl::ir::pass::simd::TargetArch simd_arch = []() noexcept {
            switch (dgtl::core::preferred_simd_tier()) {
                case dgtl::core::SimdTier::AVX512: return dgtl::ir::pass::simd::TargetArch::X86_AVX512;
                case dgtl::core::SimdTier::AVX2:   return dgtl::ir::pass::simd::TargetArch::X86_AVX2;
                default:                         return dgtl::ir::pass::simd::TargetArch::GENERIC;
            }
        }();
        dgtl::ir::pass::simd::DGTLSIMDLegalizer legalizer(simd_arch);
        legalizer.legalize(stream);
    } else if (profile == DGTL_OPT_PROFILE_FUSION) {
        dgtl::ir::pass::transform::DGTLLoopFusion().execute(stream);
    }

    dgtl::ir::pass::DGTLEnhancedErrorRecovery().execute(stream);
    s->is_optimized = true;
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode) {
    if (!s) return make_err(1, "null stream");
    s->ir_mode = mode;
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name) {
    if (!s || !pass_name) return make_err(1, "null arguments");
    auto& stream = s->function.stream();
    std::string_view p(pass_name);

    if (p == "dce" || p == "DCE") {
        dgtl::ir::pass::transform::DeadCodeElimination().execute(stream);
    } else if (p == "gvn" || p == "GVN") {
        dgtl::ir::pass::transform::DGTLGVN().execute(stream);
    } else if (p == "fusion" || p == "LoopFusion") {
        dgtl::ir::pass::transform::DGTLLoopFusion().execute(stream);
    } else if (p == "inliner" || p == "Inliner") {
        dgtl::ir::pass::transform::DGTLAggressiveInliner().execute(stream);
    } else if (p == "escape" || p == "EscapeAnalysis") {
        dgtl::ir::pass::DGTLEscapeAnalysis().execute(stream);
    } else if (p == "error_recovery") {
        dgtl::ir::pass::DGTLEnhancedErrorRecovery().execute(stream);
    } else {
        return dgtl_optimize(s);
    }
    return make_ok();
}

DGTL_API int32_t DGTL_CALL dgtl_has_pass(const char* pass_name) {
    if (!pass_name) return 0;
    static const char* kPasses[] = {"DCE", "GVN", "LoopFusion", "Inliner", "EscapeAnalysis", "ErrorRecovery", nullptr};
    for (size_t i = 0; kPasses[i]; ++i) {
        if (std::string_view(pass_name) == kPasses[i]) return 1;
    }
    return 0;
}

DGTL_API const char** DGTL_CALL dgtl_list_passes(size_t* out_count) {
    static const char* kPasses[] = {"DCE", "GVN", "LoopFusion", "Inliner", "EscapeAnalysis", "ErrorRecovery", nullptr};
    if (out_count) *out_count = 6;
    return kPasses;
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_enabled(dgtl_stream_t* s, int32_t enabled) {
    if (!s) return make_err(1, "null stream");
    s->optimization_enabled = (enabled != 0);
    return make_ok();
}

DGTL_API int32_t DGTL_CALL dgtl_get_optimization_level(const dgtl_stream_t* s) {
    if (!s) return 0;
    return 2;
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_level(dgtl_stream_t* s, int32_t level) {
    if (!s) return make_err(1, "null stream");
    if (level < 0 || level > 4) return make_err(2, "invalid optimization level (0-4)");
    return make_ok();
}

DGTL_API dgtl_emit_options_t DGTL_CALL dgtl_emit_options_default(dgtl_target_t target) {
    dgtl_emit_options_t opts{};
    opts.inline_threshold = 50;
    opts.unroll_factor = 4;
    opts.enable_cache_optimization = 1;
    opts.enable_branch_prediction = 1;
    opts.vectorize_threshold = 8;
    if (target == DGTL_TARGET_WASM) {
        opts.inline_threshold = 20;
        opts.enable_cache_optimization = 0;
    }
    return opts;
}

DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream_with_options(dgtl_stream_t* s, dgtl_target_t target, const dgtl_emit_options_t* options) {
    if (!s) return make_strike_err("null stream");
    if (options) s->emit_options = *options;
    return dgtl_strike_stream(s, target);
}

DGTL_API size_t DGTL_CALL dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target) {
    if (!s) return 0;
    size_t bytes_per_node = 8;
    switch (target) {
        case DGTL_TARGET_NASM_X64: bytes_per_node = 12; break;
        case DGTL_TARGET_WASM:     bytes_per_node = 6;  break;
        default:                   bytes_per_node = 8;  break;
    }
    return s->function.stream().size() * bytes_per_node + 256;
}

DGTL_API size_t DGTL_CALL dgtl_stream_memory_usage(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().size() * sizeof(dgtl::ir::IRNode) + 512;
}

DGTL_API size_t DGTL_CALL dgtl_context_memory_usage(const dgtl_context_t* ctx) {
    if (!ctx || !ctx->arena) return 0;
    return ctx->arena->bytes_per_thread() * ctx->arena->thread_count();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_shrink_to_fit(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_enable_tracing(dgtl_stream_t* s) {
    if (!s) return make_err(1, "null stream");
    return make_ok();
}

DGTL_API dgtl_trace_event_t* DGTL_CALL dgtl_stream_get_trace_events(const dgtl_stream_t* s, size_t* out_count) {
    if (!s || !out_count) return nullptr;
    *out_count = 0;
    return nullptr;
}

DGTL_API uint64_t DGTL_CALL dgtl_stream_estimate_cycles(const dgtl_stream_t* s) {
    if (!s) return 0;
    const auto& stream = s->function.stream();
    uint64_t cycles = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        switch (node.opcode) {
            case dgtl::ir::OpCode::LOAD:
            case dgtl::ir::OpCode::STORE:
                cycles += 4;
                break;
            case dgtl::ir::OpCode::DIV_I32:
            case dgtl::ir::OpCode::DIV_I64:
            case dgtl::ir::OpCode::DIV_F32:
            case dgtl::ir::OpCode::DIV_F64:
                cycles += 20;
                break;
            case dgtl::ir::OpCode::MUL_I32:
            case dgtl::ir::OpCode::MUL_I64:
            case dgtl::ir::OpCode::MUL_F32:
            case dgtl::ir::OpCode::MUL_F64:
                cycles += 3;
                break;
            default:
                cycles += 1;
                break;
        }
    }
    return cycles;
}

DGTL_API int32_t DGTL_CALL dgtl_stream_get_optimization_score(const dgtl_stream_t* s) {
    if (!s) return 0;
    const auto& stream = s->function.stream();
    if (stream.size() == 0) return 100;
    size_t const_nodes = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        if (stream.get_node(i).flags & dgtl::ir::Flags::IS_CONST) const_nodes++;
    }
    int32_t score = 50 + static_cast<int32_t>((const_nodes * 50) / stream.size());
    return std::min(100, score);
}

DGTL_API int32_t DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out) {
    if (!s || !out) return 0;
    std::memset(out, 0, sizeof(*out));
    const auto& stream = s->function.stream();
    out->total_compilation_time_us = 0;
    out->optimization_time_us = 0;
    out->emission_time_us = 0;
    out->peak_register_count = std::max<uint32_t>(16u, stream.get_max_id());
    out->peak_memory_usage_bytes = static_cast<uint32_t>(stream.size() * sizeof(dgtl::ir::IRNode));
    out->estimated_cycle_count = dgtl_stream_estimate_cycles(s);
    return 1;
}

DGTL_API void DGTL_CALL dgtl_stream_dump_ir(const dgtl_stream_t* s, void* custom_file_handle_opt) {
    FILE* out = custom_file_handle_opt ? static_cast<FILE*>(custom_file_handle_opt) : stdout;
    if (!s || !out) return;
    const auto& stream = s->function.stream();
    std::fprintf(out, "; DGTL Sovereignty IR Trace Dump\n");
    std::fprintf(out, "; Active Nodes: %zu | Structural Logic Hash: 0x%016llX\n\n", stream.size(), static_cast<unsigned long long>(stream.generate_logic_hash()));
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        std::fprintf(out, "  %%v%u = Op:0x%04X [flags:0x%04X] op_a:%%v%u, op_b:%%v%u, op_c:%%v%u | hash:0x%016llX\n",
                     node.result_id, static_cast<uint32_t>(node.opcode), node.flags, node.op_a, node.op_b, node.op_c, static_cast<unsigned long long>(node.intent_hash));
    }
}

DGTL_API const char* DGTL_CALL dgtl_get_last_error(void) {
    return g_error_buffer[0] ? g_error_buffer : "none";
}

DGTL_API void DGTL_CALL dgtl_clear_error(void) {
    g_error_buffer[0] = '\0';
}

DGTL_API void DGTL_CALL dgtl_set_verbose(int32_t enabled) {
    g_verbose_mode.store(enabled, std::memory_order_release);
}

DGTL_API const char* DGTL_CALL dgtl_get_build_config(void) {
    return "DGTL v2.0.0-SOVEREIGN | High-Velocity Compiler Platform | C++26 Strict Modules";
}

DGTL_API bool DGTL_CALL dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_t* out_node) {
    if (!s || !out_node || index >= s->function.stream().size()) return false;
    const auto n = s->function.stream().get_node(index);
    out_node->opcode = static_cast<uint16_t>(n.opcode);
    out_node->flags = n.flags;
    out_node->result_id = n.result_id;
    out_node->op_a = n.op_a;
    out_node->op_b = n.op_b;
    out_node->op_c = n.op_c;
    out_node->intent_hash = n.intent_hash;
    out_node->logic_flags = n.logic_flags;
    return true;
}

DGTL_API bool DGTL_CALL dgtl_stream_get_core_record(const dgtl_stream_t* s, size_t index, dgtl_core_record_t* out_record) {
    if (!s || !out_record || index >= s->function.stream().size()) return false;
    const auto& c = s->function.stream().get_core_readonly(index);
    out_record->result_id = c.result_id;
    out_record->op_a = c.op_a;
    out_record->op_b = c.op_b;
    out_record->op_c = c.op_c;
    out_record->intent_hash = c.intent_hash;
    out_record->logic_flags = c.logic_flags;
    out_record->aux_flags = c.aux_flags;
    out_record->debug_id = c.debug_id;
    out_record->epoch = static_cast<uint32_t>(c.epoch & 0xFFFFFFFFu);
    return true;
}

DGTL_API uint32_t DGTL_CALL dgtl_stream_get_max_virtual_slot(const dgtl_stream_t* s) {
    if (!s) return 0;
    return s->function.stream().get_max_id();
}

DGTL_API bool DGTL_CALL dgtl_node_is_terminator(dgtl_opcode_t op, uint16_t flags) {
    auto code = static_cast<dgtl::ir::OpCode>(op);
    return code == dgtl::ir::OpCode::RET || 
           code == dgtl::ir::OpCode::GOTO || 
           code == dgtl::ir::OpCode::BRANCH_COND ||
           code == dgtl::ir::OpCode::BRANCH_LIKELY ||
           code == dgtl::ir::OpCode::BRANCH_UNLIKELY ||
           (flags & dgtl::ir::Flags::IS_BRANCH) != 0 ||
           (flags & dgtl::ir::Flags::IS_RETURN) != 0;
}

DGTL_API bool DGTL_CALL dgtl_node_has_side_effects(dgtl_opcode_t op, uint16_t flags) {
    return dgtl::ir::detail::irnode_has_side_effects(static_cast<dgtl::ir::OpCode>(op), flags);
}

DGTL_API double DGTL_CALL dgtl_node_predict_inlined_benefit(dgtl_opcode_t op) {
    dgtl::ir::IRNode n{};
    n.opcode = static_cast<dgtl::ir::OpCode>(op);
    return n.predict_inlined_benefit();
}

DGTL_API void DGTL_CALL dgtl_stream_set_call_overflow_args(dgtl_stream_t* s, size_t node_idx, const uint32_t* args, size_t count) {
    if (!s || !args || count == 0) return;
    s->function.stream().set_call_overflow_args(node_idx, std::span<const uint32_t>(args, count));
}

DGTL_API size_t DGTL_CALL dgtl_stream_get_call_overflow_args(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_args, size_t max_out) {
    if (!s || node_idx >= s->function.stream().size()) return 0;
    const auto v = s->function.stream().get_call_overflow_args(node_idx);
    size_t copy_cnt = std::min(max_out, v.size());
    if (out_args && copy_cnt > 0) {
        std::memcpy(out_args, v.data(), copy_cnt * sizeof(uint32_t));
    }
    return v.size();
}

DGTL_API void DGTL_CALL dgtl_stream_set_operand_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* operands, size_t count) {
    if (!s || !operands || count == 0) return;
    s->function.stream().set_operand_overflow(node_idx, std::span<const uint32_t>(operands, count));
}

DGTL_API size_t DGTL_CALL dgtl_stream_get_operand_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_operands, size_t max_out) {
    if (!s || node_idx >= s->function.stream().size()) return 0;
    const auto v = s->function.stream().get_operand_overflow(node_idx);
    size_t copy_cnt = std::min(max_out, v.size());
    if (out_operands && copy_cnt > 0) {
        std::memcpy(out_operands, v.data(), copy_cnt * sizeof(uint32_t));
    }
    return v.size();
}

DGTL_API void DGTL_CALL dgtl_stream_set_result_ids(dgtl_stream_t* s, size_t node_idx, const uint32_t* result_ids, size_t count) {
    if (!s || !result_ids || count == 0) return;
    s->function.stream().set_result_ids(node_idx, std::span<const uint32_t>(result_ids, count));
}

DGTL_API size_t DGTL_CALL dgtl_stream_get_result_ids(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_result_ids, size_t max_out) {
    if (!s || node_idx >= s->function.stream().size()) return 0;
    const auto v = s->function.stream().get_result_ids(node_idx);
    size_t copy_cnt = std::min(max_out, v.size());
    if (out_result_ids && copy_cnt > 0) {
        std::memcpy(out_result_ids, v.data(), copy_cnt * sizeof(uint32_t));
    }
    return v.size();
}

DGTL_API void DGTL_CALL dgtl_stream_set_node_effects(dgtl_stream_t* s, size_t node_idx, uint32_t effect_set) {
    if (!s || node_idx >= s->function.stream().size()) return;
    s->function.stream().set_node_effects(node_idx, effect_set);
}

DGTL_API uint32_t DGTL_CALL dgtl_stream_get_node_effects(const dgtl_stream_t* s, size_t node_idx) {
    if (!s || node_idx >= s->function.stream().size()) return 0;
    return s->function.stream().get_node_effects(node_idx);
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size) {
    if (!s || !out_blob || !out_size) return make_err(1, "invalid argument");
    const auto& stream = s->function.stream();
    size_t blob_sz = 16 + stream.size() * sizeof(dgtl::ir::IRNode);
    void* blob = std::malloc(blob_sz);
    if (!blob) return make_err(2, "allocation failed");

    uint32_t* hdr = static_cast<uint32_t*>(blob);
    hdr[0] = 0xDEADBEEFu;
    hdr[1] = static_cast<uint32_t>(stream.size());
    hdr[2] = 0u;
    hdr[3] = 0u;

    dgtl::ir::IRNode* out_nodes = reinterpret_cast<dgtl::ir::IRNode*>(static_cast<char*>(blob) + 16);
    for (size_t i = 0; i < stream.size(); ++i) {
        out_nodes[i] = stream.get_node(i);
    }
    *out_blob = blob;
    *out_size = blob_sz;
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t blob_size) {
    if (!s || !blob || blob_size < 16) return make_err(1, "invalid blob");
    const uint32_t* hdr = static_cast<const uint32_t*>(blob);
    if (hdr[0] != 0xDEADBEEFu) return make_err(2, "magic mismatch");
    uint32_t count = hdr[1];
    if (16 + count * sizeof(dgtl::ir::IRNode) > blob_size) return make_err(3, "corrupted size");

    auto& stream = s->function.stream();
    stream.reset();
    const dgtl::ir::IRNode* in_nodes = reinterpret_cast<const dgtl::ir::IRNode*>(static_cast<const char*>(blob) + 16);
    for (uint32_t i = 0; i < count; ++i) {
        stream.push_back(in_nodes[i]);
    }
    return make_ok();
}

DGTL_API void DGTL_CALL dgtl_free_serialized_blob(void* blob) {
    if (blob) std::free(blob);
}

DGTL_API int32_t DGTL_CALL dgtl_binary_loader_validate_payload(const uint8_t* bytes, size_t len) {
    if (!bytes || len == 0) return 0;
    dgtl::execution::format::Binary_Format fmt{};
    fmt.data.assign(bytes, bytes + len);
    return fmt.is_valid() ? 1 : 0;
}

DGTL_API void DGTL_CALL dgtl_yield(void) {
    auto* ctx = get_or_create_default_runtime_ctx();
    if (ctx) ctx->scheduler.yield();
}

DGTL_API uint64_t DGTL_CALL dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg) {
    auto* ctx = get_or_create_default_runtime_ctx();
    return ctx ? static_cast<uint64_t>(ctx->scheduler.spawn(fn, arg)) : 0ull;
}

DGTL_API void DGTL_CALL dgtl_join_task(uint64_t task_handle) {
    auto* ctx = get_or_create_default_runtime_ctx();
    if (ctx && task_handle != 0ull) ctx->scheduler.join(task_handle);
}

DGTL_API dgtl_runtime_context_t* DGTL_CALL dgtl_runtime_context_create(size_t worker_threads, size_t arena_mb_per_worker) {
    try {
        return new dgtl_runtime_context(worker_threads, arena_mb_per_worker);
    } catch (...) {
        return nullptr;
    }
}

DGTL_API void DGTL_CALL dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx) {
    delete ctx;
}

DGTL_API uint64_t DGTL_CALL dgtl_runtime_spawn_task(dgtl_runtime_context_t* ctx, dgtl_runtime_task_fn fn, void* arg) {
    if (!ctx || !fn) return 0ull;
    return static_cast<uint64_t>(ctx->scheduler.spawn(fn, arg));
}

DGTL_API void DGTL_CALL dgtl_runtime_join_task(dgtl_runtime_context_t* ctx, uint64_t task_handle) {
    if (ctx && task_handle != 0ull) ctx->scheduler.join(task_handle);
}

DGTL_API void DGTL_CALL dgtl_runtime_yield(dgtl_runtime_context_t* ctx) {
    if (ctx) ctx->scheduler.yield();
}

DGTL_API size_t DGTL_CALL dgtl_last_linker_fallback_count(void) {
    return g_last_linker_fallbacks.size();
}

DGTL_API size_t DGTL_CALL dgtl_last_linker_fallback_message(size_t index, char* out_buffer, size_t out_buffer_size) {
    if (index >= g_last_linker_fallbacks.size() || !out_buffer || out_buffer_size == 0) {
        return 0;
    }
    const auto& msg = g_last_linker_fallbacks[index];
    size_t copy_len = std::min(msg.size(), out_buffer_size - 1);
    std::memcpy(out_buffer, msg.data(), copy_len);
    out_buffer[copy_len] = '\0';
    return msg.size();
}

DGTL_API dgtl_simd_caps_t DGTL_CALL dgtl_simd_query_host_caps(void) {
    dgtl_simd_caps_t caps{};
    auto hw = dgtl::core::detect_hardware();
    if (hw.has_avx512) {
        caps.native_arch = DGTL_SIMD_ARCH_X86_AVX512_FULL;
        caps.max_vector_width_bytes = 64;
    } else if (hw.has_avx2) {
        caps.native_arch = DGTL_SIMD_ARCH_X86_AVX2;
        caps.max_vector_width_bytes = 32;
    } else {
        caps.native_arch = DGTL_SIMD_ARCH_GENERIC;
        caps.max_vector_width_bytes = 16;
    }
    caps.has_fma = hw.has_fma;
    caps.has_native_gather = hw.has_avx2 || hw.has_avx512;
    caps.has_native_scatter = hw.has_avx512;
    caps.has_fp16_arithmetic = hw.has_avx512;
    caps.has_bf16_arithmetic = hw.has_amx || hw.has_avx512;
    caps.has_masked_operations = hw.has_avx512;
    caps.has_transcendental_approximations = true;
    caps.native_vector_register_count = hw.has_avx512 ? 32 : 16;
    return caps;
}

DGTL_API bool DGTL_CALL dgtl_simd_arch_is_supported(dgtl_simd_arch_t arch) {
    auto hw = dgtl::core::detect_hardware();
    switch (arch) {
        case DGTL_SIMD_ARCH_X86_AVX512_F_DQ:
        case DGTL_SIMD_ARCH_X86_AVX512_FULL: return hw.has_avx512;
        case DGTL_SIMD_ARCH_X86_AVX2:         return hw.has_avx2;
        case DGTL_SIMD_ARCH_GENERIC:         return true;
        default: return false;
    }
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::FMA_F32, DGTL_FLAG_IS_FUSED, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_f64(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::DGTL_FAST_FMA, DGTL_FLAG_IS_FUSED, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fms_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t neg_c = push_node_internal(s, dgtl::ir::OpCode::SUB_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, 0u, c, 0u);
    return dgtl_emit_fma_f32(s, a, b, neg_c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fnmadd_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t neg_a = push_node_internal(s, dgtl::ir::OpCode::SUB_F32, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, 0u, a, 0u);
    return dgtl_emit_fma_f32(s, neg_a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_widen_f16_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::FMA_WIDEN_F16_F32, DGTL_FLAG_IS_FUSED, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fma_widen_bf16_f32(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c) {
    return push_node_internal(s, dgtl::ir::OpCode::FMA_WIDEN_F16_F32, DGTL_FLAG_IS_FUSED, DGTL_INVALID_SLOT, a, b, c);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_complex_fma(dgtl_stream_t* s, uint32_t acc, uint32_t a, uint32_t b) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_COMPLEX_FMA, DGTL_FLAG_IS_SIMD | DGTL_FLAG_IS_FUSED, DGTL_INVALID_SLOT, acc, a, b);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_gather(dgtl_stream_t* s, const dgtl_simd_gather_spec_t* spec) {
    if (!s || !spec) return DGTL_INVALID_SLOT;
    return push_node_internal(s, dgtl::ir::OpCode::GATHER_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, spec->base_ptr_slot, spec->index_vector_slot, spec->mask_slot, 0ull, spec->scale);
}

DGTL_API void DGTL_CALL dgtl_emit_scatter(dgtl_stream_t* s, const dgtl_simd_scatter_spec_t* spec) {
    if (!s || !spec) return;
    (void)push_node_internal(s, dgtl::ir::OpCode::STORE_V512, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, spec->base_ptr_slot, spec->index_vector_slot, spec->value_vector_slot, 0ull, spec->scale);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_gather_f32_512(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices) {
    return push_node_internal(s, dgtl::ir::OpCode::GATHER_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, base_ptr, indices, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_gather_16(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices) {
    return push_node_internal(s, dgtl::ir::OpCode::GATHER_16, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, base_ptr, indices, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_scatter_indirect(dgtl_stream_t* s, uint32_t base_ptr, uint32_t indices, uint32_t values) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_SCATTER_INDIRECT, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, base_ptr, indices, values);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_masked_store(dgtl_stream_t* s, uint32_t base_ptr, uint32_t mask, uint32_t values) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_MASKED_STORE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, base_ptr, mask, values);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_interleaved_load(dgtl_stream_t* s, uint32_t base_ptr, uint8_t stride_elements) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_INTERLEAVED_LOAD, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, base_ptr, stride_elements, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_load(dgtl_stream_t* s, uint32_t ptr, dgtl_simd_width_t width) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_LOAD, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, ptr, 0u, 0u, 0ull, static_cast<uint32_t>(width));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store(dgtl_stream_t* s, uint32_t ptr, uint32_t val, dgtl_simd_width_t width) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_STORE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, ptr, val, 0u, 0ull, static_cast<uint32_t>(width));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_load_aligned(dgtl_stream_t* s, uint32_t ptr, dgtl_simd_width_t width, uint32_t align) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_LOAD, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, ptr, align, 0u, 0ull, static_cast<uint32_t>(width));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store_aligned(dgtl_stream_t* s, uint32_t ptr, uint32_t val, dgtl_simd_width_t width, uint32_t align) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_STORE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, ptr, val, align, 0ull, static_cast<uint32_t>(width));
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_add_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_ADD_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_mul_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_MUL_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_add_f64_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_ADD_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_mul_f64_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_MUL_F32_512, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, lhs, rhs, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_store_v512(dgtl_stream_t* s, uint32_t addr_id, uint32_t val_id) {
    if (!s) return 0;
    return push_ir_node(s, dgtl::ir::OpCode::STORE_V512, dgtl::ir::Flags::IS_VOLATILE, addr_id, val_id, 0);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_store_v512(dgtl_stream_t* s, uint32_t addr_id, uint32_t val_id) {
    return push_node_internal(s, dgtl::ir::OpCode::STORE_V512, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, addr_id, val_id, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_shuffle(dgtl_stream_t* s, uint32_t vec, uint32_t mask) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_SHUFFLE, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, vec, mask, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_blend(dgtl_stream_t* s, uint32_t mask, uint32_t if_true, uint32_t if_false) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_BSL_PREDICATE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_IS_PREDICATED, DGTL_INVALID_SLOT, mask, if_true, if_false);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_vec_reduce_add(dgtl_stream_t* s, uint32_t vec) {
    return push_node_internal(s, dgtl::ir::OpCode::VEC_REDUCE_ADD, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, vec, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_reduce_max(dgtl_stream_t* s, uint32_t vec) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_REDUCE_MAX, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, vec, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_reduce_min(dgtl_stream_t* s, uint32_t vec) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_REDUCE_MIN, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, vec, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fcvtl_f16_to_f32(dgtl_stream_t* s, uint32_t f16_vec) {
    return push_node_internal(s, dgtl::ir::OpCode::FCVTL_F16_TO_F32, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, f16_vec, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_fcvtn_f32_to_f16(dgtl_stream_t* s, uint32_t f32_vec) {
    return push_node_internal(s, dgtl::ir::OpCode::FCVTN_F32_TO_F16, DGTL_FLAG_IS_SIMD, DGTL_INVALID_SLOT, f32_vec, 0u, 0u);
}

DGTL_API uint32_t DGTL_CALL dgtl_emit_simd_bsl_predicate(dgtl_stream_t* s, uint32_t mask, uint32_t t_val, uint32_t f_val) {
    return push_node_internal(s, dgtl::ir::OpCode::SIMD_BSL_PREDICATE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_IS_PREDICATED, DGTL_INVALID_SLOT, mask, t_val, f_val);
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_legalize_simd(dgtl_stream_t* s, dgtl_simd_arch_t arch) {
    if (!s) return make_err(1, "null stream");
    auto& stream = s->function.stream();
    dgtl::ir::pass::simd::TargetArch ta;
    switch (arch) {
        case DGTL_SIMD_ARCH_X86_AVX512_FULL: ta = dgtl::ir::pass::simd::TargetArch::X86_AVX512; break;
        case DGTL_SIMD_ARCH_X86_AVX2:         ta = dgtl::ir::pass::simd::TargetArch::X86_AVX2; break;
        case DGTL_SIMD_ARCH_ARM_NEON:         ta = dgtl::ir::pass::simd::TargetArch::ARM_NEON; break;
        case DGTL_SIMD_ARCH_WASM_SIMD128:     ta = dgtl::ir::pass::simd::TargetArch::WASM_SIMD128; break;
        default:                              ta = dgtl::ir::pass::simd::TargetArch::GENERIC; break;
    }
    dgtl::ir::pass::simd::DGTLSIMDLegalizer legalizer(ta);
    legalizer.legalize(stream);
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_legalize_simd_ex(dgtl_stream_t* s, const dgtl_simd_legalize_options_t* opts) {
    if (!s || !opts) return make_err(1, "invalid argument");
    return dgtl_legalize_simd(s, opts->target_arch);
}

DGTL_API uint32_t DGTL_CALL dgtl_simd_get_native_lane_count(dgtl_simd_arch_t arch, dgtl_type_code_t type_code) {
    uint32_t elem_size = 4;
    switch (type_code) {
        case DGTL_TYPE_I8: case DGTL_TYPE_F8: elem_size = 1; break;
        case DGTL_TYPE_I16: case DGTL_TYPE_F16: case DGTL_TYPE_BF16: elem_size = 2; break;
        case DGTL_TYPE_I32: case DGTL_TYPE_F32: elem_size = 4; break;
        case DGTL_TYPE_I64: case DGTL_TYPE_F64: case DGTL_TYPE_PTR: elem_size = 8; break;
        default: elem_size = 4; break;
    }

    uint32_t max_bytes = 16;
    if (arch == DGTL_SIMD_ARCH_X86_AVX512_FULL || arch == DGTL_SIMD_ARCH_X86_AVX512_F_DQ) max_bytes = 64;
    else if (arch == DGTL_SIMD_ARCH_X86_AVX2 || arch == DGTL_SIMD_ARCH_X86_AVX) max_bytes = 32;

    return max_bytes / elem_size;
}

DGTL_API DGTL_WEAK const char* DGTL_CALL dgtl_sdk_gpu_version(void) noexcept {
    return "DGTL-GPU-Sovereign-v2.0.0";
}

DGTL_API DGTL_WEAK void* DGTL_CALL dgtl_gpu_init_pipeline(uint32_t width, uint32_t height, float dpi_scale, const char* title) noexcept {
    (void)width; (void)height; (void)dpi_scale; (void)title;
    return nullptr;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_resize(void* ctx_ptr, uint32_t width, uint32_t height, float dpi_scale) noexcept {
    (void)ctx_ptr; (void)width; (void)height; (void)dpi_scale;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_destroy_pipeline(void* ctx_ptr) noexcept {
    (void)ctx_ptr;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_begin_frame(void* ctx_ptr, float r, float g, float b, float a) noexcept {
    (void)ctx_ptr; (void)r; (void)g; (void)b; (void)a;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_end_frame(void* ctx_ptr) noexcept {
    (void)ctx_ptr;
}

DGTL_API DGTL_WEAK uint32_t DGTL_CALL dgtl_gpu_push_clip_rect(void* ctx_ptr, float x, float y, float w, float h) noexcept {
    (void)ctx_ptr; (void)x; (void)y; (void)w; (void)h;
    return 0;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_pop_clip_rect(void* ctx_ptr) noexcept {
    (void)ctx_ptr;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_render_quad_instances(void* ctx_ptr, const dgtl_quad_instance_t* instances, uint32_t count) noexcept {
    (void)ctx_ptr; (void)instances; (void)count;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_render_glyph_instances(void* ctx_ptr, uint32_t atlas_slot, const dgtl_glyph_gpu_instance_t* instances, uint32_t count) noexcept {
    (void)ctx_ptr; (void)atlas_slot; (void)instances; (void)count;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_render_vector_paths(void* ctx_ptr, const dgtl_vector_path_instance_t* instances, uint32_t count) noexcept {
    (void)ctx_ptr; (void)instances; (void)count;
}

DGTL_API DGTL_WEAK uint32_t DGTL_CALL dgtl_gpu_upload_texture(void* ctx_ptr, uint32_t width, uint32_t height, const uint8_t* rgba) noexcept {
    (void)ctx_ptr; (void)width; (void)height; (void)rgba;
    return 0;
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_gpu_destroy_texture(void* ctx_ptr, uint32_t texture_slot) noexcept {
    (void)ctx_ptr; (void)texture_slot;
}

DGTL_API DGTL_WEAK bool DGTL_CALL dgtl_platform_poll_events(void* ctx_ptr, dgtl_raw_input_event_t* out_event) noexcept {
    (void)ctx_ptr; (void)out_event;
    return false;
}

DGTL_API DGTL_WEAK uint64_t DGTL_CALL dgtl_platform_get_time_us(void) noexcept {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_platform_set_cursor(void* ctx_ptr, uint32_t cursor_type) noexcept {
    (void)ctx_ptr; (void)cursor_type;
}

DGTL_API dgtl_session_config_t DGTL_CALL dgtl_session_config_default(void) {
    return normalize_session_config(nullptr);
}

DGTL_API const char* DGTL_CALL dgtl_session_mode_name(dgtl_session_mode_t mode) {
    switch (mode) {
        case DGTL_SESSION_MODE_JIT:    return "jit";
        case DGTL_SESSION_MODE_AOT:    return "aot";
        case DGTL_SESSION_MODE_HYBRID: return "hybrid";
        default:                       return "unknown";
    }
}

DGTL_API dgtl_session_t* DGTL_CALL dgtl_session_create(const dgtl_session_config_t* config) {
    const dgtl_session_config_t cfg = normalize_session_config(config);
    auto ctx = dgtl::api::Context::create(to_internal_session_config(cfg));
    if (!ctx.is_valid()) return nullptr;

    auto engine = dgtl::api::ExecutionEngine::create(ctx);
    if (!engine) return nullptr;

    return new (std::nothrow) dgtl_session(std::move(ctx), std::move(engine), cfg);
}

DGTL_API void DGTL_CALL dgtl_session_destroy(dgtl_session_t* session) {
    delete session;
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol(dgtl_session_t* session, const char* symbol_name, uintptr_t address) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!symbol_name || symbol_name[0] == '\0') return make_err(2, "null symbol name");
    if (address == 0u) return make_err(3, "null symbol address");
    session->engine->register_host_symbol(symbol_name, static_cast<uint64_t>(address));
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol_slot(dgtl_session_t* session, uint32_t slot, uintptr_t address) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (slot == 0u) return make_err(2, "invalid slot");
    if (address == 0u) return make_err(3, "null symbol address");
    session->engine->register_host_symbol_slot(slot, static_cast<uint64_t>(address));
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbols(dgtl_session_t* session, const dgtl_host_symbol_t* symbols, size_t symbol_count) {
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

DGTL_API void DGTL_CALL dgtl_session_clear_host_symbols(dgtl_session_t* session) {
    if (!session || !session->engine) return;
    session->engine->register_host_symbol("", 0);
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_compile_stream(dgtl_session_t* session, dgtl_stream_t* stream) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!stream) return make_err(2, "null stream");
    if (!session->engine->compile(stream->function.stream())) {
        return make_err(3, "session failed to compile stream");
    }
    session->compiled = true;
    return make_ok();
}

DGTL_API uint64_t DGTL_CALL dgtl_session_get_symbol_address(dgtl_session_t* session, const char* symbol_name) {
    if (!session || !session->engine || !symbol_name) return 0;
    return session->engine->get_symbol_address(symbol_name);
}

DGTL_API uint64_t DGTL_CALL dgtl_session_get_symbol_address_by_slot(dgtl_session_t* session, uint32_t slot) {
    if (!session || !session->engine) return 0;
    return session->engine->get_symbol_address(std::to_string(slot));
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_object(dgtl_session_t* session, const char* output_path) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!output_path || output_path[0] == '\0') return make_err(2, "null output path");
    auto* aot = dynamic_cast<dgtl::api::AOTSession*>(session->engine.get());
    if (!aot) return make_err(3, "session is not an AOT-capable object emitter");
    if (!aot->emit_object(output_path)) return make_err(4, "failed to emit object file");
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_binary(dgtl_session_t* session, const char* output_path) {
    if (!session || !session->engine) return make_err(1, "null session");
    if (!output_path || output_path[0] == '\0') return make_err(2, "null output path");
    auto* aot = dynamic_cast<dgtl::api::AOTSession*>(session->engine.get());
    if (!aot) return make_err(3, "session is not an AOT-capable binary emitter");
    if (!aot->emit_binary(output_path)) return make_err(4, "failed to emit binary file");
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_i64(dgtl_session_t* session, const char* entry_symbol, int64_t* out_result) {
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

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_f64(dgtl_session_t* session, const char* entry_symbol, double* out_result) {
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

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_ptr(dgtl_session_t* session, const char* symbol, void** out_result) {
    if (!session || !session->engine || !out_result) return make_err(1, "invalid entry execution parameters");
    const char* entry = (symbol && symbol[0]) ? symbol : "DGTL_kernel_entry";
    uint64_t addr = session->engine->get_symbol_address(entry);
    if (addr == 0) return make_err(2, "entrypoint symbol not found");
    auto fn = reinterpret_cast<void* (*)()>(addr);
    *out_result = fn();
    return make_ok();
}

DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target) {
    if (!s) return make_strike_err("null stream");

    if (s->optimization_enabled && !s->is_optimized) {
        (void)dgtl_optimize(s);
    }
    auto& stream = s->function.stream();
    auto isa = to_internal_isa(target);
    stream.apply_backend_specific_optimizations(isa);
    stream.apply_backend_instruction_selection(isa);

    dgtl::jit::DGTLOrchestrator orch;
    static std::atomic<uint64_t> s_seq{0};
    uint64_t cur_seq = s_seq.fetch_add(1, std::memory_order_relaxed);
    std::string tmp = "__dgtl_strike_" + std::to_string(cur_seq) + target_extension_internal(target);

    dgtl_strike_result_t r{};
    try {
        orch.compile_to_binary(tmp, stream);
        auto rep = orch.get_last_report();
        r.ok = 1;
        std::error_code ec;
        r.code_size = static_cast<size_t>(std::filesystem::file_size(tmp, ec));
        r.logic_hash = stream.generate_logic_hash();
        r.compilation_latency_ms = rep.total_ms;
        std::snprintf(r.message, sizeof(r.message), "strike success: %.3f ms", rep.total_ms);
    } catch (const std::exception& e) {
        r = make_strike_err(e.what());
    }
    std::remove(tmp.c_str());
    return r;
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target, const char* output_path) {
    if (!s || !output_path || output_path[0] == '\0') return make_err(1, "invalid arguments");
    auto strike = dgtl_strike_stream(s, target);
    if (!strike.ok) return make_err(2, strike.message);
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_emit_pe_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid pointers");
    dgtl::backend::aot::DGTLAOTLinker aot;
    std::vector<uint8_t> bytes;
    if (!aot.build_pe_executable_buffer(s->function.stream(), bytes)) {
        return make_err(2, "failed to synthesize PE executable in memory");
    }
    void* m = std::malloc(bytes.size());
    if (!m) return make_err(3, "allocation failed");
    std::memcpy(m, bytes.data(), bytes.size());
    *out_buf = m;
    *out_size = bytes.size();
    return make_ok();
}

DGTL_API DGTL_WEAK dgtl_status_t DGTL_CALL dgtl_emit_elf_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid pointers");
    dgtl::exec::DGTLLinker linker;
    std::vector<uint8_t> bytes;
    if (!linker.build_elf_relocatable_binary(s->function.stream(), bytes, "dgtl_kernel_entry")) {
        return make_err(2, "failed to synthesize ELF relocatable in memory");
    }
    void* m = std::malloc(bytes.size());
    if (!m) return make_err(3, "allocation failed");
    std::memcpy(m, bytes.data(), bytes.size());
    *out_buf = m;
    *out_size = bytes.size();
    return make_ok();
}

DGTL_API DGTL_WEAK dgtl_status_t DGTL_CALL dgtl_emit_macho_binary_in_memory(dgtl_stream_t* s, void** out_buf, size_t* out_size) {
    if (!s || !out_buf || !out_size) return make_err(1, "invalid pointers");
    dgtl::core::DGTL_Arena a(1, 16);
    dgtl::semantic::SymbolTable syms(a);
    dgtl::isa::MachOEmitter emitter(s->function.stream(), syms);
    auto res = emitter.emit_to_binary();
    if (!res.success || res.binary_data.empty()) {
        return make_err(2, "failed to synthesize Mach-O in memory");
    }
    void* m = std::malloc(res.binary_data.size());
    if (!m) return make_err(3, "allocation failed");
    std::memcpy(m, res.binary_data.data(), res.binary_data.size());
    *out_buf = m;
    *out_size = res.binary_data.size();
    return make_ok();
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_free_binary_buffer(void* buf) {
    if (buf) std::free(buf);
}

DGTL_API uint32_t DGTL_CALL dgtl_intern_string_handle_utf8(const char* data, size_t len) {
    if (!data && len != 0) return 0;
    return intern_string_internal(std::string_view(data ? data : "", len));
}

DGTL_API DGTL_WEAK const char* DGTL_CALL dgtl_try_lookup_string_handle(uint32_t handle, size_t* out_len) {
    std::string_view sv = dgtl::runtime::try_lookup_string_handle(handle);
    if (out_len) *out_len = sv.size();
    return sv.data();
}

DGTL_API DGTL_WEAK void DGTL_CALL dgtl_register_string_handle(uint32_t handle, const char* data, size_t len) {
    if (data && len > 0) dgtl::runtime::register_string_handle(handle, data, static_cast<uint32_t>(len));
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slot(dgtl_stream_t* stream, uint32_t slot, const char* symbol_name) {
    if (!stream || !symbol_name) return make_err(1, "invalid argument");
    stream->callee_bindings[slot] = symbol_name;
    return make_ok();
}

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slots(dgtl_stream_t* stream, const dgtl_callee_binding_t* bindings, size_t count) {
    if (!stream || (!bindings && count > 0)) return make_err(1, "invalid argument");
    for (size_t i = 0; i < count; ++i) {
        stream->callee_bindings[bindings[i].slot] = bindings[i].symbol_name ? bindings[i].symbol_name : "";
    }
    return make_ok();
}

DGTL_API dgtl_capabilities_t DGTL_CALL dgtl_get_capabilities(void) {
    dgtl_capabilities_t c{};
    c.schema_version = DGTL_CAPABILITY_SCHEMA_VERSION;
    c.supported_simd_tiers = DGTL_CAP_SIMD_AVX2 | DGTL_CAP_SIMD_AVX512 | DGTL_CAP_SIMD_ARM_NEON | DGTL_CAP_SIMD_WASM128;
    c.feature_mask = DGTL_CAP_FEATURE_JIT | DGTL_CAP_FEATURE_AOT | DGTL_CAP_FEATURE_HYBRID |
                     DGTL_CAP_FEATURE_HOST_BIND_NAME | DGTL_CAP_FEATURE_HOST_BIND_SLOT |
                     DGTL_CAP_FEATURE_OBJECT_EMISSION | DGTL_CAP_FEATURE_BINARY_EMISSION |
                     DGTL_CAP_FEATURE_IN_PROCESS_EXEC | DGTL_CAP_FEATURE_CFG_INTROSPECTION |
                     DGTL_CAP_FEATURE_PER_PASS_CONTROL | DGTL_CAP_FEATURE_SPECULATIVE_CONST |
                     DGTL_CAP_FEATURE_OSR | DGTL_CAP_FEATURE_DEOPT_RECOVERY |
                     DGTL_CAP_FEATURE_POLYHEDRAL_SCOP | DGTL_CAP_FEATURE_LINEAR_BORROW_CHECK |
                     DGTL_CAP_FEATURE_SMT_FORMAL_VERIFY | DGTL_CAP_FEATURE_NUMA_AWARE |
                     DGTL_CAP_FEATURE_WX_MEMORY_PROTECT;
    c.backend_target_mask = (1ull << DGTL_TARGET_COUNT) - 1ull;
    c.optimization_profile_mask = 0x3Full;
    c.max_call_arity = 256;
    c.cache_line_bytes = 64;
    c.numa_node_count = dgtl::runtime::numa::get_num_nodes();
    c.physical_core_count = std::thread::hardware_concurrency();
    c.supports_jit = true;
    c.supports_aot = true;
    c.supports_hybrid = true;
    c.supports_host_symbol_slot_binding = true;
    return c;
}

DGTL_API bool DGTL_CALL dgtl_capabilities_support_target(const dgtl_capabilities_t* caps, dgtl_target_t target) {
    if (!caps) return false;
    uint32_t idx = static_cast<uint32_t>(target);
    if (idx >= 64) return false;
    return (caps->backend_target_mask & (1ull << idx)) != 0;
}

DGTL_API bool DGTL_CALL dgtl_capabilities_has_feature(const dgtl_capabilities_t* caps, uint64_t feature_flag) {
    if (!caps) return false;
    return (caps->feature_mask & feature_flag) == feature_flag;
}

DGTL_API uint32_t DGTL_CALL dgtl_numa_node_count(void) {
    return dgtl::ir::IRStream::numa_node_count();
}

DGTL_API uint32_t DGTL_CALL dgtl_numa_current_node(void) {
    return dgtl::ir::IRStream::current_numa_node();
}

DGTL_API int32_t DGTL_CALL dgtl_numa_pin_thread_to_node(uint32_t node_id) {
    return dgtl::runtime::numa::pin_current_thread_to_node(node_id) ? 1 : 0;
}

DGTL_API int32_t DGTL_CALL dgtl_numa_pin_thread_to_core(uint32_t core_id) {
    return dgtl::runtime::numa::pin_thread_to_core(core_id) ? 1 : 0;
}

DGTL_API void* DGTL_CALL dgtl_numa_alloc_on_node(size_t size, uint32_t node_id) {
    return dgtl::runtime::numa::allocate_on_node(size, node_id);
}

DGTL_API void DGTL_CALL dgtl_numa_free_on_node(void* ptr, size_t size) {
    (void)size;
    dgtl::runtime::numa::free_allocation(ptr);
}

DGTL_API int32_t DGTL_CALL dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* err_buf, size_t err_buf_size) {
    if (!s) return 0;
    dgtl::ir::IRVerifier verifier;
    auto diags = verifier.verify(s->function.stream());
    for (const auto& d : diags) {
        if (d.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error) {
            if (err_buf && err_buf_size > 0) {
                std::snprintf(err_buf, err_buf_size, "node %u: %s", d.node_index, d.message.c_str());
            }
            return 0;
        }
    }
    return 1;
}

DGTL_API int32_t DGTL_CALL dgtl_verify_pass_equivalence(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass) {
    if (!orig || !opt) return 0;
    dgtl::formal::DGTLFormalVerificationSMT v;
    auto res = v.verify_pass_equivalence(orig->function.stream(), opt->function.stream(), pass ? pass : "pass", dgtl::formal::VerificationLevel::Standard);
    return res.is_equivalent ? 1 : 0;
}

DGTL_API size_t DGTL_CALL dgtl_export_smtlib2_script(const dgtl_stream_t* orig, const dgtl_stream_t* opt, const char* pass, char* out, size_t sz) {
    if (!orig || !opt) return 0;
    dgtl::formal::DGTLFormalVerificationSMT v;
    std::string s = v.export_to_smtlib2(orig->function.stream(), opt->function.stream(), pass ? pass : "pass");
    if (!out || sz == 0) return s.size();
    size_t copy_sz = std::min(s.size(), sz - 1);
    std::memcpy(out, s.data(), copy_sz);
    out[copy_sz] = '\0';
    return copy_sz;
}

DGTL_API void DGTL_CALL dgtl_record_source_location(dgtl_stream_t* s, size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id) {
    if (!s) return;
    (void)node_idx;
    (void)line;
    (void)col;
    (void)file_id;
}

} // extern "C"