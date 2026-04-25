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

#include <iostream>
#include <vector>
#include <intrin.h>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"

void run_reg_exhaustion_test() {
    dgtl::core::DGTL_Arena arena(1, 10);
    dgtl::ir::IRStream stream(arena, 5000);
    dgtl::isa::DGTLNasmEmitter nasm;
    dgtl::core::DGTLSymbolTable symbols;

    std::cout << "\033[1;36m[DGTL-REG-TEST]\033[0m Starting Register Exhaustion (32 Live Vars)..." << std::endl;

    // PILLAR: Create a chain of 32 dependent nodes.
    dgtl::pipeline::probes::build_add_chain_probe(stream, 1000, 500, 1, 32);

    // Measure Emission Latency
    uint64_t start = __rdtsc();
    nasm.emit(stream, symbols);
    uint64_t end = __rdtsc();

    std::cout << "\033[1;33m[RESULT]\033[0m Emission Latency for 32-Node Chain: " << (end - start) << " cycles" << std::endl;
    
    // Inspect the generated ASM for 'mov [rsp + ...]' - which indicates spilling
    std::string asm_out = nasm.get_output();
    size_t spills = 0;
    size_t pos = asm_out.find("rsp");
    while (pos != std::string::npos) {
        spills++;
        pos = asm_out.find("rsp", pos + 1);
    }

    std::cout << "[INFO] Stack/Shadow References detected: " << spills << " (Lower is better)" << std::endl;

    if (spills < 10) {
        std::cout << "\033[1;32m[opt]\033[0m High-density register allocation verified." << std::endl;
    } else {
        std::cout << "\033[1;35m[NOTE]\033[0m Stack spilling detected. This is a Tier-3 optimization target." << std::endl;
    }

    return;
}







