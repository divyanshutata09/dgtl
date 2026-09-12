/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : BACKEND LOWERING
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRANSACTIONAL STREAM & SHADOW-AWARE STACK-MAPPING
 * STACK PILLER: Lexical Scope, Stack Frame Layout, & Variable Shadowing
 * STANDARDS: C++26 / UCRT64 (Zer
o-Latency Scope Resolution)
 */

module;

#include <cstdlib>

#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

export module dgtl.ir_core.stack_spiller;

import dgtl.ir_core;
import dgtl.arena;
import dgtl.ir_core.abi_lowering;

using dgtl::ir::OpCode;

export namespace dgtl::backend::lowering {

/**
 * @struct StackSlot
 * @brief Represents a single variable's allocation within a stack frame.
 * Aligned to 16 bytes for SIMD-safe spills.
 */
struct alignas(16) StackSlot {
    int32_t offset = 0;       // Offset relative to Frame Pointer (FP)
    uint32_t size = 0;        // Size in bytes
    OpCode type_hint = OpCode::NOP;     // Type for alignment validation
    uint32_t version = 0;     // For shadowing detection
    bool is_spill = false;    // Flagged if this is a register-allocator spill
};

/**
 * @class StackPiller
 * @brief Manages the lexical lifecycle of symbols and their physical stack offsets.
 * Implements Pillar 2 "Shadowing" through nested hash-map scopes.
 */
class alignas(64) StackPiller {
public:
    using ScopeLayer = std::unordered_map<uint64_t, StackSlot>;
    static constexpr uint32_t STACK_ALIGNMENT = 16;

    explicit StackPiller(dgtl::core::DGTL_Arena& arena) 
        : m_arena(arena), m_current_frame_size(0) {
        m_scope_stack.reserve(16);
        m_scope_frame_checkpoints.reserve(16);
        
        // Initialize Global Scope
        m_scope_stack.emplace_back();
        m_scope_frame_checkpoints.push_back(0);
    }

    // --- SCOPE LIFECYCLE ---

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

    // --- ALLOCATION LOGIC ---

    /**
     * @brief Allocates a slot on the current virtual stack frame.
     * @return The stack-relative offset.
     */
    int32_t allocate(std::string_view name, uint32_t bytes, OpCode type = OpCode::NOP) {
        if (bytes == 0) [[unlikely]] {
            return 0;
        }

        // Maintain 16-byte alignment for hardware efficiency (AVX-512/NEON/Win64 ABI)
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
            .type_hint = type,
            .version = version,
            .is_spill = false
        };

        m_scope_stack.back()[name_hash] = slot;
        return offset;
    }

    /**
     * @brief Allocates a dedicated register-spill slot.
     */
    int32_t allocate_spill(uint32_t virtual_id, uint32_t bytes = 8) {
        const int32_t offset = allocate("", bytes, OpCode::NOP);
        const uint64_t spill_token = 0xFFFF000000000000ULL | static_cast<uint64_t>(virtual_id);
        
        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .type_hint = OpCode::NOP,
            .version = 1,
            .is_spill = true
        };
        m_scope_stack.back()[spill_token] = slot;
        return offset;
    }

    /**
     * @brief Manually binds a symbol to a specific offset. 
     */
    void bind_explicit_slot(std::string_view name, int32_t offset, uint32_t bytes, OpCode type = OpCode::NOP) {
        if (name.empty()) [[unlikely]] return;

        uint64_t name_hash = hash_name(name);
        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .type_hint = type,
            .version = 1,
            .is_spill = false
        };
        m_scope_stack.back()[name_hash] = slot;
    }

    /**
     * @brief Resolves a variable to its stack offset, respecting lexical shadowing.
     */
    [[nodiscard]] std::optional<StackSlot> resolve(std::string_view name) const noexcept {
        uint64_t name_hash = hash_name(name);

        for (auto it = m_scope_stack.rbegin(); it != m_scope_stack.rend(); ++it) {
            if (auto found = it->find(name_hash); found != it->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    // --- METRICS ---

    [[nodiscard]] uint32_t current_frame_size() const noexcept {
        return m_current_frame_size;
    }

    [[nodiscard]] size_t nesting_depth() const noexcept {
        return m_scope_stack.size();
    }

    /**
     * @brief Resets the frame size for a new function body emission.
     */
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

    /**
     * @brief FNV-1a Hash for symbol lookup.
     */
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

    [[nodiscard]] uint32_t compute_win64_aligned_frame(uint32_t non_volatile_spills_count, bool makes_external_calls) const noexcept {
        const uint32_t shadow_space = makes_external_calls ? 32u : 0u;
        const uint32_t raw_locals = m_current_frame_size;
        const uint32_t raw_body = raw_locals + shadow_space;
        const uint32_t callee_saved_bytes = non_volatile_spills_count * 8u;
        const uint32_t total_pushed_before_sub = 8u + 8u + callee_saved_bytes;
        const uint32_t remainder = (total_pushed_before_sub + raw_body) % 16u;
        const uint32_t pad = (remainder != 0u) ? (16u - remainder) : 0u;
        return raw_body + pad;
    }
};

using StackSpiller = StackPiller;

} // namespace dgtl::backend::lowering

export namespace dgtl::isa {
    using StackSlot = dgtl::backend::lowering::StackSlot;
    using StackPiller = dgtl::backend::lowering::StackPiller;
    using StackSpiller = dgtl::backend::lowering::StackSpiller;
}

/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * DGTL (Deep General-purpose Transformation Layer)
 *
 * COMPONENT:
 *   Stack Slot / Lexical Scope Allocation
 *
 * PURPOSE:
 *   Deterministic stack-frame slot allocation, lexical shadowing,
 *   register-spill management, explicit frame bindings, and ABI-aware
 *   frame-size calculation.
 *
 * DESIGN GOALS:
 *   - deterministic allocation
 *   - collision-safe symbol identity
 *   - checked arithmetic
 *   - strict alignment validation
 *   - lexical shadowing
 *   - stable spill identity
 *   - no silent integer overflow
 *   - no accidental frame corruption on failed allocation
 *   - ABI-independent core allocation
 *   - Win64 alignment calculation
 *
 * STANDARDS:
 *   C++26
 */

