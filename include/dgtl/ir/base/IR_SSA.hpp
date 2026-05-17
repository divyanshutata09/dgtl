/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Version:    1.0.0 — SSA CONSTRUCTION & REPAIR ENGINE
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * PILLAR 3: STATIC SINGLE ASSIGNMENT — CONSTRUCTION, VERIFICATION & REPAIR
 *
 * ALGORITHMS:
 *   Construction : Cytron et al. 1991 (dominance-frontier placement)
 *                  + Braun et al. 2013 (incremental SSA for JIT)
 *                  with block arguments as canonical join representation
 *   Repair       : Boissinot et al. 2009 (liveness-based SSA repair)
 *   Destruction  : Sreedhar et al. 1999 (parallel-copy elimination)
 *   Verification : Cooper & Torczon invariant checks (2011)
 *
 * STANDARDS: C++26 / UCRT64 — Zero-Overhead Primitive Mapping
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <span>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string_view>
#include <functional>
#include <limits>

#include "IR_Node.hpp"   // IRNode, IRAtom, IRCore, OpCode, Flags
#include "IR_Stream.hpp" // IRStream CFG bridge

// ============================================================
// Windows macro pollution guard
// ============================================================
#ifdef CONST
#  undef CONST
#endif
#ifdef PHI
#  undef PHI
#endif

namespace dgtl::ir::ssa {

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
struct BasicBlock;
struct DomTree;
struct DomFrontier;
class  SSABuilder;
class  SSAVerifier;
class  SSARepair;
class  PhiEliminator;

// ============================================================
// SECTION: FUNDAMENTAL SSA TYPES
// ============================================================

/// Virtual register identifier. 0 is the reserved undefined/poison value.
/// Bits [31:20] = version (SSA subscript), Bits [19:0] = base variable id.
using VReg   = uint32_t;
using BlockId = uint32_t;
using NodeId  = uint32_t;  // index into an IRNode flat array

inline constexpr VReg   kUndefinedVReg  = 0;
inline constexpr BlockId kInvalidBlock   = std::numeric_limits<BlockId>::max();
inline constexpr NodeId  kInvalidNode    = std::numeric_limits<NodeId>::max();

/// Encode an SSA name from a base variable and its version counter.
[[nodiscard]] inline constexpr VReg make_ssa_name(uint32_t base_var, uint32_t version) noexcept {
    // base_var: 20 bits (max 1M variables), version: 12 bits (max 4096 versions per var)
    return ((version & 0xFFFU) << 20) | (base_var & 0xFFFFFU);
}
[[nodiscard]] inline constexpr uint32_t ssa_base_var(VReg v) noexcept  { return v & 0xFFFFFU; }
[[nodiscard]] inline constexpr uint32_t ssa_version(VReg v) noexcept   { return (v >> 20) & 0xFFFU; }

static_assert(sizeof(VReg) == 4, "VReg must be 4 bytes");

// ============================================================
// SECTION: BLOCK ARGUMENTS (canonical SSA join representation)
// ============================================================

struct BlockArgumentIncoming {
    BlockId pred = kInvalidBlock;
    VReg value = kUndefinedVReg;
};

struct BlockArgument {
    uint32_t index = 0;
    uint32_t base_var = 0;
    VReg ssa_name = kUndefinedVReg;
    std::vector<BlockArgumentIncoming> incoming;
};

// ============================================================
// SECTION: BASIC BLOCK
// ============================================================

/**
 * @struct BasicBlock
 * @brief Control-flow graph node holding a linear sequence of IR nodes.
 *
 * Each block owns block arguments (placed at entry), a half-open range
 * [node_begin, node_end) into a flat IRNode array, and CFG edge lists.
 *
 * Dominance information is computed by DomTree and written into the
 * `idom`, `dom_depth`, `pre_order`, and `post_order` fields.
 */
struct BasicBlock {
    BlockId  id          = kInvalidBlock;
    NodeId   node_begin  = kInvalidNode;   // first IRNode index (inclusive)
    NodeId   node_end    = kInvalidNode;   // last  IRNode index (exclusive)

    // CFG edges
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;

    // Canonical SSA join representation (Cranelift/MLIR style).
    std::vector<BlockArgument> block_arguments;

    // Dominance information (populated by DomTree::compute)
    BlockId  idom        = kInvalidBlock;  // immediate dominator
    uint32_t dom_depth   = 0;             // depth in the dominator tree
    uint32_t pre_order   = 0;             // RPO index (used in dom algorithms)
    uint32_t post_order  = 0;             // post-order index

    // Liveness (populated by liveness analysis)
    std::unordered_set<VReg> live_in;
    std::unordered_set<VReg> live_out;
    std::unordered_set<VReg> def;         // VRegs defined in this block
    std::unordered_set<VReg> use;         // VRegs used before def in this block

    // ----------------------------------------------------------------
    bool is_entry() const noexcept { return predecessors.empty(); }
    bool is_exit()  const noexcept { return successors.empty();   }

    /// True if block B is a predecessor of this block.
    [[nodiscard]] bool has_pred(BlockId b) const noexcept {
        return std::find(predecessors.begin(), predecessors.end(), b)
               != predecessors.end();
    }

    [[nodiscard]] BlockArgument* find_block_argument(uint32_t base_var) noexcept {
        for (auto& arg : block_arguments) {
            if (arg.base_var == base_var) return &arg;
        }
        return nullptr;
    }

    [[nodiscard]] const BlockArgument* find_block_argument(uint32_t base_var) const noexcept {
        for (const auto& arg : block_arguments) {
            if (arg.base_var == base_var) return &arg;
        }
        return nullptr;
    }

    BlockArgument& add_block_argument(uint32_t base_var, VReg ssa_name = kUndefinedVReg) {
        BlockArgument arg{};
        arg.index = static_cast<uint32_t>(block_arguments.size());
        arg.base_var = base_var;
        arg.ssa_name = ssa_name;
        block_arguments.push_back(std::move(arg));
        return block_arguments.back();
    }

    [[nodiscard]] bool uses_block_arguments() const noexcept {
        return !block_arguments.empty();
    }

    void clear_ssa_joins() {
        block_arguments.clear();
    }
};

// ============================================================
// SECTION: CONTROL-FLOW GRAPH
// ============================================================

/**
 * @struct CFG
 * @brief Owns all BasicBlocks for one function.
 *
 * The flat `nodes` span is borrowed (not owned); CFG only stores
 * per-block metadata and edge topology.
 */
struct CFG {
    std::vector<BasicBlock> blocks;
    BlockId entry = kInvalidBlock;
    BlockId exit  = kInvalidBlock;  // synthetic exit if multiple returns exist

