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

export module dgtl.linker.entry_point;

import dgtl.mir;

export namespace dgtl::linker {

struct EntryPoint {
    std::string symbol = "DGTL_kernel_entry";
    uint64_t address = 0;
    bool resolved = false;

    void set_symbol(std::string_view s) {
        symbol = std::string(s);
        resolved = false;
    }

    void set_from_function(const machine::MachineFunction& fn) {
        symbol = fn.name();
        resolved = false;
    }

    void resolve(uint64_t a) noexcept {
        address = a;
        resolved = true;
    }

    [[nodiscard]] bool is_resolved() const noexcept {
        return resolved;
    }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
} // namespace dgtl::backend::linker


