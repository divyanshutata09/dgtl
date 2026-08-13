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
 * Unified scalar-core transform header:
 * - Global value numbering
 * - Dead code elimination
 * - Instruction folding / fusion / reorder
 */

module;

#include <cstdlib>

#include <vector>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <bitset>
#include <optional>
#include <atomic>
#include <immintrin.h>
#include <mutex>
#include <stdexcept>
#include <cstddef>

#ifdef CONST
#undef CONST
#endif

export module dgtl.ir_core.transform_scalar_core;

import dgtl.arena;
import dgtl.ir_core;
import dgtl.op_traits;
import dgtl.ir_core.pass_manager_config;

export namespace dgtl::pass::transform {

namespace analysis = dgtl::analysis;
namespace core = dgtl::core;
namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;
namespace iface = dgtl::ir::interfaces;

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
    uint32_t memory_version = 0;
    static constexpr size_t PRE_WINDOW = 64;
    std::vector<ValueKey> pre_keys;
    std::vector<uint32_t> pre_ids;

public:
    explicit DGTLGVN(size_t max_ids = 1048576) {
        // FIXED: Fixed the double make typo here
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

            // FIXED: Checked side effects via the native flag mask layout directly
            if (atom.flags & ir::Flags::HAS_SIDE_EFFECTS) {
                memory_version++;
            }

            if (atom.opcode == ir::OpCode::NOP) {
                ir::IRNode n{atom.opcode, atom.flags,
                             core.result_id,
                             core.op_a, core.op_b, core.op_c, 0, 0u};
                n.intent_hash = ir::IRNode::calculate_hash(n.opcode, n.result_id, n.op_a, n.op_b, n.op_c);
                stream.push_to_shadow(n);
                continue;
            }

            // FIXED: Checked side effects and traits via explicit flag masks and op traits cleanly
            const bool has_se =
                (atom.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
                iface::has_trait(atom.opcode, iface::OpTrait::IsTerminator);

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

            ir::IRNode temp{ atom.opcode, atom.flags, core.result_id, a, b, c, 0, 0u };
            uint64_t hash64 = ir::IRNode::calculate_hash(temp.opcode, temp.result_id, a, b, c);
            temp.intent_hash = hash64;
            const uint32_t hash = static_cast<uint32_t>(hash64 & 0xFFFFFFFFu);

            uint16_t sem_flags = temp.flags & (ir::Flags::IS_CONST |
                                               ir::Flags::IS_VOLATILE |
                                               ir::Flags::HAS_SIDE_EFFECTS);

            ValueKey key{
                static_cast<uint16_t>(temp.opcode),
                sem_flags,
                a, b, c,
                hash ^ memory_version
            };

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

                    if (pre_keys.size() >= PRE_WINDOW) {
                        pre_keys.erase(pre_keys.begin());
                        pre_ids.erase(pre_ids.begin());
                    }
                    pre_keys.push_back(key);
                    pre_ids.push_back(core.result_id);
                }
            }

        next_iter:
            ;
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
    void map_id(uint32_t from, uint32_t to) {
        if (from == 0 || to == 0) return;
        if (from >= id_map.size()) return;
        id_map[from] = to;
    }

    uint32_t resolve(uint32_t id) {
        if (id == 0 || id == ir::INVALID_SLOT) return id;
        if (id >= id_map.size()) return id;

        uint32_t root = id;
        while (root < id_map.size() && id_map[root] != root)
            root = id_map[root];

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
        return iface::has_trait(op, iface::OpTrait::IsCommutative);
    }

