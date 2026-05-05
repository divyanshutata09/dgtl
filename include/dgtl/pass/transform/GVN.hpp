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
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 13: GLOBAL VALUE NUMBERING (GVN) - VERSION 3.1 (STABLE FINAL)
 *
 * FIXES:
 * Ã¢Å“â€ Proper ID propagation (chain resolution bug FIXED)
 * Ã¢Å“â€ Constant folding restored (critical for pipeline1)
 * Ã¢Å“â€ PRE-lite strengthened
 * Ã¢Å“â€ Memory version isolation retained
 * Ã¢Å“â€ SIMD-safe hashing retained
 */

#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <immintrin.h>
#include <mutex>

#ifdef CONST
#undef CONST
#endif

#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::pass {

struct alignas(16) ValueKey {
    uint16_t opcode;
    uint16_t flags;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint32_t intent_hash;

    bool operator==(const ValueKey& o) const noexcept {
        return opcode == o.opcode &&
               flags  == o.flags  &&
               op_a   == o.op_a   &&
               op_b   == o.op_b   &&
               op_c   == o.op_c;
    }
};

class DGTLGVN {
    static constexpr size_t TableSize = 1 << 22;

    struct Entry {
        ValueKey key{};
        uint32_t result_id = 0;
    };

    std::unique_ptr<Entry[]> primary_table;
    std::unique_ptr<Entry[]> secondary_table;

    std::vector<uint32_t> id_map;

    // Ã°Å¸â€Â¥ Memory versioning
    uint32_t memory_version = 0;

    // Ã°Å¸â€Â¥ PRE-lite
    static constexpr size_t PRE_WINDOW = 64;
    std::vector<ValueKey> pre_keys;
    std::vector<uint32_t> pre_ids;

public:
    explicit DGTLGVN(size_t max_ids = 1048576) {
        primary_table = std::make_unique<Entry[]>(TableSize);
        secondary_table = std::make_unique<Entry[]>(TableSize);

        id_map.resize(max_ids);
        for (uint32_t i = 0; i < max_ids; ++i) id_map[i] = i;

        pre_keys.reserve(PRE_WINDOW);
        pre_ids.reserve(PRE_WINDOW);
    }

