/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * Project: DGTL (Deep General-purpose Transformation Layer)
 * Component: Production-Grade Semantic & Symbol Table Pipeline Integration Test
 */

#include <iostream>
#include <cassert>
#include <string_view>

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_core.stream;
import dgtl.ir_core.function;
import dgtl.ir_core.semantic;
import dgtl.ir_core.verifier;

int main() {
    std::cout << "[DGTL-Test 14] Initializing Production-Grade Semantic Pipeline Integration Test...\n";

    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::semantic::SymbolTable symtab(arena, 4096);
    symtab.register_builtin_runtime_symbols();

    dgtl::core::DGTLFunction func(arena, "production_kernel_entry", 1024);
    auto& stream = func.stream();

    symtab.push_scope();
    auto* arg_x = symtab.insert_variable("arg_x", 1, dgtl::ir::TypeCode::I64, 8, 8);
    auto* arg_y = symtab.insert_variable("arg_y", 2, dgtl::ir::TypeCode::I64, 8, 8);
    auto* accum = symtab.insert_variable("accum", 3, dgtl::ir::TypeCode::I64, 8, 8);

    assert(arg_x != nullptr && arg_y != nullptr && accum != nullptr);
    std::cout << "  [OK] Sovereign Symbol Table successfully bound 3 scoped variables to SSA slots.\n";

    dgtl::ir::IRNode c_node{};
    c_node.opcode = dgtl::ir::OpCode::CONST;
    c_node.flags = dgtl::ir::Flags::IS_CONST;
    c_node.result_id = 4;
    c_node.op_a = 42;
    c_node.op_b = 0;
    c_node.refresh_intent_hash();
    stream.push_back(c_node);

    dgtl::ir::IRNode add_node{};
    add_node.opcode = dgtl::ir::OpCode::ADD_I64;
    add_node.flags = dgtl::ir::Flags::NONE;
    add_node.result_id = 5;
    add_node.op_a = arg_x->slot_id;
    add_node.op_b = arg_y->slot_id;
    add_node.refresh_intent_hash();
    stream.push_back(add_node);

    dgtl::ir::IRNode ret_node{};
    ret_node.opcode = dgtl::ir::OpCode::RET;
    ret_node.flags = dgtl::ir::Flags::IS_RETURN;
    ret_node.result_id = dgtl::ir::INVALID_SLOT;
    ret_node.op_a = 5;
    ret_node.refresh_intent_hash();
    stream.push_back(ret_node);

    std::cout << "  [OK] Constructed production IRStream with " << stream.size() << " instructions.\n";

    symtab.bind_physical_stack_offset(arg_x->slot_id, -8);
    symtab.bind_physical_stack_offset(arg_y->slot_id, -16);
    symtab.bind_physical_stack_offset(accum->slot_id, -24);

    assert(symtab.lookup_by_slot(1)->stack_offset == -8);
    assert(symtab.lookup_by_slot(2)->stack_offset == -16);
    assert(symtab.lookup_by_slot(3)->stack_offset == -24);
    std::cout << "  [OK] Physical stack offset binding verified across symbol table slots.\n";

    dgtl::ir::IRVerifier verifier;
    auto diagnostics = verifier.verify(stream);
    
    bool has_error = false;
    for (const auto& diag : diagnostics) {
        if (diag.severity == dgtl::ir::IRVerifyDiagnostic::Severity::Error) {
            std::cerr << "  [FAIL] IR Verification Error at node " << diag.node_index << ": " << diag.message << "\n";
            has_error = true;
        }
    }
    assert(!has_error);
    std::cout << "  [OK] IR Stream structural verification passed with zero errors.\n";

    symtab.pop_scope();
    assert(symtab.lookup_current_scope("arg_x") == nullptr);
    std::cout << "  [OK] Scope successfully popped and unwound.\n";

    std::cout << "[SUCCESS] Production-Grade Semantic & SymbolTable Pipeline Integration Passed!\n";
    return 0;
}