    uint32_t lookup_or_insert(const ValueKey& key, uint32_t id) {
        static constexpr size_t MaxProbes = 32;

        const uint64_t h1 = mix_h1(key) & (TableSize - 1);
        const uint64_t h2 = mix_h2(key) & (TableSize - 1);

        if (primary_table[h1].result_id && primary_table[h1].key == key)
            return primary_table[h1].result_id;

        if (!primary_table[h1].result_id) {
            primary_table[h1] = {key, id};
            return id;
        }

        if (secondary_table[h2].result_id && secondary_table[h2].key == key)
            return secondary_table[h2].result_id;

        if (!secondary_table[h2].result_id) {
            secondary_table[h2] = {key, id};
            return id;
        }

        for (size_t probes = 1; probes < MaxProbes; ++probes) {
            const uint64_t idx = (h1 + probes) & (TableSize - 1);
            if (primary_table[idx].result_id && primary_table[idx].key == key)
                return primary_table[idx].result_id;
            if (!primary_table[idx].result_id) {
                primary_table[idx] = {key, id};
                return id;
            }
        }

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

class DGTLDCESolver {
    static constexpr uint32_t MAX_TRACKED_IDS = 65536;

public:
    DGTLDCESolver() = default;

    void execute(ir::IRStream& stream) {
        execute(stream, nullptr);
    }

    void execute(ir::IRStream& stream, const pipeline::DGTLLanguageRootMarker* root_marker) {
        if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

        ir::IRStream shadow_stream = ir::IRStream::allocate_transactional(stream.capacity());
        if (shadow_stream.is_null()) return;

        std::bitset<MAX_TRACKED_IDS> live_map;
        mark_live_instructions(stream, live_map, root_marker);
        propagate_liveness(stream, live_map, root_marker);

        shadow_stream.begin_transaction();
        bool success = prune_to_shadow(stream, shadow_stream, live_map, root_marker);

        if (success && shadow_stream.validate_shadow_integrity()) {
            stream.commit_from_shadow(shadow_stream);
        } else {
            shadow_stream.abort_transaction();
        }
    }

private:
    static void mark_live_instructions(
        const ir::IRStream& src,
        std::bitset<MAX_TRACKED_IDS>& live_map,
        const pipeline::DGTLLanguageRootMarker* root_marker
    ) {
        const size_t sz = src.size();
        for (int i = static_cast<int>(sz) - 1; i >= 0; --i) {
            ir::IRNode node = src.get_node(i);

            const bool is_call_like =
                iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow) &&
                !iface::has_trait(node.opcode, iface::OpTrait::IsTerminator);

            if (is_call_like || node.opcode == ir::OpCode::VOLATILE_STORE || node.opcode == ir::OpCode::VOLATILE_LOAD) {
                live_map.set(i % MAX_TRACKED_IDS);
            }

            if (node.result_id != ir::INVALID_SLOT) {
                if (root_marker && root_marker->is_root(node)) {
                    live_map.set(node.result_id % MAX_TRACKED_IDS);
                }
            }
        }
    }

    static void propagate_liveness(
        const ir::IRStream& src,
        std::bitset<MAX_TRACKED_IDS>& live_map,
        const pipeline::DGTLLanguageRootMarker* root_marker
    ) {
        const size_t sz = src.size();
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = static_cast<int>(sz) - 1; i >= 0; --i) {
                ir::IRNode node = src.get_node(i);

                const bool is_call_like =
                    iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow) &&
                    !iface::has_trait(node.opcode, iface::OpTrait::IsTerminator);

                const bool is_memory_write =
                    iface::has_trait(node.opcode, iface::OpTrait::IsMemoryWrite);

                const bool is_essential = node.is_terminator() ||
                                           is_call_like ||
                                           (node.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
                                           is_memory_write ||
                                           (root_marker != nullptr && root_marker->is_root(node));

                const bool has_result = node.result_id != ir::INVALID_SLOT;
                const bool needed_by_successor = has_result && live_map.test(node.result_id % MAX_TRACKED_IDS);

                if (is_essential || needed_by_successor) {
                    if (node.op_a != ir::INVALID_SLOT && !live_map.test(node.op_a % MAX_TRACKED_IDS)) {
                        live_map.set(node.op_a % MAX_TRACKED_IDS);
                        changed = true;
                    }
                    if (node.op_b != ir::INVALID_SLOT && !live_map.test(node.op_b % MAX_TRACKED_IDS)) {
                        live_map.set(node.op_b % MAX_TRACKED_IDS);
                        changed = true;
                    }
                    if (node.op_c != ir::INVALID_SLOT && !live_map.test(node.op_c % MAX_TRACKED_IDS)) {
                        live_map.set(node.op_c % MAX_TRACKED_IDS);
                        changed = true;
                    }
                    if (has_result && !live_map.test(node.result_id % MAX_TRACKED_IDS)) {
                        live_map.set(node.result_id % MAX_TRACKED_IDS);
                        changed = true;
                    }
                }
            }
        }
    }

    static bool prune_to_shadow(
        const ir::IRStream& src,
        ir::IRStream& shadow,
        const std::bitset<MAX_TRACKED_IDS>& live_map,
        const pipeline::DGTLLanguageRootMarker* root_marker
    ) {
        for (size_t i = 0; i < src.size(); ++i) {
            ir::IRNode node = src.get_node(i);

            const bool is_call_like =
                iface::implements_interface(node.opcode, iface::OpInterface::ControlFlow) &&
                !iface::has_trait(node.opcode, iface::OpTrait::IsTerminator);

            const bool is_memory_write =
                iface::has_trait(node.opcode, iface::OpTrait::IsMemoryWrite);

            // FIXED: Swapped out unmapped IRTraitsStatic with native flags mask evaluation
            bool is_live = node.is_terminator() ||
                           is_call_like ||
                           (node.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
                           is_memory_write ||
                           (root_marker != nullptr && root_marker->is_root(node)) ||
                           (node.result_id != ir::INVALID_SLOT && live_map.test(node.result_id % MAX_TRACKED_IDS));

            if (is_live) {
                if (node.result_id == ir::INVALID_SLOT &&
                    !node.is_terminator() &&
                    !is_call_like &&
                    !(node.flags & ir::Flags::HAS_SIDE_EFFECTS)) {
                    if (node.opcode != ir::OpCode::NOP) return false;
                }

                node.intent_hash = ir::IRNode::calculate_hash(
                    node.opcode,
                    node.result_id,
                    node.op_a,
                    node.op_b,
                    node.op_c
                );

                shadow.push_back(node);
            }
        }
        return true;
    }
};

