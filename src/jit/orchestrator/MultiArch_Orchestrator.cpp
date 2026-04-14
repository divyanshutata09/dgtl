#include "dgtl/jit/orchestrator/MultiArch_Orchestrator.hpp"

#include <fstream>
#include <vector>

#include "dgtl/isa/linker/Linker_Backend.hpp"
#include "dgtl/isa/target/Wasm_Target.hpp"

namespace dgtl::core {

void DGTLOrchestrator::dispatch_to_linker(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    linker.emit_elf64(path, stream);
}

void DGTLOrchestrator::dispatch_to_macho(std::string_view path, const ir::IRStream& stream) {
    std::vector<uint32_t> machine_code;
    machine_code.reserve(stream.size());

    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        machine_code.push_back(static_cast<uint32_t>(node.opcode));
    }

    const auto blob = dgtl::target::DGTLAsmEmitter::emit_macho_binary(machine_code);
    std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        trigger_fatal_trap("MACHO_WRITE_FAILED", "Unable to open output file for Mach-O emission");
    }
    if (!blob.empty()) {
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }
    if (!out.good()) {
        trigger_fatal_trap("MACHO_WRITE_FAILED", "Failed while writing Mach-O blob");
    }
}

void DGTLOrchestrator::dispatch_to_wasm(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    linker.emit_wasm(path, stream);
}

} // namespace dgtl::core
