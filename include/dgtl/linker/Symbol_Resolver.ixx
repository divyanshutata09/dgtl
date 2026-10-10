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

export module dgtl.linker.symbols;

import dgtl.mir;
import dgtl.object.symbol;

export namespace dgtl::linker {

class SymbolResolver {
    std::unordered_map<std::string, uint64_t> m_;

public:
    bool define(std::string n, uint64_t v) {
        return m_.emplace(std::move(n), v).second;
    }

    void set(std::string n, uint64_t v) {
        m_[std::move(n)] = v;
    }

    bool resolve(std::string_view n, uint64_t& v) const noexcept {
        auto i = m_.find(std::string(n));
        if (i == m_.end()) return false;
        v = i->second;
        return true;
    }

    bool contains(std::string_view n) const {
        return m_.contains(std::string(n));
    }

    void import_machine_function(const machine::MachineFunction& fn, uint64_t address) {
        set(std::string(fn.name()), address);
    }

    void import_symbol(const dgtl::object::Symbol& sym, uint64_t section_base = 0) {
        if (!sym.name.empty() && sym.is_defined()) {
            set(sym.name, section_base + sym.value);
        }
    }

    [[nodiscard]] size_t size() const noexcept { return m_.size(); }
    [[nodiscard]] const std::unordered_map<std::string, uint64_t>& all() const noexcept { return m_; }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
}