class DeadCodeElimination {
public:
    void execute(dgtl::ir::IRStream& stream) {
        stream.begin_transaction();
        ir::IRAtom* shadow_atoms = stream.raw_atoms();
        ir::IRCore* shadow_cores = stream.raw_cores();
        if (!shadow_atoms || !shadow_cores) {
            stream.rollback_transaction();
            return;
        }

        size_t shadow_count = stream.size();

        uint32_t max_reg_id = 0;
        for (size_t i = 0; i < shadow_count; ++i) {
            const ir::IRCore& c = shadow_cores[i];
            auto bump = [&](uint32_t r) {
                if (r != ir::INVALID_SLOT && r > max_reg_id) {
                    max_reg_id = r;
                }
            };
            bump(c.result_id);
            bump(c.op_a);
            bump(c.op_b);
            bump(c.op_c);
        }

        constexpr uint32_t kDenseIdLimit = 8u * 1024u * 1024u;
        const bool use_dense = (max_reg_id != ir::INVALID_SLOT) && (max_reg_id <= kDenseIdLimit);

        dgtl::core::ThreadLocalArena scratch;
        const size_t dense_sz = use_dense ? (static_cast<size_t>(max_reg_id) + 1u) : 0u;

        size_t ss_sz = 4u;
        if (!use_dense) {
            const size_t needed = (shadow_count > 0u) ? (shadow_count * 3u) : 4u;
            while (ss_sz < needed) {
                ss_sz <<= 1;
            }
        }

        scratch.init(dense_sz + ss_sz * sizeof(uint32_t) + 512u + 64u * 8u);

        uint8_t* dense_live = use_dense ? scratch.allocate<uint8_t>(dense_sz) : nullptr;
        uint32_t* ss_keys = !use_dense ? scratch.allocate<uint32_t>(ss_sz) : nullptr;

        if ((use_dense && !dense_live) || (!use_dense && !ss_keys)) {
            stream.rollback_transaction();
            return;
        }

        if (ss_keys) {
            std::memset(ss_keys, 0xFF, ss_sz * sizeof(uint32_t));
        }

        const size_t ss_mask = ss_sz - 1u;
        constexpr uint32_t SS_EMPTY = 0xFFFFFFFFu;
        constexpr uint32_t SS_DELETED = 0xFFFFFFFEu;

        const auto ss_contains = [&](uint32_t k) noexcept -> bool {
            size_t slot = static_cast<size_t>(k) & ss_mask;
            while (ss_keys[slot] != SS_EMPTY) {
                if (ss_keys[slot] == k) {
                    return true;
                }
                slot = (slot + 1u) & ss_mask;
            }
            return false;
        };

        const auto ss_insert = [&](uint32_t k) noexcept {
            size_t slot = static_cast<size_t>(k) & ss_mask;
            while (ss_keys[slot] != SS_EMPTY && ss_keys[slot] != SS_DELETED && ss_keys[slot] != k) {
                slot = (slot + 1u) & ss_mask;
            }
            ss_keys[slot] = k;
        };

        const auto ss_erase = [&](uint32_t k) noexcept {
            size_t slot = static_cast<size_t>(k) & ss_mask;
            while (ss_keys[slot] != SS_EMPTY) {
                if (ss_keys[slot] == k) {
                    ss_keys[slot] = SS_DELETED;
                    return;
                }
                slot = (slot + 1u) & ss_mask;
            }
        };

        const auto is_live_fn = [&](uint32_t r) noexcept -> bool {
            if (r == ir::INVALID_SLOT) {
                return false;
            }
            return use_dense ? (r < dense_sz && dense_live[r] != 0u) : ss_contains(r);
        };

        const auto set_live = [&](uint32_t r) noexcept {
            if (r == ir::INVALID_SLOT) {
                return;
            }
            if (use_dense) {
                if (r < dense_sz) {
                    dense_live[r] = 1u;
                }
            } else {
                ss_insert(r);
            }
        };

        const auto clear_live = [&](uint32_t r) noexcept {
            if (r == ir::INVALID_SLOT) {
                return;
            }
            if (use_dense) {
                if (r < dense_sz) {
                    dense_live[r] = 0u;
                }
            } else {
                ss_erase(r);
            }
        };

        for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
            ir::IRAtom& atom = shadow_atoms[i];
            ir::IRCore& core = shadow_cores[i];

            // FIXED: Substituted IRTraitsStatic with clear optraits trait definitions and flag logic mapping
            const bool is_terminator = iface::has_trait(atom.opcode, iface::OpTrait::IsTerminator);
            const bool is_call_like = atom.opcode == ir::OpCode::CALL || atom.opcode == ir::OpCode::SYSCALL;
            const bool is_essential = is_call_like ||
                                      (atom.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
                                      is_terminator ||
                                      is_live_fn(core.result_id);

            if (!is_essential) {
                atom.flags |= ir::Flags::IS_DEAD;
            } else {
                clear_live(core.result_id);
                const bool is_imm = atom.opcode == ir::OpCode::LOAD_IMM || atom.opcode == ir::OpCode::IMMEDIATE;
                if (!is_imm) {
                    set_live(core.op_a);
                    set_live(core.op_b);
                    set_live(core.op_c);
                }
            }
        }

        size_t write_idx = 0;
        for (size_t i = 0; i < shadow_count; ++i) {
            if (!(shadow_atoms[i].flags & ir::Flags::IS_DEAD)) {
                if (write_idx != i) {
                    shadow_atoms[write_idx] = shadow_atoms[i];
                    shadow_cores[write_idx] = shadow_cores[i];
                }
                shadow_cores[write_idx].intent_hash = ir::IRNode::calculate_hash(
                    shadow_atoms[write_idx].opcode,
                    shadow_cores[write_idx].result_id,
                    shadow_cores[write_idx].op_a,
                    shadow_cores[write_idx].op_b,
                    shadow_cores[write_idx].op_c);
                ++write_idx;
            }
        }

        stream.set_shadow_count(write_idx);
        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }
};

