/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

import dgtl.arena;
import dgtl.ir;

#include <iostream>
#include <stdexcept>

void run_test_t05_lib_link_smoke() {
    std::cout << "[T05] DGTL lib link smoke\n";

    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, 16);

    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 1, 0, 0, 7);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 1, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT);

    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T05: IR stream integrity check failed");
    }

    const uint64_t hash = stream.generate_logic_hash();
    if (hash == 0u) {
        throw std::runtime_error("T05: logic hash unexpectedly zero");
    }

    std::cout << "[T05] PASS - DGTL lib link smoke OK\n";
}
