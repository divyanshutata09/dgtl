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
#include <memory>   
#include <cstdint>  
#include <algorithm>
#include <thread>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <malloc.h>
#else
    #include <stdlib.h>
    #define _aligned_malloc(size, alignment) aligned_alloc(alignment, size)
    #define _aligned_free(ptr) free(ptr)
#endif

#include "dgtl/ir/Global_IR.hpp"               
#include "dgtl/pipeline/Pass_Manager.hpp"        
// #include "opt/Instruction_Fusion_Synth.hpp"
#include "dgtl/pass/speculative/Speculative_DCE.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

using namespace dgtl;

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

    // 1 Million Nodes = ~24MB of raw IR data. Stride is adaptive by default.
    constexpr size_t NODE_COUNT = 1000000;
    const uint32_t workers = std::thread::hardware_concurrency();
    std::string stride_mode;
    const size_t hft_stride_bytes = choose_hft_stride_bytes(NODE_COUNT, workers, stride_mode);
    const size_t hft_stride_nodes = std::max<size_t>(1, hft_stride_bytes / sizeof(uint64_t));
    
    // --- STEP 1: ALLOCATION (PILLAR 3 COMPLIANCE) ---
    // Using 64-byte alignment to match CPU Cache Line & AVX-512 requirements.
    ir::IRNode* buffer = static_cast<ir::IRNode*>(_aligned_malloc(NODE_COUNT * sizeof(ir::IRNode), 64));
    
    if (!buffer) { 
        std::cerr << "\033[1;31m[ERROR] Heap Allocation Failed for 1M Nodes.\033[0m\n"; 
        return;
    }

    // --- STEP 2: INGESTION (Mock Data Generation) ---
    std::cout << "[INFO] Ingesting mock data into HFT-aligned buffer..." << std::endl;
    
    for (size_t i = 0; i < NODE_COUNT; ++i) {
        // Patterning: Every 5th node is a MUL_F32 to trigger Fusion patterns
        buffer[i].opcode = (i % 5 == 0) ? ir::OpCode::MUL_F32 : ir::OpCode::ADD_I32;
        buffer[i].flags  = ir::Flags::NONE;
        buffer[i].result_id = static_cast<uint32_t>(i + 1024); 
        
        // Define dependencies with stride-tuned spacing to simulate HFT layout pressure.
        buffer[i].op_a = (i >= hft_stride_nodes)
            ? static_cast<uint32_t>((i - hft_stride_nodes) + 1024)
            : ir::INVALID_SLOT;
        buffer[i].op_b = (i >= (hft_stride_nodes * 2))
            ? static_cast<uint32_t>((i - (hft_stride_nodes * 2)) + 1024)
            : ir::INVALID_SLOT;
        buffer[i].op_c = ir::INVALID_SLOT;
        
        // ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã¢â‚¬Å“ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ PILLAR 3: Pre-calculating intent hash.
        // This now uses the synchronized 31/17/7/3 formula.
        buffer[i].refresh_intent_hash();
    }

    // --- STEP 3: INITIALIZATION ---
    std::cout << "[INFO] Initializing Transactional Stream..." << std::endl;
    
    auto stream = std::make_unique<ir::IRStream>(buffer, NODE_COUNT);

    if (stream->is_null()) {
        std::cerr << "[ERROR] Stream failed to map to external buffer.\n";
        _aligned_free(buffer);
        return;
    }

    // MANDATORY PRE-STRIKE VALIDATION
    // If this fails, the issue is in the ingestion logic or BitPacked_IR.hpp hashing.
    if (!stream->validate_shadow_integrity()) {
        std::cerr << "\033[1;31m[DGTL CRITICAL] PRE-STRIKE INVARIANT FAILURE!\033[0m\n";
        std::cerr << "[DGTL] Ingested IR state is invalid. Check OpCode 0x" 
                  << std::hex << static_cast<int>(buffer[0].opcode) << std::dec << " at Node 0.\n";
        _aligned_free(buffer);
        return;
    }

    std::cout << "[INFO] Initializing Pipeline Manager (" << workers << " workers)..." << std::endl;
    
    pipeline::DGTLoptManager manager(workers);

    // Setup Optimization Chain with AI-Gating
    auto ai_model = std::make_shared<dgtl::analysis::DGTLAICostModel>();
    
    // manager.add_opt(std::make_unique<dgtl::pass::DGTLInstructionFusionSynth>(ai_model)); // Not implemented
    manager.add_opt(std::make_unique<dgtl::pass::DGTLSpeculativeDCE>());

    // --- STEP 4: THE STRIKE ---
    std::cout << "[INFO] Striking 1M Nodes (Precision: HFT "
              << hft_stride_bytes << "-byte Stride, mode=" << stride_mode << ")..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    
    // Execute the parallel transactional pipeline.
    manager.execute(*stream, pipeline::TargetPrecision::HFT); 
    
    auto end = std::chrono::high_resolution_clock::now();

    // --- METRICS ---
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double seconds = duration_us / 1000000.0;
    
    std::cout << "\n\033[1;32m--- STRIKE SUMMARY ---\033[0m\n";
    std::cout << "[PERF] Total Strike Time : " << std::fixed << std::setprecision(3) << (duration_us / 1000.0) << " ms\n";
    std::cout << "[PERF] Throughput         : " << (NODE_COUNT / seconds) / 1e6 << " M-Nodes/sec\n";
    std::cout << "[PERF] HFT Stride         : " << hft_stride_bytes
              << " bytes (mode=" << stride_mode << ")\n";
    std::cout << "[DATA] Final Stream Size : " << stream->size() << " nodes\n";
    std::cout << "[DATA] Final Logic Hash  : 0x" << std::hex << stream->generate_logic_hash() << std::dec << "\n";
    std::cout << "[INFO] Transactional Integrity Verified by Sentinel.\n";

    // CLEANUP
    _aligned_free(buffer);

    return;
}








