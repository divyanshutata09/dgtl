/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 01 — IR Stream Correctness
 *
 * Judges: push/get node round-trip, size tracking, hash determinism,
 *         shadow integrity. Core IR layer health check.
 */

#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/core/Arena.hpp"

#include <iostream>
#include <stdexcept>

void run_test_t01_ir_stream_correctness() {
    std::cout << "[T01] IR Stream Correctness\n";

    constexpr size_t kArenaThreads  = 1;
    constexpr size_t kArenaMb       = 8;
    constexpr size_t kReserved      = 64;

    dgtl::core::DGTL_Arena arena(kArenaThreads, kArenaMb);
    dgtl::ir::IRStream stream(arena, kReserved);

    if (stream.is_null()) {
        throw std::runtime_error("T01: IRStream reports null after construction with valid arena");
    }

    // --- push three well-known nodes ---
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST,  10, 0,  0, 42);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST,  11, 0,  0, 99);
    stream.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::NONE,       1, 10, 11,  0);

    if (stream.size() != 3) {
        throw std::runtime_error("T01: size() expected 3 after three push_node calls, got "
                                 + std::to_string(stream.size()));
    }

    // --- round-trip get_node ---
    const auto n0 = stream.get_node(0);
    if (n0.opcode != dgtl::ir::OpCode::LOAD_IMM) {
        throw std::runtime_error("T01: node[0] opcode mismatch — expected LOAD_IMM");
    }
    if (n0.result_id != 10) {
        throw std::runtime_error("T01: node[0] result_id mismatch — expected 10");
    }

    const auto n2 = stream.get_node(2);
    if (n2.opcode != dgtl::ir::OpCode::ADD_I32) {
        throw std::runtime_error("T01: node[2] opcode mismatch — expected ADD_I32");
    }
    if (n2.op_a != 10 || n2.op_b != 11) {
        throw std::runtime_error("T01: node[2] operands mismatch — expected op_a=10, op_b=11");
    }

    // --- hash must be non-zero and deterministic ---
    const uint64_t hash1 = stream.generate_logic_hash();
    const uint64_t hash2 = stream.generate_logic_hash();
    if (hash1 == 0) {
        throw std::runtime_error("T01: generate_logic_hash() returned zero — hash computation broken");
    }
    if (hash1 != hash2) {
        throw std::runtime_error("T01: generate_logic_hash() is non-deterministic — different on two calls");
    }

    // --- shadow integrity must hold on a freshly built stream ---
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T01: validate_shadow_integrity() failed on a freshly built stream");
    }

    std::cout << "[T01] PASS — push/get round-trip, size, hash determinism, shadow integrity OK\n";
}
