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

# DGTL Language Backend Usage Guide

This guide shows how to use DGTL as the backend for your own language frontend.

## 1. What You Build

You build a frontend (lexer, parser, AST, type checker) in any language you want.
Then you lower your typed AST into DGTL IR.
DGTL handles optimization and code emission.

Pipeline:
1. Parse source code.
2. Build typed AST.
3. Lower AST to DGTL IR.
4. Run DGTL optimizations.
5. Emit target output (ARM64, x64, WASM, NASM, ELF .o, COFF .obj/.lib, Mach-O).

## 2. Minimal C++ Backend Integration

```cpp
#include <iostream>
#include <sstream>
#include "dgtl/all.hpp"

int main() {
    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::core::SymbolTable symbols(arena);
    dgtl::ir::IRStream stream(arena, 256);

    // Example: return 21 + 21
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST,
                     1, dgtl::ir::INVALID_SLOT, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST,
                     2, dgtl::ir::INVALID_SLOT, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE,
                     3, 1, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN,
                     0, 3, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();

    // Optimize
    stream.perform_gvn();
    stream.perform_dead_code_elimination();

    // Emit ARM64 assembly
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = true;

    std::ostringstream out;
    emitter.emit_assembly(out, cfg);
    std::cout << out.str() << "\n";

    return 0;
}
```

## 3. AST-to-IR Lowering Pattern

Use a function that returns the DGTL result id for each expression.

```cpp
uint32_t lower_expr(const Expr& expr, dgtl::ir::IRStream& stream) {
    switch (expr.kind) {
        case ExprKind::IntLiteral: {
            uint32_t id = new_result_id();
            stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST,
                             id, dgtl::ir::INVALID_SLOT, expr.int_value,
                             dgtl::ir::INVALID_SLOT, 0);
            return id;
        }
        case ExprKind::Add: {
            uint32_t lhs = lower_expr(*expr.left, stream);
            uint32_t rhs = lower_expr(*expr.right, stream);
            uint32_t id = new_result_id();
            stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE,
                             id, lhs, rhs, dgtl::ir::INVALID_SLOT, 0);
            return id;
        }
        default:
            return dgtl::ir::INVALID_SLOT;
    }
}
```

## 4. Symbol Mapping

Map source variables to DGTL result ids.

```cpp
// pseudo-structure
std::unordered_map<std::string, uint32_t> vars;

uint32_t define_var(const std::string& name, uint32_t value_id) {
    vars[name] = value_id;
    return value_id;
}

uint32_t load_var(const std::string& name) {
    return vars.at(name);
}
```

## 5. Multi-Target Emission

Pick emitter by target in your compiler driver.

```cpp
if (target == "arm64") {
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = true;
    emitter.emit_assembly(output_stream, cfg);
} else if (target == "x64") {
    dgtl::isa::X64StrikeEmitter emitter;
    auto bytes = emitter.emit(stream);
    // write bytes or disassemble for display
} else if (target == "wasm") {
    dgtl::isa::DGTLWasmEncoder encoder;
    auto bytes = encoder.encode_stream(stream);
} else if (target == "elfobj") {
    dgtl::exec::DGTLUniversalLinker linker;
    std::vector<uint8_t> obj;
    linker.build_elf_relocatable(obj, stream, "my_kernel_entry");
} else if (target == "coffobj") {
    dgtl::exec::DGTLUniversalLinker linker;
    std::vector<uint8_t> obj;
    linker.build_coff_relocatable(obj, stream, "my_kernel_entry");
} else if (target == "macho") {
    dgtl::isa::MachOEmitter emitter(stream, symbols);
    auto result = emitter.emit_to_binary();
}
```

## 6. Calling DGTL From Other Frontend Languages

You can keep DGTL in a C++ backend process and call it from any frontend language.

Pattern:
1. Frontend (Rust/Go/Python/C) parses source.
2. Frontend serializes an IR plan (JSON or binary).
3. DGTL backend executable reads the plan and builds IRStream.
4. DGTL emits target output.
5. Frontend receives artifact path or bytes.

Example backend command contract:

```text
my_backend --ir-plan plan.json --target arm64 --out build/out.s
```

## 7. Safety Rules For Frontend Authors

1. Use dgtl::ir::INVALID_SLOT for unused operands.
2. Always begin_transaction before bulk push_node and commit_transaction after.
3. Validate stream integrity after lowering and after optimization.
4. Keep result ids unique within a function.
5. Do not execute untrusted scripts during build or package steps.

## 8. Build Command Example

```powershell
Set-Location "D:\TataG\DGTL"
$main = (Resolve-Path "src/main.cpp").Path
$sources = Get-ChildItem "src" -Recurse -Filter "*.cpp" | Where-Object { $_.FullName -ne $main } | ForEach-Object { $_.FullName }
g++ -std=c++2b -Iinclude $main $sources -o build/main_release_smoke.exe
```

## 9. Current Reality

DGTL is backend-strong today.
You still need to build your own frontend language components:
- lexer
- parser
- type checker
- lowering layer

Once those exist, DGTL can power your code generation pipeline across targets.
