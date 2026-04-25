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
#include <sstream>
#include <stdexcept>

#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"

using namespace dgtl;

void run_test_arm64_fcvtn_narrowing() {
    std::cout << "--- ARM64 SIMD Narrowing Probe (fcvtn) ---\n";

    core::DGTL_Arena arena(1, 64);
    core::SymbolTable symbols(arena);
    ir::IRStream stream(arena, 16);

    pipeline::probes::build_fcvtn_probe(stream, 22, 11);

    isa::ARM64Emitter emitter(stream, symbols);
    isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = false;

    std::ostringstream oss;
    emitter.emit_assembly(oss, cfg);
    const std::string text = oss.str();

    if (text.find("fcvtn v22.4h, v11.4s") == std::string::npos) {
        throw std::runtime_error("ARM64 narrowing probe did not emit expected fcvtn instruction");
    }

    std::cout << "PASS: fcvtn emitted for SIMD narrowing conversion.\n";
}


