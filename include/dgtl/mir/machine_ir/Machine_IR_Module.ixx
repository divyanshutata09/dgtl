/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine IR Module (Container for Compiled Functions & Symbols)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.mir.module;

import dgtl.mir.machine_ir;

export namespace dgtl::machine {

struct MachineFunctionEntry {
    std::string name;
    std::uint32_t slot = 0xFFFFFFFFu;
    std::vector<MachineRegister> params;
    std::unique_ptr<MachineFunction> function;
};

class MachineModule {
public:
    explicit MachineModule(std::string_view name = "module") : name_(name) {}

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    MachineFunction& add_function(std::string_view fn_name, std::uint32_t slot = 0xFFFFFFFFu) {
        auto fn = std::make_unique<MachineFunction>(fn_name);
        MachineFunction* ptr = fn.get();
        functions_.push_back({std::string(fn_name), slot, {}, std::move(fn)});
        return *ptr;
    }

    [[nodiscard]] std::span<const MachineFunctionEntry> functions() const noexcept {
        return functions_;
    }

    [[nodiscard]] std::span<MachineFunctionEntry> functions() noexcept {
        return functions_;
    }

    [[nodiscard]] MachineFunction* find_function(std::string_view fn_name) noexcept {
        for (auto& f : functions_) {
            if (f.name == fn_name) return f.function.get();
        }
        return nullptr;
    }

    [[nodiscard]] MachineFunctionEntry* find_entry(std::string_view fn_name) noexcept {
        for (auto& f : functions_) {
            if (f.name == fn_name) return &f;
        }
        return nullptr;
    }

    void set_entry_point(std::string_view entry) {
        entry_point_ = entry;
    }

    [[nodiscard]] std::string_view entry_point() const noexcept {
        return entry_point_;
    }

    std::uint32_t get_or_create_symbol(std::string_view name) {
        auto it = name_to_id_.find(std::string(name));
        if (it != name_to_id_.end()) return it->second;
        std::uint32_t id = static_cast<std::uint32_t>(symbol_names_.size());
        symbol_names_.emplace_back(name);
        name_to_id_.emplace(std::string(name), id);
        return id;
    }

    [[nodiscard]] std::string_view symbol_name(std::uint32_t id) const noexcept {
        if (id < symbol_names_.size()) return symbol_names_[id];
        return {};
    }

private:
    std::string name_;
    std::string entry_point_;
    std::vector<MachineFunctionEntry> functions_;
    std::vector<std::string> symbol_names_;
    std::unordered_map<std::string, std::uint32_t> name_to_id_;
};

} // namespace dgtl::machine

export namespace dgtl::mir {
    using dgtl::machine::MachineModule;
    using dgtl::machine::MachineFunctionEntry;
}
