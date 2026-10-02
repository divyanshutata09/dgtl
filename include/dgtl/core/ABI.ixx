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

module;

#include <cstdlib>

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <functional>

export module dgtl.ir_core.abi;

export namespace dgtl::core {

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
    [[maybe_unused]] std::string name;
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
    bool is_runtime_intrinsic = false;
    bool is_concurrency_boundary = false;
};

enum class DGTLCallValidationMode : uint8_t {
    exact_arity,
    at_least_arity,
    skip_arity
};

struct DGTLCallValidationOptions {
    DGTLCallValidationMode mode = DGTLCallValidationMode::exact_arity;
    bool allow_missing_signature = false;
    bool allow_runtime_intrinsic_contract = false;
    // Safety budget for total outbound ABI argument storage.
    // Effective max arg count = min(kDGTLAbiMaxCallArgsHard, arg_memory_budget_bytes / 8).
    size_t arg_memory_budget_bytes = 2048;

    [[nodiscard]] static constexpr DGTLCallValidationOptions strict_production() noexcept {
        return DGTLCallValidationOptions{
            DGTLCallValidationMode::exact_arity,
            false,
            false,
            2048
        };
    }

    [[nodiscard]] static constexpr DGTLCallValidationOptions runtime_intrinsic_validation() noexcept {
        return DGTLCallValidationOptions{
            DGTLCallValidationMode::exact_arity,
            false,
            true,
            2048
        };
    }

    [[nodiscard]] static constexpr DGTLCallValidationOptions lenient_runtime_intrinsics() noexcept {
        return DGTLCallValidationOptions{
            DGTLCallValidationMode::at_least_arity,
            true,
            true,
            2048
        };
    }
};

inline constexpr size_t kDGTLAbiMaxCallArgsHard = 256;
inline constexpr size_t kDGTLAbiArgBytesFloor = sizeof(uint64_t);

[[nodiscard]] inline size_t max_abi_args_for_budget(size_t budget_bytes) noexcept {
    const size_t by_memory = budget_bytes / kDGTLAbiArgBytesFloor;
    return std::min(kDGTLAbiMaxCallArgsHard, by_memory);
}

// --- C++20 Transparent Hashing for Zero-Allocation ABI Lookups ---
struct ABIStringViewHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct ABIStringViewEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

using DGTLCallNameNormalizer = std::string_view(*)(std::string_view) noexcept;

inline DGTLCallNameNormalizer g_call_name_normalizer = nullptr;

inline void set_call_name_normalizer(DGTLCallNameNormalizer normalizer) noexcept {
    g_call_name_normalizer = normalizer;
}

[[nodiscard]] inline std::string_view canonical_callee_name(std::string_view name) noexcept {
    if (g_call_name_normalizer != nullptr) {
        const std::string_view normalized = g_call_name_normalizer(name);
        if (!normalized.empty()) return normalized;
    }
    return name;
}

class DGTLABIRegistry {
public:
    void register_signature(DGTLCallSignature signature) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::string migrated_name(canonical_callee_name(signature.callee));
        signatures_.insert_or_assign(std::move(migrated_name), std::move(signature));
    }

    [[nodiscard]] const DGTLCallSignature* find_signature(std::string_view callee) const noexcept {
        std::string_view actual_callee = canonical_callee_name(callee);
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        const auto it = signatures_.find(actual_callee);
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
                             std::span<const DGTLABIValueClass>{},
                             DGTLCallValidationOptions{},
                             error);
    }

    [[nodiscard]] bool validate_call(std::string_view callee,
                                     size_t arg_count,
                                     const DGTLCallValidationOptions& options,
                                     std::string* error = nullptr) const noexcept {
        return validate_call(callee,
                             arg_count,
                             std::span<const DGTLABIValueClass>{},
                             options,
                             error);
    }

    [[nodiscard]] bool validate_call(std::string_view callee,
                                     size_t arg_count,
                                     std::span<const DGTLABIValueClass> argument_classes,
                                     const DGTLCallValidationOptions& options,
                                     std::string* error = nullptr) const noexcept {
        std::string_view actual_callee = canonical_callee_name(callee);

        const size_t max_args = max_abi_args_for_budget(options.arg_memory_budget_bytes);
        if (arg_count > max_args) [[unlikely]] {
            if (error) {
                error->clear();
                error->append("ABI arg limit exceeded for callee: ").append(actual_callee)
                     .append(" (arg_count=").append(std::to_string(arg_count))
                     .append(", max=").append(std::to_string(max_args)).append(")");
            }
            return false;
        }
        
        if (options.allow_runtime_intrinsic_contract) {
            if (const DGTLCallSignature* contract = find_signature(actual_callee); contract && contract->is_runtime_intrinsic) {
                return true;
            }
        }
        
        const DGTLCallSignature* signature = find_signature(actual_callee);
        
        if (!signature) {
            if (options.allow_missing_signature) {
                return true;
            }
            if (error) {
                error->clear();
                error->append("missing ABI signature for callee: ").append(actual_callee);
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

            if (!arity_ok) [[unlikely]] {
                if (error) {
                    error->clear();
                    error->append("arity mismatch for callee: ").append(actual_callee);
                }
                return false;
            }

            if (!argument_classes.empty()) {
                const size_t comparable = std::min(expected, argument_classes.size());
                for (size_t i = 0; i < comparable; ++i) {
                    const DGTLABIValueClass actual = argument_classes[i];
                    const DGTLABIValueClass expected_cls = signature->parameters[i].value_class;
                    if (actual == DGTLABIValueClass::none || actual == DGTLABIValueClass::custom) {
                        continue;
                    }
                    if (expected_cls == DGTLABIValueClass::none || expected_cls == DGTLABIValueClass::custom) {
                        continue;
                    }
                    if (actual != expected_cls) [[unlikely]] {
                        if (error) {
                            error->clear();
                            error->append("ABI value-class mismatch for callee: ").append(actual_callee);
                        }
                        return false;
                    }
                }
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
        const size_t param_count = std::min(parameters.size(), kDGTLAbiMaxCallArgsHard);
        signature.parameters.assign(parameters.begin(), parameters.begin() + param_count);
        signature.is_variadic = is_variadic;
        signature.is_foreign = false;
        signature.is_runtime_intrinsic = false;
        signature.is_concurrency_boundary = false;
        
        register_signature(std::move(signature));
    }

    [[nodiscard]] size_t size() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return signatures_.size();
    }

private:
    alignas(64) mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, DGTLCallSignature, ABIStringViewHash, ABIStringViewEqual> signatures_;
};

} // namespace dgtl::core

