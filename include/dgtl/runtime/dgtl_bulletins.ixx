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
#include <cstddef>
#include <string_view>
#include <vector>

export module dgtl.dgtl_bulletins;

export namespace dgtl::runtime {

void register_dgtl_string(uint32_t handle, const char* data, uint32_t len);
[[nodiscard]] std::string_view lookup_string(uint32_t handle) noexcept;
[[nodiscard]] std::string_view try_lookup_string(uint64_t raw_value) noexcept;
[[nodiscard]] std::string_view try_lookup_string_handle(uint32_t handle) noexcept;
[[nodiscard]] std::string_view lookup_string_handle(uint32_t handle) noexcept;
void register_string_handle(uint32_t handle, const char* data, uint32_t len);
void unregister_string_handle(uint32_t handle) noexcept;
[[nodiscard]] size_t get_registered_string_count() noexcept;
[[nodiscard]] std::vector<uint8_t> build_registered_strings_rdata();

} // namespace dgtl::runtime

export extern "C" {

void __dgtl_rt_register_string(uint32_t handle, const char* data, uint32_t len) noexcept;
void __dgtl_rt_unregister_string(uint32_t handle) noexcept;
const char* __builtin_detect_platform_name() noexcept;
const char* __builtin_detect_architecture() noexcept;
int64_t __builtin_detect_word_size() noexcept;
int64_t __builtin_detect_endianness() noexcept;
int64_t __builtin_get_page_size() noexcept;
int64_t __builtin_get_cpu_count() noexcept;
int64_t __builtin_get_total_memory() noexcept;
int64_t __builtin_detect_simd_width_bits() noexcept;
int64_t __builtin_detect_print_lane_bits() noexcept;
int64_t __builtin_detect_u64_lane_count() noexcept;

} // export extern "C"