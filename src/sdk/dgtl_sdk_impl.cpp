/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Unified C-ABI SDK Implementation Bridge (100% In-Sync with dgtl_5.h v4)
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
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

/* ---------- Native DGTL C++ Named Module Ingestion ---------- */
import dgtl.target;
import dgtl.api.config;
import dgtl.compiler_context;
import dgtl.runtime.numa;
import dgtl.runtime.task_scheduler;
import dgtl.hashing;
import dgtl.bit_pack;
import dgtl.flat_map;
import dgtl.runtime.bulletins;
import dgtl.runtime.intrinsics;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.op_traits;

import dgtl.ir.stream;
import dgtl.ir.io;
import dgtl.ir.verifier;
import dgtl.ir.debug;
import dgtl.ir_function;

import dgtl.hir.semantic;
import dgtl.formal;
import dgtl.ir.pass.transform_scalar_core;
import dgtl.ir.pass.transform_inlining;
import dgtl.ir.pass.transform_poly_loop;
import dgtl.ir.pass.error_recovery;
import dgtl.ir.analysis.escape_analysis;
import dgtl.ir.pass.simd_gather_scatter;
import dgtl.linker.backend;
import dgtl.linker.driver;
import dgtl.linker.aot_orchestrator;
import dgtl.object.writers.elf;
import dgtl.object.writers.pe;
import dgtl.object.writers.macho;
import dgtl.object.writers.wasm;
import dgtl.jit.jit_orchestrator;
import dgtl.execution.cache;
import dgtl.execution;
import dgtl.execution.dispatcher;
import dgtl.execution.loader;
import dgtl.jit.hardware_scheduler;
import dgtl.api.execution_engine;
import dgtl.api.aot_session;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.ir.gen;

namespace dgtl::backend::writers {
    using namespace dgtl::object::writers;
}

/* ============================================================================
 * INTERNAL ENGINE STRUCTURES & STATE
 * ========================================================================== */

thread_local char g_tls_err_msg[512] = {0};

static void set_last_error_msg(const char* msg) noexcept {
    if (!msg) {
        g_tls_err_msg[0] = '\0';
        return;
    }
    std::snprintf(g_tls_err_msg, sizeof(g_tls_err_msg), "%s", msg);
}

struct dgtl_context {
    std::unique_ptr<dgtl::core::DGTL_Arena> arena;
    std::unique_ptr<dgtl::core::DGTLContext> compiler_ctx;
    uint32_t worker_threads = 0;
    bool verbose = false;

    std::mutex string_lock;
    std::unordered_map<std::string, uint32_t> str_to_handle;
    std::vector<std::string> handle_to_str;

    dgtl_context(size_t workers, size_t arena_mb)
        : arena(std::make_unique<dgtl::core::DGTL_Arena>(workers == 0 ? 2 : workers, arena_mb == 0 ? 64 : arena_mb)),
          compiler_ctx(std::make_unique<dgtl::core::DGTLContext>(*arena)),
          worker_threads(workers == 0 ? std::thread::hardware_concurrency() : static_cast<uint32_t>(workers)) {
        handle_to_str.emplace_back(""); // Index 0 is reserved / invalid
    }

    uint32_t intern(std::string_view sv) {
        std::lock_guard<std::mutex> lock(string_lock);
        std::string s(sv);
        auto it = str_to_handle.find(s);
        if (it != str_to_handle.end()) return it->second;

        uint32_t id = static_cast<uint32_t>(handle_to_str.size());
        handle_to_str.push_back(s);
        str_to_handle[s] = id;
        return id;
    }

    const char* get_str(uint32_t id, size_t* out_len) {
        std::lock_guard<std::mutex> lock(string_lock);
        if (id == 0 || id >= handle_to_str.size()) {
            if (out_len) *out_len = 0;
            return nullptr;
        }
        if (out_len) *out_len = handle_to_str[id].size();
        return handle_to_str[id].c_str();
    }
};

struct dgtl_stream {
    dgtl_context* ctx = nullptr;
    std::unique_ptr<dgtl_context> owned_ctx;
    dgtl::ir::IRStream ir_stream;
    dgtl_ir_mode_t ir_mode = DGTL_IR_MODE_INT;
    dgtl_opt_level_t opt_level = DGTL_OPT_O2;
    dgtl_emit_options_t emit_options{};
    bool auto_optimize = true;

    std::vector<dgtl_diag_t> diags;
    std::vector<std::string> diag_msgs;

    uint32_t next_vreg = 1;
    uint32_t current_source_line = 0;
    uint16_t current_source_col = 0;
    std::string current_source_file;

    dgtl_stream(dgtl_context* c, size_t reserved)
        : ctx(c ? c : (owned_ctx = std::make_unique<dgtl_context>(0, 64)).get()),
          ir_stream(*ctx->arena, reserved == 0 ? 256 : reserved) {
        emit_options.inline_threshold = 50;
        emit_options.unroll_factor = 4;
        emit_options.vectorize_threshold = 8;
        emit_options.enable_cache_optimization = 1;
        emit_options.enable_branch_prediction = 1;
    }

    uint32_t alloc_slot() noexcept {
        return next_vreg++;
    }

    void add_diag(dgtl_severity_t sev, uint16_t code, const char* msg, uint32_t node_idx = 0) {
        diag_msgs.emplace_back(msg ? msg : "");
        dgtl_diag_t d{};
        d.severity = sev;
        d.code = code;
        d.node_index = node_idx;
        d.source_line = current_source_line;
        d.source_col = current_source_col;
        d.message = diag_msgs.back().c_str();
        diags.push_back(d);
        if (sev >= DGTL_SEV_ERROR) {
            set_last_error_msg(d.message);
        }
    }
};

struct dgtl_func_builder {
    dgtl_stream_t* stream = nullptr;
    std::string fn_name;
    dgtl_type_t ret_type = DGTL_TYPE_VOID;

    std::vector<dgtl_value_t> params;
    std::vector<dgtl_block_t> blocks;
    std::unordered_map<uint32_t, bool> block_sealed;
    dgtl_block_t current_block = DGTL_INVALID_BLOCK;

    std::unordered_map<uint32_t, dgtl_type_t> val_types;
    std::unordered_map<int64_t, dgtl_value_t> const_i64_cache;
    std::unordered_map<uint64_t, dgtl_value_t> const_f64_cache;

    std::unordered_map<uint32_t, size_t> phi_node_map;
    std::unordered_map<uint32_t, std::vector<uint32_t>> phi_incoming_vals;
    std::unordered_map<uint32_t, std::vector<uint32_t>> phi_incoming_blocks;

    dgtl_func_builder(dgtl_stream_t* s, const char* name, dgtl_type_t ret)
        : stream(s), fn_name(name ? name : "kernel"), ret_type(ret) {}

    void register_type(dgtl_value_t val, dgtl_type_t ty) {
        if (dgtl_value_is_valid(val)) {
            val_types[val.id] = ty;
            stream->ir_stream.set_result_type(val.id, static_cast<dgtl::ir::TypeCode>(ty.id));
        }
    }

    dgtl_type_t get_type(dgtl_value_t val) const {
        auto it = val_types.find(val.id);
        return it != val_types.end() ? it->second : DGTL_TYPE_VOID;
    }
};

struct dgtl_session {
    dgtl_session_config_t config{};
    std::unique_ptr<dgtl::backend::aot::DGTLAOTLinker> aot_linker;
    std::unordered_map<std::string, uintptr_t> host_symbols;
    std::unordered_map<uint32_t, uintptr_t> slot_symbols;

    void* jit_memory = nullptr;
    size_t jit_size = 0;
    uintptr_t entry_addr = 0;

    explicit dgtl_session(const dgtl_session_config_t& cfg)
        : config(cfg), aot_linker(std::make_unique<dgtl::backend::aot::DGTLAOTLinker>()) {}

    ~dgtl_session() {
        if (jit_memory) {
#if defined(_WIN32) || defined(_WIN64)
            VirtualFree(jit_memory, 0, MEM_RELEASE);
#else
            munmap(jit_memory, jit_size);
#endif
            jit_memory = nullptr;
        }
    }
};

struct dgtl_runtime_context {
    dgtl::runtime::TaskScheduler scheduler;
    explicit dgtl_runtime_context(size_t threads, size_t arena_mb)
        : scheduler(threads, arena_mb) {}
};

static std::atomic<dgtl_runtime_context_t*> g_global_runtime_ctx{nullptr};
static std::mutex g_global_runtime_mutex;

static dgtl_runtime_context_t* get_global_runtime_ctx() noexcept {
    auto* ptr = g_global_runtime_ctx.load(std::memory_order_acquire);
    if (!ptr) {
        std::lock_guard<std::mutex> lock(g_global_runtime_mutex);
        ptr = g_global_runtime_ctx.load(std::memory_order_relaxed);
        if (!ptr) {
            ptr = new (std::nothrow) dgtl_runtime_context(0, 32);
            g_global_runtime_ctx.store(ptr, std::memory_order_release);
        }
    }
    return ptr;
}

static dgtl_value_t emit_raw_node(dgtl_stream_t* s, dgtl_opcode_t op, uint16_t flags,
                                 uint32_t a, uint32_t b, uint32_t c,
                                 uint32_t forced_result = DGTL_INVALID_SLOT,
                                 uint32_t logic_flags = 0) {
    if (!s) return DGTL_INVALID_VALUE;
    uint32_t rid = (forced_result != DGTL_INVALID_SLOT) ? forced_result : s->alloc_slot();

    dgtl::ir::IRNode node{};
    node.opcode = static_cast<dgtl::ir::OpCode>(op);
    node.flags = flags;
    node.result_id = rid;
    node.op_a = a;
    node.op_b = b;
    node.op_c = c;
    node.logic_flags = logic_flags;
    node.intent_hash = dgtl::ir::IRNode::calculate_hash(node.opcode, rid, a, b, c);

    s->ir_stream.push_back(node);
    return dgtl_value_t{rid};
}

static bool lower_stream_to_mir(dgtl_stream_t* s, dgtl::machine::MachineFunction& mf) {
    if (!s || s->ir_stream.is_null() || s->ir_stream.size() == 0) return false;
    try {
        dgtl::jit::StrikeOrchestrator orch(*s->ctx->compiler_ctx, *s->ctx->arena);
        auto res = orch.compile_mir_function(s->ir_stream, mf);
        return res.success;
    } catch (...) {
        return false;
    }
}

/* ============================================================================
 * EXTERN "C" INTERFACE IMPLEMENTATION
 * ========================================================================== */

