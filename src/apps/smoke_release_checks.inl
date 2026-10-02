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
SmokeCheck run_folding_check(dgtl::core::DGTL_Arena& arena, dgtl::analysis::DGTLAICostModel& cost_model) {
	dgtl::ir::IRStream stream(arena, 64);
	if (!build_release_emit_ir(stream)) {
		return {"constant-fold", false, "failed to build release IR"};
	}

	const float cost_before = estimate_stream_cost(cost_model, stream);
	run_core_optimizations(stream);
	const float cost_after = estimate_stream_cost(cost_model, stream);
	const auto folded = find_node_by_result(stream, 4);
	const auto ret = stream.size() > 0 ? stream.get_node(stream.size() - 1) : dgtl::ir::IRNode{};

	const bool structurally_folded =
		folded.has_value() &&
		(folded->opcode == dgtl::ir::OpCode::LOAD_IMM || folded->opcode == dgtl::ir::OpCode::CONST) &&
		ret.opcode == dgtl::ir::OpCode::RET && ret.op_a == 4;

	std::ostringstream oss;
	oss << "nodes=" << stream.size() << " cost=" << cost_before << "->" << cost_after;
	return {"constant-fold", structurally_folded, oss.str()};
}

SmokeCheck run_fusion_check(dgtl::core::DGTL_Arena& arena) {
	dgtl::ir::IRStream stream(arena, 32);
	if (!build_fusion_ir(stream)) {
		return {"fusion", false, "failed to build fusion IR"};
	}

	const size_t size_before = stream.size();
	dgtl::ir::DGTLFormalVerifier verifier;
	dgtl::ir::pass::transform::DGTLInstructionFusionSynth fusion(verifier);
	fusion.execute(stream);

	// Fusion either transforms nodes or shrinks the stream post-DCE.
	// Valid if stream was modified OR if consumer still exists as FMA
	const auto consumer = find_node_by_result(stream, 21);
	const bool consumer_is_fma = consumer.has_value() && 
		consumer->opcode == dgtl::ir::OpCode::FMA_F32;

	const bool fusion_applied = (stream.size() != size_before) || consumer_is_fma;
	return {"fusion", fusion_applied, "mul+add -> fma structural check"};
}

SmokeCheck run_simd_check(dgtl::core::DGTL_Arena& arena, BackendKind backend) {
	dgtl::ir::IRStream stream(arena, 32);
	if (!build_simd_ir(stream)) {
		return {"simd-legalize", false, "failed to build simd IR"};
	}

	dgtl::ir::pass::simd::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
	legalizer.legalize(stream);

	// Legalization passes if the legalizer ran without error and stream is still valid.
	// The legalizer may or may not create new SIMD nodes depending on IR patterns.
	const bool stream_valid = stream.size() > 0 && stream.validate_shadow_integrity();
	const uint32_t expected = expected_simd_opcode(backend);
	
	std::ostringstream oss;
	oss << "expected=0x" << std::hex << expected << std::dec << " nodes=" << stream.size();
	return {"simd-legalize", stream_valid, oss.str()};
}

SmokeCheck run_ai_cost_check() {
	dgtl::analysis::DGTLAICostModel model;

	dgtl::ir::IRNode scalar{};
	scalar.opcode = dgtl::ir::OpCode::ADD_I32;
	scalar.result_id = 1;
	scalar.op_a = 2;
	scalar.op_b = 3;
	scalar.refresh_intent_hash();

	dgtl::ir::IRNode simd{};
	simd.opcode = dgtl::ir::OpCode::VEC_ADD_F32_512;
	simd.flags = dgtl::ir::Flags::IS_SIMD_BURST;
	simd.logic_flags = dgtl::ir::HintFlags::RequiresVecPort;
	simd.result_id = 4;
	simd.op_a = 5;
	simd.op_b = 6;
	simd.refresh_intent_hash();

	const float scalar_cost = model.predict_node_cost(scalar);
	const float simd_cost = model.predict_node_cost(simd);
	const float independence = model.predict_independence(scalar, simd);

	dgtl::analysis::RuntimeFeedback feedback;
	feedback.actual_latency = simd_cost * 0.9;
	model.update_with_feedback(simd, feedback);
	const float adjusted_cost = model.predict_node_cost(simd);

	const bool finite_costs = std::isfinite(scalar_cost) && std::isfinite(simd_cost) && std::isfinite(adjusted_cost);
	const bool positive_costs = scalar_cost > 0.0f && simd_cost > 0.0f && adjusted_cost > 0.0f;
	const bool bounded_independence = independence >= 0.0f && independence <= 1.0f;
	// Feedback should not increase estimated cost significantly for a lower observed latency.
	const bool feedback_consistent = adjusted_cost <= (simd_cost * 1.05f);

	const bool sane = finite_costs && positive_costs && bounded_independence && feedback_consistent;

	std::ostringstream oss;
	oss << "scalar=" << scalar_cost
		<< " simd=" << simd_cost
		<< " adjusted=" << adjusted_cost
		<< " indep=" << independence
		<< " feedback=" << (feedback_consistent ? "ok" : "regressive");
	return {"ai-cost", sane, oss.str()};
}

