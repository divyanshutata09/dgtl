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
#include <unordered_set>
#include <bitset>
#include <optional>
#include <atomic>
#include <immintrin.h>
#include <mutex>
#include <stdexcept>
#include <cstddef>
#include <span>
#include <utility>

#ifdef CONST
#undef CONST
#endif

export module dgtl.ir.pass.transform_scalar_core;

import dgtl.arena;
import dgtl.ir;
import dgtl.op_traits;
import dgtl.ir.pipeline.pass_manager_config;
import dgtl.ir.analysis.cost_model;
import dgtl.ir_formal_verifier;
import dgtl.ir.analysis.liveness_memory_dependency_unified;

export namespace dgtl::ir::pass {

namespace analysis = dgtl::analysis;
namespace core = dgtl::core;
namespace pipeline = dgtl::pipeline;
namespace ir = dgtl::ir;
// interfaces resolved via dgtl::ir::gen

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

// ============================================================================
// Shared helpers for the scalar-core passes.
//
// The HIR stream is NOT in SSA form: frontends (e.g. MYZON `let mut`) re-assign
// the same result slot many times, and control flow is expressed with
// LABEL / GOTO / BRANCH / BRANCH_COND nodes. Every pass below therefore:
//   * treats a slot as a mutable variable (never as a single SSA definition);
//   * treats every opcode that is not on an explicit pure whitelist as opaque;
//   * never interprets LOAD_IMM / IMMEDIATE payload words as value references.
// ============================================================================
namespace scalar_core_detail {

// Node flags that make an otherwise-pure opcode observable / non-removable.
inline constexpr std::uint16_t kBarrierFlagMask = static_cast<std::uint16_t>(
    ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE | ir::Flags::IS_BRANCH |
    ir::Flags::IS_RETURN | ir::Flags::IS_GUARD | ir::Flags::MAY_DEOPT);

// Whitelist of opcodes whose only effect is writing result_id from register
// operands (or immediates). Anything not listed here is conservatively opaque:
// a DCE root and a GVN block barrier. Integer DIV/MOD are deliberately excluded
// (they may trap); memory, control flow, calls, PHIs and casts are excluded.
[[nodiscard]] inline bool is_pure_value_opcode(ir::OpCode op) noexcept {
    switch (op) {
        case ir::OpCode::REG_MOVE:
        case ir::OpCode::LOAD_IMM:
        case ir::OpCode::IMMEDIATE:
        case ir::OpCode::ADD_I8:
        case ir::OpCode::ADD_I32:
        case ir::OpCode::SUB_I32:
        case ir::OpCode::MUL_I32:
        case ir::OpCode::SHL_I32:
        case ir::OpCode::AND_I32:
        case ir::OpCode::OR_I32:
        case ir::OpCode::XOR_I32:
        case ir::OpCode::SHR_I32:
        case ir::OpCode::ADD_I64:
        case ir::OpCode::SUB_I64:
        case ir::OpCode::MUL_I64:
        case ir::OpCode::AND_I64:
        case ir::OpCode::OR_I64:
        case ir::OpCode::XOR_I64:
        case ir::OpCode::SHL_I64:
        case ir::OpCode::SHR_I64:
        case ir::OpCode::ADD_F32:
        case ir::OpCode::SUB_F32:
        case ir::OpCode::MUL_F32:
        case ir::OpCode::DIV_F32:
        case ir::OpCode::FMA_F32:
        case ir::OpCode::ADD_F64:
        case ir::OpCode::SUB_F64:
        case ir::OpCode::MUL_F64:
        case ir::OpCode::DIV_F64:
        case ir::OpCode::CMP_EQ_I32:
        case ir::OpCode::CMP_NE_I32:
        case ir::OpCode::CMP_LT_I32:
        case ir::OpCode::CMP_LE_I32:
        case ir::OpCode::CMP_GT_I32:
        case ir::OpCode::CMP_GE_I32:
        case ir::OpCode::SELECT:
        case ir::OpCode::BOOL_SELECT:
            return true;
        default:
            return false;
    }
}

// Opcodes whose op_a/op_b/op_c are immediate payload words (CONST lo/hi), not
// value references.
[[nodiscard]] inline bool is_immediate_payload_opcode(ir::OpCode op) noexcept {
    return op == ir::OpCode::LOAD_IMM || op == ir::OpCode::IMMEDIATE;
}

[[nodiscard]] inline bool is_pure_node(ir::OpCode op, std::uint16_t flags) noexcept {
    return is_pure_value_opcode(op) && (flags & kBarrierFlagMask) == 0u;
}

// Commutative pure opcodes for GVN key canonicalisation (op_a <-> op_b).
// Compares are intentionally excluded (fcmp encodes a predicate in op_c).
[[nodiscard]] inline bool is_gvn_commutative(ir::OpCode op) noexcept {
    switch (op) {
        case ir::OpCode::ADD_I8:
        case ir::OpCode::ADD_I32:
        case ir::OpCode::MUL_I32:
        case ir::OpCode::AND_I32:
        case ir::OpCode::OR_I32:
        case ir::OpCode::XOR_I32:
        case ir::OpCode::ADD_I64:
        case ir::OpCode::MUL_I64:
        case ir::OpCode::AND_I64:
        case ir::OpCode::OR_I64:
        case ir::OpCode::XOR_I64:
        case ir::OpCode::ADD_F32:
        case ir::OpCode::MUL_F32:
        case ir::OpCode::ADD_F64:
        case ir::OpCode::MUL_F64:
            return true;
        default:
            return false;
    }
}

// Snapshot of every node-index-keyed side table held by IRStream.
struct NodeSidecars {
    std::vector<std::uint32_t> call_overflow;     // CALL args[3..N-1]
    std::vector<std::uint32_t> call_abi_layout;   // packed ABI plan words
    std::vector<std::uint32_t> operand_overflow;  // variadic operands
    std::vector<std::uint32_t> result_ids;        // multi-result ids / PHI blocks
    std::uint32_t effects = 0u;                   // EffectSet