class GlobalValueNumbering {
public:
    void execute(dgtl::ir::IRStream& stream) {
        stream.begin_transaction();
        ir::IRAtom* shadow_atoms = stream.raw_atoms();
        ir::IRCore* shadow_cores = stream.raw_cores();
        if (!shadow_atoms || !shadow_cores) {
            stream.rollback_transaction();
            return;
        }

        size_t shadow_count = stream.size();

        uint32_t max_reg_id = 0;
        for (size_t i = 0; i < shadow_count; ++i) {
            const ir::IRCore& c = shadow_cores[i];
            auto bump = [&](uint32_t r) {
                if (r != ir::INVALID_SLOT && r > max_reg_id) {
                    max_reg_id = r;
                }
            };
            bump(c.result_id);
            bump(c.op_a);
            bump(c.op_b);
            bump(c.op_c);
        }
        const size_t reg_sz = (max_reg_id != ir::INVALID_SLOT)
            ? (static_cast<size_t>(max_reg_id) + 1u)
            : 1u;

        size_t et_sz = 4u;
        {
            const size_t needed = (shadow_count > 0u) ? (shadow_count * 2u) : 4u;
            while (et_sz < needed) {
                et_sz <<= 1;
            }
        }

        dgtl::core::ThreadLocalArena scratch;
        scratch.init(reg_sz * (sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint8_t))
                     + et_sz * (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t))
                     + 512u + 64u * 8u);

        uint32_t* remap_table = scratch.allocate<uint32_t>(reg_sz);
        uint64_t* const_tab = scratch.allocate<uint64_t>(reg_sz);
        uint8_t* has_const = scratch.allocate<uint8_t>(reg_sz);
        uint64_t* et_keys = scratch.allocate<uint64_t>(et_sz);
        uint32_t* et_vals = scratch.allocate<uint32_t>(et_sz);
        uint8_t* et_occ = scratch.allocate<uint8_t>(et_sz);

        if (!remap_table || !const_tab || !has_const || !et_keys || !et_vals || !et_occ) {
            stream.rollback_transaction();
            return;
        }

        std::memset(remap_table, 0xFF, reg_sz * sizeof(uint32_t));

        const size_t et_mask = et_sz - 1u;

        const auto et_find = [&](uint64_t k) noexcept -> uint32_t {
            size_t slot = static_cast<size_t>(k) & et_mask;
            while (et_occ[slot]) {
                if (et_keys[slot] == k) {
                    return et_vals[slot];
                }
                slot = (slot + 1u) & et_mask;
            }
            return ir::INVALID_SLOT;
        };

        const auto et_insert = [&](uint64_t k, uint32_t v) noexcept {
            size_t slot = static_cast<size_t>(k) & et_mask;
            while (et_occ[slot] && et_keys[slot] != k) {
                slot = (slot + 1u) & et_mask;
            }
            et_keys[slot] = k;
            et_vals[slot] = v;
            et_occ[slot] = 1u;
        };

        const auto remap_get = [&](uint32_t r) noexcept -> uint32_t {
            if (r == ir::INVALID_SLOT || r >= static_cast<uint32_t>(reg_sz)) {
                return ir::INVALID_SLOT;
            }
            return remap_table[r];
        };

        const auto remap_set = [&](uint32_t from, uint32_t to) noexcept {
            if (from != ir::INVALID_SLOT && from < static_cast<uint32_t>(reg_sz)) {
                remap_table[from] = to;
            }
        };

        const auto const_get = [&](uint32_t r, uint64_t& out) noexcept -> bool {
            if (r == ir::INVALID_SLOT || r >= static_cast<uint32_t>(reg_sz)) {
                return false;
            }
            if (!has_const[r]) {
                return false;
            }
            out = const_tab[r];
            return true;
        };

        const auto const_set = [&](uint32_t r, uint64_t val) noexcept {
            if (r != ir::INVALID_SLOT && r < static_cast<uint32_t>(reg_sz)) {
                const_tab[r] = val;
                has_const[r] = 1u;
            }
        };

        for (size_t i = 0; i < shadow_count; ++i) {
            ir::IRAtom& atom = shadow_atoms[i];
            ir::IRCore& core = shadow_cores[i];

            const bool is_imm_node = atom.opcode == ir::OpCode::LOAD_IMM || atom.opcode == ir::OpCode::IMMEDIATE;
            if (!is_imm_node) {
                {
                    uint32_t r = remap_get(core.op_a);
                    if (r != ir::INVALID_SLOT) {
                        core.op_a = r;
                    }
                }
                {
                    uint32_t r = remap_get(core.op_b);
                    if (r != ir::INVALID_SLOT) {
                        core.op_b = r;
                    }
                }
                {
                    uint32_t r = remap_get(core.op_c);
                    if (r != ir::INVALID_SLOT) {
                        core.op_c = r;
                    }
                }
            }

            if (atom.opcode == ir::OpCode::PHI_NODE) {
                bool all_const = true;
                bool found_any = false;
                uint64_t common_val = 0;
                for (uint32_t op : {core.op_a, core.op_b, core.op_c}) {
                    if (op == ir::INVALID_SLOT) {
                        continue;
                    }
                    uint64_t cv = 0;
                    if (!const_get(op, cv)) {
                        all_const = false;
                        break;
                    }
                    if (!found_any) {
                        common_val = cv;
                        found_any = true;
                    } else if (cv != common_val) {
                        all_const = false;
                        break;
                    }
                }

                if (all_const && found_any) {
                    atom.opcode = ir::OpCode::LOAD_IMM;
                    atom.flags |= static_cast<uint16_t>(ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST);
                    core.op_a = static_cast<uint32_t>((common_val >> 32) & 0xFFFFFFFFu);
                    core.op_b = static_cast<uint32_t>(common_val & 0xFFFFFFFFu);
                    core.op_c = ir::INVALID_SLOT;
                    const_set(core.result_id, common_val);
                }

                core.intent_hash = ir::IRNode::calculate_hash(
                    atom.opcode,
                    core.result_id,
                    core.op_a,
                    core.op_b,
                    core.op_c);
                continue;
            }

            if (atom.opcode >= ir::OpCode::ADD_I32 && atom.opcode <= ir::OpCode::SHL_I32) {
                uint64_t ca = 0;
                uint64_t cb = 0;
                if (const_get(core.op_a, ca) && const_get(core.op_b, cb)) {
                    const uint32_t lhs = static_cast<uint32_t>(ca);
                    const uint32_t rhs = static_cast<uint32_t>(cb);
                    bool folded = true;
                    uint32_t result_v = 0;
                    switch (atom.opcode) {
                        case ir::OpCode::ADD_I32: result_v = lhs + rhs; break;
                        case ir::OpCode::SUB_I32: result_v = lhs - rhs; break;
                        case ir::OpCode::MUL_I32: result_v = lhs * rhs; break;
                        case ir::OpCode::DIV_I32:
                            if (!rhs) {
                                folded = false;
                            } else {
                                result_v = lhs / rhs;
                            }
                            break;
                        case ir::OpCode::AND_I32: result_v = lhs & rhs; break;
                        case ir::OpCode::OR_I32: result_v = lhs | rhs; break;
                        case ir::OpCode::XOR_I32: result_v = lhs ^ rhs; break;
                        case ir::OpCode::SHL_I32: result_v = lhs << (rhs & 31u); break;
                        default: folded = false; break;
                    }
                    if (folded) {
                        atom.opcode = ir::OpCode::LOAD_IMM;
                        atom.flags |= static_cast<uint16_t>(ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST);
                        core.op_a = 0u;
                        core.op_b = result_v;
                        core.op_c = ir::INVALID_SLOT;
                    }
                }
            }

            if (atom.opcode == ir::OpCode::SELECT) {
                uint64_t cond_v = 0;
                uint64_t true_v = 0;
                uint64_t false_v = 0;
                if (const_get(core.op_a, cond_v) && const_get(core.op_b, true_v) && const_get(core.op_c, false_v)) {
                    const uint64_t pick = (cond_v != 0u) ? true_v : false_v;
                    atom.opcode = ir::OpCode::LOAD_IMM;
                    atom.flags |= static_cast<uint16_t>(ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST);
                    core.op_a = static_cast<uint32_t>((pick >> 32) & 0xFFFFFFFFu);
                    core.op_b = static_cast<uint32_t>(pick & 0xFFFFFFFFu);
                    core.op_c = ir::INVALID_SLOT;
                }
            }

            if (iface::has_trait(atom.opcode, iface::OpTrait::IsCommutative) && core.op_a > core.op_b) {
                std::swap(core.op_a, core.op_b);
            }

            core.intent_hash = ir::IRNode::calculate_hash(
                atom.opcode,
                core.result_id,
                core.op_a,
                core.op_b,
                core.op_c);

            if (atom.opcode == ir::OpCode::LOAD_IMM || atom.opcode == ir::OpCode::IMMEDIATE) {
                const uint32_t high = (core.op_a == ir::INVALID_SLOT) ? 0u : core.op_a;
                const_set(core.result_id, (static_cast<uint64_t>(high) << 32) | core.op_b);
            }

            const bool is_call_like = atom.opcode == ir::OpCode::CALL || atom.opcode == ir::OpCode::SYSCALL;
            
            // FIXED: Substituted unresolved IRTraitsStatic with precise bitwise state lookups 
            const bool is_blocking = is_call_like ||
                                     (atom.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
                                     iface::has_trait(atom.opcode, iface::OpTrait::IsTerminator) ||
                                     (atom.flags & ir::Flags::IS_VOLATILE);

            if (!is_blocking) {
                const uint64_t sem_hash = ir::IRNode::calculate_semantic_hash(
                    atom.opcode,
                    core.op_a,
                    core.op_b,
                    core.op_c,
                    atom.flags);
                const uint32_t canonical = et_find(sem_hash);
                if (canonical != ir::INVALID_SLOT) {
                    remap_set(core.result_id, canonical);
                    atom.flags |= ir::Flags::IS_DEAD;
                    continue;
                }
                et_insert(sem_hash, core.result_id);
            }
        }

        size_t write_idx = 0;
        for (size_t i = 0; i < shadow_count; ++i) {
            if (!(shadow_atoms[i].flags & ir::Flags::IS_DEAD)) {
                if (write_idx != i) {
                    shadow_atoms[write_idx] = shadow_atoms[i];
                    shadow_cores[write_idx] = shadow_cores[i];
                }
                shadow_cores[write_idx].intent_hash = ir::IRNode::calculate_hash(
                    shadow_atoms[write_idx].opcode,
                    shadow_cores[write_idx].result_id,
                    shadow_cores[write_idx].op_a,
                    shadow_cores[write_idx].op_b,
                    shadow_cores[write_idx].op_c);
                ++write_idx;
            }
        }

        stream.set_shadow_count(write_idx);
        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }
};