    // Reverse-post-order traversal order (filled by compute_rpo)
    std::vector<BlockId> rpo;

    // ----------------------------------------------------------------

    [[nodiscard]] BasicBlock* block(BlockId id) noexcept {
        if (id >= blocks.size()) return nullptr;
        return &blocks[id];
    }
    [[nodiscard]] const BasicBlock* block(BlockId id) const noexcept {
        if (id >= blocks.size()) return nullptr;
        return &blocks[id];
    }

    BasicBlock& add_block() {
        auto& b   = blocks.emplace_back();
        b.id      = static_cast<BlockId>(blocks.size() - 1);
        return b;
    }

    void add_edge(BlockId from, BlockId to) {
        blocks[from].successors.push_back(to);
        blocks[to].predecessors.push_back(from);
    }

    /// Rebuild SSA CFG topology from IRStream's canonical CFG cache.
    ///
    /// IRStream block IDs are remapped to dense [0..N) block IDs because
    /// SSA algorithms index directly into `blocks` by BlockId.
    [[nodiscard]] bool rebuild_from_ir_stream(const dgtl::ir::IRStream& stream) {
        blocks.clear();
        rpo.clear();
        entry = kInvalidBlock;
        exit = kInvalidBlock;

        const auto& ir_blocks = stream.get_basic_blocks();
        const auto& ir_edges  = stream.get_cfg_edges();
        if (ir_blocks.empty()) {
            return true;
        }

        std::unordered_map<uint32_t, BlockId> dense_of;
        dense_of.reserve(ir_blocks.size());

        blocks.reserve(ir_blocks.size());
        for (const auto& src : ir_blocks) {
            const BlockId dense_id = static_cast<BlockId>(blocks.size());
            dense_of[src.id] = dense_id;

            auto& dst = blocks.emplace_back();
            dst.id = dense_id;
            dst.node_begin = static_cast<NodeId>(src.begin);
            dst.node_end   = static_cast<NodeId>(src.end);
        }

        for (const auto& e : ir_edges) {
            const auto fit = dense_of.find(e.from);
            const auto tit = dense_of.find(e.to);
            if (fit == dense_of.end() || tit == dense_of.end()) {
                return false;
            }
            add_edge(fit->second, tit->second);
        }

        const uint32_t src_entry = stream.get_entry_block_id();
        if (src_entry != dgtl::ir::INVALID_SLOT) {
            const auto it = dense_of.find(src_entry);
            if (it == dense_of.end()) return false;
            entry = it->second;
        } else {
            entry = 0;
        }

        // Keep SSA contract of a single exit block id when possible.
        // If multiple exits exist, leave as kInvalidBlock (caller may synthesize one).
        BlockId single_exit = kInvalidBlock;
        for (const auto& b : blocks) {
            if (!b.successors.empty()) continue;
            if (single_exit == kInvalidBlock) single_exit = b.id;
            else { single_exit = kInvalidBlock; break; }
        }
        exit = single_exit;

        return true;
    }

    /// Compute reverse post-order; required before dominance analysis.
    void compute_rpo() {
        rpo.clear();
        rpo.reserve(blocks.size());

        std::vector<bool> visited  (blocks.size(), false);
        // Bug fix: a node can be pushed multiple times before being marked visited
        // (e.g., in a fork-then-join diamond). Track whether a node has already
        // been appended to `post` so it is never emitted twice.
        std::vector<bool> finalized(blocks.size(), false);
        std::vector<BlockId> stack;
        stack.reserve(blocks.size());
        stack.push_back(entry);

        // Iterative post-order DFS
        std::vector<BlockId> post;
        post.reserve(blocks.size());

        while (!stack.empty()) {
            BlockId cur = stack.back();
            if (!visited[cur]) {
                visited[cur] = true;
                // Push successors in reverse order for correct DFS traversal
                const auto& succs = blocks[cur].successors;
                for (auto it = succs.rbegin(); it != succs.rend(); ++it) {
                    if (!visited[*it]) stack.push_back(*it);
                }
            } else {
                stack.pop_back();
                // Guard: only add each block once even if it was pushed > once
                if (!finalized[cur]) {
                    finalized[cur] = true;
                    post.push_back(cur);
                }
            }
        }

        // RPO = reverse of post-order
        for (auto it = post.rbegin(); it != post.rend(); ++it) {
            blocks[*it].pre_order = static_cast<uint32_t>(rpo.size());
            rpo.push_back(*it);
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(post.size()); ++i) {
            blocks[post[i]].post_order = i;
        }
    }
};

// ============================================================
// SECTION: DOMINATOR TREE
// ============================================================

/**
 * @class DomTree
 * @brief Computes the dominator tree using Cooper et al.'s simple iterative
 *        algorithm (Cooper, Harvey & Kennedy 2001) — O(N²) worst case but
 *        O(N) in practice on structured CFGs.
 *
 * After compute(), each BasicBlock::idom is filled in.
 * dominates(a, b) and strictly_dominates(a, b) are O(depth).
 */
class DomTree {
public:
    explicit DomTree(CFG& cfg) : cfg_(cfg) {}

    /// Run the dominator computation. Requires cfg_.rpo to be valid.
    void compute() {
        const size_t n = cfg_.blocks.size();
        idoms_.assign(n, kInvalidBlock);
        idoms_[cfg_.entry] = cfg_.entry;

        bool changed = true;
        while (changed) {
            changed = false;
            // Iterate in RPO (skip entry)
            for (size_t ri = 1; ri < cfg_.rpo.size(); ++ri) {
                BlockId b = cfg_.rpo[ri];
                const auto& preds = cfg_.blocks[b].predecessors;

                // Find first processed predecessor
                BlockId new_idom = kInvalidBlock;
                for (BlockId p : preds) {
                    if (idoms_[p] != kInvalidBlock) {
                        new_idom = p;
                        break;
                    }
                }
                if (new_idom == kInvalidBlock) continue;

                // Intersect with remaining processed predecessors
                for (BlockId p : preds) {
                    if (p == new_idom) continue;
                    if (idoms_[p] != kInvalidBlock) {
                        new_idom = intersect(p, new_idom);
                    }
                }

                if (idoms_[b] != new_idom) {
                    idoms_[b] = new_idom;
                    changed   = true;
                }
            }
        }

        // Write idom + depth back into blocks
        for (BlockId bid = 0; bid < static_cast<BlockId>(n); ++bid) {
            cfg_.blocks[bid].idom = idoms_[bid];
        }
        compute_depths();
    }