    [[nodiscard]] bool empty() const noexcept {
        return call_overflow.empty() && call_abi_layout.empty() &&
               operand_overflow.empty() && result_ids.empty() && effects == 0u;
    }
};

[[nodiscard]] inline NodeSidecars read_sidecars(const ir::IRStream& s, size_t idx) {
    NodeSidecars sc;
    sc.call_overflow    = s.get_call_overflow_args(idx);
    sc.call_abi_layout  = s.get_call_abi_layout_words(idx);
    sc.operand_overflow = s.get_operand_overflow(idx);
    sc.result_ids       = s.get_result_ids(idx);
    sc.effects          = s.get_node_effects(idx);
    return sc;
}

inline void clear_sidecars(ir::IRStream& s, size_t idx) {
    s.clear_call_overflow_args(idx);
    s.clear_call_abi_layout_words(idx);
    s.clear_operand_overflow(idx);
    s.clear_result_ids(idx);
    s.clear_node_effects(idx);
}

inline void write_sidecars(ir::IRStream& s, size_t idx, const NodeSidecars& sc) {
    if (!sc.call_overflow.empty()) {
        s.set_call_overflow_args(idx, std::span<const std::uint32_t>(sc.call_overflow.data(), sc.call_overflow.size()));
    }
    if (!sc.call_abi_layout.empty()) {
        s.set_call_abi_layout_words(idx, std::span<const std::uint32_t>(sc.call_abi_layout.data(), sc.call_abi_layout.size()));
    }
    if (!sc.operand_overflow.empty()) {
        s.set_operand_overflow(idx, std::span<const std::uint32_t>(sc.operand_overflow.data(), sc.operand_overflow.size()));
    }
    if (!sc.result_ids.empty()) {
        s.set_result_ids(idx, std::span<const std::uint32_t>(sc.result_ids.data(), sc.result_ids.size()));
    }
    if (sc.effects != 0u) {
        s.set_node_effects(idx, sc.effects);
    }
}

/**
 * Flow-insensitive mark-and-sweep DCE, safe on non-SSA, CFG-shaped IR.
 *
 * Roots: every node that is not a whitelisted pure value op (calls, stores,
 * loads, labels, branches, returns, params, function headers, unknown ops),
 * every node carrying a barrier flag, every node with an index-keyed sidecar,
 * and every node accepted by `is_extra_root`.
 * Uses: op_a/op_b/op_c (except LOAD_IMM/IMMEDIATE payloads), non-zero
 * logic_flags (CALL 3rd argument), CALL overflow args and operand overflow.
 * A slot is live if any live node uses it; a pure node is live iff its result
 * slot is live (all definitions of a slot are kept together, which is what
 * makes this sound for loop-carried re-definitions such as i = i + 1).
 *
 * Sweep: compaction with remapping of every index-keyed sidecar. If the stream
 * carries node-id keyed metadata that cannot be remapped through the public
 * IRStream API (ownership / concurrency / DSA maps), dead nodes are replaced
 * by NOP in place instead, so no node index ever shifts.
 */
template <typename ExtraRootFn>
inline void run_mark_sweep_dce(ir::IRStream& stream, ExtraRootFn&& is_extra_root) {
    if (stream.is_null() || stream.is_active_transaction()) return;
    const size_t n = stream.size();
    if (n == 0u) return;

    std::vector<ir::IRNode> nodes(n);
    std::vector<std::uint8_t> has_sc(n, 0u);
    std::unordered_map<size_t, NodeSidecars> sc_map;
    for (size_t i = 0; i < n; ++i) {
        nodes[i] = stream.get_node(i);
        NodeSidecars sc = read_sidecars(stream, i);
        if (!sc.empty()) {
            has_sc[i] = 1u;
            sc_map.emplace(i, std::move(sc));
        }
    }

    std::vector<std::uint8_t> live(n, 0u);
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> pure_defs;  // slot -> pure def nodes
    std::unordered_set<std::uint32_t> live_slots;
    std::vector<std::uint32_t> pending;
    pure_defs.reserve(n);
    live_slots.reserve(n * 2u);

    const auto mark_slot = [&](std::uint32_t slot) {
        if (slot == ir::INVALID_SLOT) return;
        if (live_slots.insert(slot).second) pending.push_back(slot);
    };

    const auto mark_uses = [&](size_t i) {
        const ir::IRNode& nd = nodes[i];
        if (!is_immediate_payload_opcode(nd.opcode)) {
            mark_slot(nd.op_a);
            mark_slot(nd.op_b);
            mark_slot(nd.op_c);
            if (nd.logic_flags != 0u) mark_slot(nd.logic_flags);
        }
        if (has_sc[i]) {
            const auto it = sc_map.find(i);
            if (it != sc_map.end()) {
                for (const std::uint32_t s : it->second.call_overflow) mark_slot(s);
                for (const std::uint32_t s : it->second.operand_overflow) mark_slot(s);
            }
        }
    };

    // Mark: roots first.
    for (size_t i = 0; i < n; ++i) {
        const ir::IRNode& nd = nodes[i];
        bool removable = !has_sc[i] &&
                         (nd.opcode == ir::OpCode::NOP || is_pure_node(nd.opcode, nd.flags));
        if (removable && is_extra_root(nd)) removable = false;

        if (!removable) {
            live[i] = 1u;
            mark_uses(i);
            continue;
        }
        if (nd.opcode != ir::OpCode::NOP && nd.result_id != ir::INVALID_SLOT) {
            pure_defs[nd.result_id].push_back(static_cast<std::uint32_t>(i));
        }
    }

    // Fixpoint: a newly-live slot makes every pure definition of it live.
    while (!pending.empty()) {
        const std::uint32_t slot = pending.back();
        pending.pop_back();
        const auto it = pure_defs.find(slot);
        if (it == pure_defs.end()) continue;
        for (const std::uint32_t idx : it->second) {
            if (live[idx]) continue;
            live[idx] = 1u;
            mark_uses(idx);
        }
    }

    size_t live_count = 0;
    for (size_t i = 0; i < n; ++i) live_count += live[i];
    if (live_count == n) return;

    // Node-id keyed maps (ownership / concurrency / DSA) cannot be remapped via
    // the public API; keep indices stable in that case.
    const bool can_compact = stream.estimate_complexity_score() == 0u;

    stream.begin_transaction();
    ir::IRAtom* atoms = stream.raw_atoms();
    ir::IRCore* cores = stream.raw_cores();
    if (!atoms || !cores) {
        stream.rollback_transaction();
        return;
    }

    if (!can_compact) {
        bool changed = false;
        for (size_t i = 0; i < n; ++i) {
            if (live[i]) continue;
            if (atoms[i].opcode == ir::OpCode::NOP &&
                (atoms[i].flags & ir::Flags::IS_DEAD) != 0u &&
                cores[i].result_id == ir::INVALID_SLOT) {
                continue;
            }
            atoms[i].opcode = ir::OpCode::NOP;
            atoms[i].flags = ir::Flags::IS_DEAD;
            cores[i].result_id = ir::INVALID_SLOT;
            cores[i].op_a = ir::INVALID_SLOT;
            cores[i].op_b = ir::INVALID_SLOT;
            cores[i].op_c = ir::INVALID_SLOT;
            cores[i].logic_flags = 0u;
            cores[i].intent_hash = ir::IRNode::calculate_hash(
                atoms[i].opcode, cores[i].result_id, cores[i].op_a, cores[i].op_b, cores[i].op_c);
            changed = true;
        }
        if (changed && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
        return;
    }

    // Compaction. Full IRAtom/IRCore copies preserve logic_flags, aux_flags,
    // debug_id and epoch.
    std::vector<std::pair<size_t, size_t>> moved_sidecars;  // old index -> new index
    size_t write_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!live[i]) continue;
        if (write_idx != i) {
            atoms[write_idx] = atoms[i];
            cores[write_idx] = cores[i];
            if (has_sc[i]) moved_sidecars.emplace_back(i, write_idx);
        }
        cores[write_idx].intent_hash = ir::IRNode::calculate_hash(
            atoms[write_idx].opcode,
            cores[write_idx].result_id,
            cores[write_idx].op_a,
            cores[write_idx].op_b,
            cores[write_idx].op_c);
        ++write_idx;
    }

