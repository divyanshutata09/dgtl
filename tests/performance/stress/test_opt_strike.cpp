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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * TEST SUITE: PARALLEL OPTIMIZATION STRIKE (1M NODE STRESS TEST)
 * EMERGENCY BYopt: RAW POINTER INITIALIZATION
 * STATUS: PILLAR 3 COMPLIANT (MASTER ARCHITECT)
 */

#include <iostream>
#include <chrono>
#include <iomanip>  
#include <cstdint>  
#include <algorithm>
#include <thread>
#include <cstdlib>
#include <string>

#include "dgtl/dgtl_all.hpp"
#include "dgtl/pass/speculative/Speculative_DCE.hpp"

namespace {

size_t normalize_hft_stride(size_t bytes) {
    if (bytes <= 4) return 4;
    if (bytes <= 8) return 8;
    return 16;
}

size_t choose_hft_stride_bytes(size_t node_count, uint32_t workers, std::string& mode) {
    const char* env = std::getenv("DGTL_HFT_STRIDE_BYTES");
    if (env != nullptr && *env != '\0') {
        std::string raw(env);
        if (raw == "auto") {
            mode = "auto";
        } else {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(env, &end, 10);
            if (end != env && *end == '\0') {
                mode = "env";
                return normalize_hft_stride(static_cast<size_t>(parsed));
            }
            mode = "env-invalid->auto";
        }
    } else {
        mode = "auto";
    }

    if (node_count >= 1000000 && workers >= 16) {
        return 16;
    }
    if (node_count >= 1000000 && workers <= 4) {
        return 4;
    }
    return 8;
}

} // namespace

/**
 * @brief Main Entry for the 1M Node Optimization Strike.
 * Validates Pillar 2 (Transactional IR) and Pillar 3 (The Sentinel) under load.
 */