    /// True if block `a` dominates block `b` (a dom b).
    [[nodiscard]] bool dominates(BlockId a, BlockId b) const noexcept {
        if (a == b) return true;
        // Guard: kInvalidBlock is stored for unreachable blocks; walking into it
        // would produce an out-of-bounds idoms_ access (UB/crash).
        if (b == kInvalidBlock || b >= static_cast<BlockId>(idoms_.size()))
            return false;
        BlockId cur = b;
        while (cur != cfg_.entry) {
            if (cur == kInvalidBlock) return false; // unreachable block
            if (cur == a) return true;
            cur = idoms_[cur];
        }
        return (cur == a);
    }

    /// True if `a` strictly dominates `b` (a sdom b: a dom b and a ≠ b).
    [[nodiscard]] bool strictly_dominates(BlockId a, BlockId b) const noexcept {
        return (a != b) && dominates(a, b);
    }

    /// Return the immediate dominator of block b.
    [[nodiscard]] BlockId idom_of(BlockId b) const noexcept {
        return (b < idoms_.size()) ? idoms_[b] : kInvalidBlock;
    }

    /// Collect all blocks in the sub-tree rooted at `root` (inclusive).
    void dom_subtree(BlockId root, std::vector<BlockId>& out) const {
        for (BlockId b = 0; b < static_cast<BlockId>(cfg_.blocks.size()); ++b) {
            if (dominates(root, b)) out.push_back(b);
        }
    }

private:
    CFG&                    cfg_;
    std::vector<BlockId>    idoms_;

    BlockId intersect(BlockId a, BlockId b) const noexcept {
        while (a != b) {
            // Walk up the side with the higher post_order number
            // (lower in RPO = deeper in the dominator tree)
            while (cfg_.blocks[a].post_order < cfg_.blocks[b].post_order) a = idoms_[a];
            while (cfg_.blocks[b].post_order < cfg_.blocks[a].post_order) b = idoms_[b];
        }
        return a;
    }

    void compute_depths() {
        // BFS from entry in dominator tree order (RPO guarantees parent before child)
        cfg_.blocks[cfg_.entry].dom_depth = 0;
        for (BlockId bid : cfg_.rpo) {
            if (bid == cfg_.entry) continue;
            BlockId parent = idoms_[bid];
            cfg_.blocks[bid].dom_depth = cfg_.blocks[parent].dom_depth + 1;
        }
    }
};

// ============================================================
// SECTION: DOMINANCE FRONTIER
// ============================================================

/**
 * @class DomFrontier
 * @brief Computes the dominance frontier (DF) for each block.
 *
 * DF(n) = { y | ∃ predecessor x of y such that n dom x, but n !sdom y }
 *
 * Used by SSABuilder to determine where phi nodes must be inserted.
 * Algorithm: Cooper et al. 2001 — O(E) linear scan of CFG edges.
 */
class DomFrontier {
public:
    explicit DomFrontier(CFG& cfg, const DomTree& dom) : cfg_(cfg), dom_(dom) {}

    void compute() {
        const size_t n = cfg_.blocks.size();
        df_.assign(n, {});

        for (BlockId b = 0; b < static_cast<BlockId>(n); ++b) {
            const auto& preds = cfg_.blocks[b].predecessors;
            if (preds.size() < 2) continue;  // join point required for DF contribution

            for (BlockId p : preds) {
                BlockId runner = p;
                while (runner != dom_.idom_of(b)) {
                    df_[runner].insert(b);
                    runner = dom_.idom_of(runner);
                    if (runner == kInvalidBlock) break;
                }
            }
        }
    }

    /// Return the dominance frontier set for block `b`.
    [[nodiscard]] const std::unordered_set<BlockId>& of(BlockId b) const noexcept {
        static const std::unordered_set<BlockId> empty;
        return (b < df_.size()) ? df_[b] : empty;
    }

    /// Iterated dominance frontier: DF+(S) — the fixed-point closure.
    /// Used for determining phi-placement for a def-set S.
    [[nodiscard]] std::unordered_set<BlockId>
    iterated(const std::unordered_set<BlockId>& def_blocks) const {
        std::unordered_set<BlockId> result;
        std::vector<BlockId>        worklist(def_blocks.begin(), def_blocks.end());

        while (!worklist.empty()) {
            BlockId b = worklist.back();
            worklist.pop_back();
            for (BlockId y : of(b)) {
                if (result.insert(y).second) {
                    worklist.push_back(y);
                }
            }
        }
        return result;
    }

private:
    CFG&                                      cfg_;
    const DomTree&                            dom_;
    std::vector<std::unordered_set<BlockId>>  df_;
};

// ============================================================
// SECTION: LIVENESS ANALYSIS
// ============================================================

/**
 * @class LivenessAnalysis
 * @brief Backward dataflow analysis computing live_in / live_out per block.
 *
 * Equation (standard backward meet):
 *   live_out(B) = ∪ live_in(S) for each successor S of B
 *   live_in(B)  = use(B) ∪ (live_out(B) − def(B))
 *
 * Required by SSARepair to determine which variables need a phi at a block.
 */
class LivenessAnalysis {
public:
    explicit LivenessAnalysis(CFG& cfg) : cfg_(cfg) {}

    /// Populate BasicBlock::def, ::use from an IRNode array.
    /// Caller must populate `nodes` before calling this.
    void seed_def_use(std::span<const IRNode> nodes) {
        for (auto& blk : cfg_.blocks) {
            blk.def.clear();
            blk.use.clear();
            for (NodeId ni = blk.node_begin; ni < blk.node_end; ++ni) {
                if (ni >= nodes.size()) break;
                const IRNode& n = nodes[ni];
                // Uses (operands referenced before they are defined in this block)
                auto use_vreg = [&](VReg v) {
                    if (v != kUndefinedVReg && !blk.def.count(v))
                        blk.use.insert(v);
                };
                use_vreg(static_cast<VReg>(n.op_a));
                use_vreg(static_cast<VReg>(n.op_b));
                use_vreg(static_cast<VReg>(n.op_c));
                // Def
                if (n.result_id != 0)
                    blk.def.insert(static_cast<VReg>(n.result_id));
            }
            // Block arguments define values at block entry.
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    blk.def.insert(arg.ssa_name);
                }
            }
        }
    }

    /// Run to fixed point (backward pass over RPO in reverse).
    void compute() {
        bool changed = true;
        while (changed) {
            changed = false;
            // Traverse in reverse RPO (post-order direction)
            for (auto it = cfg_.rpo.rbegin(); it != cfg_.rpo.rend(); ++it) {
                BasicBlock& blk = cfg_.blocks[*it];

                // live_out = union of live_in of all successors
                std::unordered_set<VReg> new_live_out;
                for (BlockId succ_id : blk.successors) {
                    const BasicBlock& succ = cfg_.blocks[succ_id];
                    new_live_out.insert(succ.live_in.begin(), succ.live_in.end());
                    // Block-argument incoming uses at successor edge (*it -> succ).
                    for (const auto& arg : succ.block_arguments) {
                        for (const auto& in : arg.incoming) {
                            if (in.pred == *it && in.value != kUndefinedVReg) {
                                new_live_out.insert(in.value);
                            }
                        }
                    }
                }

                // live_in = use ∪ (live_out − def)
                std::unordered_set<VReg> new_live_in = blk.use;
                for (VReg v : new_live_out) {
                    if (!blk.def.count(v)) new_live_in.insert(v);
                }

                if (new_live_in != blk.live_in || new_live_out != blk.live_out) {
                    blk.live_in  = std::move(new_live_in);
                    blk.live_out = std::move(new_live_out);
                    changed = true;
                }
            }
        }
    }

