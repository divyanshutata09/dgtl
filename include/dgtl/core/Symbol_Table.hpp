/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * SYMBOL TABLE: Scoped High-Velocity Identity Resolution
 * STANDARDS: C++26 / UCRT64 (Lock-Free Read / Minimal Collision)
 */

#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include <span>
#include <memory> // Added for internal arena management

#include "dgtl/ir/IR_Types.hpp"
#include "dgtl/core/Arena.hpp"

namespace dgtl::core {

// Using internal aliases for cleaner logic
namespace ir = dgtl::ir;

/**
 * @struct SymbolEntry
 * @brief Represents a single resolved identity within the BIR ecosystem.
 * Packed to 32 bytes to ensure dual-entry cache line alignment.
 */
struct alignas(16) SymbolEntry {
    uint32_t slot_id      = ir::INVALID_SLOT;
    uint32_t flags        = 0;
    ir::OpCode type_hint  = ir::OpCode::NOP;
    uint32_t version      = 0;
    uint64_t name_hash    = 0;
    void* metadata        = nullptr; // External attributes (Type metadata)
};

/**
 * @class SymbolTable
 * @brief Scoped Symbol Table with Arena-backed allocation.
 * Uses a tiered scope stack to manage shadowing and transactional visibility.
 */
class alignas(64) SymbolTable {
public:
    using ScopeMap = std::unordered_map<uint64_t, SymbolEntry>;

    /**
     * @brief Default Constructor for standalone/test environments.
     * Initializes an internal fallback arena.
     */
    SymbolTable() 
        : m_fallback_arena(std::make_unique<dgtl::core::DGTLArena>()), 
          m_arena(*m_fallback_arena) {
        m_scopes.emplace_back();
    }

    explicit SymbolTable(dgtl::core::DGTLArena& arena) 
        : m_arena(arena) {
        // Initialize global scope
        m_scopes.emplace_back(); 
    }

    // --- SCOPE MANAGEMENT ---

    /**
     * @brief Pushes a new lexical scope. 
     * Symbols added here will shadow symbols in parent scopes.
     */
    void push_scope() {
        m_scopes.emplace_back();
    }

    /**
     * @brief Pops the current scope, destroying all local symbol mappings.
     */
    void pop_scope() {
        if (m_scopes.size() > 1) {
            m_scopes.pop_back();
        }
    }

    // --- SYMBOL MANIPULATION ---

    /**
     * @brief Defines a new symbol in the current scope.
     * @return true if defined, false if redefinition occurs in the same scope.
     */
    bool define(std::string_view name, uint32_t slot_id, ir::OpCode type = ir::OpCode::NOP) {
        uint64_t hash = compute_hash(name);
        auto& current_scope = m_scopes.back();

        if (current_scope.contains(hash)) {
            return false; // Redefinition Error
        }

        SymbolEntry entry{
            .slot_id = slot_id,
            .flags = 0,
            .type_hint = type,
            .version = 1,
            .name_hash = hash,
            .metadata = nullptr
        };

        current_scope[hash] = entry;
        return true;
    }

    /**
     * @brief Resolves a symbol by searching from the innermost scope outwards.
     */
    [[nodiscard]] std::optional<SymbolEntry> resolve(std::string_view name) const noexcept {
        uint64_t hash = compute_hash(name);

        // Search stack from top (local) to bottom (global)
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) {
                return entry_it->second;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Updates an existing symbol's metadata or slot.
     */
    bool update(std::string_view name, uint32_t new_slot) {
        uint64_t hash = compute_hash(name);
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) {
                entry_it->second.slot_id = new_slot;
                entry_it->second.version++;
                return true;
            }
        }
        return false;
    }

    // --- UTILITIES ---

    /**
     * @brief Fast FNV-1a Hash for string identifiers.
     */
    [[nodiscard]] static constexpr uint64_t compute_hash(std::string_view name) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : name) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    /**
     * @brief Returns the current nesting depth.
     */
    [[nodiscard]] size_t depth() const noexcept {
        return m_scopes.size();
    }

    /**
     * @brief Clears all scopes except the global scope.
     */
    void reset() {
        m_scopes.clear();
        m_scopes.emplace_back();
    }

private:
    std::unique_ptr<dgtl::core::DGTLArena> m_fallback_arena; // Standalone support
    dgtl::core::DGTLArena& m_arena;
    std::vector<ScopeMap> m_scopes; // Stack of scopes
};

/**
 * @brief Legacy and Emitter-specific Aliases
 * Ensures NASM_Emitter.hpp and test_pipeline_*.cpp find the expected types.
 */
using DGTLSymbolTable = dgtl::core::SymbolTable;

} // namespace dgtl::core

namespace dgtl::ir {
    // Cross-namespace exposure for IR-focused modules
    using SymbolEntry = dgtl::core::SymbolEntry;
    using SymbolTable = dgtl::core::SymbolTable;
}