class DGTLInstructionFolder {
    core::DGTLFormalVerifier& formal_verifier;
    std::vector<std::optional<uint64_t>> const_table;

public:
    explicit DGTLInstructionFolder(core::DGTLFormalVerifier& fv,
                                   size_t max_ids = 1048576) noexcept
        : formal_verifier(fv)
    {
        const_table.resize(max_ids);
    }

    void fold_stream(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size == 0) [[unlikely]] return;

        std::fill(const_table.begin(), const_table.end(), std::nullopt);

        stream.begin_transaction();
        bool transaction_valid = true;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);

            if (node.opcode == ir::OpCode::NOP) {
                sync_and_finalize(stream, i, node);
                continue;
            }

            auto collapsed = try_fold_node(node);
            auto simpler = (!collapsed)
                ? formal_verifier.find_minimal_equivalent(node)
                : std::nullopt;

            if (collapsed || simpler) {
                ir::IRAtom& s_atom = stream.get_shadow_atom(i);
                ir::IRCore& s_core = stream.get_shadow_core(i);

                if (collapsed) {
                    s_atom.opcode = ir::OpCode::LOAD_IMM;
                    s_atom.flags  = ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST;

                    s_core.result_id = node.result_id;
                    s_core.op_a = static_cast<uint32_t>(collapsed->low);
                    s_core.op_b = static_cast<uint32_t>(collapsed->high);
                    s_core.op_c = ir::INVALID_SLOT;

                    if (node.result_id < const_table.size()) {
                        const_table[node.result_id] = static_cast<uint64_t>(collapsed->low);
                    }
                } else {
                    s_atom.opcode = simpler->opcode;
                    s_atom.flags  = simpler->flags | ir::Flags::IS_MACRO_FUSED;

                    s_core.result_id = node.result_id;
                    s_core.op_a = simpler->op_a;
                    s_core.op_b = simpler->op_b;
                    s_core.op_c = simpler->op_c;
                }

                std::atomic_signal_fence(std::memory_order_release);
                finalize_shadow_node(stream, i);
                if (s_atom.opcode == ir::OpCode::UNDEF) {
                    transaction_valid = false;
                }
            } else {
                propagate_const(node);
                sync_and_finalize(stream, i, node);
            }
        }

        if (transaction_valid && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:
    void propagate_const(const ir::IRNode& node) noexcept {
        if (node.opcode == ir::OpCode::LOAD_IMM) {
            if (node.result_id < const_table.size()) {
                const_table[node.result_id] = static_cast<uint64_t>(node.op_a);
            }
        }
    }

    static void sync_and_finalize(ir::IRStream& stream, uint32_t i, const ir::IRNode& node) noexcept {
        ir::IRAtom& s_atom = stream.get_shadow_atom(i);
        ir::IRCore& s_core = stream.get_shadow_core(i);

        s_atom.opcode = node.opcode;
        s_atom.flags  = node.flags;

        s_core.result_id = node.result_id;
        s_core.op_a = node.op_a;
        s_core.op_b = node.op_b;
        s_core.op_c = node.op_c;

        finalize_shadow_node(stream, i);
    }

    static void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        core.intent_hash = ir::IRNode::calculate_hash(
            atom.opcode,
            core.result_id,
            core.op_a,
            core.op_b,
            core.op_c
        );
    }

    std::optional<dgtl::core::ConstantResult>
    try_fold_node(const ir::IRNode& node) noexcept {
        auto get_const = [&](uint32_t id) -> std::optional<uint64_t> {
            if (id < const_table.size()) return const_table[id];
            return std::nullopt;
        };

        auto a = get_const(node.op_a);
        auto b = get_const(node.op_b);

        switch (node.opcode) {
            case ir::OpCode::ADD_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a + *b), 0 };
                break;
            case ir::OpCode::MUL_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a * *b), 0 };
                break;
            case ir::OpCode::SUB_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a - *b), 0 };
                if (node.op_a == node.op_b && !node.is_volatile())
                    return dgtl::core::ConstantResult{0, 0};
                break;
            case ir::OpCode::AND_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a & *b), 0 };
                break;
            case ir::OpCode::OR_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a | *b), 0 };
                break;
            case ir::OpCode::XOR_I32:
                if (a && b)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a ^ *b), 0 };
                break;
            case ir::OpCode::DIV_I32:
                if (a && b && *b != 0)
                    return dgtl::core::ConstantResult{ static_cast<uint32_t>(*a / *b), 0 };
                break;
            case ir::OpCode::SELECT: {
                auto c = get_const(node.op_c);
                if (a && b && c) {
                    return dgtl::core::ConstantResult{
                        static_cast<uint32_t>((*a != 0u) ? *b : *c),
                        0
                    };
                }
                break;
            }
            default:
                return formal_verifier.solve_static_math(node);
        }

        return std::nullopt;
    }
};

