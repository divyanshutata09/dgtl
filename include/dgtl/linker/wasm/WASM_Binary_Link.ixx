/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Sovereign WebAssembly (WASM) Standalone Binary Linker
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 * Module:      dgtl.linker.wasm
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <span>

export module dgtl.linker.wasm;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.object.writers.wasm;
export import dgtl.linker.wasm.sections;
export import dgtl.linker.wasm.relocations;
import dgtl.mir;

export namespace dgtl::linker::wasm {

struct WASMLinkerOptions {
    std::string entry_symbol = "DGTL_kernel_entry";
    std::string import_module = "env";
    uint32_t initial_memory_pages = 2; // 128 KB
    uint32_t max_memory_pages = 256;    // 16 MB
    bool export_memory = true;
    bool export_all_globals = true;
    uint32_t data_base_address = 1024;  // 1 KB base for static data
    uint32_t stack_size = 65536;        // 64 KB stack
};

class WASMBinaryLink {
public:
    using Options = WASMLinkerOptions;

    // Sovereign In-Memory Standalone WASM Module Linker
    bool link(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        const Options& options = Options{}) const {

        out_bytes.clear();

        // 1. WASM Magic and Version Header (8 bytes)
        out_bytes.push_back(0x00);
        out_bytes.push_back(0x61);
        out_bytes.push_back(0x73);
        out_bytes.push_back(0x6D); // \0asm
        out_bytes.push_back(0x01);
        out_bytes.push_back(0x00);
        out_bytes.push_back(0x00);
        out_bytes.push_back(0x00); // version 1

        // 2. Extract Code Sections (.text)
        const auto* text_sec = obj.find_section(".text");
        if (!text_sec || text_sec->bytes.empty()) {
            for (const auto& s : obj.sections()) {
                if (s.is_code() && !s.bytes.empty()) {
                    text_sec = &s;
                    break;
                }
            }
        }

        std::vector<uint8_t> code_bytes;
        if (text_sec && !text_sec->bytes.empty()) {
            code_bytes = text_sec->bytes;
        } else {
            // Default minimal function: i32.const 0; end
            code_bytes = {0x41, 0x00, 0x0B};
        }

        // 3. Extract and Pack Data Sections (.rodata, .data, .rdata)
        std::vector<uint8_t> data_bytes;
        std::unordered_map<std::string, uint32_t> data_symbol_addresses;
        uint32_t cur_data_offset = 0;

        for (const auto& sec : obj.sections()) {
            if (sec.name == ".rodata" || sec.name == ".data" || sec.name == ".rdata") {
                while ((data_bytes.size() % 16) != 0) data_bytes.push_back(0);
                const uint32_t base = static_cast<uint32_t>(data_bytes.size());
                data_bytes.insert(data_bytes.end(), sec.bytes.begin(), sec.bytes.end());

                // Index symbols residing within this data section
                for (const auto& sym : obj.symbols()) {
                    if (sym.is_defined() && sym.section_index < obj.sections().size() &&
                        obj.sections()[sym.section_index].name == sec.name) {
                        data_symbol_addresses[sym.name] = options.data_base_address + base + static_cast<uint32_t>(sym.value);
                    }
                }
            }
        }

        // 4. Identify Undefined External Imports from Relocations
        std::vector<std::string> imported_functions;
        std::unordered_set<std::string> seen_imports;
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            const auto* sym = obj.find_symbol(rel.symbol_name);
            if (!sym || sym->is_external()) {
                if (seen_imports.insert(rel.symbol_name).second) {
                    imported_functions.push_back(rel.symbol_name);
                }
            }
        }

        // 5. Index Functions (Imports first, then defined functions)
        std::unordered_map<std::string, uint32_t> func_index_map;
        uint32_t next_func_idx = 0;
        for (const auto& imp : imported_functions) {
            func_index_map[imp] = next_func_idx++;
        }

