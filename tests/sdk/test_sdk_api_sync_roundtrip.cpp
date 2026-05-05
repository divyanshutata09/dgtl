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

#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_core.h"

#include <cstdint>
#include <stdexcept>

namespace {

constexpr uint64_t kExpectNonZeroImmediate = ~0ull;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_node(const dgtl_stream_t* stream,
                 size_t index,
                 dgtl_opcode_t expected_opcode,
                 uint16_t expected_flags,
                 uint32_t expected_lhs,
                 uint32_t expected_rhs,
                 uint64_t expected_immediate,
                 const char* context) {
    dgtl_node_info_t node{};
    if (!dgtl_stream_get_node(stream, index, &node)) {
        throw std::runtime_error(context);
    }

    require(node.opcode == expected_opcode, "sdk opcode roundtrip mismatch");
    require(node.flags == expected_flags, "sdk flag roundtrip mismatch");
    require(node.lhs == expected_lhs, "sdk lhs roundtrip mismatch");
    require(node.rhs == expected_rhs, "sdk rhs roundtrip mismatch");
    if (expected_immediate == kExpectNonZeroImmediate) {
        require(node.immediate != 0u, "sdk immediate/hash should be non-zero");
    } else {
        require(node.immediate == expected_immediate, "sdk immediate roundtrip mismatch");
    }
}

} // namespace

void run_test_sdk_api_sync_roundtrip() {
    dgtl_context_t* ctx = dgtl_context_create();
    require(ctx != nullptr, "sdk context create returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 64);
    require(stream != nullptr, "sdk stream create returned null");

    const uint32_t kLhs = 11u;
    const uint32_t kRhs = 17u;
    const uint64_t kImm = 0x123456789abcdef0ull;

    uint32_t slot0 = dgtl_emit_op(stream, DGTL_OP_CONST, 0u, 0u, kImm, DGTL_FLAG_IS_CONST);
    require(slot0 != 0u, "sdk emit const failed");

    uint32_t slot1 = dgtl_emit_op(stream,
                                  DGTL_OP_ADD,
                                  kLhs,
                                  kRhs,
                                  0u,
                                  static_cast<uint16_t>(DGTL_FLAG_HAS_SIDE_EFFECT | DGTL_FLAG_VOLATILE));
    require(slot1 != 0u, "sdk emit add failed");

    uint32_t slot2 = dgtl_emit_op(stream,
                                  DGTL_OP_FMA,
                                  3u,
                                  5u,
                                  0u,
                                  static_cast<uint16_t>(DGTL_FLAG_FMA_FUSED));
    require(slot2 != 0u, "sdk emit fma failed");

    expect_node(stream,
                0,
                DGTL_OP_CONST,
                DGTL_FLAG_IS_CONST,
                static_cast<uint32_t>(kImm & 0xFFFFFFFFull),
                static_cast<uint32_t>(kImm >> 32),
                kImm,
                "sdk get node[0] failed");

    expect_node(stream,
                1,
                DGTL_OP_ADD,
                static_cast<uint16_t>(DGTL_FLAG_HAS_SIDE_EFFECT | DGTL_FLAG_VOLATILE),
                kLhs,
                kRhs,
                kExpectNonZeroImmediate,
                "sdk get node[1] failed");

    expect_node(stream,
                2,
                DGTL_OP_FMA,
                static_cast<uint16_t>(DGTL_FLAG_FMA_FUSED | DGTL_FLAG_IS_FP),
                3u,
                5u,
                kExpectNonZeroImmediate,
                "sdk get node[2] failed");

    dgtl_status_t mode = dgtl_set_ir_mode(stream, DGTL_IR_MODE_FLOAT);
    require(mode.ok == 1, "sdk set ir mode float failed");

    uint32_t slot3 = dgtl_emit_op(stream, DGTL_OP_ADD, 21u, 34u, 0u, DGTL_FLAG_NONE);
    require(slot3 != 0u, "sdk emit add in float mode failed");

    expect_node(stream,
                3,
                DGTL_OP_FADD,
                DGTL_FLAG_IS_FP,
                21u,
                34u,
                kExpectNonZeroImmediate,
                "sdk float mode add mapping mismatch");

    dgtl_status_t strike_status = dgtl_optimize(stream);
    require(strike_status.ok == 1, "sdk optimize failed");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);
}