    stream.set_shadow_count(write_idx);
    if (!stream.validate_shadow_integrity()) {
        stream.rollback_transaction();
        return;
    }
    stream.commit_transaction();

    // Remap index-keyed side tables. Dead nodes never carry sidecars (they are
    // roots), so only moved survivors need remapping. Clear all old slots first
    // so overlapping old/new indices cannot clobber each other.
    for (const auto& mv : moved_sidecars) {
        clear_sidecars(stream, mv.first);
    }
    for (const auto& mv : moved_sidecars) {
        const auto it = sc_map.find(mv.first);
        if (it != sc_map.end()) write_sidecars(stream, mv.second, it->second);
    }
}

} // namespace scalar_core_detail

/**
 * Block-local value numbering (local CSE) for non-SSA HIR.
 *
 * - The available-expression table is cleared at every node that is not a
 *   whitelisted pure value op (LABEL, branches, GOTO, RET, CALL, stores,
 *   function headers, unknown ops, nodes with sidecars or barrier flags).
 * - Whenever a slot is (re)defined, every entry whose operands or result equal
 *   that slot is invalidated.
 * - LOAD_IMM / IMMEDIATE payloads are never treated as value references and
 *   constants are not numbered (they stay rematerialisable immediates).
 * - A redundant computation `r = op(a, b)` with an available equivalent `c` is
 *   rewritten in place to `r = MOV c`. Nodes are never removed or reordered, so
 *   node indices and every index-keyed side table stay valid. logic_flags,
 *   aux_flags, debug_id and epoch are preserved.
 */
