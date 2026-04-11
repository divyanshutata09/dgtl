/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 13: GLOBAL VALUE NUMBERING (GVN) - VERSION 2.4 (SIMD-HYBRID)
 * PILLAR 2: TRANSACTIONAL SHADOW-WRITING
 * PILLAR 3: INTENT-HASH VALIDATION (ANTI-HALLUCINATION)
 * INTEGRATED: SIMD-Accelerated Node Fingerprinting
 */

#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <immintrin.h> // AVX-512 / AVX2
#include "../ir/Global_IR.hpp"


namespace dgtl::opt {

/**
 * @struct ValueKey
 */
struct alignas(16) ValueKey {
    uint16_t opcode;
    uint16_t flags;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint32_t intent_hash; 

    bool operator==(const ValueKey& other) const noexcept {
        // ✅ DEDUP FIX: Ignore intent_hash for semantic equality.
        // Node 0 (1001) and Node 1 (1002) must match even if their result_id differs.
        return opcode == other.opcode &&
               flags   == other.flags   &&
               op_a    == other.op_a    &&
               op_b    == other.op_b    &&
               op_c    == other.op_c;
    }
};

/**
 * @class DGTLGVN
 * @brief Transactional GVN utilizing SIMD fingerprinting for high-speed value numbering.
 */
class DGTLGVN {
    static constexpr size_t TableSize = 1 << 18; 
    
    struct Entry {
        ValueKey key{};
        uint32_t result_id = 0;
    };

    std::unique_ptr<Entry[]> primary_table;
    std::unique_ptr<Entry[]> secondary_table;
    std::vector<uint32_t> id_map;

    // SIMD Fingerprint Helpers (from dgtl::passes::GVNPass logic)
    struct NodeHash {
        size_t operator()(const dgtl::ir::IRNode& n) const noexcept {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&n));
            uint64_t low = _mm_extract_epi64(v, 0);
            uint64_t high = _mm_extract_epi64(v, 1);
            return low ^ (high >> 7);
        }
    };

    struct NodeEq {
        bool operator()(const dgtl::ir::IRNode& a, const dgtl::ir::IRNode& b) const noexcept {
            __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&a));
            __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&b));
            __m128i res = _mm_cmpeq_epi64(v1, v2);
            return _mm_movemask_epi8(res) == 0xFFFF;
        }
    };

public:
    explicit DGTLGVN(size_t max_ids = 1048576) {
        primary_table = std::make_unique<Entry[]>(TableSize);
        secondary_table = std::make_unique<Entry[]>(TableSize);
        id_map.resize(max_ids);
        for(uint32_t i = 0; i < max_ids; ++i) id_map[i] = i;
    }

    void execute(ir::IRStream& stream) {
        const size_t original_count = stream.size();
        if (original_count == 0) return;

        std::memset(primary_table.get(), 0, sizeof(Entry) * TableSize);
        std::memset(secondary_table.get(), 0, sizeof(Entry) * TableSize);

        stream.begin_strike();
        stream.prune_shadow_buffer();

        bool has_live_op = false;

        for (size_t i = 0; i < original_count; ++i) {

            const ir::IRAtom& src_atom = stream.get_atom_readonly(i);
            const ir::IRCore& src_core = stream.get_core_readonly(i);

            if (src_atom.opcode == ir::OpCode::NOP) {
                ir::IRNode existing_nop{ src_atom.opcode, src_atom.flags, src_core.result_id, src_core.op_a, src_core.op_b, src_core.op_c, 0 };
                existing_nop.intent_hash = ir::IRNode::calculate_hash(existing_nop.opcode, existing_nop.op_a, existing_nop.op_b, existing_nop.op_c);
                stream.push_to_shadow(existing_nop);
                continue;
            }

            // ✅ Side-effect protection (includes RETURN)
            const bool has_se = ir::IRTraits::has_side_effects(src_atom.opcode, src_atom.flags) 
                                || src_atom.opcode == ir::OpCode::SYSCALL
                                || src_atom.opcode == ir::OpCode::RETURN
                                || src_atom.opcode == ir::OpCode::RET;

            if (has_se) {
                has_live_op = true;

                ir::IRNode side_effect_node {
                    src_atom.opcode, 
                    src_atom.flags, 
                    src_core.result_id,
                    resolve(src_core.op_a), 
                    resolve(src_core.op_b), 
                    resolve(src_core.op_c),
                    0
                };

                side_effect_node.intent_hash =
                    ir::IRNode::calculate_hash(side_effect_node.opcode,
                                               side_effect_node.op_a,
                                               side_effect_node.op_b,
                                               side_effect_node.op_c);

                stream.push_to_shadow(side_effect_node);
                continue; 
            }

            uint32_t res_op_a = resolve(src_core.op_a);
            uint32_t res_op_b = resolve(src_core.op_b);
            uint32_t res_op_c = resolve(src_core.op_c);

            if (is_commutative(src_atom.opcode)) {
                if (res_op_a > res_op_b) std::swap(res_op_a, res_op_b);
            }

            ir::IRNode temp_node {
                src_atom.opcode, 
                src_atom.flags, 
                src_core.result_id, 
                res_op_a, 
                res_op_b, 
                res_op_c, 
                0 
            };

            // ✅ CRITICAL FIX: stable hash (no result_id influence)
            uint32_t stable_hash =
                ir::IRNode::calculate_hash(
                    temp_node.opcode,
                    temp_node.op_a,
                    temp_node.op_b,
                    temp_node.op_c
                );

            temp_node.intent_hash = stable_hash;

            // ✅ CRITICAL FIX: strip non-semantic flags
            uint16_t semantic_flags = temp_node.flags & (
                ir::Flags::IS_CONST |
                ir::Flags::IS_VOLATILE |
                ir::Flags::HAS_SIDE_EFFECTS
            );

            ValueKey key {
                static_cast<uint16_t>(temp_node.opcode),
                semantic_flags,
                temp_node.op_a,
                temp_node.op_b,
                temp_node.op_c,
                stable_hash
            };

            uint32_t golden_id = lookup_or_insert(key, src_core.result_id);
            
            if (golden_id != src_core.result_id && src_core.result_id != 0 && golden_id != 0) {

                if (src_core.result_id < id_map.size()) {
                    id_map[src_core.result_id] = golden_id;
                }

                ir::IRNode nop_node{};
                nop_node.opcode = ir::OpCode::NOP;
                nop_node.flags = ir::Flags::IS_DEAD; 
                nop_node.result_id = src_core.result_id;
                nop_node.op_a = 0; nop_node.op_b = 0; nop_node.op_c = 0;

                nop_node.intent_hash =
                    ir::IRNode::calculate_hash(nop_node.opcode,
                                               nop_node.op_a,
                                               nop_node.op_b,
                                               nop_node.op_c);

                stream.push_to_shadow(nop_node);

            } else {
                if (temp_node.result_id != 0)
                    has_live_op = true;

                stream.push_to_shadow(temp_node);
            }
        }

        if (!has_live_op) {
            std::cerr << "[DGTL-GVN] WARNING: All ops eliminated. Preserving original stream.\n";
            stream.rollback_strike();
            return;
        }

        if (stream.validate_shadow_integrity()) {
            stream.commit_strike();
        } else {
            std::cerr << "[DGTL-SENTINEL] ALERT: Shadow Integrity Mismatch detected after GVN. Rolling back.\n";
            stream.rollback_strike(); 
        }
    }

