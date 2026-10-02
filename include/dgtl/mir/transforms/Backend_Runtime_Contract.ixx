/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Pure Machine IR Backend Runtime Contract & Intrinsic Resolution
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <array>
#include <cstddef>
#include <string_view>

export module dgtl.mir.runtime_contract;

// Pure Machine IR imports only - zero upstream ir::OpCode dependencies
import dgtl.mir.machine_ir;
import dgtl.target;

/**
 * @brief Backend Runtime Contract
 *
 * Defines the contract between compiled Machine IR instructions and the host runtime.
 * Implements direct mapping of target-independent GenericMachineOpcodes (GM_*) and
 * MachineSemanticKind operations to pre-compiled runtime helper symbols.
 * Zero dependency on high-level ir::OpCode.
 */
export namespace dgtl::mir::runtime_contract {

inline constexpr std::string_view kRuntimeContractVersion = "3";

namespace detail {

using namespace dgtl::machine;

inline constexpr size_t kMaxGenericOpcodeIndex = 256;

// Cacheline-aligned table mapping GenericMachineOpcode directly to runtime symbols
alignas(64) inline constexpr auto kRuntimeSymbolTable = []() constexpr {
    std::array<const char*, kMaxGenericOpcodeIndex> table{};
    table.fill(nullptr);

    // String Manipulation
    table[static_cast<size_t>(GM_STRING_LENGTH)]      = "__dgtl_rt_string_length";
    table[static_cast<size_t>(GM_STRING_COMPARE)]     = "__dgtl_rt_string_compare";
    table[static_cast<size_t>(GM_STRING_CONCAT)]      = "__dgtl_rt_string_concat";
    table[static_cast<size_t>(GM_STRING_SUBSTRING)]   = "__dgtl_rt_string_substring";
    table[static_cast<size_t>(GM_STRING_COPY)]        = "__dgtl_rt_string_copy";
    table[static_cast<size_t>(GM_STRING_EQUALS)]      = "__dgtl_rt_string_equals";
    table[static_cast<size_t>(GM_STRING_FIND)]        = "__dgtl_rt_string_find";
    table[static_cast<size_t>(GM_STRING_HASH)]        = "__dgtl_rt_string_hash";

    // Fences & Synchronization
    table[static_cast<size_t>(GM_FENCE)]              = "__dgtl_rt_memory_fence";
    table[static_cast<size_t>(GM_ACQUIRE_FENCE)]      = "__dgtl_rt_acquire_fence";
    table[static_cast<size_t>(GM_RELEASE_FENCE)]      = "__dgtl_rt_release_fence";
    table[static_cast<size_t>(GM_ATOMIC_FENCE)]       = "__dgtl_rt_atomic_fence";
    table[static_cast<size_t>(GM_BARRIER_SYNC)]       = "__dgtl_rt_barrier_sync";
    table[static_cast<size_t>(GM_SYNC_BARRIER)]       = "__dgtl_rt_sync_barrier";
    table[static_cast<size_t>(GM_LOCK_ACQUIRE)]       = "__dgtl_rt_lock_acquire";
    table[static_cast<size_t>(GM_LOCK_RELEASE)]       = "__dgtl_rt_lock_release";
    table[static_cast<size_t>(GM_SPIN_ACQUIRE)]       = "__dgtl_rt_spin_acquire";

    // Atomics
    table[static_cast<size_t>(GM_ATOMIC_LOAD)]        = "__dgtl_rt_atomic_load";
    table[static_cast<size_t>(GM_ATOMIC_STORE)]       = "__dgtl_rt_atomic_store";
    table[static_cast<size_t>(GM_ATOMIC_SNAPSHOT)]    = "__dgtl_rt_atomic_snapshot";

    // Async, Fibers, Coroutines, Threads
    table[static_cast<size_t>(GM_ASYNC_AWAIT)]        = "__dgtl_rt_async_await";
    table[static_cast<size_t>(GM_ASYNC_LAUNCH)]       = "__dgtl_rt_async_launch";
    table[static_cast<size_t>(GM_ASYNC_SPAWN)]        = "__dgtl_rt_async_spawn";
    table[static_cast<size_t>(GM_FIBER_YIELD)]        = "__dgtl_rt_fiber_yield";
    table[static_cast<size_t>(GM_FIBER_YIELD_LOOP)]   = "__dgtl_rt_fiber_yield_loop";
    table[static_cast<size_t>(GM_SUSPEND_CTX)]        = "__dgtl_rt_suspend_ctx";
    table[static_cast<size_t>(GM_RESUME_CTX)]         = "__dgtl_rt_resume_ctx";
    table[static_cast<size_t>(GM_THREAD_SPAWN)]       = "__dgtl_rt_thread_spawn";

    // Dynamic Allocations, Data Structures, Graphs, Trees
    table[static_cast<size_t>(GM_DSA_ALLOCATE)]       = "__dgtl_rt_dsa_allocate";
    table[static_cast<size_t>(GM_DSA_DEALLOCATE)]     = "__dgtl_rt_dsa_deallocate";
    table[static_cast<size_t>(GM_RING_PUSH_FREE)]     = "__dgtl_rt_ring_push_free";
    table[static_cast<size_t>(GM_DEOPT_TRIGGER)]      = "__dgtl_rt_deopt_trigger";
    table[static_cast<size_t>(GM_TREE_BUILD)]         = "__dgtl_rt_tree_build";
    table[static_cast<size_t>(GM_TREE_TRAVERSE)]      = "__dgtl_rt_tree_traverse";
    table[static_cast<size_t>(GM_TRIE_BUILD)]         = "__dgtl_rt_trie_build";
    table[static_cast<size_t>(GM_TRIE_INSERT)]        = "__dgtl_rt_trie_insert";
    table[static_cast<size_t>(GM_TRIE_LOOKUP)]        = "__dgtl_rt_trie_lookup";
    table[static_cast<size_t>(GM_GRAPH_BUILD)]        = "__dgtl_rt_graph_build";
    table[static_cast<size_t>(GM_GRAPH_TRAVERSE)]     = "__dgtl_rt_graph_traverse";
    table[static_cast<size_t>(GM_TUPLE_PACK)]         = "__dgtl_rt_tuple_pack";
    table[static_cast<size_t>(GM_TUPLE_UNPACK)]       = "__dgtl_rt_tuple_unpack";

    // Math & Float Conversions / Intrinsics
    table[static_cast<size_t>(GM_FFMA32)]             = "__dgtl_rt_fma_f32";
    table[static_cast<size_t>(GM_FFMA64)]             = "__dgtl_rt_fma_f64";
    table[static_cast<size_t>(GM_FAST_INV_SQRT)]      = "__dgtl_rt_fast_inv_sqrt";
    table[static_cast<size_t>(GM_VEC_GATHER)]         = "__dgtl_rt_gather_f32_512";

    // Memory Helpers & Cache Intrinsics
    table[static_cast<size_t>(GM_LEA)]                = "__dgtl_rt_lea";
    table[static_cast<size_t>(GM_GEP)]                = "__dgtl_rt_gep";
    table[static_cast<size_t>(GM_PTR_DIFF)]           = "__dgtl_rt_ptr_diff";
    table[static_cast<size_t>(GM_PTR_TO_INT)]         = "__dgtl_rt_ptr_to_int";
    table[static_cast<size_t>(GM_INT_TO_PTR)]         = "__dgtl_rt_int_to_ptr";
    table[static_cast<size_t>(GM_PREFETCH)]           = "__dgtl_rt_prefetch";
    table[static_cast<size_t>(GM_PREFETCH_T0)]        = "__dgtl_rt_prefetch_t0";
    table[static_cast<size_t>(GM_CLFLUSH)]            = "__dgtl_rt_clflush";
    table[static_cast<size_t>(GM_READ_TSC)]           = "__dgtl_rt_read_tsc";

    // Tensor Operations & Accelerators
    table[static_cast<size_t>(GM_TENSOR_OP)]          = "__dgtl_rt_tensor_gemm";
    table[static_cast<size_t>(GM_TENSOR_BROADCAST)]   = "__dgtl_rt_tensor_broadcast";
    table[static_cast<size_t>(GM_TENSOR_CONCAT)]      = "__dgtl_rt_tensor_concat";
    table[static_cast<size_t>(GM_TENSOR_CONV)]        = "__dgtl_rt_tensor_conv";
    table[static_cast<size_t>(GM_TENSOR_GEMM)]        = "__dgtl_rt_tensor_gemm";
    table[static_cast<size_t>(GM_TENSOR_GATHER)]      = "__dgtl_rt_tensor_gather";
    table[static_cast<size_t>(GM_TENSOR_POOL)]        = "__dgtl_rt_tensor_pool";
    table[static_cast<size_t>(GM_TENSOR_REDUCE)]      = "__dgtl_rt_tensor_reduce";
    table[static_cast<size_t>(GM_TENSOR_RESHAPE)]     = "__dgtl_rt_tensor_reshape";
    table[static_cast<size_t>(GM_TENSOR_SCATTER_D)]   = "__dgtl_rt_tensor_scatter_d";
    table[static_cast<size_t>(GM_TENSOR_SLICE)]       = "__dgtl_rt_tensor_slice";
    table[static_cast<size_t>(GM_TENSOR_SOFTMAX)]     = "__dgtl_rt_tensor_softmax";
    table[static_cast<size_t>(GM_TENSOR_TRANSPOSE)]   = "__dgtl_rt_tensor_transpose";
    table[static_cast<size_t>(GM_MATMUL_BF16)]        = "__dgtl_rt_matmul_bf16";
    table[static_cast<size_t>(GM_MATMUL_FP8)]         = "__dgtl_rt_matmul_fp8";
    table[static_cast<size_t>(GM_SDPA_F16)]           = "__dgtl_rt_sdpa_f16";
    table[static_cast<size_t>(GM_SOFTMAX_FAST)]       = "__dgtl_rt_softmax_fast";
    table[static_cast<size_t>(GM_LAYER_NORM)]         = "__dgtl_rt_layer_norm";
    table[static_cast<size_t>(GM_ROPE_EMBED)]         = "__dgtl_rt_rope_embed";
    table[static_cast<size_t>(GM_MAT4_MUL)]           = "__dgtl_rt_mat4_mul";
    table[static_cast<size_t>(GM_QUAT_SLERP)]         = "__dgtl_rt_quat_slerp";
    table[static_cast<size_t>(GM_VEC3_CROSS)]         = "__dgtl_rt_vec3_cross";
    table[static_cast<size_t>(GM_VEC4_DOT)]           = "__dgtl_rt_vec4_dot";
    table[static_cast<size_t>(GM_RAY_AABB_ISECT)]     = "__dgtl_rt_ray_aabb_isect";
    table[static_cast<size_t>(GM_GPU_LAUNCH)]         = "__dgtl_rt_gpu_launch";
    table[static_cast<size_t>(GM_WASM_INVOKE)]        = "__dgtl_rt_wasm_invoke";

    return table;
}();

} // namespace detail

/**
 * @brief Resolves a GenericMachineOpcode to its runtime ABI symbol name.
 * Operates strictly on MachineOpcode with zero upstream IR dependencies.
 */
[[nodiscard]] inline const char* runtime_symbol_for_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    const size_t index = static_cast<size_t>(opcode);
    if (index < detail::kRuntimeSymbolTable.size()) [[likely]] {
        return detail::kRuntimeSymbolTable[index];
    }
    return nullptr;
}