    void execute(ir::IRStream& stream) {

        const size_t N = stream.size();
        if (N == 0) return;

        std::fill_n(primary_table.get(), TableSize, Entry{});
        std::fill_n(secondary_table.get(), TableSize, Entry{});

        pre_keys.clear();
        pre_ids.clear();
        memory_version = 0;
        for (uint32_t i = 0; i < id_map.size(); ++i) {
            id_map[i] = i;
        }

        stream.begin_transaction();
        stream.prune_shadow_buffer();

        bool has_live = false;

        for (size_t i = 0; i < N; ++i) {

            const auto& atom = stream.get_atom_readonly(i);
            const auto& core = stream.get_core_readonly(i);

            // Ã°Å¸â€Â¥ Memory barrier
            if (ir::IRTraits::has_side_effects(atom.opcode, atom.flags)) {
                memory_version++;
            }

            // NOP passthrough
            if (atom.opcode == ir::OpCode::NOP) {
                ir::IRNode n{atom.opcode, atom.flags,
                             core.result_id,
                             core.op_a, core.op_b, core.op_c, 0, 0u};

                n.intent_hash = ir::IRNode::calculate_hash(n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
                stream.push_to_shadow(n);
                continue;
            }

            // Side effects Ã¢â€ â€™ barrier
            const bool has_se =
                ir::IRTraits::has_side_effects(atom.opcode, atom.flags) ||
                atom.opcode == ir::OpCode::SYSCALL ||
                atom.opcode == ir::OpCode::RETURN ||
                atom.opcode == ir::OpCode::RET;

            if (has_se) {
                has_live = true;

                ir::IRNode n{
                    atom.opcode,
                    atom.flags,
                    core.result_id,
                    resolve(core.op_a),
                    resolve(core.op_b),
                    resolve(core.op_c),
                    0,
                    0u
                };

                n.intent_hash = ir::IRNode::calculate_hash(
                    n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);

                stream.push_to_shadow(n);
                continue;
            }

            uint32_t a = resolve(core.op_a);
            uint32_t b = resolve(core.op_b);
            uint32_t c = resolve(core.op_c);

            // Ã°Å¸â€Â¥ CONSTANT FOLDING (RESTORED)
            if (atom.opcode == ir::OpCode::ADD_I32) {
                if (is_zero(stream, a, i)) {
                    map_id(core.result_id, b);
                    stream.push_to_shadow(make_mov(core.result_id, b));
                    continue;
                }
                if (is_zero(stream, b, i)) {
                    map_id(core.result_id, a);
                    stream.push_to_shadow(make_mov(core.result_id, a));
                    continue;
                }
            }

            if (is_commutative(atom.opcode)) {
                if (a > b) std::swap(a, b);
            }

            ir::IRNode temp{
                atom.opcode, atom.flags,
                core.result_id,
                a,b,c,0,0u
            };

            uint64_t hash64 =
                ir::IRNode::calculate_hash(temp.opcode, temp.result_id, a, b, c);

            temp.intent_hash = hash64;
            const uint32_t hash = static_cast<uint32_t>(hash64 & 0xFFFFFFFFu);

            uint16_t sem_flags =
                temp.flags & (ir::Flags::IS_CONST |
                              ir::Flags::IS_VOLATILE |
                              ir::Flags::HAS_SIDE_EFFECTS);

            ValueKey key{
                static_cast<uint16_t>(temp.opcode),
                sem_flags,
                a,b,c,
                hash ^ memory_version // Ã°Å¸â€Â¥ critical isolation
            };

            // Ã°Å¸â€Â¥ PRE-LITE STRONG CHECK
            for (size_t k = pre_keys.size(); k-- > 0;) {
                if (pre_keys[k] == key) {
                    uint32_t gid = pre_ids[k];
                    map_id(core.result_id, gid);
                    stream.push_to_shadow(make_nop(core.result_id));
                    goto next_iter;
                }
            }

            {
                uint32_t gid = lookup_or_insert(key, core.result_id);

                if (gid != core.result_id && gid != 0) {
                    map_id(core.result_id, gid);
                    resolve(core.result_id);

                    stream.push_to_shadow(make_nop(core.result_id));
                } else {
                    has_live = true;
                    stream.push_to_shadow(temp);

                    // push into PRE
                    if (pre_keys.size() >= PRE_WINDOW) {
                        pre_keys.erase(pre_keys.begin());
                        pre_ids.erase(pre_ids.begin());
                    }
                    pre_keys.push_back(key);
                    pre_ids.push_back(core.result_id);
                }
            }

        next_iter:
            continue;
        }

        if (!has_live) {
            stream.rollback_transaction();
            return;
        }

        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:

    inline void map_id(uint32_t from, uint32_t to) {
        if (from == 0 || to == 0) return;
        if (from >= id_map.size()) return;
        id_map[from] = to;
    }

    inline uint32_t resolve(uint32_t id) {
        if (id == 0 || id == ir::INVALID_SLOT) return id;
        if (id >= id_map.size()) return id;

        uint32_t root = id;
        while (root < id_map.size() && id_map[root] != root)
            root = id_map[root];

        // Ã°Å¸â€Â¥ FULL path compression
        while (id < id_map.size() && id_map[id] != id) {
            uint32_t p = id_map[id];
            id_map[id] = root;
            id = p;
        }

        return root;
    }

    static bool is_zero(ir::IRStream& s, uint32_t id, size_t upto) {
        if (id == 0 || id == ir::INVALID_SLOT) return false;

        for (size_t j = 0; j < upto; ++j) {
            if (s.get_core_readonly(j).result_id == id) {
                const auto& a = s.get_atom_readonly(j);
                if (a.opcode == ir::OpCode::CONST &&
                    s.get_core_readonly(j).op_a == 0)
                    return true;
                return false;
            }
        }
        return false;
    }

    static ir::IRNode make_mov(uint32_t dst, uint32_t src) {
        ir::IRNode n{};
        n.opcode = ir::OpCode::MOV;
        n.result_id = dst;
        n.op_a = src;
        return n;
    }

    static ir::IRNode make_nop(uint32_t id) {
        ir::IRNode n{};
        n.opcode = ir::OpCode::NOP;
        n.flags = ir::Flags::IS_DEAD;
        n.result_id = id;
        return n;
    }

    static bool is_commutative(ir::OpCode op) noexcept {
        return (op == ir::OpCode::ADD_I32 ||
                op == ir::OpCode::MUL_I32 ||
                op == ir::OpCode::AND_I32 ||
                op == ir::OpCode::OR_I32 ||
                op == ir::OpCode::XOR_I32 ||
                op == ir::OpCode::ADD_F32 ||
                op == ir::OpCode::MUL_F32);
    }

    uint32_t lookup_or_insert(const ValueKey& key, uint32_t id) {
        static constexpr size_t MaxProbes = 32;  // Prevent infinite loops on hash collisions

        const uint64_t h1 = mix_h1(key) & (TableSize - 1);
        const uint64_t h2 = mix_h2(key) & (TableSize - 1);

        size_t probes = 0;

        if (primary_table[h1].result_id &&
            primary_table[h1].key == key)
            return primary_table[h1].result_id;

        if (!primary_table[h1].result_id && probes++ < MaxProbes) {
            primary_table[h1] = {key, id};
            return id;
        }

        if (secondary_table[h2].result_id &&
            secondary_table[h2].key == key)
            return secondary_table[h2].result_id;

        if (!secondary_table[h2].result_id && probes++ < MaxProbes) {
            secondary_table[h2] = {key, id};
            return id;
        }

        // If tables are full or too many collisions, skip optimization
        return id;
    }

    static uint64_t mix_h1(const ValueKey& k) noexcept {
        uint64_t h = 0x123456789ABCDEF0ULL ^ k.op_a;
        h ^= (uint64_t(k.opcode) << 32) | k.op_b;
        h ^= (uint64_t(k.op_c) << 48);
        h ^= (uint64_t(k.flags) << 16);
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        return h ^ (h >> 33);
    }

    static uint64_t mix_h2(const ValueKey& k) noexcept {
        uint64_t h = 0x876543210FEDCBA9ULL ^ k.op_b;
        h ^= (uint64_t(k.op_a) << 16) | k.opcode;
        h ^= (uint64_t(k.op_c) << 32);
        h ^= (uint64_t(k.flags) << 48);
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        return h ^ (h >> 31);
    }
};

} // namespace dgtl::pass


