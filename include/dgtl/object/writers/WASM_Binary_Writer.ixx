/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly Binary Module (.wasm) Writer
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <type_traits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module dgtl.object.writers.wasm;

import dgtl.object.file;
import dgtl.object.writer;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.mir.machine_ir;

export namespace dgtl::object::writers {

class WASMBinaryWriter final : public dgtl::object::ObjectWriter {
public:
    [[nodiscard]] std::string_view format_name() const noexcept override {
        return "WebAssembly";
    }

    [[nodiscard]] bool write(
        const dgtl::object::ObjectFile& obj,
        std::vector<std::uint8_t>& out) const override {

        out.clear();

        // 1. WASM Header (8 bytes): \0asm + version 1
        out.push_back(0x00); out.push_back(0x61); out.push_back(0x73); out.push_back(0x6D);
        out.push_back(0x01); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);

        auto emit_u32_leb = [](std::vector<std::uint8_t>& buf, std::uint32_t val) {
            do {
                std::uint8_t byte = val & 0x7Fu;
                val >>= 7u;
                if (val != 0) byte |= 0x80u;
                buf.push_back(byte);
            } while (val != 0);
        };

        auto emit_section = [&](std::uint8_t sec_id, const std::vector<std::uint8_t>& payload) {
            out.push_back(sec_id);
            emit_u32_leb(out, static_cast<std::uint32_t>(payload.size()));
            out.insert(out.end(), payload.begin(), payload.end());
        };

        // Section 1: Type Section (func: () -> i32)
        std::vector<std::uint8_t> type_payload;
        emit_u32_leb(type_payload, 1); // 1 type
        type_payload.push_back(0x60);  // func
        emit_u32_leb(type_payload, 0); // 0 params
        emit_u32_leb(type_payload, 1); // 1 result
        type_payload.push_back(0x7F);  // i32
        emit_section(1, type_payload);

        // Section 3: Function Section
        std::vector<std::uint8_t> func_payload;
        emit_u32_leb(func_payload, 1); // 1 function
        emit_u32_leb(func_payload, 0); // type 0
        emit_section(3, func_payload);

        // Section 5: Memory Section
        std::vector<std::uint8_t> mem_payload;
        emit_u32_leb(mem_payload, 1); // 1 memory
        mem_payload.push_back(0x00);  // limits: min only
        emit_u32_leb(mem_payload, 1); // min 1 page (64KB)
        emit_section(5, mem_payload);

        // Section 7: Export Section
        std::vector<std::uint8_t> export_payload;
        emit_u32_leb(export_payload, 2); // 2 exports

        // Export "DGTL_kernel_entry" function
        const std::string fn_name = "DGTL_kernel_entry";
        emit_u32_leb(export_payload, static_cast<std::uint32_t>(fn_name.size()));
        for (char c : fn_name) export_payload.push_back(static_cast<std::uint8_t>(c));
        export_payload.push_back(0x00); // exportkind: func
        emit_u32_leb(export_payload, 0); // func index 0

        // Export "memory"
        const std::string mem_name = "memory";
        emit_u32_leb(export_payload, static_cast<std::uint32_t>(mem_name.size()));
        for (char c : mem_name) export_payload.push_back(static_cast<std::uint8_t>(c));
        export_payload.push_back(0x02); // exportkind: memory
        emit_u32_leb(export_payload, 0); // mem index 0
        emit_section(7, export_payload);

        // Section 10: Code Section
        const auto* text = obj.find_section(".text");
        std::vector<std::uint8_t> code_payload;
        emit_u32_leb(code_payload, 1); // 1 func body

        std::vector<std::uint8_t> body;
        emit_u32_leb(body, 0); // 0 locals

        if (text && !text->bytes.empty()) {
            body.insert(body.end(), text->bytes.begin(), text->bytes.end());
        } else {
            // i32.const 0, end
            body.push_back(0x41);
            body.push_back(0x00);
            body.push_back(0x0B);
        }

        emit_u32_leb(code_payload, static_cast<std::uint32_t>(body.size()));
        code_payload.insert(code_payload.end(), body.begin(), body.end());
        emit_section(10, code_payload);

        return true;
    }

    [[nodiscard]] bool write(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out) const {
        (void)function;
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::WASM);
        return write(obj, out);
    }

    [[nodiscard]] bool write_machine_function(
        const machine::MachineFunction& function,
        std::vector<std::uint8_t>& out) const {
        return write(function, out);
    }

    [[nodiscard]] bool write_code(
        std::span<const std::uint8_t> code,
        std::vector<std::uint8_t>& out) const {
        dgtl::object::ObjectFile obj(dgtl::object::ObjectFormat::WASM);
        auto& text = obj.get_text_section();
        text.append_bytes(code.data(), code.size());
        obj.add_function_symbol("DGTL_kernel_entry", 0u, code.size(), true);
        return write(obj, out);
    }

    // Preserved compatibility entry point, constrained to MachineFunction.
    template <typename MachineFunctionLike>
        requires std::same_as<std::remove_cvref_t<MachineFunctionLike>,
                              machine::MachineFunction>
    [[nodiscard]] bool write_machine_function_legacy(
        const MachineFunctionLike& function,
        std::vector<std::uint8_t>& out) const {
        return write(static_cast<const machine::MachineFunction&>(function), out);
    }
};

} // namespace dgtl::object::writers

export namespace dgtl::backend::object {
    using WASMBinaryWriter = dgtl::object::writers::WASMBinaryWriter;
}