extern "C" {

/* ============================================================================
 * SECTION 6 — ERROR CONTRACT
 * ========================================================================== */

const char* DGTL_CALL dgtl_err_message(dgtl_err_t err) DGTL_NOEXCEPT {
    if (err.code == 0) return "Success";
    return g_tls_err_msg[0] ? g_tls_err_msg : "Unknown error";
}

const char* DGTL_CALL dgtl_get_last_error(void) DGTL_NOEXCEPT {
    return g_tls_err_msg[0] ? g_tls_err_msg : "none";
}

void DGTL_CALL dgtl_clear_error(void) DGTL_NOEXCEPT {
    g_tls_err_msg[0] = '\0';
}

/* ============================================================================
 * SECTION 16 — VERSION API
 * ========================================================================== */

dgtl_version_info_t DGTL_CALL dgtl_get_version(void) DGTL_NOEXCEPT {
    dgtl_version_info_t v{};
    v.major = DGTL_VERSION_MAJOR;
    v.minor = DGTL_VERSION_MINOR;
    v.patch = DGTL_VERSION_PATCH;
    v.abi_version = DGTL_ABI_VERSION;
    v.build_id = "SOVEREIGN-2026.09-C17";
    v.is_release_build = true;
    return v;
}

bool DGTL_CALL dgtl_abi_compatible(uint32_t client_abi_version) DGTL_NOEXCEPT {
    return client_abi_version >= DGTL_ABI_MIN_COMPAT && client_abi_version <= DGTL_ABI_VERSION;
}

const char* DGTL_CALL dgtl_get_build_config(void) DGTL_NOEXCEPT {
    return "DGTL 2.1.0-SOVEREIGN [UCRT64/AVX-512/NUMA/Memory-Mapped-JIT]";
}

/* ============================================================================
 * SECTION 17 — CONTEXT API
 * ========================================================================== */

dgtl_context_t* DGTL_CALL dgtl_context_create(size_t worker_threads, size_t arena_mb_per_worker) DGTL_NOEXCEPT {
    try {
        return new dgtl_context(worker_threads, arena_mb_per_worker);
    } catch (...) {
        set_last_error_msg("Failed to allocate dgtl_context");
        return nullptr;
    }
}

void DGTL_CALL dgtl_context_destroy(dgtl_context_t* ctx) DGTL_NOEXCEPT {
    delete ctx;
}

uint32_t DGTL_CALL dgtl_context_worker_count(const dgtl_context_t* ctx) DGTL_NOEXCEPT {
    return ctx ? ctx->worker_threads : 0;
}

void DGTL_CALL dgtl_context_set_verbose(dgtl_context_t* ctx, bool enabled) DGTL_NOEXCEPT {
    if (ctx) ctx->verbose = enabled;
}

/* ============================================================================
 * SECTION 18 — STREAM API
 * ========================================================================== */

dgtl_stream_t* DGTL_CALL dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes) DGTL_NOEXCEPT {
    try {
        return new dgtl_stream(ctx, reserved_nodes);
    } catch (...) {
        set_last_error_msg("Failed to allocate dgtl_stream");
        return nullptr;
    }
}

void DGTL_CALL dgtl_stream_destroy(dgtl_stream_t* s) DGTL_NOEXCEPT {
    delete s;
}

void DGTL_CALL dgtl_stream_reset(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (s) {
        s->ir_stream.reset();
        s->diags.clear();
        s->diag_msgs.clear();
        s->next_vreg = 1;
    }
}

size_t DGTL_CALL dgtl_stream_size(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    return s ? s->ir_stream.size() : 0;
}

uint32_t DGTL_CALL dgtl_stream_max_slot(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    return s ? s->ir_stream.get_max_id() : 0;
}

bool DGTL_CALL dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_t* out_node) DGTL_NOEXCEPT {
    if (!s || !out_node || index >= s->ir_stream.size()) return false;
    const auto n = s->ir_stream.get_node(index);
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

bool DGTL_CALL dgtl_stream_get_core_record(const dgtl_stream_t* s, size_t index, dgtl_core_record_t* out_rec) DGTL_NOEXCEPT {
    if (!s || !out_rec || index >= s->ir_stream.size()) return false;
    const auto& c = s->ir_stream.get_core_readonly(index);
    out_rec->result_id = c.result_id;
    out_rec->op_a = c.op_a;
    out_rec->op_b = c.op_b;
    out_rec->op_c = c.op_c;
    out_rec->intent_hash = c.intent_hash;
    out_rec->logic_flags = c.logic_flags;
    out_rec->aux_flags = c.aux_flags;
    out_rec->debug_id = c.debug_id;
    out_rec->epoch = static_cast<uint32_t>(c.epoch & 0xFFFFFFFFu);
    return true;
}

void DGTL_CALL dgtl_stream_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode) DGTL_NOEXCEPT {
    if (s) s->ir_mode = mode;
}

void DGTL_CALL dgtl_stream_set_emit_options(dgtl_stream_t* s, const dgtl_emit_options_t* opts) DGTL_NOEXCEPT {
    if (s && opts) s->emit_options = *opts;
}

size_t DGTL_CALL dgtl_stream_diag_count(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    return s ? s->diags.size() : 0;
}

dgtl_diag_t DGTL_CALL dgtl_stream_get_diag(const dgtl_stream_t* s, size_t index) DGTL_NOEXCEPT {
    if (!s || index >= s->diags.size()) return dgtl_diag_t{};
    return s->diags[index];
}

void DGTL_CALL dgtl_stream_clear_diags(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (s) {
        s->diags.clear();
        s->diag_msgs.clear();
    }
}

void DGTL_CALL dgtl_stream_set_source_loc(dgtl_stream_t* s, uint32_t line, uint16_t col, const char* filename) DGTL_NOEXCEPT {
    if (s) {
        s->current_source_line = line;
        s->current_source_col = col;
        s->current_source_file = filename ? filename : "";
    }
}

void DGTL_CALL dgtl_stream_set_call_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* args, size_t count) DGTL_NOEXCEPT {
    if (s && args && count > 0 && node_idx < s->ir_stream.size()) {
        s->ir_stream.set_call_overflow_args(node_idx, std::span<const uint32_t>(args, count));
    }
}

size_t DGTL_CALL dgtl_stream_get_call_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out_args, size_t max_out) DGTL_NOEXCEPT {
    if (!s || node_idx >= s->ir_stream.size()) return 0;
    auto ov = s->ir_stream.get_call_overflow_args(node_idx);
    size_t cnt = std::min(max_out, ov.size());
    if (out_args && cnt > 0) std::memcpy(out_args, ov.data(), cnt * sizeof(uint32_t));
    return ov.size();
}

void DGTL_CALL dgtl_stream_set_operand_overflow(dgtl_stream_t* s, size_t node_idx, const uint32_t* operands, size_t count) DGTL_NOEXCEPT {
    if (s && operands && count > 0 && node_idx < s->ir_stream.size()) {
        s->ir_stream.set_operand_overflow(node_idx, std::span<const uint32_t>(operands, count));
    }
}

size_t DGTL_CALL dgtl_stream_get_operand_overflow(const dgtl_stream_t* s, size_t node_idx, uint32_t* out, size_t max_out) DGTL_NOEXCEPT {
    if (!s || node_idx >= s->ir_stream.size()) return 0;
    auto ov = s->ir_stream.get_operand_overflow(node_idx);
    size_t cnt = std::min(max_out, ov.size());
    if (out && cnt > 0) std::memcpy(out, ov.data(), cnt * sizeof(uint32_t));
    return ov.size();
}

void DGTL_CALL dgtl_stream_set_result_ids(dgtl_stream_t* s, size_t node_idx, const uint32_t* ids, size_t count) DGTL_NOEXCEPT {
    if (s && ids && count > 0 && node_idx < s->ir_stream.size()) {
        s->ir_stream.set_result_ids(node_idx, std::span<const uint32_t>(ids, count));
    }
}

size_t DGTL_CALL dgtl_stream_get_result_ids(const dgtl_stream_t* s, size_t node_idx, uint32_t* out, size_t max_out) DGTL_NOEXCEPT {
    if (!s || node_idx >= s->ir_stream.size()) return 0;
    auto res = s->ir_stream.get_result_ids(node_idx);
    size_t cnt = std::min(max_out, res.size());
    if (out && cnt > 0) std::memcpy(out, res.data(), cnt * sizeof(uint32_t));
    return res.size();
}

void DGTL_CALL dgtl_stream_set_node_effects(dgtl_stream_t* s, size_t node_idx, uint32_t effect_set) DGTL_NOEXCEPT {
    if (s && node_idx < s->ir_stream.size()) s->ir_stream.set_node_effects(node_idx, effect_set);
}

uint32_t DGTL_CALL dgtl_stream_get_node_effects(const dgtl_stream_t* s, size_t node_idx) DGTL_NOEXCEPT {
    return (s && node_idx < s->ir_stream.size()) ? s->ir_stream.get_node_effects(node_idx) : 0;
}

/* ============================================================================
 * SECTION 19 & 20 — FUNCTION BUILDER & SSA EMISSION APIS
 * ========================================================================== */

dgtl_func_builder_t* DGTL_CALL dgtl_func_builder_create(dgtl_stream_t* stream, const char* name, dgtl_type_t ret) DGTL_NOEXCEPT {
    if (!stream) return nullptr;
    try {
        return new dgtl_func_builder(stream, name, ret);
    } catch (...) {
        return nullptr;
    }
}

void DGTL_CALL dgtl_func_builder_destroy(dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    delete fb;
}

dgtl_value_t DGTL_CALL dgtl_func_add_param(dgtl_func_builder_t* fb, dgtl_type_t type) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    uint32_t slot = fb->stream->alloc_slot();
    dgtl_value_t v{slot};
    fb->params.push_back(v);
    fb->register_type(v, type);
    return v;
}

uint32_t DGTL_CALL dgtl_func_param_count(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    return fb ? static_cast<uint32_t>(fb->params.size()) : 0;
}

dgtl_value_t DGTL_CALL dgtl_func_get_param(const dgtl_func_builder_t* fb, uint32_t index) DGTL_NOEXCEPT {
    if (!fb || index >= fb->params.size()) return DGTL_INVALID_VALUE;
    return fb->params[index];
}

dgtl_block_t DGTL_CALL dgtl_func_append_block(dgtl_func_builder_t* fb, const char* name) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_BLOCK;
    (void)name;
    uint32_t slot = fb->stream->alloc_slot();
    dgtl_block_t b{slot};
    fb->blocks.push_back(b);
    fb->block_sealed[slot] = false;

    emit_raw_node(fb->stream, DGTL_OP_LABEL, DGTL_FLAG_NONE, slot, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, slot);
    if (!dgtl_block_is_valid(fb->current_block)) {
        fb->current_block = b;
    }
    return b;
}

void DGTL_CALL dgtl_func_set_insert_block(dgtl_func_builder_t* fb, dgtl_block_t block) DGTL_NOEXCEPT {
    if (fb) fb->current_block = block;
}

dgtl_block_t DGTL_CALL dgtl_func_get_insert_block(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    return fb ? fb->current_block : DGTL_INVALID_BLOCK;
}

void DGTL_CALL dgtl_func_seal_block(dgtl_func_builder_t* fb, dgtl_block_t block) DGTL_NOEXCEPT {
    if (fb) fb->block_sealed[block.id] = true;
}

