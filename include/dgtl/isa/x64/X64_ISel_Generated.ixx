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

/* Auto-generated from include/dgtl/isa/defs/x64_core.dgtl_isel. Do not edit manually. */
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

export module dgtl.ir_core.isel_gen;

import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_core.bridge;

export namespace dgtl::isa::lowering {

enum class MachineForm : uint8_t {
    Unhandled = 0,
    NoOp,
    Move,
    LoadImmediate,
    LoadStack,
    StoreStack,
    IntegerBinary32,
    IntegerBinary64,
    FloatBinary32,
    FloatBinary64,
    FloatTernaryFma32,
    Compare32,
    VectorBinary512,
    VectorGather512,
    VectorStore512,
    Call,
    Return,
    Goto,
    BranchCond,
    BranchCompare,
    Syscall,
    TensorOp,
    HardwareFence,
    AtomicOp,
    ConcurrentSync,
};

namespace generated {

enum class X64IntegerBinary32Kind : uint8_t { None = 0, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr };
enum class X64IntegerBinary64Kind : uint8_t { None = 0, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr };
enum class X64FloatBinary32Kind : uint8_t { None = 0, Add, Sub, Mul, Div };
enum class X64FloatBinary64Kind : uint8_t { None = 0, Add, Sub, Mul, Div };
enum class X64Compare32Kind : uint8_t { None = 0, Any };
enum class X64VectorBinary512Kind : uint8_t { None = 0, Add, Mul };

inline constexpr size_t kMaxOpCodeIndex = []() constexpr {
    size_t highest = 0;
    if (static_cast<size_t>(ir::OpCode::NOP) > highest) highest = static_cast<size_t>(ir::OpCode::NOP);
    if (static_cast<size_t>(ir::OpCode::LOAD) > highest) highest = static_cast<size_t>(ir::OpCode::LOAD);
    if (static_cast<size_t>(ir::OpCode::STORE) > highest) highest = static_cast<size_t>(ir::OpCode::STORE);
    if (static_cast<size_t>(ir::OpCode::REG_MOVE) > highest) highest = static_cast<size_t>(ir::OpCode::REG_MOVE);
    if (static_cast<size_t>(ir::OpCode::MOV) > highest) highest = static_cast<size_t>(ir::OpCode::MOV);
    if (static_cast<size_t>(ir::OpCode::LOAD_IMM) > highest) highest = static_cast<size_t>(ir::OpCode::LOAD_IMM);
    if (static_cast<size_t>(ir::OpCode::CONST) > highest) highest = static_cast<size_t>(ir::OpCode::CONST);
    if (static_cast<size_t>(ir::OpCode::ADD_I32) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_I32);
    if (static_cast<size_t>(ir::OpCode::SUB_I32) > highest) highest = static_cast<size_t>(ir::OpCode::SUB_I32);
    if (static_cast<size_t>(ir::OpCode::MUL_I32) > highest) highest = static_cast<size_t>(ir::OpCode::MUL_I32);
    if (static_cast<size_t>(ir::OpCode::DIV_I32) > highest) highest = static_cast<size_t>(ir::OpCode::DIV_I32);
    if (static_cast<size_t>(ir::OpCode::MOD_I32) > highest) highest = static_cast<size_t>(ir::OpCode::MOD_I32);
    if (static_cast<size_t>(ir::OpCode::AND_I32) > highest) highest = static_cast<size_t>(ir::OpCode::AND_I32);
    if (static_cast<size_t>(ir::OpCode::OR_I32) > highest) highest = static_cast<size_t>(ir::OpCode::OR_I32);
    if (static_cast<size_t>(ir::OpCode::XOR_I32) > highest) highest = static_cast<size_t>(ir::OpCode::XOR_I32);
    if (static_cast<size_t>(ir::OpCode::SHL_I32) > highest) highest = static_cast<size_t>(ir::OpCode::SHL_I32);
    if (static_cast<size_t>(ir::OpCode::SHR_I32) > highest) highest = static_cast<size_t>(ir::OpCode::SHR_I32);
    if (static_cast<size_t>(ir::OpCode::ADD_I64) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_I64);
    if (static_cast<size_t>(ir::OpCode::SUB_I64) > highest) highest = static_cast<size_t>(ir::OpCode::SUB_I64);
    if (static_cast<size_t>(ir::OpCode::MUL_I64) > highest) highest = static_cast<size_t>(ir::OpCode::MUL_I64);
    if (static_cast<size_t>(ir::OpCode::DIV_I64) > highest) highest = static_cast<size_t>(ir::OpCode::DIV_I64);
    if (static_cast<size_t>(ir::OpCode::MOD_I64) > highest) highest = static_cast<size_t>(ir::OpCode::MOD_I64);
    if (static_cast<size_t>(ir::OpCode::ADD_F32) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_F32);
    if (static_cast<size_t>(ir::OpCode::SUB_F32) > highest) highest = static_cast<size_t>(ir::OpCode::SUB_F32);
    if (static_cast<size_t>(ir::OpCode::MUL_F32) > highest) highest = static_cast<size_t>(ir::OpCode::MUL_F32);
    if (static_cast<size_t>(ir::OpCode::DIV_F32) > highest) highest = static_cast<size_t>(ir::OpCode::DIV_F32);
    if (static_cast<size_t>(ir::OpCode::FMA_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FMA_F32);
    if (static_cast<size_t>(ir::OpCode::ADD_F64) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_F64);
    if (static_cast<size_t>(ir::OpCode::SUB_F64) > highest) highest = static_cast<size_t>(ir::OpCode::SUB_F64);
    if (static_cast<size_t>(ir::OpCode::MUL_F64) > highest) highest = static_cast<size_t>(ir::OpCode::MUL_F64);
    if (static_cast<size_t>(ir::OpCode::DIV_F64) > highest) highest = static_cast<size_t>(ir::OpCode::DIV_F64);
    if (static_cast<size_t>(ir::OpCode::CMP_EQ_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_EQ_I32);
    if (static_cast<size_t>(ir::OpCode::CMP_NE_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_NE_I32);
    if (static_cast<size_t>(ir::OpCode::CMP_LT_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_LT_I32);
    if (static_cast<size_t>(ir::OpCode::CMP_LE_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_LE_I32);
    if (static_cast<size_t>(ir::OpCode::CMP_GT_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_GT_I32);
    if (static_cast<size_t>(ir::OpCode::CMP_GE_I32) > highest) highest = static_cast<size_t>(ir::OpCode::CMP_GE_I32);
    if (static_cast<size_t>(ir::OpCode::GATHER_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::GATHER_F32_512);
    if (static_cast<size_t>(ir::OpCode::STORE_V512) > highest) highest = static_cast<size_t>(ir::OpCode::STORE_V512);
    if (static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512);
    if (static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512);
    if (static_cast<size_t>(ir::OpCode::CALL) > highest) highest = static_cast<size_t>(ir::OpCode::CALL);
    if (static_cast<size_t>(ir::OpCode::CALL_VARIADIC) > highest) highest = static_cast<size_t>(ir::OpCode::CALL_VARIADIC);
    if (static_cast<size_t>(ir::OpCode::SELECT) > highest) highest = static_cast<size_t>(ir::OpCode::SELECT);
    if (static_cast<size_t>(ir::OpCode::STRING_LENGTH) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_LENGTH);
    if (static_cast<size_t>(ir::OpCode::STRING_COMPARE) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_COMPARE);
    if (static_cast<size_t>(ir::OpCode::STRING_CONCAT) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_CONCAT);
    if (static_cast<size_t>(ir::OpCode::STRING_SUBSTRING) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_SUBSTRING);
    if (static_cast<size_t>(ir::OpCode::LABEL) > highest) highest = static_cast<size_t>(ir::OpCode::LABEL);
    if (static_cast<size_t>(ir::OpCode::BRANCH) > highest) highest = static_cast<size_t>(ir::OpCode::BRANCH);
    if (static_cast<size_t>(ir::OpCode::RET) > highest) highest = static_cast<size_t>(ir::OpCode::RET);
    if (static_cast<size_t>(ir::OpCode::RETURN) > highest) highest = static_cast<size_t>(ir::OpCode::RETURN);
    if (static_cast<size_t>(ir::OpCode::GOTO) > highest) highest = static_cast<size_t>(ir::OpCode::GOTO);
    if (static_cast<size_t>(ir::OpCode::BRANCH_COND) > highest) highest = static_cast<size_t>(ir::OpCode::BRANCH_COND);
    if (static_cast<size_t>(ir::OpCode::BRANCH_COMPARE_FUSED) > highest) highest = static_cast<size_t>(ir::OpCode::BRANCH_COMPARE_FUSED);
    if (static_cast<size_t>(ir::OpCode::SYSCALL) > highest) highest = static_cast<size_t>(ir::OpCode::SYSCALL);
    if (static_cast<size_t>(ir::OpCode::ACQUIRE_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::ACQUIRE_FENCE);
    if (static_cast<size_t>(ir::OpCode::AND_I64) > highest) highest = static_cast<size_t>(ir::OpCode::AND_I64);
    if (static_cast<size_t>(ir::OpCode::ASSUME) > highest) highest = static_cast<size_t>(ir::OpCode::ASSUME);
    if (static_cast<size_t>(ir::OpCode::ASYNC_AWAIT) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_AWAIT);
    if (static_cast<size_t>(ir::OpCode::ASYNC_LAUNCH) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_LAUNCH);
    if (static_cast<size_t>(ir::OpCode::ASYNC_SPAWN) > highest) highest = static_cast<size_t>(ir::OpCode::ASYNC_SPAWN);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_FENCE);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_LOAD) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_LOAD);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_SNAPSHOT) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_SNAPSHOT);
    if (static_cast<size_t>(ir::OpCode::ATOMIC_STORE) > highest) highest = static_cast<size_t>(ir::OpCode::ATOMIC_STORE);
    if (static_cast<size_t>(ir::OpCode::BARRIER_SYNC) > highest) highest = static_cast<size_t>(ir::OpCode::BARRIER_SYNC);
    if (static_cast<size_t>(ir::OpCode::BOOL_SELECT) > highest) highest = static_cast<size_t>(ir::OpCode::BOOL_SELECT);
    if (static_cast<size_t>(ir::OpCode::CHAN_RECV) > highest) highest = static_cast<size_t>(ir::OpCode::CHAN_RECV);
    if (static_cast<size_t>(ir::OpCode::CHAN_SEND) > highest) highest = static_cast<size_t>(ir::OpCode::CHAN_SEND);
    if (static_cast<size_t>(ir::OpCode::DEOPT_TRIGGER) > highest) highest = static_cast<size_t>(ir::OpCode::DEOPT_TRIGGER);
    if (static_cast<size_t>(ir::OpCode::DSA_ALLOCATE) > highest) highest = static_cast<size_t>(ir::OpCode::DSA_ALLOCATE);
    if (static_cast<size_t>(ir::OpCode::DSA_DEALLOCATE) > highest) highest = static_cast<size_t>(ir::OpCode::DSA_DEALLOCATE);
    if (static_cast<size_t>(ir::OpCode::FCVTL_F16_TO_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FCVTL_F16_TO_F32);
    if (static_cast<size_t>(ir::OpCode::FCVTN_F32_TO_F16) > highest) highest = static_cast<size_t>(ir::OpCode::FCVTN_F32_TO_F16);
    if (static_cast<size_t>(ir::OpCode::FIBER_YIELD) > highest) highest = static_cast<size_t>(ir::OpCode::FIBER_YIELD);
    if (static_cast<size_t>(ir::OpCode::FMA_WIDEN_F16_F32) > highest) highest = static_cast<size_t>(ir::OpCode::FMA_WIDEN_F16_F32);
    if (static_cast<size_t>(ir::OpCode::FUSION_GROUP) > highest) highest = static_cast<size_t>(ir::OpCode::FUSION_GROUP);
    if (static_cast<size_t>(ir::OpCode::GATHER_16) > highest) highest = static_cast<size_t>(ir::OpCode::GATHER_16);
    if (static_cast<size_t>(ir::OpCode::GEP) > highest) highest = static_cast<size_t>(ir::OpCode::GEP);
    if (static_cast<size_t>(ir::OpCode::GPU_LAUNCH) > highest) highest = static_cast<size_t>(ir::OpCode::GPU_LAUNCH);
    if (static_cast<size_t>(ir::OpCode::GRAPH_BUILD) > highest) highest = static_cast<size_t>(ir::OpCode::GRAPH_BUILD);
    if (static_cast<size_t>(ir::OpCode::GRAPH_TRAVERSE) > highest) highest = static_cast<size_t>(ir::OpCode::GRAPH_TRAVERSE);
    if (static_cast<size_t>(ir::OpCode::GUARD_ALIAS) > highest) highest = static_cast<size_t>(ir::OpCode::GUARD_ALIAS);
    if (static_cast<size_t>(ir::OpCode::GUARD_VALUE) > highest) highest = static_cast<size_t>(ir::OpCode::GUARD_VALUE);
    if (static_cast<size_t>(ir::OpCode::IMMEDIATE) > highest) highest = static_cast<size_t>(ir::OpCode::IMMEDIATE);
    if (static_cast<size_t>(ir::OpCode::LEA) > highest) highest = static_cast<size_t>(ir::OpCode::LEA);
    if (static_cast<size_t>(ir::OpCode::LOCK_ACQUIRE) > highest) highest = static_cast<size_t>(ir::OpCode::LOCK_ACQUIRE);
    if (static_cast<size_t>(ir::OpCode::LOCK_RELEASE) > highest) highest = static_cast<size_t>(ir::OpCode::LOCK_RELEASE);
    if (static_cast<size_t>(ir::OpCode::MEMORY_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::MEMORY_FENCE);
    if (static_cast<size_t>(ir::OpCode::OR_I64) > highest) highest = static_cast<size_t>(ir::OpCode::OR_I64);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_ACQUIRE) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_ACQUIRE);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_RELEASE) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_RELEASE);
    if (static_cast<size_t>(ir::OpCode::OWNERSHIP_TRANSFER) > highest) highest = static_cast<size_t>(ir::OpCode::OWNERSHIP_TRANSFER);
    if (static_cast<size_t>(ir::OpCode::PHI_MULTI) > highest) highest = static_cast<size_t>(ir::OpCode::PHI_MULTI);
    if (static_cast<size_t>(ir::OpCode::PHI_NODE) > highest) highest = static_cast<size_t>(ir::OpCode::PHI_NODE);
    if (static_cast<size_t>(ir::OpCode::PREFETCH) > highest) highest = static_cast<size_t>(ir::OpCode::PREFETCH);
    if (static_cast<size_t>(ir::OpCode::PTR_CAST) > highest) highest = static_cast<size_t>(ir::OpCode::PTR_CAST);
    if (static_cast<size_t>(ir::OpCode::PTR_TO_INT) > highest) highest = static_cast<size_t>(ir::OpCode::PTR_TO_INT);
    if (static_cast<size_t>(ir::OpCode::INT_TO_PTR) > highest) highest = static_cast<size_t>(ir::OpCode::INT_TO_PTR);
    if (static_cast<size_t>(ir::OpCode::PTR_DIFF) > highest) highest = static_cast<size_t>(ir::OpCode::PTR_DIFF);
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
    if (static_cast<size_t>(ir::OpCode::REGION_BEGIN) > highest) highest = static_cast<size_t>(ir::OpCode::REGION_BEGIN);
    if (static_cast<size_t>(ir::OpCode::REGION_END) > highest) highest = static_cast<size_t>(ir::OpCode::REGION_END);
    if (static_cast<size_t>(ir::OpCode::RELEASE_FENCE) > highest) highest = static_cast<size_t>(ir::OpCode::RELEASE_FENCE);
    if (static_cast<size_t>(ir::OpCode::BRANCH_LIKELY) > highest) highest = static_cast<size_t>(ir::OpCode::BRANCH_LIKELY);
    if (static_cast<size_t>(ir::OpCode::BRANCH_UNLIKELY) > highest) highest = static_cast<size_t>(ir::OpCode::BRANCH_UNLIKELY);
    if (static_cast<size_t>(ir::OpCode::CLFLUSH) > highest) highest = static_cast<size_t>(ir::OpCode::CLFLUSH);
    if (static_cast<size_t>(ir::OpCode::PREFETCH_T0) > highest) highest = static_cast<size_t>(ir::OpCode::PREFETCH_T0);
    if (static_cast<size_t>(ir::OpCode::SCOP_ANALYZE) > highest) highest = static_cast<size_t>(ir::OpCode::SCOP_ANALYZE);
    if (static_cast<size_t>(ir::OpCode::SELECT_MULTI) > highest) highest = static_cast<size_t>(ir::OpCode::SELECT_MULTI);
    if (static_cast<size_t>(ir::OpCode::SHL_I64) > highest) highest = static_cast<size_t>(ir::OpCode::SHL_I64);
    if (static_cast<size_t>(ir::OpCode::SHR_I64) > highest) highest = static_cast<size_t>(ir::OpCode::SHR_I64);
    if (static_cast<size_t>(ir::OpCode::SIMD_BSL_PREDICATE) > highest) highest = static_cast<size_t>(ir::OpCode::SIMD_BSL_PREDICATE);
    if (static_cast<size_t>(ir::OpCode::STRING_CONST) > highest) highest = static_cast<size_t>(ir::OpCode::STRING_CONST);
    if (static_cast<size_t>(ir::OpCode::SWITCH_DISPATCH) > highest) highest = static_cast<size_t>(ir::OpCode::SWITCH_DISPATCH);
    if (static_cast<size_t>(ir::OpCode::SYNC_BARRIER) > highest) highest = static_cast<size_t>(ir::OpCode::SYNC_BARRIER);
    if (static_cast<size_t>(ir::OpCode::TENSOR_BROADCAST) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_BROADCAST);
    if (static_cast<size_t>(ir::OpCode::TENSOR_CONCAT) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_CONCAT);
    if (static_cast<size_t>(ir::OpCode::TENSOR_CONV) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_CONV);
    if (static_cast<size_t>(ir::OpCode::TENSOR_GEMM) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_GEMM);
    if (static_cast<size_t>(ir::OpCode::TENSOR_POOL) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_POOL);
    if (static_cast<size_t>(ir::OpCode::TENSOR_REDUCE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_REDUCE);
    if (static_cast<size_t>(ir::OpCode::TENSOR_RESHAPE) > highest) highest = static_cast<size_t>(ir::OpCode::TENSOR_RESHAPE);
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
    if (static_cast<size_t>(ir::OpCode::VEC_SHUFFLE) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_SHUFFLE);
    if (static_cast<size_t>(ir::OpCode::VEC_STORE) > highest) highest = static_cast<size_t>(ir::OpCode::VEC_STORE);
    if (static_cast<size_t>(ir::OpCode::VOLATILE_LOAD) > highest) highest = static_cast<size_t>(ir::OpCode::VOLATILE_LOAD);
    if (static_cast<size_t>(ir::OpCode::VOLATILE_STORE) > highest) highest = static_cast<size_t>(ir::OpCode::VOLATILE_STORE);
    if (static_cast<size_t>(ir::OpCode::XOR_I64) > highest) highest = static_cast<size_t>(ir::OpCode::XOR_I64);
    if (static_cast<size_t>(ir::OpCode::OP_ADD) > highest) highest = static_cast<size_t>(ir::OpCode::OP_ADD);
    if (static_cast<size_t>(ir::OpCode::ADD_I8) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_I8);
    if (static_cast<size_t>(ir::OpCode::ADD_I128) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_I128);
    if (static_cast<size_t>(ir::OpCode::ADD_I256) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_I256);
    if (static_cast<size_t>(ir::OpCode::ADD_F8) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_F8);
    if (static_cast<size_t>(ir::OpCode::ADD_BF16) > highest) highest = static_cast<size_t>(ir::OpCode::ADD_BF16);
    if (static_cast<size_t>(ir::OpCode::BIT_FIELD_INSERT) > highest) highest = static_cast<size_t>(ir::OpCode::BIT_FIELD_INSERT);
    if (static_cast<size_t>(ir::OpCode::BIT_FIELD_EXTRACT) > highest) highest = static_cast<size_t>(ir::OpCode::BIT_FIELD_EXTRACT);
    if (static_cast<size_t>(ir::OpCode::UNDEF) > highest) highest = static_cast<size_t>(ir::OpCode::UNDEF);
    return highest < 65536 ? highest : (65536 - 1);
}();

alignas(64) inline constexpr auto kMachineFormTable = []() constexpr {
    std::array<MachineForm, kMaxOpCodeIndex + 1> table{};
    table.fill(MachineForm::Unhandled);
    auto set = [&](ir::OpCode opcode, MachineForm form) constexpr {
        const size_t index = static_cast<size_t>(opcode);
        if (index < table.size()) table[index] = form;
    };

    set(ir::OpCode::NOP, MachineForm::NoOp);
    set(ir::OpCode::LOAD, MachineForm::Move);
    set(ir::OpCode::STORE, MachineForm::StoreStack);
    set(ir::OpCode::REG_MOVE, MachineForm::Move);
    set(ir::OpCode::MOV, MachineForm::Move);
    set(ir::OpCode::LOAD_IMM, MachineForm::LoadImmediate);
    set(ir::OpCode::CONST, MachineForm::LoadImmediate);
    set(ir::OpCode::ADD_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SUB_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::MUL_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::DIV_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::MOD_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::AND_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::OR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::XOR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SHL_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::SHR_I32, MachineForm::IntegerBinary32);
    set(ir::OpCode::ADD_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::SUB_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::MUL_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::DIV_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::MOD_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::ADD_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::SUB_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::MUL_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::DIV_F32, MachineForm::FloatBinary32);
    set(ir::OpCode::FMA_F32, MachineForm::FloatTernaryFma32);
    set(ir::OpCode::ADD_F64, MachineForm::FloatBinary64);
    set(ir::OpCode::SUB_F64, MachineForm::FloatBinary64);
    set(ir::OpCode::MUL_F64, MachineForm::FloatBinary64);
    set(ir::OpCode::DIV_F64, MachineForm::FloatBinary64);
    set(ir::OpCode::CMP_EQ_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_NE_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_LT_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_LE_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_GT_I32, MachineForm::Compare32);
    set(ir::OpCode::CMP_GE_I32, MachineForm::Compare32);
    set(ir::OpCode::GATHER_F32_512, MachineForm::VectorGather512);
    set(ir::OpCode::STORE_V512, MachineForm::VectorStore512);
    set(ir::OpCode::VEC_ADD_F32_512, MachineForm::VectorBinary512);
    set(ir::OpCode::VEC_MUL_F32_512, MachineForm::VectorBinary512);
    set(ir::OpCode::CALL, MachineForm::Call);
    set(ir::OpCode::CALL_VARIADIC, MachineForm::Call);
    set(ir::OpCode::SELECT, MachineForm::Call);
    set(ir::OpCode::STRING_LENGTH, MachineForm::Call);
    set(ir::OpCode::STRING_COMPARE, MachineForm::Call);
    set(ir::OpCode::STRING_CONCAT, MachineForm::Call);
    set(ir::OpCode::STRING_SUBSTRING, MachineForm::Call);
    set(ir::OpCode::LABEL, MachineForm::NoOp);
    set(ir::OpCode::BRANCH, MachineForm::Goto);
    set(ir::OpCode::RET, MachineForm::Return);
    set(ir::OpCode::RETURN, MachineForm::Return);
    set(ir::OpCode::GOTO, MachineForm::Goto);
    set(ir::OpCode::BRANCH_COND, MachineForm::BranchCond);
    set(ir::OpCode::BRANCH_COMPARE_FUSED, MachineForm::BranchCompare);
    set(ir::OpCode::SYSCALL, MachineForm::Syscall);
    set(ir::OpCode::ACQUIRE_FENCE, MachineForm::HardwareFence);
    set(ir::OpCode::AND_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::ASSUME, MachineForm::NoOp);
    set(ir::OpCode::ASYNC_AWAIT, MachineForm::ConcurrentSync);
    set(ir::OpCode::ASYNC_LAUNCH, MachineForm::ConcurrentSync);
    set(ir::OpCode::ASYNC_SPAWN, MachineForm::ConcurrentSync);
    set(ir::OpCode::ATOMIC_FENCE, MachineForm::AtomicOp);
    set(ir::OpCode::ATOMIC_LOAD, MachineForm::AtomicOp);
    set(ir::OpCode::ATOMIC_SNAPSHOT, MachineForm::AtomicOp);
    set(ir::OpCode::ATOMIC_STORE, MachineForm::AtomicOp);
    set(ir::OpCode::BARRIER_SYNC, MachineForm::ConcurrentSync);
    set(ir::OpCode::BOOL_SELECT, MachineForm::Call);
    set(ir::OpCode::CHAN_RECV, MachineForm::ConcurrentSync);
    set(ir::OpCode::CHAN_SEND, MachineForm::ConcurrentSync);
    set(ir::OpCode::DEOPT_TRIGGER, MachineForm::Call);
    set(ir::OpCode::DSA_ALLOCATE, MachineForm::Call);
    set(ir::OpCode::DSA_DEALLOCATE, MachineForm::Call);
    set(ir::OpCode::FCVTL_F16_TO_F32, MachineForm::Call);
    set(ir::OpCode::FCVTN_F32_TO_F16, MachineForm::Call);
    set(ir::OpCode::FIBER_YIELD, MachineForm::ConcurrentSync);
    set(ir::OpCode::FMA_WIDEN_F16_F32, MachineForm::Call);
    set(ir::OpCode::FUSION_GROUP, MachineForm::NoOp);
    set(ir::OpCode::GATHER_16, MachineForm::Call);
    set(ir::OpCode::GEP, MachineForm::Call);
    set(ir::OpCode::GPU_LAUNCH, MachineForm::Call);
    set(ir::OpCode::GRAPH_BUILD, MachineForm::Call);
    set(ir::OpCode::GRAPH_TRAVERSE, MachineForm::Call);
    set(ir::OpCode::GUARD_ALIAS, MachineForm::Call);
    set(ir::OpCode::GUARD_VALUE, MachineForm::Call);
    set(ir::OpCode::IMMEDIATE, MachineForm::LoadImmediate);
    set(ir::OpCode::LEA, MachineForm::Call);
    set(ir::OpCode::LOCK_ACQUIRE, MachineForm::ConcurrentSync);
    set(ir::OpCode::LOCK_RELEASE, MachineForm::ConcurrentSync);
    set(ir::OpCode::MEMORY_FENCE, MachineForm::HardwareFence);
    set(ir::OpCode::OR_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::OWNERSHIP_ACQUIRE, MachineForm::Call);
    set(ir::OpCode::OWNERSHIP_RELEASE, MachineForm::Call);
    set(ir::OpCode::OWNERSHIP_TRANSFER, MachineForm::Call);
    set(ir::OpCode::PHI_MULTI, MachineForm::Call);
    set(ir::OpCode::PHI_NODE, MachineForm::Call);
    set(ir::OpCode::PREFETCH, MachineForm::Call);
    set(ir::OpCode::PTR_CAST, MachineForm::Move);
    set(ir::OpCode::PTR_TO_INT, MachineForm::Move);
    set(ir::OpCode::INT_TO_PTR, MachineForm::Move);
    set(ir::OpCode::PTR_DIFF, MachineForm::Call);
    set(ir::OpCode::READ_TSC, MachineForm::Call);
    set(ir::OpCode::SPIN_ACQUIRE, MachineForm::Call);
    set(ir::OpCode::RING_PUSH_FREE, MachineForm::Call);
    set(ir::OpCode::WASM_INVOKE, MachineForm::Call);
    set(ir::OpCode::FIBER_YIELD_LOOP, MachineForm::ConcurrentSync);
    set(ir::OpCode::SUSPEND_CTX, MachineForm::Call);
    set(ir::OpCode::RESUME_CTX, MachineForm::Call);
    set(ir::OpCode::NEON_VADD_Q, MachineForm::Call);
    set(ir::OpCode::MATMUL_BF16, MachineForm::TensorOp);
    set(ir::OpCode::MATMUL_FP8, MachineForm::TensorOp);
    set(ir::OpCode::SDPA_F16, MachineForm::TensorOp);
    set(ir::OpCode::SOFTMAX_FAST, MachineForm::TensorOp);
    set(ir::OpCode::LAYER_NORM, MachineForm::TensorOp);
    set(ir::OpCode::ROPE_EMBED, MachineForm::TensorOp);
    set(ir::OpCode::FAST_INV_SQRT, MachineForm::Call);
    set(ir::OpCode::VEC3_CROSS, MachineForm::Call);
    set(ir::OpCode::VEC4_DOT, MachineForm::Call);
    set(ir::OpCode::MAT4_MUL, MachineForm::Call);
    set(ir::OpCode::QUAT_SLERP, MachineForm::Call);
    set(ir::OpCode::RAY_AABB_ISECT, MachineForm::Call);
    set(ir::OpCode::REGION_BEGIN, MachineForm::NoOp);
    set(ir::OpCode::REGION_END, MachineForm::NoOp);
    set(ir::OpCode::RELEASE_FENCE, MachineForm::HardwareFence);
    set(ir::OpCode::BRANCH_LIKELY, MachineForm::BranchCond);
    set(ir::OpCode::BRANCH_UNLIKELY, MachineForm::BranchCond);
    set(ir::OpCode::CLFLUSH, MachineForm::Call);
    set(ir::OpCode::PREFETCH_T0, MachineForm::Call);
    set(ir::OpCode::SCOP_ANALYZE, MachineForm::Call);
    set(ir::OpCode::SELECT_MULTI, MachineForm::Call);
    set(ir::OpCode::SHL_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::SHR_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::SIMD_BSL_PREDICATE, MachineForm::Call);
    set(ir::OpCode::STRING_CONST, MachineForm::LoadImmediate);
    set(ir::OpCode::SWITCH_DISPATCH, MachineForm::Call);
    set(ir::OpCode::SYNC_BARRIER, MachineForm::ConcurrentSync);
    set(ir::OpCode::TENSOR_BROADCAST, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_CONCAT, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_CONV, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_GEMM, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_POOL, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_REDUCE, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_RESHAPE, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_SLICE, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_SOFTMAX, MachineForm::TensorOp);
    set(ir::OpCode::TENSOR_TRANSPOSE, MachineForm::TensorOp);
    set(ir::OpCode::THREAD_SPAWN, MachineForm::ConcurrentSync);
    set(ir::OpCode::TREE_BUILD, MachineForm::Call);
    set(ir::OpCode::TREE_TRAVERSE, MachineForm::Call);
    set(ir::OpCode::TRIE_BUILD, MachineForm::Call);
    set(ir::OpCode::TRIE_INSERT, MachineForm::Call);
    set(ir::OpCode::TRIE_LOOKUP, MachineForm::Call);
    set(ir::OpCode::TUPLE_PACK, MachineForm::Call);
    set(ir::OpCode::TUPLE_UNPACK, MachineForm::Call);
    set(ir::OpCode::VEC_LOAD, MachineForm::Call);
    set(ir::OpCode::VEC_REDUCE_ADD, MachineForm::Call);
    set(ir::OpCode::VEC_SHUFFLE, MachineForm::Call);
    set(ir::OpCode::VEC_STORE, MachineForm::Call);
    set(ir::OpCode::VOLATILE_LOAD, MachineForm::LoadStack);
    set(ir::OpCode::VOLATILE_STORE, MachineForm::StoreStack);
    set(ir::OpCode::XOR_I64, MachineForm::IntegerBinary64);
    set(ir::OpCode::OP_ADD, MachineForm::IntegerBinary32);
    set(ir::OpCode::ADD_I8, MachineForm::IntegerBinary32);
    set(ir::OpCode::ADD_I128, MachineForm::IntegerBinary64);
    set(ir::OpCode::ADD_I256, MachineForm::IntegerBinary64);
    set(ir::OpCode::ADD_F8, MachineForm::FloatBinary32);
    set(ir::OpCode::ADD_BF16, MachineForm::FloatBinary32);
    set(ir::OpCode::BIT_FIELD_INSERT, MachineForm::Call);
    set(ir::OpCode::BIT_FIELD_EXTRACT, MachineForm::Call);
    set(ir::OpCode::UNDEF, MachineForm::NoOp);
    return table;
}();

alignas(64) inline constexpr auto kIntegerBinary32KindTable = []() constexpr {
    std::array<X64IntegerBinary32Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64IntegerBinary32Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_I32)] = X64IntegerBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_I32)] = X64IntegerBinary32Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_I32)] = X64IntegerBinary32Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_I32)] = X64IntegerBinary32Kind::Div;
    table[static_cast<size_t>(ir::OpCode::MOD_I32)] = X64IntegerBinary32Kind::Mod;
    table[static_cast<size_t>(ir::OpCode::AND_I32)] = X64IntegerBinary32Kind::And;
    table[static_cast<size_t>(ir::OpCode::OR_I32)] = X64IntegerBinary32Kind::Or;
    table[static_cast<size_t>(ir::OpCode::XOR_I32)] = X64IntegerBinary32Kind::Xor;
    table[static_cast<size_t>(ir::OpCode::SHL_I32)] = X64IntegerBinary32Kind::Shl;
    table[static_cast<size_t>(ir::OpCode::SHR_I32)] = X64IntegerBinary32Kind::Shr;
    table[static_cast<size_t>(ir::OpCode::OP_ADD)] = X64IntegerBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::ADD_I8)] = X64IntegerBinary32Kind::Add;
    return table;
}();

