#include <iostream>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

void run_test_pipeline_14_unaligned_load() {
    std::cout << "--- [DGTL-TEST] 14_unaligned_load ---" << std::endl;
    core::DGTL_Arena arena(64);
    ir::IRStream stream(arena, 128);

    stream.begin_transaction();
    // Simulate an unaligned byte offset load.
    stream.push_node(ir::OpCode::LOAD, ir::Flags::NONE, 1, 1000, 0, 3, 0);
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 1, 1, 0, 0);
    stream.push_node(ir::OpCode::RET, ir::Flags::IS_RETURN, 0, 2, 0, 0, 0);
    stream.commit_transaction();

    const size_t before = stream.size();
    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);

    const bool size_stable = stream.size() > 0 && stream.size() <= before;
    const bool integrity_ok = stream.validate_shadow_integrity();

    if (size_stable && integrity_ok) {
        std::cout << "SUCCESS" << std::endl;
        return;
    }

    std::cout << "FAIL" << std::endl;
    return;
}


