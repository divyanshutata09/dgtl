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
    bool sovereign = true;              // Standalone sovereign mode: synthesizes math intrinsics natively
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

        // 2. Extract and Consolidate Code Sections (.text, .text.*)
        std::vector<uint8_t> code_bytes;
        std::vector<uint32_t> section_code_offsets(obj.sections().size(), 0);
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& s = obj.sections()[i];
            if (s.is_code() || s.name == ".text" || s.name.starts_with(".text.")) {
                if (!s.bytes.empty()) {
                    section_code_offsets[i] = static_cast<uint32_t>(code_bytes.size());
                    code_bytes.insert(code_bytes.end(), s.bytes.begin(), s.bytes.end());
                }
            }
        }

        if (code_bytes.empty()) {
            // Default minimal function: i32.const 0; end
            code_bytes = {0x41, 0x00, 0x0B};
        }

        // 3. Extract and Pack Data Sections (.rodata, .data, .rdata, .bss)
        std::vector<uint8_t> data_bytes;
        std::unordered_map<std::string, uint32_t> data_symbol_addresses;
        uint32_t total_bss_bytes = 0;

        for (const auto& sec : obj.sections()) {
            if (sec.is_code()) continue;
            if (sec.is_bss() || sec.name == ".bss") {
                const uint32_t align_val = static_cast<uint32_t>(std::max<uint64_t>(16u, sec.alignment));
                const uint32_t rem = total_bss_bytes % align_val;
                if (rem != 0) total_bss_bytes += (align_val - rem);
                const uint32_t bss_base = total_bss_bytes;
                total_bss_bytes += static_cast<uint32_t>(sec.bss_size);

                // Index symbols residing within this BSS section
                for (const auto& sym : obj.symbols()) {
                    if (sym.is_defined() && sym.section_index < obj.sections().size() &&
                        obj.sections()[sym.section_index].name == sec.name) {
                        data_symbol_addresses[sym.name] = options.data_base_address + static_cast<uint32_t>(data_bytes.size()) + bss_base + static_cast<uint32_t>(sym.value);
                    }
                }
            } else if (sec.name == ".rodata" || sec.name == ".data" || sec.name == ".rdata" || (!sec.empty() && sec.name != ".text")) {
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
        std::vector<std::string> sovereign_intrinsics;
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            const auto* sym = obj.find_symbol(rel.symbol_name);
            if (!sym) {
                auto pos = rel.symbol_name.rfind("__");
                if (pos != std::string::npos && pos + 2 < rel.symbol_name.size()) {
                    sym = obj.find_symbol(rel.symbol_name.substr(pos + 2));
                }
            }
            if (!sym || sym->is_external()) {
                std::string_view stripped = rel.symbol_name;
                if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
                else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
                else if (stripped.starts_with("__myzon_")) stripped.remove_prefix(8);
                else if (stripped.starts_with("myzon_")) stripped.remove_prefix(6);
                else if (stripped.starts_with("__")) stripped.remove_prefix(2);
                else if (stripped.starts_with("_")) stripped.remove_prefix(1);

                bool is_intrinsic = rel.symbol_name.starts_with("__dgtl_rt_") || rel.symbol_name.starts_with("dgtl_rt_") ||
                                    rel.symbol_name.starts_with("__myzon_") || rel.symbol_name.starts_with("myzon_") ||
                                    stripped == "sqrt" || stripped == "sqrtf" || stripped == "abs" || stripped == "absf" ||
                                    stripped == "fabs" || stripped == "fabsf" || stripped == "floor" || stripped == "floorf" ||
                                    stripped == "ceil" || stripped == "ceilf" || stripped == "round" || stripped == "roundf" ||
                                    stripped == "trunc" || stripped == "truncf" || stripped == "memcpy" || stripped == "memset" ||
                                    stripped == "memcmp" || stripped == "memmove" || stripped == "chkstk" || stripped == "alloca" ||
                                    stripped == "pow" || stripped == "i64_pow" || stripped == "ipow" ||
                                    stripped == "fmod" || stripped == "fmodf" ||
                                    stripped == "bits_to_f64" || stripped == "f64_to_bits" ||
                                    stripped == "bits_to_f32" || stripped == "f32_to_bits" ||
                                    stripped == "relu" || stripped == "reluf" ||
                                    stripped == "fma_f32" || stripped == "fma_f64";
                if (options.sovereign && is_intrinsic) {
                    if (seen_imports.insert(rel.symbol_name).second) {
                        sovereign_intrinsics.push_back(rel.symbol_name);
                    }
                } else {
                    if (seen_imports.insert(rel.symbol_name).second) {
                        imported_functions.push_back(rel.symbol_name);
                    }
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

        for (const auto& intrin : sovereign_intrinsics) {
            if (!func_index_map.contains(intrin)) {
                func_index_map[intrin] = next_func_idx++;
                defined_functions.push_back(intrin);
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
        // Type 4: (f64) -> f64 (f64 unary math)
        // Type 5: (f32) -> f32 (f32 unary math)
        // Type 6: (i32, i32, i32) -> i32 (memcpy / memset / memmove / memcmp)
        // Type 7: (i64, i64) -> i64 (pow / i64_pow)
        // Type 8: (f64, f64) -> f64 (fmod)
        // Type 9: (f32, f32) -> f32 (fmodf)
        // Type 10: (i64) -> f64 (bits_to_f64)
        // Type 11: (f64) -> i64 (f64_to_bits)
        // Type 12: (i32) -> f32 (bits_to_f32)
        // Type 13: (f32) -> i32 (f32_to_bits)
        // Type 14: (f64, f64, f64) -> f64 (fma_f64)
        // Type 15: (f32, f32, f32) -> f32 (fma_f32)
        std::vector<uint8_t> type_payload;
        encode_u32_leb(type_payload, 16); // 16 signatures

        // Type 0: () -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 0);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);

        // Type 1: (i32) -> ()
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 0);

        // Type 2: (i32, i32) -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 2);
        type_payload.push_back(kValTypeI32);
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);

        // Type 3: () -> ()
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 0);
        encode_u32_leb(type_payload, 0);

        // Type 4: (f64) -> f64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);

        // Type 5: (f32) -> f32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);

        // Type 6: (i32, i32, i32) -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 3);
        type_payload.push_back(kValTypeI32);
        type_payload.push_back(kValTypeI32);
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);

        // Type 7: (i64, i64) -> i64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 2);
        type_payload.push_back(kValTypeI64);
        type_payload.push_back(kValTypeI64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI64);

        // Type 8: (f64, f64) -> f64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 2);
        type_payload.push_back(kValTypeF64);
        type_payload.push_back(kValTypeF64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);

        // Type 9: (f32, f32) -> f32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 2);
        type_payload.push_back(kValTypeF32);
        type_payload.push_back(kValTypeF32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);

        // Type 10: (i64) -> f64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);

        // Type 11: (f64) -> i64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI64);

        // Type 12: (i32) -> f32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);

        // Type 13: (f32) -> i32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeI32);

        // Type 14: (f64, f64, f64) -> f64
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 3);
        type_payload.push_back(kValTypeF64);
        type_payload.push_back(kValTypeF64);
        type_payload.push_back(kValTypeF64);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF64);

        // Type 15: (f32, f32, f32) -> f32
        type_payload.push_back(kFuncTypeTag);
        encode_u32_leb(type_payload, 3);
        type_payload.push_back(kValTypeF32);
        type_payload.push_back(kValTypeF32);
        type_payload.push_back(kValTypeF32);
        encode_u32_leb(type_payload, 1);
        type_payload.push_back(kValTypeF32);

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
            const std::string& fn_name = defined_functions[i];
            uint32_t type_idx = 0; // Default to Type 0: () -> i32
            if (options.sovereign) {
                std::string_view name = fn_name;
                if (name.starts_with("__dgtl_rt_")) name.remove_prefix(10);
                else if (name.starts_with("dgtl_rt_")) name.remove_prefix(8);
                else if (name.starts_with("__myzon_")) name.remove_prefix(8);
                else if (name.starts_with("myzon_")) name.remove_prefix(6);
                else if (name.starts_with("__")) name.remove_prefix(2);
                else if (name.starts_with("_")) name.remove_prefix(1);

                if (name == "sqrtf" || name == "absf" || name == "fabsf" ||
                    name == "floorf" || name == "ceilf" || name == "truncf" || name == "roundf" || name == "reluf") {
                    type_idx = 5; // Type 5: (f32) -> f32
                } else if (name == "sqrt" || name == "abs" || name == "fabs" ||
                           name == "floor" || name == "ceil" || name == "trunc" || name == "round" || name == "relu") {
                    type_idx = 4; // Type 4: (f64) -> f64
                } else if (name == "memcpy" || name == "memset" || name == "memmove" || name == "memcmp") {
                    type_idx = 6; // Type 6: (i32, i32, i32) -> i32
                } else if (name == "pow" || name == "i64_pow" || name == "ipow") {
                    type_idx = 7; // Type 7: (i64, i64) -> i64
                } else if (name == "fmod") {
                    type_idx = 8; // Type 8: (f64, f64) -> f64
                } else if (name == "fmodf") {
                    type_idx = 9; // Type 9: (f32, f32) -> f32
                } else if (name == "bits_to_f64") {
                    type_idx = 10; // Type 10: (i64) -> f64
                } else if (name == "f64_to_bits") {
                    type_idx = 11; // Type 11: (f64) -> i64
                } else if (name == "bits_to_f32") {
                    type_idx = 12; // Type 12: (i32) -> f32
                } else if (name == "f32_to_bits") {
                    type_idx = 13; // Type 13: (f32) -> i32
                } else if (name == "fma_f64") {
                    type_idx = 14; // Type 14: (f64, f64, f64) -> f64
                } else if (name == "fma_f32") {
                    type_idx = 15; // Type 15: (f32, f32, f32) -> f32
                }
            }
            encode_u32_leb(func_payload, type_idx);
        }
        encode_section(out_bytes, kSectionFunction, func_payload);

        // 9. Section 5: Memory Section
        std::vector<uint8_t> mem_payload;
        encode_u32_leb(mem_payload, 1); // 1 linear memory

        // Calculate pages needed for static data + BSS + stack
        const uint32_t total_static_bytes = options.data_base_address + static_cast<uint32_t>(data_bytes.size()) + total_bss_bytes + options.stack_size;
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
        } else if (auto it_ke = func_index_map.find("DGTL_kernel_entry"); it_ke != func_index_map.end()) {
            entry_func_idx = it_ke->second;
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

            const uint32_t rel_code_offset = (rel.section_index < section_code_offsets.size())
                ? (section_code_offsets[rel.section_index] + static_cast<uint32_t>(rel.offset))
                : static_cast<uint32_t>(rel.offset);

            // Function call relocation
            auto it_fn = func_index_map.find(rel.symbol_name);
            if (it_fn == func_index_map.end()) {
                auto pos = rel.symbol_name.rfind("__");
                if (pos != std::string::npos && pos + 2 < rel.symbol_name.size()) {
                    it_fn = func_index_map.find(rel.symbol_name.substr(pos + 2));
                }
            }
            if (it_fn != func_index_map.end()) {
                const uint32_t target_idx = it_fn->second;
                if (rel_code_offset + 5 <= code_bytes.size()) {
                    WasmRelocationApplier::patch_u32_leb_5(code_bytes.data() + rel_code_offset, target_idx);
                }
                continue;
            }

            // Data address relocation
            auto it_data = data_symbol_addresses.find(rel.symbol_name);
            if (it_data == data_symbol_addresses.end()) {
                auto pos = rel.symbol_name.rfind("__");
                if (pos != std::string::npos && pos + 2 < rel.symbol_name.size()) {
                    it_data = data_symbol_addresses.find(rel.symbol_name.substr(pos + 2));
                }
            }
            if (it_data != data_symbol_addresses.end()) {
                const uint32_t target_addr = it_data->second + static_cast<uint32_t>(rel.addend);
                if (rel_code_offset + 5 <= code_bytes.size()) {
                    WasmRelocationApplier::patch_u32_leb_5(code_bytes.data() + rel_code_offset, target_addr);
                }
            } else if (rel.symbol_name.starts_with("__str_") || rel.symbol_name.starts_with(".rdata") || rel.symbol_name.starts_with(".rodata")) {
                uint32_t eff_val = 0;
                if (rel.symbol_name.starts_with("__str_")) {
                    try {
                        eff_val = static_cast<uint32_t>(std::stoull(rel.symbol_name.substr(6)));
                    } catch (...) {}
                }
                const uint32_t target_addr = options.data_base_address + eff_val + static_cast<uint32_t>(rel.addend);
                if (rel_code_offset + 5 <= code_bytes.size()) {
                    WasmRelocationApplier::patch_u32_leb_5(code_bytes.data() + rel_code_offset, target_addr);
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
            encode_u32_leb(func_body, 0); // Default: 0 local declarations

            bool sliced = false;
            std::string_view int_name = fn_name;
            if (int_name.starts_with("__dgtl_rt_")) int_name.remove_prefix(10);
            else if (int_name.starts_with("dgtl_rt_")) int_name.remove_prefix(8);
            else if (int_name.starts_with("__")) int_name.remove_prefix(2);
            else if (int_name.starts_with("_")) int_name.remove_prefix(1);

            bool is_sovereign_intrin = options.sovereign && (
                fn_name.starts_with("__dgtl_rt_") || fn_name.starts_with("dgtl_rt_") ||
                int_name == "memcpy" || int_name == "memset" || int_name == "memmove" || int_name == "memcmp" ||
                int_name == "chkstk" || int_name == "_chkstk_ms" || int_name == "chkstk_ms" || int_name == "alloca" ||
                int_name == "sqrt" || int_name == "sqrtf" || int_name == "abs" || int_name == "absf" ||
                int_name == "fabs" || int_name == "fabsf" || int_name == "floor" || int_name == "floorf" ||
                int_name == "ceil" || int_name == "ceilf" || int_name == "round" || int_name == "roundf" ||
                int_name == "trunc" || int_name == "truncf" ||
                int_name == "relu" || int_name == "reluf" ||
                int_name == "fma_f32" || int_name == "fma_f64" ||
                int_name == "pow" || int_name == "i64_pow" || int_name == "ipow" ||
                int_name == "fmod" || int_name == "fmodf" ||
                int_name == "bits_to_f64" || int_name == "f64_to_bits" ||
                int_name == "bits_to_f32" || int_name == "f32_to_bits"
            );

            if (is_sovereign_intrin) {
                if (int_name == "chkstk" || int_name == "_chkstk_ms" || int_name == "chkstk_ms" || int_name == "alloca") {
                    func_body.insert(func_body.end(), { 0x41, 0x00, 0x0B }); // i32.const 0, end
                } else if (int_name == "sqrt") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x9F, 0x0B }); // local.get 0, f64.sqrt, end
                } else if (int_name == "sqrtf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x91, 0x0B }); // local.get 0, f32.sqrt, end
                } else if (int_name == "abs" || int_name == "fabs") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x99, 0x0B }); // local.get 0, f64.abs, end
                } else if (int_name == "absf" || int_name == "fabsf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x8B, 0x0B }); // local.get 0, f32.abs, end
                } else if (int_name == "floor") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x9C, 0x0B }); // local.get 0, f64.floor, end
                } else if (int_name == "floorf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x8E, 0x0B }); // local.get 0, f32.floor, end
                } else if (int_name == "ceil") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x9B, 0x0B }); // local.get 0, f64.ceil, end
                } else if (int_name == "ceilf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x8D, 0x0B }); // local.get 0, f32.ceil, end
                } else if (int_name == "trunc") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x9D, 0x0B }); // local.get 0, f64.trunc, end
                } else if (int_name == "truncf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x8F, 0x0B }); // local.get 0, f32.trunc, end
                } else if (int_name == "round") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x9E, 0x0B }); // local.get 0, f64.nearest, end
                } else if (int_name == "roundf") {
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x90, 0x0B }); // local.get 0, f32.nearest, end
                } else if (int_name == "relu") {
                    // local.get 0, f64.const 0.0, f64.max, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x0B });
                } else if (int_name == "reluf") {
                    // local.get 0, f32.const 0.0f, f32.max, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x43, 0x00, 0x00, 0x00, 0x00, 0x98, 0x0B });
                } else if (int_name == "fma_f32") {
                    // local.get 0, local.get 1, f32.mul, local.get 2, f32.add, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x01, 0x94, 0x20, 0x02, 0x92, 0x0B });
                } else if (int_name == "fma_f64") {
                    // local.get 0, local.get 1, f64.mul, local.get 2, f64.add, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x01, 0xA2, 0x20, 0x02, 0xA0, 0x0B });
                } else if (int_name == "bits_to_f64") {
                    // local.get 0, f64.reinterpret_i64, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0xBD, 0x0B });
                } else if (int_name == "f64_to_bits") {
                    // local.get 0, i64.reinterpret_f64, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0xBC, 0x0B });
                } else if (int_name == "bits_to_f32") {
                    // local.get 0, f32.reinterpret_i32, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0xBE, 0x0B });
                } else if (int_name == "f32_to_bits") {
                    // local.get 0, i32.reinterpret_f32, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0xBF, 0x0B });
                } else if (int_name == "fmod") {
                    // a - trunc(a / b) * b
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x00, 0x20, 0x01, 0xA3, 0x9D, 0x20, 0x01, 0xA2, 0xA1, 0x0B });
                } else if (int_name == "fmodf") {
                    // a - trunc(a / b) * b
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x00, 0x20, 0x01, 0x95, 0x8F, 0x20, 0x01, 0x94, 0x93, 0x0B });
                } else if (int_name == "pow" || int_name == "i64_pow" || int_name == "ipow") {
                    // Discrete binary exponentiation loop with 1 i64 local (res = 1)
                    func_body.clear();
                    encode_u32_leb(func_body, 1);     // 1 local declaration vector
                    encode_u32_leb(func_body, 1);     // 1 local of type i64
                    func_body.push_back(kValTypeI64); // i64 (local index 2: res)
                    func_body.insert(func_body.end(), {
                        0x42, 0x01,                   // i64.const 1
                        0x21, 0x02,                   // local.set 2 (res = 1)
                        0x02, 0x40,                   // block
                        0x03, 0x40,                   // loop
                        0x20, 0x01, 0x50, 0x0D, 0x01, // local.get 1, i64.eqz, br_if 1
                        0x20, 0x01, 0x42, 0x01, 0x83, // local.get 1, i64.const 1, i64.and
                        0x04, 0x40,                   // if
                        0x20, 0x02, 0x20, 0x00, 0x7E, 0x21, 0x02, // local.get 2, local.get 0, i64.mul, local.set 2
                        0x0B,                         // end if
                        0x20, 0x00, 0x20, 0x00, 0x7E, 0x21, 0x00, // local.get 0, local.get 0, i64.mul, local.set 0
                        0x20, 0x01, 0x42, 0x01, 0x88, 0x21, 0x01, // local.get 1, i64.const 1, i64.shr_u, local.set 1
                        0x0C, 0x00,                   // br 0
                        0x0B,                         // end loop
                        0x0B,                         // end block
                        0x20, 0x02,                   // local.get 2
                        0x0B                          // end
                    });
                } else if (int_name == "memcpy" || int_name == "memmove") {
                    // local.get 0, local.get 1, local.get 2, memory.copy 0 0, local.get 0, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0A, 0x00, 0x00, 0x20, 0x00, 0x0B });
                } else if (int_name == "memset") {
                    // local.get 0, local.get 1, local.get 2, memory.fill 0, local.get 0, end
                    func_body.insert(func_body.end(), { 0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xFC, 0x0B, 0x00, 0x20, 0x00, 0x0B });
                } else if (int_name == "memcmp") {
                    // Byte-by-byte memory comparison loop returning difference
                    func_body.insert(func_body.end(), {
                        0x02, 0x40,                               // block
                        0x03, 0x40,                               // loop
                        0x20, 0x02, 0x45, 0x0D, 0x01,             // local.get 2, i32.eqz, br_if 1
                        0x20, 0x00, 0x2D, 0x00, 0x00,             // local.get 0, i32.load8_u 0 0
                        0x20, 0x01, 0x2D, 0x00, 0x00,             // local.get 1, i32.load8_u 0 0
                        0x47,                                     // i32.ne
                        0x04, 0x40,                               // if
                        0x20, 0x00, 0x2D, 0x00, 0x00,             // local.get 0, i32.load8_u 0 0
                        0x20, 0x01, 0x2D, 0x00, 0x00,             // local.get 1, i32.load8_u 0 0
                        0x6B,                                     // i32.sub
                        0x0F,                                     // return
                        0x0B,                                     // end
                        0x20, 0x00, 0x41, 0x01, 0x6A, 0x21, 0x00, // local.get 0, i32.const 1, i32.add, local.set 0
                        0x20, 0x01, 0x41, 0x01, 0x6A, 0x21, 0x01, // local.get 1, i32.const 1, i32.add, local.set 1
                        0x20, 0x02, 0x41, 0x01, 0x6B, 0x21, 0x02, // local.get 2, i32.const 1, i32.sub, local.set 2
                        0x0C, 0x00,                               // br 0
                        0x0B,                                     // end loop
                        0x0B,                                     // end block
                        0x41, 0x00, 0x0B                          // i32.const 0, end
                    });
                } else {
                    func_body.insert(func_body.end(), { 0x41, 0x00, 0x0B });       // i32.const 0, end
                }
                sliced = true;
            } else if (num_defined == 1) {
                if (!code_bytes.empty()) {
                    if (code_bytes[0] == 0x00 && code_bytes.size() > 1 &&
                        (code_bytes[1] == 0x20 || code_bytes[1] == 0x41 || code_bytes[1] == 0x42 ||
                         code_bytes[1] == 0x43 || code_bytes[1] == 0x44 || code_bytes[1] == 0x01 ||
                         code_bytes[1] == 0x02 || code_bytes[1] == 0x03 || code_bytes[1] == 0x0B ||
                         code_bytes[1] == 0x0F || code_bytes[1] == 0x10 || code_bytes[1] == 0x28 ||
                         code_bytes[1] == 0x36)) {
                        func_body.clear();
                    } else if (code_bytes.size() >= 3 && code_bytes[0] == 0x01 && (code_bytes[2] == 0x7F || code_bytes[2] == 0x7E || code_bytes[2] == 0x7D || code_bytes[2] == 0x7C)) {
                        func_body.clear();
                    }
                    func_body.insert(func_body.end(), code_bytes.begin(), code_bytes.end());
                    sliced = true;
                }
            } else if (sym) {
                const size_t sec_off = (sym->section_index < section_code_offsets.size())
                    ? section_code_offsets[sym->section_index]
                    : 0;
                const size_t start = sec_off + static_cast<size_t>(sym->value);
                if (start < code_bytes.size()) {
                    size_t len = 0;
                    if (sym->size > 0 && start + sym->size <= code_bytes.size()) {
                        len = static_cast<size_t>(sym->size);
                    } else {
                        size_t next_start = code_bytes.size();
                        for (const auto& other_name : defined_functions) {
                            if (const auto* other = obj.find_symbol(other_name)) {
                                const size_t other_sec_off = (other->section_index < section_code_offsets.size())
                                    ? section_code_offsets[other->section_index]
                                    : 0;
                                const size_t other_start = other_sec_off + static_cast<size_t>(other->value);
                                if (other_start > start && other_start < next_start) {
                                    next_start = other_start;
                                }
                            }
                        }
                        len = next_start - start;
                    }
                    if (len > 0) {
                        const uint8_t* p = code_bytes.data() + start;
                        if (p[0] == 0x00 && len > 1 &&
                            (p[1] == 0x20 || p[1] == 0x41 || p[1] == 0x42 || p[1] == 0x43 ||
                             p[1] == 0x44 || p[1] == 0x01 || p[1] == 0x02 || p[1] == 0x03 ||
                             p[1] == 0x0B || p[1] == 0x0F || p[1] == 0x10 || p[1] == 0x28 ||
                             p[1] == 0x36)) {
                            func_body.clear();
                        } else if (len >= 3 && p[0] == 0x01 && (p[2] == 0x7F || p[2] == 0x7E || p[2] == 0x7D || p[2] == 0x7C)) {
                            func_body.clear();
                        }
                        func_body.insert(func_body.end(), code_bytes.begin() + start, code_bytes.begin() + start + len);
                        sliced = true;
                    }
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

        // 14. Section 0: Custom Name Section (Standard WebAssembly Stack Tracing)
        {
            std::vector<uint8_t> name_sec_payload;
            encode_string(name_sec_payload, "name");

            // Subsection 1: Function names
            std::vector<uint8_t> fn_names_payload;
            const uint32_t total_named_funcs = static_cast<uint32_t>(imported_functions.size() + defined_functions.size());
            encode_u32_leb(fn_names_payload, total_named_funcs);

            for (uint32_t i = 0; i < imported_functions.size(); ++i) {
                encode_u32_leb(fn_names_payload, i);
                encode_string(fn_names_payload, imported_functions[i]);
            }
            for (uint32_t i = 0; i < defined_functions.size(); ++i) {
                encode_u32_leb(fn_names_payload, static_cast<uint32_t>(imported_functions.size() + i));
                encode_string(fn_names_payload, defined_functions[i]);
            }

            name_sec_payload.push_back(0x01); // Subsection ID 1: Function Names
            encode_u32_leb(name_sec_payload, static_cast<uint32_t>(fn_names_payload.size()));
            name_sec_payload.insert(name_sec_payload.end(), fn_names_payload.begin(), fn_names_payload.end());

            encode_section(out_bytes, kSectionCustom, name_sec_payload);
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