class DGTLInstructionFusionSynth : public pipeline::DGTLOptimizationPass {
    core::DGTLFormalVerifier& formal_verifier;

public:
    explicit DGTLInstructionFusionSynth(core::DGTLFormalVerifier& fv) noexcept
        : formal_verifier(fv) {}

    void execute(ir::IRStream& stream) override {
        synthesize(stream);
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "Instruction Fusion Synthesis v1.0.0";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostSpecialization;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

    void synthesize(ir::IRStream& stream) noexcept {
        const size_t stream_size = stream.size();
        if (stream_size < 2) [[unlikely]] return;

        stream.begin_transaction();

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            sync_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size - 1); ++i) {
            ir::IRNode current = stream.get_node_readonly(i);

            if (current.opcode == ir::OpCode::NOP) continue;
            if (current.flags & ir::Flags::HAS_SIDE_EFFECTS) continue;

            if (is_mul(current.opcode)) {
                if (auto fusion = try_synthesize_fma(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx;
                    continue;
                }
            }

            if (current.opcode == ir::OpCode::SHL_I32) {
                if (auto fusion = try_synthesize_shifted_logic(stream, i)) {
                    apply_fusion(stream, i, *fusion);
                    i = fusion->consumer_idx;
                    continue;
                }
            }
        }

        if (stream.validate_shadow_integrity()) {
            std::atomic_signal_fence(std::memory_order_release);
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:
    struct FusionMatch {
        uint32_t consumer_idx{0};
        ir::OpCode fused_op{ir::OpCode::NOP};
        uint32_t op_a{0}, op_b{0}, op_c{0};
        uint32_t result_id{0};
    };

    bool has_multiple_users(const ir::IRStream& s, uint32_t res_id) const noexcept {
        uint32_t count = 0;
        const size_t sz = s.size();

        for (size_t i = 0; i < sz; ++i) {
            const auto n = s.get_node_readonly(static_cast<uint32_t>(i));
            if (n.op_a == res_id || n.op_b == res_id || n.op_c == res_id) {
                if (++count > 1) return true;
            }
        }
        return false;
    }

    bool is_barrier(const ir::IRNode& n) const noexcept {
        return (n.flags & ir::Flags::HAS_SIDE_EFFECTS) ||
               iface::has_trait(n.opcode, iface::OpTrait::IsTerminator);
    }

    static bool is_mul(ir::OpCode op) noexcept {
        return op == ir::OpCode::MUL_I32 || op == ir::OpCode::MUL_F32;
    }

    void sync_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        const ir::IRNode src = s.get_node_readonly(idx);

        ir::IRAtom& atom = s.get_shadow_atom(idx);
        ir::IRCore& core = s.get_shadow_core(idx);

        atom.opcode = src.opcode;
        atom.flags  = src.flags;

        core.result_id   = src.result_id;
        core.op_a        = src.op_a;
        core.op_b        = src.op_b;
        core.op_c        = src.op_c;
        core.intent_hash = src.intent_hash;
    }

    std::optional<FusionMatch> try_synthesize_fma(const ir::IRStream& s, uint32_t p_idx) noexcept {
        const ir::IRNode producer = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 8, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {
            const ir::IRNode consumer = s.get_node_readonly(j);

            if (is_barrier(consumer)) break;

            if (consumer.opcode == ir::OpCode::ADD_I32 ||
                consumer.opcode == ir::OpCode::ADD_F32) {

                if (consumer.op_a == producer.result_id ||
                    consumer.op_b == producer.result_id) {

                    if (has_multiple_users(s, producer.result_id)) return std::nullopt;

                    return FusionMatch{
                        j,
                        (producer.opcode == ir::OpCode::MUL_F32)
                            ? ir::OpCode::FMA_F32
                            : ir::OpCode::ADD_I32,
                        producer.op_a,
                        producer.op_b,
                        (consumer.op_a == producer.result_id)
                            ? consumer.op_b
                            : consumer.op_a,
                        consumer.result_id
                    };
                }
            }
        }

        return std::nullopt;
    }

    std::optional<FusionMatch> try_synthesize_shifted_logic(const ir::IRStream& s, uint32_t p_idx) noexcept {
        const ir::IRNode p = s.get_node_readonly(p_idx);
        const uint32_t lookahead = std::min(p_idx + 4, static_cast<uint32_t>(s.size()));

        for (uint32_t j = p_idx + 1; j < lookahead; ++j) {
            const ir::IRNode c = s.get_node_readonly(j);

            if (is_barrier(c)) break;

            if (c.opcode == ir::OpCode::XOR_I32 ||
                c.opcode == ir::OpCode::OR_I32) {

                if (c.op_a == p.result_id || c.op_b == p.result_id) {
                    if (has_multiple_users(s, p.result_id)) return std::nullopt;

                    return FusionMatch{
                        j,
                        ir::OpCode::BIT_FIELD_INSERT,
                        p.op_a,
                        p.op_b,
                        (c.op_a == p.result_id) ? c.op_b : c.op_a,
                        c.result_id
                    };
                }
            }
        }

        return std::nullopt;
    }

    void apply_fusion(ir::IRStream& s, uint32_t p_idx, const FusionMatch& m) noexcept {
        ir::IRAtom& p_atom = s.get_shadow_atom(p_idx);
        ir::IRCore& p_core = s.get_shadow_core(p_idx);

        p_atom.opcode    = ir::OpCode::NOP;
        p_atom.flags     = ir::Flags::IS_DEAD;
        p_core.result_id = ir::INVALID_SLOT;
        p_core.op_a      = 0;
        p_core.op_b      = 0;
        p_core.op_c      = 0;

        p_core.intent_hash = ir::IRNode::calculate_hash(
            ir::OpCode::NOP,
            p_core.result_id,
            p_core.op_a,
            p_core.op_b,
            p_core.op_c
        );

        ir::IRAtom& c_atom = s.get_shadow_atom(m.consumer_idx);
        ir::IRCore& c_core = s.get_shadow_core(m.consumer_idx);

        c_atom.opcode  = m.fused_op;
        c_atom.flags  |= (ir::Flags::IS_MACRO_FUSED | ir::Flags::IS_OPTIMIZED);

        c_core.op_a      = m.op_a;
        c_core.op_b      = m.op_b;
        c_core.op_c      = m.op_c;
        c_core.result_id = m.result_id;

        c_core.intent_hash = ir::IRNode::calculate_hash(
            c_atom.opcode,
            c_core.result_id,
            c_core.op_a,
            c_core.op_b,
            c_core.op_c
        );
    }
};

struct IR_Op {
    uint32_t original_stream_idx;
    ir::OpCode opcode;
    std::vector<uint32_t> reads;
    std::vector<uint32_t> writes;
    bool has_side_effects;

