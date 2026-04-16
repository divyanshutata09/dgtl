#include <iostream>
#include <stdexcept>

#include "dgtl/core/Arena.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"

using namespace dgtl;

void run_test_release_ir_modes() {
    std::cout << "--- RELEASE MODE REGRESSION: int(default) vs float(FMA) ---\n";

    core::DGTL_Arena arena(1, 64);

    // Integer-first release mode should remain scalar integer math.
    ir::IRStream int_stream(arena, 64);
    pipeline::probes::build_release_int_probe(int_stream);
    pipeline::probes::run_common_optimizations(int_stream);
    const bool saw_mul_i32 = pipeline::probes::stream_has_opcode(int_stream, ir::OpCode::MUL_I32);
    if (!saw_mul_i32) {
        throw std::runtime_error("integer release mode no longer emits MUL_I32 chain");
    }

    // Float mode should preserve fusion availability (MUL+ADD -> FMA).
    ir::IRStream float_stream(arena, 64);
    pipeline::probes::build_release_float_probe(float_stream);
    pipeline::probes::run_common_optimizations(float_stream);
    pipeline::probes::apply_float_fusion_probe(float_stream);
    const bool saw_fma = pipeline::probes::stream_has_opcode(float_stream, ir::OpCode::FMA_F32);
    if (!saw_fma) {
        throw std::runtime_error("float release mode lost FMA fusion");
    }

    std::cout << "PASS: integer default and float/FMA mode behavior validated.\n";
}