alignas(64) inline constexpr auto kIntegerBinary64KindTable = []() constexpr {
    std::array<X64IntegerBinary64Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64IntegerBinary64Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_I64)] = X64IntegerBinary64Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_I64)] = X64IntegerBinary64Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_I64)] = X64IntegerBinary64Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_I64)] = X64IntegerBinary64Kind::Div;
    table[static_cast<size_t>(ir::OpCode::MOD_I64)] = X64IntegerBinary64Kind::Mod;
    table[static_cast<size_t>(ir::OpCode::AND_I64)] = X64IntegerBinary64Kind::And;
    table[static_cast<size_t>(ir::OpCode::OR_I64)] = X64IntegerBinary64Kind::Or;
    table[static_cast<size_t>(ir::OpCode::SHL_I64)] = X64IntegerBinary64Kind::Shl;
    table[static_cast<size_t>(ir::OpCode::SHR_I64)] = X64IntegerBinary64Kind::Shr;
    table[static_cast<size_t>(ir::OpCode::XOR_I64)] = X64IntegerBinary64Kind::Xor;
    table[static_cast<size_t>(ir::OpCode::ADD_I128)] = X64IntegerBinary64Kind::Add;
    table[static_cast<size_t>(ir::OpCode::ADD_I256)] = X64IntegerBinary64Kind::Add;
    return table;
}();

