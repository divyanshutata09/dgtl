/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

#include <array>
#include <cstddef>

export module dgtl.ir_core.runtime_contract;

// Import native compiler base layouts and enum models via partition leaves
import dgtl.ir_core;
import dgtl.ir_enums;

/**
 * @brief Backend Runtime Contract
 *
 * Defines the contract between the compiled JIT stream and the host runtime.
 * Implements mapping of complex/unhandled IR opcodes (e.g. string manipulation,
 * synchronization fences, fiber yields) to specialized, pre-compiled runtime helper
 * functions. Provides the symbol table for resolving runtime fallback addresses during
 * instruction selection and machine code lowering.
 */
export namespace dgtl::isa::lowering::runtime_contract {
namespace detail {

// Evaluation framework to extract the tightest bound array size dynamically
inline constexpr size_t kMaxOpCodeRuntimeIndex = []() constexpr {
    size_t highest = 0;
    // Core foundational opcodes
    if (static_cast<size_t>(ir::OpCode::STRING_LENGTH) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_LENGTH);
    if (static_cast<size_t>(ir::OpCode::STRING_COMPARE) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_COMPARE);
    if (static_cast<size_t>(ir::OpCode::STRING_CONCAT) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_CONCAT);
    if (static_cast<size_t>(ir::OpCode::STRING_SUBSTRING) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_SUBSTRING);
    if (static_cast<size_t>(ir::OpCode::ACQUIRE_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::ACQUIRE_FENCE);
    if (static_cast<size_t>(ir::OpCode::ASYNC_AWAIT) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_AWAIT);
    if (static_cast<size_t>(ir::OpCode::ASYNC_LAUNCH) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_LAUNCH);
    if (static_cast<size_t>(ir::OpCode::ASYNC_SPAWN) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_SPAWN);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_FENCE);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_LOAD) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_LOAD);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_SNAPSHOT) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_SNAPSHOT);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_STORE) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_STORE);
    if (static_cast<size_t>(ir::OpCode::BARRIER_SYNC) > highest) highest = static_cast<size_t>(ir::OpCode::BARRIER_SYNC);
    if (static_cast<size_t>(ir::OpCode::BOOL_SELECT) > highest) highest = static_cast<size_t>(ir::OpCode::BOOL_SELECT);
    if (static_cast<size_t>(ir::OpCode::SELECT) > highest) highest = static_cast<size_t>(ir::OpCode::SELECT);
    if (static_cast<size_t>(ir::OpCode::CHAN_RECV) > highest) highest = static_cast<size_t>(ir::OpCode::CHAN_RECV);
    if (static_cast<size_t>(ir::OpCode::CHAN_SEND) > highest) highest = static_cast<size_t>(ir::OpCode::CHAN_SEND);
    if (static_cast<size_t>(ir::OpCode::DEOPT_TRIGGER) > highest) highest = static_cast<size_t>(ir::OpCode::DEOPT_TRIGGER);
    if (static_cast<size_t>(ir::OpCode::DSA_ALLOCATE) > highest) highest = static_cast<size_t>(ir::OpCode::DSA_ALLOCATE);
    if (static_cast<size_t>(ir::OpCode::DSA_DEALLOCATE) > highest) highest = static_cast<size_t>(ir::OpCode::DSA_DEALLOCATE);
    if (static_cast<size_t>(ir::OpCode::FCVTL_F16_TO_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FCVTL_F16_TO_F32);
    if (static_cast<size_t>(ir::OpCode::FCVTN_F32_TO_F16) > highest) highest = static_cast<size_t>(ir::OpCode::FCVTN_F32_TO_F16);
    if (static_cast<size_t>(ir::OpCode::FIBER_YIELD) > highest) highest = static_cast<size_t>(ir::OpCode::FIBER_YIELD);
    if (static_cast<size_t>(ir::OpCode::FMA_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FMA_F32);
    if (static_cast<size_t>(ir::OpCode::FMA_WIDEN_F16_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FMA_WIDEN_F16_F32);
    if (static_cast<size_t>(ir::OpCode::GATHER_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::GATHER_F32_512);
    if (static_cast<size_t>(ir::OpCode::GATHER_16) > highest) highest = static_cast<size_t>(ir::OpCode::GATHER_16);
    if (static_cast<size_t>(ir::OpCode::GPU_LAUNCH) > highest) highest = static_cast<size_t>(ir::OpCode::GPU_LAUNCH);
    if (static_cast<size_t>(ir::OpCode::GRAPH_BUILD) > highest) highest = static_cast<size_t>(ir::OpCode::GRAPH_BUILD);
    if (static_cast<size_t>(ir::OpCode::GRAPH_TRAVERSE) > highest) highest = static_cast<size_t>(ir::OpCode::GRAPH_TRAVERSE);
    if (static_cast<size_t>(ir::OpCode::GUARD_ALIAS) > highest) highest = static_cast<size_t>(ir::OpCode::GUARD_ALIAS);
    if (static_cast<size_t>(ir::OpCode::GUARD_VALUE) > highest) highest = static_cast<size_t>(ir::OpCode::GUARD_VALUE);
    if (static_cast<size_t>(ir::OpCode::LEA) > highest) highest = static_cast<size_t>(ir::OpCode::LEA);
    if (static_cast<size_t>(ir::OpCode::GEP) > highest) highest = static_cast<size_t>(ir::OpCode::GEP);
    if (static_cast<size_t>(ir::OpCode::PTR_TO_INT) > highest) highest = static_cast<size_t>(ir::OpCode::PTR_TO_INT);
    if (static_cast<size_t>(ir::OpCode::INT_TO_PTR) > highest) highest = static_cast<size_t>(ir::OpCode::INT_TO_PTR);
    if (static_cast<size_t>(ir::OpCode::LOCK_ACQUIRE) > highest) highest = static_cast<size_t>(ir::OpCode::LOCK_ACQUIRE);
    if (static_cast<size_t>(ir::OpCode::LOCK_RELEASE) > highest) highest = static_cast<size_t>(ir::OpCode::LOCK_RELEASE);
    if (static_cast<size_t>(ir::OpCode::MEMORY_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::MEMORY_FENCE);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_ACQUIRE) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_ACQUIRE);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_RELEASE) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_RELEASE);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_TRANSFER) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_TRANSFER);
    if (static_cast<size_t>(ir::OpCode::PHI_MULTI) > highest) highest = static_cast<size_t>(ir::OpCode::PHI_MULTI);
    if (static_cast<size_t>(ir::OpCode::PHI_NODE) > highest) highest = static_cast<size_t>(ir::OpCode::PHI_NODE);
    if (static_cast<size_t>(ir::OpCode::PREFETCH) > highest) highest = static_cast<size_t>(ir::OpCode::PREFETCH);
    if (static_cast<size_t>(ir::OpCode::PREFETCH_T0) > highest) highest = static_cast<size_t>(ir::OpCode::PREFETCH_T0);
    if (static_cast<size_t>(ir::OpCode::CLFLUSH) > highest) highest = static_cast<size_t>(ir::OpCode::CLFLUSH);
    if (static_cast<size_t>(ir::OpCode::READ_TSC) > highest) highest = static_cast<size_t>(ir::OpCode::READ_TSC);
    if (static_cast<size_t>(ir::OpCode::SPIN_ACQUIRE) > highest) highest = static_cast<size_t>(ir::OpCode::SPIN_ACQUIRE);
    if (static_cast<size_t>(ir::OpCode::RING_PUSH_FREE) > highest) highest = static_cast<size_t>(ir::OpCode::RING_PUSH_FREE);
    if (static_cast<size_t>(ir::OpCode::WASM_INVOKE) > highest) highest = static_cast<size_t>(ir::OpCode::WASM_INVOKE);
    if (static_cast<size_t>(ir::OpCode::FIBER_YIELD_LOOP) > highest) highest = static_cast<size_t>(ir::OpCode::FIBER_YIELD_LOOP);
    if (static_cast<size_t>(ir::OpCode::SUSPEND_CTX) > highest) highest = static_cast<size_t>(ir::OpCode::SUSPEND_CTX);
    if (static_cast<size_t>(ir::OpCode::RESUME_CTX) > highest) highest = static_cast<size_t>(ir::OpCode::RESUME_CTX);
    if (static_cast<size_t>(ir::OpCode::NEON_VADD_Q) > highest) highest = static_cast<size_t>(ir::OpCode::NEON_VADD_Q);
    if (static_cast<size_t>(ir::OpCode::MATMUL_BF16) > highest) highest = static_cast<size_t>(ir::OpCode::MATMUL_BF16);
    if (static_cast<size_t>(ir::OpCode::MATMUL_FP8) > highest) highest = static_cast<size_t>(ir::OpCode::MATMUL_FP8);
    if (static_cast<size_t>(ir::OpCode::SDPA_F16) > highest) highest = static_cast<size_t>(ir::OpCode::SDPA_F16);
    if (static_cast<size_t>(ir::OpCode::SOFTMAX_FAST) > highest) highest = static_cast<size_t>(ir::OpCode::SOFTMAX_FAST);
    if (static_cast<size_t>(ir::OpCode::LAYER_NORM) > highest) highest = static_cast<size_t>(ir::OpCode::LAYER_NORM);
    if (static_cast<size_t>(ir::OpCode::ROPE_EMBED) > highest) highest = static_cast<size_t>(ir::OpCode::ROPE_EMBED);
    if (static_cast<size_t>(ir::OpCode::FAST_INV_SQRT) > highest) highest = static_cast<size_t>(ir::OpCode::FAST_INV_SQRT);
    if (static_cast<size_t>(ir::OpCode::VEC3_CROSS) > highest) highest = static_cast<size_t>(ir::OpCode::VEC3_CROSS);
    if (static_cast<size_t>(ir::OpCode::VEC4_DOT) > highest) highest = static_cast<size_t>(ir::OpCode::VEC4_DOT);
    if (static_cast<size_t>(ir::OpCode::MAT4_MUL) > highest) highest = static_cast<size_t>(ir::OpCode::MAT4_MUL);
    if (static_cast<size_t>(ir::OpCode::QUAT_SLERP) > highest) highest = static_cast<size_t>(ir::OpCode::QUAT_SLERP);
    if (static_cast<size_t>(ir::OpCode::RAY_AABB_ISECT) > highest) highest = static_cast<size_t>(ir::OpCode::RAY_AABB_ISECT);
    if (static_cast<size_t>(ir::OpCode::PTR_DIFF) > highest) highest = static_cast<size_t>(ir::OpCode::PTR_DIFF);
    if (static_cast<size_t>(ir::OpCode::RELEASE_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::RELEASE_FENCE);
    if (static_cast<size_t>(ir::OpCode::SCOP_ANALYZE) > highest) highest = static_cast<size_t>(ir::OpCode::SCOP_ANALYZE);
    if (static_cast<size_t>(ir::OpCode::SELECT_MULTI) > highest) highest = static_cast<size_t>(ir::OpCode::SELECT_MULTI);
    if (static_cast<size_t>(ir::OpCode::SIMD_BSL_PREDICATE) > highest) highest = static_cast<size_t>(ir::OpCode::SIMD_BSL_PREDICATE);
    if (static_cast<size_t>(ir::OpCode::SWITCH_DISPATCH) > highest) highest = static_cast<size_t>(ir::OpCode::SWITCH_DISPATCH);
    if (static_cast<size_t>(ir::OpCode::SYNC_BARRIER) > highest) highest = static_cast<size_t>(ir::OpCode::SYNC_BARRIER);
    if (static_cast<size_t>(ir::OpCode::TENSOR_BROADCAST) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_BROADCAST);
    if (static_cast<size_t>(ir::OpCode::TENSOR_CONCAT) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_CONCAT);
    if (static_cast<size_t>(ir::OpCode::TENSOR_CONV) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_CONV);
    if (static_cast<size_t>(ir::OpCode::TENSOR_GEMM) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_GEMM);
    if (static_cast<size_t>(ir::OpCode::TENSOR_GATHER) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_GATHER);
    if (static_cast<size_t>(ir::OpCode::TENSOR_POOL) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_POOL);
    if (static_cast<size_t>(ir::OpCode::TENSOR_REDUCE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_REDUCE);
    if (static_cast<size_t>(ir::OpCode::TENSOR_RESHAPE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_RESHAPE);
    if (static_cast<size_t>(ir::OpCode::TENSOR_SCATTER_D) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_SCATTER_D);
    if (static_cast<size_t>(ir::OpCode::TENSOR_SLICE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_SLICE);
    if (static_cast<size_t>(ir::OpCode::TENSOR_SOFTMAX) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_SOFTMAX);
    if (static_cast<size_t>(ir::OpCode::TENSOR_TRANSPOSE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_TRANSPOSE);
    if (static_cast<size_t>(ir::OpCode::THREAD_SPAWN) > highest) highest = static_cast<size_t>(ir::OpCode::THREAD_SPAWN);
    if (static_cast<size_t>(ir::OpCode::TREE_BUILD) > highest) highest = static_cast<size_t>(ir::OpCode::TREE_BUILD);
    if (static_cast<size_t>(ir::OpCode::TREE_TRAVERSE) > highest) highest = static_cast<size_t>(ir::OpCode::TREE_TRAVERSE);
    if (static_cast<size_t>(ir::OpCode::TRIE_BUILD) > highest) highest = static_cast<size_t>(ir::OpCode::TRIE_BUILD);
    if (static_cast<size_t>(ir::OpCode::TRIE_INSERT) > highest) highest = static_cast<size_t>(ir::OpCode::TRIE_INSERT);
    if (static_cast<size_t>(ir::OpCode::TRIE_LOOKUP) > highest) highest = static_cast<size_t>(ir::OpCode::TRIE_LOOKUP);
    if (static_cast<size_t>(ir::OpCode::TUPLE_PACK) > highest) highest = static_cast<size_t>(ir::OpCode::TUPLE_PACK);
    if (static_cast<size_t>(ir::OpCode::TUPLE_UNPACK) > highest) highest = static_cast<size_t>(ir::OpCode::TUPLE_UNPACK);
    if (static_cast<size_t>(ir::OpCode::VEC_LOAD) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_LOAD);
    if (static_cast<size_t>(ir::OpCode::VEC_REDUCE_ADD) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_REDUCE_ADD);
    if (static_cast<size_t>(ir::OpCode::SIMD_SCATTER_INDIRECT) > highest) highest = static_cast<size_t>(ir::OpCode::SIMD_SCATTER_INDIRECT);
    if (static_cast<size_t>(ir::OpCode::SIMD_COMPLEX_FMA) > highest) highest = static_cast<size_t>(ir::OpCode::SIMD_COMPLEX_FMA);
    if (static_cast<size_t>(ir::OpCode::DGTL_FAST_FMA) > highest) highest = static_cast<size_t>(ir::OpCode::DGTL_FAST_FMA);
    if (static_cast<size_t>(ir::OpCode::VEC_SHUFFLE) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_SHUFFLE);
    if (static_cast<size_t>(ir::OpCode::VEC_STORE) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_STORE);
    if (static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512);
    if (static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512);
    if (static_cast<size_t>(ir::OpCode::STORE_V512) > highest) highest = static_cast<size_t>(ir::OpCode::STORE_V512);
    return highest;
}();

// 64-byte alignment maps directly to standard hardware cache lines
alignas(64) inline constexpr auto kRuntimeSymbolTable = []() constexpr {
    std::array<const char*, kMaxOpCodeRuntimeIndex + 1> table{};
    table.fill(nullptr);

    table[static_cast<size_t>(ir::OpCode::STRING_LENGTH)] = "__dgtl_rt_string_length";
    table[static_cast<size_t>(ir::OpCode::STRING_COMPARE)] = "__dgtl_rt_string_compare";
    table[static_cast<size_t>(ir::OpCode::STRING_CONCAT)] = "__dgtl_rt_string_concat";
    table[static_cast<size_t>(ir::OpCode::STRING_SUBSTRING)] = "__dgtl_rt_string_substring";
    table[static_cast<size_t>(ir::OpCode::ACQUIRE_FENCE)] = "__dgtl_rt_acquire_fence";
    table[static_cast<size_t>(ir::OpCode::ASYNC_AWAIT)] = "__dgtl_rt_async_await";
    table[static_cast<size_t>(ir::OpCode::ASYNC_LAUNCH)] = "__dgtl_rt_async_launch";
    table[static_cast<size_t>(ir::OpCode::ASYNC_SPAWN)] = "__dgtl_rt_async_spawn";
    table[static_cast<size_t>(ir::OpCode::ATOMIC_FENCE)] = "__dgtl_rt_atomic_fence";
    table[static_cast<size_t>(ir::OpCode::ATOMIC_LOAD)] = "__dgtl_rt_atomic_load";
    table[static_cast<size_t>(ir::OpCode::ATOMIC_SNAPSHOT)] = "__dgtl_rt_atomic_snapshot";
    table[static_cast<size_t>(ir::OpCode::ATOMIC_STORE)] = "__dgtl_rt_atomic_store";
    table[static_cast<size_t>(ir::OpCode::BARRIER_SYNC)] = "__dgtl_rt_barrier_sync";
    table[static_cast<size_t>(ir::OpCode::BOOL_SELECT)] = "__dgtl_rt_bool_select";
    table[static_cast<size_t>(ir::OpCode::SELECT)] = "__dgtl_rt_select";
    table[static_cast<size_t>(ir::OpCode::CHAN_RECV)] = "__dgtl_rt_chan_recv";
    table[static_cast<size_t>(ir::OpCode::CHAN_SEND)] = "__dgtl_rt_chan_send";
    table[static_cast<size_t>(ir::OpCode::DEOPT_TRIGGER)] = "__dgtl_rt_deopt_trigger";
    table[static_cast<size_t>(ir::OpCode::DSA_ALLOCATE)] = "__dgtl_rt_dsa_allocate";
    table[static_cast<size_t>(ir::OpCode::DSA_DEALLOCATE)] = "__dgtl_rt_dsa_deallocate";
    table[static_cast<size_t>(ir::OpCode::FCVTL_F16_TO_F32)] = "__dgtl_rt_fcvtl_f16_to_f32";
    table[static_cast<size_t>(ir::OpCode::FCVTN_F32_TO_F16)] = "__dgtl_rt_fcvtn_f32_to_f16";
    table[static_cast<size_t>(ir::OpCode::FIBER_YIELD)] = "__dgtl_rt_fiber_yield";
    table[static_cast<size_t>(ir::OpCode::FMA_F32)] = "__dgtl_rt_fma_f32";
    table[static_cast<size_t>(ir::OpCode::FMA_WIDEN_F16_F32)] = "__dgtl_rt_fma_widen_f16_f32";
    table[static_cast<size_t>(ir::OpCode::GATHER_F32_512)] = "__dgtl_rt_gather_f32_512";
    table[static_cast<size_t>(ir::OpCode::GATHER_16)] = "__dgtl_rt_gather_16";
    table[static_cast<size_t>(ir::OpCode::GPU_LAUNCH)] = "__dgtl_rt_gpu_launch";
    table[static_cast<size_t>(ir::OpCode::GRAPH_BUILD)] = "__dgtl_rt_graph_build";
    table[static_cast<size_t>(ir::OpCode::GRAPH_TRAVERSE)] = "__dgtl_rt_graph_traverse";
    table[static_cast<size_t>(ir::OpCode::GUARD_ALIAS)] = "__dgtl_rt_guard_alias";
    table[static_cast<size_t>(ir::OpCode::GUARD_VALUE)] = "__dgtl_rt_guard_value";
    table[static_cast<size_t>(ir::OpCode::LEA)] = "__dgtl_rt_lea";
    table[static_cast<size_t>(ir::OpCode::GEP)] = "__dgtl_rt_gep";
    table[static_cast<size_t>(ir::OpCode::PTR_TO_INT)] = "__dgtl_rt_ptr_to_int";
    table[static_cast<size_t>(ir::OpCode::INT_TO_PTR)] = "__dgtl_rt_int_to_ptr";
    table[static_cast<size_t>(ir::OpCode::LOCK_ACQUIRE)] = "__dgtl_rt_lock_acquire";
    table[static_cast<size_t>(ir::OpCode::LOCK_RELEASE)] = "__dgtl_rt_lock_release";
    table[static_cast<size_t>(ir::OpCode::MEMORY_FENCE)] = "__dgtl_rt_memory_fence";
    table[static_cast<size_t>(ir::OpCode::OWNERSHIP_ACQUIRE)] = "__dgtl_rt_ownership_acquire";
    table[static_cast<size_t>(ir::OpCode::OWNERSHIP_RELEASE)] = "__dgtl_rt_ownership_release";
    table[static_cast<size_t>(ir::OpCode::OWNERSHIP_TRANSFER)] = "__dgtl_rt_ownership_transfer";
    table[static_cast<size_t>(ir::OpCode::PHI_MULTI)] = "__dgtl_rt_phi_multi";
    table[static_cast<size_t>(ir::OpCode::PHI_NODE)] = "__dgtl_rt_phi_node";
    table[static_cast<size_t>(ir::OpCode::PREFETCH)] = "__dgtl_rt_prefetch";
    table[static_cast<size_t>(ir::OpCode::PREFETCH_T0)] = "__dgtl_rt_prefetch_t0";
    table[static_cast<size_t>(ir::OpCode::CLFLUSH)] = "__dgtl_rt_clflush";
    table[static_cast<size_t>(ir::OpCode::READ_TSC)] = "__dgtl_rt_read_tsc";
    table[static_cast<size_t>(ir::OpCode::SPIN_ACQUIRE)] = "__dgtl_rt_spin_acquire";
    table[static_cast<size_t>(ir::OpCode::RING_PUSH_FREE)] = "__dgtl_rt_ring_push_free";
    table[static_cast<size_t>(ir::OpCode::WASM_INVOKE)] = "__dgtl_rt_wasm_invoke";
    table[static_cast<size_t>(ir::OpCode::FIBER_YIELD_LOOP)] = "__dgtl_rt_fiber_yield_loop";
    table[static_cast<size_t>(ir::OpCode::SUSPEND_CTX)] = "__dgtl_rt_suspend_ctx";
    table[static_cast<size_t>(ir::OpCode::RESUME_CTX)] = "__dgtl_rt_resume_ctx";
    table[static_cast<size_t>(ir::OpCode::NEON_VADD_Q)] = "__dgtl_rt_neon_vadd_q";
    table[static_cast<size_t>(ir::OpCode::MATMUL_BF16)] = "__dgtl_rt_matmul_bf16";
    table[static_cast<size_t>(ir::OpCode::MATMUL_FP8)] = "__dgtl_rt_matmul_fp8";
    table[static_cast<size_t>(ir::OpCode::SDPA_F16)] = "__dgtl_rt_sdpa_f16";
    table[static_cast<size_t>(ir::OpCode::SOFTMAX_FAST)] = "__dgtl_rt_softmax_fast";
    table[static_cast<size_t>(ir::OpCode::LAYER_NORM)] = "__dgtl_rt_layer_norm";
    table[static_cast<size_t>(ir::OpCode::ROPE_EMBED)] = "__dgtl_rt_rope_embed";
    table[static_cast<size_t>(ir::OpCode::FAST_INV_SQRT)] = "__dgtl_rt_fast_inv_sqrt";
    table[static_cast<size_t>(ir::OpCode::VEC3_CROSS)] = "__dgtl_rt_vec3_cross";
    table[static_cast<size_t>(ir::OpCode::VEC4_DOT)] = "__dgtl_rt_vec4_dot";
    table[static_cast<size_t>(ir::OpCode::MAT4_MUL)] = "__dgtl_rt_mat4_mul";
    table[static_cast<size_t>(ir::OpCode::QUAT_SLERP)] = "__dgtl_rt_quat_slerp";
    table[static_cast<size_t>(ir::OpCode::RAY_AABB_ISECT)] = "__dgtl_rt_ray_aabb_isect";
    table[static_cast<size_t>(ir::OpCode::PTR_DIFF)] = "__dgtl_rt_ptr_diff";
    table[static_cast<size_t>(ir::OpCode::RELEASE_FENCE)] = "__dgtl_rt_release_fence";
    table[static_cast<size_t>(ir::OpCode::SCOP_ANALYZE)] = "__dgtl_rt_scop_analyze";
    table[static_cast<size_t>(ir::OpCode::SELECT_MULTI)] = "__dgtl_rt_select_multi";
    table[static_cast<size_t>(ir::OpCode::SIMD_BSL_PREDICATE)] = "__dgtl_rt_simd_bsl_predicate";
    table[static_cast<size_t>(ir::OpCode::SWITCH_DISPATCH)] = "__dgtl_rt_switch_dispatch";
    table[static_cast<size_t>(ir::OpCode::SYNC_BARRIER)] = "__dgtl_rt_sync_barrier";
    table[static_cast<size_t>(ir::OpCode::TENSOR_BROADCAST)] = "__dgtl_rt_tensor_broadcast";
    table[static_cast<size_t>(ir::OpCode::TENSOR_CONCAT)] = "__dgtl_rt_tensor_concat";
    table[static_cast<size_t>(ir::OpCode::TENSOR_CONV)] = "__dgtl_rt_tensor_conv";
    table[static_cast<size_t>(ir::OpCode::TENSOR_GEMM)] = "__dgtl_rt_tensor_gemm";
    table[static_cast<size_t>(ir::OpCode::TENSOR_GATHER)] = "__dgtl_rt_tensor_gather";
    table[static_cast<size_t>(ir::OpCode::TENSOR_POOL)] = "__dgtl_rt_tensor_pool";
    table[static_cast<size_t>(ir::OpCode::TENSOR_REDUCE)] = "__dgtl_rt_tensor_reduce";
    table[static_cast<size_t>(ir::OpCode::TENSOR_RESHAPE)] = "__dgtl_rt_tensor_reshape";
    table[static_cast<size_t>(ir::OpCode::TENSOR_SCATTER_D)] = "__dgtl_rt_tensor_scatter_d";
    table[static_cast<size_t>(ir::OpCode::TENSOR_SLICE)] = "__dgtl_rt_tensor_slice";
    table[static_cast<size_t>(ir::OpCode::TENSOR_SOFTMAX)] = "__dgtl_rt_tensor_softmax";
    table[static_cast<size_t>(ir::OpCode::TENSOR_TRANSPOSE)] = "__dgtl_rt_tensor_transpose";
    table[static_cast<size_t>(ir::OpCode::THREAD_SPAWN)] = "__dgtl_rt_thread_spawn";
    table[static_cast<size_t>(ir::OpCode::TREE_BUILD)] = "__dgtl_rt_tree_build";
    table[static_cast<size_t>(ir::OpCode::TREE_TRAVERSE)] = "__dgtl_rt_tree_traverse";
    table[static_cast<size_t>(ir::OpCode::TRIE_BUILD)] = "__dgtl_rt_trie_build";
    table[static_cast<size_t>(ir::OpCode::TRIE_INSERT)] = "__dgtl_rt_trie_insert";
    table[static_cast<size_t>(ir::OpCode::TRIE_LOOKUP)] = "__dgtl_rt_trie_lookup";
    table[static_cast<size_t>(ir::OpCode::TUPLE_PACK)] = "__dgtl_rt_tuple_pack";
    table[static_cast<size_t>(ir::OpCode::TUPLE_UNPACK)] = "__dgtl_rt_tuple_unpack";
    table[static_cast<size_t>(ir::OpCode::VEC_LOAD)] = "__dgtl_rt_vec_load";
    table[static_cast<size_t>(ir::OpCode::VEC_REDUCE_ADD)] = "__dgtl_rt_vec_reduce_add";
    table[static_cast<size_t>(ir::OpCode::SIMD_SCATTER_INDIRECT)] = "__dgtl_rt_simd_scatter_indirect";
    table[static_cast<size_t>(ir::OpCode::SIMD_COMPLEX_FMA)] = "__dgtl_rt_simd_complex_fma";
    table[static_cast<size_t>(ir::OpCode::DGTL_FAST_FMA)] = "__dgtl_rt_dgtl_fast_fma";
    table[static_cast<size_t>(ir::OpCode::VEC_SHUFFLE)] = "__dgtl_rt_vec_shuffle";
    table[static_cast<size_t>(ir::OpCode::VEC_STORE)] = "__dgtl_rt_vec_store";
    table[static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512)] = "__dgtl_rt_vec_add_f32_512";
    table[static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512)] = "__dgtl_rt_vec_mul_f32_512";
    table[static_cast<size_t>(ir::OpCode::STORE_V512)] = "__dgtl_rt_store_v512";

    return table;
}();
} // namespace detail

[[nodiscard]] inline const char* runtime_symbol_for_opcode(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    if (index < detail::kRuntimeSymbolTable.size()) [[likely]] {
        return detail::kRuntimeSymbolTable[index];
    }
    return nullptr;
}

[[nodiscard]] inline bool has_runtime_symbol(ir::OpCode opcode) noexcept {
    return runtime_symbol_for_opcode(opcode) != nullptr;
}

} // namespace dgtl::isa::lowering::runtime_contract