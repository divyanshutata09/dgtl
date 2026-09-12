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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * SYMBOL TABLE: Scoped High-Velocity Identity Resolution
 * STANDARDS: C++26 / UCRT64 (Lock-Free Read / Minimal Collision)
 */

module;

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <unordered_map>
#include <span>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <concepts>
#include <bit>

export module dgtl.ir_core.semantic;

// FIXED CYCLIC LOOP: Import local core tier siblings via short-form

// Import standalone external primitives safely
import dgtl.arena;
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.hashing;

export namespace dgtl::semantic {

/**
 * @struct SymbolEntry
 * @brief Represents a single resolved identity within the BIR ecosystem.
 * Packed to 32 bytes to ensure dual-entry cache line alignment.
 */
struct alignas(16) SymbolEntry {
    uint32_t slot_id          = dgtl::ir::INVALID_SLOT;
    uint32_t flags            = 0;
    dgtl::ir::OpCode type_hint  = dgtl::ir::OpCode::NOP;
    uint32_t version          = 0;
    uint64_t name_hash        = 0;
    void* metadata            = nullptr; // External attributes (Type metadata)

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return slot_id != dgtl::ir::INVALID_SLOT;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] constexpr uint32_t value() const noexcept {
        return slot_id;
    }

    [[nodiscard]] constexpr uint32_t operator*() const noexcept {
        return slot_id;
    }
};

/**
 * @enum SymbolKind
 * @brief Semantic category of the entity.
 */
enum class SymbolKind : uint8_t {
    Unknown = 0,
    Variable,
    Parameter,
    Function,
    Label,
    Constant,
    Type,
    Global,
    MemoryRegion
};

/**
 * @enum SymbolLinkage
 * @brief Visibility and linkage contract across object boundaries.
 */
enum class SymbolLinkage : uint8_t {
    Private = 0,
    Internal,
    External,
    Exported,
    Weak
};

/**
 * @struct SymbolFlags
 * @brief Bitfield attributes reflecting semantic modifiers and mutability.
 */
struct SymbolFlags {
    static constexpr uint32_t None        = 0u;
    static constexpr uint32_t IsConst     = 1u << 0;
    static constexpr uint32_t IsVolatile  = 1u << 1;
    static constexpr uint32_t IsMutable   = 1u << 2;
    static constexpr uint32_t IsAllocated = 1u << 3;
    static constexpr uint32_t IsSpillSlot = 1u << 4;
    static constexpr uint32_t IsShadowed  = 1u << 5;
    static constexpr uint32_t IsHotPath   = 1u << 6;
    static constexpr uint32_t IsUsed      = 1u << 7;
};

/**
 * @struct alignas(64) Symbol
 * @brief Intrusive, cache-line-aligned symbol node with hardware layout awareness.
 */
struct alignas(64) Symbol {
    std::string_view name;
    uint64_t name_hash = 0;
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
    SymbolKind kind = SymbolKind::Unknown;
    SymbolLinkage linkage = SymbolLinkage::Internal;
    dgtl::ir::TypeCode type_code = dgtl::ir::TypeCode::VOID_T;
    uint32_t type_hint = 0;

    uint32_t flags = SymbolFlags::None;
    uint32_t scope_depth = 0;
    uint32_t version = 1;

    int32_t stack_offset = 0;
    uint32_t size_bytes = 8;
    uint32_t alignment = 8;

    uint64_t constant_value = 0;
    uintptr_t runtime_address = 0;
    void* metadata = nullptr;

    // Intrusive shadowing and scope rollback links
    Symbol* shadow_prev = nullptr;
    Symbol* scope_next = nullptr;

    [[nodiscard]] constexpr bool has_flag(uint32_t flag) const noexcept {
        return (flags & flag) != 0;
    }

    constexpr void set_flag(uint32_t flag) noexcept {
        flags |= flag;
    }

    constexpr void clear_flag(uint32_t flag) noexcept {
        flags &= ~flag;
    }
};

/**
 * @class alignas(64) SymbolTable
 * @brief Scoped Symbol Table with Arena-backed allocation.
 * Uses a tiered scope stack to manage shadowing and transactional visibility.
 */
