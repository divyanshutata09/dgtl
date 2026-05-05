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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRANSACTIONAL STREAM & SHADOW-AWARE STACK-MAPPING
 * STACK PILLER: Lexical Scope, Stack Frame Layout, & Variable Shadowing
 * STANDARDS: C++26 / UCRT64 (Zero-Latency Scope Resolution)
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

using dgtl::ir::OpCode;

namespace dgtl::isa {

/**
 * @struct StackSlot
 * @brief Represents a single variable's allocation within a stack frame.
 * Aligned to 16 bytes for SIMD-safe spills.
 */
struct alignas(16) StackSlot {
    int32_t offset;       // Offset relative to Frame Pointer (FP)
    uint32_t size;        // Size in bytes
    OpCode type_hint;     // Type for alignment validation
    uint32_t version;     // For shadowing detection
    bool is_spill;        // Flagged if this is a register-allocator spill
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
        if (m_scope_stack.size() > 1) {
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
        if (name.empty() || bytes == 0) {
            return 0;
        }

        // Maintain 16-byte alignment for hardware efficiency (AVX-512/NEON).
        const uint32_t aligned_frame = align_up(m_current_frame_size, STACK_ALIGNMENT);
        if (aligned_frame < m_current_frame_size) {
            return 0;
        }

        if (bytes > (std::numeric_limits<uint32_t>::max)() - aligned_frame) {
            return 0;
        }

        const uint32_t next_frame_size = aligned_frame + bytes;
        if (next_frame_size > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)())) {
            return 0;
        }

        int32_t offset = -static_cast<int32_t>(next_frame_size);
        m_current_frame_size = next_frame_size;

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
     * @brief Resolves a variable to its stack offset, respecting lexical shadowing.
     */
    [[nodiscard]] std::optional<StackSlot> resolve(std::string_view name) const noexcept {
        uint64_t name_hash = hash_name(name);

        // Search from innermost scope to global scope
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
        m_scope_stack.emplace_back();
        m_scope_frame_checkpoints.clear();
        m_scope_frame_checkpoints.push_back(0);
    }

private:
    dgtl::core::DGTL_Arena& m_arena;
    std::vector<ScopeLayer> m_scope_stack;
    std::vector<uint32_t> m_scope_frame_checkpoints;
    uint32_t m_current_frame_size;

    /**
     * @brief FNV-1a Hash for ultra-fast symbol lookup.
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
        if (alignment == 0) {
            return value;
        }
        const uint32_t mask = alignment - 1U;
        return (value + mask) & ~mask;
    }
};

using StackSpiller = StackPiller;

} // namespace dgtl::isa

/**
 * @brief Backward Compatibility Alias for DGTL-Zenith high-level logic.
 */



