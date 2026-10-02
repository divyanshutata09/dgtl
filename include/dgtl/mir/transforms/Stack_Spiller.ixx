/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Pure Machine IR Stack Spiller & Lexical Scope Mapper
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

export module dgtl.mir.stack_spiller;

import dgtl.mir.machine_ir;
import dgtl.arena;

export namespace dgtl::mir::regalloc {

using dgtl::machine::RegisterClassKind;
using dgtl::machine::StackSlotId;
using dgtl::machine::INVALID_STACK_SLOT;

/**
 * @struct StackSlot
 * @brief Represents a single variable's allocation within a stack frame.
 * Aligned to 16 bytes for SIMD-safe spills.
 */
struct alignas(16) StackSlot {
    int32_t offset = 0;
    uint32_t size = 0;
    RegisterClassKind reg_class = RegisterClassKind::General;
    uint32_t version = 0;
    bool is_spill = false;
};

/**
 * @struct SpillLifetime
 * @brief Live interval segment and requirements for a spilled virtual register.
 */
struct SpillLifetime {
    uint32_t virtual_id = 0;
    uint32_t start_point = 0;
    uint32_t end_point = 0;
    uint32_t size_bytes = 8;
    uint32_t alignment = 8;
    RegisterClassKind reg_class = RegisterClassKind::General;

    [[nodiscard]] constexpr bool overlaps(const SpillLifetime& other) const noexcept {
        return !(end_point <= other.start_point || other.end_point <= start_point);
    }
};

/**
 * @class StackPiller
 * @brief Manages the lexical lifecycle of symbols and their physical stack offsets.
 */
class alignas(64) StackPiller {
public:
    using ScopeLayer = std::unordered_map<uint64_t, StackSlot>;
    static constexpr uint32_t STACK_ALIGNMENT = 16;

    explicit StackPiller(dgtl::core::DGTL_Arena& arena) 
        : m_arena(arena), m_current_frame_size(0) {
        m_scope_stack.reserve(16);
        m_scope_frame_checkpoints.reserve(16);
        
        m_scope_stack.emplace_back();
        m_scope_frame_checkpoints.push_back(0);
    }

    void push_scope() {
        m_scope_stack.emplace_back();
        m_scope_frame_checkpoints.push_back(m_current_frame_size);
    }

    void pop_scope() {
        if (m_scope_stack.size() > 1) [[likely]] {
            m_scope_stack.pop_back();
            m_current_frame_size = m_scope_frame_checkpoints.back();
            m_scope_frame_checkpoints.pop_back();
        }
    }

    int32_t allocate(std::string_view name, uint32_t bytes, RegisterClassKind reg_class = RegisterClassKind::General) {
        if (bytes == 0) [[unlikely]] {
            return 0;
        }

        const uint32_t aligned_frame = align_up(m_current_frame_size, STACK_ALIGNMENT);
        if (aligned_frame < m_current_frame_size) [[unlikely]] {
            return 0;
        }

        if (bytes > (std::numeric_limits<uint32_t>::max)() - aligned_frame) [[unlikely]] {
            return 0;
        }

        const uint32_t next_frame_size = aligned_frame + bytes;
        if (next_frame_size > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)())) [[unlikely]] {
            return 0;
        }

        int32_t offset = -static_cast<int32_t>(next_frame_size);
        m_current_frame_size = next_frame_size;

        if (name.empty()) {
            return offset;
        }