class alignas(64) SymbolTable {
public:
    using ScopeMap = std::unordered_map<uint64_t, SymbolEntry>;
    using ExtendedScopeMap = std::unordered_map<uint64_t, Symbol>;

    static constexpr size_t kInitialCapacity = 1024;
    static constexpr uint64_t kEmptyHash = 0ULL;
    static constexpr uint64_t kTombstoneHash = 1ULL;

    struct TableEntry {
        uint64_t hash = kEmptyHash;
        Symbol* symbol = nullptr;
    };

    struct ScopeCheckpoint {
        Symbol* head = nullptr;
        uint32_t depth = 0;
    };

    /**
     * @brief Default Constructor for standalone/test environments.
     * Initializes an internal fallback arena.
     */
    SymbolTable()
        : m_fallback_arena(std::make_unique<dgtl::core::DGTL_Arena>(1, 16)),
          m_arena(*m_fallback_arena),
          arena_(*m_fallback_arena),
          capacity_(std::bit_ceil(kInitialCapacity)),
          mask_(capacity_ - 1),
          slot_capacity_(kInitialCapacity),
          slot_mask_(slot_capacity_ - 1) {
        m_scopes.reserve(8);
        m_scopes.emplace_back();
        m_extended_scopes.reserve(8);
        m_extended_scopes.emplace_back();
        scope_stack_.reserve(16);
        allocate_table(capacity_);
        allocate_slot_table(slot_capacity_);
        push_scope(); // Root/Global scope
    }

    explicit SymbolTable(dgtl::core::DGTL_Arena& arena)
        : m_arena(arena),
          arena_(arena),
          capacity_(std::bit_ceil(kInitialCapacity)),
          mask_(capacity_ - 1),
          slot_capacity_(kInitialCapacity),
          slot_mask_(slot_capacity_ - 1) {
        m_scopes.reserve(8);
        // Initialize global scope
        m_scopes.emplace_back();
        m_extended_scopes.reserve(8);
        m_extended_scopes.emplace_back();
        scope_stack_.reserve(16);
        allocate_table(capacity_);
        allocate_slot_table(slot_capacity_);
        push_scope(); // Root/Global scope
    }

    explicit SymbolTable(dgtl::core::DGTL_Arena& arena, size_t initial_cap) noexcept
        : m_arena(arena),
          arena_(arena),
          capacity_(std::bit_ceil(initial_cap)),
          mask_(capacity_ - 1),
          slot_capacity_(std::bit_ceil(initial_cap)),
          slot_mask_(slot_capacity_ - 1) {
        m_scopes.reserve(8);
        m_scopes.emplace_back();
        m_extended_scopes.reserve(8);
        m_extended_scopes.emplace_back();
        scope_stack_.reserve(16);
        allocate_table(capacity_);
        allocate_slot_table(slot_capacity_);
        push_scope(); // Root/Global scope
    }

    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;
    SymbolTable(SymbolTable&&) = delete;
    SymbolTable& operator=(SymbolTable&&) = delete;

    ~SymbolTable() = default;

    // --- SCOPE MANAGEMENT ---

    /**
     * @brief Pushes a new lexical scope.
     * Symbols added here will shadow symbols in parent scopes.
     */
    void push_scope() {
        m_scopes.emplace_back();
        m_extended_scopes.emplace_back();
        const uint32_t next_depth = scope_depth_counter_++;
        scope_stack_.push_back(ScopeCheckpoint{
            .head = nullptr,
            .depth = next_depth
        });
    }

