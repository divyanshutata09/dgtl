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
#include <thread>

import dgtl.arena;
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir_core.stream;
import dgtl.ir_core.io;
import dgtl.ir_core.gen;
import dgtl.ir_core.semantic;
import dgtl.ir_core.evolution;
import dgtl.ir_core.graph_region;
import dgtl.target;
import dgtl.ir_core.ai_cost_model;
import dgtl.ir_core.verified_formal;
import dgtl.ir_core.liveness_memory_dependency_unified;
import dgtl.ir_core.pipeline_probes;
import dgtl.ir_core.pass_manager_analysis;
import dgtl.ir_core.pass_manager_orchestrator;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.transform_control_predication;
import dgtl.ir_core.speculate_alias_dce_inliner;
import dgtl.ir_core.simd_gather_scatter;
import dgtl.ir_core.jit_executor;
import dgtl.ir_core.hardware_scheduler;
import dgtl.task_scheduler;
import dgtl.ir_core.arm64_emitter;
import dgtl.ir_core.nasm_emitter;
import dgtl.ir_core.wasm_encoder;
import dgtl.ir_core.macho_emitter;
import dgtl.ir_core.linker_backend;
import dgtl.ir_core.backend_driver_api;
import dgtl.ir_core.backend_driver_shared_utils;
import dgtl.ir_core.x64_lowering;
import dgtl.ir_core.arm64_regalloc;

namespace {

inline float estimate_stream_cost(dgtl::analysis::DGTLAICostModel& model,
								  const dgtl::ir::IRStream& stream) {
	float total = 0.0f;
	for (size_t i = 0; i < stream.size(); ++i) {
		total += model.predict_node_cost(stream.get_node_readonly(i));
	}
	return total;
}

#include "smoke_cli_and_options.inl"
#include "smoke_ir_builders_and_profiles.inl"
#include "smoke_emitters_and_io.inl"
#include "smoke_release_checks.inl"
#include "smoke_benchmark_pipeline.inl"

} // namespace

#include "smoke_main_entry.inl"