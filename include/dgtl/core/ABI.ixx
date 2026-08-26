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
};

enum class DGTLCallValidationMode : uint8_t {
    exact_arity,
    at_least_arity,
    skip_arity
};

struct DGTLCallValidationOptions {
    DGTLCallValidationMode mode = DGTLCallValidationMode::exact_arity;
    bool allow_missing_signature = false;
    bool allow_runtime_intrinsic_bypass = false;
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

    [[nodiscard]] static constexpr DGTLCallValidationOptions lenient_runtime_intrinsics() noexcept {
        return DGTLCallValidationOptions{
            DGTLCallValidationMode::exact_arity,
            false,
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

using DGTLExternalIntrinsicMigrator = std::string_view(*)(std::string_view) noexcept;
using DGTLExternalIntrinsicPredicate = bool(*)(std::string_view) noexcept;

inline DGTLExternalIntrinsicMigrator g_external_intrinsic_migrator = nullptr;
inline DGTLExternalIntrinsicPredicate g_external_intrinsic_predicate = nullptr;

inline void set_external_intrinsic_hooks(DGTLExternalIntrinsicMigrator migrator,
                                         DGTLExternalIntrinsicPredicate predicate) noexcept {
    g_external_intrinsic_migrator = migrator;
    g_external_intrinsic_predicate = predicate;
}

/**
 * @brief Canonical runtime intrinsic migrator and name normalizer.
 * Keeps DGTL core language-agnostic while supporting external language adapters
 * through pluggable hook callbacks.
 */
[[nodiscard]] inline std::string_view migrate_callee_name(std::string_view name) noexcept {
    if (name.empty() || name[0] != '_') return name;

    if (g_external_intrinsic_migrator != nullptr) {
        const std::string_view migrated = g_external_intrinsic_migrator(name);
        if (!migrated.empty()) {
            return migrated;
        }
    }

    if (name.starts_with("__builtin_detect_") || name.starts_with("__builtin_get_")) {
        return name;
    }

    return name;
}

/**
 * @brief Recognize runtime intrinsics (platform + adapter-provided domains).
 * Used to skip signature validation or apply lenient validation.
 */
[[nodiscard]] inline bool is_runtime_intrinsic(std::string_view callee) noexcept {
    if (callee.empty()) return false;

    if (callee.starts_with("__dgtl_rt_")) {
        return true;
    }

    if (callee.starts_with("__builtin_detect_") || callee.starts_with("__builtin_get_")) {
        return true;
    }

    if (g_external_intrinsic_predicate != nullptr && g_external_intrinsic_predicate(callee)) {
        return true;
    }

    return false;
}

[[nodiscard]] inline bool is_legacy_runtime_intrinsic(std::string_view callee) noexcept {
    return is_runtime_intrinsic(callee);
}

/**
 * @brief HFT-Upgraded Concurrency Boundary Check
 * Allocation-free, case-insensitive string parsing.
 */
[[nodiscard]] inline bool is_concurrency_boundary_callee(std::string_view callee) noexcept {
    if (callee.empty()) return false;

    if (callee.starts_with("__dgtl_rt_")) {
        auto contains_ascii_nocase = [](std::string_view haystack, std::string_view needle) {
            if (needle.empty() || haystack.size() < needle.size()) return false;
            for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
                bool ok = true;
                for (size_t j = 0; j < needle.size(); ++j) {
                    const unsigned char hc = static_cast<unsigned char>(haystack[i + j]);
                    const unsigned char nc = static_cast<unsigned char>(needle[j]);
                    if (std::tolower(hc) != std::tolower(nc)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) return true;
            }
            return false;
        };

        // Runtime namespace evolves with .dgtl_def/.dgtl_isel; keep this matcher semantic.
        if (contains_ascii_nocase(callee, "thread") ||
            contains_ascii_nocase(callee, "spawn") ||
            contains_ascii_nocase(callee, "yield") ||
            contains_ascii_nocase(callee, "fiber") ||
            contains_ascii_nocase(callee, "suspend") ||
            contains_ascii_nocase(callee, "resume") ||
            contains_ascii_nocase(callee, "atomic") ||
            contains_ascii_nocase(callee, "lock") ||
            contains_ascii_nocase(callee, "spin") ||
            contains_ascii_nocase(callee, "barrier") ||
            contains_ascii_nocase(callee, "fence") ||
            contains_ascii_nocase(callee, "chan") ||
            contains_ascii_nocase(callee, "ownership") ||
            contains_ascii_nocase(callee, "ring")) {
            return true;
        }
        return false;
    }

    // Zero-allocation case-insensitive compare
    auto equals_ignore_case = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != 
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };

    if (equals_ignore_case(callee, "spawn") || equals_ignore_case(callee, "async") ||
        equals_ignore_case(callee, "fiber") || equals_ignore_case(callee, "thread") ||
        equals_ignore_case(callee, "yield") || equals_ignore_case(callee, "lock") ||
        equals_ignore_case(callee, "unlock") || equals_ignore_case(callee, "release") ||
        equals_ignore_case(callee, "fence") || equals_ignore_case(callee, "barrier") ||
        equals_ignore_case(callee, "atomic_cas") || equals_ignore_case(callee, "atomic_add") ||
        equals_ignore_case(callee, "atomic_sub")) {
        return true;
    }

    // Fast substring check for "lock"
    for (size_t i = 0; i + 3 < callee.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(callee[i])) == 'l' &&
            std::tolower(static_cast<unsigned char>(callee[i+1])) == 'o' &&
            std::tolower(static_cast<unsigned char>(callee[i+2])) == 'c' &&
            std::tolower(static_cast<unsigned char>(callee[i+3])) == 'k') {
            return true;
        }
    }

    return false;
}

class DGTLABIRegistry {
public:
    void register_signature(DGTLCallSignature signature) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::string migrated_name(migrate_callee_name(signature.callee));
        signatures_.insert_or_assign(std::move(migrated_name), std::move(signature));
    }

    [[nodiscard]] const DGTLCallSignature* find_signature(std::string_view callee) const noexcept {
        std::string_view actual_callee = migrate_callee_name(callee);
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
        std::string_view actual_callee = migrate_callee_name(callee);

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
        
        if (options.allow_runtime_intrinsic_bypass && is_runtime_intrinsic(actual_callee)) {
            return true;
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