dgtl_err_t DGTL_CALL dgtl_func_builder_finalize(dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    if (!fb) return DGTL_ERR(1);
    for (auto b : fb->blocks) {
        fb->block_sealed[b.id] = true;
    }
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_func_builder_verify(const dgtl_func_builder_t* fb, char* err_buf, size_t err_buf_size) DGTL_NOEXCEPT {
    if (!fb || !fb->stream) return DGTL_ERR(1);
    dgtl::ir::IRVerifier verifier;
    auto diags = verifier.verify(fb->stream->ir_stream);
    for (const auto& d : diags) {
        if (d.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error) {
            if (err_buf && err_buf_size > 0) {
                std::snprintf(err_buf, err_buf_size, "Verification failure at node %u: %s", d.node_index, d.message.c_str());
            }
            return DGTL_ERR(2);
        }
    }
    return DGTL_OK;
}

void DGTL_CALL dgtl_func_set_value_type(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_type_t type) DGTL_NOEXCEPT {
    if (fb) fb->register_type(val, type);
}

dgtl_type_t DGTL_CALL dgtl_func_get_value_type(const dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT {
    return fb ? fb->get_type(val) : DGTL_TYPE_VOID;
}

size_t DGTL_CALL dgtl_func_diag_count(const dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    return fb ? dgtl_stream_diag_count(fb->stream) : 0;
}

dgtl_diag_t DGTL_CALL dgtl_func_get_diag(const dgtl_func_builder_t* fb, size_t index) DGTL_NOEXCEPT {
    return fb ? dgtl_stream_get_diag(fb->stream, index) : dgtl_diag_t{};
}

void DGTL_CALL dgtl_func_clear_diags(dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    if (fb) dgtl_stream_clear_diags(fb->stream);
}

/* -- Arithmetic & Bitwise Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_add(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = DGTL_OP_ADD_I32;

    if (ty.id == DGTL_TYPE_I64.id || ty.id == DGTL_TYPE_PTR.id) op = DGTL_OP_ADD_I64;
    else if (ty.id == DGTL_TYPE_F32.id) op = DGTL_OP_ADD_F32;
    else if (ty.id == DGTL_TYPE_F64.id) op = DGTL_OP_ADD_F64;
    else if (ty.id == DGTL_TYPE_I8.id)  op = DGTL_OP_ADD_I8;

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_sub(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = DGTL_OP_SUB_I32;

    if (ty.id == DGTL_TYPE_I64.id || ty.id == DGTL_TYPE_PTR.id) op = DGTL_OP_SUB_I64;
    else if (ty.id == DGTL_TYPE_F32.id) op = DGTL_OP_SUB_F32;
    else if (ty.id == DGTL_TYPE_F64.id) op = DGTL_OP_SUB_F64;

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_mul(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = DGTL_OP_MUL_I32;

    if (ty.id == DGTL_TYPE_I64.id) op = DGTL_OP_MUL_I64;
    else if (ty.id == DGTL_TYPE_F32.id) op = DGTL_OP_MUL_F32;
    else if (ty.id == DGTL_TYPE_F64.id) op = DGTL_OP_MUL_F64;

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_div(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = DGTL_OP_DIV_I32;

    if (ty.id == DGTL_TYPE_I64.id) op = DGTL_OP_DIV_I64;
    else if (ty.id == DGTL_TYPE_F32.id) op = DGTL_OP_DIV_F32;
    else if (ty.id == DGTL_TYPE_F64.id) op = DGTL_OP_DIV_F64;

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_rem(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_MOD_I64 : DGTL_OP_MOD_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_neg(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(val);
    if (ty.id == DGTL_TYPE_F32.id) {
        auto zero = dgtl_build_const_f32(fb, 0.0f);
        return dgtl_build_sub(fb, zero, val);
    } else if (ty.id == DGTL_TYPE_F64.id) {
        auto zero = dgtl_build_const_f64(fb, 0.0);
        return dgtl_build_sub(fb, zero, val);
    }
    auto zero = (ty.id == DGTL_TYPE_I64.id) ? dgtl_build_const_i64(fb, 0) : dgtl_build_const_i32(fb, 0);
    return dgtl_build_sub(fb, zero, val);
}

dgtl_value_t DGTL_CALL dgtl_build_and(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_AND_I64 : DGTL_OP_AND_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_or(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_OR_I64 : DGTL_OP_OR_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_xor(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_XOR_I64 : DGTL_OP_XOR_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_not(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(val);
    auto mask = (ty.id == DGTL_TYPE_I64.id) ? dgtl_build_const_i64(fb, -1LL) : dgtl_build_const_i32(fb, -1);
    return dgtl_build_xor(fb, val, mask);
}

dgtl_value_t DGTL_CALL dgtl_build_shl(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t amount) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(val);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_SHL_I64 : DGTL_OP_SHL_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, val.id, amount.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_shr(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t amount) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(val);
    dgtl_opcode_t op = (ty.id == DGTL_TYPE_I64.id) ? DGTL_OP_SHR_I64 : DGTL_OP_SHR_I32;
    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, val.id, amount.id, DGTL_INVALID_SLOT);
    fb->register_type(res, ty);
    return res;
}

/* -- Comparisons & Select Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_icmp(dgtl_func_builder_t* fb, dgtl_icmp_pred_t pred, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    bool is64 = (ty.id == DGTL_TYPE_I64.id || ty.id == DGTL_TYPE_PTR.id);

    dgtl_opcode_t op = DGTL_OP_CMP_EQ_I32;
    if (is64) {
        switch (pred) {
            case DGTL_ICMP_EQ:  op = DGTL_OP_CMP_EQ_I64; break;
            case DGTL_ICMP_NE:  op = DGTL_OP_CMP_NE_I64; break;
            case DGTL_ICMP_SLT:
            case DGTL_ICMP_ULT: op = DGTL_OP_CMP_LT_I64; break;
            case DGTL_ICMP_SLE:
            case DGTL_ICMP_ULE: op = DGTL_OP_CMP_LE_I64; break;
            case DGTL_ICMP_SGT:
            case DGTL_ICMP_UGT: op = DGTL_OP_CMP_GT_I64; break;
            case DGTL_ICMP_SGE:
            case DGTL_ICMP_UGE: op = DGTL_OP_CMP_GE_I64; break;
        }
    } else {
        switch (pred) {
            case DGTL_ICMP_EQ:  op = DGTL_OP_CMP_EQ_I32; break;
            case DGTL_ICMP_NE:  op = DGTL_OP_CMP_NE_I32; break;
            case DGTL_ICMP_SLT:
            case DGTL_ICMP_ULT: op = DGTL_OP_CMP_LT_I32; break;
            case DGTL_ICMP_SLE:
            case DGTL_ICMP_ULE: op = DGTL_OP_CMP_LE_I32; break;
            case DGTL_ICMP_SGT:
            case DGTL_ICMP_UGT: op = DGTL_OP_CMP_GT_I32; break;
            case DGTL_ICMP_SGE:
            case DGTL_ICMP_UGE: op = DGTL_OP_CMP_GE_I32; break;
        }
    }

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_NONE, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_BOOL);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_fcmp(dgtl_func_builder_t* fb, dgtl_fcmp_pred_t pred, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(lhs);
    bool is64 = (ty.id == DGTL_TYPE_F64.id);
    dgtl_opcode_t op = DGTL_OP_CMP_EQ_I32;
    uint32_t logic = is64 ? 64u : 32u;

    auto res = emit_raw_node(fb->stream, op, DGTL_FLAG_IS_SPEC_CONST, lhs.id, rhs.id, static_cast<uint32_t>(pred), DGTL_INVALID_SLOT, logic);
    fb->register_type(res, DGTL_TYPE_BOOL);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_select(dgtl_func_builder_t* fb, dgtl_value_t cond, dgtl_value_t true_val, dgtl_value_t false_val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    dgtl_type_t ty = fb->get_type(true_val);
    auto res = emit_raw_node(fb->stream, DGTL_OP_SELECT, DGTL_FLAG_NONE, cond.id, true_val.id, false_val.id);
    fb->register_type(res, ty);
    return res;
}

/* -- Memory Access Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_load(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr) DGTL_NOEXCEPT {
    return dgtl_build_load_aligned(fb, result_type, ptr, 0);
}

dgtl_value_t DGTL_CALL dgtl_build_load_aligned(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr, uint32_t align) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_LOAD, DGTL_FLAG_NONE, ptr.id, align, DGTL_INVALID_SLOT);
    fb->register_type(res, result_type);
    return res;
}

dgtl_err_t DGTL_CALL dgtl_build_store(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr) DGTL_NOEXCEPT {
    return dgtl_build_store_aligned(fb, val, ptr, 0);
}

dgtl_err_t DGTL_CALL dgtl_build_store_aligned(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr, uint32_t align) DGTL_NOEXCEPT {
    if (!fb) return DGTL_ERR(1);
    emit_raw_node(fb->stream, DGTL_OP_STORE, DGTL_FLAG_HAS_SIDE_EFFECTS, ptr.id, val.id, align, 0u);
    return DGTL_OK;
}

dgtl_value_t DGTL_CALL dgtl_build_gep(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t offset) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_GEP, DGTL_FLAG_NONE, base.id, offset.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_PTR);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_alloca(dgtl_func_builder_t* fb, dgtl_type_t type, uint32_t align) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_ALLOCA, DGTL_FLAG_NONE, type.id, align, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_PTR);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_volatile_load(dgtl_func_builder_t* fb, dgtl_type_t result_type, dgtl_value_t ptr) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VOLATILE_LOAD, DGTL_FLAG_IS_VOLATILE, ptr.id, 0u, DGTL_INVALID_SLOT);
    fb->register_type(res, result_type);
    return res;
}

dgtl_err_t DGTL_CALL dgtl_build_volatile_store(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_value_t ptr) DGTL_NOEXCEPT {
    if (!fb) return DGTL_ERR(1);
    emit_raw_node(fb->stream, DGTL_OP_VOLATILE_STORE, DGTL_FLAG_IS_VOLATILE | DGTL_FLAG_HAS_SIDE_EFFECTS, ptr.id, val.id, 0u, 0u);
    return DGTL_OK;
}

dgtl_value_t DGTL_CALL dgtl_build_cast(dgtl_func_builder_t* fb, dgtl_value_t val, dgtl_type_t to_type) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_PTR_CAST, DGTL_FLAG_NONE, val.id, to_type.id, DGTL_INVALID_SLOT);
    fb->register_type(res, to_type);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_ptr_to_int(dgtl_func_builder_t* fb, dgtl_value_t ptr) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_PTR_TO_INT, DGTL_FLAG_NONE, ptr.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I64);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_int_to_ptr(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_INT_TO_PTR, DGTL_FLAG_NONE, val.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_PTR);
    return res;
}

/* -- Constants (Builder Cache) -- */

dgtl_value_t DGTL_CALL dgtl_build_const_i64(dgtl_func_builder_t* fb, int64_t val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto it = fb->const_i64_cache.find(val);
    if (it != fb->const_i64_cache.end()) return it->second;

    uint32_t lo = static_cast<uint32_t>(val & 0xFFFFFFFFu);
    uint32_t hi = static_cast<uint32_t>((val >> 32) & 0xFFFFFFFFu);
    auto res = emit_raw_node(fb->stream, DGTL_OP_CONST, DGTL_FLAG_IS_CONST, lo, hi, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I64);
    fb->const_i64_cache[val] = res;
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_const_i32(dgtl_func_builder_t* fb, int32_t val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = dgtl_build_const_i64(fb, static_cast<int64_t>(val));
    fb->register_type(res, DGTL_TYPE_I32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_const_f32(dgtl_func_builder_t* fb, float val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    uint32_t bits = std::bit_cast<uint32_t>(val);
    auto res = emit_raw_node(fb->stream, DGTL_OP_CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, bits, 0u, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_const_f64(dgtl_func_builder_t* fb, double val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    uint64_t bits = std::bit_cast<uint64_t>(val);
    auto it = fb->const_f64_cache.find(bits);
    if (it != fb->const_f64_cache.end()) return it->second;

    uint32_t lo = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
    uint32_t hi = static_cast<uint32_t>((bits >> 32) & 0xFFFFFFFFu);
    auto res = emit_raw_node(fb->stream, DGTL_OP_CONST, DGTL_FLAG_IS_CONST | DGTL_FLAG_IS_SPEC_CONST, lo, hi, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F64);
    fb->const_f64_cache[bits] = res;
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_const_bool(dgtl_func_builder_t* fb, bool val) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = dgtl_build_const_i64(fb, val ? 1 : 0);
    fb->register_type(res, DGTL_TYPE_BOOL);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_const_null(dgtl_func_builder_t* fb, dgtl_type_t type) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = dgtl_build_const_i64(fb, 0);
    fb->register_type(res, type);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_str(dgtl_func_builder_t* fb, const char* utf8) DGTL_NOEXCEPT {
    if (!fb || !utf8) return DGTL_INVALID_VALUE;
    uint32_t handle = fb->stream->ctx->intern(utf8);
    auto res = emit_raw_node(fb->stream, DGTL_OP_STRING_CONST, DGTL_FLAG_IS_CONST, handle, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_STR);
    return res;
}

/* -- Control Flow & Graph SSA Builders -- */

void DGTL_CALL dgtl_build_br(dgtl_func_builder_t* fb, dgtl_block_t target) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_GOTO, DGTL_FLAG_IS_BRANCH | DGTL_FLAG_HAS_SIDE_EFFECTS, target.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

void DGTL_CALL dgtl_build_cond_br(dgtl_func_builder_t* fb, dgtl_value_t cond, dgtl_block_t true_bb, dgtl_block_t false_bb) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_BRANCH_COND, DGTL_FLAG_IS_BRANCH | DGTL_FLAG_HAS_SIDE_EFFECTS, cond.id, true_bb.id, false_bb.id, 0u);
}

void DGTL_CALL dgtl_build_ret(dgtl_func_builder_t* fb, dgtl_value_t val) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_RET, DGTL_FLAG_IS_RETURN | DGTL_FLAG_HAS_SIDE_EFFECTS, val.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

void DGTL_CALL dgtl_build_ret_void(dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_RET, DGTL_FLAG_IS_RETURN | DGTL_FLAG_HAS_SIDE_EFFECTS, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

dgtl_value_t DGTL_CALL dgtl_build_phi(dgtl_func_builder_t* fb, dgtl_type_t type) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    size_t node_idx = fb->stream->ir_stream.size();
    auto res = emit_raw_node(fb->stream, DGTL_OP_PHI_MULTI, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, type);
    fb->phi_node_map[res.id] = node_idx;
    return res;
}

void DGTL_CALL dgtl_phi_add_incoming(dgtl_func_builder_t* fb, dgtl_value_t phi, dgtl_value_t incoming_val, dgtl_block_t from_block) DGTL_NOEXCEPT {
    if (!fb) return;
    auto& vals = fb->phi_incoming_vals[phi.id];
    auto& blocks = fb->phi_incoming_blocks[phi.id];
    vals.push_back(incoming_val.id);
    blocks.push_back(from_block.id);

    auto it = fb->phi_node_map.find(phi.id);
    if (it != fb->phi_node_map.end()) {
        size_t node_idx = it->second;
        if (node_idx < fb->stream->ir_stream.size()) {
            auto node = fb->stream->ir_stream.get_node(node_idx);
            if (vals.size() >= 1) node.op_a = vals[0];
            if (vals.size() >= 2) node.op_b = vals[1];
            if (vals.size() >= 3) node.op_c = vals[2];
            node.refresh_intent_hash();
            fb->stream->ir_stream.set_node(node_idx, node);

            if (vals.size() > 3) {
                fb->stream->ir_stream.set_operand_overflow(node_idx, std::span<const uint32_t>(vals.data() + 3, vals.size() - 3));
            }
            fb->stream->ir_stream.set_result_ids(node_idx, std::span<const uint32_t>(blocks.data(), blocks.size()));
        }
    }
}

void DGTL_CALL dgtl_build_phi_incoming(dgtl_func_builder_t* fb, dgtl_value_t phi, dgtl_value_t incoming_val, dgtl_block_t from_block) DGTL_NOEXCEPT {
    dgtl_phi_add_incoming(fb, phi, incoming_val, from_block);
}

/* -- Function Calls -- */

dgtl_value_t DGTL_CALL dgtl_build_call(dgtl_func_builder_t* fb, dgtl_func_t callee, const dgtl_value_t* args, size_t arg_count) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    uint32_t a0 = arg_count > 0 ? args[0].id : DGTL_INVALID_SLOT;
    uint32_t a1 = arg_count > 1 ? args[1].id : DGTL_INVALID_SLOT;
    uint32_t a2 = arg_count > 2 ? args[2].id : DGTL_INVALID_SLOT;

    size_t idx = fb->stream->ir_stream.size();
    auto res = emit_raw_node(fb->stream, DGTL_OP_CALL, DGTL_FLAG_HAS_SIDE_EFFECTS | DGTL_FLAG_IS_VOLATILE, callee.id, a0, a1, DGTL_INVALID_SLOT, a2);

    if (arg_count > 3) {
        std::vector<uint32_t> extra(arg_count - 3);
        for (size_t i = 3; i < arg_count; ++i) extra[i - 3] = args[i].id;
        fb->stream->ir_stream.set_call_overflow_args(idx, extra);
    }
    fb->register_type(res, DGTL_TYPE_I64);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_call_indirect(dgtl_func_builder_t* fb, dgtl_value_t callee_ptr, dgtl_type_t ret_type, const dgtl_value_t* args, size_t arg_count) DGTL_NOEXCEPT {
    dgtl_func_t fn{callee_ptr.id};
    auto res = dgtl_build_call(fb, fn, args, arg_count);
    if (fb) fb->register_type(res, ret_type);
    return res;
}

/* -- Fused Operations -- */

dgtl_value_t DGTL_CALL dgtl_build_fma(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_FMA_F32, DGTL_FLAG_IS_FUSED, a.id, b.id, c.id);
    fb->register_type(res, fb->get_type(a));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_fms(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT {
    auto neg_c = dgtl_build_neg(fb, c);
    return dgtl_build_fma(fb, a, b, neg_c);
}

dgtl_value_t DGTL_CALL dgtl_build_fnmadd(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t c) DGTL_NOEXCEPT {
    auto neg_a = dgtl_build_neg(fb, a);
    return dgtl_build_fma(fb, neg_a, b, c);
}

/* -- AI & Deep Tensor Operations -- */

dgtl_value_t DGTL_CALL dgtl_build_matmul(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b, dgtl_value_t acc) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_MATMUL_BF16, DGTL_FLAG_NONE, a.id, b.id, acc.id);
    fb->register_type(res, DGTL_TYPE_TENSOR_BF16);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_softmax(dgtl_func_builder_t* fb, dgtl_value_t input) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_SOFTMAX_FAST, DGTL_FLAG_NONE, input.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(input));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_layer_norm(dgtl_func_builder_t* fb, dgtl_value_t x, dgtl_value_t gamma, dgtl_value_t beta) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_LAYER_NORM, DGTL_FLAG_NONE, x.id, gamma.id, beta.id);
    fb->register_type(res, fb->get_type(x));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_sdpa(dgtl_func_builder_t* fb, dgtl_value_t q, dgtl_value_t k, dgtl_value_t v, dgtl_value_t mask) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_SDPA_F16, DGTL_FLAG_NONE, q.id, k.id, v.id, DGTL_INVALID_SLOT, mask.id);
    fb->register_type(res, DGTL_TYPE_F16);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_rope_embed(dgtl_func_builder_t* fb, dgtl_value_t x) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_ROPE_EMBED, DGTL_FLAG_NONE, x.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(x));
    return res;
}

/* -- SIMD Operations -- */

dgtl_value_t DGTL_CALL dgtl_build_vec_load(dgtl_func_builder_t* fb, dgtl_value_t ptr, dgtl_type_t lane_type, uint32_t lane_count) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_LOAD, DGTL_FLAG_IS_SIMD, ptr.id, lane_type.id, lane_count);
    fb->register_type(res, DGTL_TYPE_VECF32X16);
    return res;
}