alignas(64) inline constexpr auto kFloatBinary32KindTable = []() constexpr {
    std::array<X64FloatBinary32Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64FloatBinary32Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_F32)] = X64FloatBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_F32)] = X64FloatBinary32Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_F32)] = X64FloatBinary32Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_F32)] = X64FloatBinary32Kind::Div;
    table[static_cast<size_t>(ir::OpCode::ADD_F8)] = X64FloatBinary32Kind::Add;
    table[static_cast<size_t>(ir::OpCode::ADD_BF16)] = X64FloatBinary32Kind::Add;
    return table;
}();

alignas(64) inline constexpr auto kCompare32KindTable = []() constexpr {
    std::array<X64Compare32Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64Compare32Kind::None);
    table[static_cast<size_t>(ir::OpCode::CMP_EQ_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_NE_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_LT_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_LE_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_GT_I32)] = X64Compare32Kind::Any;
    table[static_cast<size_t>(ir::OpCode::CMP_GE_I32)] = X64Compare32Kind::Any;
    return table;
}();

alignas(64) inline constexpr auto kFloatBinary64KindTable = []() constexpr {
    std::array<X64FloatBinary64Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64FloatBinary64Kind::None);
    table[static_cast<size_t>(ir::OpCode::ADD_F64)] = X64FloatBinary64Kind::Add;
    table[static_cast<size_t>(ir::OpCode::SUB_F64)] = X64FloatBinary64Kind::Sub;
    table[static_cast<size_t>(ir::OpCode::MUL_F64)] = X64FloatBinary64Kind::Mul;
    table[static_cast<size_t>(ir::OpCode::DIV_F64)] = X64FloatBinary64Kind::Div;
    return table;
}();

