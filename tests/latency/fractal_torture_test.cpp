#include <iostream>
#include <intrin.h>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"

int main() {
    dgtl::core::DGTLArena arena(20); 
    dgtl::ir::IRStream stream(arena, 10000);
    dgtl::exec::DGTLNasmEmitter nasm;
    dgtl::core::DGTLSymbolTable symbols;

    std::cout << "\033[1;36m[DGTL-FRACTAL]\033[0m Starting Non-Linear Dependency Torture..." << std::endl;

    stream.begin_transaction();
    
    // Create a "Root" node
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1000, 5, 5, 0); // r1000 = 10

    // Create 50 nodes that ALL depend on r1000
    // This tests the "Fan-out" stability of the Symbol Table
    for(int i = 0; i < 50; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2000 + i, 1000, i, 0);
    }

    // Create 50 nodes that are EXACT DUPLICATES of the previous 50
    // This is the ultimate test for the GVN Hash Map under pressure
    for(int i = 0; i < 50; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3000 + i, 1000, i, 0);
    }

    stream.commit_transaction();

    // Measure Optimization + Emission Latency
    uint64_t start = __rdtsc();
    stream.perform_gvn(); 
    nasm.emit(stream, symbols);
    uint64_t end = __rdtsc();

    size_t total_nodes = stream.size();
    int active_nodes = 0;
    for(size_t i = 0; i < total_nodes; ++i) {
        if(!(stream.get_node(i).flags & dgtl::ir::Flags::IS_DEAD)) active_nodes++;
    }

    std::cout << "\033[1;33m[RESULT]\033[0m Fractal GVN + Emission: " << (end - start) << " cycles" << std::endl;
    std::cout << "[INFO] Total Injected: " << total_nodes << " | Active after GVN: " << active_nodes << std::endl;

    if (active_nodes <= 51) {
        std::cout << "\033[1;32m[PASS]\033[0m GVN successfully collapsed 50 redundant fractal branches." << std::endl;
    } else {
        std::cout << "\033[1;31m[FAIL]\033[0m Redundant nodes leaked through GVN." << std::endl;
    }

    return 0;
}