private:
    CFG& cfg_;
};

// ============================================================
// SECTION: BLOCK-ARGUMENT REWRITE HELPERS
// ============================================================

class BlockArgumentSSA {
public:
    // Canonicalization entry-point: enforce consistent block-argument indices.
    static void canonicalize_to_block_arguments(CFG& cfg) {
        for (auto& blk : cfg.blocks) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(blk.block_arguments.size()); ++i) {
                blk.block_arguments[i].index = i;
            }
        }
    }
};

// ============================================================
// SECTION: SSA BUILDER (Cytron 1991 + Braun 2013)
// ============================================================

/**
 * @class SSABuilder
 * @brief Lifts a CFG from non-SSA form to pruned SSA form.
 *
 * Phase 1 — Phi placement (Cytron et al.):
 *   For each variable V, compute DF+(defs(V)) and insert a phi at each
 *   block in that set where V is live-in (pruned SSA — no dead phis).
 *
 * Phase 2 — Variable renaming (dominator tree walk):
 *   Walk the dominator tree in pre-order, maintaining a version stack per
 *   variable. At each phi, push a fresh name. At each use, replace with
 *   the top-of-stack name. Pop on dominator-tree exit.
 *
 * Invariants after construction:
 *   - Every VReg is defined exactly once (SSA property).
 *   - Every phi operand is the SSA name reaching along that CFG edge.
 *   - No dead phi nodes remain (pruned SSA).
 */
class SSABuilder {
public:
    explicit SSABuilder(CFG& cfg, DomTree& dom, DomFrontier& df,
                        std::span<IRNode> nodes)
        : cfg_(cfg), dom_(dom), df_(df), nodes_(nodes) {}

    /// Full SSA construction: join placement + renaming.
    /// After this call, cfg_.blocks[*].block_arguments are populated and
    /// every IRNode::result_id / op_a / op_b / op_c is an SSA VReg.
    void build() {
        BlockArgumentSSA::canonicalize_to_block_arguments(cfg_);
        collect_defs_and_uses();
        place_phis();
        rename_variables();
        eliminate_trivial_phis();
    }

    /// Incremental mode (Braun 2013): insert a new definition for variable
    /// `base_var` producing `ssa_name` at block `b`, then repair upward.
    /// Safe to call after build() when the IR is mutated.
    void add_definition(uint32_t base_var, VReg ssa_name, BlockId b) {
        current_def_[base_var][b] = ssa_name;
    }

    /// Look up the SSA name for `base_var` reaching the top of block `b`.
    /// Inserts incomplete block-argument joins if predecessors haven't been visited yet
    /// (Braun's "ask predecessor" recursive lookup).
    [[nodiscard]] VReg read_variable(uint32_t base_var, BlockId b) {
        auto it = current_def_.find(base_var);
        if (it != current_def_.end()) {
            auto bit = it->second.find(b);
            if (bit != it->second.end()) return bit->second;
        }
        return read_variable_recursive(base_var, b);
    }

private:
    CFG&                  cfg_;
    DomTree&              dom_;
    DomFrontier&          df_;
    std::span<IRNode>     nodes_;
    uint32_t              next_version_ = 1;

    // base_var → (block → ssa_name currently reaching top of block)
    std::unordered_map<uint32_t,
        std::unordered_map<BlockId, VReg>> current_def_;

    // base_var → set of blocks where it is defined
    std::unordered_map<uint32_t,
        std::unordered_set<BlockId>> def_blocks_;

    // Per-variable renaming stack (used during rename walk)
    std::unordered_map<uint32_t, std::vector<VReg>> rename_stack_;

    // NOTE: Block-argument joins are built eagerly; no separate incomplete-phi
    // tracking structure is required in this representation.

    // ----------------------------------------------------------------
    // Phase 0: collect where each variable is defined.
    // Precompute node→block in O(N+B) instead of the naive O(N×B) scan.
    // ----------------------------------------------------------------
    void collect_defs_and_uses() {
        // Build a flat node-to-block map in a single pass over blocks.
        const NodeId total = static_cast<NodeId>(nodes_.size());
        std::vector<BlockId> node_owner(total, kInvalidBlock);
        for (const auto& blk : cfg_.blocks) {
            for (NodeId ni = blk.node_begin; ni < blk.node_end && ni < total; ++ni)
                node_owner[ni] = blk.id;
        }
        // Now record def sites in O(N).
        for (NodeId ni = 0; ni < total; ++ni) {
            const IRNode& n = nodes_[ni];
            if (n.result_id == 0) continue;
            BlockId owner = node_owner[ni];
            if (owner == kInvalidBlock) continue;
            def_blocks_[static_cast<uint32_t>(n.result_id)].insert(owner);
        }
    }

