#pragma once
#include <filesystem>
#include <fstream>
#include "JIT_Executor.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/jit/speculation/Async_Speculator.hpp"

namespace dgtl::jit {

class TieredController {
    dgtl::isa::DGTLJitExecutor& _jit;
    dgtl::isa::DGTLNasmEmitter _aot;
    std::unordered_map<uintptr_t, uint32_t> _hot_counts;
    const uint32_t BAKE_THRESHOLD = 10000;

public:
    TieredController(dgtl::isa::DGTLJitExecutor& jit) : _jit(jit) {}

    /**
     * @brief Record execution and check if we should bake to disk.
     */
    void profile_and_check(dgtl::isa::JitFunction func_ptr, const core::DGTLSymbolTable& symbols) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(func_ptr);
        
        if (++_hot_counts[addr] == BAKE_THRESHOLD) {
            bake_to_disk(addr, symbols);
        }
    }

    /**
     * @brief Select the best speculative version based on runtime profiling.
     * @param versions Vector of speculative versions to choose from
     */
    void select_best_version(std::vector<SpeculativeVersion>& versions) {
        if (versions.empty()) return;
        
        // Sort by confidence score (highest first)
        std::sort(versions.begin(), versions.end(), 
                  [](const SpeculativeVersion& a, const SpeculativeVersion& b) {
                      return a.confidence > b.confidence;
                  });
        
        // The best version is now at index 0
        // In production, this would also consider runtime metrics
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

} // namespace dgtl::jit


