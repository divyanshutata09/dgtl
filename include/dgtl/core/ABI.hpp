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

#pragma once

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dgtl::core {

enum class DGTLCallingConvention : uint8_t {
    c_decl,
    fastcall,
    stdcall,
    vectorcall,
    dgtl_internal,
    foreign
};

enum class DGTLABIValueClass : uint8_t {
    none,
    i32,
    i64,
    f32,
    f64,
    pointer,
    aggregate,
    custom
};

struct DGTLABIParameter {
    std::string name;
    DGTLABIValueClass value_class = DGTLABIValueClass::none;
    uint8_t preferred_register = 0xFF;
    uint8_t flags = 0;
    uint64_t type_handle = 0;
};

struct DGTLCallSignature {
    std::string callee;
    DGTLCallingConvention calling_convention = DGTLCallingConvention::dgtl_internal;
    DGTLABIParameter return_value{};
    std::vector<DGTLABIParameter> parameters;
    bool is_variadic = false;
    bool is_foreign = false;
};

enum class DGTLCallValidationMode : uint8_t {
    exact_arity,
    at_least_arity,
    skip_arity
};

struct DGTLCallValidationOptions {
    DGTLCallValidationMode mode = DGTLCallValidationMode::exact_arity;
    bool allow_missing_signature = false;
};

[[nodiscard]] inline bool is_concurrency_boundary_callee(std::string_view callee) {
    std::string lower(callee);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "spawn" || lower == "async" || lower == "fiber" || lower == "thread" ||
        lower == "yield" || lower == "lock" || lower == "unlock" || lower == "release" ||
        lower == "fence" || lower == "barrier" ||
        lower == "atomic_cas" || lower == "atomic_add" || lower == "atomic_sub" ||
        lower == "__myzon_spawn" || lower == "__myzon_yield" ||
        lower == "__myzon_atomic_add" || lower == "__myzon_atomic_sub" ||
        lower == "__myzon_concurrent_op" || lower == "__myzon_select_arm") {
        return true;
    }

    return lower.find("lock") != std::string::npos;
}

class DGTLABIRegistry {
public:
    void register_signature(DGTLCallSignature signature) {
        signatures_[signature.callee] = std::move(signature);
    }

    [[nodiscard]] const DGTLCallSignature* find_signature(std::string_view callee) const noexcept {
        const auto it = signatures_.find(std::string(callee));
        return it == signatures_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::optional<size_t> expected_arity(std::string_view callee) const noexcept {
        const DGTLCallSignature* signature = find_signature(callee);
        if (!signature) {
            return std::nullopt;
        }
        return signature->parameters.size();
    }

    [[nodiscard]] bool validate_call(std::string_view callee,
                                     size_t arg_count,
                                     std::string* error = nullptr) const noexcept {
        return validate_call(callee,
                             arg_count,
                             DGTLCallValidationOptions{},
                             error);
    }

    [[nodiscard]] bool validate_call(std::string_view callee,
                                     size_t arg_count,
                                     const DGTLCallValidationOptions& options,
                                     std::string* error = nullptr) const noexcept {
        const DGTLCallSignature* signature = find_signature(callee);
        if (!signature) {
            if (options.allow_missing_signature) {
                return true;
            }
            if (error) {
                *error = "missing ABI signature for callee: " + std::string(callee);
            }
            return false;
        }

        if (!signature->is_variadic) {
            const size_t expected = signature->parameters.size();
            bool arity_ok = true;

            if (options.mode == DGTLCallValidationMode::exact_arity) {
                arity_ok = (arg_count == expected);
            } else if (options.mode == DGTLCallValidationMode::at_least_arity) {
                arity_ok = (arg_count >= expected);
            }

            if (!arity_ok) {
                if (error) {
                    *error = "arity mismatch for callee: " + std::string(callee);
                }
                return false;
            }
        }

        return true;
    }

    void register_internal_signature(std::string_view callee,
                                     DGTLABIValueClass return_class,
                                     uint64_t return_type_handle,
                                     std::span<const DGTLABIParameter> parameters,
                                     bool is_variadic = false) {
        DGTLCallSignature signature;
        signature.callee = std::string(callee);
        signature.calling_convention = DGTLCallingConvention::dgtl_internal;
        signature.return_value = DGTLABIParameter{"ret", return_class, 0xFF, 0, return_type_handle};
        signature.parameters.assign(parameters.begin(), parameters.end());
        signature.is_variadic = is_variadic;
        signature.is_foreign = false;
        register_signature(std::move(signature));
    }

    [[nodiscard]] size_t size() const noexcept {
        return signatures_.size();
    }

private:
    std::unordered_map<std::string, DGTLCallSignature> signatures_;
};

} // namespace dgtl::core