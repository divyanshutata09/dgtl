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

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"

namespace {

void run_codegen_worker(int iterations, std::atomic<int>& failures) {
    dgtl::core::DGTL_Arena arena(1, 32);
    dgtl::core::SymbolTable symbols(arena);

    for (int i = 0; i < iterations; ++i) {
        dgtl::ir::IRStream stream(arena, 64);
        if ((i % 3) == 0) {
            dgtl::pipeline::probes::build_fma_probe(stream, 22, 10, 11, 12);
        } else if ((i % 3) == 1) {
            dgtl::pipeline::probes::build_fcvtl_probe(stream, 21, 10);
        } else {
            dgtl::pipeline::probes::build_fcvtn_probe(stream, 22, 11);
        }

        dgtl::isa::ARM64Emitter emitter(stream, symbols);
        dgtl::isa::ARM64Emitter::Config cfg;
        cfg.verbose_asm = false;

        std::ostringstream oss;
        emitter.emit_assembly(oss, cfg);
        const std::string text = oss.str();
        if (text.empty()) {
            failures.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const bool ok_fma = (text.find("fmadd") != std::string::npos);
        const bool ok_widen = (text.find("fcvtl") != std::string::npos);
        const bool ok_narrow = (text.find("fcvtn") != std::string::npos);
        if (!(ok_fma || ok_widen || ok_narrow)) {
            failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace

void run_test_performance_mt_codegen() {
    std::cout << "--- PERFORMANCE: Multi-threaded ARM64 codegen stress ---\n";

    const unsigned hw = std::thread::hardware_concurrency();
    const int thread_count = static_cast<int>(hw == 0 ? 8 : hw);
    const int iterations_per_thread = 256;

    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(thread_count));

    const auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < thread_count; ++i) {
        workers.emplace_back(run_codegen_worker, iterations_per_thread, std::ref(failures));
    }
    for (auto& t : workers) {
        t.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    const double total_ops = static_cast<double>(thread_count) * static_cast<double>(iterations_per_thread);
    const double sec = static_cast<double>(us) / 1000000.0;
    const double ops_per_sec = sec > 0.0 ? (total_ops / sec) : 0.0;

    std::cout << "[perf] threads=" << thread_count
              << " iterations/thread=" << iterations_per_thread
              << " total_ops=" << static_cast<unsigned long long>(total_ops)
              << " failures=" << failures.load() << "\n";
    std::cout << "[perf] elapsed_us=" << us
              << " ops_per_sec=" << static_cast<unsigned long long>(ops_per_sec) << "\n";

    if (failures.load() != 0) {
        throw std::runtime_error("multi-threaded ARM64 codegen stress reported failures");
    }
}


