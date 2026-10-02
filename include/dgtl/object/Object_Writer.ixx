/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Polymorphic Object Writer Interface
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <string_view>
#include <vector>

export module dgtl.object.writer;

import dgtl.object.file;

export namespace dgtl::object {

class ObjectWriter {
public:
    virtual ~ObjectWriter() = default;

    [[nodiscard]] virtual bool write(
        const ObjectFile& obj,
        std::vector<std::uint8_t>& out) const = 0;

    [[nodiscard]] virtual std::string_view format_name() const noexcept = 0;
};

} // namespace dgtl::object