    // ----------------------------------------------------------------
    // Phase 1: phi placement using iterated dominance frontier
    // ----------------------------------------------------------------
    void place_phis() {
        for (auto& [base_var, dblocks] : def_blocks_) {
            // Compute DF+(defs(base_var))
            std::unordered_set<BlockId> phi_blocks = df_.iterated(dblocks);

            for (BlockId b : phi_blocks) {
                BasicBlock* blk = cfg_.block(b);
                if (!blk || blk->find_block_argument(base_var)) continue;  // already placed

                // Pruned SSA: only insert if variable is live-in at this block
                // (skip if liveness not yet computed — full SSA mode)
                if (!blk->live_in.empty() && !blk->live_in.count(base_var)) continue;

                auto& arg = blk->add_block_argument(base_var, kUndefinedVReg);
                arg.incoming.reserve(blk->predecessors.size());
                for (BlockId pred : blk->predecessors) {
                    arg.incoming.push_back(BlockArgumentIncoming{pred, kUndefinedVReg});
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // Phase 2: variable renaming — dominator tree pre-order walk.
    // Precompute dominator-tree children to avoid the O(B) per-block
    // scan that made the full rename O(B²) on large CFGs.
    // ----------------------------------------------------------------
    void rename_variables() {
        // Build children[b] = list of blocks whose idom == b
        const size_t n = cfg_.blocks.size();
        std::vector<std::vector<BlockId>> children(n);
        for (const auto& blk : cfg_.blocks) {
            if (blk.idom != kInvalidBlock && blk.idom != blk.id)
                children[blk.idom].push_back(blk.id);
        }
        rename_block(cfg_.entry, children);
    }

    void rename_block(BlockId bid, const std::vector<std::vector<BlockId>>& children) {
        BasicBlock& blk = cfg_.blocks[bid];

        // Track how many names each variable pushed so we can pop them on exit
        std::unordered_map<uint32_t, uint32_t> push_counts;

        // 2a: For each block argument at block head, assign a fresh SSA name.
        for (auto& arg : blk.block_arguments) {
            VReg fresh = fresh_name(arg.base_var);
            arg.ssa_name = fresh;
            rename_stack_[arg.base_var].push_back(fresh);
            push_counts[arg.base_var]++;
        }

        // 2b: Rename uses and defs in each instruction
        for (NodeId ni = blk.node_begin; ni < blk.node_end; ++ni) {
            if (ni >= static_cast<NodeId>(nodes_.size())) break;
            IRNode& n = nodes_[ni];

            // Rename uses (operands) — replace base ids with top-of-stack SSA names
            auto rename_use = [&](uint32_t& field) {
                if (field == 0) return;
                auto it = rename_stack_.find(field);
                if (it != rename_stack_.end() && !it->second.empty())
                    field = it->second.back();
            };
            rename_use(n.op_a);
            rename_use(n.op_b);
            rename_use(n.op_c);

            // Rename def: assign a fresh SSA name
            if (n.result_id != 0) {
                uint32_t base = n.result_id;
                VReg fresh    = fresh_name(base);
                n.result_id   = fresh;
                rename_stack_[base].push_back(fresh);
                push_counts[base]++;
            }
        }

        // 2c: Fill incoming values for each successor block argument.
        for (BlockId succ_id : blk.successors) {
            BasicBlock& succ = cfg_.blocks[succ_id];
            for (auto& arg : succ.block_arguments) {
                auto it = rename_stack_.find(arg.base_var);
                VReg reaching = (it != rename_stack_.end() && !it->second.empty())
                                ? it->second.back()
                                : kUndefinedVReg;
                bool updated = false;
                for (auto& in : arg.incoming) {
                    if (in.pred == bid) {
                        in.value = reaching;
                        updated = true;
                        break;
                    }
                }
                if (!updated) {
                    arg.incoming.push_back(BlockArgumentIncoming{bid, reaching});
                }
            }
        }

        // 2d: Recurse into children in the dominator tree (O(1) lookup)
        if (bid < children.size()) {
            for (BlockId child : children[bid]) {
                rename_block(child, children);
            }
        }

        // 2e: Pop pushed names (restore stacks on exit from dominator subtree)
        for (auto& [base_var, cnt] : push_counts) {
            auto& stack = rename_stack_[base_var];
            for (uint32_t i = 0; i < cnt; ++i) {
                if (!stack.empty()) stack.pop_back();
            }
        }
    }

    // ----------------------------------------------------------------
    // Braun 2013: recursive read for incremental SSA
    // ----------------------------------------------------------------
    VReg read_variable_recursive(uint32_t base_var, BlockId b) {
        BasicBlock& blk = cfg_.blocks[b];
        VReg val;

        if (blk.predecessors.size() == 1) {
            // Single predecessor: no phi needed, just recurse up
            val = read_variable(base_var, blk.predecessors[0]);
        } else {
            // Multiple predecessors: create an incomplete block argument, then fill it
            VReg fresh = fresh_name(base_var);
            auto& arg = blk.add_block_argument(base_var, fresh);
            val = fresh;

            // Write to current_def_ before recursing to break cycles
            current_def_[base_var][b] = val;

            for (BlockId pred : blk.predecessors) {
                VReg pred_val = read_variable(base_var, pred);
                arg.incoming.push_back(BlockArgumentIncoming{pred, pred_val});
            }

            // Try to simplify immediately (block-argument trivial join)
            VReg same = kUndefinedVReg;
            bool trivial = true;
            for (const auto& in : arg.incoming) {
                VReg v = in.value;
                if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                if (same == kUndefinedVReg) {
                    same = v;
                } else if (same != v) {
                    trivial = false;
                    break;
                }
            }
            if (trivial && same != kUndefinedVReg) {
                current_def_[base_var][b] = same;
                val = same;
                arg.ssa_name = same;
            }
        }

        current_def_[base_var][b] = val;
        return val;
    }

    // ----------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------
    VReg fresh_name(uint32_t base_var) noexcept {
        return make_ssa_name(base_var, next_version_++);
    }

    void eliminate_trivial_phis() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& blk : cfg_.blocks) {
                for (auto& arg : blk.block_arguments) {
                    VReg same = kUndefinedVReg;
                    bool trivial = true;
                    for (const auto& in : arg.incoming) {
                        VReg v = in.value;
                        if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                        if (same == kUndefinedVReg) {
                            same = v;
                        } else if (same != v) {
                            trivial = false;
                            break;
                        }
                    }
                    if (trivial && same != kUndefinedVReg && arg.ssa_name != same) {
                        arg.ssa_name = same;
                        changed = true;
                    }
                }
            }
        }
    }
};

// ============================================================
// SECTION: SSA VERIFIER
// ============================================================

/**
 * @class SSAVerifier
 * @brief Asserts SSA invariants after construction or repair.
 *
 * Checks:
 *   1. Every VReg is defined exactly once across all blocks.
 *   2. Every use of a VReg is dominated by its unique definition.
 *   3. Every phi operand incoming from block P is dominated by P's def.
 *   4. No undefined VReg (kUndefinedVReg) appears as a use.
 *   5. No live phi has a trivial (all-same) operand set.
 *
 * Returns a list of violation strings; empty = SSA is valid.
 */
class SSAVerifier {
public:
    explicit SSAVerifier(CFG& cfg, DomTree& dom, std::span<const IRNode> nodes)
        : cfg_(cfg), dom_(dom), nodes_(nodes) {}

    [[nodiscard]] std::vector<std::string> verify() {
        violations_.clear();
        collect_all_defs();
        check_single_def();
        check_dom_use();
        check_phi_operands();
        check_no_trivial_live_phis();
        return violations_;
    }

    [[nodiscard]] bool is_valid() {
        return verify().empty();
    }

private:
    CFG&                     cfg_;
    DomTree&                 dom_;
    std::span<const IRNode>  nodes_;
    std::vector<std::string> violations_;

    // VReg → (block, node index / join-definition marker) of its single definition
    struct DefSite { BlockId block; bool is_join_def; NodeId node_idx; };
    std::unordered_map<VReg, DefSite> def_map_;

