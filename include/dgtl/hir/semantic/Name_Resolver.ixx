/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Hierarchical Name & Overload Resolution Engine
 * Standards:   C++26 / UCRT64 / High-Performance Symbol Resolution
 */

module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <vector>
#include <span>
#include <optional>
#include <algorithm>

export module dgtl.hir.name_resolver;

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.hir.semantic;
import dgtl.hir.type_system;
import dgtl.ir_core.abi;

export namespace dgtl::hir::semantic {

/**
 * @struct FunctionOverloadCandidate
 * @brief Describes an overload candidate for function dispatch.
 */
struct FunctionOverloadCandidate {
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
    std::string_view name;
    std::string_view mangled_name;
    dgtl::ir::TypeCode return_type = dgtl::ir::TypeCode::VOID_T;
    std::vector<dgtl::ir::TypeCode> parameter_types;
    bool is_variadic = false;
};

/**
 * @class NameResolver
 * @brief Handles hierarchical namespace paths, overload resolution, and ABI mangling.
 */
class NameResolver {
public:
    explicit NameResolver(dgtl::core::DGTL_Arena& arena,
                          SymbolTable& sym_table,
                          TypeSystem& type_sys) noexcept
        : arena_(arena), symbol_table_(sym_table), type_system_(type_sys) {
        active_namespaces_.reserve(8);
        using_namespaces_.reserve(16);
    }

    ~NameResolver() = default;

    // =========================================================================
    // NAMESPACE MANAGEMENT
    // =========================================================================

    void enter_namespace(std::string_view ns_name) {
        active_namespaces_.push_back(intern_string(ns_name));
    }

    void leave_namespace() noexcept {
        if (!active_namespaces_.empty()) {
            active_namespaces_.pop_back();
        }
    }

    void add_using_namespace(std::string_view ns_name) {
        using_namespaces_.push_back(intern_string(ns_name));
    }

    [[nodiscard]] std::string current_namespace_prefix() const {
        std::string prefix;
        for (const auto& ns : active_namespaces_) {
            prefix.append(ns).append("::");
        }
        return prefix;
    }

    // =========================================================================
    // QUALIFIED PATH RESOLUTION
    // =========================================================================

    [[nodiscard]] Symbol* resolve_path(std::string_view qualified_path) const noexcept {
        if (qualified_path.empty()) return nullptr;

        // 1. Direct absolute lookup
        if (auto* sym = symbol_table_.lookup(qualified_path)) {
            return sym;
        }

        // 2. Relative lookup under current active namespaces
        std::string scoped_candidate;
        for (auto it = active_namespaces_.rbegin(); it != active_namespaces_.rend(); ++it) {
            scoped_candidate.clear();
            scoped_candidate.append(*it).append("::").append(qualified_path);
            if (auto* sym = symbol_table_.lookup(scoped_candidate)) {
                return sym;
            }
        }

        // 3. Lookup under "using namespace" declarations
        for (const auto& using_ns : using_namespaces_) {
            scoped_candidate.clear();
            scoped_candidate.append(using_ns).append("::").append(qualified_path);
            if (auto* sym = symbol_table_.lookup(scoped_candidate)) {
                return sym;
            }
        }

        return nullptr;
    }

    // =========================================================================
    // FUNCTION OVERLOAD REGISTRATION & RESOLUTION
    // =========================================================================

    void register_overload(FunctionOverloadCandidate candidate) {
        if (candidate.mangled_name.empty()) {
            candidate.mangled_name = mangle_sovereign(candidate.name, candidate.parameter_types);
        }
        overload_candidates_.push_back(std::move(candidate));
    }

    [[nodiscard]] const FunctionOverloadCandidate* resolve_overload(
        std::string_view name,
        std::span<const dgtl::ir::TypeCode> argument_types
    ) const noexcept {
        const FunctionOverloadCandidate* best_candidate = nullptr;
        int32_t best_penalty = 100000;

        for (const auto& cand : overload_candidates_) {
            if (cand.name != name) continue;

            if (!cand.is_variadic && cand.parameter_types.size() != argument_types.size()) {
                continue;
            }
            if (cand.is_variadic && argument_types.size() < cand.parameter_types.size()) {
                continue;
            }

            int32_t current_penalty = 0;
            bool compatible = true;

            const size_t check_count = std::min(cand.parameter_types.size(), argument_types.size());
            for (size_t i = 0; i < check_count; ++i) {
                const auto expected = cand.parameter_types[i];
                const auto actual = argument_types[i];

                if (expected == actual) {
                    continue; // 0 penalty for exact type match
                }

                const auto* desc_actual = type_system_.get_primitive(actual);
                const auto* desc_expected = type_system_.get_primitive(expected);

                if (desc_actual && desc_expected && type_system_.can_losslessly_coerce(*desc_actual, *desc_expected)) {
                    current_penalty += 1; // 1 penalty point for safe widening
                } else {
                    compatible = false;
                    break;
                }
            }

            if (compatible && current_penalty < best_penalty) {
                best_penalty = current_penalty;
                best_candidate = &cand;
            }
        }

        return best_candidate;
    }

