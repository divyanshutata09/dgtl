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
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 4: IR STREAM CORE IMPLEMENTATION, iterator access, and hot path operations
 * ., transantion gaurd: it has three cpp files, IR_Stream_Core 
 * (lifecycle, allocation), 
 * IR_Stream_Operations (emit, access), and 
 * IR_Stream_Optimizations (passes)
 * * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"     
#include "dgtl/ir/base/IR_Stream.hpp" 
#include "dgtl/core/Arena.hpp" 

#include <cstring>
#include <algorithm>

namespace dgtl::ir {

namespace {
/**
 * @brief Commutativity check for GVN normalization.
 * Ensures (a + b) and (b + a) produce identical semantic hashes.
 */
constexpr bool is_commutative_opcode(const OpCode op) noexcept {
    return op == OpCode::ADD_I32 ||
           op == OpCode::MUL_I32 ||
           op == OpCode::AND_I32 ||
           op == OpCode::OR_I32 ||
           op == OpCode::XOR_I32 ||
           op == OpCode::ADD_F32 ||
           op == OpCode::MUL_F32;
}
} // namespace

// --- Optimization Passes (From Files 4, 5) ---

void IRStream::push_shadow_fold(const IRNode& node) noexcept {
    IRNode folded = node.clone();
    folded.fold_if_possible();
    if (!in_transaction.load(std::memory_order_acquire)) begin_transaction();
    push_to_shadow(folded);
}

/**
 * @brief Dead Code Elimination (DCE)
 * Performs a reverse-pass liveness analysis. Optimized for dense ID spaces.
 */
void IRStream::perform_dead_code_elimination() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) {
        rollback_transaction();
        return;
    }

    // Step 1: Scan for Max ID to determine liveness tracking strategy (Dense vs Sparse)
    uint32_t max_reg_id = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        const IRCore& core = shadow_cores[i];
        if (core.result_id != INVALID_SLOT && core.result_id > max_reg_id) max_reg_id = core.result_id;
        if (core.op_a != INVALID_SLOT && core.op_a > max_reg_id) max_reg_id = core.op_a;
        if (core.op_b != INVALID_SLOT && core.op_b > max_reg_id) max_reg_id = core.op_b;
        if (core.op_c != INVALID_SLOT && core.op_c > max_reg_id) max_reg_id = core.op_c;
    }

    constexpr uint32_t kDenseIdLimit = 8u * 1024u * 1024u;
    const bool use_dense_live = (max_reg_id != INVALID_SLOT) && (max_reg_id <= kDenseIdLimit);

    // --- Pass-local scratch arena: replaces std::vector + std::unordered_set ---
    dgtl::core::ThreadLocalArena scratch_dce;
    const size_t dense_size = use_dense_live ? (static_cast<size_t>(max_reg_id) + 1u) : 0u;

    // Sparse open-addressing hash set: power-of-2 >= shadow_count*3 (load ~33%)
    size_t ss_size = 4u;
    if (!use_dense_live) {
        const size_t needed = (shadow_count > 0u) ? (shadow_count * 3u) : 4u;
        while (ss_size < needed) ss_size <<= 1;
    }

    const size_t scratch_dce_bytes = dense_size
                                   + ss_size * sizeof(uint32_t)
                                   + 512u;  // alignment headroom
    scratch_dce.init(scratch_dce_bytes + 64u * 8u);

    uint8_t*  dense_live = use_dense_live  ? scratch_dce.allocate<uint8_t>(dense_size)   : nullptr;
    uint32_t* ss_keys    = !use_dense_live ? scratch_dce.allocate<uint32_t>(ss_size) : nullptr;

    if ( use_dense_live && !dense_live) { rollback_transaction(); return; }
    if (!use_dense_live && !ss_keys)    { rollback_transaction(); return; }

    // dense_live is zero-initialised by VirtualAlloc/mmap (not live = 0). Good as-is.
    // ss_keys needs INVALID_SLOT (0xFFFFFFFF) as the empty-slot sentinel.
    if (ss_keys) std::memset(ss_keys, 0xFF, ss_size * sizeof(uint32_t));

    const size_t ss_mask = ss_size - 1u;
    // Tombstone value for erase: 0xFFFFFFFE (never a valid reg_id; INVALID_SLOT guards insertion).
    constexpr uint32_t SS_EMPTY   = 0xFFFFFFFFu;
    constexpr uint32_t SS_DELETED = 0xFFFFFFFEu;

    const auto ss_contains = [&](uint32_t k) noexcept -> bool {
        size_t slot = static_cast<size_t>(k) & ss_mask;
        while (ss_keys[slot] != SS_EMPTY) {
            if (ss_keys[slot] == k) return true;
            slot = (slot + 1u) & ss_mask;
        }
        return false;
    };
    const auto ss_insert = [&](uint32_t k) noexcept {
        size_t slot = static_cast<size_t>(k) & ss_mask;
        while (ss_keys[slot] != SS_EMPTY && ss_keys[slot] != SS_DELETED && ss_keys[slot] != k)
            slot = (slot + 1u) & ss_mask;
        ss_keys[slot] = k;
    };
    const auto ss_erase = [&](uint32_t k) noexcept {
        size_t slot = static_cast<size_t>(k) & ss_mask;
        while (ss_keys[slot] != SS_EMPTY) {
            if (ss_keys[slot] == k) { ss_keys[slot] = SS_DELETED; return; }
            slot = (slot + 1u) & ss_mask;
        }
    };

    const auto is_live = [&](uint32_t reg) noexcept -> bool {
        if (reg == INVALID_SLOT) return false;
        if (use_dense_live) return reg < dense_size && dense_live[reg] != 0u;
        return ss_contains(reg);
    };
    const auto set_live = [&](uint32_t reg) noexcept {
        if (reg == INVALID_SLOT) return;
        if (use_dense_live) { if (reg < dense_size) dense_live[reg] = 1u; }
        else ss_insert(reg);
    };
    const auto clear_live = [&](uint32_t reg) noexcept {
        if (reg == INVALID_SLOT) return;
        if (use_dense_live) { if (reg < dense_size) dense_live[reg] = 0u; }
        else ss_erase(reg);
    };

    // Step 2: Backward Pass for Liveness Propagation
    for (int i = static_cast<int>(shadow_count) - 1; i >= 0; --i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];

        const bool is_terminator = IRTraits::is_terminator(atom.opcode);
        const bool is_essential = IRTraits::has_side_effects(atom.opcode, atom.flags) ||
                                  is_terminator ||
                                  is_live(core.result_id);

        if (!is_essential) {
            atom.flags |= Flags::IS_DEAD;
        } else {
            clear_live(core.result_id);
            // LOAD_IMM/IMMEDIATE operands are immediate values, not register references.
            const bool is_imm_node = atom.opcode == OpCode::LOAD_IMM ||
                                     atom.opcode == OpCode::IMMEDIATE;
            if (!is_imm_node) {
                set_live(core.op_a);
                set_live(core.op_b);
                set_live(core.op_c);
            }
        }
    }

    // Step 3: Compaction and Hash Refresh
    size_t write_idx = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (!(shadow_atoms[i].flags & Flags::IS_DEAD)) {
            if (write_idx != i) {
                shadow_atoms[write_idx] = shadow_atoms[i];
                shadow_cores[write_idx] = shadow_cores[i];
            }
            shadow_cores[write_idx].intent_hash = IRNode::calculate_hash(
                shadow_atoms[write_idx].opcode,
                shadow_cores[write_idx].result_id,
                shadow_cores[write_idx].op_a,
                shadow_cores[write_idx].op_b,
                shadow_cores[write_idx].op_c
            );
            write_idx++;
        }
    }
    shadow_count = write_idx;
    
    if (validate_shadow_integrity()) {
        commit_transaction();
    } else {
        rollback_transaction();
    }
}