    IR_Op(uint32_t idx, const ir::IRNode& node)
        : original_stream_idx(idx), opcode(node.opcode) {
        has_side_effects = (node.flags & ir::Flags::HAS_SIDE_EFFECTS);

        if (node.op_a != ir::INVALID_SLOT) reads.push_back(node.op_a);
        if (node.op_b != ir::INVALID_SLOT) reads.push_back(node.op_b);
        if (node.op_c != ir::INVALID_SLOT) reads.push_back(node.op_c);

        if (node.result_id != ir::INVALID_SLOT) {
            writes.push_back(node.result_id);
        }
    }
};

class Instruction_Reordering_Pass : public pipeline::DGTLOptimizationPass {
protected:
    analysis::DGTLMemorySmasher& m_smasher;
    analysis::DGTLAICostModel* m_ai_model;

public:
    explicit Instruction_Reordering_Pass(
        analysis::DGTLMemorySmasher& smasher,
        analysis::DGTLAICostModel* ai = nullptr) noexcept
        : m_smasher(smasher), m_ai_model(ai) {}

    void execute(ir::IRStream& stream) override {
        const size_t stream_size = stream.size();
        if (stream_size < 2) return;

        std::vector<IR_Op> block;
        block.reserve(stream_size);
        for (uint32_t i = 0; i < (uint32_t)stream_size; ++i) {
            block.emplace_back(i, stream.get_node_readonly(i));
        }

        run_optimization(block, stream);

        stream.begin_transaction();
        for (uint32_t i = 0; i < (uint32_t)block.size(); ++i) {
            const ir::IRNode& source_node = stream.get_node_readonly(block[i].original_stream_idx);
            apply_to_shadow(stream, i, source_node);
        }

        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "InstructionReordering";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return pipeline::PassAffinity::High;
    }

