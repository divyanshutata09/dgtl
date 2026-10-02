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

export module dgtl.linker.wasm;

import dgtl.linker.backend;
import dgtl.object.writers.wasm;
import dgtl.object.file;
import dgtl.mir;

export namespace dgtl::linker::wasm {

class WASMBinaryLink {
public:
    // Sovereign MIR-native linking
    bool link(const machine::MachineFunction& fn, std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write_machine_function(fn, out_bytes);
    }

    bool link(const dgtl::object::ObjectFile& obj, std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link(std::span<const uint8_t> code_bytes, std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write_code(code_bytes, out_bytes);
    }
};

} // namespace dgtl::linker::wasm

export namespace dgtl::backend::linker::wasm {
    using WASMBinaryLink = dgtl::linker::wasm::WASMBinaryLink;
}