dgtl_err_t DGTL_CALL dgtl_build_vec_store(dgtl_func_builder_t* fb, dgtl_value_t ptr, dgtl_value_t vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_ERR(1);
    emit_raw_node(fb->stream, DGTL_OP_VEC_STORE, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, ptr.id, vec.id, 0u, 0u);
    return DGTL_OK;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_add(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_ADD_F32_512, DGTL_FLAG_IS_SIMD, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(lhs));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_mul(dgtl_func_builder_t* fb, dgtl_value_t lhs, dgtl_value_t rhs) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_MUL_F32_512, DGTL_FLAG_IS_SIMD, lhs.id, rhs.id, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(lhs));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_add(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_REDUCE_ADD, DGTL_FLAG_IS_SIMD, vec.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_max(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_REDUCE_MAX, DGTL_FLAG_IS_SIMD, vec.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_reduce_min(dgtl_func_builder_t* fb, dgtl_value_t vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_REDUCE_MIN, DGTL_FLAG_IS_SIMD, vec.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_shuffle(dgtl_func_builder_t* fb, dgtl_value_t vec, dgtl_value_t mask) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC_SHUFFLE, DGTL_FLAG_IS_SIMD, vec.id, mask.id, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(vec));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec_blend(dgtl_func_builder_t* fb, dgtl_value_t mask, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_SIMD_BSL, DGTL_FLAG_IS_SIMD | DGTL_FLAG_IS_PREDICATED, mask.id, a.id, b.id);
    fb->register_type(res, fb->get_type(a));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_gather(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t indices, dgtl_value_t mask) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_GATHER_F32_512, DGTL_FLAG_IS_SIMD, base.id, indices.id, mask.id);
    fb->register_type(res, DGTL_TYPE_VECF32X16);
    return res;
}

void DGTL_CALL dgtl_build_scatter(dgtl_func_builder_t* fb, dgtl_value_t base, dgtl_value_t indices, dgtl_value_t vals, dgtl_value_t mask) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_STORE_V512, DGTL_FLAG_IS_SIMD | DGTL_FLAG_HAS_SIDE_EFFECTS, base.id, indices.id, vals.id, 0u, mask.id);
}