/**
 * @brief Checks if a GenericMachineOpcode maps to an external runtime library symbol.
 */
[[nodiscard]] inline bool has_runtime_symbol(dgtl::machine::MachineOpcode opcode) noexcept {
    return runtime_symbol_for_opcode(opcode) != nullptr;
}

/**
 * @brief Resolves target-independent machine semantic operations to runtime ABI symbol names.
 */
[[nodiscard]] inline const char* runtime_symbol_for_semantic_kind(dgtl::machine::MachineSemanticKind kind) noexcept {
    using K = dgtl::machine::MachineSemanticKind;
    switch (kind) {
        // Trigonometry
        case K::FloatSin:      return "__dgtl_rt_sin";
        case K::FloatCos:      return "__dgtl_rt_cos";
        case K::FloatTan:      return "__dgtl_rt_tan";
        case K::FloatAsin:     return "__dgtl_rt_asin";
        case K::FloatAcos:     return "__dgtl_rt_acos";
        case K::FloatAtan:     return "__dgtl_rt_atan";
        case K::FloatAtan2:    return "__dgtl_rt_atan2";
        case K::FloatSinh:     return "__dgtl_rt_sinh";
        case K::FloatCosh:     return "__dgtl_rt_cosh";
        case K::FloatTanh:     return "__dgtl_rt_tanh";

        // Logarithmic, Exponential & Algebra
        case K::FloatLog:      return "__dgtl_rt_log";
        case K::FloatLog2:     return "__dgtl_rt_log2";
        case K::FloatLog10:    return "__dgtl_rt_log10";
        case K::FloatLog1p:    return "__dgtl_rt_log1p";
        case K::FloatExp:      return "__dgtl_rt_exp";
        case K::FloatExp2:     return "__dgtl_rt_exp2";
        case K::FloatExp10:    return "__dgtl_rt_exp10";
        case K::FloatExpm1:    return "__dgtl_rt_expm1";
        case K::FloatPow:      return "__dgtl_rt_pow";
        case K::FloatCbrt:     return "__dgtl_rt_cbrt";
        case K::FloatHypot:    return "__dgtl_rt_hypot";

        // String Operations
        case K::StringLength:    return "__dgtl_rt_string_length";
        case K::StringCompare:   return "__dgtl_rt_string_compare";
        case K::StringConcat:    return "__dgtl_rt_string_concat";
        case K::StringSubstring: return "__dgtl_rt_string_substring";
        case K::StringCopy:      return "__dgtl_rt_string_copy";
        case K::StringEquals:    return "__dgtl_rt_string_equals";
        case K::StringFind:      return "__dgtl_rt_string_find";
        case K::StringHash:      return "__dgtl_rt_string_hash";

        // Bit Manipulation Fallbacks
        case K::IntegerPopcount: return "__dgtl_rt_popcount";
        case K::IntegerClz:      return "__dgtl_rt_clz";
        case K::IntegerCtz:      return "__dgtl_rt_ctz";

        default:
            return nullptr;
    }
}

