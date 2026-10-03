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
 * STEP 14: SIDE-TABLE DEBUG METADATA (DWARF/PDB LINE MAPPING)
 * PILLAR 2.5: SHADOW-AWARE METADATA & TRANSACTIONAL LINE-MAPPING
 * STRATEGY: Double-Buffered Metadata Slices for Zero-Corruption Validation.
 */

module;

#include <cstdlib>

#include <vector>
#include <cstdint>
#include <span>
#include <cmath>
#include <atomic>
#include <cstring>
#include <algorithm>

export module dgtl.ir.debug;

// Import native compiler sub-systems and types

import dgtl.arena;
import dgtl.ir_enums;

export namespace dgtl::ir::debug {

/**
 * @struct SourceLoc
 * @brief Source Location Information packed for high-density storage.
 * alignas(8) ensures we can load/store this in a single 64-bit CPU cycle.
 */
struct alignas(8) SourceLoc {
    uint32_t line;
    uint16_t column;
    uint16_t file_id; 
};

struct alignas(8) TypeLoc {
    uint16_t type_tag;
    uint16_t ownership_flags;
    uint16_t nullability_flags;
    uint16_t aux_flags;
};

/**
 * @class DGTLDebugStore
 * @brief High-Velocity Metadata Store with Shadow-Buffer support for debugging.
 */
class DGTLDebugStore {
private:
    // Primary Mappings (Stable/Committed IR)
    SourceLoc* locations = nullptr;
    // Shadow Mappings (In-Flight Strike/Transactional IR)
    SourceLoc* shadow_locations = nullptr;
    TypeLoc* type_locations = nullptr;
    TypeLoc* shadow_type_locations = nullptr;

    size_t capacity = 0;
    std::atomic<size_t> count{0};

    struct ScopeEntry {
        [[maybe_unused]] uint32_t start_node;
        [[maybe_unused]] uint32_t end_node;
        [[maybe_unused]] uint32_t name_sym;
    };
    std::vector<ScopeEntry> scopes;

    [[nodiscard]] bool has_storage() const noexcept {
        return capacity != 0 && locations && shadow_locations && type_locations && shadow_type_locations;
    }

public:
    /**
     * @brief Allocates paired Metadata slices from the DGTL_Arena.
     * PILLAR 2: Dual-allocation supports atomic rollbacks and shadow validation.
     */
    explicit DGTLDebugStore(core::DGTL_Arena& arena, size_t max_nodes) {
        capacity = max_nodes;
        
        // Corrected allocation: passing alignment if the Arena signature supports it, 
        // or using the standard T, count pattern.
        locations = arena.allocate<SourceLoc>(max_nodes);
        shadow_locations = arena.allocate<SourceLoc>(max_nodes);
        type_locations = arena.allocate<TypeLoc>(max_nodes);
        shadow_type_locations = arena.allocate<TypeLoc>(max_nodes);
        
        if (!locations || !shadow_locations || !type_locations || !shadow_type_locations) [[unlikely]] {
            capacity = 0;
        } else {
            std::memset(locations, 0, sizeof(SourceLoc) * max_nodes);
            std::memset(shadow_locations, 0, sizeof(SourceLoc) * max_nodes);
            std::memset(type_locations, 0, sizeof(TypeLoc) * max_nodes);
            std::memset(shadow_type_locations, 0, sizeof(TypeLoc) * max_nodes);
        }
    }

