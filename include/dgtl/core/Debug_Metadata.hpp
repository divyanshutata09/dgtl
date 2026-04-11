/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 14: SIDE-TABLE DEBUG METADATA (DWARF/PDB LINE MAPPING)
 * PILLAR 2.5: SHADOW-AWARE METADATA & TRANSACTIONAL LINE-MAPPING
 * STRATEGY: Double-Buffered Metadata Slices for Zero-Corruption Validation.
 */

#pragma once
#include <vector>
#include <cstdint>
#include <span>
#include <cmath>
#include <atomic>
#include <cstring>
#include "../ir/Global_IR.hpp"
#include "core/Arena.hpp"

namespace dgtl::debug {

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

/**
 * @class DGTLDebugStore
 * @brief High-Velocity Metadata Store with Shadow-Buffer support for JIT/AOT debugging.
 */
class DGTLDebugStore {
    // Primary Mappings (Stable/Committed IR)
    SourceLoc* locations = nullptr;
    // Shadow Mappings (In-Flight Strike/Transactional IR)
    SourceLoc* shadow_locations = nullptr;

    size_t capacity = 0;
    std::atomic<size_t> count{0};

    struct ScopeEntry {
        uint32_t start_node;
        uint32_t end_node;
        uint32_t name_sym;
    };
    std::vector<ScopeEntry> scopes;

public:
    /**
     * @brief Allocates paired Metadata slices from the DGTLArena.
     * PILLAR 2: Dual-allocation supports atomic rollbacks and shadow validation.
     */
    explicit DGTLDebugStore(core::DGTLArena& arena, size_t max_nodes) {
        capacity = max_nodes;
        // 64-byte alignment for cache-line optimization
        locations = arena.allocate<SourceLoc>(max_nodes, 64);
        shadow_locations = arena.allocate<SourceLoc>(max_nodes, 64);
        
        if (!locations || !shadow_locations) [[unlikely]] {
            capacity = 0;
        } else {
            std::memset(locations, 0, sizeof(SourceLoc) * max_nodes);
            std::memset(shadow_locations, 0, sizeof(SourceLoc) * max_nodes);
        }
    }

    /**
     * @brief O(1) Metadata Recording.
     * @param node_idx The index in the IRStream.
     * @param is_shadow If true, writes to the strike-validation buffer (Pillar 2).
     */
    void record_loc(size_t node_idx, uint32_t line, uint16_t col, uint16_t file_id, bool is_shadow = false) noexcept {
        if (node_idx < capacity) [[likely]] {
            SourceLoc* target = is_shadow ? shadow_locations : locations;
            target[node_idx] = {line, col, file_id};
            
            // Atomic update to count for multi-threaded emission safety
            size_t current = count.load(std::memory_order_relaxed);
            while (node_idx + 1 > current && !count.compare_exchange_weak(current, node_idx + 1));
        }
    }

    /**
     * @brief Retrieves metadata for a node. 
     * During optimization strikes, preferred access is given to the shadow slice.
     */
    [[nodiscard]] SourceLoc get_loc(size_t node_idx, bool prefer_shadow = false) const noexcept {
        if (node_idx >= count.load(std::memory_order_relaxed)) return {0, 0, 0};
        
        if (prefer_shadow && shadow_locations[node_idx].line != 0) {
            return shadow_locations[node_idx];
        }
        return locations[node_idx];
    }

    /**
     * @brief Pillar 2 Commit: Transfers validated shadow metadata to primary storage.
     * Invoked only after the Sentinel (Pillar 3) validates the shadow IR.
     */
    void commit_shadow_slice(size_t start, size_t end) noexcept {
        const size_t limit = std::min(end, capacity);
        for (size_t i = start; i < limit; ++i) {
            if (shadow_locations[i].line != 0) {
                locations[i] = shadow_locations[i];
                // Zero out shadow after commit to keep the buffer clean for next strike
                shadow_locations[i] = {0, 0, 0};
            }
        }
    }

    /**
     * @brief Metadata Rollback: Purges the shadow slice on strike failure.
     */
    void rollback_shadow_slice(size_t start, size_t end) noexcept {
        const size_t limit = std::min(end, capacity);
        for (size_t i = start; i < limit; ++i) {
            shadow_locations[i] = {0, 0, 0};
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
     * Operates strictly on validated primary storage.
     */
    std::vector<uint8_t> emit_dwarf_line_table() const {
        std::vector<uint8_t> buffer;
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
            // DW_LNS_copy (0x01): Append a row to the line table matrix
            buffer.push_back(0x01); 
        }
        return buffer;
    }

    /**
     * @brief Resets the store and clears all metadata mappings.
     */
    void reset() noexcept { 
        count.store(0, std::memory_order_release); 
        scopes.clear(); 
        if (locations && capacity > 0) {
            std::memset(locations, 0, sizeof(SourceLoc) * capacity);
            std::memset(shadow_locations, 0, sizeof(SourceLoc) * capacity);
        }
    }
};

} // namespace dgtl::debug
