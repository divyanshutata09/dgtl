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
import dgtl.ir.stream;
import dgtl.ir.io;
import dgtl.ir.gen;
import dgtl.hir.semantic;
import dgtl.ir.evolution;
import dgtl.graph.region;
import dgtl.target;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.ir_schema;
import dgtl.ir_contracts;
import dgtl.ir.analysis.cost_model;
import dgtl.ir_formal_verifier;
import dgtl.ir.analysis.liveness_memory_dependency_unified;
import dgtl.ir.pipeline.pipeline_probes;
import dgtl.ir.pipeline.pass_manager_analysis;
import dgtl.ir.pipeline.pass_manager_orchestrator;
import dgtl.ir.pass.transform_scalar_core;
import dgtl.ir.pass.transform_control_predication;
import dgtl.ir.pass.speculate_alias_dce_inliner;
import dgtl.ir.pass.simd_gather_scatter;
import dgtl.jit.executor;
import dgtl.jit.hardware_scheduler;
import dgtl.runtime.task_scheduler;
import dgtl.isa.arm64.arm64_machine_form_lowering;
import dgtl.isa.x64.nasm_emitter;
import dgtl.isa.x64.x64_machine_form_lowering;
import dgtl.isa.x64.x64_reg_map;
import dgtl.isa.x64.x64_graphcoloring_regalloc;
import dgtl.isa.x64.target;
import dgtl.mir.regalloc.universal;
import dgtl.isa.wasm.encoder;
import dgtl.linker.backend;
import dgtl.linker.driver.api;
import dgtl.linker.driver;
import dgtl.linker.utils;

namespace dgtl::backend::driver {
    using namespace dgtl::linker::driver;
}
namespace dgtl::backend::driver::shared {
    using namespace dgtl::linker::driver::shared;
}

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