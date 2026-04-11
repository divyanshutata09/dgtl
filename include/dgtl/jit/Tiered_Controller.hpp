#pragma once
#include <filesystem>
#include <fstream>
#include "JIT_Executor.hpp"
#include "../isa/NASM_Emitter.hpp"

namespace dgtl::exec {

class TieredController {
    DGTLJitExecutor& _jit;
    DGTLNasmEmitter _aot;
    std::unordered_map<uintptr_t, uint32_t> _hot_counts;
    const uint32_t BAKE_THRESHOLD = 10000;

public:
    TieredController(DGTLJitExecutor& jit) : _jit(jit) {}

    /**
     * @brief Record execution and check if we should bake to disk.
     */
    void profile_and_check(JitFunction func_ptr, const core::DGTLSymbolTable& symbols) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(func_ptr);
        
        if (++_hot_counts[addr] == BAKE_THRESHOLD) {
            bake_to_disk(addr, symbols);
        }
    }

private:
    void bake_to_disk(uintptr_t addr, const core::DGTLSymbolTable& symbols) {
        auto ir = _jit.get_ir_from_address(addr);
        if (ir.is_null()) return;

        _aot.clear();
        _aot.emit(ir, symbols);

        std::string filename = std::format("kernel_0x{:X}.asm", addr);
        std::ofstream file(filename);
        file << _aot.get_output();
        file.close();
        
        // Potential Step: Trigger NASM assembly automatically here
    }
};

} // namespace dgtl::exec