    // =========================================================================
    // SOVEREIGN ABI SYMBOL MANGLING
    // =========================================================================

    [[nodiscard]] std::string mangle_sovereign(
        std::string_view base_name,
        std::span<const dgtl::ir::TypeCode> param_types
    ) const {
        std::string mangled;
        mangled.reserve(64);
        mangled.append("_Z_DGTL_").append(std::to_string(base_name.size())).append(base_name);

        for (const auto tc : param_types) {
            mangled.push_back('_');
            switch (tc) {
                case dgtl::ir::TypeCode::VOID_T:        mangled.append("v"); break;
                case dgtl::ir::TypeCode::BOOL:          mangled.append("b"); break;
                case dgtl::ir::TypeCode::PREDICATE:     mangled.append("p"); break;
                case dgtl::ir::TypeCode::ADDRESS_SPACE: mangled.append("as"); break;
                case dgtl::ir::TypeCode::PTR:           mangled.append("ptr"); break;
                case dgtl::ir::TypeCode::STRING_HANDLE: mangled.append("str"); break;
                case dgtl::ir::TypeCode::I4:            mangled.append("i4"); break;
                case dgtl::ir::TypeCode::I8:            mangled.append("i8"); break;
                case dgtl::ir::TypeCode::I16:           mangled.append("i16"); break;
                case dgtl::ir::TypeCode::I32:           mangled.append("i32"); break;
                case dgtl::ir::TypeCode::I64:           mangled.append("i64"); break;
                case dgtl::ir::TypeCode::I128:          mangled.append("i128"); break;
                case dgtl::ir::TypeCode::I256:          mangled.append("i256"); break;
                case dgtl::ir::TypeCode::I512:          mangled.append("i512"); break;
                case dgtl::ir::TypeCode::F8:            mangled.append("f8"); break;
                case dgtl::ir::TypeCode::BF16:          mangled.append("bf16"); break;
                case dgtl::ir::TypeCode::F16:           mangled.append("f16"); break;
                case dgtl::ir::TypeCode::F32:           mangled.append("f32"); break;
                case dgtl::ir::TypeCode::F64:           mangled.append("f64"); break;
                case dgtl::ir::TypeCode::F128:          mangled.append("f128"); break;
                case dgtl::ir::TypeCode::VEC3_F32:      mangled.append("v3f32"); break;
                case dgtl::ir::TypeCode::VEC4_F32:      mangled.append("v4f32"); break;
                case dgtl::ir::TypeCode::MAT4X4_F32:    mangled.append("m4x4f32"); break;
                case dgtl::ir::TypeCode::QUAT_F32:      mangled.append("qf32"); break;
                case dgtl::ir::TypeCode::VECTOR_F32X16: mangled.append("v16f32"); break;
                case dgtl::ir::TypeCode::VECTOR_I32X16: mangled.append("v16i32"); break;
                case dgtl::ir::TypeCode::VECTOR_F16X32: mangled.append("v32f16"); break;
                case dgtl::ir::TypeCode::TENSOR_FP8:    mangled.append("tfp8"); break;
                case dgtl::ir::TypeCode::TENSOR_BF16:   mangled.append("tbf16"); break;
                case dgtl::ir::TypeCode::TENSOR_F32:    mangled.append("tf32"); break;
                case dgtl::ir::TypeCode::TENSOR_I64:    mangled.append("ti64"); break;
                default:                                mangled.append("any"); break;
            }
        }
        return mangled;
    }

private:
    dgtl::core::DGTL_Arena& arena_;
    SymbolTable& symbol_table_;
    TypeSystem& type_system_;

    std::vector<std::string_view> active_namespaces_;
    std::vector<std::string_view> using_namespaces_;
    std::vector<FunctionOverloadCandidate> overload_candidates_;

    [[nodiscard]] std::string_view intern_string(std::string_view src) noexcept {
        if (src.empty()) return "";
        char* mem = arena_.allocate<char>(src.size() + 1);
        if (!mem) return src;
        std::memcpy(mem, src.data(), src.size());
        mem[src.size()] = '\0';
        return std::string_view(mem, src.size());
    }
};

} // namespace dgtl::hir::semantic

export namespace dgtl::semantic {
    using namespace dgtl::hir::semantic;
    using FunctionOverloadCandidate = dgtl::hir::semantic::FunctionOverloadCandidate;
    using NameResolver = dgtl::hir::semantic::NameResolver;
}

export namespace dgtl::hir {
    using namespace dgtl::hir::semantic;
    using FunctionOverloadCandidate = dgtl::hir::semantic::FunctionOverloadCandidate;
    using NameResolver = dgtl::hir::semantic::NameResolver;
}