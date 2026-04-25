/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl/jit/orchestrator/MultiArch_Orchestrator.hpp"

#include <fstream>
#include <vector>

#include "dgtl/isa/linker/Linker_Backend.hpp"
#include "dgtl/isa/target/Wasm_Target.hpp"
#include "dgtl/isa/emitter/MachOs_Emitter.hpp"
#include "dgtl/core/Symbol_Table.hpp"

namespace dgtl::core {

void DGTLOrchestrator::dispatch_to_linker(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    // Pass the orchestrator's target context to the linker for hardware-specific ELF headers
    linker.emit_elf64(path, stream);
}

void DGTLOrchestrator::dispatch_to_relocatable(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    // Ensure relocatable objects carry the correct ISA markers from our TargetMachine
    if (!linker.emit_elf_relocatable(path, stream, "DGTL_kernel_entry")) {
        trigger_fatal_trap("ELF_RELOCATABLE_WRITE_FAILED", "Unable to emit ELF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_archive(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.emit_static_archive(path, stream, "module.o")) {
        trigger_fatal_trap("ARCHIVE_WRITE_FAILED", "Unable to emit static archive");
    }
}

void DGTLOrchestrator::dispatch_to_coff_relocatable(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.emit_coff_relocatable(path, stream, "DGTL_kernel_entry")) {
        trigger_fatal_trap("COFF_RELOCATABLE_WRITE_FAILED", "Unable to emit COFF relocatable object");
    }
}

void DGTLOrchestrator::dispatch_to_coff_archive(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.emit_coff_archive(path, stream, "module.obj", "DGTL_kernel_entry")) {
        trigger_fatal_trap("COFF_ARCHIVE_WRITE_FAILED", "Unable to emit COFF archive");
    }
}

void DGTLOrchestrator::dispatch_to_macho(std::string_view path, const ir::IRStream& stream) {
    dgtl::core::SymbolTable symbols;
    // Mach-O emission is architecture-sensitive (ARM64 vs x86_64)
    dgtl::isa::MachOEmitter emitter(stream, symbols);
    
    if (!emitter.save_to_file(std::string(path))) {
        trigger_fatal_trap("MACHO_WRITE_FAILED", "Failed to emit Mach-O object binary");
    }
}

void DGTLOrchestrator::dispatch_to_wasm(std::string_view path, const ir::IRStream& stream) {
    dgtl::exec::DGTLUniversalLinker linker;
    // WebAssembly ignores native SIMD detection but follows the WASM-specific spec
    linker.emit_wasm(path, stream);
}

} // namespace dgtl::core