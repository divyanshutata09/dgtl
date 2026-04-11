#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/ARM64_Emitter.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

int main() {
    // 1. You MUST have an Arena because your IR/SymbolTable requires it
    dgtl::core::DGTLArena arena(1024 * 64); 

    // 2. Correctly initialize SymbolTable and Stream with the Arena
    ir::SymbolTable symbols(arena);
    ir::IRStream stream(arena, 100);

    // 3. IRNode is Bit-Packed. Use the direct push_node interface 
    // instead of non-existent .set_op() methods.
    stream.push_node(
        ir::OpCode::VEC_ADD_F32_512, // opcode
        0,                           // flags
        10,                          // result_id (v10)
        1,                           // op_a (v1)
        2,                           // op_b (v2)
        0,                           // op_c
        0xABC123                     // intent_hash
    );

    // 4. Emit the assembly
    ir::ARM64Emitter emitter(stream, symbols);
    emitter.emit_assembly(std::cout);

    return 0;
}