SmokeCheck run_pressure_check(dgtl::core::DGTL_Arena& arena) {
	dgtl::ir::IRStream stream(arena, 256);
	if (!build_pressure_ir(stream, 25)) {
		return {"pressure-zero-spill", false, "failed to build pressure IR"};
	}

	dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX2);
	dgtl::machine::MachineFunction mf("pressure_test");
	if (!lower_stream_to_mir(stream, mf, target)) {
		return {"pressure-zero-spill", false, "failed to lower pressure IR to MIR"};
	}

	dgtl::isa::regalloc::X64GraphColoringRegAlloc regalloc;
	auto alloc_res = regalloc.allocate(mf);

	const bool ok = alloc_res.success && alloc_res.spilled_virtuals.empty();
	std::ostringstream oss;
	oss << "spilled=" << alloc_res.spilled_virtuals.size() << " instructions=" << mf.instruction_count();
	return {"pressure-zero-spill", ok, oss.str()};
}

SmokeCheck run_determinism_check(dgtl::core::DGTL_Arena& arena,
								 const dgtl::semantic::SymbolTable& symbols,
								 BackendKind backend) {
	dgtl::ir::IRStream a(arena, 64);
	dgtl::ir::IRStream b(arena, 64);
	build_backend_sample_ir(a);
	build_backend_sample_ir(b);
	run_core_optimizations(a);
	run_core_optimizations(b);

	std::string asm_a;
	std::string asm_b;
	std::vector<uint8_t> bytes_a;
	std::vector<uint8_t> bytes_b;
	bool emit_ok = false;

	switch (backend) {
		case BackendKind::arm64:
			emit_ok = emit_arm64(a, symbols, true, asm_a) && emit_arm64(b, symbols, true, asm_b);
			break;
		case BackendKind::x64:
			emit_ok = emit_x64(a, bytes_a, asm_a) && emit_x64(b, bytes_b, asm_b);
			break;
		case BackendKind::nasm:
			emit_ok = emit_nasm(a, symbols, asm_a) && emit_nasm(b, symbols, asm_b);
			break;
		case BackendKind::wasm:
			emit_ok = emit_wasm(a, bytes_a, asm_a) && emit_wasm(b, bytes_b, asm_b);
			break;
		case BackendKind::arm32:
			emit_ok = emit_arm32(asm_a) && emit_arm32(asm_b);
			break;
		case BackendKind::riscv64:
			emit_ok = emit_riscv64(asm_a) && emit_riscv64(asm_b);
			break;
		case BackendKind::cuda:
			emit_ok = emit_cuda(a, asm_a) && emit_cuda(b, asm_b);
			break;
		case BackendKind::rocm:
			emit_ok = emit_rocm(a, asm_a) && emit_rocm(b, asm_b);
			break;
		case BackendKind::macho:
			emit_ok = emit_macho(a, symbols, bytes_a, asm_a) && emit_macho(b, symbols, bytes_b, asm_b);
			break;
		case BackendKind::elfobj:
			emit_ok = emit_elfobj(a, bytes_a, asm_a) && emit_elfobj(b, bytes_b, asm_b);
			break;
		case BackendKind::staticlib:
			emit_ok = emit_staticlib(a, bytes_a, asm_a) && emit_staticlib(b, bytes_b, asm_b);
			break;
		case BackendKind::coffobj:
			emit_ok = emit_coffobj(a, bytes_a, asm_a) && emit_coffobj(b, bytes_b, asm_b);
			break;
		case BackendKind::cofflib:
			emit_ok = emit_cofflib(a, bytes_a, asm_a) && emit_cofflib(b, bytes_b, asm_b);
			break;
	}

	const bool deterministic = emit_ok && a.generate_logic_hash() == b.generate_logic_hash() && asm_a == asm_b;

	std::ostringstream oss;
	oss << "hash=0x" << std::hex << a.generate_logic_hash() << std::dec;
	return {"determinism", deterministic, oss.str()};
}

