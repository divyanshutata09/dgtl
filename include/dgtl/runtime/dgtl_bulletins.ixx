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

#include <cstdint>
#include <string_view>

export module dgtl.dgtl_bulletins;

export namespace dgtl::runtime {

void register_dgtl_string(uint32_t handle, const char* data, uint32_t len);
[[nodiscard]] std::string_view lookup_string(uint32_t handle) noexcept;
[[nodiscard]] std::string_view try_lookup_string(uint64_t raw_value) noexcept;
[[nodiscard]] std::string_view try_lookup_string_handle(uint32_t handle) noexcept;
[[nodiscard]] std::string_view lookup_string_handle(uint32_t handle) noexcept;
void register_string_handle(uint32_t handle, const char* data, uint32_t len);

} // namespace dgtl::runtime

export extern "C" {

void __dgtl_rt_register_string(uint32_t handle, const char* data, uint32_t len);
const char* __builtin_detect_platform_name();
const char* __builtin_detect_architecture();
int64_t __builtin_detect_word_size();
int64_t __builtin_detect_endianness();
int64_t __builtin_get_page_size();
int64_t __builtin_get_cpu_count();
int64_t __builtin_get_total_memory();
int64_t __builtin_detect_simd_width_bits();
int64_t __builtin_detect_print_lane_bits();
int64_t __builtin_detect_u64_lane_count();

} // export extern "C"