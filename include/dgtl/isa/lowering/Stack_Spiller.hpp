/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRANSACTIONAL STREAM & SHADOW-AWARE STACK-MAPPING
 * STACK PILLER: Lexical Scope, Stack Frame Layout, & Variable Shadowing
 * STANDARDS: C++26 / UCRT64 (Zero-Latency Scope Resolution)
 */

#pragma once
#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

#include "dgtl/ir/base/Global_IR.hpp"
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

    explicit StackPiller(dgtl::core::DGTL_Arena& arena) 
        : m_arena(arena), m_current_frame_size(0) {
        // Initialize Global Scope
        m_scope_stack.emplace_back();
    }

    // --- SCOPE LIFECYCLE ---

    void push_scope() {
        m_scope_stack.emplace_back();
    }

    void pop_scope() {
        if (m_scope_stack.size() > 1) {
            m_scope_stack.pop_back();
        }
    }

    // --- ALLOCATION LOGIC ---

    /**
     * @brief Allocates a slot on the current virtual stack frame.
     * @return The stack-relative offset.
     */
    int32_t allocate(std::string_view name, uint32_t bytes, OpCode type = OpCode::NOP) {
        // Maintain 16-byte alignment for hardware efficiency (AVX-512/NEON)
        m_current_frame_size = (m_current_frame_size + 15) & ~15;
        
        int32_t offset = -static_cast<int32_t>(m_current_frame_size + bytes);
        m_current_frame_size += bytes;

        uint64_t name_hash = hash_name(name);
        StackSlot slot{
            .offset = offset,
            .size = bytes,
            .type_hint = type,
            .version = 1,
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
    }

private:
    dgtl::core::DGTL_Arena& m_arena;
    std::vector<ScopeLayer> m_scope_stack;
    uint32_t m_current_frame_size;

    /**
     * @brief FNV-1a Hash for ultra-fast symbol lookup.
     */
    [[nodiscard]] static constexpr uint64_t hash_name(std::string_view name) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : name) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }
};

} // namespace dgtl::isa

/**
 * @brief Backward Compatibility Alias for DGTL-Zenith high-level logic.
 */


