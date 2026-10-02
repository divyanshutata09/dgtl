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

export module dgtl.linker.elf;

import dgtl.linker.backend;
import dgtl.object.writers.elf;
import dgtl.object.file;
import dgtl.mir;

export namespace dgtl::linker::elf {

class ELFLinker {
public:
    // Sovereign MIR-native linking
    bool link_relocatable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write_machine_function(fn, out_bytes, entry_symbol);
    }

    bool link_relocatable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link_static_archive(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write_machine_function(fn, obj_bytes, fn.name())) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    bool link_static_archive(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        std::string_view member_name = "module.o") const {
        std::vector<uint8_t> obj_bytes;
        dgtl::object::writers::ELFObjectWriter writer;
        if (!writer.write(obj, obj_bytes)) return false;
        return writer.build_archive(obj_bytes, out_bytes, member_name);
    }

    bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        uint64_t image_base = 0x400000) const {
        (void)image_base;
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link_executable(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        dgtl::object::writers::ELFObjectWriter writer;
        return writer.write_machine_function(fn, out_bytes, entry_symbol);
    }
};

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using ELFLinker = dgtl::linker::elf::ELFLinker;
}



