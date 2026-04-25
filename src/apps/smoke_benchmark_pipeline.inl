/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

#include "dgtl/jit/runtime/Hardware_Scheduler.hpp"
#include "dgtl/pipeline/Pass_Decision_Engine.hpp"
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#endif

/**
 * @brief Dispatches the optimized IR stream to the requested backend.
 */
bool emit_backend(BackendKind backend,
                  const dgtl::ir::IRStream& stream,
                  const dgtl::core::SymbolTable& symbols,
                  bool verbose_asm,
                  bool arm64_binary_stream,
                  EmissionResult& result) {
    result = EmissionResult{};

    switch (backend) {
        case BackendKind::arm64:
            if (arm64_binary_stream) {
                result.ok = emit_arm64(stream,
                                       symbols,
                                       verbose_asm,
                                       result.emitted_text,
                                       &result.emitted_bytes);
                result.emitted_binary = true;
            } else {
                result.ok = emit_arm64(stream, symbols, verbose_asm, result.emitted_text, nullptr);
                result.emitted_binary = false;
            }
            return result.ok;
        case BackendKind::x64:
            result.ok = emit_x64(stream,
                                 result.emitted_bytes,
                                 result.emitted_text,
                                 &result.fallback_ops);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::nasm:
            result.ok = emit_nasm(stream, symbols, result.emitted_text);
            result.emitted_binary = false;
            return result.ok;
        case BackendKind::wasm:
            result.ok = emit_wasm(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
    }

    return false;
}

/**
 * @brief Executes a single end-to-end pipeline pass with Titan-Stable hardware pinning.
 */
TimedEmissionResult time_backend_pipeline(dgtl::core::DGTL_Arena& arena,
                                         dgtl::core::SymbolTable& symbols,
                                         BackendKind backend,
                                         bool verbose_asm,
                                         BenchmarkProfile profile,
                                         uint32_t pressure_vars,
                                         bool arm64_binary_stream) {
    
    // TITAN-STABLE: Lock thread to prevent context-switching and frequency scaling jitter
    [[maybe_unused]] const auto quiet_core_policy = dgtl::jit::DGTLHardwareScheduler::apply_quiet_core_policy(2);

    TimedEmissionResult timed{};
    const size_t capacity = (profile == BenchmarkProfile::pressure)
        ? static_cast<size_t>(pressure_vars) * 3 + 16
        : 128;
    
    dgtl::ir::IRStream stream(arena, capacity);
    if (!build_ir_for_profile(stream, profile, pressure_vars)) {
        return timed;
    }

    timed.hot_swap_mode = profile_name(profile);
    timed.nodes_built = stream.size();

    // Initialize the Titan-Stable Optimization Stack
    dgtl::analysis::DGTLAICostModel ai_model;
    dgtl::pipeline::PassDecisionEngine pde;
    dgtl::jit::DGTLHardwareScheduler scheduler(ai_model);
    dgtl::pipeline::DGTLAnalysisManager am;

    timed.ai_predicted_cost_pre = ai_model.predict_cost(stream);
    const auto optimize_start = std::chrono::high_resolution_clock::now();

    // 1. Profile-Specific Pass (Gated by Decision Engine)
    if (profile == BenchmarkProfile::fusion) {
        dgtl::core::DGTLFormalVerifier verifier;
        dgtl::pass::DGTLInstructionFusionSynth fusion_pass(verifier);
        dgtl::graph::Region region;
        if (pde.should_run_pass(&fusion_pass, &region, am, ai_model)) {
            fusion_pass.execute(stream);
        }
    } else if (profile == BenchmarkProfile::simd) {
        dgtl::pass::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
        legalizer.legalize(stream);
    } else if (profile == BenchmarkProfile::predication) {
        dgtl::pass::DGTLMaskedPredicationLowering predication_pass;
        predication_pass.execute(stream);
    }

    // 2. Hot-Swap Logic for ARM64 high-pressure kernels
    if (backend == BackendKind::arm64 && timed.ai_predicted_cost_pre > 35.0f) {
        dgtl::pass::DGTLTemporalVectorFusion temporal_fusion;
        temporal_fusion.execute(stream);
        
        if (timed.ai_predicted_cost_pre > 65.0f) {
            dgtl::pass::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
            legalizer.legalize(stream);
            timed.hot_swap_mode = "int->float-simd/predication";
        } else {
            timed.hot_swap_mode = "int->float-fusion";
        }
        timed.hot_swap_applied = true;
    }

    // 3. PILLAR 2: Transactional Shadow-Writing (Hardware Port Scheduling)
    // This eliminates the 2.5ms jitter in HFT Branching/SIMD kernels
    scheduler.schedule(stream);

    timed.nodes_after_profile = stream.size();
    
    // 4. Production Core Optimizations
    run_core_optimizations(stream);
    
    timed.nodes_after_core_opt = stream.size();
    timed.ai_predicted_cost_post = ai_model.predict_cost(stream);

    const auto optimize_end = std::chrono::high_resolution_clock::now();

    // 5. Backend Emission
    timed.ok = emit_backend(backend,
                            stream,
                            symbols,
                            verbose_asm,
                            arm64_binary_stream && backend == BackendKind::arm64,
                            timed.emission);
                            
    const auto emit_end = std::chrono::high_resolution_clock::now();

    timed.optimize_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(optimize_end - optimize_start).count();
    timed.emit_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(emit_end - optimize_end).count();
    timed.total_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(emit_end - optimize_start).count();
    timed.output_size = timed.emission.emitted_binary ? timed.emission.emitted_bytes.size() : timed.emission.emitted_text.size();
    timed.fallback_ops = timed.emission.fallback_ops;
    
    return timed;
}

/**
 * @brief Runs multiple iterations of the pipeline to calculate averages and check stability.
 */
BackendBenchmark run_backend_benchmark(dgtl::core::DGTL_Arena& arena,
                                       dgtl::core::SymbolTable& symbols,
                                       BackendKind backend,
                                       size_t iterations,
                                       bool verbose_asm,
                                       BenchmarkProfile profile,
                                       uint32_t pressure_vars,
                                       bool arm64_binary_stream) {
    BackendBenchmark result{};
    if (iterations == 0) {
        result.detail = "iterations must be > 0";
        return result;
    }

    double total_opt_us = 0.0;
    double total_emit_us = 0.0;
    double total_total_us = 0.0;
    size_t output_size = 0;
    EmissionResult last_emission;

    for (size_t i = 0; i < iterations; ++i) {
        const TimedEmissionResult timed = time_backend_pipeline(arena,
                                                                symbols,
                                                                backend,
                                                                verbose_asm,
                                                                profile,
                                                                pressure_vars,
                                                                arm64_binary_stream);
        if (!timed.ok) {
            result.detail = "backend emission failed during benchmark";
            return result;
        }
        total_opt_us += timed.optimize_us;
        total_emit_us += timed.emit_us;
        total_total_us += timed.total_us;
        output_size = timed.output_size;
        result.fallback_ops = timed.fallback_ops;
        result.nodes_built = timed.nodes_built;
        result.nodes_after_profile = timed.nodes_after_profile;
        result.nodes_after_core_opt = timed.nodes_after_core_opt;
        if (timed.hot_swap_applied) {
            ++result.hot_swap_count;
            result.hot_swap_last_mode = timed.hot_swap_mode;
        }
        last_emission = timed.emission;
    }

    result.ok = true;
    result.optimize_us = total_opt_us / static_cast<double>(iterations);
    result.emit_us = total_emit_us / static_cast<double>(iterations);
    result.total_us = total_total_us / static_cast<double>(iterations);
    result.output_size = output_size;
    result.dump_is_binary = last_emission.emitted_binary;
    
    if (last_emission.emitted_binary) {
        result.dump_text = bytes_to_hex(last_emission.emitted_bytes);
    } else {
        result.dump_text = last_emission.emitted_text;
    }

    // JIT Execution Path (Strike Runner integration)
    if (backend == BackendKind::x64 && profile == BenchmarkProfile::fold &&
        !last_emission.emitted_bytes.empty()) {
        
        dgtl::exec::StrikeRunner runner;
        const size_t injected = runner.inject_code(arena, last_emission.emitted_bytes);
        
        if (injected != 0) {
            result.runtime_supported = true;
            double total_exec_us = 0.0;
            bool exec_ok = true;

            for (size_t i = 0; i < iterations; ++i) {
                const auto exec_start = std::chrono::high_resolution_clock::now();
                
                // TITAN-STABLE: Direct Arena-pinned execution
                const auto metrics = runner.execute(arena, injected, nullptr);
                
                const auto exec_end = std::chrono::high_resolution_clock::now();
                if (metrics.status_code != 0) { // Ok
                    exec_ok = false;
                    break;
                }
                total_exec_us += std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(exec_end - exec_start).count();
            }

            if (!exec_ok) {
                result.ok = false;
                result.detail = "x64 runtime execution failed during benchmark";
                return result;
            }
            result.runtime_us = total_exec_us / static_cast<double>(iterations);
        }
    }

    // Generate diagnostic summary
    std::ostringstream oss;
    oss << "profile=" << profile_name(profile)
        << " opt_us=" << result.optimize_us
        << " emit_us=" << result.emit_us
        << " total_us=" << result.total_us
        << " output_size=" << result.output_size
        << " fallback_ops=" << result.fallback_ops
        << " nodes=" << result.nodes_built
        << "->" << result.nodes_after_profile
        << "->" << result.nodes_after_core_opt
        << " hot_swaps=" << result.hot_swap_count;
    
    if (!result.hot_swap_last_mode.empty()) {
        oss << " hot_swap_mode=" << result.hot_swap_last_mode;
    }
    
    if (result.runtime_supported) {
        oss << " exec_us=" << result.runtime_us;
    } else {
        oss << " exec_us=n/a";
    }
    
    oss << " iterations=" << iterations;
    result.detail = oss.str();
    
    return result;
}