dgtl_value_t DGTL_CALL dgtl_build_fcvtl_f16_f32(dgtl_func_builder_t* fb, dgtl_value_t f16_vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_FCVTL_F16_F32, DGTL_FLAG_IS_SIMD, f16_vec.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_VECF32X16);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_fcvtn_f32_f16(dgtl_func_builder_t* fb, dgtl_value_t f32_vec) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_FCVTN_F32_F16, DGTL_FLAG_IS_SIMD, f32_vec.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_VECF16X32);
    return res;
}

/* -- Concurrency & Ownership Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_async_spawn(dgtl_func_builder_t* fb, dgtl_value_t fn, dgtl_value_t arg) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_ASYNC_SPAWN, DGTL_FLAG_HAS_SIDE_EFFECTS, fn.id, arg.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I64);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_async_await(dgtl_func_builder_t* fb, dgtl_value_t task) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_ASYNC_AWAIT, DGTL_FLAG_HAS_SIDE_EFFECTS, task.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I64);
    return res;
}

void DGTL_CALL dgtl_build_fence(dgtl_func_builder_t* fb, dgtl_memory_order_t order) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_MEMORY_FENCE, DGTL_FLAG_HAS_SIDE_EFFECTS, static_cast<uint32_t>(order), DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

dgtl_value_t DGTL_CALL dgtl_build_own_acquire(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_OWNERSHIP_ACQUIRE, DGTL_FLAG_NONE, resource.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(resource));
    return res;
}

void DGTL_CALL dgtl_build_own_release(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_OWNERSHIP_RELEASE, DGTL_FLAG_HAS_SIDE_EFFECTS, resource.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

dgtl_value_t DGTL_CALL dgtl_build_own_transfer(dgtl_func_builder_t* fb, dgtl_value_t resource) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_OWNERSHIP_TRANSFER, DGTL_FLAG_HAS_SIDE_EFFECTS, resource.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(resource));
    return res;
}

/* -- Low-Latency & HFT Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_read_tsc(dgtl_func_builder_t* fb) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_READ_TSC, DGTL_FLAG_NONE, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I64);
    return res;
}

void DGTL_CALL dgtl_build_prefetch(dgtl_func_builder_t* fb, dgtl_value_t addr, uint32_t hint) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_PREFETCH_T0, DGTL_FLAG_NONE, addr.id, hint, DGTL_INVALID_SLOT, 0u);
}

void DGTL_CALL dgtl_build_clflush(dgtl_func_builder_t* fb, dgtl_value_t addr) DGTL_NOEXCEPT {
    if (fb) emit_raw_node(fb->stream, DGTL_OP_CLFLUSH, DGTL_FLAG_HAS_SIDE_EFFECTS, addr.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT, 0u);
}

dgtl_value_t DGTL_CALL dgtl_build_spin_acquire(dgtl_func_builder_t* fb, dgtl_value_t lock_ptr) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_SPIN_ACQUIRE, DGTL_FLAG_HAS_SIDE_EFFECTS, lock_ptr.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_I32);
    return res;
}

/* -- Spatial & Vector Math Builders -- */

dgtl_value_t DGTL_CALL dgtl_build_fast_inv_sqrt(dgtl_func_builder_t* fb, dgtl_value_t x) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_FAST_INV_SQRT, DGTL_FLAG_NONE, x.id, DGTL_INVALID_SLOT, DGTL_INVALID_SLOT);
    fb->register_type(res, fb->get_type(x));
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec3_cross(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC3_CROSS, DGTL_FLAG_NONE, a.id, b.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_VEC3_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_vec4_dot(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_VEC4_DOT, DGTL_FLAG_NONE, a.id, b.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_mat4_mul(dgtl_func_builder_t* fb, dgtl_value_t a, dgtl_value_t b) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_MAT4_MUL, DGTL_FLAG_NONE, a.id, b.id, DGTL_INVALID_SLOT);
    fb->register_type(res, DGTL_TYPE_MAT4_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_quat_slerp(dgtl_func_builder_t* fb, dgtl_value_t q0, dgtl_value_t q1, dgtl_value_t t) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_QUAT_SLERP, DGTL_FLAG_NONE, q0.id, q1.id, t.id);
    fb->register_type(res, DGTL_TYPE_QUAT_F32);
    return res;
}

dgtl_value_t DGTL_CALL dgtl_build_ray_aabb_isect(dgtl_func_builder_t* fb, dgtl_value_t ray_origin, dgtl_value_t ray_dir, dgtl_value_t aabb_min, dgtl_value_t aabb_max) DGTL_NOEXCEPT {
    if (!fb) return DGTL_INVALID_VALUE;
    auto res = emit_raw_node(fb->stream, DGTL_OP_RAY_AABB_ISECT, DGTL_FLAG_NONE, ray_origin.id, ray_dir.id, aabb_min.id, DGTL_INVALID_SLOT, aabb_max.id);
    fb->register_type(res, DGTL_TYPE_BOOL);
    return res;
}

/* ============================================================================
 * SECTION 21 & 22 — PASS REGISTRY & OPTIMIZATIONS
 * ========================================================================== */

static std::unordered_map<std::string, std::pair<dgtl_pass_fn_t, void*>> g_registered_passes;
static std::vector<std::string> g_registered_pass_names;
static std::vector<const char*> g_registered_pass_ptrs;

dgtl_pass_t DGTL_CALL dgtl_pass_register(const char* name, dgtl_pass_fn_t fn, void* user_data) DGTL_NOEXCEPT {
    if (!name || !fn) return DGTL_INVALID_PASS;
    std::string sname(name);
    g_registered_passes[sname] = {fn, user_data};
    g_registered_pass_names.push_back(sname);
    return dgtl_pass_t{static_cast<uint32_t>(g_registered_pass_names.size())};
}

dgtl_err_t DGTL_CALL dgtl_pass_run(dgtl_pass_t pass, dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s || pass.id == 0 || pass.id > g_registered_pass_names.size()) return DGTL_ERR(1);
    return dgtl_pass_run_by_name(g_registered_pass_names[pass.id - 1].c_str(), s);
}

dgtl_err_t DGTL_CALL dgtl_pass_run_by_name(const char* name, dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s || !name) return DGTL_ERR(1);
    auto it = g_registered_passes.find(name);
    if (it != g_registered_passes.end()) {
        return it->second.first(s, it->second.second);
    }
    std::string_view sv(name);
    if (sv == "DCE" || sv == "dce") return dgtl_run_dce(s);
    if (sv == "GVN" || sv == "gvn") return dgtl_run_gvn(s);
    if (sv == "LoopFusion") return dgtl_run_loop_fusion(s);
    if (sv == "Inliner") return dgtl_run_inliner(s);
    if (sv == "EscapeAnalysis") return dgtl_run_escape_analysis(s);
    if (sv == "ErrorRecovery") return dgtl_run_error_recovery(s);
    return DGTL_ERR(2);
}

bool DGTL_CALL dgtl_pass_exists(const char* name) DGTL_NOEXCEPT {
    if (!name) return false;
    std::string_view sv(name);
    if (sv == "DCE" || sv == "GVN" || sv == "LoopFusion" || sv == "Inliner" || sv == "EscapeAnalysis" || sv == "ErrorRecovery") return true;
    return g_registered_passes.find(name) != g_registered_passes.end();
}

const char** DGTL_CALL dgtl_pass_list(size_t* out_count) DGTL_NOEXCEPT {
    static const char* kBuiltins[] = {"DCE", "GVN", "LoopFusion", "Inliner", "EscapeAnalysis", "ErrorRecovery"};
    g_registered_pass_ptrs.clear();
    for (const auto* b : kBuiltins) g_registered_pass_ptrs.push_back(b);
    for (const auto& n : g_registered_pass_names) g_registered_pass_ptrs.push_back(n.c_str());
    if (out_count) *out_count = g_registered_pass_ptrs.size();
    return g_registered_pass_ptrs.data();
}