    [[nodiscard]] pipeline::PassAffinity memory_affinity() const noexcept override {
        return pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] pipeline::PassAffinity vector_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

    [[nodiscard]] pipeline::PassAffinity speculation_affinity() const noexcept override {
        return pipeline::PassAffinity::Low;
    }

protected:
    void run_optimization(std::vector<IR_Op>& block, const ir::IRStream& stream) {
        bool changed = true;
        size_t limit = block.size() * 2;

        while (changed && limit--) {
            changed = false;
            for (size_t i = 0; i + 1 < block.size(); ++i) {
                if (should_swap(block[i], block[i + 1], stream)) {
                    std::swap(block[i], block[i + 1]);
                    changed = true;
                }
            }
        }
    }

    bool should_swap(const IR_Op& A, const IR_Op& B, const ir::IRStream& stream) {
        const bool b_reads_a_writes = std::any_of(B.reads.begin(), B.reads.end(), [&](auto r) {
            return std::any_of(A.writes.begin(), A.writes.end(), [r](auto w) { return r == w; });
        });
        if (b_reads_a_writes) return false;

        const bool b_writes_a_reads = std::any_of(B.writes.begin(), B.writes.end(), [&](auto w) {
            return std::any_of(A.reads.begin(), A.reads.end(), [w](auto r) { return w == r; });
        });
        if (b_writes_a_reads) return false;

        const bool b_writes_a_writes = std::any_of(B.writes.begin(), B.writes.end(), [&](auto w1) {
            return std::any_of(A.writes.begin(), A.writes.end(), [w1](auto w2) { return w1 == w2; });
        });
        if (b_writes_a_writes) return false;

        if (A.has_side_effects || B.has_side_effects) return false;

        if (!m_smasher.can_reorder(stream, A.original_stream_idx, B.original_stream_idx)) {
            return false;
        }

        float cost_a = get_instruction_cost(A, stream);
        float cost_b = get_instruction_cost(B, stream);
        return (cost_b > cost_a);
    }

    float get_instruction_cost(const IR_Op& op, const ir::IRStream& stream) {
        if (m_ai_model) {
            const ir::IRNode node = stream.get_node_readonly(op.original_stream_idx);
            return m_ai_model->predict_node_cost(node);
        }
        return iface::has_trait(op.opcode, iface::OpTrait::IsMemoryRead) ? 10.0f : 1.0f;
    }

    void apply_to_shadow(ir::IRStream& s, uint32_t shadow_idx, const ir::IRNode& node) {
        ir::IRAtom& s_atom = s.get_shadow_atom(shadow_idx);
        ir::IRCore& s_core = s.get_shadow_core(shadow_idx);

        s_atom.opcode = node.opcode;
        s_atom.flags  = node.flags;

        s_core.result_id   = node.result_id;
        s_core.op_a        = node.op_a;
        s_core.op_b        = node.op_b;
        s_core.op_c        = node.op_c;
        s_core.intent_hash = node.intent_hash;
    }
};

} // namespace dgtl::pass::transform