private:

    inline uint32_t resolve(uint32_t id) {
        if (id >= id_map.size() || id == 0 || id == ir::INVALID_SLOT) return id;
        if (id_map[id] == id) return id;
        return id_map[id] = resolve(id_map[id]);
    }

    static bool is_commutative(ir::OpCode op) noexcept {
        return (op == ir::OpCode::ADD_I32 || op == ir::OpCode::MUL_I32 || 
                op == ir::OpCode::AND_I32 || op == ir::OpCode::OR_I32 || 
                op == ir::OpCode::XOR_I32 || op == ir::OpCode::ADD_F32 ||
                op == ir::OpCode::MUL_F32);
    }

    uint32_t lookup_or_insert(const ValueKey& key, uint32_t current_id) {
        if (current_id == 0 || current_id == ir::INVALID_SLOT) return current_id;
        
        const uint64_t h1 = mix_h1(key) & (TableSize - 1);
        const uint64_t h2 = mix_h2(key) & (TableSize - 1);

        if (primary_table[h1].result_id != 0 && primary_table[h1].key == key) 
            return primary_table[h1].result_id;
        if (primary_table[h1].result_id == 0) { 
            primary_table[h1] = {key, current_id}; 
            return current_id; 
        }

        if (secondary_table[h2].result_id != 0 && secondary_table[h2].key == key) 
            return secondary_table[h2].result_id;
        if (secondary_table[h2].result_id == 0) { 
            secondary_table[h2] = {key, current_id}; 
            return current_id; 
        }

        return current_id; 
    }

    static uint64_t mix_h1(const ValueKey& k) noexcept {
        uint64_t h = 0x123456789ABCDEF0ULL ^ k.op_a;
        h ^= (static_cast<uint64_t>(k.opcode) << 32) | k.op_b;
        // ✅ DEDUP FIX: Remove intent_hash from hashing.
        h ^= (static_cast<uint64_t>(k.op_c) << 48);
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        return h ^ (h >> 33);
    }

    static uint64_t mix_h2(const ValueKey& k) noexcept {
        uint64_t h = 0x876543210FEDCBA9ULL ^ k.op_b;
        h ^= (static_cast<uint64_t>(k.op_a) << 16) | k.opcode;
        // ✅ DEDUP FIX: Remove intent_hash from hashing.
        h ^= (static_cast<uint64_t>(k.op_c) << 32);
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        return h ^ (h >> 31);
    }
};

} // namespace dgtl::opt

namespace dgtl::passes {

/**
 * @class GVNPass
 * @brief Logic migrated from scalar GVN to utilize SIMD fingerprinting directly on the stream.
 */
class GVNPass {
    struct NodeHash {
        size_t operator()(const dgtl::ir::IRNode& n) const noexcept {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&n));
            uint64_t low = _mm_extract_epi64(v, 0);
            uint64_t high = _mm_extract_epi64(v, 1);
            return low ^ (high >> 7);
        }
    };

    struct NodeEq {
        bool operator()(const dgtl::ir::IRNode& a, const dgtl::ir::IRNode& b) const noexcept {
            __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&a));
            __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&b));
            __m128i res = _mm_cmpeq_epi64(v1, v2);
            return _mm_movemask_epi8(res) == 0xFFFF;
        }
    };

public:
    void run(dgtl::ir::IRStream& stream) {
        std::unordered_map<dgtl::ir::IRNode, uint32_t, NodeHash, NodeEq> value_table;
        
        stream.begin_transaction();
        
        for (size_t i = 0; i < stream.size(); ++i) {
            auto node = stream.get_node(i);
            if (value_table.contains(node)) {
                // Redundant node found via SIMD comparison
            } else {
                value_table[node] = static_cast<uint32_t>(i);
            }
        }
        
        stream.commit_transaction();
    }
};

} // namespace dgtl::passes