    /**
     * @brief O(1) Metadata Recording.
     * @param node_idx The index in the IRStream.
     * @param is_shadow If true, writes to the strike-validation buffer (Pillar 2).
     */
    void record_loc(size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id, bool is_shadow = false) noexcept {
        if (has_storage() && node_idx < capacity) [[likely]] {
            SourceLoc* target = is_shadow ? shadow_locations : locations;
            target[node_idx] = {line, col, file_id};
            
            // Atomic update to count for multi-threaded emission safety
            size_t current = count.load(std::memory_order_relaxed);
            while (node_idx + 1 > current &&
                   !count.compare_exchange_weak(current, node_idx + 1,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {}
        }
    }

    /**
     * @brief Retrieves metadata for a node. 
     * During optimization strikes, preferred access is given to the shadow slice.
     */
    [[nodiscard]] SourceLoc get_loc(size_t node_idx, bool prefer_shadow = false) const noexcept {
        if (!has_storage()) return {0, 0, 0};
        if (node_idx >= count.load(std::memory_order_relaxed)) return {0, 0, 0};
        
        if (prefer_shadow && shadow_locations[node_idx].line != 0) {
            return shadow_locations[node_idx];
        }
        return locations[node_idx];
    }

    void record_type(size_t node_idx,
                     uint16_t type_tag,
                     uint16_t ownership_flags,
                     uint16_t nullability_flags,
                     uint16_t aux_flags = 0,
                     bool is_shadow = false) noexcept {
        if (has_storage() && node_idx < capacity) [[likely]] {
            TypeLoc* target = is_shadow ? shadow_type_locations : type_locations;
            target[node_idx] = {type_tag, ownership_flags, nullability_flags, aux_flags};

            size_t current = count.load(std::memory_order_relaxed);
            while (node_idx + 1 > current &&
                   !count.compare_exchange_weak(current, node_idx + 1,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {}
        }
    }

    [[nodiscard]] TypeLoc get_type(size_t node_idx, bool prefer_shadow = false) const noexcept {
        if (!has_storage()) return {};
        if (node_idx >= count.load(std::memory_order_relaxed)) return {};

        if (prefer_shadow) {
            const TypeLoc& shadow = shadow_type_locations[node_idx];
            if (shadow.type_tag != 0 || shadow.ownership_flags != 0 || shadow.nullability_flags != 0 || shadow.aux_flags != 0) {
                return shadow;
            }
        }
        return type_locations[node_idx];
    }

    void record_type_code(size_t node_idx,
                          TypeCode type_code,
                          uint16_t ownership_flags,
                          uint16_t nullability_flags,
                          uint16_t aux_flags = 0,
                          bool is_shadow = false) noexcept {
        record_type(node_idx,
                    static_cast<uint16_t>(type_code),
                    ownership_flags,
                    nullability_flags,
                    aux_flags,
                    is_shadow);
    }

    [[nodiscard]] TypeCode get_type_code(size_t node_idx, bool prefer_shadow = false) const noexcept {
        const TypeLoc t = get_type(node_idx, prefer_shadow);
        if (!is_valid_type_handle(t.type_tag)) {
            return TypeCode::VOID_T;
        }
        return static_cast<TypeCode>(t.type_tag);
    }

    /**
     * @brief Pillar 2 Commit: Transfers validated shadow metadata to primary storage.
     * Invoked only after the Sentinel (Pillar 3) validates the shadow IR.
     */
    void commit_shadow_slice(size_t start, size_t end) noexcept {
        if (!has_storage()) return;
        const size_t limit = std::min(end, capacity);
        if (start >= limit) return;
        for (size_t i = start; i < limit; ++i) {
            if (shadow_locations[i].line != 0) {
                locations[i] = shadow_locations[i];
                // Zero out shadow after commit to keep the buffer clean for next strike
                shadow_locations[i] = {0, 0, 0};
            }
            if (shadow_type_locations[i].type_tag != 0 ||
                shadow_type_locations[i].ownership_flags != 0 ||
                shadow_type_locations[i].nullability_flags != 0 ||
                shadow_type_locations[i].aux_flags != 0) {
                type_locations[i] = shadow_type_locations[i];
                shadow_type_locations[i] = {};
            }
        }
    }

    /**
     * @brief Metadata Rollback: Purges the shadow slice on strike failure.
     */
    void rollback_shadow_slice(size_t start, size_t end) noexcept {
        if (!has_storage()) return;
        const size_t limit = std::min(end, capacity);
        if (start >= limit) return;
        for (size_t i = start; i < limit; ++i) {
            shadow_locations[i] = {0, 0, 0};
            shadow_type_locations[i] = {};
        }
    }

    /**
     * @brief Signed LEB128 Encoding for DWARF/WASM metadata compression.
     */
    static size_t write_sleb128(uint8_t* dest, int64_t value) noexcept {
        uint8_t* p = dest;
        bool more = true;
        while (more) {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
                more = false;
            } else {
                byte |= 0x80;
            }
            *p++ = byte;
        }
        return static_cast<size_t>(p - dest);
    }

    /**
     * @brief Generates DWARF-compatible .debug_line deltas.
     */
    std::vector<uint8_t> emit_dwarf_line_table() const {
        std::vector<uint8_t> buffer;
        if (!has_storage()) return buffer;
        const size_t current_count = count.load(std::memory_order_acquire);
        buffer.reserve(current_count * 2); 

        uint32_t last_line = 1;
        uint8_t temp_leb[10];

        for (size_t i = 0; i < current_count; ++i) {
            const auto& loc = locations[i];
            if (loc.line == 0) continue;

            int32_t line_delta = static_cast<int32_t>(loc.line) - static_cast<int32_t>(last_line);
            
            if (line_delta != 0) {
                // DW_LNS_advance_line (0x03)
                buffer.push_back(0x03); 
                size_t n = write_sleb128(temp_leb, line_delta);
                buffer.insert(buffer.end(), temp_leb, temp_leb + n);
                last_line = loc.line;
            }
            // DW_LNS_copy (0x01)
            buffer.push_back(0x01); 
        }
        return buffer;
    }

    /**
     * @brief Resets the store and clears all metadata mappings.
     */
    void reset() noexcept { 
        const size_t current_count = std::min(count.exchange(0, std::memory_order_acq_rel), capacity); 
        scopes.clear(); 
        if (locations && current_count > 0) {
            std::memset(locations, 0, sizeof(SourceLoc) * current_count);
            std::memset(shadow_locations, 0, sizeof(SourceLoc) * current_count);
            std::memset(type_locations, 0, sizeof(TypeLoc) * current_count);
            std::memset(shadow_type_locations, 0, sizeof(TypeLoc) * current_count);
        }
    }
};

} // namespace dgtl::ir::debug