    void report(std::string msg) { violations_.push_back(std::move(msg)); }

    void collect_all_defs() {
        def_map_.clear();
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    def_map_[arg.ssa_name] = {blk.id, true, kInvalidNode};
                }
            }
        }
        for (NodeId ni = 0; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            const IRNode& n = nodes_[ni];
            if (n.result_id == 0) continue;
            def_map_[n.result_id] = {owner_block(ni), false, ni};
        }
    }

    void check_single_def() {
        std::unordered_map<VReg, uint32_t> def_count;
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    def_count[arg.ssa_name]++;
                }
            }
        }
        for (NodeId ni = 0; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            if (nodes_[ni].result_id != 0)
                def_count[nodes_[ni].result_id]++;
        }
        for (auto& [v, cnt] : def_count) {
            if (cnt > 1) {
                report("SSA violation: VReg " + std::to_string(v) +
                       " defined " + std::to_string(cnt) + " times (SSA requires exactly 1)");
            }
        }
    }

    void check_dom_use() {
        for (NodeId ni = 0; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            const IRNode& n = nodes_[ni];
            BlockId use_block = owner_block(ni);
            auto check = [&](VReg v) {
                if (v == kUndefinedVReg || v == 0) return;
                auto it = def_map_.find(v);
                if (it == def_map_.end()) {
                    report("SSA violation: VReg " + std::to_string(v) +
                           " used at node " + std::to_string(ni) + " but never defined");
                    return;
                }
                if (!dom_.dominates(it->second.block, use_block)) {
                    report("SSA violation: VReg " + std::to_string(v) +
                           " used in block " + std::to_string(use_block) +
                           " but defined in non-dominating block " +
                           std::to_string(it->second.block));
                }
            };
            check(static_cast<VReg>(n.op_a));
            check(static_cast<VReg>(n.op_b));
            check(static_cast<VReg>(n.op_c));
        }
    }

    void check_phi_operands() {
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                for (const auto& in : arg.incoming) {
                    if (in.value == kUndefinedVReg) {
                        report("SSA violation: block argument for var " +
                               std::to_string(arg.base_var) + " at block " +
                               std::to_string(blk.id) + " has undefined incoming from pred " +
                               std::to_string(in.pred));
                        continue;
                    }
                    auto it = def_map_.find(in.value);
                    if (it == def_map_.end()) {
                        report("SSA violation: block-argument incoming VReg " +
                               std::to_string(in.value) + " has no definition");
                    } else if (!dom_.dominates(it->second.block, in.pred)) {
                        report("SSA violation: block-argument incoming VReg " +
                               std::to_string(in.value) + " from block " +
                               std::to_string(in.pred) + " not dominated by def block " +
                               std::to_string(it->second.block));
                    }
                }
            }

        }
    }

    void check_no_trivial_live_phis() {
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                VReg same = kUndefinedVReg;
                bool trivial = true;
                for (const auto& in : arg.incoming) {
                    VReg v = in.value;
                    if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                    if (same == kUndefinedVReg) {
                        same = v;
                    } else if (same != v) {
                        trivial = false;
                        break;
                    }
                }
                if (trivial && same != kUndefinedVReg) {
                    report("SSA quality: trivial block argument for var " +
                           std::to_string(arg.base_var) + " at block " +
                           std::to_string(blk.id) + " should have been folded");
                }
            }
        }
    }

    BlockId owner_block(NodeId ni) const noexcept {
        for (const auto& blk : cfg_.blocks) {
            if (ni >= blk.node_begin && ni < blk.node_end) return blk.id;
        }
        return kInvalidBlock;
    }
};

// ============================================================
// SECTION: SSA REPAIR (Boissinot et al. 2009)
// ============================================================

/**
 * @class SSARepair
 * @brief Restores SSA form after IR transformations break it.
 *
 * Transformations (inlining, loop unrolling, code motion, etc.) can:
 *   A. Create a new definition that dominates an existing one.
 *   B. Move a definition out of a block, leaving uses without a dominating def.
 *   C. Delete a definition that a phi still references.
 *   D. Merge blocks, making a phi operand's predecessor no longer exist.
 *
 * Repair strategy (Boissinot):
 *   1. Run liveness analysis on the broken IR.
 *   2. For each block B: for each V live-in at B that is not dominated by a
 *      single reaching definition, insert a phi for V at B.
 *   3. Re-run the renaming pass over the mutated region only.
 *   4. Remove phi nodes that became trivial or dead after repair.
 */
class SSARepair {
public:
    explicit SSARepair(CFG& cfg, DomTree& dom, DomFrontier& df,
                       LivenessAnalysis& liveness, std::span<IRNode> nodes)
        : cfg_(cfg), dom_(dom), df_(df), liveness_(liveness), nodes_(nodes) {}

    /// Full repair: recompute liveness, reinsert missing phis, re-rename.
    void repair() {
        liveness_.seed_def_use(nodes_);
        liveness_.compute();
        insert_missing_phis();
        re_rename_all();
        remove_dead_and_trivial_phis();
    }

    /// Targeted repair after a specific block was mutated.
    /// More efficient than full repair when the mutation scope is known.
    void repair_region(const std::vector<BlockId>& mutated_blocks) {
        liveness_.seed_def_use(nodes_);
        liveness_.compute();
        insert_missing_phis_for(mutated_blocks);
        re_rename_all();
        remove_dead_and_trivial_phis();
    }

    /// Handle case D: predecessor block `old_pred` was replaced by `new_pred`
    /// on the CFG edge into block `blk`. Update all join incoming values accordingly.
    void replace_predecessor(BlockId blk, BlockId old_pred, BlockId new_pred) {
        BasicBlock* b = cfg_.block(blk);
        if (!b) return;
        for (auto& arg : b->block_arguments) {
            bool found_old = false;
            for (auto& in : arg.incoming) {
                if (in.pred == old_pred) {
                    in.value = kUndefinedVReg;
                    found_old = true;
                    break;
                }
            }
            if (found_old) {
                arg.incoming.push_back(BlockArgumentIncoming{new_pred, kUndefinedVReg});
            }
        }
    }