[[nodiscard]] inline bool has_runtime_symbol_for_semantic(dgtl::machine::MachineSemanticKind kind) noexcept {
    return runtime_symbol_for_semantic_kind(kind) != nullptr;
}

/**
 * @brief Returns whether a semantic operation can be expanded inline into hardware
 * instructions on the specified target machine, avoiding runtime library calls.
 */
[[nodiscard]] inline bool can_expand_inline(
    const core::TargetMachine& target,
    dgtl::machine::MachineSemanticKind kind) noexcept {
    using K = dgtl::machine::MachineSemanticKind;

    switch (kind) {
        case K::FloatFma:
            return target.supports_fma() ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::VectorAdd:
        case K::VectorMul:
            return target.description().supports_vector_width(512) ||
                   target.description().supports_vector_width(256) ||
                   target.has_feature(core::TargetFeature::NEON) ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::VectorGather:
        case K::VectorStore:
            return target.description().supports_vector_width(512) ||
                   (target.has_feature(core::TargetFeature::AVX2) && target.supports_vector_memory()) ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::FloatRsqrt:
            return target.family() == core::TargetFamily::X64 ||
                   target.family() == core::TargetFamily::ARM64 ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::IntegerPopcount:
            return target.has_feature(core::TargetFeature::BMI2) ||
                   target.has_feature(core::TargetFeature::NEON) ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::IntegerClz:
        case K::IntegerCtz:
            return target.has_feature(core::TargetFeature::BMI2) ||
                   target.family() == core::TargetFamily::ARM64 ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::TensorOp:
            return target.has_feature(core::TargetFeature::Tensor) ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        case K::HardwareFence:
        case K::AtomicOp:
        case K::ConcurrentSync:
            return target.has_feature(core::TargetFeature::Atomics) ||
                   target.family() == core::TargetFamily::CUDA ||
                   target.family() == core::TargetFamily::ROCm;

        default:
            return false;
    }
}

} // namespace dgtl::mir::runtime_contract