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

#include <cstdint>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <cstdlib>
#include <vector>

#include "dgtl/all.hpp"

namespace {

#include "smoke_cli_and_options.inl"
#include "smoke_ir_builders_and_profiles.inl"
#include "smoke_release_checks.inl"
#include "smoke_benchmark_pipeline.inl"
#include "smoke_emitters_and_io.inl"

} // namespace

#include "smoke_main_entry.inl"