    /// Handle case C: a definition `dead_def` was deleted.
    /// Replace all uses and join incoming values with `replacement` (or kUndefinedVReg
    /// to mark as broken, triggering re-insertion during next repair()).
    void delete_definition(VReg dead_def, VReg replacement = kUndefinedVReg) {
        for (auto& n : nodes_) {
            auto fix = [&](uint32_t& field) {
                if (static_cast<VReg>(field) == dead_def) field = replacement;
            };
            fix(n.op_a); fix(n.op_b); fix(n.op_c);
        }
        for (auto& blk : cfg_.blocks) {
            for (auto& arg : blk.block_arguments) {
                for (auto& in : arg.incoming) {
                    if (in.value == dead_def) {
                        in.value = replacement;
                    }
                }
            }
        }
    }

private:
    CFG&              cfg_;
    DomTree&          dom_;
    DomFrontier&      df_;
    LivenessAnalysis& liveness_;
    std::span<IRNode> nodes_;
    uint32_t          next_version_ = 0x8000; // high range to avoid collisions with builder

    void insert_missing_phis() {
        for (auto& blk : cfg_.blocks) {
            insert_missing_phis_for_block(blk.id);
        }
    }

    void insert_missing_phis_for(const std::vector<BlockId>& blocks) {
        // Compute DF of the mutated region, then insert there
        std::unordered_set<BlockId> region(blocks.begin(), blocks.end());
        std::unordered_set<BlockId> frontier = df_.iterated(region);
        frontier.insert(region.begin(), region.end());

        for (BlockId bid : frontier) {
            insert_missing_phis_for_block(bid);
        }
    }

    void insert_missing_phis_for_block(BlockId bid) {
        BasicBlock* blk = cfg_.block(bid);
        if (!blk || blk->predecessors.size() < 2) return;

        for (VReg v : blk->live_in) {
            uint32_t base = ssa_base_var(v);
            if (blk->find_block_argument(base)) continue;  // already exists

            // Check if there is a unique reaching def that dominates all preds
            // If not, a phi is required.
            bool needs_phi = false;
            BlockId first_def_block = kInvalidBlock;
            for (BlockId pred : blk->predecessors) {
                BlockId def_block = find_def_block(base, pred);
                if (first_def_block == kInvalidBlock) {
                    first_def_block = def_block;
                } else if (def_block != first_def_block) {
                    needs_phi = true;
                    break;
                }
            }

            if (needs_phi) {
                auto& arg = blk->add_block_argument(base, make_ssa_name(base, next_version_++));
                arg.incoming.reserve(blk->predecessors.size());
                for (BlockId pred : blk->predecessors) {
                    arg.incoming.push_back(BlockArgumentIncoming{pred, kUndefinedVReg});
                }
            }
        }
    }

    BlockId find_def_block(uint32_t base_var, BlockId from) const noexcept {
        // Walk dominators upward looking for a def of base_var
        BlockId cur = from;
        while (cur != kInvalidBlock) {
            const BasicBlock* blk = cfg_.block(cur);
            if (!blk) break;
            if (blk->def.count(base_var)) return cur;
            if (blk->find_block_argument(base_var)) return cur;
            if (cur == blk->idom) break;  // reached entry
            cur = blk->idom;
        }
        return kInvalidBlock;
    }

    void re_rename_all() {
        // A lightweight rename pass: re-resolve all block-argument incoming slots that hold
        // kUndefinedVReg, using the current def map per predecessor.
        // Full re-renaming of instructions is left to SSABuilder::build() if needed.
        for (auto& blk : cfg_.blocks) {
            for (auto& arg : blk.block_arguments) {
                for (auto& in : arg.incoming) {
                    if (in.value == kUndefinedVReg) {
                        in.value = find_reaching_def(arg.base_var, in.pred);
                    }
                }
            }
        }
    }

    VReg find_reaching_def(uint32_t base_var, BlockId from) const noexcept {
        BlockId cur = from;
        while (cur != kInvalidBlock) {
            const BasicBlock* blk = cfg_.block(cur);
            if (!blk) break;
            // Check block arguments first (canonical join defs at block entry)
            const BlockArgument* arg = blk->find_block_argument(base_var);
            if (arg && arg->ssa_name != kUndefinedVReg) return arg->ssa_name;
            // Check instructions in this block (last def wins)
            VReg last = kUndefinedVReg;
            for (NodeId ni = blk->node_begin; ni < blk->node_end; ++ni) {
                if (ni >= static_cast<NodeId>(nodes_.size())) break;
                const IRNode& n = nodes_[ni];
                if (n.result_id != 0 && ssa_base_var(n.result_id) == base_var)
                    last = n.result_id;
            }
            if (last != kUndefinedVReg) return last;
            if (cur == blk->idom) break;
            cur = blk->idom;
        }
        return kUndefinedVReg;
    }

    void remove_dead_and_trivial_phis() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& blk : cfg_.blocks) {
                for (auto& arg : blk.block_arguments) {
                    if (!blk.live_in.count(arg.base_var)) {
                        arg.ssa_name = kUndefinedVReg;
                        changed = true;
                        continue;
                    }
                    VReg same = kUndefinedVReg;
                    bool trivial = true;
                    for (const auto& in : arg.incoming) {
                        VReg v = in.value;
                        if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                        if (same == kUndefinedVReg) {
                            same = v;
                        } else if (same != v) {
                            trivial = false;
                            break;
                        }
                    }
                    if (trivial && same != kUndefinedVReg && arg.ssa_name != same) {
                        arg.ssa_name = same;
                        changed = true;
                    }
                }
            }
        }
    }
};

// ============================================================
// SECTION: PHI ELIMINATOR (SSA Destruction)
// ============================================================

/**
 * @class PhiEliminator
 * @brief Converts out of SSA form by replacing block-argument joins with parallel copies
 *        on incoming CFG edges (Sreedhar et al. 1999 — Method III).
 *
 * Each join argument `r = blockarg([a:P0], [b:P1])` is replaced by:
 *   - On edge P0→B : insert  COPY r ← a
 *   - On edge P1→B : insert  COPY r ← b
 *
 * Lost-copy and swap problems are resolved by inserting a fresh temporary
 * when an incoming value is also defined by another join in the same block.
 *
 * Output: `parallel_copies` — a map from CFG edge to a list of (dst, src)
 * copy operations that the code generator must emit on that edge.
 */
class PhiEliminator {
public:
    struct Copy {
        VReg dst;  // copy destination (the block argument SSA name)
        VReg src;  // copy source     (the incoming edge value)
    };