    /**
     * @brief Pops the current scope, destroying all local symbol mappings.
     */
    void pop_scope() {
        if (m_scopes.size() > 1) [[likely]] {
            for (const auto& [hash, entry] : m_scopes.back()) {
                if (entry.slot_id != dgtl::ir::INVALID_SLOT) {
                    m_slot_to_entry.erase(entry.slot_id);
                }
            }
            for (const auto& [hash, sym] : m_extended_scopes.back()) {
                if (sym.slot_id != dgtl::ir::INVALID_SLOT) {
                    unindex_slot(sym.slot_id);
                }
            }
            m_scopes.pop_back();
            m_extended_scopes.pop_back();
        }

        if (scope_stack_.size() > 1) [[likely]] {
            ScopeCheckpoint active = scope_stack_.back();
            scope_stack_.pop_back();
            scope_depth_counter_--;

            // O(1) Intrusive Unwind: Walk scope list and restore shadowed symbols
            Symbol* curr = active.head;
            while (curr != nullptr) {
                Symbol* next = curr->scope_next;

                // 1. Unwind Primary Name Table
                size_t idx = curr->name_hash & mask_;
                while (table_[idx].hash != kEmptyHash) {
                    if (table_[idx].symbol == curr) {
                        if (curr->shadow_prev) {
                            // Restore shadowed outer definition in-place
                            table_[idx].symbol = curr->shadow_prev;
                            table_[idx].symbol->clear_flag(SymbolFlags::IsShadowed);
                        } else {
                            table_[idx].hash = kTombstoneHash;
                            table_[idx].symbol = nullptr;
                            occupied_count_--;
                        }
                        break;
                    }
                    idx = (idx + 1) & mask_;
                }

                // 2. Unwind SSA Slot Index
                if (curr->slot_id != dgtl::ir::INVALID_SLOT) {
                    size_t s_idx = hash_slot(curr->slot_id) & slot_mask_;
                    while (slot_table_[s_idx].slot_id != dgtl::ir::INVALID_SLOT) {
                        if (slot_table_[s_idx].symbol == curr) {
                            slot_table_[s_idx].slot_id = dgtl::ir::INVALID_SLOT;
                            slot_table_[s_idx].symbol = nullptr;
                            slot_count_--;
                            break;
                        }
                        s_idx = (s_idx + 1) & slot_mask_;
                    }
                }

                curr = next;
            }
        }
    }

    // --- SYMBOL MANIPULATION ---

    /**
     * @brief Defines a new symbol in the current scope.
     * @return true if defined, false if redefinition occurs in the same scope.
     */
    bool define(std::string_view name, uint32_t slot_id, dgtl::ir::OpCode type = dgtl::ir::OpCode::NOP) {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return false;
        }

        uint64_t hash = compute_hash(name);
        auto& current_scope = m_scopes.back();

        if (current_scope.contains(hash)) [[unlikely]] {
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

        auto res = current_scope.emplace(hash, entry);
        if (res.second) {
            m_slot_to_entry.insert_or_assign(slot_id, &res.first->second);

            // Register in extended symbol map
            Symbol sym{};
            sym.name = intern_string(name);
            sym.name_hash = hash;
            sym.slot_id = slot_id;
            sym.kind = SymbolKind::Variable;
            sym.type_hint = static_cast<uint32_t>(type);
            sym.scope_depth = static_cast<uint32_t>(depth() - 1);
            
            auto& ext_scope = m_extended_scopes.back();
            auto ext_res = ext_scope.insert_or_assign(hash, sym);
            index_slot(&ext_res.first->second);
            return true;
        }
        return false;
    }

    /**
     * @brief Defines a new symbol with generic/frontend type hint handles (used by Function.ixx).
     */
    template <typename TypeHintT>
    requires (!std::is_same_v<std::decay_t<TypeHintT>, dgtl::ir::OpCode>)
    bool define(std::string_view name, uint32_t slot_id, TypeHintT type_hint, dgtl::ir::OpCode op = dgtl::ir::OpCode::NOP, bool is_param = false) {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return false;
        }

        uint64_t hash = compute_hash(name);
        auto& current_scope = m_scopes.back();

        if (current_scope.contains(hash)) [[unlikely]] {
            return false;
        }

        uint32_t numeric_type_hint = 0;
        if constexpr (std::is_integral_v<TypeHintT>) {
            numeric_type_hint = static_cast<uint32_t>(type_hint);
        } else if constexpr (requires { { type_hint.value } -> std::convertible_to<uint32_t>; }) {
            numeric_type_hint = static_cast<uint32_t>(type_hint.value);
        } else if constexpr (requires { { type_hint.handle } -> std::convertible_to<uint32_t>; }) {
            numeric_type_hint = static_cast<uint32_t>(type_hint.handle);
        }

