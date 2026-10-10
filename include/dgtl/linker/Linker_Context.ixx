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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.linker.context;

import dgtl.mir;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;

export namespace dgtl::linker {

struct LinkerContext {
    uint64_t image_base = 0;
    uint64_t entry_address = 0;
    std::string entry_symbol = "DGTL_kernel_entry";
    std::unordered_map<std::string, uint64_t> symbols;
    std::vector<std::string> diagnostics;

    void define_symbol(std::string name, uint64_t address) {
        symbols[std::move(name)] = address;
    }

    bool resolve_symbol(std::string_view name, uint64_t& out_address) const noexcept {
        auto it = symbols.find(std::string(name));
        if (it == symbols.end()) return false;
        out_address = it->second;
        return true;
    }

    bool has_symbol(std::string_view name) const noexcept {
        return symbols.find(std::string(name)) != symbols.end();
    }

    void import_object_symbols(const dgtl::object::ObjectFile& obj, uint64_t default_section_base = 0) {
        for (const auto& sym : obj.symbols()) {
            if (sym.is_defined() && !sym.name.empty()) {
                uint64_t base = default_section_base;
                if (sym.section_index < obj.sections().size()) {
                    const auto& sec = obj.sections()[sym.section_index];
                    if (sec.address != 0) {
                        base = sec.address;
                    }
                }
                symbols[sym.name] = base + sym.value;
            }
        }
    }

    void define_machine_function(const machine::MachineFunction& fn, uint64_t fn_address) {
        symbols[std::string(fn.name())] = fn_address;
    }

    void error(std::string message) {
        diagnostics.push_back(std::move(message));
    }

    [[nodiscard]] bool has_errors() const noexcept {
        return !diagnostics.empty();
    }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
}