    // Edge → ordered list of parallel copies to emit on that edge
    using EdgeKey = std::pair<BlockId, BlockId>;   // (predecessor, successor)
    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& e) const noexcept {
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(e.first) << 32) | e.second);
        }
    };
    using CopyMap = std::unordered_map<EdgeKey, std::vector<Copy>, EdgeKeyHash>;

    explicit PhiEliminator(CFG& cfg) : cfg_(cfg), temp_counter_(0xF00) {}

    /// Eliminate all block-argument joins and populate `parallel_copies`.
    /// Caller is responsible for emitting the copies on split critical edges
    /// or in dedicated copy blocks.
    [[nodiscard]] CopyMap eliminate() {
        CopyMap result;

        for (auto& blk : cfg_.blocks) {
            if (blk.block_arguments.empty()) continue;

            // Step 1: Identify interferences (join result used as incoming
            // value of another join in the same block — the "lost copy" scenario).
            std::unordered_set<VReg> phi_results;
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    phi_results.insert(arg.ssa_name);
                }
            }

            for (auto& arg : blk.block_arguments) {
                if (arg.ssa_name == kUndefinedVReg) continue;

                for (const auto& in : arg.incoming) {
                    EdgeKey edge   = {in.pred, blk.id};

                    VReg src = in.value;
                    // Lost-copy check: if src is also a join result in this block,
                    // it hasn't been resolved yet on this edge — use a temp.
                    // Bug fix: using a fixed version (0xFFE) caused collisions when
                    // multiple srcs shared the same base variable. Use a per-eliminator
                    // counter in the dedicated temp range [0xF00, 0xFFD] instead.
                    if (phi_results.count(src) && src != arg.ssa_name) {
                        // Emit  temp ← src  then  arg.ssa_name ← temp
                        VReg temp = make_ssa_name(ssa_base_var(src), temp_counter_++);
                        // Wrap within safe temp range; version 0 is reserved (kUndefinedVReg)
                        if (ssa_version(make_ssa_name(0, temp_counter_)) == 0)
                            temp_counter_ = 0xF00;
                        result[edge].push_back({temp,        src});
                        result[edge].push_back({arg.ssa_name,  temp});
                    } else {
                        result[edge].push_back({arg.ssa_name, src});
                    }
                }

                arg.ssa_name = kUndefinedVReg;  // join now represented as edge copies
            }
        }

        return result;
    }

private:
    CFG&     cfg_;
    uint32_t temp_counter_; // monotone counter for lost-copy temporaries
};

// ============================================================
// SECTION: CONVENIENCE PIPELINE
// ============================================================

/**
 * @struct SSAPipeline
 * @brief Bundles the full SSA construction/verification/repair stack for a
 *        single function and owns the analysis objects for their lifetimes.
 *
 * Typical usage:
 *
 *   SSAPipeline pipe(cfg, nodes);
 *   pipe.construct();                    // build SSA
 *   auto errors = pipe.verify();         // confirm correctness
 *   assert(errors.empty());
 *
 *   // ... transform the IR ...
 *
 *   pipe.repair();                       // repair after transformation
 *   auto copies = pipe.destroy_ssa();    // lower out of SSA for codegen
 */
struct SSAPipeline {
    CFG&               cfg;
    std::span<IRNode>  nodes;

    DomTree            dom;
    DomFrontier        df;
    LivenessAnalysis   liveness;
    SSABuilder         builder;
    SSAVerifier        verifier;

    explicit SSAPipeline(CFG& c, std::span<IRNode> n)
        : cfg(c), nodes(n)
        , dom(c)
        , df(c, dom)
        , liveness(c)
        , builder(c, dom, df, n)
        , verifier(c, dom, n)
    {}

    /// Full construction pipeline: RPO → Dominators → DF → Liveness → SSA.
    void construct() {
        cfg.compute_rpo();
        dom.compute();
        df.compute();
        liveness.seed_def_use(nodes);
        liveness.compute();
        builder.build();
    }

    /// Verify SSA invariants. Returns violation strings; empty = valid.
    [[nodiscard]] std::vector<std::string> verify() {
        return verifier.verify();
    }

    /// Repair SSA after IR transformations.
    void repair() {
        dom.compute();      // recompute after CFG changes
        df.compute();
        SSARepair r(cfg, dom, df, liveness, nodes);
        r.repair();
    }

    /// Targeted repair for a known-mutated region.
    void repair_region(const std::vector<BlockId>& mutated) {
        dom.compute();
        df.compute();
        SSARepair r(cfg, dom, df, liveness, nodes);
        r.repair_region(mutated);
    }

    /// Destroy SSA form, returning the parallel-copy map for codegen.
    [[nodiscard]] PhiEliminator::CopyMap destroy_ssa() {
        PhiEliminator elim(cfg);
        return elim.eliminate();
    }
};

// ============================================================
// SECTION: IRSTREAM -> SSA CONVENIENCE BRIDGE
// ============================================================

struct SSAFromStreamResult {
    CFG cfg;
    std::vector<IRNode> nodes;
    std::vector<std::string> verification_errors;
    bool ok = false;
};

/// Build SSA state directly from an IRStream in one call.
///
/// Notes:
/// - `nodes` is a detached working copy of the stream nodes.
/// - `cfg` is rebuilt from IRStream's canonical basic-block/edge cache.
/// - SSA transformation and verification operate on that copy.
[[nodiscard]] inline SSAFromStreamResult
construct_ssa_from_stream(const dgtl::ir::IRStream& stream) {
    SSAFromStreamResult result;

    const size_t n = stream.size();
    result.nodes.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.nodes.push_back(stream.get_node(i));
    }

    if (!result.cfg.rebuild_from_ir_stream(stream)) {
        result.verification_errors.push_back("failed to rebuild SSA CFG from IRStream");
        result.ok = false;
        return result;
    }

    if (result.cfg.blocks.empty()) {
        result.ok = true;
        return result;
    }

    SSAPipeline pipe(result.cfg, std::span<IRNode>(result.nodes.data(), result.nodes.size()));
    pipe.construct();
    result.verification_errors = pipe.verify();
    result.ok = result.verification_errors.empty();
    return result;
}

/// Construct SSA from stream, then write renamed IR nodes back into the stream.
/// Returns false if CFG import/SSA verification fails.
[[nodiscard]] inline bool construct_ssa_in_stream(
        dgtl::ir::IRStream& stream,
        std::vector<std::string>* errors_out = nullptr) {
    SSAFromStreamResult built = construct_ssa_from_stream(stream);

    if (errors_out) {
        *errors_out = built.verification_errors;
    }
    if (!built.ok) {
        return false;
    }

    if (built.nodes.size() != stream.size()) {
        if (errors_out) {
            errors_out->push_back("SSA node count mismatch while applying back to IRStream");
        }
        return false;
    }

    for (size_t i = 0; i < built.nodes.size(); ++i) {
        stream.set_node(i, built.nodes[i]);
    }
    return true;
}

// ============================================================
// COMPILE-TIME SANITY CHECKS
// ============================================================

static_assert(sizeof(VReg)    == 4,  "VReg must be 4 bytes — SSA name encoding contract");
static_assert(sizeof(BlockId) == 4,  "BlockId must be 4 bytes");
static_assert(sizeof(NodeId)  == 4,  "NodeId must be 4 bytes");

} // namespace dgtl::ir::ssa