        // Enumerate defined function symbols
        std::vector<std::string> defined_functions;
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined() && (sym.is_function() || sym.name == options.entry_symbol || sym.name == "main")) {
                if (!func_index_map.contains(sym.name)) {
                    func_index_map[sym.name] = next_func_idx++;
                    defined_functions.push_back(sym.name);
                }
            }
        }

        if (defined_functions.empty()) {
            defined_functions.push_back(options.entry_symbol);
            func_index_map[options.entry_symbol] = next_func_idx++;
        }

        const uint32_t num_imports = static_cast<uint32_t>(imported_functions.size());
        const uint32_t num_defined = static_cast<uint32_t>(defined_functions.size());

        // 6. Section 1: Type Section (Function Signatures)
        // Type 0: () -> i32 (standard entry / main)
        // Type 1: (i32) -> () (standard print / exit)
        // Type 2: (i32, i32) -> i32 (binary op / write)
        // Type 3: () -> () (void entry)
        std::vector<uint8_t> type_payload;
        encode_u32_leb(type_payload, 4); // 4 signatures

        // Type 0: () -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 0); // 0 params
        encode_u32_leb(type_payload, 1); // 1 result
        type_payload.push_back(kValTypeI32);

        // Type 1: (i32) -> ()
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1); // 1 param
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 0); // 0 results

        // Type 2: (i32, i32) -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 2); // 2 params
        type_payload.push_back(kValTypeI32);
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 1); // 1 result
        type_payload.push_back(kValTypeI32);

        // Type 3: () -> ()
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 0);
        encode_u32_leb(type_payload, 0);

        encode_section(out_bytes, kSectionType, type_payload);

        // 7. Section 2: Import Section
        if (num_imports > 0) {
            std::vector<uint8_t> import_payload;
            encode_u32_leb(import_payload, num_imports);
            for (const auto& fn : imported_functions) {
                encode_string(import_payload, options.import_module);
                encode_string(import_payload, fn);
                import_payload.push_back(kExternalFunction);
                encode_u32_leb(import_payload, 0); // Default to Type 0
            }
            encode_section(out_bytes, kSectionImport, import_payload);
        }

        // 8. Section 3: Function Section
        std::vector<uint8_t> func_payload;
        encode_u32_leb(func_payload, num_defined);
        for (uint32_t i = 0; i < num_defined; ++i) {
            encode_u32_leb(func_payload, 0); // Type 0: () -> i32
        }
        encode_section(out_bytes, kSectionFunction, func_payload);

        // 9. Section 5: Memory Section
        std::vector<uint8_t> mem_payload;
        encode_u32_leb(mem_payload, 1); // 1 linear memory

        // Calculate pages needed for static data + stack
        const uint32_t total_static_bytes = options.data_base_address + static_cast<uint32_t>(data_bytes.size()) + options.stack_size;
        const uint32_t min_pages_required = std::max(options.initial_memory_pages, (total_static_bytes + kWasmPageBytes - 1) / kWasmPageBytes);

        mem_payload.push_back(0x01); // flags: has maximum
        encode_u32_leb(mem_payload, min_pages_required);
        encode_u32_leb(mem_payload, options.max_memory_pages);
        encode_section(out_bytes, kSectionMemory, mem_payload);

        // 10. Section 6: Global Section (__stack_pointer)
        std::vector<uint8_t> global_payload;
        encode_u32_leb(global_payload, 1); // 1 global
        global_payload.push_back(kValTypeI32);
        global_payload.push_back(0x01); // mutable
        // init expr: i32.const <stack_top>; end
        global_payload.push_back(kOpcodeI32Const);
        encode_i32_sleb(global_payload, static_cast<int32_t>(total_static_bytes));
        global_payload.push_back(kOpcodeEnd);
        encode_section(out_bytes, kSectionGlobal, global_payload);

        // 11. Section 7: Export Section
        std::vector<WasmExportSpec> exports;

        // Entry point export
        uint32_t entry_func_idx = num_imports; // First defined function
        auto it_entry = func_index_map.find(options.entry_symbol);
        if (it_entry != func_index_map.end()) {
            entry_func_idx = it_entry->second;
        } else {
            auto it_main = func_index_map.find("main");
            if (it_main != func_index_map.end()) {
                entry_func_idx = it_main->second;
            }
        }
        exports.push_back({options.entry_symbol, kExternalFunction, entry_func_idx});
        if (options.entry_symbol != "main" && !func_index_map.contains("main")) {
            exports.push_back({"main", kExternalFunction, entry_func_idx});
        }

        // Memory export
        if (options.export_memory) {
            exports.push_back({"memory", kExternalMemory, 0});
        }

        // Stack pointer export
        exports.push_back({"__stack_pointer", kExternalGlobal, 0});

        // Other defined globals if requested
        if (options.export_all_globals) {
            for (const auto& sym : obj.symbols()) {
                if (sym.is_defined() && sym.is_global() && sym.is_function() &&
                    sym.name != options.entry_symbol && sym.name != "main") {
                    auto it_fn = func_index_map.find(sym.name);
                    if (it_fn != func_index_map.end()) {
                        exports.push_back({sym.name, kExternalFunction, it_fn->second});
                    }
                }
            }
        }

        std::vector<uint8_t> export_payload;
        encode_u32_leb(export_payload, static_cast<uint32_t>(exports.size()));
        for (const auto& exp : exports) {
            encode_string(export_payload, exp.name);
            export_payload.push_back(exp.kind);
            encode_u32_leb(export_payload, exp.index);
        }
        encode_section(out_bytes, kSectionExport, export_payload);

        // 12. Section 10: Code Section
        // Apply relocations to code
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;

            // Function call relocation
            auto it_fn = func_index_map.find(rel.symbol_name);
            if (it_fn != func_index_map.end()) {
                const uint32_t target_idx = it_fn->second;
                if (rel.offset + 5 <= code_bytes.size()) {
                    WasmRelocationApplier::patch_u32_leb_5(code_bytes.data() + rel.offset, target_idx);
                }
                continue;
            }

            // Data address relocation
            auto it_data = data_symbol_addresses.find(rel.symbol_name);
            if (it_data != data_symbol_addresses.end()) {
                const uint32_t target_addr = it_data->second + static_cast<uint32_t>(rel.addend);
                if (rel.offset + 5 <= code_bytes.size()) {
                    WasmRelocationApplier::patch_u32_leb_5(code_bytes.data() + rel.offset, target_addr);
                }
            }
        }

        std::vector<uint8_t> code_payload;
        encode_u32_leb(code_payload, num_defined);

        // Emit code for the defined functions
        for (uint32_t i = 0; i < num_defined; ++i) {
            const std::string& fn_name = defined_functions[i];
            const auto* sym = obj.find_symbol(fn_name);

            std::vector<uint8_t> func_body;
            encode_u32_leb(func_body, 0); // 0 local declarations

            bool sliced = false;
            if (num_defined == 1) {
                if (!code_bytes.empty()) {
                    func_body.insert(func_body.end(), code_bytes.begin(), code_bytes.end());
                    sliced = true;
                }
            } else if (sym && sym->value < code_bytes.size()) {
                size_t start = static_cast<size_t>(sym->value);
                size_t len = 0;
                if (sym->size > 0 && start + sym->size <= code_bytes.size()) {
                    len = static_cast<size_t>(sym->size);
                } else {
                    size_t next_start = code_bytes.size();
                    for (const auto& other_name : defined_functions) {
                        if (const auto* other = obj.find_symbol(other_name)) {
                            if (other->value > sym->value && other->value < next_start) {
                                next_start = static_cast<size_t>(other->value);
                            }
                        }
                    }
                    len = next_start - start;
                }
                if (len > 0) {
                    func_body.insert(func_body.end(), code_bytes.begin() + start, code_bytes.begin() + start + len);
                    sliced = true;
                }
            }

            if (sliced) {
                if (func_body.empty() || func_body.back() != kOpcodeEnd) {
                    func_body.push_back(kOpcodeEnd);
                }
            } else {
                // Return default i32.const 0; end
                func_body.push_back(kOpcodeI32Const);
                func_body.push_back(0x00);
                func_body.push_back(kOpcodeEnd);
            }

            encode_u32_leb(code_payload, static_cast<uint32_t>(func_body.size()));
            code_payload.insert(code_payload.end(), func_body.begin(), func_body.end());
        }
        encode_section(out_bytes, kSectionCode, code_payload);

        // 13. Section 11: Data Section (Linear memory initialization)
        if (!data_bytes.empty()) {
            std::vector<uint8_t> data_payload;
            encode_u32_leb(data_payload, 1); // 1 active segment
            data_payload.push_back(0x00);    // segment flags: active, memory index 0

            // Offset init expr: i32.const <data_base_address>; end
            data_payload.push_back(kOpcodeI32Const);
            encode_i32_sleb(data_payload, static_cast<int32_t>(options.data_base_address));
            data_payload.push_back(kOpcodeEnd);

            encode_u32_leb(data_payload, static_cast<uint32_t>(data_bytes.size()));
            data_payload.insert(data_payload.end(), data_bytes.begin(), data_bytes.end());

            encode_section(out_bytes, kSectionData, data_payload);
        }

        return true;
    }

    bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        std::string_view entry_symbol = "DGTL_kernel_entry") const {
        Options opts;
        opts.entry_symbol = std::string(entry_symbol);
        return link(obj, out_bytes, opts);
    }

    bool link_relocatable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write(obj, out_bytes);
    }

    bool link(
        const machine::MachineFunction& fn,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write_machine_function(fn, out_bytes);
    }

    bool link(
        std::span<const uint8_t> code_bytes,
        std::vector<uint8_t>& out_bytes) const {
        dgtl::object::writers::WASMBinaryWriter writer;
        return writer.write_code(code_bytes, out_bytes);
    }
};

} // namespace dgtl::linker::wasm

export namespace dgtl::backend::linker::wasm {
    using WASMBinaryLink = dgtl::linker::wasm::WASMBinaryLink;
    using WASMLinkerOptions = dgtl::linker::wasm::WASMLinkerOptions;
}
