#include <iostream>
#include <cstdio>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp" 
#include "dgtl/core/Symbol_Table.hpp"

using namespace dgtl;

int main() {
    std::cout << "\033[1;36m--- [DGTL-MLIR-X] Pipeline Integration Test ---\033[0m\n";

    // Initialize high-performance arena and stream
    // FIX: The constructor expects size in MB. 1MB is sufficient for this test.
    core::DGTLArena arena(1); 
    ir::IRStream stream(arena, 1024); 
    stream.set_count(0); 

    // --- TRANSACTION START ---
    // Injecting 3 nodes: Two identical ADDs and one SYSCALL anchor
    // FIX: Renamed begin_strike to begin_transaction to match IRStream implementation
    stream.begin_transaction(); 

    // Node 0: r1001 = 10 + 11
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1001, 10, 11, 0);
    
    // Node 1: r1002 = 10 + 11 (Duplicate of Node 0)
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1002, 10, 11, 0);

    // Node 2: SYSCALL using r1002. 
    // GVN will rewrite this to use r1001 and mark Node 1 as IS_DEAD.
    stream.push_node(ir::OpCode::SYSCALL, ir::Flags::NONE, 0, 1002, 0, 0);

    // FIX: Renamed commit_strike to commit_transaction
    stream.commit_transaction();
    // --- TRANSACTION END ---

    size_t final_count = stream.size();
    std::cout << "[Step 1] IR Injected. Initial Count: " << final_count << "\n";

    if (final_count < 3) {
        std::cout << "[FATAL] Stream integrity check failed. Nodes lost during injection.\n";
        return 1; 
    }

    // --- [PILLAR 2] Apply BIR 2.1 GVN Logic ---
    // We call the internal stream method to ensure side-effect safety
    stream.perform_gvn(); 

    std::cout << "[Step 2] GVN Applied. Inspecting Stream:\n";
    
    int active_ops = 0;
    for(size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i); 
        // A node is active if it's not a NOP and doesn't carry the IS_DEAD flag
        bool is_active = (node.opcode != ir::OpCode::NOP && !(node.flags & ir::Flags::IS_DEAD));
        
        if(is_active) active_ops++;

        // Detailed node inspection
        std::cout << "  Node " << i << ": [" << (int)node.opcode << "] "
                  << "Res: " << node.result_id << ", "
                  << "OpA: " << node.op_a << ", "
                  << "Flags: " << (int)node.flags 
                  << (is_active ? " (ACTIVE)" : " (CLEANED)") << "\n";
    }
    
    std::cout << "[Result] Active Ops (Expected 2): " << active_ops << "\n";

    // --- NASM Emission ---
    exec::DGTLNasmEmitter nasm;
    core::DGTLSymbolTable symbols; 
    
    // The emitter now processes the optimized stream
    nasm.emit(stream, symbols);
    
    std::cout << "\n--- NASM OUTPUT ---\n" << nasm.get_output() << "\n";

    if (active_ops != 2) {
        std::cout << "\033[1;31m[ERROR] Pipeline Optimization Mismatch. Expected 2 active nodes.\033[0m\n";
        return 1;
    }

    std::cout << "\033[1;32m[SUCCESS] GVN merged identical nodes and preserved side-effecting SYSCALL.\033[0m\n";
    return 0;
}
