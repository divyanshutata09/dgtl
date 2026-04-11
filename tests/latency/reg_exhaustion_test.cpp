#include <iostream>
#include <vector>
#include <intrin.h>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"

int main() {
    dgtl::core::DGTLArena arena(10); 
    dgtl::ir::IRStream stream(arena, 5000);
    dgtl::exec::DGTLNasmEmitter nasm;
    dgtl::core::DGTLSymbolTable symbols;

    std::cout << "\033[1;36m[DGTL-REG-TEST]\033[0m Starting Register Exhaustion (32 Live Vars)..." << std::endl;

    stream.begin_transaction();
    
    // PILLAR: Create a chain of 32 dependent nodes.
    // Each node depends on the previous one, forcing the compiler to keep the result 'live'.
    for(int i = 0; i < 32; ++i) {
        // r[1000+i] = r[1000+i-1] + constant
        uint32_t result_id = 1000 + i;
        uint32_t op_a = (i == 0) ? 500 : (1000 + i - 1);
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, result_id, op_a, 1, 0);
    }

    stream.commit_transaction();

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

    return 0;
}
