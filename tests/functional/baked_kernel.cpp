// C:\projects\DGTL_framework\tests\functional\test_pipeline_20_hybrid_tiering.cpp
#include "dgtl/jit/JIT_Executor.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include <iostream>
#include <assert.h>

int main() {
    using namespace dgtl::ir;
    using namespace dgtl::exec;

    DGTLJitExecutor jit(1); // 1MB JIT Buffer
    DGTLNasmEmitter aot;
    IRStream stream;

    // Build a simple kernel: r0 = 42 + r1
    stream.push_node(OpCode::IMMEDIATE, 0, 0, 0, 42, 0, 0); 
    stream.push_node(OpCode::ADD_I32,   0, 0, 0, 1, 0, 0);
    stream.push_node(OpCode::RET,       0, 0, 0, 0, 0, 0);

    std::cout << "[DGTL] Loading JIT Kernel..." << std::endl;
    // Simulate JIT loading (using empty binary for trace mapping test)
    std::vector<uint8_t> dummy_bin = { 0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; // mov rax, 42; ret
    auto func = jit.load_and_prepare(dummy_bin, stream);

    if(func) {
        std::cout << "[DGTL] JIT Execution successful at " << (void*)func << std::endl;
        
        // --- HYBRID TRIGGER ---
        std::cout << "[DGTL] Hot Path Detected! Baking to Disk..." << std::endl;
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(func);
        auto retrieved_ir = jit.get_ir_from_address(addr);
        
        dgtl::core::DGTLSymbolTable symbols;
        aot.emit(retrieved_ir, symbols);
        
        std::cout << "--- BAKED ASM START ---" << std::endl;
        std::cout << aot.get_output() << std::endl;
        std::cout << "--- BAKED ASM END ---" << std::endl;
        
        assert(aot.get_output().find("DGTL_kernel_entry") != std::string::npos);
        std::cout << "[TEST optED] Hybrid Handover Verified." << std::endl;
    }

    return 0;
}
