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

module;

#include <cstdint>
#include <string_view>
#include <vector>
#include <span>

export module dgtl.linker.driver_api;

import dgtl.linker.backend;
import dgtl.linker.elf;
import dgtl.linker.wasm;
import dgtl.object.writers.pe;
import dgtl.object.writers.elf;
import dgtl.object.writers.macho;
import dgtl.object.writers.wasm;
import dgtl.object.file;
import dgtl.mir;

export namespace dgtl::linker {

enum class OutputFormat : uint8_t {
    ELFObject,
    StaticArchive,
    COFFObject,
    COFFArchive,
    PEExecutable,
    WASMBinary,
    MachOObject
};

class LinkerDriver {
public:
    // Sovereign MIR MachineFunction emission
    bool emit(
        OutputFormat format,
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        switch (format) {
            case OutputFormat::ELFObject: {
                dgtl::object::writers::ELFObjectWriter writer;
                return writer.write_machine_function(fn, out_bytes, entry_symbol);
            }
            case OutputFormat::StaticArchive: {
                elf::ELFLinker l;
                return l.link_static_archive(fn, out_bytes, "module.o");
            }
            case OutputFormat::COFFObject: {
                dgtl::object::writers::PEObjectWriter writer;
                return writer.write_machine_function(fn, out_bytes, entry_symbol);
            }
            case OutputFormat::COFFArchive: {
                dgtl::object::writers::PEObjectWriter writer;
                std::vector<uint8_t> obj;
                if (!writer.write_machine_function(fn, obj, entry_symbol)) return false;
                return writer.build_archive(obj, out_bytes, "module.obj");
            }
            case OutputFormat::PEExecutable: {
                dgtl::object::writers::PEObjectWriter writer;
                return writer.write_standalone_executable(fn, out_bytes, entry_symbol);
            }
            case OutputFormat::WASMBinary: {
                wasm::WASMBinaryLink l;
                return l.link(fn, out_bytes);
            }
            case OutputFormat::MachOObject: {
                dgtl::object::writers::MachOObjectWriter writer;
                return writer.write_machine_function(fn, out_bytes, entry_symbol);
            }
        }
        return false;
    }

    // Universal ObjectFile emission
    bool emit(
        OutputFormat format,
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes) const {
        switch (format) {
            case OutputFormat::ELFObject: {
                dgtl::object::writers::ELFObjectWriter writer;
                return writer.write(obj, out_bytes);
            }
            case OutputFormat::StaticArchive: {
                elf::ELFLinker l;
                return l.link_static_archive(obj, out_bytes, "module.o");
            }
            case OutputFormat::COFFObject: {
                dgtl::object::writers::PEObjectWriter writer;
                return writer.write(obj, out_bytes);
            }
            case OutputFormat::COFFArchive: {
                dgtl::object::writers::PEObjectWriter writer;
                std::vector<uint8_t> coff_bytes;
                if (!writer.write(obj, coff_bytes)) return false;
                return writer.build_archive(coff_bytes, out_bytes, "module.obj");
            }
            case OutputFormat::PEExecutable: {
                dgtl::object::writers::PEObjectWriter writer;
                return writer.write_standalone_executable(obj, out_bytes);
            }
            case OutputFormat::WASMBinary: {
                wasm::WASMBinaryLink l;
                return l.link(obj, out_bytes);
            }
            case OutputFormat::MachOObject: {
                dgtl::object::writers::MachOObjectWriter writer;
                return writer.write(obj, out_bytes);
            }
        }
        return false;
    }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using LinkerDriver = dgtl::linker::LinkerDriver;
    using OutputFormat = dgtl::linker::OutputFormat;
}



