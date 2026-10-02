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

import dgtl;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir_enums;
import dgtl.ir_node;

#include <intrin.h>
#include <iostream>

void run_reg_exhaustion_test() {
    constexpr size_t kArenaMbPerThread = 10;
    constexpr size_t kReservedNodes = 5000;
    constexpr uint32_t kChainStartResult = 1000;
    constexpr uint32_t kChainLhs = 500;
    constexpr uint32_t kChainRhs = 1;
    constexpr size_t kChainDepth = 32;

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    dgtl::isa::DGTLNasmEmitter nasm;
    dgtl::core::SymbolTable symbols(arena);

    std::cout << "\033[1;36m[DGTL-REG-TEST]\033[0m Starting Register Exhaustion (32 Live Vars)...\n";

    // PILLAR: Create a chain of 32 dependent nodes.
    dgtl::pipeline::probes::build_add_chain_probe(
        stream, kChainStartResult, kChainLhs, kChainRhs, kChainDepth);

    // Measure Emission Latency
    uint64_t start = __rdtsc();
    nasm.emit(stream, symbols);
    uint64_t end = __rdtsc();

    std::cout << "\033[1;33m[RESULT]\033[0m Emission Latency for 32-Node Chain: " << (end - start) << " cycles\n";
    
    // Inspect the generated ASM for 'mov [rsp + ...]' - which indicates spilling
    std::string asm_out = nasm.get_output();
    size_t spills = 0;
    size_t pos = asm_out.find("rsp");
    while (pos != std::string::npos) {
        ++spills;
        pos = asm_out.find("rsp", pos + 1);
    }

    std::cout << "[INFO] Stack/Shadow References detected: " << spills << " (Lower is better)\n";

    if (spills < 10) {
        std::cout << "\033[1;32m[opt]\033[0m High-density register allocation verified.\n";
    } else {
        std::cout << "\033[1;35m[NOTE]\033[0m Stack spilling detected. This is a Tier-3 optimization target.\n";
    }

    return;
}







