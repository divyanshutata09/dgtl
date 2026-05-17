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
int main(int argc, char** argv) {
	try {
		AppOptions opts;
		const ParseStatus parse_status = parse_args(argc, argv, opts);
		if (parse_status == ParseStatus::help_requested) {
			return static_cast<int>(ExitCode::ok);
		}
		if (parse_status == ParseStatus::error) {
			return static_cast<int>(ExitCode::invalid_args);
		}

		if (opts.verbose) {
			std::cout << "[DGTL] Starting production smoke pipeline\n";
		}

		dgtl::core::DGTL_Arena arena(1, 64);
		dgtl::semantic::SymbolTable symbols(arena);

		if (!run_release_smoke_suite(opts, arena, symbols)) {
			std::cerr << "[DGTL] Release smoke suite failed\n";
			return static_cast<int>(ExitCode::pipeline_failure);
		}

		dgtl::ir::IRStream stream(arena, 256);
		if (!build_backend_sample_ir(stream)) {
			std::cerr << "[DGTL] Failed to build backend sample IR\n";
			return static_cast<int>(ExitCode::pipeline_failure);
		}

		run_core_optimizations(stream);
		if (stream.size() == 0 || !stream.validate_shadow_integrity()) {
			std::cerr << "[DGTL] IR validation failed after optimization\n";
			return static_cast<int>(ExitCode::pipeline_failure);
		}

		if (opts.run_evolution) {
			run_evolution_step(stream);
		}

		EmissionResult emission;
		if (!emit_backend(opts.backend,
						  stream,
						  symbols,
						  opts.verbose,
						  opts.arm64_binary_stream && opts.backend == BackendKind::arm64,
						  emission)) {
			std::cerr << "[DGTL] " << opts.backend_name
					  << " emission produced no output (code=0x"
					  << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
					  << std::dec << ")\n";
			return static_cast<int>(ExitCode::emit_failure);
		}

		std::string emitted_text = std::move(emission.emitted_text);
		std::vector<uint8_t> emitted_bytes = std::move(emission.emitted_bytes);
		const bool emitted_binary = emission.emitted_binary;

		if (!opts.asm_output_path.empty()) {
			const bool ok = emitted_binary
				? write_binary_file(opts.asm_output_path, emitted_bytes)
				: write_text_file(opts.asm_output_path, emitted_text);

			if (!ok) {
				std::cerr << "[DGTL] Failed to write output: " << opts.asm_output_path
						  << " (code=0x" << std::hex
						  << static_cast<uint32_t>(BackendErrorCode::output_write_failed)
						  << std::dec << ")\n";
				return static_cast<int>(ExitCode::io_failure);
			}
		}

		if (opts.verbose) {
			std::cout << "[DGTL] Backend=" << opts.backend_name
					  << (emitted_binary ? " (binary)" : " (text)") << "\n";
		}

		if (opts.benchmark_mode) {
			const BackendBenchmark benchmark = run_backend_benchmark(arena,
																	symbols,
																	opts.backend,
																	opts.benchmark_iterations,
																	false,
																	opts.benchmark_profile,
																	opts.pressure_vars,
																	opts.arm64_binary_stream && opts.backend == BackendKind::arm64);
			if (!benchmark.ok) {
				std::cerr << "[DGTL] Benchmark failed: " << benchmark.detail << "\n";
				return static_cast<int>(ExitCode::pipeline_failure);
			}
			std::cout << "[DGTL] Benchmark :: " << benchmark.detail << "\n";
			std::cout << "[DGTL] Polyhedral :: mode=" << opts.polyhedral_mode_name
					  << " parallel=" << opts.polyhedral_parallel_name
					  << " tile=" << opts.polyhedral_tile << "\n";
			if (opts.benchmark_dump) {
				std::cout << "[DGTL] BenchmarkDump "
						  << (benchmark.dump_is_binary ? "(hex)" : "(text)") << "\n";
				std::cout << benchmark.dump_text;
				if (!benchmark.dump_text.empty() && benchmark.dump_text.back() != '\n') {
					std::cout << '\n';
				}
			}
		}

		std::cout << "[DGTL] Nodes=" << stream.size()
				  << " PolyMode=" << opts.polyhedral_mode_name
				  << " PolyParallel=" << opts.polyhedral_parallel_name
				  << " PolyTile=" << opts.polyhedral_tile
				  << " LogicHash=0x" << std::hex << stream.generate_logic_hash() << std::dec << "\n";

		if (opts.print_asm) {
			std::cout << emitted_text;
			if (!emitted_text.empty() && emitted_text.back() != '\n') {
				std::cout << '\n';
			}
		}

		if (opts.verbose) {
			std::cout << "[DGTL] Pipeline completed successfully\n";
		}

		return static_cast<int>(ExitCode::ok);
	} catch (const std::exception& ex) {
		std::cerr << "[DGTL] Fatal exception: " << ex.what() << "\n";
		return static_cast<int>(ExitCode::internal_error);
	} catch (...) {
		std::cerr << "[DGTL] Fatal unknown exception\n";
		return static_cast<int>(ExitCode::internal_error);
	}
}