/**
 * @brief Global Value Numbering (GVN)
 * Combines CSE (Common Subexpression Elimination) with constant propagation.
 * All hot tables are arena-backed flat arrays â€” zero heap/hash-map overhead.
 */
void IRStream::perform_gvn() noexcept {
    begin_transaction();
    if (!shadow_atoms || !shadow_cores) {
        rollback_transaction();
        return;
    }

    // Pre-scan: find max_reg_id to size dense reg-indexed tables.
    uint32_t max_reg_id = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        const IRCore& c = shadow_cores[i];
        if (c.result_id != INVALID_SLOT && c.result_id > max_reg_id) max_reg_id = c.result_id;
        if (c.op_a     != INVALID_SLOT && c.op_a     > max_reg_id) max_reg_id = c.op_a;
        if (c.op_b     != INVALID_SLOT && c.op_b     > max_reg_id) max_reg_id = c.op_b;
        if (c.op_c     != INVALID_SLOT && c.op_c     > max_reg_id) max_reg_id = c.op_c;
    }
    const size_t reg_sz = (max_reg_id != INVALID_SLOT) ? (static_cast<size_t>(max_reg_id) + 1u) : 1u;

    // expression_map: open-addressing flat table (uint64_t key â†’ uint32_t result_id).
    // Size = next power-of-2 >= shadow_count*2 (load factor â‰¤ 50 %).
    size_t et_size = 4u;
    {
        const size_t needed = (shadow_count > 0u) ? (shadow_count * 2u) : 4u;
        while (et_size < needed) et_size <<= 1;
    }

    // --- Pass-local scratch arena (replaces all three unordered_maps) ---
    dgtl::core::ThreadLocalArena scratch_gvn;
    const size_t scratch_gvn_bytes =
        reg_sz  * sizeof(uint32_t)   // remap_table  (regâ†’canonical reg)
      + reg_sz  * sizeof(uint64_t)   // const_tab    (regâ†’immediate value)
      + reg_sz  * sizeof(uint8_t)    // has_const    (presence flag)
      + et_size * sizeof(uint64_t)   // et_keys      (semantic hash)
      + et_size * sizeof(uint32_t)   // et_vals      (result_id)
      + et_size * sizeof(uint8_t)    // et_occ       (occupied flag)
      + 512u;                        // alignment headroom
    scratch_gvn.init(scratch_gvn_bytes + 64u * 8u);

    uint32_t* remap_table = scratch_gvn.allocate<uint32_t>(reg_sz);
    uint64_t* const_tab   = scratch_gvn.allocate<uint64_t>(reg_sz);
    uint8_t*  has_const   = scratch_gvn.allocate<uint8_t> (reg_sz);
    uint64_t* et_keys     = scratch_gvn.allocate<uint64_t>(et_size);
    uint32_t* et_vals     = scratch_gvn.allocate<uint32_t>(et_size);
    uint8_t*  et_occ      = scratch_gvn.allocate<uint8_t> (et_size);

    if (!remap_table || !const_tab || !has_const || !et_keys || !et_vals || !et_occ) {
        rollback_transaction();
        return;
    }

    // remap_table sentinel = INVALID_SLOT (0xFFFFFFFF); everything else is zero-init by OS.
    std::memset(remap_table, 0xFF, reg_sz * sizeof(uint32_t));
    // has_const, et_occ already zero (no entry present). const_tab, et_keys, et_vals: irrelevant until set.

    const size_t et_mask = et_size - 1u;

    // expression_map helpers (open-addressing, et_occ as guard).
    const auto et_find = [&](uint64_t k) noexcept -> uint32_t {
        size_t slot = static_cast<size_t>(k) & et_mask;
        while (et_occ[slot]) {
            if (et_keys[slot] == k) return et_vals[slot];
            slot = (slot + 1u) & et_mask;
        }
        return INVALID_SLOT;
    };
    const auto et_insert = [&](uint64_t k, uint32_t v) noexcept {
        size_t slot = static_cast<size_t>(k) & et_mask;
        while (et_occ[slot] && et_keys[slot] != k)
            slot = (slot + 1u) & et_mask;
        et_keys[slot] = k; et_vals[slot] = v; et_occ[slot] = 1u;
    };

    // register_remap helpers (dense array indexed by reg_id, INVALID_SLOT = no mapping).
    const auto remap_get = [&](uint32_t reg) noexcept -> uint32_t {
        if (reg == INVALID_SLOT || reg >= static_cast<uint32_t>(reg_sz)) return INVALID_SLOT;
        return remap_table[reg];
    };
    const auto remap_set = [&](uint32_t from, uint32_t to) noexcept {
        if (from == INVALID_SLOT || from >= static_cast<uint32_t>(reg_sz)) return;
        remap_table[from] = to;
    };

    // const_values helpers (dense array + presence flag).
    const auto const_get = [&](uint32_t reg, uint64_t& out) noexcept -> bool {
        if (reg == INVALID_SLOT || reg >= static_cast<uint32_t>(reg_sz)) return false;
        if (!has_const[reg]) return false;
        out = const_tab[reg]; return true;
    };
    const auto const_set = [&](uint32_t reg, uint64_t val) noexcept {
        if (reg == INVALID_SLOT || reg >= static_cast<uint32_t>(reg_sz)) return;
        const_tab[reg] = val; has_const[reg] = 1u;
    };

    for (size_t i = 0; i < shadow_count; ++i) {
        IRAtom& atom = shadow_atoms[i];
        IRCore& core = shadow_cores[i];

        // Register Remapping (CSE Propagation)
        const bool is_imm_node = atom.opcode == OpCode::LOAD_IMM ||
                                 atom.opcode == OpCode::IMMEDIATE;
        if (!is_imm_node) {
            { uint32_t r = remap_get(core.op_a); if (r != INVALID_SLOT) core.op_a = r; }
            { uint32_t r = remap_get(core.op_b); if (r != INVALID_SLOT) core.op_b = r; }
            { uint32_t r = remap_get(core.op_c); if (r != INVALID_SLOT) core.op_c = r; }
        }

        // PHI Optimization: Fold redundant merges
        if (atom.opcode == OpCode::PHI_NODE) {
            bool all_const = true;
            bool found_any = false;
            uint64_t common_val = 0;

            const uint32_t phi_ops[3] = { core.op_a, core.op_b, core.op_c };
            for (uint32_t op : phi_ops) {
                if (op == INVALID_SLOT) continue;
                uint64_t cv = 0;
                if (!const_get(op, cv)) { all_const = false; break; }
                if (!found_any) { common_val = cv; found_any = true; }
                else if (cv != common_val) { all_const = false; break; }
            }

            if (all_const && found_any) {
                atom.opcode = OpCode::LOAD_IMM;
                atom.flags = static_cast<uint16_t>(atom.flags | Flags::IS_CONST | Flags::IS_SPEC_CONST);
                core.op_a = static_cast<uint32_t>((common_val >> 32) & 0xFFFFFFFFu);
                core.op_b = static_cast<uint32_t>(common_val & 0xFFFFFFFFu);
                core.op_c = INVALID_SLOT;
                const_set(core.result_id, common_val);
            }
            core.intent_hash = IRNode::calculate_hash(atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c);
            continue;
        }

        // Arithmetic Constant Folding
        if (atom.opcode >= OpCode::ADD_I32 && atom.opcode <= OpCode::SHL_I32) {
            uint64_t ca = 0, cb = 0;
            if (const_get(core.op_a, ca) && const_get(core.op_b, cb)) {
                const uint32_t lhs = static_cast<uint32_t>(ca);
                const uint32_t rhs = static_cast<uint32_t>(cb);
                bool folded = true;
                uint32_t folded_val = 0;

                switch (atom.opcode) {
                    case OpCode::ADD_I32: folded_val = lhs + rhs; break;
                    case OpCode::SUB_I32: folded_val = lhs - rhs; break;
                    case OpCode::MUL_I32: folded_val = lhs * rhs; break;
                    case OpCode::DIV_I32:
                        if (rhs == 0) folded = false;
                        else folded_val = lhs / rhs;
                        break;
                    case OpCode::AND_I32: folded_val = lhs & rhs; break;
                    case OpCode::OR_I32: folded_val = lhs | rhs; break;
                    case OpCode::XOR_I32: folded_val = lhs ^ rhs; break;
                    case OpCode::SHL_I32: folded_val = lhs << (rhs & 31U); break;
                    default: folded = false; break;
                }

                if (folded) {
                    atom.opcode = OpCode::LOAD_IMM;
                    atom.flags |= (Flags::IS_CONST | Flags::IS_SPEC_CONST);
                    core.op_a = 0;
                    core.op_b = folded_val;
                    core.op_c = INVALID_SLOT;
                }
            }
        }

        // Canonicalization for Commutative Ops
        if (is_commutative_opcode(atom.opcode) && core.op_a > core.op_b) {
            std::swap(core.op_a, core.op_b);
        }

        core.intent_hash = IRNode::calculate_hash(atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c);

        // Constant Tracking for future nodes
        if (atom.opcode == OpCode::LOAD_IMM || atom.opcode == OpCode::IMMEDIATE) {
            const uint32_t high = (core.op_a == INVALID_SLOT) ? 0u : core.op_a;
            const uint64_t imm = (static_cast<uint64_t>(high) << 32) | core.op_b;
            const_set(core.result_id, imm);
        }

        // Semantic Hashing & CSE Elimination
        const bool is_blocking = IRTraits::has_side_effects(atom.opcode, atom.flags) ||
                                 IRTraits::is_terminator(atom.opcode) ||
                                 (atom.flags & Flags::IS_VOLATILE);

        if (!is_blocking) {
            const uint64_t semantic_hash = IRNode::calculate_semantic_hash(
                atom.opcode,
                core.op_a,
                core.op_b,
                core.op_c,
                atom.flags
            );

            const uint32_t canonical = et_find(semantic_hash);
            if (canonical != INVALID_SLOT) {
                remap_set(core.result_id, canonical);
                atom.flags |= Flags::IS_DEAD;
                continue;
            }
            et_insert(semantic_hash, core.result_id);
        }
    }

    // Compactor sweep
    size_t write_idx = 0;
    for (size_t i = 0; i < shadow_count; ++i) {
        if (!(shadow_atoms[i].flags & Flags::IS_DEAD)) {
            if (write_idx != i) {
                shadow_atoms[write_idx] = shadow_atoms[i];
                shadow_cores[write_idx] = shadow_cores[i];
            }
            shadow_cores[write_idx].intent_hash = IRNode::calculate_hash(
                shadow_atoms[write_idx].opcode,
                shadow_cores[write_idx].result_id,
                shadow_cores[write_idx].op_a,
                shadow_cores[write_idx].op_b,
                shadow_cores[write_idx].op_c
            );
            write_idx++;
        }
    }
    shadow_count = write_idx;
    if (validate_shadow_integrity()) {
        commit_transaction();
    } else {
        rollback_transaction();
    }
}