class DGTLGVN {
    struct Key {
        std::uint16_t opcode = 0u;
        std::uint16_t flags = 0u;
        std::uint32_t op_a = 0u;
        std::uint32_t op_b = 0u;
        std::uint32_t op_c = 0u;
        std::uint32_t logic_flags = 0u;

        bool operator==(const Key& o) const noexcept {
            return opcode == o.opcode && flags == o.flags &&
                   op_a == o.op_a && op_b == o.op_b && op_c == o.op_c &&
                   logic_flags == o.logic_flags;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            std::uint64_t h = ir::IRNode::calculate_semantic_hash(
                static_cast<ir::OpCode>(k.opcode), k.op_a, k.op_b, k.op_c, k.flags);
            h ^= static_cast<std::uint64_t>(k.logic_flags) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };

    std::unordered_map<Key, std::uint32_t, KeyHash> table_;
    std::unordered_map<std::uint32_t, std::vector<Key>> deps_;  // slot -> keys mentioning it

public:
    explicit DGTLGVN(size_t max_ids = 64) {
        table_.reserve(std::min<size_t>(std::max<size_t>(max_ids, 64u), 4096u));
    }

    void execute(ir::IRStream& stream) {
        if (stream.is_null() || stream.is_active_transaction()) return;
        const size_t n = stream.size();
        if (n == 0u) return;

        // Nodes carrying index-keyed sidecars are opaque barriers.
        std::vector<std::uint8_t> has_sc(n, 0u);
        for (size_t i = 0; i < n; ++i) {
            has_sc[i] = scalar_core_detail::read_sidecars(stream, i).empty() ? 0u : 1u;
        }

        reset_block();

        stream.begin_transaction();
        ir::IRAtom* atoms = stream.raw_atoms();
        ir::IRCore* cores = stream.raw_cores();
        if (!atoms || !cores) {
            stream.rollback_transaction();
            return;
        }

        bool changed = false;
        for (size_t i = 0; i < n; ++i) {
            ir::IRAtom& atom = atoms[i];
            ir::IRCore& core = cores[i];
            const ir::OpCode op = atom.opcode;

            if (op == ir::OpCode::NOP) continue;

            if (has_sc[i] || !scalar_core_detail::is_pure_node(op, atom.flags)) {
                // Block boundary or opaque instruction: nothing stays available.
                reset_block();
                continue;
            }

            const std::uint32_t r = core.result_id;
            if (r == ir::INVALID_SLOT) continue;

            // Moves and constants are not numbered; they only redefine r.
            if (op == ir::OpCode::REG_MOVE || scalar_core_detail::is_immediate_payload_opcode(op)) {
                invalidate_slot(r);
                continue;
            }

            Key key{};
            key.opcode = static_cast<std::uint16_t>(op);
            key.flags = atom.flags;
            key.op_a = core.op_a;
            key.op_b = core.op_b;
            key.op_c = core.op_c;
            key.logic_flags = core.logic_flags;
            if (scalar_core_detail::is_gvn_commutative(op) && key.op_a > key.op_b) {
                std::swap(key.op_a, key.op_b);
            }

            bool rewritten = false;
            if (const auto it = table_.find(key); it != table_.end()) {
                const std::uint32_t canonical = it->second;
                if (canonical == r) {
                    // Recomputes the value r already holds; nothing changes.
                    continue;
                }
                atom.opcode = ir::OpCode::REG_MOVE;
                atom.flags = static_cast<std::uint16_t>(
                    atom.flags & ~(ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST | ir::Flags::IS_FUSED));
                core.op_a = canonical;
                core.op_b = ir::INVALID_SLOT;
                core.op_c = ir::INVALID_SLOT;
                core.intent_hash = ir::IRNode::calculate_hash(
                    atom.opcode, core.result_id, core.op_a, core.op_b, core.op_c);
                rewritten = true;
                changed = true;
            }

            // r is (re)defined here: drop everything that mentioned its old value.
            invalidate_slot(r);

            // `i = i + 1` style self-updates do not produce a reusable expression.
            if (!rewritten && key.op_a != r && key.op_b != r && key.op_c != r) {
                insert(key, r);
            }
        }

        reset_block();

        if (!changed) {
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
    void reset_block() {
        if (!table_.empty()) {
            if (table_.bucket_count() > 8192u) table_ = decltype(table_){};
            else table_.clear();
        }
        if (!deps_.empty()) {
            if (deps_.bucket_count() > 8192u) deps_ = decltype(deps_){};
            else deps_.clear();
        }
    }

    void add_dep(std::uint32_t slot, const Key& key) {
        if (slot == ir::INVALID_SLOT) return;
        deps_[slot].push_back(key);
    }

    void insert(const Key& key, std::uint32_t result) {
        table_[key] = result;
        add_dep(result, key);
        add_dep(key.op_a, key);
        add_dep(key.op_b, key);
        add_dep(key.op_c, key);
    }

    void invalidate_slot(std::uint32_t slot) {
        if (slot == ir::INVALID_SLOT) return;
        const auto it = deps_.find(slot);
        if (it == deps_.end()) return;
        for (const Key& k : it->second) table_.erase(k);
        deps_.erase(it);
    }
};

class DGTLDCESolver {
public:
    DGTLDCESolver() = default;

    void execute(ir::IRStream& stream) {
        execute(stream, nullptr);
    }

    void execute(ir::IRStream& stream, const pipeline::DGTLLanguageRootMarker* root_marker) {
        scalar_core_detail::run_mark_sweep_dce(stream, [root_marker](const ir::IRNode& node) {
            return root_marker != nullptr && root_marker->is_root(node);
        });
    }
};

class DeadCodeElimination {
public:
    void execute(dgtl::ir::IRStream& stream) {
        scalar_core_detail::run_mark_sweep_dce(stream, [](const ir::IRNode&) noexcept {
            return false;
        });
    }
};

// Legacy entry point kept for existing callers; delegates to the block-local,
// non-SSA-safe value numbering above.
class GlobalValueNumbering {
public:
    void execute(dgtl::ir::IRStream& stream) {
        DGTLGVN gvn(stream.size());
        gvn.execute(stream);
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

            const auto collapsed = try_fold_node(node);
            const std::optional<ir::IRNode> simpler = std::nullopt;

            if (collapsed.has_value() || simpler.has_value()) {
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
               n.is_terminator();
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
    ir::analysis::AICostModel* m_ai_model;

public:
    explicit Instruction_Reordering_Pass(
        analysis::DGTLMemorySmasher& smasher,
        ir::analysis::AICostModel* ai = nullptr) noexcept
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
        return dgtl::ir::gen::op_has_trait(op.opcode, dgtl::ir::gen::GenTrait::IsMemoryRead) ? 10.0f : 1.0f;
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

/**
 * @class TailCallOptimizationPass
 * @brief Converts tail-position CALL instructions into zero-overhead iterative jumps (TCO).
 */
class TailCallOptimizationPass : public pipeline::DGTLOptimizationPass {
public:
    explicit TailCallOptimizationPass() noexcept = default;

    void execute(ir::IRStream& stream) override {
        const size_t count = stream.size();
        if (count < 2) return;

        stream.begin_transaction();
        ir::IRCore* cores = stream.raw_cores();
        ir::IRAtom* atoms = stream.raw_atoms();
        if (!cores || !atoms) {
            stream.rollback_transaction();
            return;
        }

        for (size_t i = 0; i + 1 < count; ++i) {
            if (atoms[i].opcode == ir::OpCode::CALL && 
                (atoms[i + 1].opcode == ir::OpCode::RET || atoms[i + 1].opcode == ir::OpCode::RETURN)) {
                if (cores[i].result_id == cores[i + 1].op_a) {
                    atoms[i].flags |= 0x0080; // Tail-call mark
                }
            }
        }

        if (stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "TailCallOptimization";
    }

    [[nodiscard]] pipeline::PassStage get_stage() const noexcept override {
        return pipeline::PassStage::PostInlining;
    }
};

} // namespace dgtl::ir::pass

export namespace dgtl::ir::pass::transform {
    using namespace dgtl::ir::pass;
    using dgtl::ir::pass::DeadCodeElimination;
    using dgtl::ir::pass::GlobalValueNumbering;
    using dgtl::ir::pass::DGTLInstructionFusionSynth;
}

export namespace dgtl::pass {
    using namespace dgtl::ir::pass;
}

export namespace dgtl::pass::transform {
    using namespace dgtl::ir::pass;
}