void run_test_opt_strike() {
    std::cout << "\033[1;36m--- DGTL: PARALLEL OPTIMIZATION STRIKE ---\033[0m\n";

    constexpr size_t kArenaMbPerThread = 256;
    constexpr size_t kNodeCount = 1000000;
    constexpr uint32_t kResultBase = 1024;
    constexpr double kMillion = 1000000.0;
    constexpr double kExpectedLogicalThroughputMin = 175.0;
    constexpr double kExpectedLogicalThroughputMax = 211.5;
    constexpr size_t kValidationLanesPerNode = 6; // opcode, result_id, op_a, op_b, op_c, intent_hash
    constexpr size_t kValidationBytesPerNode =
        sizeof(dgtl::ir::OpCode) +
        sizeof(uint32_t) * 4 +
        sizeof(uint64_t);

    const uint32_t workers = std::max<uint32_t>(1u, std::thread::hardware_concurrency());
    std::string stride_mode;
    const size_t hft_stride_bytes = choose_hft_stride_bytes(kNodeCount, workers, stride_mode);
    const size_t hft_stride_nodes = std::max<size_t>(1, hft_stride_bytes / sizeof(uint64_t));

    // --- STEP 1: ARENA + STREAM SETUP ---
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    arena.pre_touch_all();

    dgtl::ir::IRStream stream(arena, kNodeCount);
    if (stream.is_null()) {
        std::cerr << "\033[1;31m[ERROR] IRStream arena allocation failed for 1M nodes.\033[0m\n";
        return;
    }

    // --- STEP 2: INGESTION (Mock Data Generation) ---
    std::cout << "[INFO] Ingesting mock data into HFT-aligned buffer...\n";

    stream.begin_transaction();
    for (size_t i = 0; i < kNodeCount; ++i) {
        // Patterning: Every 5th node is a MUL_F32 to trigger Fusion patterns
        // Define dependencies with stride-tuned spacing to simulate HFT layout pressure.
        const uint32_t result_id = static_cast<uint32_t>(i + kResultBase);
        const uint32_t op_a = (i >= hft_stride_nodes)
            ? static_cast<uint32_t>((i - hft_stride_nodes) + kResultBase)
            : dgtl::ir::INVALID_SLOT;
        const uint32_t op_b = (i >= (hft_stride_nodes * 2))
            ? static_cast<uint32_t>((i - (hft_stride_nodes * 2)) + kResultBase)
            : dgtl::ir::INVALID_SLOT;
        stream.push_node(
            (i % 5 == 0) ? dgtl::ir::OpCode::MUL_F32 : dgtl::ir::OpCode::ADD_I32,
            dgtl::ir::Flags::NONE,
            result_id,
            op_a,
            op_b,
            dgtl::ir::INVALID_SLOT);
    }
    stream.commit_transaction();

    // --- STEP 3: INITIALIZATION ---
    std::cout << "[INFO] Initializing Transactional Stream...\n";

    if (stream.size() != kNodeCount) {
        std::cerr << "[ERROR] Stream ingestion count mismatch. Expected " << kNodeCount
                  << ", got " << stream.size() << ".\n";
        return;
    }

    // MANDATORY PRE-STRIKE VALIDATION
    if (!stream.validate_shadow_integrity()) {
        std::cerr << "\033[1;31m[DGTL CRITICAL] PRE-STRIKE INVARIANT FAILURE!\033[0m\n";
        const auto node0 = stream.get_node(0);
        std::cerr << "[DGTL] Ingested IR state is invalid. Check OpCode 0x"
                  << std::hex << static_cast<int>(node0.opcode) << std::dec << " at Node 0.\n";
        return;
    }

    std::cout << "[INFO] Initializing SpeculativeDCE no-model fast path (" << workers
              << " validation workers available)...\n";

    dgtl::pass::DGTLSpeculativeDCE strike_pass;

    // --- STEP 4: THE STRIKE ---
    std::cout << "[INFO] Striking 1M Nodes (Precision: HFT "
              << hft_stride_bytes << "-byte Stride, mode=" << stride_mode << ")..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    strike_pass.execute(stream);
    
    auto end = std::chrono::high_resolution_clock::now();

    if (!stream.validate_shadow_integrity()) {
        std::cerr << "\033[1;31m[ERROR] Optimization strike failed runtime validation.\033[0m\n";
        return;
    }

    // --- METRICS ---
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double seconds = duration_us / kMillion;
    const double logical_throughput_mnodes = static_cast<double>(kNodeCount) / seconds / kMillion;
    const double internal_hash_lane_throughput =
        static_cast<double>(kNodeCount * kValidationLanesPerNode) / seconds / kMillion;
    const double internal_data_rate_gib =
        (static_cast<double>(kNodeCount * kValidationBytesPerNode) / seconds) /
        (1024.0 * 1024.0 * 1024.0);
    const bool within_expected_band =
        logical_throughput_mnodes >= kExpectedLogicalThroughputMin &&
        logical_throughput_mnodes <= kExpectedLogicalThroughputMax;
    
    std::cout << "\n\033[1;32m--- STRIKE SUMMARY ---\033[0m\n";
    std::cout << "[PERF] Total Strike Time : " << std::fixed << std::setprecision(3) << (duration_us / 1000.0) << " ms\n";
    std::cout << "[PERF] Logical Throughput : " << logical_throughput_mnodes << " M-Nodes/sec\n";
    std::cout << "[PERF] Internal Throughput: " << internal_hash_lane_throughput << " M-Hash-Lanes/sec\n";
    std::cout << "[PERF] Internal Data Rate : " << internal_data_rate_gib << " GiB/sec\n";
    std::cout << "[PERF] HFT Stride         : " << hft_stride_bytes
              << " bytes (mode=" << stride_mode << ")\n";
    std::cout << "[PERF] Strike Verdict    : "
              << (within_expected_band ? "within expected recovery band" : "performance notice: outside expected recovery band")
              << " (" << kExpectedLogicalThroughputMin << '-' << kExpectedLogicalThroughputMax
              << " M-Nodes/sec)\n";
    std::cout << "[DATA] Final Stream Size : " << stream.size() << " nodes\n";
    std::cout << "[DATA] Final Logic Hash  : 0x" << std::hex << stream.generate_logic_hash() << std::dec << "\n";
    std::cout << "[INFO] Transactional Integrity Verified by Sentinel.\n";

    return;
}