alignas(64) inline constexpr auto kVectorBinary512KindTable = []() constexpr {
    std::array<X64VectorBinary512Kind, kMaxOpCodeIndex + 1> table{};
    table.fill(X64VectorBinary512Kind::None);
    table[static_cast<size_t>(ir::OpCode::VEC_ADD_F32_512)] = X64VectorBinary512Kind::Add;
    table[static_cast<size_t>(ir::OpCode::VEC_MUL_F32_512)] = X64VectorBinary512Kind::Mul;
    return table;
}();

[[nodiscard]] constexpr MachineForm classify_machine_form(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kMachineFormTable.size() ? kMachineFormTable[index] : kMachineFormTable[0];
}

[[nodiscard]] constexpr X64IntegerBinary32Kind integer_binary32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kIntegerBinary32KindTable.size() ? kIntegerBinary32KindTable[index] : X64IntegerBinary32Kind::None;
}

[[nodiscard]] constexpr X64IntegerBinary64Kind integer_binary64_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kIntegerBinary64KindTable.size() ? kIntegerBinary64KindTable[index] : X64IntegerBinary64Kind::None;
}

[[nodiscard]] constexpr X64FloatBinary32Kind float_binary32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kFloatBinary32KindTable.size() ? kFloatBinary32KindTable[index] : X64FloatBinary32Kind::None;
}

[[nodiscard]] constexpr X64Compare32Kind compare32_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kCompare32KindTable.size() ? kCompare32KindTable[index] : X64Compare32Kind::None;
}

[[nodiscard]] constexpr X64FloatBinary64Kind float_binary64_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kFloatBinary64KindTable.size() ? kFloatBinary64KindTable[index] : X64FloatBinary64Kind::None;
}

[[nodiscard]] constexpr X64VectorBinary512Kind vector_binary512_kind(ir::OpCode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    return index < kVectorBinary512KindTable.size() ? kVectorBinary512KindTable[index] : X64VectorBinary512Kind::None;
}

} // namespace generated
} // namespace dgtl::isa::lowering