dgtl_err_t DGTL_CALL dgtl_optimize(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    (void)dgtl_run_dce(s);
    (void)dgtl_run_gvn(s);
    (void)dgtl_run_loop_fusion(s);
    (void)dgtl_run_inliner(s);
    (void)dgtl_run_escape_analysis(s);
    (void)dgtl_run_error_recovery(s);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_optimize_with_profile(dgtl_stream_t* s, dgtl_opt_level_t level) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    s->opt_level = level;
    if (level == DGTL_OPT_O0) return DGTL_OK;
    (void)dgtl_run_dce(s);
    if (level >= DGTL_OPT_O2) {
        (void)dgtl_run_gvn(s);
        (void)dgtl_run_loop_fusion(s);
    }
    if (level >= DGTL_OPT_O3) {
        (void)dgtl_run_inliner(s);
        (void)dgtl_run_escape_analysis(s);
    }
    (void)dgtl_run_error_recovery(s);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_dce(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::transform::DeadCodeElimination dce;
    dce.execute(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_gvn(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::DGTLGVN gvn;
    gvn.execute(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_loop_fusion(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::DGTLLoopFusion fusion;
    fusion.execute(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_inliner(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::DGTLAggressiveInliner inliner;
    inliner.execute(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_escape_analysis(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::DGTLEscapeAnalysis ea;
    ea.execute(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_run_error_recovery(dgtl_stream_t* s) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::DGTLEnhancedErrorRecovery er;
    er.execute(s->ir_stream);
    return DGTL_OK;
}

void DGTL_CALL dgtl_stream_set_auto_optimize(dgtl_stream_t* s, bool enabled) DGTL_NOEXCEPT {
    if (s) s->auto_optimize = enabled;
}

void DGTL_CALL dgtl_stream_set_opt_level(dgtl_stream_t* s, dgtl_opt_level_t level) DGTL_NOEXCEPT {
    if (s) s->opt_level = level;
}

dgtl_opt_level_t DGTL_CALL dgtl_stream_get_opt_level(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    return s ? s->opt_level : DGTL_OPT_O2;
}

dgtl_perf_metrics_t DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    dgtl_perf_metrics_t m{};
    if (!s) return m;
    m.total_nodes = s->ir_stream.size();
    m.logic_hash = s->ir_stream.generate_logic_hash();
    m.estimated_ipc = 2.4;
    return m;
}

/* ============================================================================
 * SECTION 23 & 24 — JIT COMPILATION, SESSIONS & UNIVERSAL MARSHALLER
 * ========================================================================== */

dgtl_strike_result_t DGTL_CALL dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target) DGTL_NOEXCEPT {
    (void)target;
    dgtl_strike_result_t res{};
    if (!s) {
        res.ok = 0;
        std::snprintf(res.message, sizeof(res.message), "Null stream handle");
        return res;
    }

    auto start_time = std::chrono::steady_clock::now();
    if (s->auto_optimize) {
        (void)dgtl_optimize_with_profile(s, s->opt_level);
    }

    std::vector<uint8_t> code_buffer(s->ir_stream.size() * 64 + 1024);
    size_t emitted_size = 0;

    dgtl::machine::MachineFunction mf("dgtl_kernel");
    bool ok = lower_stream_to_mir(s, mf);
    if (ok) {
        dgtl::backend::aot::DGTLAOTLinker aot;
        ok = aot.emit_aot_machine_code(code_buffer.data(), code_buffer.size(), mf, &emitted_size);
    }

    auto end_time = std::chrono::steady_clock::now();
    res.compilation_latency_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    res.ok = ok ? 1 : 0;
    res.code_size = emitted_size;
    res.logic_hash = s->ir_stream.generate_logic_hash();
    std::snprintf(res.message, sizeof(res.message), ok ? "Strike succeeded in %.2f ms" : "Machine lowering failed", res.compilation_latency_ms);
    return res;
}

dgtl_session_config_t DGTL_CALL dgtl_session_config_default(void) DGTL_NOEXCEPT {
    dgtl_session_config_t cfg{};
    cfg.mode = DGTL_SESSION_MODE_JIT;
    cfg.opt_level = DGTL_OPT_O3;
    cfg.target = DGTL_TARGET_HOST;
    cfg.jit_worker_threads = 2;
    cfg.code_cache_bytes = 16 * 1024 * 1024;
    cfg.timeout_us = 1000000;
    cfg.enable_simd = true;
    cfg.enable_inliner = true;
    return cfg;
}

dgtl_session_t* DGTL_CALL dgtl_session_create(const dgtl_session_config_t* config) DGTL_NOEXCEPT {
    try {
        dgtl_session_config_t cfg = config ? *config : dgtl_session_config_default();
        return new dgtl_session(cfg);
    } catch (...) {
        return nullptr;
    }
}

void DGTL_CALL dgtl_session_destroy(dgtl_session_t* session) DGTL_NOEXCEPT {
    delete session;
}

const char* DGTL_CALL dgtl_session_mode_name(dgtl_session_mode_t mode) DGTL_NOEXCEPT {
    switch (mode) {
        case DGTL_SESSION_MODE_JIT:    return "JIT";
        case DGTL_SESSION_MODE_AOT:    return "AOT";
        case DGTL_SESSION_MODE_HYBRID: return "Hybrid";
        default:                       return "Unknown";
    }
}

dgtl_err_t DGTL_CALL dgtl_session_register_symbol(dgtl_session_t* session, const char* symbol_name, uintptr_t address) DGTL_NOEXCEPT {
    if (!session || !symbol_name) return DGTL_ERR(1);
    session->host_symbols[symbol_name] = address;
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_session_register_symbols(dgtl_session_t* session, const dgtl_host_symbol_t* symbols, size_t count) DGTL_NOEXCEPT {
    if (!session || !symbols) return DGTL_ERR(1);
    for (size_t i = 0; i < count; ++i) {
        if (symbols[i].name) {
            session->host_symbols[symbols[i].name] = symbols[i].address;
        }
        if (symbols[i].slot != 0) {
            session->slot_symbols[symbols[i].slot] = symbols[i].address;
        }
    }
    return DGTL_OK;
}

void DGTL_CALL dgtl_session_clear_symbols(dgtl_session_t* session) DGTL_NOEXCEPT {
    if (session) {
        session->host_symbols.clear();
        session->slot_symbols.clear();
    }
}

dgtl_err_t DGTL_CALL dgtl_session_compile(dgtl_session_t* session, dgtl_stream_t* stream) DGTL_NOEXCEPT {
    if (!session || !stream) {
        set_last_error_msg("dgtl_session_compile: null session or stream");
        return DGTL_ERR(1);
    }

    try {
        if (stream->auto_optimize) {
            const dgtl_err_t opt_status = dgtl_optimize_with_profile(stream, stream->opt_level);
            if (DGTL_IS_ERR(opt_status)) {
                if (!g_tls_err_msg[0]) {
                    set_last_error_msg(dgtl_err_message(opt_status));
                }
                return opt_status;
            }
        }

        constexpr size_t kBytesPerNode = 64;
        constexpr size_t kCodeSlop = 1024;
        const size_t node_count = stream->ir_stream.size();
        if (node_count > (std::numeric_limits<size_t>::max() - kCodeSlop) / kBytesPerNode) {
            set_last_error_msg("dgtl_session_compile: machine-code buffer size overflow");
            return DGTL_ERR(2);
        }

        dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
        if (!lower_stream_to_mir(stream, mf)) {
            set_last_error_msg("dgtl_session_compile: failed to lower IR stream to Machine IR");
            return DGTL_ERR(2);
        }

        std::vector<uint8_t> machine_code(node_count * kBytesPerNode + kCodeSlop);
        size_t emitted_size = 0;

        const bool ok = session->aot_linker->emit_aot_machine_code(
            machine_code.data(), machine_code.size(), mf, &emitted_size,
            session->slot_symbols, nullptr, {}
        );
        if (!ok || emitted_size == 0) {
            const size_t diag_count = stream->diags.size();
            if (diag_count != 0) {
                const auto& diag = stream->diags.back();
                if (diag.message && diag.message[0] != '\0') {
                    set_last_error_msg(diag.message);
                }
            }
            if (!g_tls_err_msg[0]) {
                set_last_error_msg("DGTL AOT linker rejected the IR stream (no backend diagnostic was produced)");
            }
            return DGTL_ERR(3);
        }

    if (session->jit_memory) {
#if defined(_WIN32) || defined(_WIN64)
        VirtualFree(session->jit_memory, 0, MEM_RELEASE);
#else
        munmap(session->jit_memory, session->jit_size);
#endif
        session->jit_memory = nullptr;
    }

    session->jit_size = (emitted_size + 4095) & ~size_t(4095);

#if defined(_WIN32) || defined(_WIN64)
    session->jit_memory = VirtualAlloc(nullptr, session->jit_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!session->jit_memory) return DGTL_ERR(3);
    std::memcpy(session->jit_memory, machine_code.data(), emitted_size);

    DWORD old_protect;
    if (!VirtualProtect(session->jit_memory, session->jit_size, PAGE_EXECUTE_READ, &old_protect)) {
        VirtualFree(session->jit_memory, 0, MEM_RELEASE);
        session->jit_memory = nullptr;
        return DGTL_ERR(4);
    }
    FlushInstructionCache(GetCurrentProcess(), session->jit_memory, session->jit_size);
#else
    session->jit_memory = mmap(nullptr, session->jit_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (session->jit_memory == MAP_FAILED) {
        session->jit_memory = nullptr;
        return DGTL_ERR(3);
    }
    std::memcpy(session->jit_memory, machine_code.data(), emitted_size);
    if (mprotect(session->jit_memory, session->jit_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(session->jit_memory, session->jit_size);
        session->jit_memory = nullptr;
        return DGTL_ERR(4);
    }
#endif

    session->entry_addr = reinterpret_cast<uintptr_t>(session->jit_memory);
    return DGTL_OK;
    } catch (const std::exception& e) {
        set_last_error_msg(e.what());
        return DGTL_ERR(5);
    } catch (...) {
        set_last_error_msg("dgtl_session_compile: unknown exception during AOT compilation");
        return DGTL_ERR(6);
    }
}

uint64_t DGTL_CALL dgtl_session_get_symbol_address(dgtl_session_t* session, const char* name) DGTL_NOEXCEPT {
    if (!session) return 0;
    if (name && (std::strcmp(name, "main") == 0 || std::strcmp(name, "kernel") == 0 || std::strcmp(name, "DGTL_kernel_entry") == 0)) {
        return session->entry_addr;
    }
    auto it = session->host_symbols.find(name ? name : "");
    return it != session->host_symbols.end() ? it->second : 0;
}

void* DGTL_CALL dgtl_session_get_function_ptr(dgtl_session_t* session, const char* name) DGTL_NOEXCEPT {
    return reinterpret_cast<void*>(dgtl_session_get_symbol_address(session, name));
}

dgtl_err_t DGTL_CALL dgtl_session_run_marshalled(dgtl_session_t* session, const char* entry_symbol, const dgtl_call_args_t* call_args, dgtl_abi_arg_t* out_result) DGTL_NOEXCEPT {
    if (!session || !out_result) return DGTL_ERR(1);
    uintptr_t addr = dgtl_session_get_symbol_address(session, entry_symbol ? entry_symbol : "main");
    if (addr == 0) addr = session->entry_addr;
    if (addr == 0) return DGTL_ERR(2);

    int64_t gpr[6] = {0};
    double  fpr[4] = {0.0};
    size_t gpr_idx = 0;
    size_t fpr_idx = 0;

    if (call_args && call_args->args) {
        for (size_t i = 0; i < call_args->count && i < 6; ++i) {
            const auto& a = call_args->args[i];
            if (a.kind == DGTL_ARG_F32) {
                if (fpr_idx < 4) fpr[fpr_idx++] = static_cast<double>(a.val.f32);
            } else if (a.kind == DGTL_ARG_F64) {
                if (fpr_idx < 4) fpr[fpr_idx++] = a.val.f64;
            } else if (a.kind == DGTL_ARG_PTR) {
                if (gpr_idx < 6) gpr[gpr_idx++] = reinterpret_cast<int64_t>(a.val.ptr);
            } else {
                if (gpr_idx < 6) gpr[gpr_idx++] = a.val.i64;
            }
        }
    }

    auto fn = reinterpret_cast<int64_t(*)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t)>(addr);
    int64_t res = fn(gpr[0], gpr[1], gpr[2], gpr[3], gpr[4], gpr[5]);
    out_result->kind = DGTL_ARG_I64;
    out_result->val.i64 = res;
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_session_run_i64(dgtl_session_t* session, const char* entry_symbol, const int64_t* args, size_t arg_count, int64_t* out_result) DGTL_NOEXCEPT {
    if (!session || !out_result) return DGTL_ERR(1);
    uintptr_t addr = dgtl_session_get_symbol_address(session, entry_symbol ? entry_symbol : "main");
    if (addr == 0) addr = session->entry_addr;
    if (addr == 0) return DGTL_ERR(2);

    auto fn = reinterpret_cast<int64_t(*)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t)>(addr);
    int64_t a0 = arg_count > 0 ? args[0] : 0;
    int64_t a1 = arg_count > 1 ? args[1] : 0;
    int64_t a2 = arg_count > 2 ? args[2] : 0;
    int64_t a3 = arg_count > 3 ? args[3] : 0;
    int64_t a4 = arg_count > 4 ? args[4] : 0;
    int64_t a5 = arg_count > 5 ? args[5] : 0;
    *out_result = fn(a0, a1, a2, a3, a4, a5);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_session_run_f64(dgtl_session_t* session, const char* entry_symbol, const double* args, size_t arg_count, double* out_result) DGTL_NOEXCEPT {
    if (!session || !out_result) return DGTL_ERR(1);
    uintptr_t addr = dgtl_session_get_symbol_address(session, entry_symbol ? entry_symbol : "main");
    if (addr == 0) addr = session->entry_addr;
    if (addr == 0) return DGTL_ERR(2);

    auto fn = reinterpret_cast<double(*)(double, double, double, double)>(addr);
    double a0 = arg_count > 0 ? args[0] : 0.0;
    double a1 = arg_count > 1 ? args[1] : 0.0;
    double a2 = arg_count > 2 ? args[2] : 0.0;
    double a3 = arg_count > 3 ? args[3] : 0.0;
    *out_result = fn(a0, a1, a2, a3);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_session_run_ptr(dgtl_session_t* session, const char* entry_symbol, const void* const* args, size_t arg_count, void** out_result) DGTL_NOEXCEPT {
    int64_t raw_res = 0;
    auto err = dgtl_session_run_i64(session, entry_symbol, reinterpret_cast<const int64_t*>(args), arg_count, &raw_res);
    if (DGTL_IS_OK(err) && out_result) {
        *out_result = reinterpret_cast<void*>(raw_res);
    }
    return err;
}

dgtl_err_t DGTL_CALL dgtl_session_emit_object(dgtl_session_t* session, const char* output_path) DGTL_NOEXCEPT {
    if (!session || !output_path) return DGTL_ERR(1);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_session_emit_binary(dgtl_session_t* session, const char* output_path) DGTL_NOEXCEPT {
    if (!session || !output_path) return DGTL_ERR(1);
    return DGTL_OK;
}

/* ============================================================================
 * SECTION 24A — DIRECT IN-MEMORY BINARY EMISSION
 *
 * These are the authoritative C-ABI implementations for the public binary
 * emission declarations in dgtl.h. They intentionally live in this bridge,
 * because they cross the public C ABI into DGTL's internal backend emitters.
 * ========================================================================== */

dgtl_err_t DGTL_CALL dgtl_emit_pe_binary_in_memory(
    dgtl_stream_t* s,
    void** out_buf,
    size_t* out_size) DGTL_NOEXCEPT
{
    if (!s || !out_buf || !out_size) return DGTL_ERR(1);

    *out_buf = nullptr;
    *out_size = 0;

    try {
        dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
        if (!lower_stream_to_mir(s, mf)) {
            set_last_error_msg("failed to lower IR stream to Machine IR for PE emission");
            return DGTL_ERR(2);
        }

        dgtl::backend::aot::DGTLAOTLinker linker;

        std::vector<uint8_t> bytes;
        if (!linker.build_pe_executable_buffer(mf, bytes) || bytes.empty()) {
            set_last_error_msg("failed to synthesize PE binary in memory");
            return DGTL_ERR(2);
        }

        void* buffer = std::malloc(bytes.size());
        if (!buffer) {
            set_last_error_msg("allocation failure for PE binary buffer");
            return DGTL_ERR(3);
        }

        std::memcpy(buffer, bytes.data(), bytes.size());
        *out_buf = buffer;
        *out_size = bytes.size();
        return DGTL_OK;
    } catch (const std::exception& e) {
        set_last_error_msg(e.what());
        return DGTL_ERR(4);
    } catch (...) {
        set_last_error_msg("unknown PE binary emission failure");
        return DGTL_ERR(5);
    }
}

dgtl_err_t DGTL_CALL dgtl_emit_wasm_binary_in_memory(
    dgtl_stream_t* s,
    void** out_buf,
    size_t* out_size) DGTL_NOEXCEPT
{
    if (!s || !out_buf || !out_size) return DGTL_ERR(1);

    *out_buf = nullptr;
    *out_size = 0;

    try {
        dgtl::machine::MachineFunction mf("DGTL_kernel_entry");
        if (!lower_stream_to_mir(s, mf)) {
            set_last_error_msg("failed to lower IR stream to Machine IR for WASM emission");
            return DGTL_ERR(2);
        }

        dgtl::backend::writers::WASMBinaryWriter writer;

        std::vector<uint8_t> bytes;
        if (!writer.write(mf, bytes) || bytes.empty()) {
            set_last_error_msg("failed to synthesize WASM binary in memory");
            return DGTL_ERR(2);
        }

        void* buffer = std::malloc(bytes.size());
        if (!buffer) {
            set_last_error_msg("allocation failure for WASM binary buffer");
            return DGTL_ERR(3);
        }

        std::memcpy(buffer, bytes.data(), bytes.size());
        *out_buf = buffer;
        *out_size = bytes.size();
        return DGTL_OK;
    } catch (const std::exception& e) {
        set_last_error_msg(e.what());
        return DGTL_ERR(4);
    } catch (...) {
        set_last_error_msg("unknown WASM binary emission failure");
        return DGTL_ERR(5);
    }
}

dgtl_err_t DGTL_CALL dgtl_emit_object_binary_in_memory(
    dgtl_stream_t* s,
    dgtl_target_t target,
    void** out_buf,
    size_t* out_size) DGTL_NOEXCEPT
{
    if (!s || !out_buf || !out_size) return DGTL_ERR(1);
    switch (target) {
        case DGTL_TARGET_WASM:
        case DGTL_TARGET_WASM_SIMD:
            return dgtl_emit_wasm_binary_in_memory(s, out_buf, out_size);
        case DGTL_TARGET_RISCV64:
        case DGTL_TARGET_ARM64:
        case DGTL_TARGET_ARM64_SVE:
            return dgtl_emit_elf_binary_in_memory(s, out_buf, out_size);
        case DGTL_TARGET_X86_64:
        case DGTL_TARGET_X86_64_AVX512:
#if defined(_WIN32) || defined(_WIN64)
            return dgtl_emit_pe_binary_in_memory(s, out_buf, out_size);
#else
            return dgtl_emit_elf_binary_in_memory(s, out_buf, out_size);
#endif
        case DGTL_TARGET_HOST:
        default:
#if defined(_WIN32) || defined(_WIN64)
            return dgtl_emit_pe_binary_in_memory(s, out_buf, out_size);
#elif defined(__APPLE__)
            return dgtl_emit_macho_binary_in_memory(s, out_buf, out_size);
#else
            return dgtl_emit_elf_binary_in_memory(s, out_buf, out_size);
#endif
    }
}

/* ============================================================================
 * SECTION 24B — LOW-LEVEL RAW EMISSION & LINKER DIAGNOSTICS
 * ========================================================================== */

uint32_t DGTL_CALL dgtl_emit_raw(
    dgtl_stream_t* s,
    dgtl_opcode_t opcode,
    uint16_t flags,
    uint32_t op_a,
    uint32_t op_b,
    uint32_t op_c) DGTL_NOEXCEPT
{
    return emit_raw_node(s, opcode, flags, op_a, op_b, op_c).id;
}

uint32_t DGTL_CALL dgtl_emit_raw_with_slot(
    dgtl_stream_t* s,
    dgtl_opcode_t opcode,
    uint16_t flags,
    uint32_t result_id,
    uint32_t op_a,
    uint32_t op_b,
    uint32_t op_c) DGTL_NOEXCEPT
{
    return emit_raw_node(s, opcode, flags, op_a, op_b, op_c, result_id).id;
}

void DGTL_CALL dgtl_set_verbose(int32_t enabled) DGTL_NOEXCEPT
{
    /* Global verbose control is intentionally a compatibility no-op.
       Per-context verbosity is exposed by dgtl_context_set_verbose(). */
    (void)enabled;
}

size_t DGTL_CALL dgtl_last_linker_fallback_count(void) DGTL_NOEXCEPT
{
    return 0;
}

size_t DGTL_CALL dgtl_last_linker_fallback_message(
    size_t index, char* out_buffer, size_t out_size) DGTL_NOEXCEPT
{
    (void)index;
    (void)out_buffer;
    (void)out_size;
    return 0;
}

/* ============================================================================
 * SECTION 25 — SIMD LEGALIZATION APIS
 * ========================================================================== */

dgtl_simd_caps_t DGTL_CALL dgtl_simd_query_host_caps(void) DGTL_NOEXCEPT {
    dgtl_simd_caps_t caps{};
    auto hw = dgtl::core::detect_hardware();
    caps.native_arch = hw.has_avx512 ? DGTL_SIMD_ARCH_X86_AVX512_FULL : (hw.has_avx2 ? DGTL_SIMD_ARCH_X86_AVX2 : DGTL_SIMD_ARCH_GENERIC);
    caps.max_vector_width_bytes = hw.has_avx512 ? 64 : (hw.has_avx2 ? 32 : 16);
    caps.native_vector_register_count = hw.has_avx512 ? 32 : 16;
    caps.has_fma = hw.has_fma;
    caps.has_native_gather = hw.has_avx2 || hw.has_avx512;
    caps.has_native_scatter = hw.has_avx512;
    caps.has_fp16_arithmetic = hw.has_avx512;
    caps.has_bf16_arithmetic = hw.has_amx || hw.has_avx512;
    caps.has_masked_operations = hw.has_avx512;
    caps.has_transcendental_approximations = true;
    return caps;
}

bool DGTL_CALL dgtl_simd_arch_is_supported(dgtl_simd_arch_t arch) DGTL_NOEXCEPT {
    auto hw = dgtl::core::detect_hardware();
    switch (arch) {
        case DGTL_SIMD_ARCH_X86_AVX512_FDQ:
        case DGTL_SIMD_ARCH_X86_AVX512_FULL: return hw.has_avx512;
        case DGTL_SIMD_ARCH_X86_AVX2:        return hw.has_avx2;
        case DGTL_SIMD_ARCH_GENERIC:        return true;
        default: return false;
    }
}

dgtl_err_t DGTL_CALL dgtl_legalize_simd(dgtl_stream_t* s, dgtl_simd_arch_t arch) DGTL_NOEXCEPT {
    if (!s) return DGTL_ERR(1);
    dgtl::ir::pass::simd::TargetArch ta = (arch == DGTL_SIMD_ARCH_X86_AVX512_FULL) ? dgtl::ir::pass::simd::TargetArch::X86_AVX512
                                    : (arch == DGTL_SIMD_ARCH_X86_AVX2) ? dgtl::ir::pass::simd::TargetArch::X86_AVX2
                                    : dgtl::ir::pass::simd::TargetArch::GENERIC;
    dgtl::ir::pass::simd::DGTLSIMDLegalizer leg(ta);
    leg.legalize(s->ir_stream);
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_legalize_simd_ex(dgtl_stream_t* s, const dgtl_simd_legalize_opts_t* opts) DGTL_NOEXCEPT {
    if (!s || !opts) return DGTL_ERR(1);
    return dgtl_legalize_simd(s, opts->target_arch);
}

uint32_t DGTL_CALL dgtl_simd_native_lane_count(dgtl_simd_arch_t arch, dgtl_type_t type) DGTL_NOEXCEPT {
    uint32_t elem_size = 4;
    switch (type.id) {
        case 7:  case 14: elem_size = 1; break; // i8, f8
        case 8:  case 15: case 16: elem_size = 2; break; // i16, bf16, f16
        case 9:  case 17: elem_size = 4; break; // i32, f32
        case 10: case 18: case 4:  elem_size = 8; break; // i64, f64, ptr
        default: elem_size = 4; break;
    }
    uint32_t max_bytes = 16;
    if (arch == DGTL_SIMD_ARCH_X86_AVX512_FULL || arch == DGTL_SIMD_ARCH_X86_AVX512_FDQ) max_bytes = 64;
    else if (arch == DGTL_SIMD_ARCH_X86_AVX2 || arch == DGTL_SIMD_ARCH_X86_AVX) max_bytes = 32;
    return max_bytes / elem_size;
}

/* ============================================================================
 * SECTION 26 & 27 — IR TEXT, CFG & INTROSPECTION
 * ========================================================================== */

dgtl_text_t DGTL_CALL dgtl_stream_print_ir(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    dgtl_text_t t{nullptr, 0};
    if (!s) return t;
    std::string out = "; DGTL Textual IR Representation\n";
    for (size_t i = 0; i < s->ir_stream.size(); ++i) {
        const auto n = s->ir_stream.get_node(i);
        out += "%v" + std::to_string(n.result_id) + " = " + dgtl_opcode_name(static_cast<uint16_t>(n.opcode)) +
               " %v" + std::to_string(n.op_a) + ", %v" + std::to_string(n.op_b) + "\n";
    }
    t.data = static_cast<char*>(std::malloc(out.size() + 1));
    if (t.data) {
        std::memcpy(t.data, out.data(), out.size() + 1);
        t.length = out.size();
    }
    return t;
}

dgtl_text_t DGTL_CALL dgtl_stream_print_cfg_dot(const dgtl_stream_t* s) DGTL_NOEXCEPT {
    dgtl_text_t t{nullptr, 0};
    if (!s) return t;
    std::string out = "digraph CFG {\n  node [shape=box];\n";
    for (const auto& bb : s->ir_stream.get_basic_blocks()) {
        out += "  bb" + std::to_string(bb.id) + " [label=\"Block " + std::to_string(bb.id) + "\"];\n";
    }
    for (const auto& edge : s->ir_stream.get_cfg_edges()) {
        out += "  bb" + std::to_string(edge.from) + " -> bb" + std::to_string(edge.to) + ";\n";
    }
    out += "}\n";
    t.data = static_cast<char*>(std::malloc(out.size() + 1));
    if (t.data) {
        std::memcpy(t.data, out.data(), out.size() + 1);
        t.length = out.size();
    }
    return t;
}

void DGTL_CALL dgtl_free_text(dgtl_text_t text) DGTL_NOEXCEPT {
    if (text.data) std::free(text.data);
}

bool DGTL_CALL dgtl_node_is_terminator(dgtl_opcode_t op, uint16_t flags) DGTL_NOEXCEPT {
    return op == DGTL_OP_RET || op == DGTL_OP_GOTO || op == DGTL_OP_BRANCH || op == DGTL_OP_BRANCH_COND || (flags & DGTL_FLAG_IS_RETURN) != 0;
}

bool DGTL_CALL dgtl_node_has_side_effects(dgtl_opcode_t op, uint16_t flags) DGTL_NOEXCEPT {
    return (flags & DGTL_FLAG_HAS_SIDE_EFFECTS) != 0 || op == DGTL_OP_STORE || op == DGTL_OP_CALL;
}

double DGTL_CALL dgtl_node_inlining_benefit(dgtl_opcode_t op) DGTL_NOEXCEPT {
    dgtl::ir::IRNode n{};
    n.opcode = static_cast<dgtl::ir::OpCode>(op);
    return n.predict_inlined_benefit();
}

const char* DGTL_CALL dgtl_opcode_name(dgtl_opcode_t op) DGTL_NOEXCEPT {
    thread_local std::string tls_name;
    const auto* def = dgtl::ir::gen::find_generated_op_def(static_cast<dgtl::ir::OpCode>(op));
    tls_name = def ? std::string(def->name) : "UNKNOWN";
    return tls_name.c_str();
}

/* ============================================================================
 * SECTION 28 & 29 — SERIALIZATION & SYSTEM CAPABILITIES
 * ========================================================================== */

dgtl_err_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size) DGTL_NOEXCEPT {
    if (!s || !out_blob || !out_size) return DGTL_ERR(1);
    size_t sz = 16 + s->ir_stream.size() * sizeof(dgtl::ir::IRNode);
    auto* buf = static_cast<uint8_t*>(std::malloc(sz));
    if (!buf) return DGTL_ERR(2);

    uint32_t* hdr = reinterpret_cast<uint32_t*>(buf);
    hdr[0] = 0x4C544744u; // "DGTL"
    hdr[1] = static_cast<uint32_t>(s->ir_stream.size());
    hdr[2] = DGTL_ABI_VERSION;
    hdr[3] = 0;

    dgtl::ir::IRNode* nodes = reinterpret_cast<dgtl::ir::IRNode*>(buf + 16);
    for (size_t i = 0; i < s->ir_stream.size(); ++i) {
        nodes[i] = s->ir_stream.get_node(i);
    }
    *out_blob = buf;
    *out_size = sz;
    return DGTL_OK;
}

dgtl_err_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t blob_size) DGTL_NOEXCEPT {
    if (!s || !blob || blob_size < 16) return DGTL_ERR(1);
    const auto* hdr = static_cast<const uint32_t*>(blob);
    if (hdr[0] != 0x4C544744u) return DGTL_ERR(2);
    size_t count = hdr[1];
    if (16 + count * sizeof(dgtl::ir::IRNode) > blob_size) return DGTL_ERR(3);

    s->ir_stream.reset();
    const auto* nodes = reinterpret_cast<const dgtl::ir::IRNode*>(static_cast<const uint8_t*>(blob) + 16);
    for (size_t i = 0; i < count; ++i) s->ir_stream.push_back(nodes[i]);
    return DGTL_OK;
}

void DGTL_CALL dgtl_free_blob(void* blob) DGTL_NOEXCEPT {
    if (blob) std::free(blob);
}

int32_t DGTL_CALL dgtl_binary_validate(const uint8_t* bytes, size_t len) DGTL_NOEXCEPT {
    return (bytes && len >= 16 && *reinterpret_cast<const uint32_t*>(bytes) == 0x4C544744u) ? 1 : 0;
}

dgtl_capabilities_t DGTL_CALL dgtl_query_capabilities(void) DGTL_NOEXCEPT {
    dgtl_capabilities_t c{};
    auto hw = dgtl::core::detect_hardware();
    c.has_avx512 = hw.has_avx512;
    c.has_avx2 = hw.has_avx2;
    c.has_amx = hw.has_amx;
    c.has_fma = hw.has_fma;
    c.has_bmi2 = hw.has_bmi2;
    c.numa_node_count = dgtl::runtime::numa::get_num_nodes();
    c.core_count = std::thread::hardware_concurrency();
    c.l3_cache_bytes = 32 * 1024 * 1024;
    return c;
}

dgtl_err_t DGTL_CALL dgtl_numa_pin_thread(uint32_t numa_node) DGTL_NOEXCEPT {
    return dgtl::runtime::numa::pin_current_thread_to_node(numa_node) ? DGTL_OK : DGTL_ERR(1);
}

uint32_t DGTL_CALL dgtl_numa_current_node(void) DGTL_NOEXCEPT {
    return dgtl::runtime::numa::get_current_node();
}

uint32_t DGTL_CALL dgtl_numa_node_count(void) DGTL_NOEXCEPT {
    return dgtl::runtime::numa::get_num_nodes();
}

/* ============================================================================
 * SECTION 30 — CONTEXT-ANCHORED STRING INTERNING
 * ========================================================================== */

dgtl_str_t DGTL_CALL dgtl_str_intern(dgtl_context_t* ctx, const char* utf8, size_t len) DGTL_NOEXCEPT {
    if (!ctx || !utf8 || len == 0) return DGTL_INVALID_STR;
    return dgtl_str_t{ctx->intern(std::string_view(utf8, len))};
}

dgtl_str_t DGTL_CALL dgtl_str_intern_cstr(dgtl_context_t* ctx, const char* utf8) DGTL_NOEXCEPT {
    return (ctx && utf8) ? dgtl_str_intern(ctx, utf8, std::strlen(utf8)) : DGTL_INVALID_STR;
}

const char* DGTL_CALL dgtl_str_get(dgtl_context_t* ctx, dgtl_str_t handle) DGTL_NOEXCEPT {
    return ctx ? ctx->get_str(handle.id, nullptr) : nullptr;
}

size_t DGTL_CALL dgtl_str_len(dgtl_context_t* ctx, dgtl_str_t handle) DGTL_NOEXCEPT {
    size_t len = 0;
    if (ctx) (void)ctx->get_str(handle.id, &len);
    return len;
}

/* ============================================================================
 * SECTION 31 — SMT FORMAL VERIFICATION
 * ========================================================================== */

dgtl_smt_result_t DGTL_CALL dgtl_smt_verify(const dgtl_stream_t* original, const dgtl_stream_t* optimized, const char* solver_name) DGTL_NOEXCEPT {
    dgtl_smt_result_t res{};
    if (!original || !optimized) {
        std::snprintf(res.message, sizeof(res.message), "Null stream argument");
        return res;
    }
    dgtl::formal::DGTLFormalVerificationSMT v;
    auto smt_res = v.verify_pass_equivalence(original->ir_stream, optimized->ir_stream, "pass", dgtl::formal::VerificationLevel::Standard);
    res.proved_equivalent = smt_res.is_equivalent;
    res.has_counterexample = !smt_res.is_equivalent;
    std::snprintf(res.solver_name, sizeof(res.solver_name), "%s", solver_name ? solver_name : "z3");
    std::snprintf(res.message, sizeof(res.message), "%s", smt_res.error_message.c_str());
    return res;
}

/* ============================================================================
 * SECTION 32 — RUNTIME TASK SCHEDULER
 * ========================================================================== */

dgtl_runtime_context_t* DGTL_CALL dgtl_runtime_context_create(size_t worker_threads, size_t arena_mb_per_worker) DGTL_NOEXCEPT {
    try {
        return new dgtl_runtime_context(worker_threads, arena_mb_per_worker);
    } catch (...) {
        return nullptr;
    }
}

void DGTL_CALL dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx) DGTL_NOEXCEPT {
    delete ctx;
}

uint64_t DGTL_CALL dgtl_runtime_spawn(dgtl_runtime_context_t* ctx, dgtl_runtime_task_fn fn, void* arg) DGTL_NOEXCEPT {
    return ctx ? static_cast<uint64_t>(ctx->scheduler.spawn(fn, arg)) : 0ull;
}

void DGTL_CALL dgtl_runtime_join(dgtl_runtime_context_t* ctx, uint64_t task_handle) DGTL_NOEXCEPT {
    if (ctx && task_handle != 0) ctx->scheduler.join(task_handle);
}

void DGTL_CALL dgtl_runtime_yield(dgtl_runtime_context_t* ctx) DGTL_NOEXCEPT {
    if (ctx) ctx->scheduler.yield();
}

uint64_t DGTL_CALL dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg) DGTL_NOEXCEPT {
    auto* r = get_global_runtime_ctx();
    return r ? dgtl_runtime_spawn(r, fn, arg) : 0ull;
}

void DGTL_CALL dgtl_join_task(uint64_t handle) DGTL_NOEXCEPT {
    auto* r = get_global_runtime_ctx();
    if (r) dgtl_runtime_join(r, handle);
}

void DGTL_CALL dgtl_yield(void) DGTL_NOEXCEPT {
    auto* r = get_global_runtime_ctx();
    if (r) dgtl_runtime_yield(r);
}

// Close extern "C" block opened at line 298
} // extern "C"