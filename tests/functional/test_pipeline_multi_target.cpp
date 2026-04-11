#include <iostream>
#include <vector>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/pass/SIMD_Legalizer.hpp"

using namespace dgtl;

void setup_test_stream(ir::IRStream& s) {
    s.begin_strike();
    // Create 4 independent adds that SHOULD be fused
    for(int i=0; i<4; ++i) {
        s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 100+i, i, i+1, 0);
    }
    s.commit_strike();
}

int main() {
    struct Case { opt::TargetArch arch; uint32_t expected; std::string name; };
    std::vector<Case> cases = {
        { opt::TargetArch::X86_AVX512,  0x204, "X86_AVX512" },
        { opt::TargetArch::ARM_NEON,    0x304, "ARM_NEON" },
        { opt::TargetArch::WASM_SIMD128, 0x404, "WASM_SIMD128" }
    };

    bool all_pass = true;

    for (auto& c : cases) {
        core::DGTLArena a(2);
        ir::IRStream s(a, 16);
        setup_test_stream(s);

        opt::DGTLSIMDLegalizer legalizer(c.arch);
        legalizer.legalize(s);

        uint32_t result_op = static_cast<uint32_t>(s.get_node(0).opcode);
        if (result_op == c.expected) {
            std::cout << "[PASS] " << c.name << " fused to 0x" << std::hex << c.expected << std::endl;
        } else {
            std::cout << "[FAIL] " << c.name << " expected 0x" << std::hex << c.expected 
                      << " but got 0x" << result_op << std::endl;
            all_pass = false;
        }
    }

    if(all_pass) std::cout << "GLOBAL_SUCCESS";
    return 0;
}
