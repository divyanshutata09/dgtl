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
#pragma once

#include <array>
#include <string_view>

namespace dgtl::isa::lowering::generated {

inline constexpr std::array<std::string_view, 2> kX64CoreRuleDslLines = {
    "rule x64_scalar_fma: AddF32(MulF32($a,$b),$c) -> FMA_F32($a,$b,$c) [targets=x86_64_avx2|x86_64_avx512, benefit=100, require_fma=true]",
    "rule x64_vector_mul_add_fold: VecAddF32512(VecMulF32512($a,$b),$c) -> VEC_ADD_F32_512($a,$b,$c) [targets=x86_64_avx512, benefit=20]",
};

} // namespace dgtl::isa::lowering::generated