        uint64_t name_hash = hash_name(name);
        uint32_t version = 1;
        for (auto it = m_scope_stack.rbegin(); it != m_scope_stack.rend(); ++it) {
            if (auto found = it->find(name_hash); found != it->end()) {
                version = found->second.version + 1;
                break;
            }
        }

        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .reg_class = reg_class,
            .version = version,
            .is_spill = false
        };

        m_scope_stack.back()[name_hash] = slot;
        return offset;
    }

    int32_t allocate_spill(uint32_t virtual_id, uint32_t bytes = 8, RegisterClassKind reg_class = RegisterClassKind::General) {
        const int32_t offset = allocate("", bytes, reg_class);
        const uint64_t spill_token = 0xFFFF000000000000ULL | static_cast<uint64_t>(virtual_id);
        
        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .reg_class = reg_class,
            .version = 1,
            .is_spill = true
        };
        m_scope_stack.back()[spill_token] = slot;
        return offset;
    }

    [[nodiscard]] std::unordered_map<uint32_t, int32_t> allocate_colored_spills(
        std::span<const SpillLifetime> lifetimes);

    void bind_explicit_slot(std::string_view name, int32_t offset, uint32_t bytes, RegisterClassKind reg_class = RegisterClassKind::General) {
        if (name.empty()) [[unlikely]] return;

        uint64_t name_hash = hash_name(name);
        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .reg_class = reg_class,
            .version = 1,
            .is_spill = false
        };
        m_scope_stack.back()[name_hash] = slot;
    }

    [[nodiscard]] std::optional<StackSlot> resolve(std::string_view name) const noexcept {
        uint64_t name_hash = hash_name(name);

        for (auto it = m_scope_stack.rbegin(); it != m_scope_stack.rend(); ++it) {
            if (auto found = it->find(name_hash); found != it->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] uint32_t current_frame_size() const noexcept {
        return m_current_frame_size;
    }

    [[nodiscard]] size_t nesting_depth() const noexcept {
        return m_scope_stack.size();
    }

    void reset_frame() {
        m_current_frame_size = 0;
        m_scope_stack.clear();
        m_scope_frame_checkpoints.clear();
        
        m_scope_stack.reserve(16);
        m_scope_frame_checkpoints.reserve(16);
        
        m_scope_stack.emplace_back();
        m_scope_frame_checkpoints.push_back(0);
    }

private:
    dgtl::core::DGTL_Arena& m_arena;
    std::vector<ScopeLayer> m_scope_stack;
    std::vector<uint32_t> m_scope_frame_checkpoints;
    uint32_t m_current_frame_size;

    [[nodiscard]] static constexpr uint64_t hash_name(std::string_view name) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : name) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    [[nodiscard]] static constexpr uint32_t align_up(uint32_t value, uint32_t alignment) noexcept {
        if (alignment == 0) [[unlikely]] {
            return value;
        }
        const uint32_t mask = alignment - 1U;
        return (value + mask) & ~mask;
    }
};

struct ColoredSlot {
    int32_t offset = 0;
    uint32_t size_bytes = 0;
    uint32_t alignment = 8;
    std::vector<SpillLifetime> occupants;
};

class StackSlotColorer {
public:
    [[nodiscard]] static std::unordered_map<uint32_t, int32_t> color_spill_slots(
        StackPiller& spiller,
        std::span<const SpillLifetime> lifetimes) {

        std::unordered_map<uint32_t, int32_t> assignments;
        if (lifetimes.empty()) return assignments;

        std::vector<SpillLifetime> sorted(lifetimes.begin(), lifetimes.end());
        std::sort(sorted.begin(), sorted.end(), [](const SpillLifetime& a, const SpillLifetime& b) {
            if (a.start_point != b.start_point) return a.start_point < b.start_point;
            return a.size_bytes > b.size_bytes;
        });

        std::vector<ColoredSlot> slots;

        for (const auto& item : sorted) {
            bool placed = false;

            for (auto& slot : slots) {
                if (slot.size_bytes < item.size_bytes || slot.alignment < item.alignment) {
                    continue;
                }

                bool conflict = false;
                for (const auto& occ : slot.occupants) {
                    if (occ.overlaps(item)) {
                        conflict = true;
                        break;
                    }
                }

                if (!conflict) {
                    slot.occupants.push_back(item);
                    assignments[item.virtual_id] = slot.offset;
                    placed = true;
                    break;
                }
            }

            if (!placed) {
                const int32_t new_offset = spiller.allocate_spill(item.virtual_id, item.size_bytes, item.reg_class);
                ColoredSlot new_slot{};
                new_slot.offset = new_offset;
                new_slot.size_bytes = item.size_bytes;
                new_slot.alignment = item.alignment;
                new_slot.occupants.push_back(item);
                slots.push_back(new_slot);
                assignments[item.virtual_id] = new_offset;
            }
        }

        return assignments;
    }
};

inline std::unordered_map<uint32_t, int32_t> StackPiller::allocate_colored_spills(
    std::span<const SpillLifetime> lifetimes) {
    return StackSlotColorer::color_spill_slots(*this, lifetimes);
}

using StackSpiller = StackPiller;

} // namespace dgtl::mir::regalloc

export namespace dgtl::isa {
    using dgtl::mir::regalloc::StackSpiller;
    using dgtl::mir::regalloc::StackPiller;
}

export namespace dgtl::mir {
    using regalloc::StackSpiller;
    using regalloc::StackPiller;
}