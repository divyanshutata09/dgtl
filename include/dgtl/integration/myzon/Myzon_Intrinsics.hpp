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

#include <string_view>
#include <vector>

#include "dgtl/core/ABI.hpp"

namespace dgtl::integration::myzon {

[[nodiscard]] inline std::string_view canonicalize_myzon_alias(std::string_view name) noexcept {
    if (name == "__myzon_runtime_print_str") return "__dgtl_rt_print_str";
    if (name == "__myzon_runtime_print_i64") return "__dgtl_rt_print_i64";
    if (name == "__myzon_runtime_print_i64_auto") return "__dgtl_rt_print_i64_auto";
    if (name == "__myzon_runtime_print_i32") return "__dgtl_rt_print_i32";
    if (name == "__myzon_runtime_print_f64") return "__dgtl_rt_print_f64";
    if (name == "__myzon_runtime_print_f32") return "__dgtl_rt_print_f32";
    if (name == "__myzon_runtime_print_fmt_str") return "__dgtl_rt_print_fmt_str";
    if (name == "__myzon_runtime_print_fmt_str_i64x2") return "__dgtl_rt_print_fmt_str_i64x2";
    if (name == "__myzon_runtime_print_fmt_str_i64x3") return "__dgtl_rt_print_fmt_str_i64x3";
    if (name == "__myzon_runtime_print_fmt_str_i64x4") return "__dgtl_rt_print_fmt_str_i64x4";
    if (name == "__myzon_runtime_print_fmt_str_i64x5") return "__dgtl_rt_print_fmt_str_i64x5";
    if (name == "__myzon_runtime_print_fmt_str_i64x6") return "__dgtl_rt_print_fmt_str_i64x6";
    if (name == "__myzon_runtime_print_fmt_str_i64x7") return "__dgtl_rt_print_fmt_str_i64x7";
    if (name == "__myzon_runtime_print_fmt_str_i64x8") return "__dgtl_rt_print_fmt_str_i64x8";
    if (name == "__myzon_runtime_print_i64x2") return "__dgtl_rt_print_i64x2";
    if (name == "__myzon_runtime_print_i64x4") return "__dgtl_rt_print_i64x4";
    if (name == "__myzon_runtime_print_i64x8") return "__dgtl_rt_print_i64x8";
    if (name == "__myzon_runtime_flush") return "__dgtl_rt_flush";
    if (name == "__myzon_log_info") return "__dgtl_rt_log_info";
    if (name == "__myzon_log_warn") return "__dgtl_rt_log_warn";
    if (name == "__myzon_log_error") return "__dgtl_rt_log_error";
    if (name == "__myzon_log_debug") return "__dgtl_rt_log_debug";
    if (name == "__myzon_log_flush") return "__dgtl_rt_log_flush";
    if (name == "__myzon_trace_fn_entry") return "__dgtl_rt_trace_fn_entry";
    if (name == "__myzon_trace_fn_exit") return "__dgtl_rt_trace_fn_exit";
    if (name == "__myzon_trace_loop_iter") return "__dgtl_rt_trace_loop_iter";
    if (name == "__myzon_trace_alloc") return "__dgtl_rt_trace_alloc";
    if (name == "__myzon_trace_free") return "__dgtl_rt_trace_free";
    if (name == "__myzon_trace_user_event") return "__dgtl_rt_trace_user_event";
    if (name == "__myzon_trace_checkpoint") return "__dgtl_rt_trace_checkpoint";
    if (name == "__myzon_alloc_huge") return "__dgtl_rt_alloc_huge";
    if (name == "__myzon_free") return "__dgtl_rt_free";
    return name;
}

[[nodiscard]] inline std::string_view migrate_myzon_intrinsic(std::string_view name) noexcept {
    if (name.find("__myzon_write_") == 0) {
        if (name == "__myzon_write_i8" || name == "__myzon_write_i16" ||
            name == "__myzon_write_i32" || name == "__myzon_write_i64" ||
            name == "__myzon_write_u8" || name == "__myzon_write_u16" ||
            name == "__myzon_write_u32" || name == "__myzon_write_u64" ||
            name == "__myzon_write_bool" || name == "__myzon_write_char") {
            return "__dgtl_rt_print_i64";
        }
        if (name == "__myzon_write_f32") {
            return "__dgtl_rt_print_f32";
        }
        if (name == "__myzon_write_f64") {
            return "__dgtl_rt_print_f64";
        }
        if (name == "__myzon_write_string") {
            return "__dgtl_rt_print_str";
        }
    }

    if (name.find("__myzon_") == 0) {
        return canonicalize_myzon_alias(name);
    }
    return name;
}

[[nodiscard]] inline bool is_myzon_intrinsic(std::string_view callee) noexcept {
    if (callee.empty()) return false;
        return callee.find("__myzon_runtime_print_") == 0 ||
           callee == "__myzon_runtime_flush" ||
            callee == "__myzon_alloc_huge" ||
            callee == "__myzon_free" ||
           callee.find("__myzon_log_") == 0 ||
           callee.find("__myzon_trace_") == 0;
}

inline void install_myzon_intrinsic_hooks() noexcept {
    dgtl::core::set_external_intrinsic_hooks(&migrate_myzon_intrinsic, &is_myzon_intrinsic);
}

inline void register_myzon_runtime_abi_signatures(dgtl::core::DGTLABIRegistry& abi) {
    using dgtl::core::DGTLABIParameter;
    using dgtl::core::DGTLABIValueClass;

    DGTLABIParameter size_param{"size", DGTLABIValueClass::i64, 0xFF, 0, 0};
    DGTLABIParameter ptr_param{"ptr", DGTLABIValueClass::pointer, 0xFF, 0, 0};
    DGTLABIParameter value_i64_param{"value", DGTLABIValueClass::i64, 0xFF, 0, 0};
    DGTLABIParameter value_f64_param{"value", DGTLABIValueClass::f64, 0xFF, 0, 0};
    DGTLABIParameter value_f32_param{"value", DGTLABIValueClass::f32, 0xFF, 0, 0};
    DGTLABIParameter format_handle_param{"format_handle", DGTLABIValueClass::i64, 0xFF, 0, 0};
    DGTLABIParameter value_i32_param{"value", DGTLABIValueClass::i32, 0xFF, 0, 0};
    DGTLABIParameter msg_param{"msg", DGTLABIValueClass::pointer, 0xFF, 0, 0};
    DGTLABIParameter fn_addr_param{"fn_addr", DGTLABIValueClass::pointer, 0xFF, 0, 0};
    DGTLABIParameter event_id_param{"event_id", DGTLABIValueClass::i32, 0xFF, 0, 0};
    DGTLABIParameter u64_lane_param{"u64_lane_count", DGTLABIValueClass::i64, 0xFF, 0, 0};

    std::vector<DGTLABIParameter> one_i64{size_param};
    std::vector<DGTLABIParameter> one_ptr{ptr_param};
    std::vector<DGTLABIParameter> print_str_params{msg_param};
    std::vector<DGTLABIParameter> print_i64_params{value_i64_param};
    std::vector<DGTLABIParameter> print_i64_auto_params{value_i64_param};
    std::vector<DGTLABIParameter> print_i32_params{value_i32_param};
    std::vector<DGTLABIParameter> print_f64_params{value_f64_param};
    std::vector<DGTLABIParameter> print_f32_params{value_f32_param};
    std::vector<DGTLABIParameter> print_fmt_str_params{format_handle_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x2_params{format_handle_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x3_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x4_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x5_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x6_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x7_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_fmt_str_i64x8_params{format_handle_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_i64x2_params{value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_i64x4_params{value_i64_param, value_i64_param, value_i64_param, value_i64_param};
    std::vector<DGTLABIParameter> print_i64x8_params{value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param, value_i64_param};

    abi.register_internal_signature("__dgtl_rt_alloc_huge", DGTLABIValueClass::pointer, 0, one_i64);
    abi.register_internal_signature("__dgtl_rt_free", DGTLABIValueClass::i32, 0, one_ptr);
    abi.register_internal_signature("__dgtl_rt_print_str", DGTLABIValueClass::none, 0, print_str_params);
    abi.register_internal_signature("__dgtl_rt_print_i64", DGTLABIValueClass::none, 0, print_i64_params);
    abi.register_internal_signature("__dgtl_rt_print_i64_auto", DGTLABIValueClass::none, 0, print_i64_auto_params);
    abi.register_internal_signature("__dgtl_rt_print_i32", DGTLABIValueClass::none, 0, print_i32_params);
    abi.register_internal_signature("__dgtl_rt_print_f64", DGTLABIValueClass::none, 0, print_f64_params);
    abi.register_internal_signature("__dgtl_rt_print_f32", DGTLABIValueClass::none, 0, print_f32_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str", DGTLABIValueClass::none, 0, print_fmt_str_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x2", DGTLABIValueClass::none, 0, print_fmt_str_i64x2_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x3", DGTLABIValueClass::none, 0, print_fmt_str_i64x3_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x4", DGTLABIValueClass::none, 0, print_fmt_str_i64x4_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x5", DGTLABIValueClass::none, 0, print_fmt_str_i64x5_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x6", DGTLABIValueClass::none, 0, print_fmt_str_i64x6_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x7", DGTLABIValueClass::none, 0, print_fmt_str_i64x7_params);
    abi.register_internal_signature("__dgtl_rt_print_fmt_str_i64x8", DGTLABIValueClass::none, 0, print_fmt_str_i64x8_params);
    abi.register_internal_signature("__dgtl_rt_print_i64x2", DGTLABIValueClass::none, 0, print_i64x2_params);
    abi.register_internal_signature("__dgtl_rt_print_i64x4", DGTLABIValueClass::none, 0, print_i64x4_params);
    abi.register_internal_signature("__dgtl_rt_print_i64x8", DGTLABIValueClass::none, 0, print_i64x8_params);
    abi.register_internal_signature("__dgtl_rt_flush", DGTLABIValueClass::none, 0, std::vector<DGTLABIParameter>{});

    std::vector<DGTLABIParameter> log_params{msg_param};
    abi.register_internal_signature("__dgtl_rt_log_info", DGTLABIValueClass::none, 0, log_params);
    abi.register_internal_signature("__dgtl_rt_log_warn", DGTLABIValueClass::none, 0, log_params);
    abi.register_internal_signature("__dgtl_rt_log_error", DGTLABIValueClass::none, 0, log_params);
    abi.register_internal_signature("__dgtl_rt_log_debug", DGTLABIValueClass::none, 0, log_params);
    abi.register_internal_signature("__dgtl_rt_log_flush", DGTLABIValueClass::none, 0, std::vector<DGTLABIParameter>{});

    abi.register_internal_signature("__builtin_detect_platform_name", DGTLABIValueClass::pointer, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_architecture", DGTLABIValueClass::pointer, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_word_size", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_endianness", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_get_page_size", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_get_cpu_count", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_get_total_memory", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_simd_width_bits", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_print_lane_bits", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});
    abi.register_internal_signature("__builtin_detect_u64_lane_count", DGTLABIValueClass::i64, 0, std::vector<DGTLABIParameter>{});

    std::vector<DGTLABIParameter> trace_fn_params{fn_addr_param, u64_lane_param};
    std::vector<DGTLABIParameter> trace_loop_params{event_id_param, event_id_param};
    std::vector<DGTLABIParameter> trace_alloc_params{ptr_param, size_param};
    std::vector<DGTLABIParameter> trace_free_params{ptr_param};
    std::vector<DGTLABIParameter> trace_user_event_params{event_id_param, ptr_param, value_i32_param};
    std::vector<DGTLABIParameter> trace_checkpoint_params{event_id_param};

    abi.register_internal_signature("__dgtl_rt_trace_fn_entry", DGTLABIValueClass::none, 0, trace_fn_params);
    abi.register_internal_signature("__dgtl_rt_trace_fn_exit", DGTLABIValueClass::none, 0, trace_fn_params);
    abi.register_internal_signature("__dgtl_rt_trace_loop_iter", DGTLABIValueClass::none, 0, trace_loop_params);
    abi.register_internal_signature("__dgtl_rt_trace_alloc", DGTLABIValueClass::none, 0, trace_alloc_params);
    abi.register_internal_signature("__dgtl_rt_trace_free", DGTLABIValueClass::none, 0, trace_free_params);
    abi.register_internal_signature("__dgtl_rt_trace_user_event", DGTLABIValueClass::none, 0, trace_user_event_params);
    abi.register_internal_signature("__dgtl_rt_trace_checkpoint", DGTLABIValueClass::none, 0, trace_checkpoint_params);
}

} // namespace dgtl::integration::myzon