// --- Metadata Accessors ---

void IRStream::set_shadow_count(size_t val) noexcept { shadow_count = val; }
void IRStream::update_shadow_size(size_t s) noexcept { shadow_count = s; }
uint32_t IRStream::intent_crc() const noexcept { return static_cast<uint32_t>(generate_logic_hash()); }

uint32_t IRStream::get_max_id() const noexcept {
    uint32_t max_id = 0;
    bool trans = in_transaction.load(std::memory_order_acquire);
    const auto* c_ptr = trans ? shadow_cores : cores;
    const size_t current_count = trans ? shadow_count : count;

    if (!c_ptr || current_count == 0) return 0;

    for (size_t i = 0; i < current_count; ++i) {
        uint32_t rid = c_ptr[i].result_id;
        if (rid != 0xFFFFFFFF && rid < 0x80000000U) {
            if (rid > max_id) max_id = rid;
        }
    }
    return max_id;
}

/**
 * @brief Evolution hook for IR self-specialization.
 */
void IRStream::evolve_node(IRNode* node) {
    if (!node) return;

    // Pattern-based mutation for runtime efficiency
    if (node->opcode == OpCode::ADD_I32 && node->op_a == node->op_b) {
        // Transform a + a -> a << 1 for faster hardware execution
        node->opcode = OpCode::SHL_I32;
        node->op_b = 1;
        node->refresh_intent_hash();
    }
}

} // namespace dgtl::ir