        SymbolEntry entry{
            .slot_id = slot_id,
            .flags = is_param ? 1u : 0u,
            .type_hint = op,
            .version = 1,
            .name_hash = hash,
            .metadata = nullptr
        };

        auto res = current_scope.emplace(hash, entry);
        if (res.second) {
            m_slot_to_entry.insert_or_assign(slot_id, &res.first->second);

            Symbol sym{};
            sym.name = intern_string(name);
            sym.name_hash = hash;
            sym.slot_id = slot_id;
            sym.kind = is_param ? SymbolKind::Parameter : SymbolKind::Variable;
            sym.type_hint = numeric_type_hint;
            sym.scope_depth = static_cast<uint32_t>(depth() - 1);

            auto& ext_scope = m_extended_scopes.back();
            auto ext_res = ext_scope.insert_or_assign(hash, sym);
            index_slot(&ext_res.first->second);
            return true;
        }
        return false;
    }

    /**
     * @brief Resolves a symbol by searching from the innermost scope outwards.
     */
    [[nodiscard]] std::optional<SymbolEntry> resolve(std::string_view name) const noexcept {
        uint64_t hash = compute_hash(name);

        // Search stack from top (local) to bottom (global)
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) [[likely]] {
                return entry_it->second;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Direct pointer access for the Emitter via Name.
     */
    [[nodiscard]] SymbolEntry* get(std::string_view name) noexcept {
        uint64_t hash = compute_hash(name);
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) [[likely]] {
                return &(entry_it->second);
            }
        }
        return nullptr;
    }

    /**
     * @brief Direct access via slot_id to prevent uint32_t to string_view conversion errors.
     */
    [[nodiscard]] SymbolEntry* get(uint32_t slot_id) noexcept {
        auto it = m_slot_to_entry.find(slot_id);
        if (it != m_slot_to_entry.end()) return it->second;

        for (auto s_it = m_scopes.rbegin(); s_it != m_scopes.rend(); ++s_it) {
            for (auto& [hash, entry] : *s_it) {
                if (entry.slot_id == slot_id) [[unlikely]] {
                    m_slot_to_entry[slot_id] = &entry;
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Specialized getter for IR-linked function nodes.
     * Fixed: Added explicit pointer return. The caller must cast to ir::IRStream&.
     */
    [[nodiscard]] void* get_function_ir(std::string_view name) const noexcept {
        uint64_t hash = compute_hash(name);
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) [[likely]] {
                return entry_it->second.metadata;
            }
        }
        return nullptr;
    }

    /**
     * @brief Updates an existing symbol's metadata or slot.
     */
    bool update(std::string_view name, uint32_t new_slot) {
        if (new_slot == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return false;
        }

        uint64_t hash = compute_hash(name);
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto entry_it = it->find(hash); entry_it != it->end()) [[likely]] {
                const uint32_t old_slot = entry_it->second.slot_id;
                m_slot_to_entry.erase(old_slot);
                entry_it->second.slot_id = new_slot;
                entry_it->second.version++;
                m_slot_to_entry[new_slot] = &(entry_it->second);

                if (auto* sym = lookup(name)) {
                    unindex_slot(old_slot);
                    sym->slot_id = new_slot;
                    sym->version = entry_it->second.version;
                    index_slot(sym);
                }
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // SYMBOL INSERTION & MUTATION (ZERO DYNAMIC HEAP ALLOCATIONS)
    // =========================================================================

    Symbol* insert(Symbol sym) noexcept {
        if (scope_stack_.empty()) [[unlikely]] {
            push_scope();
        }

        if (sym.name_hash <= kTombstoneHash && !sym.name.empty()) {
            sym.name_hash = sanitize_hash(hash_name(sym.name));
        }

        sym.name = intern_string(sym.name);
        sym.scope_depth = static_cast<uint32_t>(current_scope_depth());

        // Check load factor (75%) and grow flat table if necessary
        if ((occupied_count_ + 1) * 4 >= capacity_ * 3) {
            grow_table();
        }

        // Allocate Symbol record directly out of contiguous Arena
        Symbol* mem = arena_.allocate<Symbol>(1);
        if (!mem) [[unlikely]] return nullptr;
        std::memcpy(mem, &sym, sizeof(Symbol));

        // Find insertion location and check for lexical shadowing
        size_t idx = mem->name_hash & mask_;
        size_t first_tombstone = capacity_;

        while (table_[idx].hash != kEmptyHash) {
            if (table_[idx].hash == kTombstoneHash) {
                if (first_tombstone == capacity_) first_tombstone = idx;
            } else if (table_[idx].hash == mem->name_hash &&
                       table_[idx].symbol->name == mem->name) {
                // Collision with matching name: Setup Intrusive Shadow Chain
                Symbol* existing = table_[idx].symbol;
                mem->version = existing->version + 1;
                mem->shadow_prev = existing;
                existing->set_flag(SymbolFlags::IsShadowed);

                table_[idx].symbol = mem; // Overwrite top pointer with new shadow layer
                track_in_current_scope(mem);
                index_slot(mem);

                // Keep legacy maps synchronized
                SymbolEntry legacy_entry{
                    .slot_id = mem->slot_id,
                    .flags = mem->flags,
                    .type_hint = static_cast<dgtl::ir::OpCode>(mem->type_hint),
                    .version = mem->version,
                    .name_hash = mem->name_hash,
                    .metadata = mem->metadata
                };
                m_scopes.back().insert_or_assign(mem->name_hash, legacy_entry);
                m_slot_to_entry.insert_or_assign(mem->slot_id, &m_scopes.back().find(mem->name_hash)->second);
                m_extended_scopes.back().insert_or_assign(mem->name_hash, *mem);
                return mem;
            }
            idx = (idx + 1) & mask_;
        }

        // Fresh symbol insertion
        const size_t insert_idx = (first_tombstone != capacity_) ? first_tombstone : idx;
        table_[insert_idx].hash = mem->name_hash;
        table_[insert_idx].symbol = mem;
        occupied_count_++;

        track_in_current_scope(mem);
        index_slot(mem);

        // Keep legacy maps synchronized
        SymbolEntry legacy_entry{
            .slot_id = mem->slot_id,
            .flags = mem->flags,
            .type_hint = static_cast<dgtl::ir::OpCode>(mem->type_hint),
            .version = mem->version,
            .name_hash = mem->name_hash,
            .metadata = mem->metadata
        };
        m_scopes.back().insert_or_assign(mem->name_hash, legacy_entry);
        m_slot_to_entry.insert_or_assign(mem->slot_id, &m_scopes.back().find(mem->name_hash)->second);
        m_extended_scopes.back().insert_or_assign(mem->name_hash, *mem);

        return mem;
    }

    Symbol* insert_variable(
        std::string_view name,
        uint32_t slot_id,
        dgtl::ir::TypeCode type = dgtl::ir::TypeCode::I64,
        uint32_t size_bytes = 8,
        uint32_t alignment = 8
    ) noexcept {
        Symbol sym{};
        sym.name = name;
        sym.slot_id = slot_id;
        sym.kind = SymbolKind::Variable;
        sym.type_code = type;
        sym.size_bytes = size_bytes;
        sym.alignment = alignment;
        return insert(sym);
    }

    Symbol* insert_function(
        std::string_view name,
        uint32_t entry_slot,
        dgtl::ir::TypeCode return_type = dgtl::ir::TypeCode::VOID_T,
        SymbolLinkage linkage = SymbolLinkage::External
    ) noexcept {
        Symbol sym{};
        sym.name = name;
        sym.slot_id = entry_slot;
        sym.kind = SymbolKind::Function;
        sym.linkage = linkage;
        sym.type_code = return_type;
        return insert(sym);
    }

    Symbol* insert_label(std::string_view name, uint32_t label_id) noexcept {
        Symbol sym{};
        sym.name = name;
        sym.slot_id = label_id;
        sym.kind = SymbolKind::Label;
        sym.type_code = dgtl::ir::TypeCode::VOID_T;
        return insert(sym);
    }

    Symbol* insert_constant_i64(std::string_view name, uint32_t slot_id, uint64_t val) noexcept {
        Symbol sym{};
        sym.name = name;
        sym.slot_id = slot_id;
        sym.kind = SymbolKind::Constant;
        sym.type_code = dgtl::ir::TypeCode::I64;
        sym.constant_value = val;
        sym.set_flag(SymbolFlags::IsConst);
        return insert(sym);
    }

    // =========================================================================
    // O(1) CACHE-LOCAL RESOLUTION
    // =========================================================================

    [[nodiscard]] Symbol* lookup(std::string_view name) const noexcept {
        if (name.empty() || occupied_count_ == 0) return nullptr;
        const uint64_t h = sanitize_hash(hash_name(name));

        size_t idx = h & mask_;
        while (table_[idx].hash != kEmptyHash) {
            if (table_[idx].hash == h && table_[idx].symbol->name == name) {
                return table_[idx].symbol;
            }
            idx = (idx + 1) & mask_;
        }
        return nullptr;
    }

    [[nodiscard]] Symbol* lookup_current_scope(std::string_view name) const noexcept {
        Symbol* sym = lookup(name);
        if (sym && sym->scope_depth == current_scope_depth()) {
            return sym;
        }
        return nullptr;
    }

    [[nodiscard]] Symbol* lookup_by_slot(uint32_t slot_id) const noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT || slot_count_ == 0) return nullptr;
        const uint64_t h = hash_slot(slot_id);

        size_t idx = h & slot_mask_;
        while (slot_table_[idx].slot_id != dgtl::ir::INVALID_SLOT) {
            if (slot_table_[idx].slot_id == slot_id) {
                return slot_table_[idx].symbol;
            }
            idx = (idx + 1) & slot_mask_;
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
        return lookup(name) != nullptr;
    }

    [[nodiscard]] bool contains_slot(uint32_t slot_id) const noexcept {
        return lookup_by_slot(slot_id) != nullptr;
    }

    // =========================================================================
    // EMITTER & BACKEND CONTRACTS
    // =========================================================================

    void bind_physical_stack_offset(uint32_t slot_id, int32_t offset) noexcept {
        if (auto* sym = lookup_by_slot(slot_id)) {
            sym->stack_offset = offset;
            sym->set_flag(SymbolFlags::IsAllocated);
        }
    }

    void bind_runtime_address(std::string_view name, uintptr_t addr) noexcept {
        if (auto* sym = lookup(name)) {
            sym->runtime_address = addr;
        }
    }

    [[nodiscard]] std::string_view resolve_name_or_default(
        uint32_t slot_id,
        std::string_view fallback = ""
    ) const noexcept {
        const auto* sym = lookup_by_slot(slot_id);
        if (sym && !sym->name.empty()) {
            return sym->name;
        }
        return fallback;
    }

    [[nodiscard]] dgtl::core::DGTL_Arena& arena() noexcept {
        return arena_;
    }

    // --- UTILITIES ---

    /**
     * @brief Fast FNV-1a Hash for string identifiers.
     */
    [[nodiscard]] static constexpr uint64_t compute_hash(std::string_view name) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : name) {
            hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
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

    [[nodiscard]] size_t current_scope_depth() const noexcept {
        return scope_stack_.empty() ? 0 : (scope_stack_.size() - 1);
    }

    /**
     * @brief Clears all scopes except the global scope.
     */
    void reset() {
        m_scopes.clear();
        m_scopes.emplace_back();
        m_extended_scopes.clear();
        m_extended_scopes.emplace_back();
        m_slot_to_entry.clear();
        m_slot_to_symbol.clear();
        scope_stack_.clear();
        scope_depth_counter_ = 0;
        occupied_count_ = 0;
        slot_count_ = 0;
        std::memset(table_, 0, sizeof(TableEntry) * capacity_);
        for (size_t i = 0; i < slot_capacity_; ++i) {
            slot_table_[i].slot_id = dgtl::ir::INVALID_SLOT;
            slot_table_[i].symbol = nullptr;
        }
        push_scope();
    }

    void clear() noexcept {
        reset();
    }

    /**
     * @brief Pre-populates the symbol table with core runtime and GPU symbols.
     */
    void register_builtin_runtime_symbols() {
        static constexpr std::string_view kBuiltinSymbols[] = {
            "dgtl_gpu_init_pipeline",
            "dgtl_gpu_resize",
            "dgtl_gpu_destroy_pipeline",
            "dgtl_gpu_begin_frame",
            "dgtl_gpu_end_frame",
            "dgtl_gpu_push_clip_rect",
            "dgtl_gpu_pop_clip_rect",
            "dgtl_gpu_render_quad_instances",
            "dgtl_gpu_render_glyph_instances",
            "dgtl_gpu_render_vector_paths",
            "dgtl_gpu_upload_texture",
            "dgtl_gpu_destroy_texture",
            "dgtl_platform_poll_events",
            "dgtl_platform_get_time_us",
            "dgtl_platform_set_cursor",
            "__dgtl_rt_string_length",
            "__dgtl_rt_string_compare",
            "__dgtl_rt_string_concat",
            "__dgtl_rt_string_substring",
            "__dgtl_rt_async_await",
            "__dgtl_rt_async_launch",
            "__dgtl_rt_async_spawn",
            "__dgtl_rt_chan_recv",
            "__dgtl_rt_chan_send",
            "__dgtl_rt_fiber_yield",
            "__dgtl_rt_dsa_allocate",
            "__dgtl_rt_dsa_deallocate"
        };

        for (uint32_t i = 0; i < sizeof(kBuiltinSymbols) / sizeof(kBuiltinSymbols[0]); ++i) {
            define(kBuiltinSymbols[i], 0x80000000u | i, dgtl::ir::OpCode::CALL);
        }
    }

    static void populate_default_symbols(SymbolTable& symtab) {
        symtab.register_builtin_runtime_symbols();
    }

private:
    struct SlotEntry {
        uint32_t slot_id = dgtl::ir::INVALID_SLOT;
        Symbol* symbol = nullptr;
    };

    std::unique_ptr<dgtl::core::DGTL_Arena> m_fallback_arena; // Standalone support
    dgtl::core::DGTL_Arena& m_arena;
    dgtl::core::DGTL_Arena& arena_;

    // Tier-1: Legacy lightweight SymbolEntry scopes
    std::vector<ScopeMap> m_scopes;
    std::unordered_map<uint32_t, SymbolEntry*> m_slot_to_entry;

    // Tier-2: Full semantic Symbol scopes with hardware layout & types
    std::vector<ExtendedScopeMap> m_extended_scopes;
    std::unordered_map<uint32_t, Symbol*> m_slot_to_symbol;

    // Tier-3: Cache-local Open-Addressing Slot Table
    TableEntry* table_ = nullptr;
    size_t capacity_ = 0;
    size_t mask_ = 0;
    size_t occupied_count_ = 0;

    SlotEntry* slot_table_ = nullptr;
    size_t slot_capacity_ = 0;
    size_t slot_mask_ = 0;
    size_t slot_count_ = 0;

    std::vector<ScopeCheckpoint> scope_stack_;
    uint32_t scope_depth_counter_ = 0;

    void allocate_table(size_t cap) noexcept {
        capacity_ = cap;
        mask_ = capacity_ - 1;
        table_ = arena_.allocate<TableEntry>(capacity_);
        std::memset(table_, 0, sizeof(TableEntry) * capacity_);
    }

    void allocate_slot_table(size_t cap) noexcept {
        slot_capacity_ = cap;
        slot_mask_ = slot_capacity_ - 1;
        slot_table_ = arena_.allocate<SlotEntry>(slot_capacity_);
        for (size_t i = 0; i < slot_capacity_; ++i) {
            slot_table_[i].slot_id = dgtl::ir::INVALID_SLOT;
            slot_table_[i].symbol = nullptr;
        }
    }

    void track_in_current_scope(Symbol* sym) noexcept {
        if (!scope_stack_.empty()) {
            sym->scope_next = scope_stack_.back().head;
            scope_stack_.back().head = sym;
        }
    }

    void index_slot(Symbol* sym) noexcept {
        if (!sym || sym->slot_id == dgtl::ir::INVALID_SLOT) return;
        if ((slot_count_ + 1) * 4 >= slot_capacity_ * 3) {
            grow_slot_table();
        }

        size_t idx = hash_slot(sym->slot_id) & slot_mask_;
        while (slot_table_[idx].slot_id != dgtl::ir::INVALID_SLOT) {
            if (slot_table_[idx].slot_id == sym->slot_id) {
                slot_table_[idx].symbol = sym;
                return;
            }
            idx = (idx + 1) & slot_mask_;
        }
        slot_table_[idx].slot_id = sym->slot_id;
        slot_table_[idx].symbol = sym;
        slot_count_++;
    }

    void unindex_slot(uint32_t slot_id) noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT || slot_count_ == 0) return;
        size_t idx = hash_slot(slot_id) & slot_mask_;
        while (slot_table_[idx].slot_id != dgtl::ir::INVALID_SLOT) {
            if (slot_table_[idx].slot_id == slot_id) {
                slot_table_[idx].slot_id = dgtl::ir::INVALID_SLOT;
                slot_table_[idx].symbol = nullptr;
                slot_count_--;
                break;
            }
            idx = (idx + 1) & slot_mask_;
        }
    }

    void grow_table() noexcept {
        const size_t old_cap = capacity_;
        TableEntry* old_table = table_;

        allocate_table(old_cap * 2);
        occupied_count_ = 0;

        for (size_t i = 0; i < old_cap; ++i) {
            if (old_table[i].hash > kTombstoneHash) {
                Symbol* sym = old_table[i].symbol;
                size_t idx = sym->name_hash & mask_;
                while (table_[idx].hash != kEmptyHash) {
                    idx = (idx + 1) & mask_;
                }
                table_[idx].hash = sym->name_hash;
                table_[idx].symbol = sym;
                occupied_count_++;
            }
        }
    }

    void grow_slot_table() noexcept {
        const size_t old_cap = slot_capacity_;
        SlotEntry* old_table = slot_table_;

        allocate_slot_table(old_cap * 2);
        slot_count_ = 0;

        for (size_t i = 0; i < old_cap; ++i) {
            if (old_table[i].slot_id != dgtl::ir::INVALID_SLOT) {
                Symbol* sym = old_table[i].symbol;
                size_t idx = hash_slot(sym->slot_id) & slot_mask_;
                while (slot_table_[idx].slot_id != dgtl::ir::INVALID_SLOT) {
                    idx = (idx + 1) & slot_mask_;
                }
                slot_table_[idx].slot_id = sym->slot_id;
                slot_table_[idx].symbol = sym;
                slot_count_++;
            }
        }
    }

    [[nodiscard]] static constexpr uint64_t hash_name(std::string_view str) noexcept {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (char c : str) {
            h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    [[nodiscard]] static constexpr uint64_t hash_slot(uint32_t slot) noexcept {
        return static_cast<uint64_t>(slot) * 0x9E3779B97F4A7C15ULL;
    }

    [[nodiscard]] static constexpr uint64_t sanitize_hash(uint64_t h) noexcept {
        return (h <= kTombstoneHash) ? (h + 2) : h;
    }

    [[nodiscard]] std::string_view intern_string(std::string_view src) noexcept {
        if (src.empty()) return "";
        char* mem = arena_.allocate<char>(src.size() + 1);
        if (!mem) [[unlikely]] return src;
        std::memcpy(mem, src.data(), src.size());
        mem[src.size()] = '\0';
        return std::string_view(mem, src.size());
    }
};

/**
 * @struct ScopeGuard
 * @brief Zero-overhead RAII sentry for lexical scoping.
 */
struct ScopeGuard {
    SymbolTable& table;
    explicit ScopeGuard(SymbolTable& t) noexcept : table(t) { table.push_scope(); }
    ~ScopeGuard() noexcept { table.pop_scope(); }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
};

/**
 * @brief Legacy and Emitter-specific Aliases
 */
using DGTLSymbolTable = dgtl::semantic::SymbolTable;

} // namespace dgtl::semantic

export namespace dgtl::ir {
    // Cross-namespace exposure for IR-focused modules
    using SymbolEntry = dgtl::semantic::SymbolEntry;
    using SymbolTable = dgtl::semantic::SymbolTable;
}