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
#include <vector>

export module dgtl.linker.layout;

import dgtl.object.section;

export namespace dgtl::linker {

[[nodiscard]] inline constexpr uint64_t align_up(uint64_t v, uint64_t a) noexcept {
    if (a <= 1) return v;
    auto r = v % a;
    return r ? v + a - r : v;
}

struct SectionPlacement {
    std::string name;
    uint64_t offset = 0;
    uint64_t address = 0;
    uint64_t size = 0;
    uint64_t alignment = 1;
    uint32_t flags = 0;
};

class SectionLayout {
    uint64_t current_offset_ = 0;
    uint64_t current_address_ = 0;
    std::vector<SectionPlacement> placements_;

public:
    explicit SectionLayout(uint64_t base_address = 0, uint64_t start_offset = 0)
        : current_offset_(start_offset), current_address_(base_address) {}

    SectionPlacement place(std::string name, uint64_t size, uint64_t alignment = 1, uint32_t flags = 0) {
        current_offset_ = align_up(current_offset_, alignment);
        current_address_ = align_up(current_address_, alignment);
        SectionPlacement p{
            std::move(name),
            current_offset_,
            current_address_,
            size,
            alignment,
            flags
        };
        current_offset_ += size;
        current_address_ += size;
        placements_.push_back(p);
        return p;
    }

    SectionPlacement place_section(const dgtl::object::Section& sec) {
        return place(sec.name, sec.size(), sec.alignment, sec.flags);
    }

    [[nodiscard]] uint64_t total_size() const noexcept { return current_offset_; }
    [[nodiscard]] uint64_t end_address() const noexcept { return current_address_; }
    [[nodiscard]] const std::vector<SectionPlacement>& placements() const noexcept { return placements_; }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
} // namespace dgtl::backend::linker