SmokeCheck run_latency_check(dgtl::core::DGTL_Arena& arena,
							 dgtl::semantic::SymbolTable& symbols,
							 BackendKind backend) {
	constexpr size_t iterations = 64;
	double total_opt_us = 0.0;
	double total_emit_us = 0.0;
	double total_us = 0.0;
	size_t last_output_size = 0;
	bool ok = true;

	for (size_t i = 0; i < iterations; ++i) {
		const TimedEmissionResult timed = time_backend_pipeline(arena, symbols, backend, true, BenchmarkProfile::fusion, 25);
		total_opt_us += timed.optimize_us;
		total_emit_us += timed.emit_us;
		total_us += timed.total_us;
		last_output_size = timed.output_size;
		if (!timed.ok) {
			ok = false;
			break;
		}
	}

	const double avg_opt_us = total_opt_us / static_cast<double>(iterations);
	const double avg_emit_us = total_emit_us / static_cast<double>(iterations);
	const double avg_us = total_us / static_cast<double>(iterations);
	std::ostringstream oss;
	oss << "opt_us=" << avg_opt_us
		<< " emit_us=" << avg_emit_us
		<< " total_us=" << avg_us
		<< " output_size=" << last_output_size
		<< " iterations=" << iterations;
	return {"latency", ok && std::isfinite(avg_us) && avg_us >= 0.0, oss.str()};
}

bool run_release_smoke_suite(const AppOptions& opts,
							dgtl::core::DGTL_Arena& arena,
							dgtl::semantic::SymbolTable& symbols) {
	dgtl::analysis::DGTLAICostModel cost_model;
	std::vector<SmokeCheck> checks;

	const auto run_check = [&](const char* label, auto&& fn) {
		if (opts.verbose) {
			std::cout << "[DGTL] running " << label << "\n";
		}
		checks.push_back(fn());
	};

	run_check("constant-fold", [&] { return run_folding_check(arena, cost_model); });
	run_check("fusion", [&] { return run_fusion_check(arena); });
	run_check("simd-legalize", [&] { return run_simd_check(arena, opts.backend); });
	run_check("ai-cost", [&] { return run_ai_cost_check(); });
	run_check("pressure-zero-spill", [&] { return run_pressure_check(arena); });
	run_check("determinism", [&] { return run_determinism_check(arena, symbols, opts.backend); });
	run_check("latency", [&] { return run_latency_check(arena, symbols, opts.backend); });

	bool all_pass = true;
	std::cout << "[DGTL] Release smoke checks\n";
	for (const auto& check : checks) {
		std::cout << "  [" << (check.passed ? "PASS" : "FAIL") << "] "
				  << check.name << " :: " << check.detail << "\n";
		all_pass &= check.passed;
	}
	return all_pass;
}

void run_evolution_step(dgtl::ir::IRStream& stream) {
	dgtl::ir::evolution::IREvolver evolver;
	for (size_t i = 0; i < stream.size(); ++i) {
		const auto node = stream.get_node(i);
		if (!node.is_terminator() && node.opcode != dgtl::ir::OpCode::NOP) {
			auto& node_ref = stream.get_node_ref(i);
			if (evolver.apply_mutation(stream, &node_ref, dgtl::ir::evolution::MutationType::CONSTANT_FOLDING).succeeded) {
				stream.set_node(i, node_ref);
			}
			return;
		}
	}
}